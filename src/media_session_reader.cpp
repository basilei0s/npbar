#include "media_session_reader.h"

#include <atomic>
#include <chrono>
#include <cwchar>
#include <mutex>
#include <new>
#include <string>

#include "gsmtc_abi.h"

#include <roapi.h>
#include <winstring.h>

#include "text_util.h"

namespace npbar {

namespace MC = ABI::Windows::Media::Control;
namespace WF = ABI::Windows::Foundation;

using MC::IAsyncOperationOfSessionManager;
using MC::IAsyncOperationOfMediaProperties;

namespace {

// ----------- ComPtr / HString -----------

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) { Reset(); p_ = o.p_; if (p_) p_->AddRef(); }
        return *this;
    }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { Reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }

    T*  Get() const { return p_; }
    T** ReleaseAndGetAddressOf() { Reset(); return &p_; }
    T** GetAddressOf() { return &p_; }
    T*  operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    void Reset() { if (p_) { p_->Release(); p_ = nullptr; } }

private:
    T* p_ = nullptr;
};

class HString {
public:
    HString() = default;
    explicit HString(HSTRING h) : h_(h) {}
    ~HString() { if (h_) WindowsDeleteString(h_); }

    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
    HString(HString&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }

    HSTRING* ReleaseAndGetAddressOf() {
        if (h_) { WindowsDeleteString(h_); h_ = nullptr; }
        return &h_;
    }
    HSTRING Get() const { return h_; }

    std::wstring ToWString() const {
        if (!h_) return {};
        UINT32 len = 0;
        const wchar_t* buf = WindowsGetStringRawBuffer(h_, &len);
        if (!buf) return {};
        return std::wstring(buf, len);
    }

    static HRESULT Create(const wchar_t* s, UINT32 len, HSTRING* out) {
        return WindowsCreateString(s, len, out);
    }

private:
    HSTRING h_ = nullptr;
};

// ----------- helpers -----------

