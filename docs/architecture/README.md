# Architecture overview

OpenMK95 reimplements the behavior of the RPG Maker 95 game runtime (GAME.EXE, version 1.23). It does not
emulate Windows 95 or the x86 CPU. Instead, it reproduces what the runtime does with the game's files, on top
of a small model of the system the original ran on: a 640×480 screen in an 8-bit palette mode, the Windows
message loop, WAV and MIDI output and AVI playback.

## Layers

```
 app        openmk95 (main.cpp): command line, game folder, start-up checks
 ──────────────────────────────────────────────────────────────────────────────────────────
 engine     runtime (main loop, waits, title)  map  events  menu  battle  save  audio rules
            display (8-bit screen model)   ui (13 window slots, text)   data (game files)
 ──────────────────────────────────────────────────────────────────────────────────────────
 services   platform (SDL2 host)     sound (mixer, MIDI)     movie (AVI decoders)
 ──────────────────────────────────────────────────────────────────────────────────────────
 core       Shift_JIS / CP932, file lookup, BMP and surfaces, font, random numbers
```

- **core** (`src/core`): code shared by everything and independent of SDL: Shift_JIS handling with a CP932
  table, case-insensitive file lookup with the original's base-name rule, BMP decoding and 8-bit surfaces, the
  bitmap font and the random-number generator. See [platform.md](platform.md).
- **platform** (`src/platform`): the only module that talks to the window system: a 640×480 window, the input
  queue (keys are Windows virtual-key codes inside the runtime), the millisecond clock, the virtual clock and
  input scripts for headless runs, and message boxes. See [platform.md](platform.md).
- **sound** (`src/sound`) and **movie** (`src/movie`): device-level audio (wave channels with the sound-numbering
  rules of the original, an SMF sequencer and three MIDI synthesizers) and AVI decoding. They know nothing about
  games. See [sound-movie.md](sound-movie.md).
- **engine** (`src/engine`): the runtime itself, organized by the chapters of the specification. See
  [engine.md](engine.md).
- **app** (`src/app`): program start: options, choosing the game folder, the start-up checks and error boxes of
  the original, and the initial game state.

## Program structure

The original runtime is a single-threaded Windows program built from nested blocking loops: the main loop, and
inside it waits, messages, menus and battles, each of which keeps the program responsive by running a
*message-processing pass* while it waits. OpenMK95 keeps exactly this structure, because the order in which input,
timers and game logic are handled is observable (and some games depend on it):

- `rt::processMessages()` is the message-processing pass (spec 01 §3.2). It pumps the host's events into the
  input queue (`plat::pumpOS`), polls the audio module for MIDI end notifications, then handles window
  notifications and input events one at a time.
- `rt::wait(n)` (`Wait(n)` of the specification), `rt::message`, the window selection loops, the battle and the
  main loop (`rt::mainLoop`, 01 §8) are ordinary loops that call `processMessages()` and `plat::idle()`.
- Game state lives in one global `Game G` (`src/engine/state.h`), mirroring the shared state of spec 00 §5.

## Display

All drawing happens on 8-bit surfaces (`disp::visible`, `back`, `cache`, the character and chip-set bitmaps)
with the three palettes of spec 03 §2 (work, committed, realised). Palette effects and fades change the realised
palette with the specification's integer formulas, exactly as palette changes in the original's 256-color mode
changed the whole screen at once. Whenever something visible changes, `disp::markDirty()` asks the platform to
compose a frame: `disp::compose()` converts the visible surface through the realised palette to RGB and draws the
window layers and a playing movie on top. See [engine.md](engine.md).

## Threads

- **Game thread:** the engine, display, windows and file access all run on the main thread, like the original.
- **Audio:** the SDL audio callback mixes the wave channels and renders the software MIDI synthesizers. MIDI
  events are sequenced inside the callback, so tempo follows the audio clock. On Windows the native MIDI output
  uses its own sequencer thread. The engine only talks to the audio module through `snd::` calls guarded by a lock.

## Determinism and testing

The random-number generator and the order in which numbers are drawn follow spec 01 §6 exactly. With
`--virtual-time` the millisecond clock is virtual (it advances only when the program idles or waits), so a run
driven by an input script gives the same frames and messages every time. The unit tests (`tests/`), the headless
runner and `tools/check_battle.sh` rely on this. See [CONTRIBUTING.md](../../CONTRIBUTING.md) for how to run them.

## Specification chapters

The code comments refer to the clean-room specification by chapter and section (for example `08 §6.2`):

| Chapter | Topic | Code |
|---|---|---|
| 00 | Rules, conventions, shared state | `src/engine/state.h` |
| 01 | Program structure, main loop, input, waits, random numbers, start-up, title, game over | `src/engine/runtime.cpp`, `src/app/main.cpp`, `src/core/rng.cpp`, `src/platform` |
| 02 | Data formats and file naming | `src/engine/data.cpp`, `src/core/fsys.cpp`, `src/core/sjis.cpp` |
| 03 | 8-bit screen model, palettes, transitions, pictures | `src/engine/display.cpp`, `src/core/gfx.cpp` |
| 04 | Maps, movement, vehicles, encounters | `src/engine/map.cpp` |
| 05 | Events and event commands | `src/engine/events.cpp` |
| 06 | Windows, fonts, messages | `src/engine/ui.cpp`, `src/core/font.cpp`, `src/engine/runtime.cpp` |
| 07 | Menus, shops, inns | `src/engine/menu.cpp` |
| 08 | Battle | `src/engine/battle.cpp` |
| 09 | Audio and movies | `src/engine/audio.cpp`, `src/engine/movieplay.cpp`, `src/sound`, `src/movie` |
| 10 | Save files | `src/engine/save.cpp` |
| 11 | Appendix: tables, quirks, verification plan | throughout; `tests/` |

The specification is in [docs/cleanroom/spec/](../cleanroom/spec/README.md); see
[docs/cleanroom/](../cleanroom/README.md) for how it was used.
