// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/badges_page.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "core/path.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/errors.hpp"
#include "scenes/frame.hpp"
#include "sys/act.hpp"
#include "sys/log.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <span>

namespace tz::scenes
{

using namespace gfx;

namespace
{

constexpr int kPreviewColumns = 7;
constexpr int kPreviewRows = 3;
constexpr int kPreviewBadge = 48;
constexpr int kPreviewGap = 6;

uint64_t keyOf(const badges::SetSource& set)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : set.name + "|" + set.folderPath + "|" + set.zipPath)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1;
}

// A screen's picture: stone 900, a little lighter towards the top.
Image backdrop(int width)
{
    Image screen(width, 240);
    for (int y = 0; y < screen.height; ++y)
    {
        for (int x = 0; x < screen.width; ++x)
        {
            uint8_t* p = screen.at(x, y);
            const uint8_t shade = static_cast<uint8_t>(36 - y / 24);
            p[0] = shade;
            p[1] = static_cast<uint8_t>(shade - 3);
            p[2] = static_cast<uint8_t>(shade - 5);
            p[3] = 255;
        }
    }
    return screen;
}

// As many of pictures as fit in a grid of rows of columns on screen, centred, from y = top. Returns how many it took.
size_t placeBadges(Image& screen, std::span<const Image> pictures, int columns, int rows, int top)
{
    const int width = columns * kPreviewBadge + (columns - 1) * kPreviewGap;
    const int left = (screen.width - width) / 2;
    const size_t count = std::min(pictures.size(), static_cast<size_t>(columns * rows));
    for (size_t i = 0; i < count; ++i)
    {
        const Image badge = resize(pictures[i], kPreviewBadge, kPreviewBadge);
        const int ox = left + static_cast<int>(i % static_cast<size_t>(columns)) * (kPreviewBadge + kPreviewGap);
        const int oy = top + static_cast<int>(i / static_cast<size_t>(columns)) * (kPreviewBadge + kPreviewGap);
        for (int y = 0; y < kPreviewBadge; ++y)
        {
            for (int x = 0; x < kPreviewBadge; ++x)
            {
                const uint8_t* from = badge.at(x, y);
                uint8_t* to = screen.at(ox + x, oy + y);
                const int a = from[3];
                for (int c = 0; c < 3; ++c)
                    to[c] = static_cast<uint8_t>((from[c] * a + to[c] * (255 - a)) / 255);
            }
        }
    }
    return count;
}

// The first badges of a set in a grid on the top screen, under the status bar, for the list.
preview::Screens grid(const std::vector<Image>& pictures)
{
    preview::Screens screens;
    screens.top = backdrop(400);
    placeBadges(screens.top, pictures, kPreviewColumns, kPreviewRows, 30);
    return screens;
}

Expected<Bytes> defaultSetIcon()
{
    FILE* file = std::fopen("romfs:/badge-set.png", "rb");
    if (!file)
        return fail(Errc::NotFound, "romfs:/badge-set.png");
    Bytes data(64 * 1024);
    const size_t read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    data.resize(read);
    return data;
}

} // namespace

BadgesPage::BadgesPage()
{
    list_.setRowHeight(metric::kRowHeight);
}

BadgesPage::~BadgesPage()
{
    token_.cancel();
}

ui::Tab BadgesPage::tab() const
{
    return {tr("Badges"), accent::kBadges};
}

void BadgesPage::onShow()
{
    if (state_ == State::NotLoaded)
        load();
}

