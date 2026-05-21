#include <atomic>
#include <chrono>
#include <string>

#include <windows.h>

#include "bar_renderer.h"
#include "cli_options.h"
#include "console_util.h"
#include "media_session_reader.h"
#include "now_playing_info.h"
#include "text_util.h"

namespace npbar {

namespace {

std::atomic<bool> g_stopFlag{false};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_stopFlag.store(true, std::memory_order_relaxed);
            return TRUE;
    }
    return FALSE;
}

int ResolveWidth(const CliOptions& opt) {
    if (opt.widthMode == WidthMode::Fixed) return opt.fixedWidth;
    if (StdoutIsRedirected()) return 120;
    return GetConsoleWidth();
}

// Build the per-session render context (palette, unicode/plain flags,
// background spec). This is computed ONCE at startup and reused across
// every frame. Per-frame work is limited to refreshing `width`.
RenderContext MakeBaseRenderContext(const CliOptions& opt, bool isWatchMode) {
    RenderContext ctx;
    ctx.unicode = opt.unicode;
    ctx.plain   = opt.plain;

    bool colorsOn = !opt.noColor && !opt.plain && AlternateScreenSupported();
    bool bgOn     = isWatchMode && colorsOn && !opt.noBackground;
    ctx.palette   = MakePalette(colorsOn, bgOn, opt.backgroundSpec);

    ctx.width = 0;  // filled in per frame via RefreshWidth()
    return ctx;
}

void RefreshWidth(RenderContext& ctx, const CliOptions& opt) {
    int w = ResolveWidth(opt);
    // Leave a tiny bit of slack so the cursor doesn't wrap on full-width consoles.
    if (w > 1 && !StdoutIsRedirected()) w -= 1;
    if (w < 20) w = 20;
    ctx.width = w;
}

int RunOneShot(MediaSessionReader& reader, const CliOptions& opt) {
    RenderContext ctx = MakeBaseRenderContext(opt, /*isWatchMode=*/false);
    RefreshWidth(ctx, opt);

    auto info = reader.GetCurrent();
    if (!info) {
        auto line = RenderEmpty(ctx, opt.emptyMode);
        if (line) WriteUtf8Line(*line);
        return 0;
    }

    std::wstring line = RenderBar(*info, ctx);
    WriteUtf8Line(line);
    return 0;
}

int RunWatch(MediaSessionReader& reader, const CliOptions& opt) {
    // Build the render context once; we only mutate `width` per frame.
    RenderContext ctx = MakeBaseRenderContext(opt, /*isWatchMode=*/true);
    RefreshWidth(ctx, opt);

    AltScreenGuard altScreen;  // clears terminal on enter, restores on exit

    // Apply the pane background BEFORE drawing so the alt-screen clear is
    // filled with our color (and the WT padding pixels too via OSC 11).
    TerminalBackgroundGuard bgGuard(
        ctx.palette.backgroundOscSet,
        ctx.palette.backgroundOscReset);

    // After OSC 11 takes effect, brute-force paint every cell with our SGR
    // background. \x1b[2J alone isn't reliable in Windows Terminal when a
    // background image is set — the final row(s) leak through.
    if (bgGuard.Active()) {
        PaintEntirePane(ctx.palette.backgroundSgrSet);
    }

    CursorGuard cursor;
    RawInputGuard rawInput;  // enables q/Q/Esc-to-quit polling

    std::wstring lastLine;
    size_t lastWidth = 0;
    bool firstFrame = true;

    // Per-frame frame buffer, reused across iterations to avoid allocation
    // churn. Single WideToUtf8 + single WriteFile per redraw.
    std::wstring frame;
    frame.reserve(512);

    // Marquee state for long artist/title segments.
    ScrollState scroll;

    while (!g_stopFlag.load(std::memory_order_relaxed)) {
        RefreshWidth(ctx, opt);

        auto info = reader.GetCurrent();
        std::wstring line;
        if (info) {
            line = RenderBar(*info, ctx, &scroll);
        } else {
            auto empty = RenderEmpty(ctx, opt.emptyMode);
            line = empty.value_or(L"");
        }

        if (firstFrame || line != lastLine) {
            frame.clear();
            if (altScreen.Active()) {
                frame.append(L"\x1b[H");                 // cursor home
                if (ctx.palette.backgroundEnabled) {
                    frame.append(ctx.palette.backgroundSgrSet);  // sticky cell bg
                }
                frame.append(line);
                frame.append(L"\x1b[K");                 // erase rest of line
                WriteUtf8(frame);                        // ONE write per frame
            } else {
                // Fallback for redirected/legacy: single-line in-place redraw.
                RewriteCurrentLine(line, lastWidth);
            }
            lastLine = line;
            lastWidth = DisplayWidth(line);
            firstFrame = false;
        }

        // Wait for: a quit key, a cache update event (track/pause/seek),
        // or the refresh interval — whichever happens first. While the
        // marquee is scrolling we shorten the wait to the scroll cadence
        // so the title advances smoothly.
        void* upd = reader.GetUpdateEvent();
        DWORD baseInterval = static_cast<DWORD>(opt.interval.count());
        DWORD remaining = scroll.scrolling
                            ? std::min<DWORD>(baseInterval, 200)
                            : baseInterval;
        const DWORD slice = 200;
        while (remaining > 0 && !g_stopFlag.load(std::memory_order_relaxed)) {
            DWORD step = remaining > slice ? slice : remaining;
            switch (WaitForKeyOrUpdate(upd, step)) {
                case WaitOutcome::Quit:
                    g_stopFlag.store(true, std::memory_order_relaxed);
                    remaining = 0;
                    break;
                case WaitOutcome::Updated:
                    // Cache changed — skip the rest of the wait and re-render.
                    remaining = 0;
                    break;
                case WaitOutcome::Timeout:
                    remaining -= step;
                    break;
            }
        }
    }

    if (!altScreen.Active()) {
        ClearCurrentLineAndReturn();
        WriteUtf8(L"\r\n");
    }
    return 0;
}

}  // namespace

}  // namespace npbar

int wmain(int argc, wchar_t* argv[]) {
    using namespace npbar;

    EnableVirtualTerminalAndUtf8();

    auto parse = ParseCli(argc, argv);
    if (!parse.ok) {
        if (parse.error) WriteUtf8Line(*parse.error);
        WriteUtf8Line(L"Run 'npbar --help' for usage.");
        return 1;
    }

    if (parse.options.showHelp)    { WriteUtf8(HelpText()); return 0; }
    if (parse.options.showVersion) { WriteUtf8(VersionText()); return 0; }

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    MediaSessionReader reader;
    if (!reader.IsReady()) {
        WriteUtf8Line(L"npbar: failed to access Windows media sessions");
        if (!reader.LastError().empty()) {
            std::wstring detail(L"  ");
            detail.append(reader.LastError());
            WriteUtf8Line(detail);
        }
        return 2;
    }

    // --plain is intended for scripts/prompts; force one-shot regardless of mode.
    if (parse.options.once || parse.options.plain) {
        return RunOneShot(reader, parse.options);
    }
    return RunWatch(reader, parse.options);
}
