#include "cli_options.h"

#include <cwchar>
#include <string>
#include <string_view>

#include "now_playing_info.h"

namespace npbar {

namespace {

bool ParseInt(std::wstring_view s, int& out) {
    if (s.empty()) return false;
    long long v = 0;
    int sign = 1;
    size_t i = 0;
    if (s[0] == L'-') { sign = -1; i = 1; }
    else if (s[0] == L'+') { i = 1; }
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < L'0' || s[i] > L'9') return false;
        v = v * 10 + (s[i] - L'0');
        if (v > 2'000'000'000LL) return false;
    }
    out = static_cast<int>(v * sign);
    return true;
}

std::wstring MakeError(std::wstring_view msg) {
    std::wstring out(L"npbar: ");
    out.append(msg);
    return out;
}

}  // namespace

CliParseResult ParseCli(int argc, wchar_t** argv) {
    CliParseResult result;
    CliOptions& opt = result.options;

    auto fail = [&](std::wstring_view msg) {
        result.ok = false;
        result.error = MakeError(msg);
    };

    for (int i = 1; i < argc; ++i) {
        std::wstring_view a = argv[i];

        auto needValue = [&](const wchar_t** out) -> bool {
            if (i + 1 >= argc) {
                std::wstring m = std::wstring(a) + L" requires a value";
                fail(m);
                return false;
            }
            *out = argv[++i];
            return true;
        };

        if (a == L"--once") {
            opt.once = true;
        } else if (a == L"--interval") {
            const wchar_t* v = nullptr;
            if (!needValue(&v)) return result;
            int ms = 0;
            if (!ParseInt(v, ms) || ms <= 0) {
                fail(L"--interval requires a positive integer in milliseconds");
                return result;
            }
            if (ms < 250) ms = 250;
            opt.interval = std::chrono::milliseconds(ms);
        } else if (a == L"--width") {
            const wchar_t* v = nullptr;
            if (!needValue(&v)) return result;
            std::wstring_view vs = v;
            if (vs == L"auto") {
                opt.widthMode = WidthMode::Auto;
            } else {
                int w = 0;
                if (!ParseInt(vs, w) || w < 20) {
                    fail(L"--width requires 'auto' or an integer >= 20");
                    return result;
                }
                opt.widthMode = WidthMode::Fixed;
                opt.fixedWidth = w;
            }
        } else if (a == L"--plain") {
            opt.plain = true;
        } else if (a == L"--no-unicode") {
            opt.unicode = false;
        } else if (a == L"--no-color") {
            opt.noColor = true;
        } else if (a == L"--no-bg") {
            opt.noBackground = true;
        } else if (a == L"--bg") {
            const wchar_t* v = nullptr;
            if (!needValue(&v)) return result;
            opt.backgroundSpec = v;
            opt.noBackground = false;
        } else if (a == L"--empty") {
            const wchar_t* v = nullptr;
            if (!needValue(&v)) return result;
            std::wstring_view vs = v;
            if (vs == L"blank") opt.emptyMode = EmptyMode::Blank;
            else if (vs == L"message") opt.emptyMode = EmptyMode::Message;
            else { fail(L"--empty must be 'blank' or 'message'"); return result; }
        } else if (a == L"--help" || a == L"-h" || a == L"/?") {
            opt.showHelp = true;
        } else if (a == L"--version" || a == L"-v") {
            opt.showVersion = true;
        } else {
            std::wstring m = L"unknown option: ";
            m.append(a);
            fail(m);
            return result;
        }
    }

    if (opt.plain) {
        opt.unicode = false;
    }

    return result;
}

std::wstring HelpText() {
    std::wstring s;
    s += L"npbar - tiny native Windows terminal now-playing bar\n\n";
    s += L"Usage:\n";
    s += L"  npbar [options]\n\n";
    s += L"By default, npbar redraws the bar in place until Ctrl+C.\n\n";
    s += L"Options:\n";
    s += L"  --once               Print one line and exit (no live refresh).\n";
    s += L"  --interval <ms>      Refresh interval in milliseconds (>=250). Default 1000.\n";
    s += L"  --width <auto|N>     Output width. 'auto' uses the console width.\n";
    s += L"  --plain              ASCII-only, simplified output for scripts. Implies --once.\n";
    s += L"  --no-unicode         ASCII fallback glyphs.\n";
    s += L"  --no-color           Disable ANSI colors. Colors are otherwise auto-enabled\n";
    s += L"                       when the terminal supports them.\n";
    s += L"  --bg <spec>          Background fill for watch mode. Default 'dark'.\n";
    s += L"                       spec = dark | darker | black | #RRGGBB\n";
    s += L"  --no-bg              No background fill (terminal default / background image shows).\n";
    s += L"  --empty <blank|message>\n";
    s += L"                       What to print when nothing is playing.\n";
    s += L"  --help               Show this help.\n";
    s += L"  --version            Show version.\n";
    return s;
}

std::wstring VersionText() {
    std::wstring s(kAppName);
    s += L" 0.1.0\n";
    return s;
}

}  // namespace npbar
