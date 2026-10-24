// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#pragma once

#include "core/result.hpp"
#include "domain/library.hpp"
#include "domain/preview.hpp"
#include "gfx/texture.hpp"
#include "sys/jobs.hpp"

#include <algorithm>
#include <functional>
#include <list>
#include <memory>

namespace tz::scenes
{

// The pictures of an item's preview, ready to draw.
struct PreviewPictures
{
    std::shared_ptr<gfx::Picture> top;
    std::shared_ptr<gfx::Picture> bottom;
    std::shared_ptr<gfx::Picture> blur; // for the backdrop
    bool missing = false;               // the item has no preview to show
};

// The previews of the last few items looked at, loaded on a worker one at a time: asking for another item drops a
// load still waiting.
class Previews
{
public:
    // Reads and decodes the preview, on a worker thread. NotFound when there is none.
    using Loader = std::function<Expected<preview::Screens>(const CancelToken&)>;

    // Without a preview, the backdrop is a blur of icon, where there is one.
    void want(uint64_t key, Loader loader, Lane lane = Lane::Io, std::shared_ptr<const library::Icon> icon = nullptr);
    // nullptr until it has loaded.
    const PreviewPictures* get(uint64_t key);
    bool loading(uint64_t key) const { return loadingKey_ == key && job_.active(); }
    void clear();

private:
    static constexpr size_t kKept = 5;
    std::list<std::pair<uint64_t, PreviewPictures>> cache_; // the most recent first
    uint64_t loadingKey_ = 0;
    JobGuard job_;
};

} // namespace tz::scenes
