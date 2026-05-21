#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace npbar {

std::string WideToUtf8(std::wstring_view s);

size_t DisplayWidth(std::wstring_view s);

std::wstring TruncateToWidth(std::wstring_view s, size_t maxWidth, std::wstring_view ellipsis);

// Return the slice of `s` starting at display column `startCol` and
// spanning `widthCols` columns. ANSI escapes are skipped (zero-width).
// If the slice ends mid-wide-grapheme the result is trimmed and right-
// padded with spaces so it is always exactly `widthCols` wide.
std::wstring DisplaySubstring(std::wstring_view s, size_t startCol, size_t widthCols);

void AppendUnsignedDecimal(std::wstring& out, unsigned long long value);
void AppendTwoDigitDecimal(std::wstring& out, unsigned long long value);
void AppendHexByteLower(std::wstring& out, unsigned int value);
void AppendHex32Upper(std::wstring& out, unsigned long value);
void AppendDuration(std::wstring& out, std::chrono::milliseconds value);

std::wstring TrimCopy(std::wstring_view s);

}  // namespace npbar