void BadgesPage::load()
{
    generation_ = App::get().folderGeneration(App::Folder::Badges);
    state_ = State::Loading;
    previews_.clear();
    atlas_.clear();
    const std::string looseName = tr("Other badges");
    jobs::async(
        Lane::Io,
        [looseName](const CancelToken&) {
            FileSystem& sd = App::get().sd();
            (void)sd.createDirectories(library::kBadgesRoot); // findSets says why when it's missing
            return badges::findSets(sd, library::kBadgesRoot, looseName);
        },
        [this, token = token_](Expected<std::vector<badges::SetSource>> found) {
            if (token.cancelled())
                return;
            if (!found)
            {
                state_ = State::Failed;
                error_ = describe(found.error()).message;
                return;
            }
            sets_.clear();
            for (badges::SetSource& source : *found)
            {
                Set set;
                set.key = keyOf(source);
                set.source = std::move(source);
                sets_.push_back(std::move(set));
            }
            list_.setCount(static_cast<int>(sets_.size()));
            list_.select(0, false);
            state_ = State::Ready;

            // how many pictures each set has, and its icon, one set after another
            for (const Set& set : sets_)
            {
                const badges::SetSource source = set.source;
                const uint64_t key = set.key;
                jobs::async(
                    Lane::Io,
                    [source](const CancelToken&) {
                        struct Result
                        {
                            Expected<badges::SetSummary> summary;
                            std::optional<std::array<uint16_t, 48 * 48>> icon;
                        };
                        auto result = std::make_shared<Result>();
                        FileSystem& sd = App::get().sd();
                        result->summary = badges::summarize(sd, source);
                        if (auto image = badges::setIcon(sd, source))
                        {
                            std::array<uint16_t, 48 * 48> icon{};
                            imageToIcon(resize(*image, 48, 48), icon.data(), 48);
                            result->icon = icon;
                        }
                        return result;
                    },
                    [this, key, token](auto result) {
                        if (token.cancelled())
                            return;
                        for (Set& set : sets_)
                        {
                            if (set.key != key)
                                continue;
                            if (result->summary)
                            {
                                set.summary = *result->summary;
                                set.summarized = true;
                            }
                            if (result->icon)
                                atlas_.put(key, result->icon->data());
                        }
                    });
            }
        },
        5);
    readInstalledCount();
}

void BadgesPage::readInstalledCount()
{
    // the badge storage opens here, on the main thread, where the app keeps it; the worker only reads it
    Error error;
    Archive* archive = App::get().badges(&error);
    if (!archive)
    {
        installedCount_ = -1;
        storageBusy_ = error.code == Errc::Busy;
        return;
    }
    jobs::async(
        Lane::Io, [archive](const CancelToken&) { return badges::installedCount(*archive); },
        [this, token = token_](int count) {
            if (!token.cancelled())
                installedCount_ = count;
        });
}

BadgesPage::Set* BadgesPage::selected()
{
    const int index = list_.selected();
    return index >= 0 && index < static_cast<int>(sets_.size()) ? &sets_[index] : nullptr;
}

int BadgesPage::includedCount() const
{
    return static_cast<int>(std::count_if(sets_.begin(), sets_.end(), [](const Set& s) { return s.included; }));
}

void BadgesPage::tick(float)
{
    if (state_ == State::Ready && App::get().folderGeneration(App::Folder::Badges) != generation_)
        load();
}

void BadgesPage::update(ui::Input& input, float dt)
{
    if (state_ != State::Ready)
        return;
    list_.setBounds(0, metric::kTabBarHeight, metric::kBottomWidth,
                    metric::kHeight - metric::kTabBarHeight - metric::kHintBarHeight);
    const auto event = list_.update(input, dt);
    Set* set = selected();
    if (!set)
        return;
    if (list_.settled())
    {
        const badges::SetSource source = set->source;
        previews_.want(set->key, [source](const CancelToken& token) -> Expected<preview::Screens> {
            auto pictures = TZ_TRY(badges::previewBadges(App::get().sd(), source, kPreviewColumns * kPreviewRows,
                                                         token));
            if (pictures.empty())
                return fail(Errc::NotFound, "no badges");
            return grid(pictures);
        });
    }
    if (event == ui::ListView::Event::Activated)
        openActions();
    else if (input.pressed(KEY_X))
        set->included = !set->included;
    else if (input.pressed(KEY_Y))
        openPreview(*set);
}