HRESULT WaitForAsync(IUnknown* op) {
    if (!op) return E_POINTER;
    ComPtr<IAsyncInfo> info;
    HRESULT hr = op->QueryInterface(__uuidof(IAsyncInfo),
        reinterpret_cast<void**>(info.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return hr;

    AsyncStatus status = Started;
    // Up to ~500 ms. GSMTC ops are normally <50 ms.
    for (int i = 0; i < 100; ++i) {
        hr = info->get_Status(&status);
        if (FAILED(hr)) return hr;
        if (status != Started) break;
        Sleep(5);
    }
    if (status == Completed) return S_OK;
    if (status == Error) {
        HRESULT err = S_OK;
        info->get_ErrorCode(&err);
        return SUCCEEDED(err) ? E_FAIL : err;
    }
    return E_FAIL;
}

std::wstring HResultMessage(HRESULT hr) {
    wchar_t buf[64];
    swprintf(buf, 64, L"HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::wstring ToLowerCopy(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    }
    return out;
}

std::wstring PlaybackStatusToString(MC::GlobalSystemMediaTransportControlsSessionPlaybackStatus s) {
    using S = MC::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (s) {
        case S::GlobalSystemMediaTransportControlsSessionPlaybackStatus_Playing:  return L"playing";
        case S::GlobalSystemMediaTransportControlsSessionPlaybackStatus_Paused:   return L"paused";
        case S::GlobalSystemMediaTransportControlsSessionPlaybackStatus_Stopped:  return L"stopped";
        case S::GlobalSystemMediaTransportControlsSessionPlaybackStatus_Changing: return L"changing";
        default:                                                                  return L"unknown";
    }
}

std::optional<std::chrono::milliseconds> ConvertTimeSpan(const WF::TimeSpan& ts, bool allowZero) {
    long long ticks = ts.Duration;
    if (ticks < 0) return std::nullopt;
    if (!allowZero && ticks == 0) return std::nullopt;
    return std::chrono::milliseconds(ticks / 10000);
}

int64_t FileTimeNowUtcTicks() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart);
}

// ----------- Event handler (one class per concrete IID) -----------
//
// A minimal ITypedEventHandler-shaped COM object. Vtable layout matches
// ITypedEventHandler<S, A>: [QueryInterface, AddRef, Release, Invoke].
// IAgileObject is reported supported so GSMTC's free-threaded dispatcher
// can call us from any thread without marshaling.

enum HandlerKind : int {
    HandlerKindManagerCurrent,
    HandlerKindManagerSessions,
    HandlerKindSessionMedia,
    HandlerKindSessionPlayback,
    HandlerKindSessionTimeline,
};

class CallbackContext {
public:
    CallbackContext(void* owner, void (*invoke)(void*, int, void*))
        : owner_(owner), invoke_(invoke) {}

    void AddRef() {
        InterlockedIncrement(&refCount_);
    }

    void Release() {
        LONG r = InterlockedDecrement(&refCount_);
        if (r == 0) delete this;
    }

    bool TryEnter() {
        if (shuttingDown_.load(std::memory_order_acquire)) return false;

        InterlockedIncrement(&activeCallbacks_);
        if (shuttingDown_.load(std::memory_order_acquire)) {
            Leave();
            return false;
        }
        return true;
    }

    void Leave() {
        InterlockedDecrement(&activeCallbacks_);
    }

    void BeginShutdown() {
        shuttingDown_.store(true, std::memory_order_release);
    }

    void WaitForIdle() const {
        while (activeCallbacks_ > 0) {
            Sleep(1);
        }
    }

    void Invoke(HandlerKind kind, IUnknown* sender) const {
        if (invoke_) invoke_(owner_, static_cast<int>(kind), sender);
    }

private:
    std::atomic<bool> shuttingDown_{false};
    volatile LONG activeCallbacks_ = 0;
    volatile LONG refCount_ = 1;
    void* owner_ = nullptr;
    void (*invoke_)(void*, int, void*) = nullptr;
};

// IMPORTANT: vtable layout MUST match ITypedEventHandler<S, A>:
//   [0] QueryInterface  [1] AddRef  [2] Release  [3] Invoke
// We override the three IUnknown methods (keeping their inherited slots)
// and declare Invoke as the FIRST new virtual so it lands in slot 3.
// No virtual destructor — it would consume slots 3-4 and push Invoke to 5,
// causing GSMTC to call the destructor whenever an event fires (silent
// no-fire at best, use-after-free at worst). Release manually runs the
// non-virtual destructor and frees storage to preserve the vtable layout.
class EventHandler : public IUnknown {
public:
    EventHandler(const GUID& handlerIid, CallbackContext* context, HandlerKind kind)
        : iid_(handlerIid), context_(context), kind_(kind) {
        if (context_) context_->AddRef();
    }

    // --- vtable slot 3: Invoke (must be the first new virtual) ---
    virtual HRESULT STDMETHODCALLTYPE Invoke(IUnknown* sender, IUnknown* /*args*/) {
        if (!context_ || !context_->TryEnter()) return S_OK;
        context_->Invoke(kind_, sender);
        context_->Leave();
        return S_OK;
    }

    // --- IUnknown overrides (slots 0, 1, 2) ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == iid_ || riid == IID_AgileObject) {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&refCount_);
        if (r == 0) {
            this->~EventHandler();
            ::operator delete(this);
        }
        return static_cast<ULONG>(r);
    }

private:
    ~EventHandler() {
        if (context_) context_->Release();
    }

    const GUID& iid_;
    CallbackContext* context_ = nullptr;
    HandlerKind kind_{};
    volatile LONG refCount_ = 1;
};

}  // namespace

// ============================================================================
//                                  Impl
// ============================================================================
struct MediaSessionReader::Impl {
    // --- COM lifetime ---
    bool                              comInitialized = false;
    ComPtr<MC::IGlobalSystemMediaTransportControlsSessionManager> manager;
    CallbackContext*                  callbackContext = nullptr;

