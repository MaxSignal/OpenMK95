# Contributing to OpenMK95

Thank you for your interest in OpenMK95. This document explains how to build and test the project, how the code
is organized, and the clean-room policy that contributions follow.

## Building and testing

Requirements: a C++17 compiler, CMake 3.16 or later, and SDL2 (or `-DOPENMK95_VENDOR_SDL=ON` to build it from
source). CI builds and tests Linux, Windows and macOS on every push (`.github/workflows/build.yml`).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/unit_tests
```

Tests that use game data read it from `$OPENMK95_SAMPLES` (default `./samples`, one folder per game) and write
scratch files to `$OPENMK95_TMP` (default: the system temporary directory). No game data is included in the
repository. Without the sample games, the tests that need them are reported as skipped; CI runs in this mode.

`tools/check_battle.sh` checks the battle random-number order against the result the original runtime showed
(spec 11 §E.4). It needs the game パンダとウサギ and `panda_original_runtime.sav` from the `testdata` folder of the
clean-room specification package.

The headless mode of `openmk95` (`--headless --virtual-time --script FILE`) is the main tool for reproducing and
checking behavior; see the README for its script format. Runs on the virtual clock are deterministic.

## Code layout

| Directory | Contents | Spec chapters |
|---|---|---|
| `src/core` | Shift_JIS text and CP932 table, file lookup and base-name rules, BMP decoding and 8-bit surfaces, font, random numbers | 01 §6, 02 §2, 03 §1, 06 §2 |
| `src/platform` | SDL2 host: window, frame output, input queue, clock, virtual time, input scripts, message boxes | 01 §3–§5 |
| `src/sound` | Wave mixer with the original's sound numbering, SMF sequencer, MIDI synthesizers (TinySoundFont, built-in, winmm) | 09 |
| `src/movie` | AVI demuxer and video decoders | 09 §7 |
| `src/engine` | The runtime: main loop and waits, data files, display model, windows, map, events, menus, battle, audio rules, save files, movie playback | 01–10 |
| `src/app` | Program start (`main.cpp`) | 01 §2, §9 |
| `tests` | Unit tests (`tests/test_<module>.cpp`) | Verification plan of 11 §E |
| `tools` | Font and code-table generators, MinGW toolchain files, the battle check, Wine observation helpers | — |
| `third_party` | TinySoundFont | — |

For how the pieces fit together, see the architecture documentation in
[docs/architecture/](docs/architecture/README.md).

Code comments refer to the specification by chapter and section, for example `05 §7.3` or `(quirk)`. Please keep
doing so: it is how reviewers check behavior against the specification.

## Clean-room policy

OpenMK95 was written without looking at the code of the original program or of other reimplementations, and we
would like to keep it that way. This protects the project and its users from questions about where the code
came from.

**What you can use**

- The clean-room specification in [docs/cleanroom/spec/](docs/cleanroom/spec/README.md).
- The game data files themselves (graphics, maps, databases, event files, sounds), which are what OpenMK95 reads.
- Your own observations of the original games running on Windows or under Wine: what is on the screen, what you
  hear and how the game reacts to input.
- Public documentation of file formats and system interfaces that are not specific to RPG Maker 95 (BMP, WAV,
  Standard MIDI Files, AVI and its codecs, Windows and SDL APIs) and the user manuals.

**What to avoid**

- Disassembling, decompiling, debugging or dumping strings or resources of GAME.EXE or other original
  executables. Running them and watching them is fine, but inspecting their code or memory is not.
- Using code, notes or analyses of RPG Maker 95 from other reimplementations or from the internet.

If you have already studied such material, please don't contribute changes to the parts it covered. Reports,
testing and work in other areas are still very welcome.

**In practice**

- When you change behavior, say in the pull request where it comes from: a section of the specification
  (for example "spec 08 §6.2"), a data file, or an observation (what you ran and what you saw).
- If the specification is unclear or contradicts itself, or you think it is wrong, open an issue rather than
  guessing. Points the specification marks **UNCERTAIN** are decided by the implementation; keep such decisions
  easy to find and change, and say in a comment that they are a decision.
- If you are not sure whether something is acceptable, ask in an issue first.

## Continuous integration and releases

`.github/workflows/build.yml` builds and tests every push and pull request on Linux, Windows and macOS and
publishes releases:

- Every push to `main` replaces the **latest** pre-release with the new build.
- To publish a versioned release, bump `project(OpenMK95 VERSION x.y.z)` in `CMakeLists.txt` and push to `main`:
  when no release `vX.Y.Z` exists yet, the workflow creates it (and its tag) for that commit.
- Pushing a `v*` tag also creates a release for that tag.

## License

By contributing, you agree that your contributions are licensed under the MIT License (see `LICENSE`).
