# OpenMK95

[![Build](https://github.com/MaxSignal/OpenMK95/actions/workflows/build.yml/badge.svg)](https://github.com/MaxSignal/OpenMK95/actions/workflows/build.yml)

OpenMK95 is a player that runs Windows games made with "RPGツクール95" (RPG Tsukuru 95 / RPG Maker 95, 1997) on
modern systems. It is not an emulator: it reads the game's data files directly and reimplements the behavior of
the original game runtime (GAME.EXE, runtime version 1.23).

## Features

- Title screen, maps and movement, vehicles, events, messages, menus, shops, inns, battles, pictures, screen
  effects and all 15 screen transitions.
- The original's 8-bit (256-color) display model, including palette effects and tints, rendered to a modern window.
- Save/load compatible with the original save files.
- Music and sound: WAV sound effects and music, MIDI through a SoundFont ([TinySoundFont](third_party/tinysoundfont)),
  the Windows MIDI synthesizer or a built-in synthesizer.
- AVI movies (uncompressed, RLE, Microsoft Video 1, Cinepak).
- Game folders with Shift_JIS or UTF-8 file names, matched case-insensitively.
- Keyboard, mouse and game controllers.
- Developed and tested on Linux and Windows (Wine); macOS builds are produced by CI.

## Downloads

Prebuilt binaries for Windows (x64 and x86), macOS (universal: Apple silicon and Intel) and Linux (x86_64) are
on the [releases page](https://github.com/MaxSignal/OpenMK95/releases):

- Versioned releases (`v0.1.0`, …) are the stable downloads.
- The **latest** pre-release is rebuilt automatically from every commit to `main`.

Notes:

- **Windows / macOS:** self-contained; SDL2 is linked statically.
- **Linux:** needs the SDL2 runtime library (e.g. `sudo apt install libsdl2-2.0-0`).
- **macOS:** the binaries are not signed. If macOS refuses to open `openmk95`, remove the quarantine attribute
  (`xattr -d com.apple.quarantine openmk95`) or allow it under System Settings → Privacy & Security.
- The easiest way to start is to put `openmk95` in a game folder (the folder containing the `.RPG` file) and
  double-click it. Started without a game, it opens a window that accepts a game folder by drag and drop.

## Getting started

### Dependencies

- A C++17 compiler
- CMake 3.16 or later
- SDL2. Alternatively, configure with `-DOPENMK95_VENDOR_SDL=ON` to download and link SDL2 statically (this is
  how the Windows and macOS release builds are made).

On Debian/Ubuntu:

```
sudo apt install build-essential cmake libsdl2-dev
```

On macOS with Homebrew: `brew install cmake sdl2`. On Windows, use Visual Studio 2022 (or newer) and configure
with `-DOPENMK95_VENDOR_SDL=ON`, or MSYS2/MinGW with its SDL2 package.

### Building

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

(With Visual Studio and Xcode generators the executables are placed in `build/Release/`.) This produces:

| Executable | Description |
|---|---|
| `openmk95` | The player (SDL2); also runs headless for testing and automation |
| `unit_tests` | Unit tests |

Cross-compiling for Windows from Linux with MinGW-w64: use `tools/mingw64.cmake` (or `tools/mingw32.cmake`) as
the toolchain file and point `SDL2_DIR` at the `lib/cmake/SDL2` folder of the SDL2 MinGW development package:

```
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=tools/mingw64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DSDL2_MINGW_ROOT=<SDL2-devel-mingw>/x86_64-w64-mingw32 \
      -DSDL2_DIR=<SDL2-devel-mingw>/x86_64-w64-mingw32/lib/cmake/SDL2
```

### Running

```
build/openmk95 [OPTIONS] [GAME_FOLDER]
```

- `GAME_FOLDER` is the folder containing the game's `.RPG` file.
- If omitted, the folder of the executable is used if it contains a `.RPG` file, then the current folder;
  otherwise a window asks for a game folder to be dropped on it.

| Option | Meaning |
|---|---|
| `--fullscreen` | Start in fullscreen (F4 or Alt+Enter toggles) |
| `--scale N` | Window scale factor (default: automatic from the screen size) |
| `--smooth` | Linear filtering when scaling |
| `--soundfont FILE` | SoundFont (`.sf2`) for MIDI music |
| `--midi auto\|sf2\|native\|builtin\|none` | MIDI synthesizer |
| `--volume N` | Master volume 0–100 |

### Controls

| Key | Function |
|---|---|
| Cursor keys / numpad 8 4 2 6 | Move, cursor, page switching in lists |
| Enter / Space | Confirm, examine |
| Esc | Cancel, open the menu |
| Shift (in battle) | Fast-forward messages |
| F4 / Alt+Enter | Toggle fullscreen |
| Mouse | As in the original (drag to walk, right click for the menu, double click to examine) |
| Game controller | D-pad = move, A = confirm, B = cancel, X = fast-forward |

### MIDI music

- `auto` (default): a SoundFont if one is found; otherwise the system MIDI synthesizer on Windows and the
  built-in synthesizer elsewhere.
- SoundFonts are searched in this order: `--soundfont`, the environment variable `OPENMK95_SOUNDFONT`, `*.sf2`
  next to the executable (or in a `soundfonts` folder there), and the usual Linux locations
  (`/usr/share/sounds/sf2/`, `/usr/share/soundfonts/`). A General MIDI SoundFont such as GeneralUser GS,
  FluidR3_GM or TimGM6mb sounds closest to the original.

### Saving and loading

The original's Windows "Open/Save file" dialogs are replaced by a list of the `*.sav` files in the game folder
(a replacement the specification allows). New saves are named `save01.sav`, `save02.sav`, … The file format is
the original's, so save files written by the original runtime can be loaded.

### Known limitations

- Text uses the Shinonome bitmap font (public domain) with synthetic bold instead of MS P Gothic. Window sizes
  follow the reference metrics of the specification (line height 18, character width 17).
- Movies in Intel Indeo 4 (IV41) are skipped. Audio tracks of AVI files are not played (none of the sample
  movies has one).
- CD audio is not supported (no sample game uses it).
- The original's two start-up logos are not shown (the specification allows this).
- Game controllers are supported through SDL instead of the original's joystick interface.

## Games

No game data is included. Supply your own game folders: each game is the folder containing its `.RPG` file and
data files. Games made with "Simulation RPGツクール95" are a different product and are not supported.

## Headless runner

`openmk95 --headless --virtual-time --script FILE GAME_FOLDER` runs a game without a window on a virtual clock,
driven by an input script, and is used for testing and automation. Script lines:

| Command | Meaning |
|---|---|
| `wait MS` | Wait MS milliseconds of game time |
| `press KEY [MS]` | Press and release a key (held for MS ms, default 60) |
| `down KEY` / `up KEY` | Press / release a key |
| `click X Y`, `rclick X Y`, `dblclick X Y`, `move X Y` | Mouse input in game coordinates |
| `shot FILE.bmp` | Save a screenshot |
| `repeat N COMMAND…` | Repeat a command N times |
| `log TEXT` | Print a line to the log |
| `quit` | End the program |

Key names: `Return`, `Space`, `Escape`, `Up`, `Down`, `Left`, `Right`, `Shift`, `F3`, `F4`, `KP2`/`KP4`/`KP6`/`KP8`
and single letters. With the environment variable `OPENMK95_LOG=1` every message shown is also printed to
standard error. `--test-battle N` starts battle with enemy troop N right after a game is loaded from the title
screen (used by `tools/check_battle.sh`).

## Running tests

```
build/unit_tests                  # unit tests
tools/check_battle.sh PANDA_DIR SAVE_FILE   # battle determinism check against the original runtime
```

Tests that need game data read it from your own copies; configure them with environment variables:

| Variable | Used by | Meaning | Default |
|---|---|---|---|
| `OPENMK95_SAMPLES` | unit tests | Folder of sample games, laid out as `<root>/<game folder>/…` | `./samples` |
| `OPENMK95_TMP` | unit tests | Scratch directory | the system temporary directory |
| `OPENMK95_PANDA` | `check_battle.sh` | Folder of the game パンダとウサギ | — |
| `OPENMK95_TESTDATA` | `check_battle.sh` | The `testdata` folder of the clean-room specification package | `./testdata` |
| `BIN` | `check_battle.sh` | Player binary to use | `build/openmk95` |

## Clean-room implementation

OpenMK95 was written only from a natural-language specification of the original runtime produced by a separate
team, together with the data files of sample games and black-box observation of the original running under
Wine. The original executable was not disassembled, decompiled or otherwise inspected, and no code or
information from other implementations or from the internet about the original was used. The records of this
process, including the specification itself, are kept in [docs/cleanroom/](docs/cleanroom/README.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Please read its clean-room policy before contributing. The developer
documentation is in [docs/](docs/README.md).

## License

- OpenMK95: MIT License ([LICENSE](LICENSE))
- `third_party/tinysoundfont`: MIT License (`third_party/tinysoundfont/LICENSE`)
- Glyph data (`src/core/gen/font_data.inc`, `tools/fonts/*.pcf.gz`): Shinonome font (public domain)
- SDL2: zlib License
- No game data or original executables are included.

## Trademarks

RPGツクール (RPG Tsukuru / RPG Maker) is a trademark of its respective owner. OpenMK95 is an independent project
and is not affiliated with or endorsed by its owner. The name is used only to describe compatibility.

## Acknowledgements

- [TinySoundFont](https://github.com/schellingb/TinySoundFont) by Bernhard Schelling, used for SoundFont playback.
- [SDL2](https://www.libsdl.org/), used for windows, input and audio.
- The Shinonome bitmap fonts (public domain), used for text rendering.
