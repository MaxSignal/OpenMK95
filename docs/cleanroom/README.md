# Clean-room records

This directory documents **how** OpenMK95 was produced. It is not user or developer documentation, and it
describes the state of the project when it was first published (October 2026). For current documentation, see
the [README](../../README.md), [CONTRIBUTING.md](../../CONTRIBUTING.md) and
[docs/architecture/](../architecture/README.md).

## Process

1. **Specification team.** A separate team studied the original runtime (RPG Maker 95, GAME.EXE version 1.23)
   and wrote a natural-language specification describing *what* a compatible runtime must do: program
   structure and timing, data formats, the 8-bit screen model, movement, every event command, windows and text,
   menus, battle formulas, audio, movies, save files, and the order of random-number draws. It contains no code
   of the original or of any reimplementation, no addresses and no internal names. The specification is
   included in [spec/](spec/README.md).
2. **Implementation team.** OpenMK95 was implemented using only:
   - the specification,
   - the data files of 31 sample games made with RPG Maker 95 (data versions 0.96 to 1.23), and the save file
     written by the original runtime that accompanies the specification,
   - black-box observation of the original runtime running under Wine: watching the screen and sending keys,
     with no debugger, memory inspection, disassembly or decompilation
     (see [wine-observation.md](wine-observation.md)),
   - general-purpose third-party components that have nothing to do with RPG Maker 95 (SDL2, TinySoundFont, the
     Shinonome fonts) and public documentation of standard formats (BMP, WAV, SMF, AVI and its codecs).

   The implementation was written by an AI coding agent (Claude, by Anthropic) working in an isolated
   environment under the rules in [process-rules.md](process-rules.md). GAME.EXE was never disassembled,
   decompiled, string-dumped or otherwise inspected, and no information about RPG Maker 95 from the internet or
   from other reimplementations was used.
3. **Open points.** Where the specification marks behavior as **UNCERTAIN**, the implementers decided the point
   themselves, as the specification allows (spec 00 §1 rule 5), and kept the decision easy to change. Where the
   specification permits a replacement (for example the file dialogs of the save and load commands, or the
   start-up logos), the replacement is documented in the README.
4. **Verification.** The implementation was checked against the verification plan of spec 11 §E:
   - the first ten random numbers match the specification (unit test `rng_first_ten_outputs`);
   - all 31 sample games load: every game file, database and monster picture, and all 64,631 event files parse
     (unit test `samples_all_files_parse`);
   - every save file among the samples loads and saves back to a file of the same size, and a second cycle is
     byte-identical (unit test `samples_save_round_trip`);
   - loading the original runtime's save file of パンダとウサギ and starting its first battle gives the damage
     numbers the original showed, 33 and then 3, which confirms the random-number order of the battle code
     (`tools/check_battle.sh`);
   - every sample game was driven with random input for several minutes, also in builds with AddressSanitizer
     and UndefinedBehaviorSanitizer, without crashes or hangs;
   - the title menu of the original running under Wine has the position and width the specification gives, and
     the Windows build of OpenMK95 was run under Wine (title, loading, menus).
   Independent review passes compared the code with the specification chapter by chapter, and their findings
   were fixed before publication.

## Files

| File | Contents |
|---|---|
| [process-rules.md](process-rules.md) | Rules the implementation worked under (allowed sources, prohibitions) |
| [wine-observation.md](wine-observation.md) | How the original runtime was observed under Wine, with known limitations |
| [spec/](spec/README.md) | The clean-room specification |

## Notes

- The project was called "rm95" during development and was renamed OpenMK95 before publication.
- The specification package also contained `testdata/panda_original_runtime.sav`, a save file written by the
  original runtime for the game パンダとウサギ. It is game data and is not included in this repository;
  `tools/check_battle.sh` takes its path as an argument.
