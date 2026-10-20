# Observing the original runtime under Wine

The original runtime was run under Wine on Linux for black-box comparison: starting a game, looking at the
screen and pressing keys. Nothing else was done with the executable (see [process-rules.md](process-rules.md)).
This page records the setup and its limitations so that observations can be repeated.

## Setup

- **Wine:** a 32-bit Wine prefix. GAME.EXE is a 32-bit program; helper programs started inside the prefix must
  also be 32-bit (`i686-w64-mingw32-gcc`).
- **File names:** game archives unpacked on Linux often keep Shift_JIS file names as raw bytes. Wine cannot see
  such names, so the observations used a copy of the game folder with the names converted to UTF-8, and Wine
  was run with a UTF-8 locale (for example `LANG=C.UTF-8`).
- **Fonts:** without the Japanese fonts the runtime asks for, Wine draws boxes instead of text. A font
  replacement in the Wine registry (`HKCU\Software\Wine\Fonts\Replacements`, mapping the MS Gothic family to an
  installed Japanese font such as IPAPGothic) fixes this. Glyph shapes and widths then differ from the original
  font, so text widths observed under Wine are not exact.
- **Display mode:** at start the runtime switches the display to 640×480 with 256 colors unless Shift is held
  (spec 01 §2.3). Under a normal X server this mode switch fails. Two workarounds were used:
  - `tools/wine/shiftlaunch.c`, a small launcher that holds Shift while it starts the program, so that the
    runtime opens its windowed mode;
  - an 8-bit Xvfb server (for example `Xvfb :98 -screen 0 640x480x8`), on which the full-screen mode works. Screenshots of
    an 8-bit X server must be taken through the colormap Wine installs; `tools/wine/xcap.c` captures the root
    window through a given colormap to a PPM file (ordinary screenshot tools showed wrong colors).

## What was observed

- The title screen and its menu: position and width of the menu window matched the specification's layout
  tables.
- The Windows build of OpenMK95 itself, run under the same Wine setup (title, loading a save file, menus), to
  check the Windows-specific code paths (file names, message boxes).

## Limitations

- Font replacement changes text widths, so only layout that does not depend on glyph widths can be compared
  exactly.
- Audio under Wine depends on the host's audio and MIDI setup and was not used for comparisons.
- Timing under Wine and Xvfb is not reliable enough for millisecond comparisons; timing behavior comes from the
  specification.
