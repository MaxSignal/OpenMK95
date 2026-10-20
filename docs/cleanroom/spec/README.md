# Clean-room specification

The natural-language specification of the RPG Maker 95 runtime (version 1.23) from which OpenMK95 was written.
It is kept here unchanged, as delivered by the specification team. Start with
[00-README.md](00-README.md), which gives the clean-room rules, conventions and shared terms.

| File | Contents |
|---|---|
| [00-README.md](00-README.md) | Rules, conventions, common terms and the shared game state |
| [01-runtime.md](01-runtime.md) | Program structure, main loop and timing, input, waits, random numbers, start-up, title, new game, game over |
| [02-data-formats.md](02-data-formats.md) | Every file the runtime reads, and how file names are resolved |
| [03-display.md](03-display.md) | The 8-bit screen model: palettes, color matching, tints and effects, transitions, pictures, map composition |
| [04-map-movement.md](04-map-movement.md) | Maps, passability, characters, movement, followers, camera, vehicles, encounters, teleport |
| [05-events.md](05-events.md) | Event objects and pages, triggers, the event runner, every event command, move routes |
| [06-windows-text.md](06-windows-text.md) | The window system, fonts and text, cursors, messages, choices, pop-ups, input in windows |
| [07-menus-shop-inn.md](07-menus-shop-inn.md) | The field menu and its sub-menus, item and magic use outside battle, shops, inns |
| [08-battle.md](08-battle.md) | Battles: commands, turn order, actions, formulas, enemies, animations, victory, defeat |
| [09-audio-movies.md](09-audio-movies.md) | Music and sound effects, MIDI looping, movies |
| [10-save-files.md](10-save-files.md) | The save file format and what loading restores |
| [11-appendix.md](11-appendix.md) | Built-in texts, sound-effect names, animation tables, the list of quirks, the verification plan |

The specification mentions a `testdata` folder with a save file written by the original runtime
(`panda_original_runtime.sav`). That file is game data and is not part of this repository.
