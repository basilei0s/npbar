#include "text_util.h"

#include <chrono>
#include <cstdint>
#include <cwctype>
#include <string>

#include <windows.h>

namespace npbar {

namespace {

// Range-based check for East Asian Wide / Fullwidth characters.
// Good enough for our use; not a full Unicode property table.
bool IsWideRange(uint32_t cp) {
    // Common CJK / wide ranges
    if (cp >= 0x1100 && cp <= 0x115F) return true;          // Hangul Jamo
    if (cp >= 0x2E80 && cp <= 0x303E) return true;          // CJK Radicals, Kangxi, etc.
    if (cp >= 0x3041 && cp <= 0x33FF) return true;          // Hiragana, Katakana, CJK symbols
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;          // CJK Ext A
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;          // CJK Unified
    if (cp >= 0xA000 && cp <= 0xA4CF) return true;          // Yi
    if (cp >= 0xAC00 && cp <= 0xD7A3) return true;          // Hangul Syllables
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;          // CJK Compat
    if (cp >= 0xFE30 && cp <= 0xFE4F) return true;          // CJK Compat forms
    if (cp >= 0xFF00 && cp <= 0xFF60) return true;          // Fullwidth forms
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return true;          // Fullwidth signs
    if (cp >= 0x1F300 && cp <= 0x1FAFF) return true;        // Emoji-ish
    if (cp >= 0x20000 && cp <= 0x3FFFD) return true;        // CJK Ext B+
    return false;
}

bool IsZeroWidth(uint32_t cp) {
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF) return true;
    if (cp >= 0x0300 && cp <= 0x036F) return true;  // combining diacritics
    if (cp == 0)                  return true;
    return false;
}

// Decode one codepoint from a wchar_t (UTF-16) sequence.
// Advances index by 1 or 2. Returns the codepoint.
uint32_t DecodeUtf16(std::wstring_view s, size_t& i) {
    uint32_t hi = static_cast<uint16_t>(s[i]);
    if (hi >= 0xD800 && hi <= 0xDBFF && i + 1 < s.size()) {
        uint32_t lo = static_cast<uint16_t>(s[i + 1]);
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
            i += 2;
            return 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
        }
    }
    i += 1;
    return hi;
}

}  // namespace

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        out.data(), needed);
    return out;
}

size_t DisplayWidth(std::wstring_view s) {
    size_t width = 0;
    size_t i = 0;
    while (i < s.size()) {
        // Skip ANSI CSI escape sequences: ESC '[' ... <final byte 0x40..0x7E>
        if (s[i] == 0x1B && i + 1 < s.size() && s[i + 1] == L'[') {
            i += 2;
            while (i < s.size()) {
                wchar_t c = s[i++];
                if (c >= 0x40 && c <= 0x7E) break;
            }
            continue;
        }
        uint32_t cp = DecodeUtf16(s, i);
        if (IsZeroWidth(cp)) continue;
        width += IsWideRange(cp) ? 2 : 1;
    }
    return width;
}

namespace {

// Step one codepoint (or one full ANSI CSI escape) starting at index i.
// Sets `outWidth` = display width contribution (0 for escapes / zero-width).
// Advances i.
void StepUnit(std::wstring_view s, size_t& i, size_t& outWidth) {
    if (s[i] == 0x1B && i + 1 < s.size() && s[i + 1] == L'[') {
        i += 2;
        while (i < s.size()) {
            wchar_t c = s[i++];
            if (c >= 0x40 && c <= 0x7E) break;
        }
        outWidth = 0;
        return;
    }
    uint32_t cp = DecodeUtf16(s, i);
    if (IsZeroWidth(cp)) { outWidth = 0; return; }
    outWidth = IsWideRange(cp) ? 2 : 1;
}

bool ContainsAnsi(std::wstring_view s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == 0x1B && s[i + 1] == L'[') return true;
    }
    return false;
}

}  // namespace

std::wstring TruncateToWidth(std::wstring_view s, size_t maxWidth, std::wstring_view ellipsis) {
    if (maxWidth == 0) return {};
    if (DisplayWidth(s) <= maxWidth) return std::wstring(s);

    const bool hadAnsi = ContainsAnsi(s);
    const std::wstring kReset = L"\x1b[0m";

    size_t ellipsisW = DisplayWidth(ellipsis);
    if (maxWidth <= ellipsisW) {
        // Not enough room for ellipsis; hard-cut.
        std::wstring out;
        size_t i = 0;
        size_t w = 0;
        while (i < s.size()) {
            size_t start = i;
            size_t cw = 0;
            StepUnit(s, i, cw);
            if (w + cw > maxWidth) break;
            out.append(s.substr(start, i - start));
            w += cw;
        }
        if (hadAnsi) out.append(kReset);
        return out;
    }

    size_t budget = maxWidth - ellipsisW;
    std::wstring out;
    size_t i = 0;
    size_t w = 0;
    while (i < s.size()) {
        size_t start = i;
        size_t cw = 0;
        StepUnit(s, i, cw);
        if (w + cw > budget) break;
        out.append(s.substr(start, i - start));
        w += cw;
    }
    if (hadAnsi) out.append(kReset);
    out.append(ellipsis);
    return out;
}

std::wstring DisplaySubstring(std::wstring_view s, size_t startCol, size_t widthCols) {
    if (widthCols == 0) return {};
    std::wstring out;
    out.reserve(widthCols);

    size_t i   = 0;
    size_t col = 0;

    // Skip ahead to startCol. ANSI escapes count as zero-width.
    while (i < s.size() && col < startCol) {
        size_t cw = 0;
        StepUnit(s, i, cw);
        col += cw;
    }

    // If a wide grapheme straddled the start boundary, pad with spaces.
    size_t outCol = 0;
    if (col > startCol) {
        size_t over = col - startCol;
        if (over > widthCols) over = widthCols;
        out.append(over, L' ');
        outCol += over;
    }

    // Collect widthCols of display content.
    while (i < s.size() && outCol < widthCols) {
        size_t start = i;
        size_t cw = 0;
        StepUnit(s, i, cw);
        if (cw == 0) {
            // Zero-width unit (ANSI escape, combining mark): copy verbatim.
            out.append(s.substr(start, i - start));
            continue;
        }
        if (outCol + cw > widthCols) break;
        out.append(s.substr(start, i - start));
        outCol += cw;
    }

    // Right-pad to fill widthCols exactly.
    if (outCol < widthCols) out.append(widthCols - outCol, L' ');
    return out;
}

std::wstring FormatDuration(std::chrono::milliseconds value) {
    if (value.count() < 0) value = std::chrono::milliseconds{0};
    long long totalSeconds = value.count() / 1000;
    long long hours = totalSeconds / 3600;
    long long minutes = (totalSeconds / 60) % 60;
    long long seconds = totalSeconds % 60;

    wchar_t buf[32];
    if (hours > 0) {
        swprintf(buf, 32, L"%lld:%02lld:%02lld", hours, minutes, seconds);
    } else {
        swprintf(buf, 32, L"%02lld:%02lld", minutes, seconds);
    }
    return buf;
}

std::wstring TrimCopy(std::wstring_view s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return std::wstring(s.substr(b, e - b));
}

}  // namespace npbar
