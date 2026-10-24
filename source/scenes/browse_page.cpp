// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/browse_page.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "core/bcstm.hpp"
#include "core/image.hpp"
#include "core/path.hpp"
#include "core/zip.hpp"
#include "domain/item_ids.hpp"
#include "domain/library.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "scenes/download.hpp"
#include "scenes/errors.hpp"
#include "scenes/frame.hpp"
#include "scenes/menu.hpp"
#include "scenes/preview_scene.hpp"
#include "services/themeplaza.hpp"
#include "services/themezer.hpp"
#include "sys/log.hpp"
#include "sys/sfx.hpp"
#include "ui/keyboard.hpp"
#include "ui/overlays.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace tz::scenes
{

using namespace gfx;
using catalog::Kind;
using catalog::Source;

namespace
{

constexpr float kSearchLineHeight = 22;
// the sub-tabs: the three kinds, then the QR code scanner
constexpr int kQrTab = 3;
// how near the end of the list the next page starts loading
constexpr int kPrefetchRows = 6;
// how long a next page that failed waits before it's tried again
constexpr uint64_t kMoreRetryMs = 3000;
// the room kept open over the list while a pull refreshes it, for the spinner
constexpr float kRefreshGap = 40;
// the spinner's radius in the row under the list while more loads
constexpr float kFooterSpinner = 8;
// icons asked for at a time: the one loading and the next, so that the icons lane never waits, and what's asked for
// next is what's on screen when one arrives
constexpr int kIconsInFlight = 2;
constexpr uint64_t kMaxIconSize = 256 * 1024;
constexpr uint64_t kMaxPreviewSize = 4 * 1024 * 1024;
constexpr uint64_t kMaxZipSize = 64 * 1024 * 1024;
constexpr uint8_t kDetailsTries = 3;

uint64_t keyOf(Source source, Kind kind, const std::string& id)
{
    uint64_t hash = 1469598103934665603ULL ^ (static_cast<uint64_t>(source) << 8 | static_cast<uint64_t>(kind));
    for (unsigned char c : id)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1;
}

const char* folderOf(Kind kind)
{
    switch (kind)
    {
    case Kind::Splashes:
        return library::kSplashesRoot;
    case Kind::Badges:
        return library::kBadgesRoot;
    case Kind::Themes:
        break;
    }
    return library::kThemesRoot;
}

App::Folder appFolderOf(Kind kind)
{
    switch (kind)
    {
    case Kind::Splashes:
        return App::Folder::Splashes;
    case Kind::Badges:
        return App::Folder::Badges;
    case Kind::Themes:
        break;
    }
    return App::Folder::Themes;
}

Accent accentOf(Kind kind)
{
    switch (kind)
    {
    case Kind::Splashes:
        return accent::kSplashes;
    case Kind::Badges:
        return accent::kBadges;
    case Kind::Themes:
        break;
    }
    return accent::kThemes;
}

Source otherSource(Source source)
{
    return source == Source::Themezer ? Source::ThemePlaza : Source::Themezer;
}

// An item's name, or its id until its details are read.
std::string nameOf(const catalog::Item& item)
{
    return item.name.empty() ? "#" + item.id : item.name;
}

// "12 badges", for a badge set whose site says how many, as the SD card's badge sets show it.
std::string badgeCountOf(const catalog::Item& item)
{
    return trf(item.badgeCount == 1 ? "{} badge" : "{} badges", std::to_string(item.badgeCount));
}

// A row's second line: who made it, after how many badges a badge set holds, as on the SD card.
std::string detailOf(const catalog::Item& item)
{
    if (item.badgeCount <= 0)
        return item.author;
    return item.author.empty() ? badgeCountOf(item) : badgeCountOf(item) + " \u00B7 " + item.author;
}

// Downloads a PNG on the network worker.
Expected<Image> downloadPng(net::Client& client, const std::string& url, uint64_t maxSize, int maxWidth,
                            int maxHeight, const CancelToken& token)
{
    net::Request request;
    request.url = url;
    request.accept = {"image/png"};
    request.maxSize = maxSize;
    request.cancel = &token;
    const net::Response response = TZ_TRY(client.get(request));
    return decodePng(response.body, maxWidth, maxHeight);
}

// Downloads one of ThemePlaza's items by its id, and asks what to do with it.
void downloadFromThemePlaza(const std::string& id)
{
    downloadAndAsk(themeplaza::itemOf(id).downloadUrl, trf("Downloading #{} from ThemePlaza", id), {},
                   "ThemePlaza " + id + ".zip", {});
}

// Looks up a quick ID on Themezer, then downloads what it is, and asks what to do with it.
void downloadFromThemezer(const std::string& quickId)
{
    App::get().scenes().push(ui::ProgressOverlay::start<Expected<themezer::Found>>(
        {trf("Looking up {}", quickId), Lane::Network, true, false, color::kPrimary},
        [quickId](ui::TaskProgress& task, const CancelToken& token) {
            task.setStatus(tr("Asking Themezer"));
            std::unique_ptr<net::Client> client = net::takeClient();
            Expected<themezer::Found> found = themezer::findQuickId(*client, quickId, token);
            net::giveBack(std::move(client));
            return found;
        },
        [quickId](Expected<themezer::Found> found) {
            if (!found)
            {
                if (found.error().code == Errc::NotFound)
                    showError({trf("Nothing on Themezer has the ID {}", quickId),
                               tr("Check the ID on the item's page on themezer.net. It's under its QR code.")});
                else if (found.error().code != Errc::Cancelled)
                    showError(downloadError(found.error()));
                return;
            }
            const std::string name = found->item.name.empty() ? quickId : found->item.name;
            downloadAndAsk(found->item.downloadUrl, trf("Downloading {}", name), found->item.filename,
                           found->item.filename, name);
        }));
}

const char* orderLabel(catalog::Order order)
{
    switch (order)
    {
    case catalog::Order::Oldest:
        return "Newest last";
    case catalog::Order::MostDownloaded:
        return "Most downloaded";
    case catalog::Order::LeastDownloaded:
        return "Least downloaded";
    case catalog::Order::MostTrending:
        return "Most trending";
    case catalog::Order::LeastTrending:
        return "Least trending";
    case catalog::Order::Newest:
        break;
    }
    return "Newest first";
}

} // namespace

BrowsePage::BrowsePage() : source_(App::get().config().browseSource)
{
    list_.setRowHeight(metric::kRowHeight);
}

std::vector<ui::Tab> BrowsePage::tabs() const
{
    return {
        {tr("Themes"), accent::kThemes},
        {tr("Splashes"), accent::kSplashes},
        {tr("Badges"), accent::kBadges},
        {{}, accent::kApp, Icon::Qr},
    };
}

void BrowsePage::selectTab(int tab)
{
    const bool wasScanning = scanningTab();
    tab_ = tab;
    // while hidden, onShow starts what the tab needs
    if (!shown_)
        return;
    if (scanningTab())
    {
        qr_.start();
        return;
    }
    if (wasScanning)
        qr_.stop();
    switchKind(static_cast<Kind>(tab_));
}

BrowsePage::~BrowsePage()
{
    token_.cancel();
}

ui::Tab BrowsePage::tab() const
{
    // the sites, browsed: a globe, as a browser has
    return {{}, accent::kApp, Icon::Globe};
}

Accent BrowsePage::accent() const
{
    return scanningTab() ? accent::kApp : accentOf(kind_);
}

void BrowsePage::onShow()
{
    shown_ = true;
    if (scanningTab())
        qr_.start();
    else
        switchKind(static_cast<Kind>(tab_));
}

void BrowsePage::onHide()
{
    shown_ = false;
    qr_.stop();
}

bool BrowsePage::scanningTab() const
{
    return tab_ == kQrTab;
}

void BrowsePage::reload()
{
    // what's loading for the old list is of no use any more
    token_.cancel();
    token_ = CancelToken();
    entries_.clear();
    iconsInFlight_ = 0;
    atlas_.clear();
    previews_.clear();
    list_.setCount(0);
    page_ = 0;
    pages_ = 1;
    fetching_ = false;
    moreError_.clear();
    moreRetryAt_ = 0;
    refreshing_ = false;
    list_.holdTop(0);
    state_ = State::Loading;
    fetchPage(1);
}

void BrowsePage::refresh()
{
    // the loads for the list there stop: its icons not there yet are asked for again if the page fails
    token_.cancel();
    token_ = CancelToken();
    iconsInFlight_ = 0;
    for (Entry& entry : entries_)
    {
        if (!entry.iconShown)
            entry.iconRequested = false;
    }
    fetching_ = false;
    moreError_.clear();
    moreRetryAt_ = 0;
    refreshing_ = true;
    list_.holdTop(kRefreshGap);
    fetchPage(1);
}

bool BrowsePage::hasMore() const
{
    return page_ < pages_;
}

void BrowsePage::fetchPage(int page)
{
    if (fetching_)
        return;
    fetching_ = true;
    const Source source = source_;
    const Kind kind = kind_;
    const std::string query = query_;
    const catalog::Order order = App::get().config().browseOrder;
    auto client = client_;
    jobs::run(
        Lane::Network, token_,
        [this, client, source, kind, query, order, page, token = token_](const CancelToken&) {
            auto result = std::make_shared<Expected<catalog::Page>>(
                catalog::fetchPage(*client, source, kind, page, query, order, token));
            jobs::post([this, result, token] {
                if (token.cancelled())
                    return;
                fetching_ = false;
                // a refresh: the old rows, to take what's still right from them, and the one selected
                std::unordered_map<uint64_t, Entry> before;
                uint64_t selectedKey = 0;
                const bool refreshed = refreshing_;
                if (refreshing_)
                {
                    // the gap over the list shuts; on a failure the list stays as it was
                    refreshing_ = false;
                    list_.holdTop(0);
                    if (!*result)
                    {
                        ui::toast(describe(result->error()).title, ui::ToastKind::Error);
                        checkOnSd(0); // the refresh stopped the one that was going
                        return;
                    }
                    if (const Entry* chosen = selected())
                        selectedKey = chosen->key;
                    for (Entry& entry : entries_)
                        before.emplace(entry.key, std::move(entry));
                    entries_.clear();
                }
                if (!*result)
                {
                    if (entries_.empty())
                    {
                        state_ = State::Failed;
                        error_ = describe(result->error()).title;
                    }
                    else
                    {
                        // said at the end of the list, and tried again in a while rather than every frame
                        moreError_ = describe(result->error()).title;
                        moreRetryAt_ = osGetTime() + kMoreRetryMs;
                    }
                    return;
                }
                moreError_.clear();
                page_ = (*result)->page;
                pages_ = (*result)->pages;
                const size_t from = entries_.size();
                for (catalog::Item& item : (*result)->items)
                {
                    Entry entry;
                    entry.key = keyOf(source_, kind_, item.id);
                    // a site that adds items while it's browsed can list one again on the next page
                    if (find(entry.key))
                        continue;
                    entry.item = std::move(item);
                    if (const auto was = before.find(entry.key); was != before.end())
                    {
                        // listed before the refresh: still on the SD card, and a site that lists only ids gave its
                        // name, author and description with its icon, which needn't be fetched again
                        entry.onSd = was->second.onSd;
                        if (!entry.item.detailsUrl.empty() && !was->second.item.name.empty())
                        {
                            entry.item.name = std::move(was->second.item.name);
                            entry.item.author = std::move(was->second.item.author);
                            entry.item.description = std::move(was->second.item.description);
                        }
                    }
                    // its icon there already (a refresh, the icons and previews of items still listed kept): not
                    // loaded again, unless its name and author come with it and they aren't there
                    if (atlas_.has(entry.key) && (entry.item.detailsUrl.empty() || !entry.item.name.empty()))
                        entry.iconRequested = entry.iconShown = true;
                    entries_.push_back(std::move(entry));
                }
                list_.setCount(static_cast<int>(entries_.size()));
                if (refreshed)
                {
                    // the one selected again, if it's among the rows the list shows at its top, where it stays
                    const auto again = std::find_if(entries_.begin(), entries_.end(),
                                                    [&](const Entry& entry) { return entry.key == selectedKey; });
                    const int index = static_cast<int>(again - entries_.begin());
                    list_.select(again != entries_.end() && index < list_.rowsPerPage() ? index : 0);
                }
                state_ = State::Ready;
                checkOnSd(from);
                requestIcons();
            });
        },
        10);
}

void BrowsePage::checkOnSd(size_t from)
{
    // which of these are on the SD card already, by their file names
    std::vector<std::pair<uint64_t, catalog::Item>> items;
    for (size_t i = from; i < entries_.size(); ++i)
        items.emplace_back(entries_[i].key, entries_[i].item);
    if (items.empty())
        return;
    const std::string folder = folderOf(kind_);
    jobs::async(
        Lane::Io,
        [items = std::move(items), folder](const CancelToken&) {
            std::vector<uint64_t> present;
            auto files = App::get().sd().list(folder);
            if (!files)
                return present;
            for (const auto& [key, item] : items)
            {
                const bool found = std::any_of(files->begin(), files->end(), [&](const DirEntry& file) {
                    return !file.isDirectory && catalog::isDownloadOf(item, file.name);
                });
                if (found)
                    present.push_back(key);
            }
            return present;
        },
        [this, token = token_](std::vector<uint64_t> present) {
            if (token.cancelled())
                return;
            for (uint64_t key : present)
                if (Entry* entry = find(key))
                    entry->onSd = true;
        });
}

void BrowsePage::requestIcons()
{
    // The rows on screen from the top, then a few below them, a couple at a time: the next is chosen when one arrives,
    // so a fast scroll loads the rows it stops at, not all those it passed.
    const auto [first, last] = list_.visibleRange();
    const int end = std::min<int>(last + 4, static_cast<int>(entries_.size()) - 1);
    for (int i = std::max(0, first); i <= end && iconsInFlight_ < kIconsInFlight; ++i)
    {
        Entry& entry = entries_[i];
        // the atlas keeps a hundred icons: one it dropped since is asked for again
        if (entry.iconShown && !atlas_.has(entry.key))
            entry.iconRequested = entry.iconShown = false;
        if (entry.iconRequested || entry.detailsFailures >= kDetailsTries)
            continue;
        const uint64_t key = entry.key;
        auto client = iconClient_;
        if (!entry.item.detailsUrl.empty())
        {
            // a site that lists only ids: the name, author and icon come together
            entry.iconRequested = true;
            ++iconsInFlight_;
            const catalog::Item item = entry.item;
            const int language = App::get().language();
            jobs::run(Lane::Icons, token_, [this, client, item, key, language, token = token_](const CancelToken&) {
                auto details = std::make_shared<Expected<catalog::Details>>(
                    catalog::fetchDetails(*client, item, language, token));
                if (token.cancelled())
                    return;
                if (!*details)
                    log::writeError(item.detailsUrl.c_str(), details->error());
                jobs::post([this, details, key, token] {
                    if (token.cancelled())
                        return;
                    --iconsInFlight_;
                    if (Entry* found = find(key))
                    {
                        if (!*details)
                        {
                            // a network hiccup: asked again while it's in view
                            found->iconRequested = false;
                            ++found->detailsFailures;
                        }
                        else
                        {
                            catalog::Details& got = **details;
                            found->item.name = std::move(got.name);
                            found->item.author = std::move(got.author);
                            found->item.description = std::move(got.description);
                            atlas_.put(key, got.icon.data());
                            // asked for by the list a refresh replaced, maybe: not asked for again
                            found->iconRequested = found->iconShown = true;
                        }
                    }
                    requestIcons();
                });
            });
            continue;
        }
        if (entry.item.iconUrl.empty())
            continue;
        entry.iconRequested = true;
        ++iconsInFlight_;
        const std::string url = entry.item.iconUrl;
        jobs::run(Lane::Icons, token_, [this, client, url, key, token = token_](const CancelToken&) {
            std::shared_ptr<std::array<uint16_t, 48 * 48>> icon;
            if (auto image = downloadPng(*client, url, kMaxIconSize, 48, 48, token))
            {
                icon = std::make_shared<std::array<uint16_t, 48 * 48>>();
                imageToIcon(resize(*image, 48, 48), icon->data(), 48);
            }
            if (token.cancelled())
                return;
            // a failed one stays a placeholder
            jobs::post([this, icon, key, token] {
                if (token.cancelled())
                    return;
                --iconsInFlight_;
                if (Entry* found = icon ? find(key) : nullptr)
                {
                    atlas_.put(key, icon->data());
                    found->iconRequested = found->iconShown = true;
                }
                requestIcons();
            });
        });
    }
}

BrowsePage::Entry* BrowsePage::selected()
{
    const int index = list_.selected();
    return index >= 0 && index < static_cast<int>(entries_.size()) ? &entries_[index] : nullptr;
}

BrowsePage::Entry* BrowsePage::find(uint64_t key)
{
    for (Entry& entry : entries_)
        if (entry.key == key)
            return &entry;
    return nullptr;
}

void BrowsePage::tick(float)
{
    qr_.tick();
    // here rather than in update(), so that it goes on, and retries, under a preview stepping through the list
    if (shown_ && !scanningTab() && state_ == State::Ready)
        fetchNearTheEnd();

    // the site was changed in the settings
    if (App::get().config().browseSource != source_)
        switchSource(App::get().config().browseSource);

    // what was downloaded meanwhile shows as on the SD card
    const App::Folder folder = appFolderOf(kind_);
    const uint32_t generation = App::get().folderGeneration(folder);
    if (generation != seenGenerations_[static_cast<size_t>(folder)] && state_ == State::Ready)
    {
        seenGenerations_[static_cast<size_t>(folder)] = generation;
        checkOnSd(0);
    }
}

void BrowsePage::update(ui::Input& input, float dt)
{
    if (scanningTab())
    {
        if (auto text = qr_.update())
            downloadScanned(*text);
        else if (qr_.idFieldTapped(input) || input.pressed(KEY_X))
            enterId();
        else if (!qr_.scanning() && input.pressed(KEY_A))
            qr_.start();
        return;
    }
    if (input.pressed(KEY_X))
    {
        openFilter();
        return;
    }
    if (!query_.empty() && input.pressed(KEY_B))
    {
        sfx::play(sfx::Sound::Back);
        query_.clear();
        reload();
        return;
    }
    if (state_ == State::Failed && input.pressed(KEY_A))
    {
        reload();
        return;
    }
    if (state_ != State::Ready)
        return;

    const float top = listTop(metric::kTabBarHeight);
    list_.setBounds(0, top, metric::kBottomWidth, metric::kHeight - top - metric::kHintBarHeight);
    // a row under the last one while there's more to load
    list_.setFooter(hasMore() ? metric::kRowHeight : 0);
    list_.setPullToRefresh(!refreshing_ && !entries_.empty());
    const auto event = list_.update(input, dt);
    if (event == ui::ListView::Event::Pulled)
        refresh();
    requestIcons();

    Entry* entry = selected();
    if (!entry)
        return;
    // the site's ThumbHash is behind it at once, even while the list scrolls (it takes a few milliseconds), while the
    // preview downloads
    if (!entry->thumbHash && !entry->item.thumbHash.empty())
        entry->thumbHash = Picture::upload(tile(preview::thumbHashPicture(entry->item.thumbHash), false));
    if (list_.settled() && !entry->item.previewUrl.empty())
    {
        const std::string url = entry->item.previewUrl;
        auto client = client_;
        previews_.want(
            entry->key,
            [client, url](const CancelToken& token) -> Expected<preview::Screens> {
                const Image picture = TZ_TRY(downloadPng(*client, url, kMaxPreviewSize, 1024, 1024, token));
                return preview::split(picture);
            },
            Lane::Network);
    }
    if (event == ui::ListView::Event::Activated)
        openActions();
    else if (input.pressed(KEY_Y))
        openPreview(*entry);
}

std::vector<ui::Hint> BrowsePage::hints() const
{
    std::vector<ui::Hint> hints;
    if (scanningTab())
    {
        // X is on the Quick ID field
        hints.push_back({KEY_A, tr("Scan"), !qr_.scanning()});
        return hints;
    }
    // the same keys while the list loads, A disabled until there's something to get
    if (state_ == State::Failed)
        hints.push_back({KEY_A, tr("Try again")});
    else
        hints.push_back({KEY_A, tr("Get"), state_ == State::Ready && !entries_.empty()}); // Y: the A menu's Preview
    hints.push_back({KEY_X, tr("Filter")});
    if (!query_.empty())
        hints.push_back({KEY_B, tr("Clear")});
    return hints;
}

float BrowsePage::listTop(float top) const
{
    // under the search when there is one
    return top + (query_.empty() ? 0 : kSearchLineHeight);
}

std::optional<std::shared_ptr<Picture>> BrowsePage::backdrop() const
{
    // the scanner has no item, so nothing of the list's behind it
    if (scanningTab())
        return nullptr;
    auto* self = const_cast<BrowsePage*>(this);
    const Entry* entry = self->selected();
    if (!entry)
        return nullptr;
    if (entry->thumbHash)
        return entry->thumbHash;
    if (entry->item.previewUrl.empty())
        return nullptr;
    const PreviewPictures* pictures = self->previews_.get(entry->key);
    if (!pictures)
        return std::nullopt;
    return pictures->blur;
}

float BrowsePage::topCover() const
{
    return scanningTab() && qr_.showsPicture() ? 1 : 0;
}

void BrowsePage::drawTop()
{
    if (scanningTab())
    {
        qr_.drawTop();
        return;
    }
    const Entry* entry = selected();
    if (state_ != State::Ready || !entry)
        return;
    const Layer layer = list_.entrance().layer(8, true);
    drawDetails(*entry);
}

void BrowsePage::drawDetails(const Entry& entry)
{
    // the item's icon, name, author and description, its preview blurred behind them (see backdrop())
    const TextShadow shadow;
    constexpr float kLeft = details::kLeft;
    constexpr float kIcon = IconAtlas::kIconSize;
    constexpr float top = details::kTop;
    details::origin(Icon::Globe, catalog::sourceName(source_));
    if (!atlas_.draw(entry.key, kLeft, top, kIcon))
        roundedRect(kLeft, top, kIcon, kIcon, metric::kRadius, color::kSurfaceRaised);
    const float textX = kLeft + kIcon + 14;
    const float width = metric::kTopWidth - textX - kLeft;
    const float middle = top + kIcon / 2;
    drawText(nameOf(entry.item), textX, textTop(middle - 10, metric::kTextTitle), metric::kTextTitle, color::kText,
             Align::Left, width);
    drawText(entry.item.author, textX, textTop(middle + 13, metric::kTextNormal), metric::kTextNormal,
             color::kTextDim, Align::Left, width);
    if (!entry.item.description.empty())
        drawWrapped(entry.item.description, kLeft, top + kIcon + details::kDescriptionGap,
                    metric::kTopWidth - 2 * kLeft, metric::kTextNormal, color::kTextDim, 4);

    // what it is, along the bottom, and what's going on with it at the right
    FactsLine facts;
    if (entry.item.badgeCount > 0)
        facts.left(Icon::Badge, badgeCountOf(entry.item), color::kTextDim);
    if (!entry.item.audioUrl.empty())
        facts.left(Icon::Music, tr("Has music"), color::kTextDim);
    if (entry.onSd)
        facts.right(Icon::Check, tr("Downloaded"), accent().light);
}

void BrowsePage::drawBottom(float top, float bottom)
{
    if (scanningTab())
    {
        qr_.drawBottom(top, bottom);
        return;
    }

    // what's searched for, above the results
    if (!query_.empty())
    {
        const float middle = top + kSearchLineHeight / 2;
        icon(Icon::Search, 14, std::round(middle - metric::kIcon / 2), metric::kIcon, color::kTextDim);
        drawText(query_, 14 + metric::kIcon + 6, textTop(middle, metric::kTextSmall), metric::kTextSmall, color::kText,
                 Align::Left, metric::kBottomWidth - 48);
    }

    const float listTop = this->listTop(top);
    const std::string site = catalog::sourceName(source_);
    if (state_ == State::Loading || state_ == State::Idle)
    {
        spinner(metric::kBottomWidth / 2, (listTop + bottom) / 2 - 8, 14, accent().light);
        drawText(trf("Loading {}", site), metric::kBottomWidth / 2,
                 textTop((listTop + bottom) / 2 + 22, metric::kTextSmall), metric::kTextSmall, color::kTextDim,
                 Align::Center);
        return;
    }
    if (state_ == State::Failed)
    {
        drawEmptyState(Icon::WifiOff, color::kTextDim, error_,
                       trf("Press A to try again, or SELECT to browse {}.", catalog::sourceName(otherSource(source_))),
                       listTop, bottom);
        return;
    }
    if (entries_.empty())
    {
        drawEmptyState(Icon::SearchEmpty, accent().light,
                       query_.empty() ? tr("Nothing here yet") : trf("Nothing found for \"{}\"", query_),
                       query_.empty() ? tr("Try another kind.")
                                      : trf("Try other words, another kind, or {} (SELECT).",
                                            catalog::sourceName(otherSource(source_))),
                       listTop, bottom);
        return;
    }
    list_.draw([this](int index, float x, float y, float w, float h, bool) {
        const Entry& entry = entries_[index];
        if (!atlas_.draw(entry.key, x + row::kIconX, row::iconTop(y, h), row::kIconSize))
            row::placeholder(x, y, h, kind_ == Kind::Themes ? Icon::Theme
                                                           : (kind_ == Kind::Splashes ? Icon::Splash : Icon::Badge));
        float right = x + w - row::kRight;
        if (entry.onSd)
            right = row::status(Icon::Check, tr("Downloaded"), accent().light, right, y, h);
        if (!entry.item.audioUrl.empty())
            right = row::mark(Icon::Music, color::kTextFaint, right, y, h);
        row::lines(nameOf(entry.item), detailOf(entry.item), x, y, h, right);
    }, [this](float x, float y, float w, float h) {
        // the next page: loading, or what went wrong until it's tried again
        const float middle = y + h / 2;
        const bool failed = !fetching_ && !moreError_.empty();
        const std::string text = failed ? moreError_ : tr("Loading more");
        const float width = 2 * kFooterSpinner + 8 + textWidth(text, metric::kTextSmall);
        const float left = x + (w - width) / 2;
        if (failed)
            icon(Icon::WifiOff, left, std::round(middle - kFooterSpinner), 2 * kFooterSpinner, color::kTextDim);
        else
            spinner(left + kFooterSpinner, middle, kFooterSpinner, accent().light);
        drawText(text, left + 2 * kFooterSpinner + 8, textTop(middle, metric::kTextSmall), metric::kTextSmall,
                 color::kTextDim);
    }, [this](float x, float y, float w, float h, bool down) { row::highlight(x, y, w, h, down, accent().solid); });

    // in the gap a pull opens over the list: the spinner's dots, coming in as it's pulled, in the accent once it's far
    // enough, turning while it refreshes
    const float pull = list_.pull();
    if (pull > 0)
    {
        const bool far = refreshing_ || pull >= ui::ListView::kPullDistance;
        pushClip(0, listTop, metric::kBottomWidth, pull);
        spinner(metric::kBottomWidth / 2, listTop + std::min(pull, kRefreshGap) / 2, 9,
                far ? accent().light : color::kTextDim,
                refreshing_ ? 1.0f : std::min(0.999f, pull / ui::ListView::kPullDistance));
        popClip();
    }
}

void BrowsePage::downloadScanned(const std::string& text)
{
    if (const auto id = ids::themePlazaIdInLink(text))
    {
        downloadFromThemePlaza(*id);
        return;
    }
    // a link's scheme counts in capitals too, which a QR code holds in fewer modules
    const auto hasScheme = [&](std::string_view scheme) {
        return text.size() >= scheme.size() &&
               path::equalsIgnoreCase(std::string_view(text).substr(0, scheme.size()), scheme);
    };
    const bool http = hasScheme("http://");
    if (!http && !hasScheme("https://"))
    {
        // a code with just an ID in it
        if (const auto quickId = ids::themezerQuickId(text))
        {
            downloadFromThemezer(*quickId);
            return;
        }
        showError({tr("That QR code isn't a download"), tr("It doesn't hold a link.")});
        return;
    }
    // Themezer's links are https; an old http one is asked for securely
    const std::string url = http ? "https://" + text.substr(7) : text;
    downloadAndAsk(url, tr("Downloading from the QR code"), {}, "download.zip", {});
}

void BrowsePage::enterId()
{
    const auto text = ui::keyboard(tr("An ID, like T1ED69 (Themezer) or 152151 (ThemePlaza)"), {}, 24, tr("Cancel"),
                                   tr("Download"));
    if (!text)
        return;
    if (const auto quickId = ids::themezerQuickId(*text))
        downloadFromThemezer(*quickId);
    else if (const auto id = ids::themePlazaId(*text))
        downloadFromThemePlaza(*id);
    else if (const auto linked = ids::themePlazaIdInLink(*text))
        downloadFromThemePlaza(*linked);
    else
        showError({trf("\"{}\" isn't an ID", *text),
                   tr("Themezer's IDs are a letter or two and a number, like T1ED69, and ThemePlaza's a number, like "
                      "152151. They're on each item's page, under its QR code.")});
}

void BrowsePage::switchKind(Kind kind)
{
    // the same kind again loads too when the site changed meanwhile, on the scanner's tab
    if (kind == kind_ && state_ != State::Idle)
        return;
    kind_ = kind;
    reload();
}

void BrowsePage::switchSource(Source source)
{
    if (source == source_)
        return;
    source_ = source;
    if (App::get().config().browseSource != source)
    {
        App::get().config().browseSource = source;
        App::get().saveConfig();
    }
    ui::toast(trf("Browsing {}", catalog::sourceName(source)));
    if (!scanningTab())
    {
        reload();
    }
    else
    {
        // loads when a kind is chosen again; what's loading for the old site stops
        token_.cancel();
        token_ = CancelToken();
        fetching_ = false;
        refreshing_ = false;
        list_.holdTop(0);
        state_ = State::Idle;
    }
}

void BrowsePage::search()
{
    const auto text =
        ui::keyboard(trf("Search {}", catalog::sourceName(source_)), query_, 64, tr("Cancel"), tr("Search"));
    if (!text || *text == query_)
        return;
    query_ = *text;
    reload();
}

void BrowsePage::openActions()
{
    Entry* entry = selected();
    if (!entry)
        return;
    const uint64_t key = entry->key;
    std::vector<ui::Action> actions;
    if (kind_ != Kind::Badges)
        actions.push_back({Icon::Install, tr("Download and install"), {}, true, false, [this, key] {
                               if (Entry* e = find(key))
                                   download(*e, true);
                           }});
    actions.push_back({Icon::Download, entry->onSd ? tr("Download again") : tr("Download"),
                       trf("Into {}", folderOf(kind_)), true, false, [this, key] {
                           if (Entry* e = find(key))
                               download(*e, false);
                       }});
    actions.push_back({Icon::Eye, tr("Preview"), entry->item.audioUrl.empty() ? std::string() : tr("With its music"),
                       true, false, [this, key] {
                           if (Entry* e = find(key))
                               openPreview(*e);
                       }});
    actions.back().key = KEY_Y;
    App::get().scenes().emplace<ui::ActionSheet>(nameOf(entry->item), std::move(actions), accent().light);
}

void BrowsePage::download(Entry& entry, bool install)
{
    const std::string name = nameOf(entry.item);
    const uint64_t key = entry.key;
    const std::string fallback = std::string(catalog::sourceName(source_)) + " " + entry.item.id + ".zip";
    downloadToSd(entry.item.downloadUrl, trf("Downloading {}", name), entry.item.filename, fallback,
                 [this, name, key, install, token = token_](Saved saved) {
                     if (!token.cancelled())
                         if (Entry* e = find(key))
                             e->onSd = true;
                     const std::string file(path::filename(saved.path));
                     const library::Source source{saved.path, file, true, saved.size};
                     if (install && saved.kind == ContentKind::Theme)
                         installTheme(source, name, ThemeMode::Full, [](bool, library::FileId) {});
                     else if (install && saved.kind == ContentKind::Splash)
                         installSplash(source, name, splashes::Screens::Both, [](bool) {});
                     else if (saved.kind == ContentKind::Badges)
                         badgesSaved(saved, name);
                     else
                         ui::toast(trf("{} is saved in {}", name, std::string(path::parent(saved.path))),
                                   ui::ToastKind::Success);
                 });
}

// The next page, before the end is reached.
void BrowsePage::fetchNearTheEnd()
{
    const int count = static_cast<int>(entries_.size());
    if (hasMore() && osGetTime() >= moreRetryAt_ &&
        (list_.selected() >= count - kPrefetchRows || list_.visibleRange().second >= count - 2))
        fetchPage(page_ + 1);
}

void BrowsePage::openPreview(Entry& entry)
{
    App::get().scenes().push(makePreviewScene(previewOf(entry)));
}

PreviewSource BrowsePage::previewOf(Entry& entry)
{
    PreviewSource preview;
    preview.title = nameOf(entry.item);
    preview.key = entry.key;
    preview.previews = &previews_;
    preview.lane = Lane::Network;
    preview.silencesMusic = kind_ == Kind::Themes;
    const std::string url = entry.item.previewUrl;
    auto client = client_;
    preview.loader = [client, url](const CancelToken& token) -> Expected<preview::Screens> {
        if (url.empty())
            return fail(Errc::NotFound, "no preview");
        const Image picture = TZ_TRY(downloadPng(*client, url, kMaxPreviewSize, 1024, 1024, token));
        return preview::split(picture);
    };
    if (!entry.item.audioUrl.empty())
    {
        const std::string audio = entry.item.audioUrl;
        // the music plays while it downloads
        preview.music = [audio]() -> Expected<std::unique_ptr<StreamReader>> {
            return net::stream(audio, bcstm::kMaxSize);
        };
    }
    else if (entry.item.musicInDownload)
    {
        // the bgm.bcstm in the zip, played as the zip downloads up to it
        const std::string zip = entry.item.downloadUrl;
        preview.music = [zip]() -> Expected<std::unique_ptr<StreamReader>> {
            return streamFromZipInOrder(net::streamForward(zip, kMaxZipSize), "bgm.bcstm");
        };
    }
    // the selected entry's neighbours in the list, which goes on loading as the preview gets near its end (see tick())
    preview.hasNeighbour = [this](int step) {
        const int index = list_.selected() + step;
        return index >= 0 && index < static_cast<int>(entries_.size());
    };
    preview.neighbour = [this](int step) {
        list_.select(list_.selected() + step);
        return previewOf(*selected());
    };
    return preview;
}

void BrowsePage::openFilter()
{
    std::vector<ui::Action> actions;
    actions.push_back({Icon::Search, trf("Search {}", catalog::sourceName(source_)),
                       query_.empty() ? std::string() : trf("Now: \"{}\"", query_), true, false, [this] { search(); }});
    actions.push_back({Icon::Sort, tr("Sort"),
                       source_ == Source::Themezer ? trf("Now: {}", tr(orderLabel(App::get().config().browseOrder)))
                                                   : std::string(tr("Only on Themezer")),
                       source_ == Source::Themezer, false, [this] { chooseOrder(); }});
    App::get().scenes().emplace<ui::ActionSheet>(tr("Filter"), std::move(actions), accent().light);
}

void BrowsePage::chooseOrder()
{
    std::vector<ui::Action> options;
    const catalog::Order current = App::get().config().browseOrder;
    for (catalog::Order order : {catalog::Order::Newest, catalog::Order::Oldest, catalog::Order::MostDownloaded,
                                 catalog::Order::LeastDownloaded, catalog::Order::MostTrending,
                                 catalog::Order::LeastTrending})
    {
        options.push_back({order == current ? Icon::SquareCheck : Icon::Square, tr(orderLabel(order)), {}, true, false,
                           [this, order] {
                               App::get().config().browseOrder = order;
                               App::get().saveConfig();
                               reload();
                           }});
    }
    openChoice(tr("Sort"), std::move(options));
}

std::vector<ui::Action> BrowsePage::menuActions()
{
    const Source other = otherSource(source_);
    return {
        {Icon::Globe, trf("Browse {} instead", catalog::sourceName(other)), {}, true, false,
         [this, other] { switchSource(other); }},
        {Icon::Refresh, tr("Load again"), {}, true, false, [this] { reload(); }},
    };
}

} // namespace tz::scenes
