#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "cli_options.h"
#include "colors.h"
#include "now_playing_info.h"

namespace npbar {

struct RenderContext {
    int width = 120;
    bool unicode = true;
    bool plain = false;
    Palette palette{};
};

// Persistent state for marquee-scrolling the "artist — title" segment
// when it doesn't fit. Owned by the watch loop; passed by pointer into
// RenderBar. The renderer resets `offsetCols` whenever `lastTitleKey`
// changes (= track changed) and sets `scrolling` to tell the watch loop
// to wake up at the marquee cadence instead of the slower refresh tick.
struct ScrollState {
    std::wstring lastTitleKey;
    std::wstring marqueeSource;
    size_t       offsetCols = 0;
    std::chrono::steady_clock::time_point lastStepAt{};
    bool         scrolling = false;
};

// Returns the empty-state line according to options.
// Returns std::nullopt if nothing should be printed at all
// (e.g., --empty blank in one-shot mode).
std::optional<std::wstring> RenderEmpty(const RenderContext& ctx, EmptyMode emptyMode);

// Renders a one-line bar for the given info, fit to ctx.width.
// Pass a non-null `scroll` to enable marquee-scrolling of the
// artist/title segment when it doesn't fit. With `scroll == nullptr`
// the renderer falls back to truncating the title with an ellipsis.
std::wstring RenderBar(const NowPlayingInfo& info, const RenderContext& ctx,
                       ScrollState* scroll = nullptr);

// Renders the simplified plain-text representation.
std::wstring RenderPlain(const NowPlayingInfo& info);

// Returns position estimated to "now" if the session is playing,
// otherwise the captured position. Clamped to duration.
std::optional<std::chrono::milliseconds> EstimatePosition(const NowPlayingInfo& info);

}  // namespace npbar
