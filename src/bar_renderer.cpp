#include "bar_renderer.h"

#include <array>
#include <chrono>
#include <string>

#include "progress_bar.h"
#include "text_util.h"

namespace npbar {

namespace {

constexpr int kDefaultBarWidth = 40;
constexpr int kMinBarWidth     = 8;

enum class Kind { Source, Title, ScrolledTitle, Album, Status, Time, Bar };

struct Segment {
    Kind kind{};
    std::wstring text;
    size_t width = 0;     // cached display width of `text`
    bool   active = false;
};

inline std::wstring_view SymIcon(bool unicode)     { return unicode ? L"♫"  : L"♪"; }
inline std::wstring_view SymSep(bool unicode)      { return unicode ? L" │ " : L" | "; }
inline std::wstring_view SymArrow(bool unicode)    { return unicode ? L"—"   : L"-"; }
inline std::wstring_view SymEllipsis(bool unicode) { return unicode ? L"…"   : L"..."; }

std::wstring BuildSourceSegment(const NowPlayingInfo& info, bool unicode) {
    std::wstring s(SymIcon(unicode));
    if (!info.source.empty()) {
        s.push_back(L' ');
        s.append(info.source);
    }
    return s;
}

std::wstring BuildTitleSegment(const NowPlayingInfo& info, bool unicode) {
    std::wstring_view artist = info.artist.empty() ? std::wstring_view(L"Unknown artist") : std::wstring_view(info.artist);
    std::wstring_view title  = info.title.empty()  ? std::wstring_view(L"Unknown title")  : std::wstring_view(info.title);
    std::wstring out;
    out.reserve(artist.size() + title.size() + 4);
    out.append(artist);
    out.push_back(L' ');
    out.append(SymArrow(unicode));
    out.push_back(L' ');
    out.append(title);
    return out;
}

std::wstring BuildTimeSegment(const NowPlayingInfo& info,
                              std::optional<std::chrono::milliseconds> position) {
    if (!info.duration) return {};
    std::wstring out;
    out.reserve(16);
    if (position) out.append(FormatDuration(*position));
    else          out.append(L"--:--");
    out.append(L" / ");
    out.append(FormatDuration(*info.duration));
    return out;
}

void SetSeg(Segment& s, Kind k, std::wstring text) {
    s.kind   = k;
    s.text   = std::move(text);
    s.width  = DisplayWidth(s.text);
    s.active = !s.text.empty();
}

void DropSeg(Segment& s) {
    s.text.clear();
    s.width  = 0;
    s.active = false;
}

size_t TotalWidth(const std::array<Segment, 6>& segs, size_t sepW) {
    size_t w = 0;
    int n = 0;
    for (auto& s : segs) {
        if (!s.active) continue;
        w += s.width;
        ++n;
    }
    if (n > 1) w += sepW * (n - 1);
    return w;
}

bool ShrinkTitleSegment(Segment& seg, size_t mustShrinkBy,
                        std::wstring_view arrow, std::wstring_view ellipsis) {
    if (mustShrinkBy == 0) return true;
    std::wstring needle;
    needle.reserve(arrow.size() + 2);
    needle.push_back(L' ');
    needle.append(arrow);
    needle.push_back(L' ');

    auto pos = seg.text.find(needle);
    if (pos == std::wstring::npos) {
        if (mustShrinkBy >= seg.width) return false;
        seg.text  = TruncateToWidth(seg.text, seg.width - mustShrinkBy, ellipsis);
        seg.width = DisplayWidth(seg.text);
        return true;
    }

    std::wstring artist = seg.text.substr(0, pos);
    std::wstring title  = seg.text.substr(pos + needle.size());
    size_t aw = DisplayWidth(artist);
    size_t tw = DisplayWidth(title);
    size_t toCut = mustShrinkBy;

    while (toCut > 0 && (aw > 1 || tw > 1)) {
        if (tw >= aw && tw > 1) {
            size_t newTw = (tw > toCut) ? (tw - toCut) : 1;
            title  = TruncateToWidth(title, newTw, ellipsis);
            size_t actual = DisplayWidth(title);
            size_t cut    = (tw > actual) ? (tw - actual) : 0;
            tw = actual;
            if (cut >= toCut) { toCut = 0; break; }
            toCut -= cut;
        } else if (aw > 1) {
            size_t newAw = (aw > toCut) ? (aw - toCut) : 1;
            artist = TruncateToWidth(artist, newAw, ellipsis);
            size_t actual = DisplayWidth(artist);
            size_t cut    = (aw > actual) ? (aw - actual) : 0;
            aw = actual;
            if (cut >= toCut) { toCut = 0; break; }
            toCut -= cut;
        } else {
            break;
        }
    }

    seg.text.clear();
    seg.text.reserve(artist.size() + needle.size() + title.size());
    seg.text.append(artist).append(needle).append(title);
    seg.width = DisplayWidth(seg.text);
    return toCut == 0;
}

void AppendColorized(std::wstring& out, const Segment& seg, const Palette& pal,
                     bool unicode, const NowPlayingInfo& info) {
    if (!pal.enabled) { out.append(seg.text); return; }

    const auto reset = pal.reset;
    auto wrap = [&](std::wstring_view color, std::wstring_view text) {
        if (color.empty()) { out.append(text); return; }
        out.append(color);
        out.append(text);
        out.append(reset);
    };

    switch (seg.kind) {
        case Kind::Source: {
            size_t sp = seg.text.find(L' ');
            if (sp == std::wstring::npos) { wrap(pal.icon, seg.text); return; }
            wrap(pal.icon, std::wstring_view(seg.text).substr(0, sp));
            out.push_back(L' ');
            wrap(pal.source, std::wstring_view(seg.text).substr(sp + 1));
            return;
        }
        case Kind::Title: {
            std::wstring_view arrow = SymArrow(unicode);
            std::wstring needle;
            needle.reserve(arrow.size() + 2);
            needle.push_back(L' ');
            needle.append(arrow);
            needle.push_back(L' ');
            auto p = seg.text.find(needle);
            if (p == std::wstring::npos) { wrap(pal.title, seg.text); return; }
            wrap(pal.artist, std::wstring_view(seg.text).substr(0, p));
            out.push_back(L' ');
            wrap(pal.artistDash, arrow);
            out.push_back(L' ');
            wrap(pal.title, std::wstring_view(seg.text).substr(p + needle.size()));
            return;
        }
        case Kind::ScrolledTitle:
            // Single colour for marquee — splitting on " — " would
            // mis-attribute portions when the window wraps around.
            wrap(pal.title, seg.text);
            return;
        case Kind::Album:
            wrap(pal.album, seg.text);
            return;
        case Kind::Status: {
            std::wstring_view color = pal.statusOther;
            if (info.status == L"playing")      color = pal.statusPlaying;
            else if (info.status == L"paused")  color = pal.statusPaused;
            wrap(color, seg.text);
            return;
        }
        case Kind::Time: {
            auto p = seg.text.find(L" / ");
            if (p == std::wstring::npos) { wrap(pal.timeDur, seg.text); return; }
            wrap(pal.timePos, std::wstring_view(seg.text).substr(0, p));
            wrap(pal.timeDur, L" / ");
            wrap(pal.timeDur, std::wstring_view(seg.text).substr(p + 3));
            return;
        }
        case Kind::Bar: {
            const wchar_t fillChar = unicode ? L'█' : L'#';
            size_t filledEnd = 0;
            while (filledEnd < seg.text.size() && seg.text[filledEnd] == fillChar) ++filledEnd;
            if (filledEnd > 0) wrap(pal.barFilled, std::wstring_view(seg.text).substr(0, filledEnd));
            if (filledEnd < seg.text.size()) wrap(pal.barEmpty, std::wstring_view(seg.text).substr(filledEnd));
            return;
        }
    }
}

}  // namespace

std::optional<std::chrono::milliseconds> EstimatePosition(const NowPlayingInfo& info) {
    if (!info.position) return std::nullopt;
    if (info.status != L"playing") return info.position;

    auto now   = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.capturedAt);
    auto est   = *info.position + delta;
    if (est.count() < 0) est = std::chrono::milliseconds{0};
    if (info.duration && est > *info.duration) est = *info.duration;
    return est;
}

