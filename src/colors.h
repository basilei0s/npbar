#pragma once

#include <string>
#include <string_view>

namespace npbar {

// ANSI SGR color escape sequences. When colors are disabled, these helpers
// return empty strings so callers can wrap segments unconditionally.
//
// Display-width computation must ignore ANSI escape sequences; see
// DisplayWidth() in text_util.h, which strips ESC [ ... <letter>.

struct Palette {
    bool enabled = false;

    // Segment colors (SGR escape sequences, applied as text foreground/attrs).
    std::wstring_view icon;       // ♫
    std::wstring_view source;     // TIDAL / Edge / ...
    std::wstring_view separator;  // │
    std::wstring_view artistDash; // " — "  joining artist and title
    std::wstring_view artist;
    std::wstring_view title;
    std::wstring_view album;
    std::wstring_view statusPlaying;
    std::wstring_view statusPaused;
    std::wstring_view statusOther;
    std::wstring_view timePos;    // current position
    std::wstring_view timeDur;    // total duration / separator "/"
    std::wstring_view barFilled;  // █
    std::wstring_view barEmpty;   // ░

    // Inter-segment reset. Always full SGR reset (\x1b[0m); the pane
    // background is set via the terminal's *default* bg (OSC 11), so SGR
    // reset returns to that default, preserving the pane fill.
    std::wstring_view reset;
    std::wstring_view resetFull;  // alias of reset for end-of-frame emits

    // Pane background sequences. Two-pronged:
    //   - SGR 48 sets cell bg (covers cells where text is drawn)
    //   - OSC 11 sets terminal default bg (covers WT padding pixels)
    // Both are needed: OSC 11 alone leaves cells transparent over a
    // background image; SGR alone leaves the WT pane padding visible.
    bool         backgroundEnabled = false;
    std::wstring backgroundSgrSet;    // e.g. "\x1b[48;2;30;30;46m"
    std::wstring backgroundOscSet;    // e.g. "\x1b]11;rgb:1e/1e/2e\x1b\\"
    std::wstring backgroundOscReset;  // e.g. "\x1b]111\x1b\\"
};

// Build a palette. `enabled` toggles all foreground colors.
// `backgroundSpec` is a user spec like "dark", "darker", "black", "#RRGGBB",
// or empty (= use default "dark"). Pass enableBackground=false to suppress
// the background fill regardless of spec.
Palette MakePalette(bool enabled, bool enableBackground, std::wstring_view backgroundSpec);

}  // namespace npbar
