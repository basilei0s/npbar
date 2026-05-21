# npbar

Tiny native Windows terminal now-playing bar. Reads the system media
session (GSMTC) and renders one live line.

![npbar example output](assets/bar.svg)

## Build

Requires Windows 10/11, CMake 3.20+, a Windows SDK, and either
MinGW-w64 (GCC 13+) or MSVC.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Output: `build/bin/npbar.exe` (~280 KB single-file). Pass
`-DNPBAR_STATIC_STDLIB=OFF` for a ~70 KB exe that needs MinGW
runtime DLLs alongside.

## Test

```powershell
ctest --test-dir build --output-on-failure -C Release
```

## Usage

`npbar` runs as a live bar in the alternate screen buffer until you
press **q**, **Esc**, or **Ctrl+C**.

| Flag | |
|------|-|
| `--once` | Print one line and exit |
| `--interval <ms>` | Refresh interval, min 250, default 1000 |
| `--width <auto\|N>` | Output width |
| `--plain` | ASCII one-shot: `Artist - Title [pos / dur]` |
| `--no-unicode` | ASCII glyphs only |
| `--no-color` | Disable colors |
| `--bg <spec>` | Pane bg: `dark` (default), `darker`, `black`, `#RRGGBB` |
| `--no-bg` | No pane background fill |
| `--empty <blank\|message>` | Behaviour when nothing is playing |

Exit codes: `0` success (including nothing playing), `1`
invalid args, `2` GSMTC failure.

## Limitations

- Lyrics, queue/next-track, and audio-quality flags are not exposed by
  GSMTC.
- If the Windows volume flyout shows wrong metadata for a player,
  npbar can't read it either.
- Position is extrapolated from `Timeline.LastUpdatedTime`, so it
  keeps ticking even when the player only pushes timeline updates on
  seek/track-change (most do).

## License

[MIT](LICENSE).
