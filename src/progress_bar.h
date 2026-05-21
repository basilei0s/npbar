#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace npbar {

std::wstring RenderProgressBar(
    std::optional<std::chrono::milliseconds> position,
    std::optional<std::chrono::milliseconds> duration,
    int width,
    bool unicode);

}  // namespace npbar
