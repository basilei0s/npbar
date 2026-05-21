#include "console_util.h"

#include <cwchar>
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

void CursorHome() {
    if (g_vtEnabled) {
        WriteUtf8(L"\x1b[H");
        return;
    }
    HANDLE h = StdOut();
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    COORD origin{0, 0};
    SetConsoleCursorPosition(h, origin);
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
        wchar_t pos[24];
        swprintf(pos, 24, L"\x1b[%d;1H", r);
        frame.append(pos);
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

// Drain any pending console input events; return true if a quit key
// ('q', 'Q', or Esc) was among them.
bool DrainConsoleInputCheckQuit(HANDLE h) {
    bool quit = false;
    INPUT_RECORD records[16];
    DWORD numRead = 0;
    DWORD pending = 0;
    while (GetNumberOfConsoleInputEvents(h, &pending) && pending > 0) {
        if (!ReadConsoleInputW(h, records, 16, &numRead)) break;
        for (DWORD i = 0; i < numRead; ++i) {
            if (records[i].EventType != KEY_EVENT) continue;
            const KEY_EVENT_RECORD& k = records[i].Event.KeyEvent;
            if (!k.bKeyDown) continue;
            wchar_t c = k.uChar.UnicodeChar;
            if (c == L'q' || c == L'Q' || c == 27 /* ESC */) {
                quit = true;
            }
        }
    }
    return quit;
}

}  // namespace

bool WaitForQuitKey(unsigned long timeoutMs) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        Sleep(timeoutMs);
        return false;
    }
    if (GetFileType(h) != FILE_TYPE_CHAR) {
        Sleep(timeoutMs);
        return false;
    }
    if (WaitForSingleObject(h, timeoutMs) != WAIT_OBJECT_0) return false;
    return DrainConsoleInputCheckQuit(h);
}

WaitOutcome WaitForKeyOrUpdate(void* updateEvent, unsigned long timeoutMs) {
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
        return DrainConsoleInputCheckQuit(in) ? WaitOutcome::Quit : WaitOutcome::Timeout;
    }
    if (which == evtIdx) {
        return WaitOutcome::Updated;
    }
    return WaitOutcome::Timeout;
}

}  // namespace npbar
