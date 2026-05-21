#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace npbar {

constexpr std::wstring_view kAppName = L"npbar";

struct NowPlayingInfo {
    std::wstring source;
    std::wstring sourceAppUserModelId;

    std::wstring artist;
    std::wstring title;
    std::wstring album;

    std::wstring status;

    std::optional<std::chrono::milliseconds> position;
    std::optional<std::chrono::milliseconds> duration;

    std::chrono::steady_clock::time_point capturedAt{};
};

}  // namespace npbar