    // --- Current session + its event handlers ---
    ComPtr<MC::IGlobalSystemMediaTransportControlsSession> session;
    EventRegistrationToken            tokSessionMedia{};
    EventRegistrationToken            tokSessionPlayback{};
    EventRegistrationToken            tokSessionTimeline{};
    bool                              sessionMediaSub    = false;
    bool                              sessionPlaybackSub = false;
    bool                              sessionTimelineSub = false;

    // --- Manager-level handlers ---
    EventRegistrationToken            tokManagerCurrent{};
    EventRegistrationToken            tokManagerSessions{};
    bool                              managerCurrentSub  = false;
    bool                              managerSessionsSub = false;

    // --- Cached snapshot read by the watch loop ---
    std::mutex                        cacheMutex;
    bool                              cacheHasSession = false;
    std::wstring                      cachedAumid;
    std::wstring                      cachedSource;
    std::wstring                      cachedArtist;
    std::wstring                      cachedTitle;
    std::wstring                      cachedAlbum;
    std::wstring                      cachedStatus = L"unknown";
    std::optional<std::chrono::milliseconds> cachedRawPosition;
    std::optional<std::chrono::milliseconds> cachedDuration;
    int64_t                           cachedLastUpdatedUtcTicks = 0;

    // --- Lifecycle gate (handler callbacks check this) ---
    std::atomic<bool>                 shuttingDown{false};

    // Serialises (un)subscription against itself. Manager-level events
    // (CurrentSessionChanged, SessionsChanged) can fire concurrently on
    // different COM worker threads — without this we'd race on the
    // session/token fields.
    std::mutex                        bindMutex;

    // Auto-reset Win32 event signaled by every cache mutation. Watch
    // loops wait on this together with their interval timeout, so a
    // track change / pause / seek wakes them immediately.
    HANDLE                            updateEvent = nullptr;

    void SignalUpdate() {
        if (updateEvent) SetEvent(updateEvent);
    }

    // --- Operations (defined out-of-line below) ---
    void RebindCurrentSession();   // (re)subscribe to the new current session
    void UnsubscribeSessionEvents();
    void RefreshMediaProperties(MC::IGlobalSystemMediaTransportControlsSession* s);
    void RefreshTimeline(MC::IGlobalSystemMediaTransportControlsSession* s);
    void RefreshPlaybackInfo(MC::IGlobalSystemMediaTransportControlsSession* s);
    void RefreshAumid(MC::IGlobalSystemMediaTransportControlsSession* s);
};

// ============================================================================
//                            Refresh helpers
// ============================================================================

void MediaSessionReader::Impl::RefreshAumid(MC::IGlobalSystemMediaTransportControlsSession* s) {
    if (!s) return;
    HString src;
    if (FAILED(s->get_SourceAppUserModelId(src.ReleaseAndGetAddressOf()))) return;
    std::wstring aumid = src.ToWString();
    std::wstring source = MediaSessionReader::NormalizeSource(aumid);
    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        cachedAumid  = std::move(aumid);
        cachedSource = std::move(source);
    }
    SignalUpdate();
}

void MediaSessionReader::Impl::RefreshMediaProperties(
        MC::IGlobalSystemMediaTransportControlsSession* s) {
    if (!s) return;
    ComPtr<IAsyncOperationOfMediaProperties> op;
    if (FAILED(s->TryGetMediaPropertiesAsync(op.ReleaseAndGetAddressOf())) || !op) return;
    if (FAILED(WaitForAsync(op.Get()))) return;
    ComPtr<MC::IGlobalSystemMediaTransportControlsSessionMediaProperties> props;
    if (FAILED(op->GetResults(props.ReleaseAndGetAddressOf())) || !props) return;

    std::wstring artist, title, album;
    HString h;
    if (SUCCEEDED(props->get_Artist(h.ReleaseAndGetAddressOf())))
        artist = TrimCopy(h.ToWString());
    if (SUCCEEDED(props->get_Title(h.ReleaseAndGetAddressOf())))
        title  = TrimCopy(h.ToWString());
    if (SUCCEEDED(props->get_AlbumTitle(h.ReleaseAndGetAddressOf())))
        album  = TrimCopy(h.ToWString());

    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        cachedArtist = std::move(artist);
        cachedTitle  = std::move(title);
        cachedAlbum  = std::move(album);
    }
    SignalUpdate();
}