std::vector<ui::Hint> BadgesPage::hints() const
{
    if (state_ != State::Ready || sets_.empty())
        return {};
    auto* self = const_cast<BadgesPage*>(this);
    const Set* set = self->selected();
    return {
        {KEY_A, tr("More")},
        {KEY_Y, tr("Preview")},
        {KEY_X, set && set->included ? tr("Skip") : tr("Include")},
    };
}

std::shared_ptr<Picture> BadgesPage::backdrop() const
{
    auto* self = const_cast<BadgesPage*>(this);
    const Set* set = self->selected();
    if (!set)
        return nullptr;
    const PreviewPictures* pictures = self->previews_.get(set->key);
    return pictures ? pictures->blur : nullptr;
}

std::string BadgesPage::detailOf(const Set& set) const
{
    if (set.source.loose())
        return tr("The pictures directly in /Badges");
    std::string detail;
    if (set.summarized)
        detail = trf(set.summary.badges == 1 ? "{} badge" : "{} badges", std::to_string(set.summary.badges));
    if (!set.source.author.empty())
        detail += (detail.empty() ? "" : " \u00B7 ") + set.source.author;
    return detail;
}

void BadgesPage::drawTop()
{
    const TextShadow shadow;
    const Set* set = selected();
    if (state_ != State::Ready || !set)
    {
        // what the HOME Menu has, even without sets on the SD card
        if (installedCount_ >= 0 && state_ == State::Ready)
            drawText(trf("{} badges on the HOME Menu", std::to_string(installedCount_)), metric::kTopWidth / 2,
                     textTop(metric::kHeight - 22, metric::kTextSmall), metric::kTextSmall, color::kTextDim,
                     Align::Center);
        return;
    }
    // the set's icon, name and author, its badges blurred behind them (see backdrop()), as the other tabs show an item
    constexpr float kLeft = details::kLeft;
    constexpr float kIcon = IconAtlas::kIconSize;
    constexpr float top = details::kTop;
    const std::string description = set->source.loose() ? tr("The pictures directly in /Badges") : std::string();
    if (!atlas_.draw(set->key, kLeft, top, kIcon))
    {
        roundedRect(kLeft, top, kIcon, kIcon, metric::kRadius, color::kSurfaceRaised);
        icon(set->source.loose() ? Icon::Images : Icon::Badge, kLeft + (kIcon - metric::kIcon) / 2,
             top + (kIcon - metric::kIcon) / 2, metric::kIcon, color::kTextFaint);
    }
    const float textX = kLeft + kIcon + 14;
    const float width = metric::kTopWidth - textX - kLeft;
    const float middle = top + kIcon / 2;
    const std::string& author = set->source.author;
    drawText(set->source.name, textX, textTop(author.empty() ? middle : middle - 10, metric::kTextTitle),
             metric::kTextTitle, color::kText, Align::Left, width);
    if (!author.empty())
        drawText(author, textX, textTop(middle + 13, metric::kTextNormal), metric::kTextNormal, color::kTextDim,
                 Align::Left, width);
    drawWrapped(description, kLeft, top + kIcon + details::kDescriptionGap, metric::kTopWidth - 2 * kLeft,
                metric::kTextNormal, color::kTextDim, 4);

    // what it is, along the bottom, and what's going on with it at the right
    FactsLine facts;
    if (set->summarized)
        facts.left(Icon::Badge,
                   trf(set->summary.badges == 1 ? "{} badge" : "{} badges", std::to_string(set->summary.badges)),
                   color::kTextDim);
    if (installedCount_ >= 0)
        facts.left(Icon::Info, trf("The HOME Menu has {} now", std::to_string(installedCount_)), color::kTextDim);
    else if (storageBusy_)
        facts.left(Icon::Info, tr("The HOME Menu is using its badges"), color::kTextFaint);
    if (!set->included)
        facts.right(Icon::Square, tr("Left out"), color::kTextFaint);
}

