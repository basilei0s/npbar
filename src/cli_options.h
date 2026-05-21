#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace npbar {

enum class EmptyMode {
    Message,
    Blank,
};

enum class WidthMode {
    Auto,
    Fixed,
};

struct CliOptions {
    bool once = false;  // --once: print one line and exit (otherwise: watch loop)
    std::chrono::milliseconds interval{1000};

    WidthMode widthMode = WidthMode::Auto;
    int fixedWidth = 0;

    bool plain = false;
    bool unicode = true;

    // Color preference. `noColor=true` disables colors regardless of
    // terminal support; otherwise colors are enabled iff the terminal
    // supports VT processing and stdout is not redirected.
    bool noColor = false;

    // Background fill for the whole terminal pane while watch mode is active.
    // Defaults to a subtle dark slate so npbar looks like a dedicated status
    // surface and overrides any terminal background image. --no-bg disables.
    bool noBackground = false;
    std::wstring backgroundSpec;  // empty = use default; otherwise: "dark",
                                  // "darker", "black", or "#RRGGBB"

    EmptyMode emptyMode = EmptyMode::Message;

    bool showHelp = false;
    bool showVersion = false;
};

struct CliParseResult {
    bool ok = true;
    std::optional<std::wstring> error;
    CliOptions options;
};

CliParseResult ParseCli(int argc, wchar_t** argv);
std::wstring HelpText();
std::wstring VersionText();

}  // namespace npbar