void MediaSessionReader::Impl::RefreshTimeline(
        MC::IGlobalSystemMediaTransportControlsSession* s) {
    if (!s) return;
    ComPtr<MC::IGlobalSystemMediaTransportControlsSessionTimelineProperties> tl;
    if (FAILED(s->GetTimelineProperties(tl.ReleaseAndGetAddressOf())) || !tl) return;
    WF::TimeSpan pos{}, dur{};
    WF::DateTime upd{};
    std::optional<std::chrono::milliseconds> rawPos, duration;
    int64_t lastUpdated = 0;
    if (SUCCEEDED(tl->get_Position(&pos))) rawPos = ConvertTimeSpan(pos, /*allowZero=*/true);
    if (SUCCEEDED(tl->get_EndTime(&dur)))  duration = ConvertTimeSpan(dur, /*allowZero=*/false);
    if (SUCCEEDED(tl->get_LastUpdatedTime(&upd))) lastUpdated = upd.UniversalTime;

    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        cachedRawPosition         = rawPos;
        cachedDuration            = duration;
        cachedLastUpdatedUtcTicks = lastUpdated;
    }
    SignalUpdate();
}

void MediaSessionReader::Impl::RefreshPlaybackInfo(
        MC::IGlobalSystemMediaTransportControlsSession* s) {
    if (!s) return;
    ComPtr<MC::IGlobalSystemMediaTransportControlsSessionPlaybackInfo> pb;
    if (FAILED(s->GetPlaybackInfo(pb.ReleaseAndGetAddressOf())) || !pb) {
        {
            std::lock_guard<std::mutex> lk(cacheMutex);
            cachedStatus = L"unknown";
        }
        SignalUpdate();
        return;
    }
    MC::GlobalSystemMediaTransportControlsSessionPlaybackStatus st{};
    std::wstring status = L"unknown";
    if (SUCCEEDED(pb->get_PlaybackStatus(&st))) status = PlaybackStatusToString(st);
    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        cachedStatus = std::move(status);
    }
    SignalUpdate();
}

// ============================================================================
//                        Session subscription mgmt
// ============================================================================

void MediaSessionReader::Impl::UnsubscribeSessionEvents() {
    if (!session) return;
    if (sessionMediaSub)    { session->remove_MediaPropertiesChanged(tokSessionMedia);    sessionMediaSub    = false; }
    if (sessionPlaybackSub) { session->remove_PlaybackInfoChanged(tokSessionPlayback);    sessionPlaybackSub = false; }
    if (sessionTimelineSub) { session->remove_TimelinePropertiesChanged(tokSessionTimeline); sessionTimelineSub = false; }
}

