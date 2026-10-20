# Engine

The engine (`src/engine`) is the runtime proper. Its files follow the chapters of the specification; the
interfaces between them are declared in `game.h`.

| File | Contents | Spec |
|---|---|---|
| `state.h` | The global game state `Game G`: switches, party, items, map characters, flags | 00 §5, 01 §1 |
| `data.*` | The `.RPG` game file (all data versions), `strings.dat`, databases, event files, monster pictures, tile attributes | 02 |
| `runtime.*` | Message-processing pass, input handling, `Wait(n)`, `Message()`, pop-ups, main loop, title, new game, continue, game over | 01, 06 §9–§11 |
| `display.*` | Palettes, palette effects, presentation, the 15 transitions, frame composition | 03 |
| `ui.*` | The 13 window slots, labels, cursor, keyboard and mouse handling in windows | 06 |
| `map.cpp` | Tiles and passability, characters, movement and followers, camera, vehicles, encounters, teleport | 04 |
| `events.cpp` | Event pages and conditions, triggers, the event runner, every event command, move routes | 05 |
| `menu.cpp` | Field menu and sub-menus, item and magic use outside battle, shops, inns | 07 |
| `battle.cpp` | Battles: command input, turn order, actions and formulas, enemies, animations, victory, defeat | 08 |
| `audio.cpp` | PlayBGM / PlaySE rules, audio slots, jingles, saved BGM | 09 §1–§6 |
| `movieplay.cpp` | Event command "play movie" | 09 §7 |
| `save.cpp` | Save file format, saving and loading, the save/load file list | 10 |

## Main loop and waits

`rt::mainLoop()` implements the main loop of spec 01 §8: each pass runs the message-processing pass, then (at
most every 21 ms) a map tick that moves the hero and the map characters, runs autorun and parallel events and
redraws the map. Event scripts, messages, menus, shops and battles run *inside* this loop as nested loops; each
of them calls `rt::processMessages()` while it waits, so input reaches the right window at the right time, as
in the original. Shared flags such as the pass-abort flag, the arrival flag and the confirm flag (spec 00 §5) are
fields of `G` with the same names.

## Display model

The screen is a 640×480 8-bit surface (`disp::visible`) plus a back buffer and a cache used by transitions and
map scrolling. Images are loaded with their own palettes; the committed palette is what the game "uses" and the
realised palette is what is on the screen. Palette effects (tints, flashes, fades) compute new realised palettes
with the specification's integer formulas. Map drawing composes chip sets and characters into the back buffer and
presents rectangles of it, which is what makes the original's partial redraws and transition artifacts
reproducible.

`disp::compose()` produces the host frame: the visible surface through the realised palette, then the windows
(`ui` provides their layers through `disp::layersProvider`), then a playing movie (`disp::movieOverlay`).

## Windows and text

Each of the 13 window slots of spec 06 holds at most one window; the slot decides layout and how a selection is
handled (`menu::onSelected`, `battle::onSelected`). Windows are drawn into their own 8-bit layers with the
Shinonome font and the reference metrics, so their rectangles match the specification pixel for pixel.

## Events

Event files are parsed on demand into pages and command lists (`data::readEventScript`). `ev::runEvent` runs a
command list; commands that wait (messages, choices, waits, move routes, battles, shops) block inside their own
loops, exactly like the original's event runner. Expansion of `$` codes in texts is in `ev::expandText`.

## Battle

`battle::start()` runs a complete battle as a nested loop: command input in windows 1, 2, 6 and 11, the turn order,
physical attacks, magic and items with the formulas of spec 08, enemy behavior, damage pop-ups and animations,
victory with experience, level-ups and drops, defeat and escape. Every random number is drawn in the documented
order; `tools/check_battle.sh` checks this against a result of the original runtime.

## Save files

`save::buildSave()` writes the save image of spec 10 §1 (including documented quirks such as the duplicated
money field and `(null)` for empty slots); `save::readSaveData()` validates a complete file before applying it.
Loading from the title and from the menu differ as described in spec 10 §2.4. The original's file dialogs are
replaced by a list of the game folder's `*.sav` files (`save::loadDialog`, `save::saveDialog`).