void BadgesPage::drawBottom(float top, float bottom)
{
    if (state_ == State::Loading || state_ == State::NotLoaded)
    {
        spinner(metric::kBottomWidth / 2, (top + bottom) / 2, 14, accent().light);
        return;
    }
    if (state_ == State::Failed)
    {
        drawEmptyState(Icon::Error, color::kDanger, tr("The SD card can't be read"), error_, top, bottom);
        return;
    }
    if (sets_.empty())
    {
        drawEmptyState(Icon::Badge, accent().light, tr("No badge sets yet"),
                       tr("Find some on the Themezer tab, or put folders or zips of PNGs in /Badges."), top, bottom);
        return;
    }
    list_.draw([this](int index, float x, float y, float w, float h, bool isSelected) {
        const Set& set = sets_[index];
        if (isSelected)
            row::highlight(x, y, w, h, accent().solid);
        if (!atlas_.draw(set.key, x + row::kIconX, row::iconTop(y, h), row::kIconSize, set.included ? 1.0f : 0.35f))
            row::placeholder(x, y, h, set.source.loose() ? Icon::Images : Icon::Badge);
        // whether it goes in with the next install
        const float right = row::mark(set.included ? Icon::SquareCheck : Icon::Square,
                                      set.included ? accent().light : color::kTextFaint, x + w - row::kRight, y, h);
        row::lines(set.source.name, detailOf(set), x, y, h, right, !set.included);
    });
}

void BadgesPage::openActions()
{
    Set* set = selected();
    if (!set)
        return;
    const uint64_t key = set->key;
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Install, trf("Install {} badge sets", std::to_string(includedCount())), {},
                       includedCount() > 0, false, [this] { install(); }});
    actions.push_back({set->included ? Icon::Square : Icon::SquareCheck,
                       set->included ? tr("Leave this set out") : tr("Include this set"), {}, true, false,
                       [this, key] {
                           for (Set& s : sets_)
                               if (s.key == key)
                                   s.included = !s.included;
                       }});
    actions.push_back({Icon::Eye, tr("Preview"), {}, true, false, [this, key] {
                           for (const Set& s : sets_)
                               if (s.key == key)
                                   openPreview(s);
                       }});
    if (!set->source.loose())
        actions.push_back({Icon::Trash, tr("Delete from the SD card"), {}, true, true, [this, key] {
                               for (Set& s : sets_)
                                   if (s.key == key)
                                       confirmDelete(s);
                           }});
    App::get().scenes().emplace<ui::ActionSheet>(set->source.name, std::move(actions), accent().light);
}

void BadgesPage::openPreview(const Set& set)
{
    App::get().scenes().push(makeBadgePreview(previewOf(set)));
}

BadgePreviewSource BadgesPage::previewOf(const Set& set)
{
    BadgePreviewSource preview;
    preview.set = set.source;
    // the selected set's neighbours in the list
    preview.hasNeighbour = [this](int step) {
        const int index = list_.selected() + step;
        return index >= 0 && index < static_cast<int>(sets_.size());
    };
    preview.neighbour = [this](int step) {
        list_.select(list_.selected() + step);
        return previewOf(*selected());
    };
    return preview;
}

void BadgesPage::install()
{
    std::vector<badges::SetSource> chosen;
    for (const Set& set : sets_)
        if (set.included)
            chosen.push_back(set.source);
    if (chosen.empty())
    {
        ui::toast(tr("Include at least one set (X)"), ui::ToastKind::Warning);
        return;
    }
    // more than the HOME Menu holds: said before rather than after
    int count = 0;
    for (const Set& set : sets_)
        if (set.included && set.summarized)
            count += set.summary.badges;
    if (static_cast<int>(chosen.size()) > badges::kMaxSets || count > badges::kMaxBadges)
    {
        App::get().scenes().emplace<ui::Dialog>(
            Icon::Badge, accent().light, tr("Not all of these fit"),
            trf("The HOME Menu has room for 1000 badges in 100 sets, and these are {} sets with {} badges. The ones at "
                "the end of the list are left out.",
                std::to_string(chosen.size()), std::to_string(count)),
            std::vector<ui::DialogButton>{
                {tr("Cancel"), [] {}},
                {tr("Install what fits"), [this, chosen] { installSets(chosen); }},
            });
        return;
    }
    installSets(std::move(chosen));
}