void MediaSessionReader::Impl::RebindCurrentSession() {
    if (shuttingDown.load(std::memory_order_acquire)) return;
    if (!manager) return;

    std::lock_guard<std::mutex> bindLock(bindMutex);
    if (shuttingDown.load(std::memory_order_acquire)) return;

    UnsubscribeSessionEvents();
    session.Reset();

    ComPtr<MC::IGlobalSystemMediaTransportControlsSession> next;
    if (FAILED(manager->GetCurrentSession(next.ReleaseAndGetAddressOf())) || !next) {
        // No session — clear cache to indicate nothing playing.
        {
            std::lock_guard<std::mutex> lk(cacheMutex);
            cacheHasSession = false;
            cachedAumid.clear(); cachedSource.clear();
            cachedArtist.clear(); cachedTitle.clear(); cachedAlbum.clear();
            cachedStatus = L"unknown";
            cachedRawPosition.reset(); cachedDuration.reset();
            cachedLastUpdatedUtcTicks = 0;
        }
        SignalUpdate();
        return;
    }
    session = next;

    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        cacheHasSession = true;
    }
    SignalUpdate();

    // Subscribe to session-level events. Handlers use the COM event sender
    // as the session, so rebinding does not leave callbacks with stale raw
    // session pointers.
    {
        auto* h = new EventHandler(
            IID_TypedEventHandler_Session_MediaPropertiesChanged,
            callbackContext,
            HandlerKindSessionMedia);
        EventRegistrationToken tok{};
        HRESULT hr = session->add_MediaPropertiesChanged(h, &tok);
        h->Release();  // manager AddRef'd it via add_X
        if (SUCCEEDED(hr)) { tokSessionMedia = tok; sessionMediaSub = true; }
    }
    {
        auto* h = new EventHandler(
            IID_TypedEventHandler_Session_PlaybackInfoChanged,
            callbackContext,
            HandlerKindSessionPlayback);
        EventRegistrationToken tok{};
        HRESULT hr = session->add_PlaybackInfoChanged(h, &tok);
        h->Release();
        if (SUCCEEDED(hr)) { tokSessionPlayback = tok; sessionPlaybackSub = true; }
    }
    {
        auto* h = new EventHandler(
            IID_TypedEventHandler_Session_TimelinePropertiesChanged,
            callbackContext,
            HandlerKindSessionTimeline);
        EventRegistrationToken tok{};
        HRESULT hr = session->add_TimelinePropertiesChanged(h, &tok);
        h->Release();
        if (SUCCEEDED(hr)) { tokSessionTimeline = tok; sessionTimelineSub = true; }
    }

    // Prime the cache once so the first GetCurrent() returns the right data
    // even before any events have fired.
    RefreshAumid(session.Get());
    RefreshMediaProperties(session.Get());
    RefreshTimeline(session.Get());
    RefreshPlaybackInfo(session.Get());
}

// ============================================================================
//                           Reader public API
// ============================================================================

MediaSessionReader::MediaSessionReader() {
    impl_ = new Impl();
    impl_->callbackContext = new CallbackContext(impl_, MediaSessionReader::DispatchEvent);

    // Auto-reset event, initially non-signaled.
    impl_->updateEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        // Already initialised somewhere up the stack; fine.
    } else if (FAILED(hr)) {
        lastError_ = L"RoInitialize failed: " + HResultMessage(hr);
        return;
    } else {
        impl_->comInitialized = true;
    }

    HString className;
    hr = HString::Create(
        kRC_GlobalSystemMediaTransportControlsSessionManager,
        static_cast<UINT32>(wcslen(kRC_GlobalSystemMediaTransportControlsSessionManager)),
        className.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        lastError_ = L"WindowsCreateString failed: " + HResultMessage(hr);
        return;
    }

    ComPtr<MC::IGlobalSystemMediaTransportControlsSessionManagerStatics> statics;
    hr = RoGetActivationFactory(
        className.Get(),
        __uuidof(MC::IGlobalSystemMediaTransportControlsSessionManagerStatics),
        reinterpret_cast<void**>(statics.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        lastError_ = L"RoGetActivationFactory failed: " + HResultMessage(hr);
        return;
    }

    ComPtr<IAsyncOperationOfSessionManager> op;
    hr = statics->RequestAsync(op.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !op) {
        lastError_ = L"GSMTC RequestAsync failed: " + HResultMessage(hr);
        return;
    }
    hr = WaitForAsync(op.Get());
    if (FAILED(hr)) {
        lastError_ = L"GSMTC RequestAsync wait failed: " + HResultMessage(hr);
        return;
    }
    hr = op->GetResults(impl_->manager.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !impl_->manager) {
        lastError_ = L"GSMTC GetResults failed: " + HResultMessage(hr);
        return;
    }

    // Subscribe to manager-level events.
    {
        auto* h = new EventHandler(
            IID_TypedEventHandler_Manager_CurrentSessionChanged,
            impl_->callbackContext,
            HandlerKindManagerCurrent);
        EventRegistrationToken tok{};
        HRESULT add_hr = impl_->manager->add_CurrentSessionChanged(h, &tok);
        h->Release();
        if (SUCCEEDED(add_hr)) { impl_->tokManagerCurrent = tok; impl_->managerCurrentSub = true; }
    }
    {
        // SessionsChanged: triggered when any app starts/stops surfacing
        // a session. We treat it the same as CurrentSessionChanged because
        // the "current" pointer can also shift in response.
        auto* h = new EventHandler(
            IID_TypedEventHandler_Manager_SessionsChanged,
            impl_->callbackContext,
            HandlerKindManagerSessions);
        EventRegistrationToken tok{};
        HRESULT add_hr = impl_->manager->add_SessionsChanged(h, &tok);
        h->Release();
        if (SUCCEEDED(add_hr)) { impl_->tokManagerSessions = tok; impl_->managerSessionsSub = true; }
    }

    // Pull the initial session so the first GetCurrent() works without
    // waiting for an event.
    impl_->RebindCurrentSession();

    ready_ = true;
}

