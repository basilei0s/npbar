#pragma once

#include <chrono>
#include <cstddef>
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

// Renders the empty-state line into `out`. Returns false when nothing should
// be printed at all (e.g., --empty blank in one-shot mode).
bool RenderEmpty(const RenderContext& ctx, EmptyMode emptyMode, std::wstring& out);

// Renders a one-line bar for the given info, fit to ctx.width.
// Pass a non-null `scroll` to enable marquee-scrolling of the
// artist/title segment when it doesn't fit. With `scroll == nullptr`
// the renderer falls back to truncating the title with an ellipsis.
void RenderBar(const NowPlayingInfo& info, const RenderContext& ctx, std::wstring& out,
               ScrollState* scroll = nullptr);

void RenderHelp(const RenderContext& ctx, std::wstring& out);

void RenderPlain(const NowPlayingInfo& info, std::wstring& out);

// Returns position estimated to "now" if the session is playing,
// otherwise the captured position. Clamped to duration.
std::optional<std::chrono::milliseconds> EstimatePosition(const NowPlayingInfo& info);

}  // namespace npbar
