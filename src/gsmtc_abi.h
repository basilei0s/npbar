#pragma once

// Self-contained classic-ABI declarations for the Windows Media Control
// (GSMTC) interfaces npbar needs. We deliberately do NOT include MinGW's
// <windows.foundation.h>, which has a duplicate-specialization bug
// (IReference<boolean> vs IReference<BYTE> collapsing to IReference<unsigned char>).
// We pull in only the small Win32/WinRT primitives we actually use.

#include <windows.h>
#include <inspectable.h>   // IInspectable, HSTRING
#include <asyncinfo.h>     // IAsyncInfo, AsyncStatus
#include <eventtoken.h>    // EventRegistrationToken

namespace ABI {
namespace Windows {
namespace Foundation {

struct TimeSpan { INT64 Duration; };
struct DateTime { INT64 UniversalTime; };

}  // namespace Foundation
}  // namespace Windows
}  // namespace ABI

namespace ABI {
namespace Windows {
namespace Media {
namespace Control {

enum GlobalSystemMediaTransportControlsSessionPlaybackStatus : int {
    GlobalSystemMediaTransportControlsSessionPlaybackStatus_Closed   = 0,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus_Opened   = 1,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus_Changing = 2,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus_Stopped  = 3,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus_Playing  = 4,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus_Paused   = 5,
};

interface IGlobalSystemMediaTransportControlsSession;
interface IGlobalSystemMediaTransportControlsSessionManager;
interface IGlobalSystemMediaTransportControlsSessionMediaProperties;

MIDL_INTERFACE("EDE34136-6F25-588D-8ECF-EA5B6735AAA5")
IGlobalSystemMediaTransportControlsSessionTimelineProperties : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(ABI::Windows::Foundation::DateTime*) = 0;
};

MIDL_INTERFACE("94B4B6CF-E8BA-51AD-87A7-C10ADE106127")
IGlobalSystemMediaTransportControlsSessionPlaybackInfo : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Controls(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(
        ABI::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AutoRepeatMode(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsShuffleActive(void**) = 0;
};

MIDL_INTERFACE("68856CF6-ADB4-54B2-AC16-05837907ACB6")
IGlobalSystemMediaTransportControlsSessionMediaProperties : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(void**) = 0;
};

interface IAsyncOperationOfMediaProperties;
interface IAsyncOperationOfSessionManager;

// Full vtable for Session — we don't call the Try* methods but they occupy
// vtable slots between GetPlaybackInfo and the add_/remove_ event methods.
// Their argument types don't matter for vtable layout since we never invoke
// them.
MIDL_INTERFACE("7148C835-9B14-5AE2-AB85-DC9B1C14E1A8")
IGlobalSystemMediaTransportControlsSession : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(
        ABI::Windows::Media::Control::IAsyncOperationOfMediaProperties** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(
        ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionTimelineProperties** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(
        ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionPlaybackInfo** result) = 0;

    // --- Try* methods (15 slots; never invoked, placeholders only) ---
    virtual HRESULT STDMETHODCALLTYPE _TryPlayAsync(void**)                  = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryPauseAsync(void**)                 = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryStopAsync(void**)                  = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryRecordAsync(void**)                = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryFastForwardAsync(void**)           = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryRewindAsync(void**)                = 0;
    virtual HRESULT STDMETHODCALLTYPE _TrySkipNextAsync(void**)              = 0;
    virtual HRESULT STDMETHODCALLTYPE _TrySkipPreviousAsync(void**)          = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryChangeChannelUpAsync(void**)       = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryChangeChannelDownAsync(void**)     = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryTogglePlayPauseAsync(void**)       = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryChangeAutoRepeatModeAsync(int, void**)        = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryChangePlaybackRateAsync(double, void**)       = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryChangeShuffleActiveAsync(boolean, void**)     = 0;
    virtual HRESULT STDMETHODCALLTYPE _TryChangePlaybackPositionAsync(INT64, void**)    = 0;

    // --- Event subscriptions (6 slots) ---
    virtual HRESULT STDMETHODCALLTYPE add_TimelinePropertiesChanged(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_TimelinePropertiesChanged(
        EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackInfoChanged(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackInfoChanged(
        EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MediaPropertiesChanged(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MediaPropertiesChanged(
        EventRegistrationToken token) = 0;
};

MIDL_INTERFACE("CACE8EAC-E86E-504A-AB31-5FF8FF1BCE49")
IGlobalSystemMediaTransportControlsSessionManager : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(
        ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSession** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE _GetSessions(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_CurrentSessionChanged(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_CurrentSessionChanged(
        EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SessionsChanged(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SessionsChanged(
        EventRegistrationToken token) = 0;
};

MIDL_INTERFACE("2050C4EE-11A0-57DE-AED7-C97C70338245")
IGlobalSystemMediaTransportControlsSessionManagerStatics : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(
        ABI::Windows::Media::Control::IAsyncOperationOfSessionManager** operation) = 0;
};

// Hand-rolled IAsyncOperation<T> specializations. We only ever invoke
// GetResults after polling via IAsyncInfo.
//   [0..2]  IUnknown   [3..5]  IInspectable
//   [6]     put_Completed   [7]  get_Completed   [8]  GetResults

MIDL_INTERFACE("3eec115e-7346-5c27-8c5f-da78514a277b")
IAsyncOperationOfSessionManager : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(
        ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManager** results) = 0;
};

MIDL_INTERFACE("b185e6f3-e0d8-51cb-913f-c98d48c93c46")
IAsyncOperationOfMediaProperties : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(
        ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionMediaProperties** results) = 0;
};

}  // namespace Control
}  // namespace Media
}  // namespace Windows
}  // namespace ABI

#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSession,
    0x7148C835, 0x9B14, 0x5AE2, 0xAB, 0x85, 0xDC, 0x9B, 0x1C, 0x14, 0xE1, 0xA8)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManager,
    0xCACE8EAC, 0xE86E, 0x504A, 0xAB, 0x31, 0x5F, 0xF8, 0xFF, 0x1B, 0xCE, 0x49)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManagerStatics,
    0x2050C4EE, 0x11A0, 0x57DE, 0xAE, 0xD7, 0xC9, 0x7C, 0x70, 0x33, 0x82, 0x45)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionMediaProperties,
    0x68856CF6, 0xADB4, 0x54B2, 0xAC, 0x16, 0x05, 0x83, 0x79, 0x07, 0xAC, 0xB6)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionPlaybackInfo,
    0x94B4B6CF, 0xE8BA, 0x51AD, 0x87, 0xA7, 0xC1, 0x0A, 0xDE, 0x10, 0x61, 0x27)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionTimelineProperties,
    0xEDE34136, 0x6F25, 0x588D, 0x8E, 0xCF, 0xEA, 0x5B, 0x67, 0x35, 0xAA, 0xA5)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IAsyncOperationOfSessionManager,
    0x3eec115e, 0x7346, 0x5c27, 0x8c, 0x5f, 0xda, 0x78, 0x51, 0x4a, 0x27, 0x7b)
