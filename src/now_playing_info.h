#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace npbar {

constexpr std::wstring_view kAppName = L"npbar";

enum class PlaybackStatus {
    Unknown,
    Playing,
    Paused,
    Stopped,
    Changing,
};

struct NowPlayingInfo {
    std::wstring source;

    std::wstring artist;
    std::wstring title;
    std::wstring album;

    PlaybackStatus status = PlaybackStatus::Unknown;

    std::optional<std::chrono::milliseconds> position;
    std::optional<std::chrono::milliseconds> duration;

    std::chrono::steady_clock::time_point capturedAt{};
};

}  // namespace npbar
