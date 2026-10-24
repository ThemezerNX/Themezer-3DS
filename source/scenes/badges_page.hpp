// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "domain/badges.hpp"
#include "gfx/icon_atlas.hpp"
#include "scenes/page.hpp"
#include "scenes/badge_preview.hpp"
#include "scenes/previews.hpp"
#include "sys/jobs.hpp"
#include "ui/list_view.hpp"

#include <vector>

namespace tz::scenes
{

// The badge sets in /Badges. They are installed together, replacing the HOME Menu's badges; a set can be skipped (from
// the A menu, or its checkbox). X opens Filter: searching them and their order.
class BadgesPage final : public Page
{
public:
    BadgesPage();
    ~BadgesPage() override;

    ui::Tab tab() const override;
    gfx::Accent accent() const override { return gfx::accent::kBadges; }
    void onShow() override;
    void tick(float dt) override;
    void update(ui::Input& input, float dt) override;
    void drawTop() override;
    void drawBottom(float top, float bottom) override;
    std::vector<ui::Hint> hints() const override;
    std::optional<std::shared_ptr<gfx::Picture>> backdrop() const override;
    std::vector<ui::Action> menuActions() override;

private:
    struct Set
    {
        badges::SetSource source;
        uint64_t key = 0;
        bool included = true;
        bool summarized = false;
        badges::SetSummary summary;
        uint64_t modified = 0; // when its folder or zip last changed, for the orders by time; 0 when unknown
    };

    enum class State : uint8_t
    {
        NotLoaded,
        Loading,
        Ready,
        Failed,
    };

    void load();
    void readInstalledCount();
    void openActions();
    void install();
    void openPreview(const Set& set);
    BadgePreviewSource previewOf(const Set& set);
    void installSets(std::vector<badges::SetSource> chosen);
    void uninstall();
    void exportBadges();
    void saveInstalledOnce();
    void restore();
    void confirmDelete(Set& set);
    // Puts the sets in the chosen order, what the search finds first; the list shows only those.
    void resort(bool keepSelection);
    void openFilter();
    void search();
    void chooseSort();
    float listTop() const;
    Set* selected();
    int includedCount() const;
    // How many pictures, and who made it.
    std::string detailOf(const Set& set) const;

    State state_ = State::NotLoaded;
    std::string error_;
    std::vector<Set> sets_; // what the search finds first, shown_ of them; all of them without one
    int shown_ = 0;
    std::string query_;
    ui::ListView list_;
    gfx::IconAtlas atlas_;
    Previews previews_;
    CancelToken token_;
    int installedCount_ = -1; // on the HOME Menu; -1 until known
    uint32_t generation_ = 0;
};

} // namespace tz::scenes
