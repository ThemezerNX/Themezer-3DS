// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/home.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "scenes/badges_page.hpp"
#include "scenes/browse_page.hpp"
#include "scenes/frame.hpp"
#include "scenes/library_page.hpp"
#include "scenes/menu.hpp"
#include "sys/sfx.hpp"
#include "ui/entrance.hpp"

#include <algorithm>
#include <cmath>

namespace tz::scenes
{

namespace
{

constexpr int kLocalTabs = 3; // themes, splashes and badges on the SD card

// Two modes, each with its tabs: what's on the SD card, and what's online (the same kinds, and the QR code scanner). The
// button at the start of the tab bar, or SELECT, switches them, staying on the same kind.
class HomeScene final : public ui::Scene
{
public:
    HomeScene()
    {
        local_.push_back(std::make_unique<LibraryPage>(library::Kind::Theme));
        local_.push_back(std::make_unique<LibraryPage>(library::Kind::Splash));
        local_.push_back(std::make_unique<BadgesPage>());
        for (const auto& page : local_)
            localTabs_.push_back(page->tab());

        // It opens on the SD card, on the kind last shown, online or not. lastTab: 0 to 2 are the SD card's tabs, the
        // ones after them online, in the same order, then the QR code scanner, which has no kind (themes, then).
        const int last = std::max(0, App::get().config().lastTab);
        const int kind = last < kLocalTabs ? last : last - kLocalTabs;
        localTab_ = kind < kLocalTabs ? kind : 0;
        onlineTab_ = localTab_;
        browse_.selectTab(onlineTab_);
        showTabs();
        page().onShow();
    }

    void tick(float dt) override
    {
        for (auto& page : local_)
            page->tick(dt);
        browse_.tick(dt);
        backdrop_.set(page().backdrop(), page().accent().solid);
        backdrop_.tick(dt);
    }

    void update(ui::Input& input, float dt) override
    {
        hints_ = page().hints();
        hints_.push_back({KEY_START, tr("Menu")});
        hints_.push_back({KEY_SELECT, online_ ? tr("SD") : tr("Online")});
        hintBar_.update(input, hints_);

        // the mode button first: the tab bar takes a tap left of its tabs for the tab before
        // a touch that starts and stays on it, or ends there having started there
        const bool fromModeButton = inModeButton(input.startX, input.startY) && inModeButton(input.x, input.y);
        const bool onModeButton = input.touching && !input.dragging && fromModeButton;
        const bool modePressed = input.pressed(KEY_SELECT) || (input.tap && fromModeButton);
        modePress_.update((input.held & KEY_SELECT) || onModeButton, modePressed);
        if (modePressed)
        {
            sfx::play(sfx::Sound::Tab);
            if (input.tap)
                input.consumeTouch();
            switchMode();
            return;
        }
        if (tabs_.update(input))
            switchTo(tabs_.selected(), tabs_.movedForward() ? 1 : -1);

        if (input.pressed(KEY_START))
        {
            std::vector<ui::Action> actions{{online_ ? gfx::Icon::SdCard : gfx::Icon::Globe,
                                             online_ ? tr("Show the SD card") : tr("Browse online"), {}, true, false,
                                             [this] { switchMode(); }, KEY_SELECT}};
            for (ui::Action& action : page().menuActions())
                actions.push_back(std::move(action));
            openMenu(std::move(actions));
            return;
        }
        page().update(input, dt);
    }

    void onResume() override { page().onResume(); }

    void drawTop() override
    {
        // dark enough under the item's details to read them on a light preview
        backdrop_.draw(0.55f);
        {
            const gfx::Layer layer = entrance_.layer(kSlide);
            page().drawTop();
        }
        if (const float cover = page().topCover(); cover < 1)
            drawStatusBar(1 - cover);
    }

    void drawBottom() override
    {
        using namespace gfx;
        backdrop_.draw(0.72f);
        drawModeButton();
        tabs_.draw(kModeButton, 0, metric::kBottomWidth - kModeButton, metric::kTabBarHeight, metric::kTextNormal);
        {
            const Layer layer = entrance_.layer(kSlide);
            page().drawBottom(metric::kTabBarHeight, metric::kHeight - metric::kHintBarHeight);
        }
        hintBar_.draw(hints_, 1.0f, page().accent().solid);
    }

private:
    // the round button at the start of the tab bar, as tall as it
    static constexpr float kModeButton = gfx::metric::kTabBarHeight;
    // how far a page comes in from, from the side of its tab
    static constexpr float kSlide = 24;

    Page& page() { return online_ ? static_cast<Page&>(browse_) : *local_[localTab_]; }

    static bool inModeButton(float x, float y)
    {
        return x >= 0 && x < kModeButton && y >= 0 && y < gfx::metric::kTabBarHeight;
    }

    // a round key, its ledge in the bar's height
    void drawModeButton()
    {
        using namespace gfx;
        const float size = metric::kTabBarHeight - 6 - ui::kKeyLip;
        const float top = ui::drawKey(3, 3, size, size, color::kSurfaceHigh, modePress_.down());
        const float at = std::round((size - metric::kIcon) / 2);
        icon(online_ ? Icon::Globe : Icon::SdCard, 3 + at, top + at, metric::kIcon,
             online_ ? accent::kApp.light : color::kText);
    }

    void showTabs()
    {
        tabs_.setTabs(online_ ? browse_.tabs() : localTabs_);
        tabs_.select(online_ ? onlineTab_ : localTab_);
    }

    // To the other mode, on the same kind; from the QR code scanner, back to the tab last shown on the SD card.
    void switchMode()
    {
        page().onHide();
        if (online_)
        {
            if (onlineTab_ < kLocalTabs)
                localTab_ = onlineTab_;
        }
        else
        {
            onlineTab_ = localTab_;
            browse_.selectTab(onlineTab_);
        }
        online_ = !online_;
        showTabs();
        page().onShow();
        entrance_.start(0);
        remember();
    }

    void switchTo(int index, int side)
    {
        entrance_.start(side);
        if (online_)
        {
            onlineTab_ = index;
            browse_.selectTab(index);
        }
        else if (index != localTab_)
        {
            page().onHide();
            localTab_ = index;
            page().onShow();
        }
        remember();
    }

    void remember() { App::get().config().lastTab = online_ ? kLocalTabs + onlineTab_ : localTab_; }

    std::vector<std::unique_ptr<Page>> local_;
    std::vector<ui::Tab> localTabs_;
    BrowsePage browse_;
    bool online_ = false;
    int localTab_ = 0;
    int onlineTab_ = 0;
    ui::TabBar tabs_;
    ui::HintBar hintBar_;
    std::vector<ui::Hint> hints_;
    Backdrop backdrop_;
    ui::Entrance entrance_; // of the page, as its tab is switched to
    ui::KeyPress modePress_;
};

} // namespace

std::unique_ptr<ui::Scene> makeHome()
{
    return std::make_unique<HomeScene>();
}

} // namespace tz::scenes
