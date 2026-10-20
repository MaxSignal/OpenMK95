# 10 — Save files

A save file (`*.sav`) holds the persistent game state: position, party, items, switches, vehicles, audio
settings and a few screen settings. It does **not** hold a running event, the events of the current map,
pictures, windows or a battle (§4). This chapter gives the byte-exact layout (§1), how a file is loaded
(§2) and saved (§3), and compatibility notes (§5).

Conventions of `00-README.md` apply: little-endian, no padding or alignment anywhere in the file, Shift_JIS
text. Fields marked **meaningless** contain values that had a meaning only inside the original program's
memory (for example references to other memory); they must be skipped when reading and may be written as
any value (recommendations in §5.3).

---------------------------------------------------------------------------------------------------

## 1. Layout

The file consists of these parts, in this order, with nothing after part 7:

| part | content | size |
|---|---|---|
| 1 | header: identification, version, map, vehicle table, riding flag | 0x8F bytes |
| 2 | vehicle event objects | variable (0 if no vehicles) |
| 3 | party and inventory | 0x160C bytes |
| 4 | audio slots and current BGM | 13 × (12 bytes + text line) |
| 5 | screen settings, teleport list, escape map, movement control | 0x4E0 bytes |

File size = 7204 + (size of part 2) + (sum of the byte lengths of the 13 names in part 4). The files in the
corpus are 7.4–7.7 KB.

### 1.1 Part 1 — header (absolute offsets)

| offset | size | type | content |
|---|---|---|---|
| 0x00 | 30 | bytes | identification: the 9 Shift_JIS bytes of `RPGﾂｸｰﾙ95` (52 50 47 C2 B8 B0 D9 39 35; half-width katakana) followed by 21 bytes 00 |
| 0x1E | 5 | text | version of the runtime that wrote the file, NUL-terminated: `1.23` + 00 for this runtime (older runtimes wrote e.g. `1.21`) |
| 0x23 | 4 | s32 | current map id (0..499) |
| 0x27 | 100 | 10 × 10 bytes | **vehicle table**, entries 0..9 (§1.1.1) |
| 0x8B | 4 | s32 | **riding flag**: 1 while the party rides a vehicle (then the vehicle of entry 0), else 0 |

#### 1.1.1 Vehicle table entry (10 bytes)

| offset | size | type | content |
|---|---|---|---|
| 0x0 | 2 | u16 | event id of the vehicle (the id of the event on its home map); 0 = empty entry |
| 0x2 | 4 | s32 | id of the map the vehicle is on now |
| 0x6 | 4 | u32 | **object marker**: non-zero if a vehicle event object follows in part 2 for this entry, 0 if not. The value is otherwise meaningless. |

The vehicle table is a move-to-front list of all vehicles the party has ever boarded (`04-map-movement.md`,
"Vehicles"): entry 0 is the most recently boarded vehicle; used entries come first.

### 1.2 Part 2 — vehicle event objects

For each vehicle-table entry 0..9 in order whose **object marker is non-zero**, one serialized event object
(variable size) follows; nothing is written for the other entries. (The reader uses a different test, §2.2
step 4.) An event object is, in this order:

| # | size | type | content |
|---|---|---|---|
| 1 | 4 | s32 | x (tile column) |
| 2 | 4 | s32 | y (tile row) |
| 3 | 4 | s32 | destination x (tile the character is moving to; = x when idle) |
| 4 | 4 | s32 | destination y |
| 5 | 4 | s32 | "drawn in the last frame" flag (stale; ignore on reading, write 0 or 1) |
| 6 | 2 | u16 | event id (0 while the party rides this vehicle — the id is then kept in the vehicle table entry) |
| 7 | 64 | 16 × s32 | the 16 character-state integers, in the order of the party character record offsets 0x1A..0x56 (§1.3.2): one-shot facing override, facing, move type, current motion, graphic, unused, sub-step counter, animation timer, animation frame, layer, speed, speed counter, vehicle type, animation type, display, trigger cooldown |
| 8 | 4 | s32 | route length (number of bytes of the current move route) |
| 9 | 4 | s32 | route position |
| 10 | 4 | s32 | route mode (values as in §1.3.2, offset 0x62) |
| 11 | 4 | s32 | route step (+1 or −1) |
| 12 | 4 | u32 | **route marker**: non-zero if the character currently holds a move route; otherwise 0; meaningless apart from zero / non-zero |
| 13 | route length | bytes | the move route bytes — **present only if the route marker is non-zero** |
| 14 | 4 | s32 | resume command index (the command index at which this event continues after waiting for its own route; 0 = not waiting) |
| 15 | 1 | s8 | current page 0..3 (−1, byte FF, = no page) |
| 16 | 0x58 | bytes | the page attribute arrays, byte-identical to bytes 0x0C..0x63 of the event file header (`02-data-formats.md`, "Event files"): trigger u8[4]; condition flags u32[4]; display u8[4]; layer u8[4]; move type u8[4]; animation type u8[4]; speed u8[4]; vehicle type u8[4]; condition item u16[4]; condition party mask u8[4]; condition money u32[4]; condition switch u16[4]; graphic u16[4] |
| 17 | var | 4 × {u8 n; n bytes} | the four page move routes (pages 0..3), each a length byte followed by that many route bytes |

