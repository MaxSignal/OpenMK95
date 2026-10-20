# RPG Maker 95 runtime — functional specification for a clean-room implementation

This document set describes, in natural language, everything a program must do to behave like the
game runtime of RPG Maker 95 (RPGツクール95), runtime version 1.23, so that unmodified games made with
RPG Maker 95 can be played. It is written for an implementation team (or agent) that has **no access**
to the original runtime, to its disassembly, or to any existing reimplementation.

## 1. Clean-room rules

The specification team produced these documents; the implementation team works only from them.

1. Implementers must not read, run under a debugger, disassemble or decompile the original runtime
   (`GAME.EXE`), and must not look at any existing reimplementation of it or at reverse-engineering
   notes. Game data files of RPG Maker 95 games may be used freely for testing.
2. The documents describe observable behaviour, file formats and required results. They contain no
   program code of the original runtime or of any reimplementation, no addresses or internal names of the
   original program, and no description of its internal memory layout except where a memory image is
   written to a file (save files; described there purely as a file layout).
3. Algorithms are given as prose, numbered steps and formulas. Implementers choose their own
   architecture, names, languages and libraries.
4. Where the documents say "must", the behaviour is required for compatibility — including behaviours
   that look like bugs. These are marked **(quirk)** and are intentional: games may rely on them.
5. Where the specification team was not certain, the text says **UNCERTAIN** and explains what is known.
   Implementers decide those points themselves and should keep them easy to change.
6. Questions from the implementation team go back to the specification team in writing; answers are
   added to these documents, never passed on as code.

## 2. Scope

In scope: the runtime program that plays a game folder: startup and title screen, maps and movement,
events, messages and menus, battles, shops and inns, pictures, screen effects, sound, movies, save files.

Out of scope: the editor (RPG Maker 95 itself), the separate "Simulation RPG Maker 95" product, and the
start-up logos of the original (two publisher logos shown for 3 s each; an implementation may omit them
or show its own).

Target platform behaviour: the original runs on Windows 95 in a 640 × 480 8-bit (256-colour) display
mode, full screen by default. The specification describes the visible result on such a system. An
implementation can run on any modern system as long as the 8-bit palette behaviour described in
`03-display.md` is reproduced.

## 3. Documents

| file | content |
|---|---|
| `00-README.md` | this file: rules, conventions, common terms and the shared game state |
| `01-runtime.md` | program structure, main loop and frame timing, input model, waiting, random numbers, start-up, title, new game, game over, quitting |
| `02-data-formats.md` | every file the runtime reads: the .RPG game file, event files, strings.dat, battle databases, monster pictures, tile attributes, images and naming rules |
| `03-display.md` | the 8-bit screen model: palettes, colour matching, tints and palette effects, screen transitions, pictures, how maps and characters are composed |
| `04-map-movement.md` | maps, tiles, passability, characters, movement, followers, camera, vehicles, random encounters, teleport |
| `05-events.md` | event objects and pages, page conditions, triggers, the event runner and every event command, move routes |
| `06-windows-text.md` | the window system, fonts and text drawing, cursors, messages, choices, pop-ups, keyboard and mouse handling in windows |
| `07-menus-shop-inn.md` | the field menu and all sub-menus, item/magic use outside battle, shops and inns |
| `08-battle.md` | battles: start, command input, turn order, actions, formulas, enemy behaviour, animations, victory, level up, defeat |
| `09-audio-movies.md` | music and sound effects, audio parameters, MIDI looping, movies |
| `10-save-files.md` | the save file format and what loading restores |
| `11-appendix.md` | data tables needed for compatibility (built-in texts, sound-effect names, animation tables), a list of all quirks, and a verification plan |

Each document ends with its open questions.

## 4. Conventions

* All multi-byte numbers in files are **little-endian** unless stated otherwise. `u8/u16/u32` are
  unsigned, `s8/s16/s32` signed. Offsets are given in hexadecimal (`0x10`) from the start of the
  structure being described.
* All text in game files is **Shift_JIS** (Windows code page 932). Text produced by the runtime itself
  is also Shift_JIS. "Characters" of a string means Shift_JIS characters (a lead byte plus trail byte
  counts as one character); "length in characters" is used for window sizing.
* Integer arithmetic is that of 32-bit C: division truncates toward zero, `%` has the sign of the
  dividend, values stored in 16-bit fields wrap modulo 2^16 and are read back as signed. Conversion
  from floating point to integer truncates toward zero. When a formula is given with fields that are
  16-bit, the intermediate computation is 32-bit and the result is truncated on storing.
* Directions: **0 = up, 1 = right, 2 = down, 3 = left**. Step offsets dx = (0, +1, 0, −1),
  dy = (−1, 0, +1, 0).
* The screen is 640 × 480 pixels. A map tile is 32 × 32 pixels; the screen shows 20 × 15 tiles. A
  character moves one tile in **4 sub-steps of 8 pixels**.
* Time: "ms" are milliseconds of real time. "A tenth" means 100 ms. "Ticks" are game logic updates
  (see `01-runtime.md`).
* Rectangles are written (left, top, right, bottom) with right/bottom exclusive, or "x, y, w × h".
* "strings[n]" means line n of the built-in message table (`strings.dat`, `02-data-formats.md`;
  defaults in `11-appendix.md`).
