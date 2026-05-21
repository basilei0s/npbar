#include "progress_bar.h"

namespace npbar {

std::wstring RenderProgressBar(
    std::optional<std::chrono::milliseconds> position,
    std::optional<std::chrono::milliseconds> duration,
    int width,
    bool unicode) {

    if (!duration || duration->count() <= 0) return {};
    if (width < 8) return {};
    if (width > 60) width = 60;

    long long pos = position ? position->count() : 0;
    long long dur = duration->count();
    if (pos < 0) pos = 0;
    if (pos > dur) pos = dur;

    int filled = static_cast<int>((pos * width) / dur);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    int empty = width - filled;

    wchar_t fillChar = unicode ? L'█' : L'#';  // █
    wchar_t blankChar = unicode ? L'░' : L'-'; // ░

    std::wstring out;
    out.reserve(static_cast<size_t>(width));
    out.append(static_cast<size_t>(filled), fillChar);
    out.append(static_cast<size_t>(empty), blankChar);
    return out;
}

}  // namespace npbar