MediaSessionReader::~MediaSessionReader() {
    if (!impl_) return;

    // Signal handlers to no-op before we tear down event registrations.
    impl_->shuttingDown.store(true, std::memory_order_release);
    if (impl_->callbackContext) impl_->callbackContext->BeginShutdown();

    if (impl_->manager) {
        if (impl_->managerCurrentSub)  impl_->manager->remove_CurrentSessionChanged(impl_->tokManagerCurrent);
        if (impl_->managerSessionsSub) impl_->manager->remove_SessionsChanged(impl_->tokManagerSessions);
    }

    {
        std::lock_guard<std::mutex> bindLock(impl_->bindMutex);
        impl_->UnsubscribeSessionEvents();
    }

    if (impl_->callbackContext) impl_->callbackContext->WaitForIdle();
    impl_->session.Reset();
    impl_->manager.Reset();

    if (impl_->updateEvent) {
        CloseHandle(impl_->updateEvent);
        impl_->updateEvent = nullptr;
    }

    bool ownsCom = impl_->comInitialized;
    CallbackContext* callbackContext = impl_->callbackContext;
    impl_->callbackContext = nullptr;
    delete impl_;
    if (callbackContext) callbackContext->Release();
    impl_ = nullptr;
    if (ownsCom) RoUninitialize();
}

void MediaSessionReader::DispatchEvent(void* impl, int kindValue, void* senderValue) {
    auto* implPtr = static_cast<Impl*>(impl);
    if (!implPtr || implPtr->shuttingDown.load(std::memory_order_acquire)) return;

    HandlerKind kind = static_cast<HandlerKind>(kindValue);
    if (kind == HandlerKindManagerCurrent || kind == HandlerKindManagerSessions) {
        implPtr->RebindCurrentSession();
        return;
    }

    IUnknown* sender = static_cast<IUnknown*>(senderValue);
    ComPtr<MC::IGlobalSystemMediaTransportControlsSession> session;
    if (sender) {
        sender->QueryInterface(
            __uuidof(MC::IGlobalSystemMediaTransportControlsSession),
            reinterpret_cast<void**>(session.ReleaseAndGetAddressOf()));
    }
    if (!session || implPtr->shuttingDown.load(std::memory_order_acquire)) return;

    switch (kind) {
        case HandlerKindSessionMedia:
            implPtr->RefreshMediaProperties(session.Get());
            return;
        case HandlerKindSessionPlayback:
            implPtr->RefreshPlaybackInfo(session.Get());
            return;
        case HandlerKindSessionTimeline:
            implPtr->RefreshTimeline(session.Get());
            return;
        default:
            return;
    }
}

