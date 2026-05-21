#include "console_util.h"

#include <string>

#include <windows.h>

#include "text_util.h"

namespace npbar {

namespace {

HANDLE StdOut() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

bool g_vtEnabled = false;

}  // namespace

void EnableVirtualTerminalAndUtf8() {
    SetConsoleOutputCP(CP_UTF8);

    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;

    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        DWORD newMode = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(h, newMode)) {
            g_vtEnabled = true;
        }
    }
}

bool StdoutIsRedirected() {
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return true;
    DWORD type = GetFileType(h);
    return type != FILE_TYPE_CHAR;
}

int GetConsoleWidth() {
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return 120;
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (w <= 0) return 120;
        return w;
    }
    return 120;
}

void WriteUtf8(std::wstring_view text) {
    std::string utf8 = WideToUtf8(text);
    if (utf8.empty()) return;
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void WriteUtf8Line(std::wstring_view line) {
    WriteUtf8(line);
    WriteUtf8(L"\r\n");
}

void HideCursor() {
    if (g_vtEnabled) {
        WriteUtf8(L"\x1b[?25l");
        return;
    }
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    CONSOLE_CURSOR_INFO ci{};
    if (GetConsoleCursorInfo(h, &ci)) {
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(h, &ci);
    }
}

void ShowCursor() {
    if (g_vtEnabled) {
        WriteUtf8(L"\x1b[?25h");
        return;
    }
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    CONSOLE_CURSOR_INFO ci{};
    if (GetConsoleCursorInfo(h, &ci)) {
        ci.bVisible = TRUE;
        SetConsoleCursorInfo(h, &ci);
    }
}

void ClearCurrentLineAndReturn() {
    if (g_vtEnabled) {
        WriteUtf8(L"\r\x1b[2K");
        return;
    }
    int w = GetConsoleWidth();
    std::wstring pad(static_cast<size_t>(w > 0 ? w - 1 : 0), L' ');
    WriteUtf8(L"\r");
    WriteUtf8(pad);
    WriteUtf8(L"\r");
}

void RewriteCurrentLine(std::wstring_view line, size_t prevDisplayWidth) {
    if (g_vtEnabled) {
        // \r            -> cursor to col 0
        // line          -> overwrite old content
        // \x1b[K        -> erase from cursor to end of line (handles shorter new line)
        WriteUtf8(L"\r");
        WriteUtf8(line);
        WriteUtf8(L"\x1b[K");
        return;
    }
    // Fallback for legacy consoles without VT: overwrite, then pad trailing
    // chars with spaces if the new line is shorter than the previous one.
    WriteUtf8(L"\r");
    WriteUtf8(line);
    size_t newW = DisplayWidth(line);
    if (prevDisplayWidth > newW) {
        std::wstring pad(prevDisplayWidth - newW, L' ');
        WriteUtf8(pad);
        // Return cursor to where the line content ended (best-effort).
        std::wstring back(prevDisplayWidth - newW, L'\b');
        WriteUtf8(back);
    }
}

CursorGuard::CursorGuard() { HideCursor(); }
CursorGuard::~CursorGuard() { ShowCursor(); }

bool AlternateScreenSupported() {
    return g_vtEnabled && !StdoutIsRedirected();
}

void EnterAlternateScreen() {
    if (!AlternateScreenSupported()) return;
    // 1049h: save cursor + switch to alt screen + clear alt screen
    WriteUtf8(L"\x1b[?1049h");
    // Belt-and-suspenders: clear screen, home cursor.
    WriteUtf8(L"\x1b[2J\x1b[H");
}

void ExitAlternateScreen() {
    if (!AlternateScreenSupported()) return;
    // 1049l: switch back to main screen + restore saved cursor
    WriteUtf8(L"\x1b[?1049l");
}

int GetConsoleHeight() {
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return 30;
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return rows > 0 ? rows : 30;
    }
    return 30;
}

void PaintEntirePane(std::wstring_view sgrBackground) {
    if (sgrBackground.empty()) return;
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;

    int cols = GetConsoleWidth();
    int rows = GetConsoleHeight();
    if (cols <= 0 || rows <= 0) return;

    // Build the pane fill once and emit a single console write. This avoids
    // per-row UTF-8 conversions and reduces visible startup churn.
    std::wstring blank(static_cast<size_t>(cols), L' ');
    std::wstring frame;
    frame.reserve(static_cast<size_t>(rows) * (blank.size() + sgrBackground.size() + 16));

    for (int r = 1; r <= rows; ++r) {
        frame.append(L"\x1b[");
        AppendUnsignedDecimal(frame, static_cast<unsigned int>(r));
        frame.append(L";1H");
        frame.append(sgrBackground);
        frame.append(blank);
    }
    frame.append(L"\x1b[H");
    WriteUtf8(frame);
}

AltScreenGuard::AltScreenGuard() {
    if (AlternateScreenSupported()) {
        EnterAlternateScreen();
        active_ = true;
    }
}
AltScreenGuard::~AltScreenGuard() {
    if (active_) ExitAlternateScreen();
}

TerminalBackgroundGuard::TerminalBackgroundGuard() = default;

TerminalBackgroundGuard::TerminalBackgroundGuard(std::wstring_view oscSet,
                                                 std::wstring_view oscReset) {
    if (oscSet.empty() || !g_vtEnabled || StdoutIsRedirected()) return;
    WriteUtf8(oscSet);
    resetSeq_.assign(oscReset);
    active_ = true;
}

