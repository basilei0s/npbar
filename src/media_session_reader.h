#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "now_playing_info.h"

namespace npbar {

class MediaSessionReader {
public:
    MediaSessionReader();
    ~MediaSessionReader();

    MediaSessionReader(const MediaSessionReader&) = delete;
    MediaSessionReader& operator=(const MediaSessionReader&) = delete;

    bool IsReady() const { return ready_; }
    std::wstring_view LastError() const { return lastError_; }

    std::optional<NowPlayingInfo> GetCurrent();

    // Win32 HANDLE (auto-reset Event) signaled whenever a GSMTC event
    // updates the cached snapshot. Watch loops can WaitForMultipleObjects
    // on it to wake up immediately on track / pause / seek changes
    // instead of waiting out the full refresh interval.
    // Returned as void* so this header stays Windows-include-free.
    void* GetUpdateEvent() const;

    static std::wstring NormalizeSource(std::wstring_view appId);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;
    std::wstring lastError_;

    static void DispatchEvent(void* impl, int kind, void* sender);
};

}  // namespace npbar