void* MediaSessionReader::GetUpdateEvent() const {
    return impl_ ? impl_->updateEvent : nullptr;
}

std::optional<NowPlayingInfo> MediaSessionReader::GetCurrent() {
    if (!ready_ || !impl_) return std::nullopt;

    NowPlayingInfo info;
    std::optional<std::chrono::milliseconds> rawPosition;
    int64_t lastUpdatedUtcTicks = 0;

    {
        std::lock_guard<std::mutex> lk(impl_->cacheMutex);
        if (!impl_->cacheHasSession) return std::nullopt;
        info.sourceAppUserModelId = impl_->cachedAumid;
        info.source               = impl_->cachedSource;
        info.artist               = impl_->cachedArtist;
        info.title                = impl_->cachedTitle;
        info.album                = impl_->cachedAlbum;
        info.status               = impl_->cachedStatus;
        info.duration             = impl_->cachedDuration;
        rawPosition               = impl_->cachedRawPosition;
        lastUpdatedUtcTicks       = impl_->cachedLastUpdatedUtcTicks;
    }

    // Position extrapolation from LastUpdatedTime to "now".
    info.position = rawPosition;
    if (rawPosition && lastUpdatedUtcTicks > 0 && info.status == L"playing") {
        int64_t deltaTicks = FileTimeNowUtcTicks() - lastUpdatedUtcTicks;
        if (deltaTicks > 0 && deltaTicks < int64_t(24) * 3600 * 10'000'000) {
            auto deltaMs = std::chrono::milliseconds(deltaTicks / 10'000);
            auto rolled = *rawPosition + deltaMs;
            if (info.duration && rolled > *info.duration) rolled = *info.duration;
            info.position = rolled;
        }
    }

    info.capturedAt = std::chrono::steady_clock::now();
    return info;
}

std::wstring MediaSessionReader::NormalizeSource(std::wstring_view appId) {
    std::wstring lower = ToLowerCopy(appId);

    auto has = [&](std::wstring_view needle) {
        return lower.find(needle) != std::wstring::npos;
    };

    if (has(L"tidal"))                                  return L"TIDAL";
    if (has(L"music.youtube") || has(L"ytmusic"))       return L"YouTube Music";
    if (has(L"spotify"))                                return L"Spotify";
    if (has(L"foobar2000") || has(L"foobar"))           return L"foobar2000";
    if (has(L"vlc"))                                    return L"VLC";
    if (has(L"groove") || has(L"zune"))                 return L"Groove";
    if (has(L"apple") && has(L"music"))                 return L"Apple Music";
    if (has(L"itunes"))                                 return L"iTunes";
    if (has(L"deezer"))                                 return L"Deezer";
    if (has(L"soundcloud"))                             return L"SoundCloud";
    if (has(L"plex"))                                   return L"Plex";
    if (has(L"chrome"))                                 return L"Chrome";
    if (has(L"msedge") || has(L"edge"))                 return L"Edge";
    if (has(L"firefox"))                                return L"Firefox";
    if (has(L"opera"))                                  return L"Opera";
    if (has(L"brave"))                                  return L"Brave";
    if (has(L"youtube"))                                return L"YouTube";

    std::wstring s(appId);
    if (s.size() >= 4) {
        std::wstring tail = ToLowerCopy(s.substr(s.size() - 4));
        if (tail == L".exe") s.resize(s.size() - 4);
    }
    auto bs = s.find_last_of(L"\\/");
    if (bs != std::wstring::npos) s = s.substr(bs + 1);
    if (s.empty()) return L"unknown";
    return s;
}

}  // namespace npbar