void BadgesPage::installSets(std::vector<badges::SetSource> chosen)
{
    Error error;
    Archive* archive = App::get().badges(&error);
    if (!archive)
    {
        showError(badgeStorageError(error));
        return;
    }
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<badges::InstallResult>>(
        {tr("Installing badges"), Lane::Io, true, true, accent().solid},
        [chosen, archive](ui::TaskProgress& progress, const CancelToken& token) -> Expected<badges::InstallResult> {
            FileSystem& sd = App::get().sd();
            progress.setStatus(tr("Keeping a copy of your badges"));
            TZ_CHECK(badges::backupOnce(*archive, sd, kBadgeBackupFolder));
            // the badge files carry the console's Nintendo Network ID; without one, what's there stays
            const auto principal = act::principalId();
            const uint32_t id = principal ? *principal : badges::storedPrincipalId(*archive);
            const Bytes icon = TZ_TRY(defaultSetIcon());
            progress.setStatus(tr("Writing the badges"));
            return badges::install(sd, *archive, chosen, id, icon, [&](int done, int total) {
                progress.set(done, total);
                return !token.cancelled();
            });
        },
        [this, token = token_](Expected<badges::InstallResult> result) {
            if (token.cancelled())
                return;
            if (!result)
            {
                showError(installError(result.error(), tr("The badges")));
                return;
            }
            installedCount_ = result->badges;
            ui::toast(trf("{} badges in {} sets are installed", std::to_string(result->badges),
                          std::to_string(result->sets)),
                      ui::ToastKind::Success);
            if (result->full)
                ui::toast(tr("The HOME Menu has room for 1000 badges in 100 sets: the rest was left out"),
                          ui::ToastKind::Warning);
            if (result->skipped > 0)
                ui::toast(trf("{} pictures couldn't be used", std::to_string(result->skipped)),
                          ui::ToastKind::Warning);
            ui::toast(tr("Press HOME to see them"));
            App::get().badgesChanged();
        }));
}

void BadgesPage::uninstall()
{
    Error error;
    Archive* archive = App::get().badges(&error);
    if (!archive)
    {
        showError(badgeStorageError(error));
        return;
    }
    App::get().scenes().emplace<ui::Dialog>(
        Icon::Trash, color::kDanger, tr("Take all badges off the HOME Menu?"),
        tr("Your badge sets stay on the SD card, and the app keeps a copy of the badges you had before it first "
           "changed them."),
        std::vector<ui::DialogButton>{
            {tr("Keep them"), nullptr},
            {tr("Take them off"),
             [this, archive] {
                 jobs::async(
                     Lane::Io,
                     [archive](const CancelToken&) -> Expected<void> {
                         TZ_CHECK(badges::backupOnce(*archive, App::get().sd(), kBadgeBackupFolder));
                         return badges::uninstall(*archive);
                     },
                     [this, token = token_](Expected<void> result) {
                         if (token.cancelled())
                             return;
                         if (!result)
                         {
                             showError(describe(result.error()));
                             return;
                         }
                         installedCount_ = 0;
                         ui::toast(tr("The badges are off the HOME Menu"), ui::ToastKind::Success);
                         App::get().badgesChanged();
                     });
             },
             true},
        });
}