std::optional<std::wstring> RenderEmpty(const RenderContext& ctx, EmptyMode emptyMode) {
    if (emptyMode == EmptyMode::Blank) {
        return std::nullopt;  // caller emits nothing at all
    }
    std::wstring_view icon = SymIcon(ctx.unicode);
    std::wstring_view text = L" nothing playing";
    std::wstring out;
    if (ctx.palette.enabled) {
        out.reserve(icon.size() + text.size() + 32);
        out.append(ctx.palette.icon); out.append(icon); out.append(ctx.palette.reset);
        out.append(ctx.palette.statusOther); out.append(text); out.append(ctx.palette.reset);
    } else {
        out.reserve(icon.size() + text.size());
        out.append(icon);
        out.append(text);
    }
    return out;
}

std::wstring RenderPlain(const NowPlayingInfo& info) {
    std::wstring_view artist = info.artist.empty() ? std::wstring_view(L"Unknown artist") : std::wstring_view(info.artist);
    std::wstring_view title  = info.title.empty()  ? std::wstring_view(L"Unknown title")  : std::wstring_view(info.title);
    std::wstring s;
    s.reserve(artist.size() + title.size() + 32);
    s.append(artist).append(L" - ").append(title);
    if (info.duration) {
        auto pos = EstimatePosition(info);
        s.append(L" [");
        if (pos) s.append(FormatDuration(*pos)); else s.append(L"--:--");
        s.append(L" / ").append(FormatDuration(*info.duration)).append(L"]");
    }
    return s;
}