__CRT_UUID_DECL(ABI::Windows::Media::Control::IAsyncOperationOfMediaProperties,
    0xb185e6f3, 0xe0d8, 0x51cb, 0x91, 0x3f, 0xc9, 0x8d, 0x48, 0xc9, 0x3c, 0x46)
#endif

inline const wchar_t kRC_GlobalSystemMediaTransportControlsSessionManager[] =
    L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";

// Stable GUIDs for the five ITypedEventHandler<S, A> specializations we use.
// Harvested from the Windows SDK 10.0.18362 winrt/windows.media.control.h.
namespace npbar {
inline constexpr GUID IID_TypedEventHandler_Manager_CurrentSessionChanged =
    {0x228bd0ed, 0x1fa2, 0x5e9b, {0xa6, 0xec, 0x42, 0x56, 0x61, 0x73, 0x10, 0x3b}};
inline constexpr GUID IID_TypedEventHandler_Manager_SessionsChanged =
    {0x2e2a8630, 0xdc8c, 0x530a, {0x97, 0x46, 0xbc, 0x98, 0x4d, 0x4b, 0x02, 0x9e}};
inline constexpr GUID IID_TypedEventHandler_Session_MediaPropertiesChanged =
    {0x0f2ce2b7, 0xafa7, 0x5ed0, {0x8c, 0xb6, 0x8c, 0x40, 0xcf, 0x9b, 0x3a, 0x5f}};
inline constexpr GUID IID_TypedEventHandler_Session_PlaybackInfoChanged =
    {0x2bdf1426, 0xd41f, 0x5896, {0x89, 0x7f, 0xef, 0xc0, 0xb0, 0xfa, 0x73, 0x92}};
inline constexpr GUID IID_TypedEventHandler_Session_TimelinePropertiesChanged =
    {0xe8bf62af, 0xfac1, 0x5fff, {0x90, 0x53, 0x0b, 0xf1, 0x91, 0xae, 0x77, 0x7e}};

// Marker interface — signals "callable from any apartment without marshaling".
// Supplying it short-circuits COM's threading-model adapter.
inline constexpr GUID IID_AgileObject =
    {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
}  // namespace npbar