Size of one object = 199 + (route length if the route marker is non-zero) + Σ over the 4 pages of (1 + n).

The meanings of the character-state fields and their value ranges are those of the party character record,
§1.3.2; the meanings of the page attributes and routes are in `05-events.md`. The ordinary events of the
current map are **not** saved.

### 1.3 Part 3 — party and inventory

Offsets relative to the start of part 3 (= 0x8F + size of part 2):

| offset | size | type | content |
|---|---|---|---|
| 0x000 | 4 | s32 | number of party characters shown on the map (= party size, or 1 while riding) |
| 0x004 | 4 | s32 | party size (1..8) |
| 0x008 | 8 | s8[8] | actor index (0..7) of party slot 0..7; entries ≥ party size are stale (0 in the samples) |
| 0x010 | 0x370 | 8 × 0x6E bytes | the 8 **party character records**, indexed by **actor index** (record k = actor k's map character), §1.3.2 |
| 0x380 | 0x1140 | block | the **party block**, §1.3.3 |
| 0x14C0 | 200 | s16[100] | item counts, index = item number 0..99 |
| 0x1588 | 4 | s32 | money (second copy; this copy is the one in effect after loading, §2.2) |
| 0x158C | 128 | bytes | switches 0..1023: switch n = bit (n & 7) of byte (n >> 3), bit 0 = least significant; 1 = ON |

#### 1.3.1 Absolute offsets without vehicles

In files with no vehicle object (part 2 empty), part 3 starts at 0x8F: the party counts are at 0x8F, the
party character records at 0x9F, the party block at 0x40F, items at 0x154F, money at 0x1617, switches at
0x161B, and part 4 at 0x169B.

#### 1.3.2 Party character record (0x6E bytes)

One record per actor (actors that are not in the party keep a record too). The state values and their
meanings are defined in `04-map-movement.md` ("Map characters") and `05-events.md`; this table gives the
file layout, the meaning in brief, and typical values in the corpus.

| offset | type | content | typical |
|---|---|---|---|
| 0x00 | u32 | **meaningless** for reading; write the bytes `50 50 46 00` (§5.3) | 50 50 46 00 |
| 0x04 | s32 | x (tile) | |
| 0x08 | s32 | y (tile) | |
| 0x0C | s32 | destination x | = x |
| 0x10 | s32 | destination y | = y |
| 0x14 | s32 | "drawn in the last frame" flag (stale) | 1 |
| 0x18 | u16 | event id — always 0 for party characters | 0 |
| 0x1A | s32 | one-shot facing override for the next drawing (−1 = none) | −1 |
| 0x1E | s32 | facing 0..3 (directions as in `00-README.md`) | |
| 0x22 | s32 | move type: 5 = player controlled, 6 = boarding, 7 = alighting, 9 = mouse walking | 5 |
| 0x26 | s32 | current motion: −1 idle, 0..3 stepping in a direction, 4..7 turning, 0x11 waiting one step | −1 |
| 0x2A | s32 | graphic (< 0x100: CHARA.BMP character 0..63; ≥ 0x100: map chip + 0x100) | |
| 0x2E | s32 | unused | 0 |
| 0x32 | s32 | sub-step counter 0..4 | 0 |
| 0x36 | s32 | animation timer | |
| 0x3A | s32 | animation frame 0..3 | |
| 0x3E | s32 | layer: 0 below, 1 same level, 2 above | 1 |
| 0x42 | s32 | speed 1..20 (ticks per 8-pixel sub-step) | 1–2 |
| 0x46 | s32 | speed counter | |
| 0x4A | s32 | vehicle type / movement class 0..6 (also selects the field music slot) | 0 |
| 0x4E | s32 | animation type 0x12..0x18 | 0x12 |
| 0x52 | s32 | display: 0x19 normal, 0x1A invisible, 0x1B removed | 0x19 |
| 0x56 | s32 | trigger cooldown | 1 |
| 0x5A | s32 | route length | 0 |
| 0x5E | s32 | route position | 0 |
| 0x62 | s32 | route mode (`04-map-movement.md` §8.3): 0x1C none, 0 running, 0x1D repeat, 0x1E back-once, 0x1F ping-pong | 0x1C |
| 0x66 | s32 | route step (+1 / −1) | 1 |
| 0x6A | u32 | route marker — **meaningless** in these records (the route bytes of party characters are never saved) | 0 |

#### 1.3.3 Party block (0x1140 bytes)

Offsets relative to the start of the block:

| offset | size | type | content |
|---|---|---|---|
| 0x000 | 12 | s32[3] | unused (0 in all samples) |
| 0x00C | 4 | s32 | **fighting party size** = min(party size, 4) |
| 0x010 | 32 | s32[8] | unused (0 in all samples) |
| 0x030 | 0x5C0 | 8 × 0xB8 | the 8 **battler records** of the party, in party order: records 0..party size − 1 are the members in party order, the following records the other actors (`00-README.md`, "Party"); layout below |
| 0x5F0 | 4 | u32 | **meaningless** |
| 0x5F4 | 40 | s8[8][5] | **equipment**, indexed by actor index: for actor k, bytes 0x5F4 + 5k .. + 4 = weapon, armour, shield, helmet, other; each an item number 0..99 or −1 = nothing |
| 0x61C | 800 | u8[8][100] | **learned magic**, indexed by actor index: byte 0x61C + 100k + j belongs to actor k and spell j: the level at which the spell is learned (a copy of the actor database's learn-level table; 0 = not learned by level), or 1 when the spell was taught by an event command. A spell counts as known when 0 < value ≤ the actor's level. See §2.3 for the fix-up done when loading. |
| 0x93C | 44 | bytes | unused (0 in all samples) |
| 0x968 | 4 | s32 | **money** (first copy) |
| 0x96C | 2004 | bytes | unused (0 in all samples) |

Battler record (0xB8 bytes) as used for party members (complete definition: `02-data-formats.md`, "Battler
record"):

| offset | type | content |
|---|---|---|
| 0x00 | s16 | level 1..100 |
| 0x02 | s16 | HP |
| 0x04 | s16 | magic defence (power of the "other" equipment) |
| 0x06 | s16 | max HP |
| 0x08 | s16 | unused |
| 0x0A | s16 | MP |
| 0x0C | s16 | max MP |
| 0x0E | s16 | vitality (体力) |
| 0x10 | s16 | power (力) |
| 0x12 | s16 | agility (素早さ) |
| 0x14 | s16 | intelligence (知力) |
| 0x16 | s16 | luck (運) |
| 0x18 | s16 | defence (防御力) |
| 0x1A | s16 | attack (攻撃力) |
| 0x1C | s16 | sleep counter (0) |
| 0x1E | s32 | experience |
| 0x22 | s32 | unused for party members |
| 0x26 | u8, u8 | unused for party members |
| 0x28 | u16 | status bits |
| 0x2A | s16 | unused |
| 0x2C | char[21] | name (Shift_JIS, NUL-terminated; actor names are at most 18 bytes; bytes after the NUL are meaningless) |
| 0x41 | u8[100] | effect rate (%) of spell i on this battler |
| 0xA5 | 10 bytes | unused for party members |
| 0xAF | s8 | actor index 0..7 |
| 0xB0 | s32 | 0 |
| 0xB4 | s32 | 0 |

### 1.4 Part 4 — audio slots and current BGM

13 records: first the 12 audio slots 0..11 in slot order (`09-audio-movies.md`, "Audio slots"), then the
**current BGM** (`09-audio-movies.md`, "Playing a BGM"). Each record is:

| size | type | content |
|---|---|---|
| 4 | u32 | **meaningless** |
| 4 | s32 | a (attenuation << 16 \| pan) |
| 4 | s32 | b (type, exclusive, target slot, frequency) |
| var | text | the name, as stored (normally the full path the editor wrote, e.g. `B:\RPGﾂｸｰﾙ95\Sys\Bgm\宿.mid`, or `演奏停止`), followed by one byte 0A (LF). No CR, no NUL. A slot that has no name at all (slot 11 of a pre-0.96 game, `02-data-formats.md` §3.3) is written by the original as the 6 characters `(null)` **(quirk)**; an implementation may write `演奏停止` instead. |

The slot records reflect all changes made by event command 49. For the current BGM, a and b are the
parameters most recently recorded by PlayBGM (they are not updated when `演奏停止` was requested, so a
current name `演奏停止` is usually accompanied by the parameters of the previous music).

### 1.5 Part 5 — settings

Offsets relative to the start of part 5:

| offset | size | type | content |
|---|---|---|---|
| 0x000 | 12 | s32[3] | the transitions table, indices 0, 1, 2 (`00-README.md`) |
| 0x00C | 4 | u8[4] | the stored **tint**: red %, green %, blue %, grey (`03-display.md`, §4.6 "Storing the tint"); normal = 100, 100, 100, 0 (bytes 64 64 64 00); one sample has 50, 50, 50, 0 |
| 0x010 | 1024 | 256 × u8[4] | a **palette**: 256 entries {red, green, blue, flags} (flags is 4 or 0 in the samples and has no meaning for loading). Which palette is written: if a map palette is currently remembered because a picture with its own palette is (or was, since the map was last shown) on screen (`03-display.md` §7.4), the **remembered map palette**; otherwise the **committed palette** (`03-display.md` §2.2; it includes a tint committed by a palette effect). |
| 0x410 | 200 | s16[100] | **teleport destination list**: area map ids added by event command 45, in order, terminated by the first value ≤ 0; unused entries 0 |
| 0x4D8 | 2 | s16 | **teleport lock** (event command 46): 1 = teleport magic is forbidden |
| 0x4DA | 2 | s16 | **escape map** (event commands 34 and 54): destination map of escape-type item/magic effects; ≤ 0 = none set |
| 0x4DC | 4 | s32 | **movement control mode** (event commands 36–39): 0 normal, 1 all map events stopped, 2 or 3 player control disabled |

The file ends at offset 0x4E0 of part 5.

---------------------------------------------------------------------------------------------------

## 2. Loading

### 2.1 Opening

The original asks for the file with the system's "open file" dialog: title `ゲームを読み込む`, file type
filter `RPGｹﾞｰﾑﾃﾞｰﾀ (*.sav)` with pattern `*.sav`, default extension `sav` (added when the typed name has
none), the chosen file must exist, the read-only option is hidden, the dialog starts in the program's current folder (normally the game folder) and the current folder
is restored afterwards. Cancelling returns "not loaded". Implementations may use their own file chooser
(§3.3).

Messages (system message boxes with an OK button; title `RPGﾂｸｰﾙ95` in half-width katakana; the runtime
uses Shift_JIS):

* the file cannot be opened: `ファイルを開くことができません` (no icon) → not loaded;
* the identification does not match: `現バージョンでこのデータを` line break `読み込むことはできません`
  (two lines; exclamation icon) → not loaded.

In both cases nothing of the game state has been changed.

### 2.2 Reading

1. Read 30 bytes; treat byte 29 as NUL; compare the bytes up to the first NUL with `RPGﾂｸｰﾙ95` as
   NUL-terminated strings. The file matches if and only if bytes 0..8 are 52 50 47 C2 B8 B0 D9 39 35 and
   byte 9 is 00; bytes 10..29 are not examined.
2. Read the 5-byte version text. **It is not checked** (any version is accepted).
3. If the identification did not match: message (§2.1), stop. Otherwise, before reading on:
   fade the screen to black (the "fade screen to black" operation of `03-display.md` with palette effect
   mode 5; the screen state becomes `ERASED`), delete all events of the current map, delete all existing
   vehicle objects and discard the party character records.
4. Read the current map id, the vehicle table and the riding flag. For each vehicle entry 0..9 in order:
   if its **event id is 0**, the entry is made completely empty (map id 0, no object); otherwise a new
   vehicle event object is created and read from part 2 (§1.2; the route bytes are read when the route
   marker is non-zero). Loaded vehicle objects are not part of any map's event list. **(quirk)** The writer
   decides by the object marker, the reader by the event id (§5.2).
5. Read the party counts and actor indices, the 8 party character records (field 0x00 and the route marker
   are ignored; party characters have no move route after loading — UNCERTAIN what the original does when
   a saved route mode is not 0x1C, see §2.3), the party block, the item counts, the money (this second copy
   replaces the money read with the party block), the switches.
6. Read the 13 audio records. For each: 12 bytes, then a text line: bytes up to and including the first 0A,
   but at most 255 bytes; the **last byte read is removed** (it is assumed to be the 0A) and the rest is
   the name. The 12 slots and the current BGM record {name, a, b} are replaced.
7. Read the transitions table, the tint (it becomes the stored tint), the palette (it becomes the
   **committed palette**, `03-display.md` §2.2; it is not displayed yet), the teleport list, the teleport
   lock, the escape map and the movement control mode.
8. Close the file and apply §2.3. Result: "loaded".

There is no check of the file length; a truncated file leaves the remaining state undefined in the original
(implementations should reject it with the second message of §2.1).

### 2.3 Fix-ups after reading

* No event is waiting for the hero any more (the record of an event suspended until the hero's move route or
  a vehicle action ends is cleared — `05-events.md`, "Suspension").
* No post-tick action (boarding / alighting finalisation, teleport notice) is pending.
* **Re-linking of the party's map characters**: the party bit mask := OR of (1 << actor index of slot i)
  for i = 0 .. party size − 1. If the riding flag is set, party slot 0's map character is the vehicle
  object of entry 0. If it is not set, for every i < party size, party slot i's map character is the
  party character record of the actor in slot i. The leader is the map character of slot 0.
  **(quirk)** When riding, the original re-links only slot 0 and leaves slots 1.. pointing to the characters
  of the session before loading; implementations should link slots 1 .. party size − 1 to the records of
  their actors in this case too (the original's behaviour is undefined here).
* **Learned-magic fix-up** against the actor database (`02-data-formats.md`, actors, "learn level" table):
  for every actor k = 0..7 and spell j = 0..99: let f = the loaded byte, d = the database's learn level of
  spell j for actor k. If f = 1 and d = 0, keep f (a spell taught by an event survives); in every other case
  set the byte to d. So level-learned spells always follow the current database, **(quirk)** and a spell
  that was taught by an event but also has a database learn level becomes learnable only at that level again.
* Party characters have no move route (see step 5). Recommended: if the loaded route mode is not 0x1C, set
  it to 0x1C with position 0 and step 1.

The "saving enabled" setting is not part of the file and keeps its value from before the load **(quirk)**.
The battle action records (`08-battle.md` §0.4) are not part of the file either and are not reset by
loading: they keep their values from before the load (this matters only for the stale-action rules of
`08-battle.md` §3.3).

### 2.4 What the caller does after a successful load

**From the field menu** (system menu "ゲームを読み込む", `07-menus-shop-inn.md`; all windows were closed
before the dialog):

1. Fade the screen to black (mode 5, screen state `ERASED`) — normally no visible change, the loader has
   already done it.
2. Reload the map palette with tint (`03-display.md` §2.4: `CHARA.BMP` is loaded as the committed palette
   and the stored tint, just read from the file, is applied instantly); the palette from the file is
   thereby replaced **(quirk)**.
3. Stop all music (`09-audio-movies.md`, "Stopping"; the current BGM name is not changed).
4. Set the pass-abort flag: the rest of the current main-loop pass is abandoned (`01-runtime.md` §1).
5. Mark the current map as "none" (a value that differs from every map id), so that the following teleport
   rebuilds the map completely.
6. Take the leader's x, y (from the map character of party slot 0 after re-linking: the vehicle when riding).
7. PlayBGM(current BGM name, current {a, b}, **force**) — the music saved in the file starts from its
   beginning (`演奏停止` = silence).
8. Teleport the party to (saved map id, x, y) (`04-map-movement.md`, "Teleport"): the map's events are
   created anew from the event files (vehicles in the vehicle table are excluded from the map's events),
   all party characters are placed on the leader's tile, and the map is drawn and shown with the
   map-change transition (transitions table index 0), screen state `MAP`.

**From the title screen** ("続きから", `01-runtime.md`, "Title"): if the dialog is cancelled or loading
fails, the title screen is shown again (redrawn, without waiting for a key first). On success:

1. Mark the current map as "none".
2. Teleport to (saved map id, leader x, leader y) as above. The screen is ERASED (the loader faded it), so
   showing the map realises the committed palette, which is the palette read from the file
   (`03-display.md` §7.2) — **(quirk)** unlike the menu path, the file's palette is shown as it is and the
   stored tint is not re-applied; the stored tint takes effect only at the next palette reload that
   applies it (for example after the next battle, `03-display.md` §2.4, §8.1).
3. PlayBGM(current BGM name, current {a, b}, **force**).
4. Realise the committed palette again (`03-display.md` §2.3; no visible change).
5. Flush keys (no wait, `01-runtime.md` §5.2), last key = −1, and continue with the main loop
   (`01-runtime.md` §9.5).

Any event that was running when the game was saved (via event command 52) is not resumed; events start
again only through their triggers (autorun events of the new map start as usual).

---------------------------------------------------------------------------------------------------

## 3. Saving

### 3.1 When saving is possible

* **System menu** "ゲームを保存する" (`07-menus-shop-inn.md`, "System"): the entry is shown disabled (and
  cannot be chosen) when the **saving-enabled setting** is off, or when the leader currently has a move
  route (route mode ≠ 0x1C, i.e. a route given by event command 33 is still running). The
  saving-enabled setting starts with the value of the game-file flag (`02-data-formats.md`, §3.4,
  flags bit 17: 1 = saving allowed) and is changed by event command 52 (operand 11 = disable, 12 = enable).
  All windows are closed before the save dialog appears.
* **Event command 52** with any other operand opens the save dialog directly — regardless of the
  saving-enabled setting and of the leader's route — and the event continues after the dialog closes
  (`05-events.md`, code 52). The event's own progress is not saved.

### 3.2 Writing

The original uses the system's "save file" dialog: title `ゲームを保存する`, filter
`RPGｹﾞｰﾑﾃﾞｰﾀ (*.sav)` with pattern `*.sav`, default extension `sav` (added when the typed name has
none), the read-only option hidden, and the system's standard "file already exists — replace it?"
confirmation (text supplied by the operating system) when an existing file is chosen. It starts in the
program's current folder; the current folder is restored afterwards.

Loop:
1. Show the dialog. Cancel → finish (nothing written).
2. Create/truncate the chosen file for binary writing. If that fails: message box
   `データを書き込むことができません` (title `RPGﾂｸｰﾙ95`, OK, no icon), then go to step 1 (the dialog
   appears again).
3. Write the file exactly as in §1 using the current state, close it, finish. Write errors after the file
   was opened are not detected.

The writer's choices for the variable parts:
* vehicle objects: for each vehicle entry that holds a vehicle object (marker ≠ 0);
* route bytes of a vehicle object: when it currently holds a move route;
* palette: §1.5;
* names: as currently stored in the slots and the current BGM record.

### 3.3 Substitute file choosers

Implementations may replace both dialogs with their own file selection (for example a list of the `*.sav`
files in the game folder whose identification matches, plus a "new file" entry for saving). They should
keep: the `.sav` extension, a confirmation before overwriting, the three messages above for the same
conditions, and the retry after a write failure.

---------------------------------------------------------------------------------------------------

## 4. State that is not saved

Not saved (reset or re-created after loading): the running event and any suspended event; all events of the
maps (re-created from their files with their initial state; only vehicles persist); pictures and the screen
state (the map is shown after loading); message, menu and other windows; the saved-BGM record of
`09-audio-movies.md`; the play-once flag; the encounter step counter (reset by the teleport); the follower
trail (reset by the teleport); the battle flags (saving is impossible during battles).

Not saved and **not** reset by loading (they keep their values from before the load): the saving-enabled
setting (§2.3) and the battle action records (`08-battle.md` §0.4). The random-number generator is not saved
either and simply continues (`01-runtime.md` §6).

---------------------------------------------------------------------------------------------------

## 5. Compatibility notes

### 5.1 Older runtimes

* Files written by older runtimes with the same identification load normally: the corpus contains files
  written by versions 1.21 and 1.23 and they have identical layouts. The version text is ignored.
* Files without the identification (very old runtimes wrote files that start directly with the map id) are
  rejected with the "現バージョンで…" message. They need not be supported.

### 5.2 Writer/reader asymmetry for vehicles

The writer emits a vehicle object for every entry with an object marker ≠ 0; the reader expects one for every
entry with an event id ≠ 0. In every state in which saving is possible the two coincide (an entry has an
object exactly when its id is non-zero; only during the few frames of a boarding animation is entry 0 an
object with id 0, and saving during those frames is not normally reachable — UNCERTAIN whether a field menu
or an autorun event with command 52 could get there). An implementation must
write files where both tests agree; a file where they disagree is misread by the original from that point on.

### 5.3 Meaningless fields

When writing, the meaningless fields can be filled as follows:

* vehicle object marker: 1 for entries with a vehicle object, else 0; route markers: 1 / 0 likewise;
* audio record first 4 bytes, party block offset 0x5F0, unused areas: 0;
* party character record offset 0x00: write the bytes **`50 50 46 00`** in all eight records. This is the
  value found in every record of the corpus files written by runtimes 1.21 and 1.23 (`KNightBlade/01.sav`,
  `ninomai/aaa.sav` and the test file written by the original 1.23); the original runtime takes these 4 bytes
  over unchanged when it loads a file, so a file meant to load in the original needs exactly this value.
  The route marker at 0x6A must be 0. (UNCERTAIN: loading an implementation's file in the original was not
  tested; one corpus file with version text 1.23, `ﾃﾞﾊﾞｯｸﾞ.sav` of ドンザレス, has `6C 16 4D 00` there and
  was apparently written by a different build.) When reading, ignore the value.

When reading, ignore all of them (except the zero / non-zero meaning of the markers).

### 5.4 Verification

The layout of §1 was checked by parsing the following files completely (each is consumed exactly to its
last byte):

| file | size | version | map | vehicles | party | notes |
|---|---|---|---|---|---|---|
| `knight/KNightBlade/01.sav` | 7470 | 1.21 | 4 | none | 2 (actors 0, 1) | money 0, current BGM `…\Prison.mid` a = 0x32, b = 2; tint 100/100/100 |
| `ninomai/ninomai/aaa.sav` | 7436 | 1.23 | 61 | none | 2 (actors 0, 1) | money 685 in both copies; slot 8 b = 0x802 (target bits set); tint 50/50/50; palette flags 0 |
| a file written by the original 1.23 for test purposes (パンダとウサギ) | 7629 | 1.23 | 7 | none | 3 (actors 0, 1, 2) | money 5000; WAV slots 7–11 |
| `08donzares/…/ﾃﾞﾊﾞｯｸﾞ.sav` | 7686 | 1.23 | 0 | entry 0: id 2082, map 0, object at (73, 60), page 1, no routes | 2 (actors 0, 2) | 203-byte vehicle object; slot 7 b = 0x019A0005 |

In all of them: identification bytes 10..29 are 00, the riding flag is 0, the party character records have
move type 5, speed 1–2, animation type 0x12, display 0x19, route mode 0x1C, the unused areas of the party
block are 0, and the file has no trailing bytes.

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* §2.2 — only the first 10 bytes of the identification are compared; the version text is never checked.
* §2.2 — the text-line reader drops the last byte read, assuming it is the LF; names of 255 bytes or more
  desynchronise the rest of the file.
* §2.2, §5.2 — vehicle objects are written by object marker but read by event id.
* §2.3 — when riding, only party slot 0 is re-linked by the original.
* §2.3 — learned magic is overwritten from the database except event-taught spells with database level 0.
* §2.4 — loading from the menu discards the saved palette (CHARA.BMP reloaded with the stored tint); loading from the title uses it.
* §1.3 — money is stored twice; the second copy wins.
* §1.4 — the current BGM's a, b are not updated by `演奏停止`.
* §1.4 — a slot without a name is written as the text `(null)`.
* §2.3, §4 — the saving-enabled setting and the battle action records keep their values across a load.
* §2.4 — loading from the title shows the saved palette as it is; the stored tint is not re-applied until the next palette reload.
* §3.1 — event command 52 saves even when saving is disabled.

## Open questions

* §2.3 — what the original does with a party character whose saved route mode is active (its route bytes
  are not in the file); only possible for saves made by event command 52 during a hero move route.
* §1.3.3 — the unused areas of the party block (0x000–0x00B, 0x010–0x02F, 0x93C–0x967, 0x96C–0x113F) are
  zero in all samples; no use is known.
* §5.3 — whether files written by an implementation load in the original runtime.
* §2.2 — behaviour of the original with truncated files.