std::wstring RenderBar(const NowPlayingInfo& info, const RenderContext& ctx,
                       ScrollState* scroll) {
    if (ctx.plain) return RenderPlain(info);

    const std::wstring_view sep      = SymSep(ctx.unicode);
    const size_t            sepW     = DisplayWidth(sep);
    const std::wstring_view ellipsis = SymEllipsis(ctx.unicode);
    const std::wstring_view arrow    = SymArrow(ctx.unicode);

    auto pos = EstimatePosition(info);

    std::array<Segment, 6> segs{};
    SetSeg(segs[0], Kind::Source, BuildSourceSegment(info, ctx.unicode));
    SetSeg(segs[1], Kind::Title,  BuildTitleSegment(info, ctx.unicode));
    SetSeg(segs[2], Kind::Album,  info.album);
    SetSeg(segs[3], Kind::Status, info.status);
    SetSeg(segs[4], Kind::Time,   BuildTimeSegment(info, pos));

    int barWidth = kDefaultBarWidth;
    SetSeg(segs[5], Kind::Bar,    RenderProgressBar(pos, info.duration, barWidth, ctx.unicode));

    // Remember the full title for scrolling before any width fitting.
    const std::wstring& fullTitle = segs[1].text;
    const size_t fullTitleW = segs[1].width;

    const size_t maxW = static_cast<size_t>(ctx.width > 0 ? ctx.width : 0);
    size_t total = TotalWidth(segs, sepW);

    auto dropIfOver = [&](size_t i) {
        if (total > maxW && segs[i].active) {
            DropSeg(segs[i]);
            total = TotalWidth(segs, sepW);
        }
    };

    // Drop expendable segments in priority order. Source is intentionally
    // NOT dropped here — when title doesn't fit we'd rather scroll the
    // title than lose the player name.
    dropIfOver(3);  // Status
    dropIfOver(2);  // Album

    // Shrink progress bar in one step, then drop it.
    if (total > maxW && segs[5].active && barWidth > kMinBarWidth) {
        size_t over = total - maxW;
        size_t shrinkable = static_cast<size_t>(barWidth - kMinBarWidth);
        size_t shrinkBy = over < shrinkable ? over : shrinkable;
        barWidth -= static_cast<int>(shrinkBy);
        SetSeg(segs[5], Kind::Bar, RenderProgressBar(pos, info.duration, barWidth, ctx.unicode));
        total = TotalWidth(segs, sepW);
    }
    if (total > maxW && segs[5].active) { DropSeg(segs[5]); total = TotalWidth(segs, sepW); }

    // If still over: try to scroll the title (marquee) instead of dropping
    // the source. The title segment becomes a fixed-width window into the
    // full string, advancing each step.
    bool scrollingNow = false;
    if (total > maxW && segs[1].active && scroll) {
        size_t over = total - maxW;
        if (over < segs[1].width) {
            const size_t windowW = segs[1].width - over;
            if (windowW >= 8) {  // refuse to scroll into useless slivers
                const std::wstring_view key = fullTitle;
                using clock = std::chrono::steady_clock;
                const auto now = clock::now();
                if (scroll->lastTitleKey != key) {
                    scroll->lastTitleKey.assign(key);
                    scroll->offsetCols = 0;
                    scroll->lastStepAt = now;
                }
                // Build the marquee source: title + separator + title (so
                // the window can wrap continuously between the end and the
                // start without a hard jump).
                static constexpr std::wstring_view kMarqueeSep = L"   ·   ";
                const size_t loopW = fullTitleW + DisplayWidth(kMarqueeSep);

                // Step the offset based on elapsed time (≈4 cols/sec).
                using ms = std::chrono::milliseconds;
                constexpr ms kStep{250};
                auto elapsed = std::chrono::duration_cast<ms>(now - scroll->lastStepAt);
                if (elapsed >= kStep) {
                    size_t steps = static_cast<size_t>(elapsed / kStep);
                    scroll->offsetCols = (scroll->offsetCols + steps) % loopW;
                    scroll->lastStepAt += kStep * steps;
                }

                scroll->marqueeSource.clear();
                scroll->marqueeSource.reserve(fullTitle.size() * 2 + kMarqueeSep.size());
                scroll->marqueeSource.append(fullTitle).append(kMarqueeSep).append(fullTitle);
                std::wstring window = DisplaySubstring(scroll->marqueeSource, scroll->offsetCols, windowW);
                SetSeg(segs[1], Kind::ScrolledTitle, std::move(window));
                segs[1].width = windowW;
                total = TotalWidth(segs, sepW);
                scrollingNow = true;
            }
        }
    }
    if (scroll) scroll->scrolling = scrollingNow;

    // Drop the source only as a last resort, after scrolling didn't save us.
    if (total > maxW && segs[0].active) {
        DropSeg(segs[0]);
        total = TotalWidth(segs, sepW);
    }

    // Final hard shrink: truncate the title with an ellipsis (no scroll).
    if (total > maxW && segs[1].active) {
        size_t over = total - maxW;
        ShrinkTitleSegment(segs[1], over, arrow, ellipsis);
        total = TotalWidth(segs, sepW);
    }
    if (total > maxW && segs[4].active && info.duration) {
        SetSeg(segs[4], Kind::Time, FormatDuration(*info.duration));
        total = TotalWidth(segs, sepW);
    }

    // Build output.
    std::wstring out;
    // Pre-reserve: plain text + per-segment color escapes (≈40 bytes each) + separators.
    out.reserve(total * 3 + 16 * 6);

    bool first = true;
    for (auto& s : segs) {
        if (!s.active) continue;
        if (!first) {
            if (ctx.palette.enabled) {
                out.append(ctx.palette.separator);
                out.append(sep);
                out.append(ctx.palette.reset);
            } else {
                out.append(sep);
            }
        }
        AppendColorized(out, s, ctx.palette, ctx.unicode, info);
        first = false;
    }

    // Final hard cut (rare).
    if (total > maxW) {
        out = TruncateToWidth(out, maxW, ellipsis);
    }
    return out;
}

}  // namespace npbar