void BadgesPage::exportBadges()
{
    Error error;
    Archive* archive = App::get().badges(&error);
    if (!archive)
    {
        showError(badgeStorageError(error));
        return;
    }
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<std::pair<int, std::string>>>(
        {tr("Saving the HOME Menu's badges"), Lane::Io, true, false, accent().solid},
        [archive](ui::TaskProgress& progress, const CancelToken& token) -> Expected<std::pair<int, std::string>> {
            const time_t now = std::time(nullptr);
            char stamp[32];
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H.%M", gmtime(&now));
            const std::string folder = std::string(kBadgeExportFolder) + "/" + stamp;
            const int count = TZ_TRY(badges::exportBadges(*archive, App::get().sd(), folder, [&](int done, int total) {
                progress.set(done, total);
                return !token.cancelled();
            }));
            return std::make_pair(count, folder);
        },
        [](Expected<std::pair<int, std::string>> result) {
            if (!result)
            {
                showError(describe(result.error()));
                return;
            }
            ui::toast(trf("{} badges saved in {}", std::to_string(result->first), result->second),
                      ui::ToastKind::Success);
        }));
}

void BadgesPage::restore()
{
    Error error;
    Archive* archive = App::get().badges(&error);
    if (!archive)
    {
        showError(badgeStorageError(error));
        return;
    }
    App::get().scenes().emplace<ui::Dialog>(
        Icon::Restore, accent().light, tr("Bring back your old badges?"),
        tr("The badges the HOME Menu had before this app first changed them come back, in place of the ones it has "
           "now."),
        std::vector<ui::DialogButton>{
            {tr("Cancel"), nullptr},
            {tr("Bring back"),
             [this, archive] {
                 jobs::async(
                     Lane::Io,
                     [archive](const CancelToken&) {
                         return badges::restoreBackup(App::get().sd(), kBadgeBackupFolder, *archive);
                     },
                     [this, token = token_](Expected<void> result) {
                         if (token.cancelled())
                             return;
                         if (!result)
                         {
                             showError(describe(result.error()));
                             return;
                         }
                         readInstalledCount();
                         ui::toast(tr("Your old badges are back"), ui::ToastKind::Success);
                     });
             }},
        });
}

void BadgesPage::confirmDelete(Set& set)
{
    const badges::SetSource source = set.source;
    App::get().scenes().emplace<ui::Dialog>(
        Icon::Trash, color::kDanger, trf("Delete {}?", source.name),
        tr("The set is removed from your SD card. Badges on the HOME Menu stay."),
        std::vector<ui::DialogButton>{
            {tr("Keep"), nullptr},
            {tr("Delete"),
             [this, source] {
                 jobs::async(
                     Lane::Io,
                     [source](const CancelToken&) -> Expected<void> {
                         FileSystem& sd = App::get().sd();
                         if (!source.folderPath.empty())
                             TZ_CHECK(sd.removeDirectory(source.folderPath, true));
                         if (!source.zipPath.empty())
                             TZ_CHECK(sd.removeFile(source.zipPath));
                         return {};
                     },
                     [this, token = token_](Expected<void> result) {
                         if (token.cancelled())
                             return;
                         if (!result)
                         {
                             showError(describe(result.error()));
                             return;
                         }
                         ui::toast(tr("The set is deleted"), ui::ToastKind::Success);
                         load();
                     });
             },
             true},
        });
}

std::vector<ui::Action> BadgesPage::menuActions()
{
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Trash, tr("Take all badges off the HOME Menu"), {}, true, true, [this] { uninstall(); }});
    actions.push_back({Icon::Save, tr("Save the HOME Menu's badges to the SD card"),
                       trf("As PNGs, a folder per set, in {}", kBadgeExportFolder), true, false,
                       [this] { exportBadges(); }});
    const bool backup = badges::hasBackup(App::get().sd(), kBadgeBackupFolder);
    actions.push_back({Icon::Restore, tr("Bring back the badges you had before"),
                       backup ? tr("From before this app first changed them") : tr("There's no copy yet"), backup,
                       false, [this] { restore(); }});
    actions.push_back({Icon::Refresh, tr("Reload from the SD card"), {}, true, false, [this] { load(); }});
    return actions;
}

} // namespace tz::scenes