* Japanese text that the runtime displays from built-in tables is given in the appendix; it is
  compatibility data and must be reproduced exactly (as Shift_JIS bytes).

## 5. Shared terms

These terms are used in all documents.

**Game folder** — the folder containing exactly one `*.RPG` file plus the game's data, images and sounds.
File names are matched case-insensitively (Windows semantics).

**Actor** — one of the 8 characters defined in the game database (index 0..7). Actors have fixed
database data (name, start values, growth curves, learnable magic).

**Party** — an ordered list of up to 8 actors. The first up to 4 party members take part in battles
("fighting members"); the others are reserve members. The party member data are kept in 8
**battler records** (layout in `02-data-formats.md`, "Battler record"), stored in party order: records
0..partySize−1 are the members, the remaining records hold the actors that are not in the party.
Equipment and learned-magic tables are kept per actor index (not per party position).

**Leader / hero** — the first party member's map character, or the vehicle while the party rides one.

**Map character** — anything drawn as a sprite on a map: the party members, events, vehicles.

**Event** — an object placed on a map tile (or a vehicle, or a special global event) with up to 4
pages; each page has conditions, appearance, movement settings and a command list.

**Switches** — 1024 on/off flags numbered 0..1023, all off at a new game.

**Item counts** — 100 counters (one per item of the item database), money — one integer.

**Screen state** — what is currently visible, one of:
`BEFORE_START` (−1, nothing shown yet after start/new game), `PICTURE_BEFORE_MAP` (−2, a picture was
shown while in BEFORE_START), `MAP` (0, the map is visible), `PICTURE_OVER_MAP` (1, a picture is drawn
over the map), `PICTURE_ON_BLACK` (2, a picture is shown on a black screen with its own palette),
`ERASED` (3, the screen has been erased by a transition), `MOVIE` (4, a movie with black background
is playing). Transitions between these states are defined in `03-display.md` and `05-events.md`.

**Windows** — rectangular UI elements drawn over the game screen. There are 13 window slots,
identified by number; each slot holds at most one window at a time, and the slot number determines
the window's layout and how selections in it are handled (`06-windows-text.md`):

| slot | used for |
|---|---|
| 0 | message window |
| 1 | battle status window |
| 2 | main menu (field) / command menu (battle) |
| 3 | (unused) |
| 4 | magic list |
| 5 | item list (also equipment candidates) |
| 6 | general list: choices, title menu, party member list, system menu, shop/inn commands, enemy target list in battle |
| 7 | teleport destination list |
| 8 | shop list |
| 9 | status screen |
| 10 | party order display |
| 11 | target / sub selection list |
| 12 | pop-up notice (an implementation may also use it for the version information box) |

**Shared run-time flags** — these global flags are used by several chapters under exactly these names; they
are defined in `01-runtime.md` §1, which also lists everything that sets them:

| name | meaning (short) |
|---|---|
| pass-abort flag | the rest of the current main-loop pass is skipped (teleport, escape outside battle, start of alighting, loading from the menu, quitting, EndGame, the end of a lost battle) |
| arrival flag | set by a teleport; hero input is skipped in every pass, and map-event/vehicle updates in the tick, until the flag is cleared after the next tick's autorun step |
| post-pass action | what to finish after the pass: none / teleported / boarding done / alighting done |
| battle-lost flag | the last battle was lost; leads to game over |
| confirm flag | a confirm key (Enter, Space, mouse) was pressed and not yet consumed |
| release latch | set when all keys have been released; required before a new confirm is accepted |

**Move-route modes** — none, running, repeat, back-once, ping-pong (`04-map-movement.md` §8.3); stored in save
files as 0x1C, 0, 0x1D, 0x1E, 0x1F respectively (`10-save-files.md`).

**Message speed** — a number from the game file (`02-data-formats.md` §3.4; before the game file is read
it is 0). Many waits are "message speed × factor"
tenths of a second; the function `Message(text, factor, position)` is defined in `06-windows-text.md`.

**Base palette** — the committed palette of `03-display.md` §2.2. "Re-apply the base palette" means realising
the committed palette on screen again, without reloading an image and without re-applying the tint.

**Wait(n)** — the basic blocking wait, defined in `01-runtime.md`: waits n tenths of a second (or until
confirmed for n = 0) while keeping the program responsive.

**Random numbers** — one global pseudo-random generator, defined in `01-runtime.md`. Many results
(battle damage, encounters, random movement) must use it **in exactly the documented order** so that
behaviour matches the original for the same inputs.

**Transitions table** — three screen-transition numbers from the game file: index 0 used when changing
maps, index 1 when a battle starts (and when the map comes back after a battle), index 2 when the battle
screen appears and disappears. Event commands can change them.

**Audio slots** — 12 system sound settings from the game file (walking BGM, vehicle BGMs, encounter,
battle, victory, level-up and inn jingles). See `09-audio-movies.md`.

## 6. Notes on the source of this specification

The behaviour described was established by black-box observation and analysis of runtime version 1.23
(the most widespread and last version), cross-checked against 31 games made with RPG Maker 95 data
versions 0.96 to 1.23, and by comparing a test implementation against the original running side by
side. Games made for older runtime versions are expected to run with the 1.23 behaviour; the game file
format differences between data versions are described in `02-data-formats.md`.
