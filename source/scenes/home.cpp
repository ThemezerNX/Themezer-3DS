// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "scenes/home.hpp"

#include "app/app.hpp"
#include "app/strings.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "scenes/badges_page.hpp"
#include "scenes/frame.hpp"
#include "scenes/library_page.hpp"
#include "scenes/menu.hpp"
#include "scenes/browse_page.hpp"

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

        // lastTab: 0 to 2 are the SD card's tabs, the ones after them online; 3 was the one online tab before
        const int onlineTabs = static_cast<int>(browse_.tabs().size());
        const int last = std::clamp(App::get().config().lastTab, 0, kLocalTabs + onlineTabs - 1);
        online_ = last >= kLocalTabs;
        if (online_)
            onlineTab_ = last - kLocalTabs;
        else
            localTab_ = last;
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
        hints_.push_back({KEY_SELECT, online_ ? tr("SD card") : tr("Online")});
        hintBar_.update(input, hints_);

        // the mode button first: the tab bar takes a tap left of its tabs for the tab before
        if (input.pressed(KEY_SELECT) || (input.tap && modeButtonHit(input)))
        {
            if (input.tap)
                input.consumeTouch();
            switchMode();
            return;
        }
        if (tabs_.update(input))
            switchTo(tabs_.selected());

        if (input.pressed(KEY_START))
        {
            openMenu(page().menuActions());
            return;
        }
        page().update(input, dt);
    }

    void onResume() override { page().onResume(); }

    void drawTop() override
    {
        // dark enough under the item's details to read them on a light preview
        backdrop_.draw(0.55f);
        page().drawTop();
        if (const float cover = page().topCover(); cover < 1)
            drawStatusBar(1 - cover);
    }

    void drawBottom() override
    {
        using namespace gfx;
        backdrop_.draw(0.72f);
        drawModeButton();
        tabs_.draw(kModeButton, 0, metric::kBottomWidth - kModeButton, metric::kTabBarHeight, metric::kTextNormal);
        page().drawBottom(metric::kTabBarHeight, metric::kHeight - metric::kHintBarHeight);
        hintBar_.draw(hints_);
    }

private:
    // the round button at the start of the tab bar, as tall as it
    static constexpr float kModeButton = gfx::metric::kTabBarHeight;

    Page& page() { return online_ ? static_cast<Page&>(browse_) : *local_[localTab_]; }

    bool modeButtonHit(const ui::Input& input) const
    {
        return input.x >= 0 && input.x < kModeButton && input.y >= 0 && input.y < gfx::metric::kTabBarHeight;
    }

    void drawModeButton()
    {
        using namespace gfx;
        const float size = metric::kTabBarHeight - 6;
        roundedRect(3, 3, size, size, size / 2, color::kSurface);
        const float at = std::round(3 + (size - metric::kIcon) / 2);
        icon(online_ ? Icon::Globe : Icon::SdCard, at, at, metric::kIcon,
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
        remember();
    }

    void switchTo(int index)
    {
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
};

} // namespace

std::unique_ptr<ui::Scene> makeHome()
{
    return std::make_unique<HomeScene>();
}

} // namespace tz::scenes
