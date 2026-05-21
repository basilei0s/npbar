#include "colors.h"

#include <cwchar>
#include <string>

namespace npbar {

namespace {

struct Rgb { int r, g, b; };

bool ParseHexChannel(wchar_t a, wchar_t b, int& out) {
    auto v = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    int hi = v(a);
    int lo = v(b);
    if (hi < 0 || lo < 0) return false;
    out = (hi << 4) | lo;
    return true;
}

// Resolve a user spec to an RGB triplet. Returns false on unrecognized spec.
bool ParseBgSpec(std::wstring_view spec, Rgb& out) {
    if (spec.empty() || spec == L"dark")   { out = {0x1e, 0x1e, 0x2e}; return true; }
    if (spec == L"darker")                 { out = {0x11, 0x11, 0x18}; return true; }
    if (spec == L"black")                  { out = {0x00, 0x00, 0x00}; return true; }
    if (spec.size() == 7 && spec[0] == L'#') {
        int r = 0, g = 0, b = 0;
        if (ParseHexChannel(spec[1], spec[2], r) &&
            ParseHexChannel(spec[3], spec[4], g) &&
            ParseHexChannel(spec[5], spec[6], b)) {
            out = {r, g, b};
            return true;
        }
    }
    return false;
}

std::wstring BuildOscSet(Rgb c) {
    // OSC 11 ; rgb:RR/GG/BB ST   (ST = ESC \)
    wchar_t buf[64];
    swprintf(buf, 64, L"\x1b]11;rgb:%02x/%02x/%02x\x1b\\", c.r, c.g, c.b);
    return buf;
}

std::wstring BuildOscReset() {
    // OSC 111 ST — reset terminal default bg to user setting
    return L"\x1b]111\x1b\\";
}

std::wstring BuildSgrBg(Rgb c) {
    // SGR 48;2;R;G;B m — set cell background to RGB truecolor
    wchar_t buf[32];
    swprintf(buf, 32, L"\x1b[48;2;%d;%d;%dm", c.r, c.g, c.b);
    return buf;
}

}  // namespace

Palette MakePalette(bool enabled, bool enableBackground, std::wstring_view backgroundSpec) {
    Palette p;
    p.enabled = enabled;
    p.reset      = L"\x1b[0m";
    p.resetFull  = L"\x1b[0m";

    if (!enabled) return p;

    p.icon          = L"\x1b[38;5;213m";   // bright pink — ♫
    p.source        = L"\x1b[1;38;5;141m"; // bold violet
    p.separator     = L"\x1b[38;5;240m";   // mid-dark gray
    p.artistDash    = L"\x1b[38;5;244m";   // " — "
    p.artist        = L"\x1b[1;38;5;255m"; // bold near-white
    p.title         = L"\x1b[38;5;252m";   // soft white
    p.album         = L"\x1b[3;38;5;109m"; // italic muted cyan
    p.statusPlaying = L"\x1b[1;38;5;82m";  // bold green
    p.statusPaused  = L"\x1b[1;38;5;220m"; // bold yellow
    p.statusOther   = L"\x1b[38;5;244m";   // gray
    p.timePos       = L"\x1b[38;5;250m";   // light gray
    p.timeDur       = L"\x1b[38;5;240m";   // dim gray
    p.barFilled     = L"\x1b[38;5;97m";    // dusty purple (#875faf) — muted
                                            // shade in the source 141 family
    p.barEmpty      = L"\x1b[38;5;238m";   // very dim gray

    if (enableBackground) {
        Rgb rgb{};
        if (!ParseBgSpec(backgroundSpec, rgb)) {
            // Unknown spec: silently fall back to default 'dark'.
            ParseBgSpec(L"dark", rgb);
        }
        p.backgroundEnabled   = true;
        p.backgroundSgrSet    = BuildSgrBg(rgb);
        p.backgroundOscSet    = BuildOscSet(rgb);
        p.backgroundOscReset  = BuildOscReset();
        // Inter-segment reset must preserve cell bg: reset bold/italic/underline + fg only.
        p.reset = L"\x1b[22;23;24;39m";
    }
    return p;
}

}  // namespace npbar
