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

// A download for the main thread: saved, or still in memory. That's when a file of its name is on the SD card already,
// to be written over once the user says so (Themezer and ThemePlaza put an item's id in its file's name, so that's
// most likely an older version of the same item), or when the user is still to say what to do with it.
struct Fetched
{
    Saved saved;
    std::shared_ptr<MemoryReader> unsaved; // not on the SD card yet
    bool taken = false;                    // a file of its name is
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

// Downloads the zip, sees what it is and where it goes, and saves it there, unless hold(kind) says to keep it in
// memory for now, or a file of its name is there already (see askToReplace()). Shows the progress, and errors.
void fetch(std::string url, std::string title, std::string name, std::string fallbackName, bool (*hold)(ContentKind),
           std::function<void(Fetched)> done)
{
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<Fetched>>(
        {std::move(title), Lane::Network, true, false, gfx::color::kPrimary},
        [url, name, fallbackName, hold](ui::TaskProgress& task, const CancelToken& token) -> Expected<Fetched> {
            // the overlay's title says what's downloading; under it, how much of it is in
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
            fetched.saved.path = path::join(folder, std::string(path::stem(chosen)) + ".zip");
            fetched.saved.size = reader->size();
            fetched.taken = sd.exists(fetched.saved.path);
            if (fetched.taken || hold(fetched.saved.kind))
            {
                fetched.unsaved = std::move(reader);
                return fetched;
            }
            task.setStatus(tr("Saving it to the SD card"));
            TZ_CHECK(sd.createDirectories(folder));
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
            done(std::move(*fetched));
        }));
}

// Saves a download held in memory, asking first when a file of its name is there.
void save(const Fetched& fetched, std::function<void(Saved)> done)
{
    if (fetched.taken)
    {
        askToReplace(fetched.saved, fetched.unsaved, std::move(done));
        return;
    }
    const Saved saved = fetched.saved;
    const std::shared_ptr<MemoryReader> data = fetched.unsaved;
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<void>>(
        {tr("Saving it to the SD card"), Lane::Io, false, false, gfx::color::kPrimary},
        [saved, data](ui::TaskProgress&, const CancelToken&) -> Expected<void> {
            FileSystem& sd = App::get().sd();
            TZ_CHECK(sd.createDirectories(std::string(path::parent(saved.path))));
            return sd.writeFile(saved.path, data->bytes());
        },
        [saved, done](Expected<void> written) {
            if (!written)
            {
                showError(downloadError(written.error()));
                return;
            }
            finish(saved, done);
        }));
}

// Installs a saved theme or splash as a whole; after() once that's over, however it went.
void install(const Saved& saved, const std::string& name, std::function<void()> after)
{
    const library::Source source{saved.path, std::string(path::filename(saved.path)), true, saved.size};
    if (saved.kind == ContentKind::Theme)
        installTheme(source, name, ThemeMode::Full, [after](bool, library::FileId) {
            if (after)
                after();
        });
    else
        installSplash(source, name, splashes::Screens::Both, [after](bool) {
            if (after)
                after();
        });
}

// Installs a download without keeping it: from a file of the app's own, removed again once the install is over.
void installOnce(const Fetched& fetched, const std::string& name)
{
    Saved once = fetched.saved;
    once.path = kInstallOncePath;
    const std::shared_ptr<MemoryReader> data = fetched.unsaved;
    const auto remove = [] {
        jobs::run(Lane::Io, {}, [](const CancelToken&) { (void)App::get().sd().removeFile(kInstallOncePath); });
    };
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<void>>(
        {trf("Installing {}", name), Lane::Io, false, false, gfx::color::kPrimary},
        [data](ui::TaskProgress&, const CancelToken&) {
            return App::get().sd().writeFile(kInstallOncePath, data->bytes());
        },
        [once, name, remove](Expected<void> written) {
            if (!written)
            {
                // a write that failed can leave the file, full size
                remove();
                showError(downloadError(written.error()));
                return;
            }
            install(once, name, remove);
        }));
}

// A theme or a splash just downloaded: save it, install it without keeping it, or both.
void askWhatToDo(const Fetched& fetched, const std::string& name)
{
    const bool theme = fetched.saved.kind == ContentKind::Theme;
    const std::string folder(path::parent(fetched.saved.path));
    App::get().scenes().emplace<ui::Dialog>(
        theme ? gfx::Icon::Theme : gfx::Icon::Splash, theme ? gfx::accent::kThemes.light : gfx::accent::kSplashes.light,
        trf("{} is downloaded", name),
        tr("What do you want to do?"),
        std::vector<ui::DialogButton>{
            {tr("Cancel"), nullptr, false, KEY_B},
            {tr("Save"),
             [fetched, name, folder] {
                 save(fetched, [name, folder](Saved) {
                     ui::toast(trf("{} is saved in {}", name, folder), ui::ToastKind::Success);
                 });
             },
             false, KEY_Y},
            {tr("Install"), [fetched, name] { installOnce(fetched, name); }, false, KEY_X},
            {tr("Install and save"),
             [fetched, name] { save(fetched, [name](Saved saved) { install(saved, name, nullptr); }); }, false,
             KEY_A},
        });
    sfx::play(sfx::Sound::Done);
}

} // namespace

std::string downloadName(const Saved& saved)
{
    const std::string_view stem = path::stem(path::filename(saved.path));
    const auto download = library::parseDownloadName(stem);
    return download ? download->name : std::string(stem);
}

void downloadToSd(std::string url, std::string title, std::string name, std::string fallbackName,
                  std::function<void(Saved)> done)
{
    fetch(std::move(url), std::move(title), std::move(name), std::move(fallbackName),
          [](ContentKind) { return false; }, [done](Fetched fetched) {
              if (fetched.unsaved)
                  askToReplace(fetched.saved, std::move(fetched.unsaved), done);
              else
                  finish(fetched.saved, done);
          });
}

void downloadAndAsk(std::string url, std::string title, std::string name, std::string fallbackName,
                    std::string displayName)
{
    // badges are saved as they come: they're installed from the Badges tab, all the sets there together
    fetch(std::move(url), std::move(title), std::move(name), std::move(fallbackName),
          [](ContentKind kind) { return kind != ContentKind::Badges; },
          [displayName](Fetched fetched) {
              const std::string shown = displayName.empty() ? downloadName(fetched.saved) : displayName;
              if (fetched.saved.kind != ContentKind::Badges)
                  askWhatToDo(fetched, shown);
              else if (fetched.unsaved)
                  askToReplace(fetched.saved, std::move(fetched.unsaved),
                               [shown](Saved saved) { badgesSaved(saved, shown); });
              else
                  finish(fetched.saved, [shown](Saved saved) { badgesSaved(saved, shown); });
          });
}

void badgesSaved(const Saved& saved, const std::string& name)
{
    App::get().scenes().emplace<ui::Dialog>(gfx::Icon::Badge, gfx::accent::kBadges.light, trf("{} is saved", name),
                                            trf("{}\nInstall your badges from the Badges tab.", saved.path),
                                            std::vector<ui::DialogButton>{{tr("OK"), nullptr}});
    sfx::play(sfx::Sound::Done);
}

} // namespace tz::scenes