TerminalBackgroundGuard::~TerminalBackgroundGuard() {
    if (!active_) return;
    if (!resetSeq_.empty()) WriteUtf8(resetSeq_);
}

RawInputGuard::RawInputGuard() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    if (GetFileType(h) != FILE_TYPE_CHAR) return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;
    handle_   = h;
    prevMode_ = mode;
    DWORD newMode = mode & ~(static_cast<DWORD>(ENABLE_LINE_INPUT)
                            | static_cast<DWORD>(ENABLE_ECHO_INPUT));
    // Keep ENABLE_PROCESSED_INPUT so Ctrl+C still routes to our handler.
    newMode |= ENABLE_PROCESSED_INPUT;
    if (SetConsoleMode(h, newMode)) active_ = true;
}

RawInputGuard::~RawInputGuard() {
    if (active_ && handle_) SetConsoleMode(static_cast<HANDLE>(handle_), prevMode_);
}

namespace {

enum class ConsoleAction {
    None,
    Quit,         // q / Q / Esc
    TogglePlay,   // space
    SkipNext,     // n / N / Right arrow
    SkipPrev,     // p / P / Left arrow
};

// Drain any pending console input events and return the most important
// action seen. Priority: Quit > others; within "others" the latest wins.
// `helpHeld` is updated in place to reflect press/release of the '?' key.
//
// Tracking the help key release is trickier than the press: Shift+/ on a
// US layout produces uChar = '?' on the key-down event, but if the user
// releases Shift before the slash key, the slash key-up arrives with
// uChar = '/'. We therefore record the virtual-key code that produced
// the '?' on press, and watch for that vk going up regardless of the
// current modifier state.
ConsoleAction DrainConsoleInputAction(HANDLE h, bool& helpHeld) {
    static WORD s_helpVk = 0;  // vk we are currently tracking for release

    ConsoleAction action = ConsoleAction::None;
    INPUT_RECORD records[16];
    DWORD numRead = 0;
    DWORD pending = 0;
    while (GetNumberOfConsoleInputEvents(h, &pending) && pending > 0) {
        if (!ReadConsoleInputW(h, records, 16, &numRead)) break;
        for (DWORD i = 0; i < numRead; ++i) {
            if (records[i].EventType != KEY_EVENT) continue;
            const KEY_EVENT_RECORD& k = records[i].Event.KeyEvent;
            wchar_t c  = k.uChar.UnicodeChar;
            WORD    vk = k.wVirtualKeyCode;

            // Help-key tracking.
            if (k.bKeyDown && c == L'?') {
                helpHeld  = true;
                s_helpVk  = vk;
                continue;
            }
            if (!k.bKeyDown && helpHeld && s_helpVk != 0 && vk == s_helpVk) {
                helpHeld = false;
                s_helpVk = 0;
                continue;
            }

            if (!k.bKeyDown) continue;             // only handle key-down for actions
            if (action == ConsoleAction::Quit) continue;  // quit dominates

            if (c == L'q' || c == L'Q' || c == 27 /* ESC */) {
                action = ConsoleAction::Quit;
            } else if (c == L' ') {
                action = ConsoleAction::TogglePlay;
            } else if (c == L'n' || c == L'N' || vk == VK_RIGHT) {
                action = ConsoleAction::SkipNext;
            } else if (c == L'p' || c == L'P' || vk == VK_LEFT) {
                action = ConsoleAction::SkipPrev;
            }
        }
    }
    return action;
}

}  // namespace

WaitOutcome WaitForKeyOrUpdate(void* updateEvent, unsigned long timeoutMs,
                               bool& helpHeld) {
    HANDLE in  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE evt = reinterpret_cast<HANDLE>(updateEvent);

    bool inIsConsole = in != INVALID_HANDLE_VALUE && in != nullptr
                       && GetFileType(in) == FILE_TYPE_CHAR;

    HANDLE handles[2];
    DWORD  count   = 0;
    int    inIdx   = -1;
    int    evtIdx  = -1;
    if (inIsConsole) { inIdx  = static_cast<int>(count); handles[count++] = in;  }
    if (evt)         { evtIdx = static_cast<int>(count); handles[count++] = evt; }

    if (count == 0) {
        Sleep(timeoutMs);
        return WaitOutcome::Timeout;
    }

    DWORD res = WaitForMultipleObjects(count, handles, FALSE, timeoutMs);
    if (res == WAIT_TIMEOUT || res == WAIT_FAILED) return WaitOutcome::Timeout;

    int which = static_cast<int>(res - WAIT_OBJECT_0);
    if (which == inIdx) {
        switch (DrainConsoleInputAction(in, helpHeld)) {
            case ConsoleAction::Quit:       return WaitOutcome::Quit;
            case ConsoleAction::TogglePlay: return WaitOutcome::TogglePlay;
            case ConsoleAction::SkipNext:   return WaitOutcome::SkipNext;
            case ConsoleAction::SkipPrev:   return WaitOutcome::SkipPrev;
            case ConsoleAction::None:       return WaitOutcome::Timeout;
        }
        return WaitOutcome::Timeout;
    }
    if (which == evtIdx) {
        return WaitOutcome::Updated;
    }
    return WaitOutcome::Timeout;
}

}  // namespace npbar
