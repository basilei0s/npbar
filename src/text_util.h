#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace npbar {

std::string WideToUtf8(std::wstring_view s);
std::wstring Utf8ToWide(std::string_view s);

size_t DisplayWidth(std::wstring_view s);

std::wstring TruncateToWidth(std::wstring_view s, size_t maxWidth, std::wstring_view ellipsis);

// Return the slice of `s` starting at display column `startCol` and
// spanning `widthCols` columns. ANSI escapes are skipped (zero-width).
// If the slice ends mid-wide-grapheme the result is trimmed and right-
// padded with spaces so it is always exactly `widthCols` wide.
std::wstring DisplaySubstring(std::wstring_view s, size_t startCol, size_t widthCols);

std::wstring FormatDuration(std::chrono::milliseconds value);

std::wstring TrimCopy(std::wstring_view s);

}  // namespace npbar
