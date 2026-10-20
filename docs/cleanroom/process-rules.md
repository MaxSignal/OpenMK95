# Process rules

These are the rules the implementation of OpenMK95 followed. They combine the clean-room rules of the
specification (spec 00 §1) with the instructions given to the implementation team.

## Allowed sources

1. The clean-room specification (`spec/`).
2. The data files of RPG Maker 95 games: game files, event files, databases, images, sounds, movies and save files.
   They may be read, parsed and used for testing without restriction.
3. Running the original runtime (GAME.EXE) under Wine and observing it from the outside: what appears on the
   screen, what is heard and how it reacts to input.
4. General-purpose libraries and tools unrelated to RPG Maker 95 (SDL2, TinySoundFont, the Shinonome fonts,
   compilers, Python, Wine, Xvfb).
5. Public documentation of standard file formats and system interfaces: BMP, RIFF/WAVE and its ADPCM variants,
   Standard MIDI Files and General MIDI, AVI and its video codecs, CP932, the Windows and SDL APIs.

## Prohibitions

1. No disassembly, decompilation or debugging of GAME.EXE, and no inspection of its memory while it runs.
2. No extraction of strings, resources or other content from GAME.EXE. (Texts the runtime displays come from the
   specification's appendix, from the games' own `strings.dat`, or from observation of the screen.)
3. No information about RPG Maker 95 from the internet: no reverse-engineering notes, file-format descriptions,
   forum posts or source code of other reimplementations.
4. No code copied from anywhere except the third-party components listed in the README, which are used under
   their licenses and kept in `third_party/` (or downloaded at build time, for SDL2).

## Working practice

- Every behavior is traced to a section of the specification; code comments cite chapter and section (for
  example `05 §7.3`) and mark the documented quirks with `(quirk)`.
- Points the specification marks **UNCERTAIN** are decided by the implementation, kept in one place and easy to
  change.
- Observations under Wine are used to confirm the specification and to check visible results such as window
  positions, not to discover undocumented behavior by probing the program.
- Sample game data and the original executables stay outside the repository.
