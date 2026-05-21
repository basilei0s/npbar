#pragma once

#include <string>
#include <string_view>

namespace npbar {

void EnableVirtualTerminalAndUtf8();
int  GetConsoleWidth();
bool StdoutIsRedirected();

void WriteUtf8Line(std::wstring_view line);
void WriteUtf8(std::wstring_view text);

void HideCursor();
void ShowCursor();

// Alternate screen buffer (VT) - switches the terminal to a clean blank
// screen and restores the previous content on exit. Used by watch mode so
// only the now-playing line is visible while npbar runs.
void EnterAlternateScreen();
void ExitAlternateScreen();
bool AlternateScreenSupported();

// Move cursor to row 0, col 0 (top-left of current screen).
void CursorHome();

// Returns the current console height in rows. Falls back to 30 if the
// console size cannot be determined (e.g. redirected output).
int GetConsoleHeight();

// Brute-force paint every visible row with the given SGR background sequence.
// Used instead of \x1b[2J because some terminals (notably Windows Terminal
// with a background image / acrylic) don't reliably fill the final row(s)
// from \x1b[2J. Writes spaces with explicit cursor positioning per row.
void PaintEntirePane(std::wstring_view sgrBackground);

void ClearCurrentLineAndReturn();

// Carriage-return, write `line`, then clear from cursor to end-of-line.
// Overwrites in place so the terminal never sees a blank intermediate frame.
// `prevDisplayWidth` is the display width of the previously rendered line,
// used by the non-VT fallback to know how many trailing chars to erase.
void RewriteCurrentLine(std::wstring_view line, size_t prevDisplayWidth);

class CursorGuard {
public:
    CursorGuard();
    ~CursorGuard();
    CursorGuard(const CursorGuard&) = delete;
    CursorGuard& operator=(const CursorGuard&) = delete;
};

class AltScreenGuard {
public:
    AltScreenGuard();
    ~AltScreenGuard();
    AltScreenGuard(const AltScreenGuard&) = delete;
    AltScreenGuard& operator=(const AltScreenGuard&) = delete;
    bool Active() const { return active_; }
private:
    bool active_ = false;
};

// Sets the terminal's default background color via OSC 11 on construction
// and restores it via OSC 111 on destruction. Used in watch mode so the
// entire pane (including padding and any background image) shows the
// chosen color while npbar runs.
class TerminalBackgroundGuard {
public:
    TerminalBackgroundGuard();
    explicit TerminalBackgroundGuard(std::wstring_view oscSet,
                                     std::wstring_view oscReset);
    ~TerminalBackgroundGuard();
    TerminalBackgroundGuard(const TerminalBackgroundGuard&) = delete;
    TerminalBackgroundGuard& operator=(const TerminalBackgroundGuard&) = delete;
    bool Active() const { return active_; }
private:
    std::wstring resetSeq_;
    bool active_ = false;
};

// Puts the console into character-mode input (no line buffering, no echo).
// Restores the previous input mode on destruction. ENABLE_PROCESSED_INPUT
// remains set so the console keeps translating Ctrl+C into our handler.
class RawInputGuard {
public:
    RawInputGuard();
    ~RawInputGuard();
    RawInputGuard(const RawInputGuard&) = delete;
    RawInputGuard& operator=(const RawInputGuard&) = delete;
    bool Active() const { return active_; }
private:
    void* handle_ = nullptr;       // HANDLE
    unsigned long prevMode_ = 0;   // DWORD
    bool active_ = false;
};

// Wait up to `timeoutMs` for console input, or until a quit key
// ('q', 'Q', or Esc) is pressed. Returns true if a quit key was pressed.
// Returns false on timeout, non-quit input, or when stdin is redirected.
// Consumes any input events available (key/mouse/focus/etc.) along the way.
bool WaitForQuitKey(unsigned long timeoutMs);

enum class WaitOutcome {
    Quit,     // q / Q / Esc pressed
    Updated,  // updateEvent signaled (caller should re-render now)
    Timeout,  // none of the above within timeoutMs
};

// Wait for either a quit key, the supplied auto-reset event to signal, or
// timeout. `updateEvent` is a Win32 HANDLE passed as void* to keep the
// header Windows-include-free; pass nullptr to wait only on key input.
WaitOutcome WaitForKeyOrUpdate(void* updateEvent, unsigned long timeoutMs);

}  // namespace npbar
