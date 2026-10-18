// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/download.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "core/path.hpp"
#include "core/zip.hpp"
#include "gfx/draw.hpp"
#include "scenes/errors.hpp"
#include "sys/net.hpp"
#include "sys/sfx.hpp"
#include "ui/overlays.hpp"

namespace tz::scenes
{

namespace
{

constexpr uint64_t kMaxDownload = 32 * 1024 * 1024;

const char* folderFor(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::Theme:
        return library::kThemesRoot;
    case ContentKind::Splash:
        return library::kSplashesRoot;
    case ContentKind::Badges:
        return library::kBadgesRoot;
    case ContentKind::Unknown:
        break;
    }
    return nullptr;
}

App::Folder appFolderFor(ContentKind kind)
{
    return kind == ContentKind::Theme ? App::Folder::Themes
                                      : (kind == ContentKind::Splash ? App::Folder::Splashes : App::Folder::Badges);
}

// A download for the main thread: saved, or, when a file of its name is on the SD card already, still to be written
// over it once the user says so. Themezer and ThemePlaza put an item's id in its file's name, so that's most likely an
// older version of the same item.
struct Fetched
{
    Saved saved;
    std::shared_ptr<MemoryReader> unsaved;
};

// Over the file of the same name. The new one is written beside it first, so that a write that fails (the SD card is
// full) leaves the old one.
Expected<void> replaceFile(FileSystem& sd, const std::string& path, ByteSpan data)
{
    const std::string part = path + ".part";
    TZ_CHECK(sd.writeFile(part, data));
    TZ_CHECK(sd.removeFile(path));
    return sd.rename(part, path);
}

void finish(const Saved& saved, const std::function<void(Saved)>& done)
{
    App::get().folderChanged(appFolderFor(saved.kind));
    done(saved);
}

void askToReplace(const Saved& saved, std::shared_ptr<MemoryReader> unsaved, std::function<void(Saved)> done)
{
    App::get().scenes().emplace<ui::Dialog>(
        gfx::Icon::Download, gfx::color::kPrimaryLight, tr("It's on the SD card already"),
        trf("{}\nReplace it with the download?", saved.path),
        std::vector<ui::DialogButton>{
            {tr("Cancel"), nullptr},
            {tr("Replace"),
             [saved, unsaved, done] {
                 App::get().scenes().push(ui::ProgressOverlay::start<Expected<void>>(
                     {tr("Saving it to the SD card"), Lane::Io, false, false, gfx::color::kPrimary},
                     [saved, unsaved](ui::TaskProgress&, const CancelToken&) {
                         return replaceFile(App::get().sd(), saved.path, unsaved->bytes());
                     },
                     [saved, done](Expected<void> replaced) {
                         if (!replaced)
                         {
                             showError(downloadError(replaced.error()));
                             return;
                         }
                         finish(saved, done);
                     }));
             }},
        });
}

} // namespace

void downloadToSd(std::string url, std::string title, std::string name, std::string fallbackName,
                  std::function<void(Saved)> done)
{
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<Fetched>>(
        {std::move(title), Lane::Network, true, false, gfx::color::kPrimary},
        [url, name, fallbackName](ui::TaskProgress& task, const CancelToken& token) -> Expected<Fetched> {
            task.setStatus(tr("Downloading"));
            net::Request request;
            request.url = url;
            request.accept = {"application/zip", "application/x-zip-compressed", "application/octet-stream"};
            request.maxSize = kMaxDownload;
            request.cancel = &token;
            request.progress = &task.download;
            std::unique_ptr<net::Client> client = net::takeClient();
            Expected<net::Response> got = client->get(request);
            net::giveBack(std::move(client));
            net::Response response = TZ_TRY(std::move(got));

            task.setStatus(tr("Saving it to the SD card"));
            auto reader = std::make_shared<MemoryReader>(std::move(response.body));
            const ZipArchive zip = TZ_TRY(ZipArchive::open(reader));
            std::vector<std::string> files;
            for (const ZipEntry& entry : zip.entries())
                files.push_back(entry.name);
            Fetched fetched;
            fetched.saved.kind = kindOf(files);
            const char* folder = folderFor(fetched.saved.kind);
            if (!folder)
                return fail(Errc::Unsupported, "neither a theme, a splash nor badges");

            std::string chosen = !name.empty() ? name : !response.filename.empty() ? response.filename : fallbackName;
            chosen = path::sanitizeFilename(chosen.empty() ? std::string("download") : chosen);
            FileSystem& sd = App::get().sd();
            TZ_CHECK(sd.createDirectories(folder));
            fetched.saved.path = path::join(folder, std::string(path::stem(chosen)) + ".zip");
            fetched.saved.size = reader->size();
            if (sd.exists(fetched.saved.path))
            {
                // asked first (see askToReplace())
                fetched.unsaved = std::move(reader);
                return fetched;
            }
            // from the download's own buffer: a copy would need its size again, and the app closes when it can't
            // have it
            TZ_CHECK(sd.writeFile(fetched.saved.path, reader->bytes()));
            return fetched;
        },
        [done](Expected<Fetched> fetched) {
            if (!fetched)
            {
                if (fetched.error().code == Errc::Unsupported)
                    showError({tr("That's not something to install"),
                               tr("The download isn't a theme, a splash screen or a badge set.")});
                else
                    showError(downloadError(fetched.error()));
                return;
            }
            if (fetched->unsaved)
                askToReplace(fetched->saved, std::move(fetched->unsaved), done);
            else
                finish(fetched->saved, done);
        }));
}

void offerInstall(const Saved& saved, const std::string& name)
{
    const std::string file(path::filename(saved.path));
    if (saved.kind == ContentKind::Badges)
    {
        App::get().scenes().emplace<ui::Dialog>(
            gfx::Icon::Badge, gfx::accent::kBadges.light, trf("{} is saved", name),
            trf("{}\nInstall your badges from the Badges tab.", saved.path),
            std::vector<ui::DialogButton>{{tr("OK"), nullptr}});
        sfx::play(sfx::Sound::Done);
        return;
    }
    const library::Source source{saved.path, file, true, saved.size};
    const bool theme = saved.kind == ContentKind::Theme;
    App::get().scenes().emplace<ui::Dialog>(
        theme ? gfx::Icon::Theme : gfx::Icon::Splash, theme ? gfx::accent::kThemes.light : gfx::accent::kSplashes.light,
        trf("{} is saved", name), trf("{}\nInstall it now?", saved.path),
        std::vector<ui::DialogButton>{
            {tr("Later"), nullptr},
            {tr("Install"),
             [source, name, theme] {
                 if (theme)
                     installTheme(source, name, ThemeMode::Full, [](bool, library::FileId) {});
                 else
                     installSplash(source, name, splashes::Screens::Both, [](bool) {});
             }},
        });
    sfx::play(sfx::Sound::Done);
}

} // namespace tz::scenes
