# 02 — Data formats and file lookup

This chapter describes every file the runtime reads, byte by byte, and how the runtime finds each file.
Behaviour of the data (what a field does in the game) is described in the chapters named in each section;
this chapter gives the layout, the value ranges, and what the runtime does with missing or malformed files.

Conventions are those of `00-README.md` §4: little-endian, `u8/u16/u32/s8/s16/s32`, offsets in hexadecimal
from the start of the structure being described, Shift_JIS text. "NUL" is the byte 0x00, "LF" the byte 0x0A,
"CR" the byte 0x0D.

Facts marked "(sample games)" were checked against the 31 sample games (31 `.RPG` files, 64 631 event files,
31 copies of each database file).

---------------------------------------------------------------------------------------------------------------

## 1. Overview of the files read

| file | when it is read | if missing | section |
|---|---|---|---|
| `*.RPG` (exactly one expected) | at start-up | runtime does not start (`01-runtime.md`) | §3 |
| `strings.dat` | at start-up, right after the game file | built-in defaults (`11-appendix.md`) | §5 |
| `EPARTY.DAT` | at start-up | error box, program ends | §6.5 |
| `MONSTPIC.DAT` | scanned at start-up; records read at every battle start | error box, program ends (start-up); error box, battle skipped (battle) | §6.6 |
| `CHARA.DAT` | at start-up | records undefined (use zeros) | §6.2 |
| `ITEM.DAT` | at start-up | records undefined (use zeros); MAGIC.DAT is then not read either | §6.3 |
| `MAGIC.DAT` | at start-up (only if ITEM.DAT was read) | records undefined (use zeros) | §6.4 |
| `<chipset name>.ATR` (4 files) | once at start-up (after the map graphics, before the logos), for the start map's chipset names | 256 zero bytes | §7 |
| `eveNNNNN.dat` | header part: when a map's events are created; whole file: every time the event starts | event not created / not run | §4 |
| `CHARA.BMP`, `<chipset name>.BMP` (4) | at start-up, after every battle, after a screen-mode switch and when lost display surfaces are restored | see §8.4 | §8 |
| `title.bmp` | title screen | no title picture | §8 |
| `bgNN.bmp` | battle start | error box, battle skipped | §8 |
| `AnmNN.bmp` | when a battle animation is played | error box, animation skipped | §8 |
| `BG01.BMP` | palette reload while in battle (quirk, `03-display.md`) | palette unchanged | §8 |
| picture files (names from event data) | event command Show picture | command does nothing | §8, `05-events.md` |
| WAV / MIDI files (names from game data), built-in sound effects | when played | nothing is played | §2.4, `09-audio-movies.md` |
| movie files (names from event data) | event command Play movie | command does nothing | `09-audio-movies.md` |
| save files `*.sav` | "続きから" / load | see `10-save-files.md` | `10-save-files.md` |
| `suicide.exe` | at start-up, existence only | (presence prevents starting, `01-runtime.md`) | — |

Files that exist in game folders but are **not read** by the runtime: `SWNAME.DAT` (switch names for the
editor), `PARAM.DAT`, `COPYBUF.*`, and any other editor file.

---------------------------------------------------------------------------------------------------------------

## 2. Locating files

### 2.1 The game folder and the game file

The **game folder** is the folder that contains the runtime program and the game data. Every file the runtime
reads is looked up directly in this folder (no sub-folders), with the single exception of tile attribute files
whose stored name carries a drive letter (§7). An implementation lets the player choose the game folder.

The game file is found by enumerating the files of the game folder whose name ends in `.rpg` (any letter case;
the sample games use both `.RPG` and `.rpg`). If several match, the original uses the **last** one returned by
the directory enumeration (an order defined by the file system, UNCERTAIN; an implementation should choose
deterministically and may warn). If none matches, the runtime does not start (`01-runtime.md`).

### 2.2 Name matching

File names are matched **case-insensitively** as on Windows: the ASCII letters A–Z and a–z are equal. The
comparison must be done per Shift_JIS character: a trail byte in the range 0x41–0x5A or 0x61–0x7A (for
example the second byte of `ア` = 0x83 0x41) is part of a double-byte character and must not be case-folded.
Whether full-width Latin letters (`Ａ`/`ａ`) are folded is UNCERTAIN (no sample game depends on it).

Names on the host file system may be stored in another encoding (for example UTF-8); an implementation must
convert them so that a Shift_JIS name from the game data matches the corresponding file. The sample games
contain file names with kanji, full-width and half-width katakana (for example `ﾋｰﾛｰｼﾝﾄﾞﾛｰﾑ.rpg`).

### 2.3 Kinds of file references

1. **Fixed names**: `strings.dat`, `EPARTY.DAT`, `MONSTPIC.DAT`, `CHARA.DAT`, `ITEM.DAT`, `MAGIC.DAT`,
   `CHARA.BMP`, `title.bmp`, `BG01.BMP`, `suicide.exe`.
2. **Numbered names**, formed with a decimal number padded with zeros:
   * event files: `eve` + 5 digits + `.dat` (`eve00000.dat`, `eve00017.dat`, `eve99999.dat`);
   * battle backgrounds: `bg` + at least 2 digits + `.bmp`, number = troop background value + 1 (§6.5);
   * battle animations: `Anm` + at least 2 digits + `.bmp` (`08-battle.md`).
   A number with more digits than the field width is written in full (for example `bg100.bmp`).
3. **Chipset names** from the map record (§3.8): the name is used as it is, with `.BMP` (images) or `.ATR`
   (attributes, §7) appended, inside the game folder. No base-name reduction is applied to chipset names.
4. **Names from game data** that the runtime reduces to a base name (§2.4) and then looks up in the game
   folder: the 12 audio-slot names of the game file (§3.3), every file name taken from an event's string list
   (music, sound effects, pictures, movies; `05-events.md`), and the built-in sound-effect names
   (`11-appendix.md`; the original forms "<program folder>\SYS\SE\<name>" first, which the reduction turns into
   just `<name>`, so built-in sound effects are also loaded from the game folder).

For kind 4 the runtime first checks that the file can be opened. If not, the action that needed it silently
does nothing (no message).

### 2.4 Base-name reduction (byte-wise)

Names in game data are often absolute paths of the author's machine, for example
`C:\Program Files\Ascii\RPGﾂｸｰﾙ95\Sys\Bgm\jp_btl.mid` or `E:\rpg\mid\TOMO.mid` (sample games: 112 of the
372 audio-slot names and nearly all event file names contain a backslash). The runtime reduces such a name as
follows:

1. Search the name **byte by byte** for the byte 0x5C (`\`). The search does not know about Shift_JIS.
2. If there is no 0x5C byte, the base name is the whole name.
3. Otherwise the base name is everything after the **last** 0x5C byte.
4. The file looked up is `<game folder>` + base name.

**(quirk)** Because the search is byte-wise, the trail byte 0x5C of a double-byte character also counts as a
separator. Shift_JIS characters whose second byte is 0x5C include `―`(81 5C), `ソ`(83 5C), `Ы`(84 5C),
`噂`(89 5C), `浬` `欺` `圭` `構` `蚕` `十`(8F 5C) `申` `曾` `箪` `貼` `能`(94 5C) `表`(95 5C) `暴`(96 5C)
`予`(97 5C) `禄` `兔`, and in general every character with lead byte 0x81–0x9F or 0xE0–0xFC and trail byte
0x5C. The part of the name up to and including such a byte is dropped, even if the name contains no real
directory separator. Examples:

* `C:\Program Files\RPGﾂｸｰﾙ95\商王伝\カーソル1.wav` → `ル1.wav` (the game 商王伝 ships a file named
  exactly `ル1.wav`; the authors adapted to this behaviour);
* `H:\RPGﾂｸｰﾙ95\Sys\Bgm\クッソーーーー！！.mid` → `ーーーー！！.mid` (shipped under that name by the game);
* `E:\Program Files\RPGﾂｸｰﾙ95\スグル\予告挨拶.bmp` → `告挨拶.bmp`;
* `ソード.wav` (no path) → `ード.wav`.

This reduction must be reproduced exactly; a Shift_JIS-aware reduction would look for files under names that
the original never used. Half-width katakana (single bytes 0xA1–0xDF) are not affected.

Only the byte 0x5C is a separator for the reduction. A `/` in a name is not removed by the reduction (on
Windows it would then act as a sub-folder separator when the file is opened; no sample game contains one).

### 2.5 Reading a text line

Several places read text lines from binary files (the game file and `strings.dat`). A **line read** is
defined as:

1. Read bytes one at a time, storing them, until either an LF byte has been stored, or 255 bytes have been
   stored, or the end of the file is reached. (There is no translation: CR bytes are ordinary bytes, the byte
   0x1A is an ordinary byte.)
2. If not a single byte could be read because the file was already at its end, the read **fails** and the
   destination keeps its previous content.
3. The result string is the stored bytes up to the first NUL byte (if a NUL was read, everything after it is
   ignored).
4. Then the **last byte of that string is removed** (whatever it is). An empty string stays empty.

Consequences: a line ending in LF loses its LF; a line ending in CR LF keeps the CR **(quirk)**; the last line
of a file that has no final LF loses its last real byte **(quirk)**; a line longer than 255 bytes is split: the
first 255 bytes become one string (minus its last byte, which is lost) and the rest is the next line read
**(quirk)**; a removed last byte may be half of a double-byte character.

---------------------------------------------------------------------------------------------------------------

## 3. The .RPG game file

### 3.1 Overall layout

The file is read sequentially from the start; there is no table of contents.

| order | size (bytes) | content | section |
|---|---|---|---|
| 1 | 30 | header text | §3.2 |
| 2 | 5 | version text | §3.2 |
| 3 | variable | 11 or 12 audio entries | §3.3 |
| 4 | 12 | transitions table: 3 × s32 | §3.4 |
| 5 | 21 | coin name | §3.4 |
| 6 | 4 | item maximum, s32 | §3.4 |
| 7 | 4 | flags word, u32 | §3.4 |
| 8 | 7464 (= 8 × 0x3A5) | 8 actor records | §3.5 |
| 9 | 200 | start item counts: 100 × s16 | §3.6 |
| 10 | 4 | start money, s32 | §3.6 |
| 11 | 12 | start map id, start x, start y: 3 × s32 | §3.6 |
| 12 | variable | map records until the end of the file | §3.7, §3.8 |

### 3.2 Header and version

**Header** (30 bytes at offset 0x00): a NUL-padded Shift_JIS text. Two forms occur in the sample games:

* half-width katakana `RPGﾂｸｰﾙ95`: bytes `52 50 47 C2 B8 B0 D9 39 35` followed by 21 NUL bytes (data versions
  1.11, 1.21, 1.22, 1.23);
* full-width katakana `RPGツクール95`: bytes `52 50 47 83 63 83 4E 81 5B 83 8B 39 35` followed by 17 NUL bytes
  (data versions 0.96 and 1.20).

The runtime does **not check** the header of the game file; any 30 bytes are accepted. (The runtime's own name
string, used as the caption of its message boxes and as the identification of save files, is the half-width
form `RPGﾂｸｰﾙ95`.)

**Version** (5 bytes at offset 0x1E): ASCII, normally 4 characters and a NUL, for example `31 2E 32 33 00`
= "1.23". Versions in the sample games: 0.96 (7 games), 1.11 (2), 1.20 (5), 1.21 (3), 1.22 (1), 1.23 (13).

The version text is the 5 version bytes up to the first NUL. If the 5 version bytes contain no NUL, the
version text continues with the header bytes at file offsets 0x05 … 0x1C, up to the first NUL among them
**(quirk)**; with the half-width header that adds the bytes `B0 D9 39 35` ("ｰﾙ95"). Its numeric value is obtained like a C
decimal-number conversion of the leading part (leading spaces skipped, digits, optional point and digits;
anything else ends the number; no number gives 0).

**Effect of the version on parsing:** if the numeric value is **less than 0.96**, the file contains **11**
audio entries; otherwise it contains **12**. Nothing else in the file depends on the version. The version
text is used only for this test (save files carry the runtime's own version, `10-save-files.md` §1.1).

### 3.3 Audio entries

Each audio entry is:

| off | size | content |
|---|---|---|
| 0x00 | s32 | parameter word A |
| 0x04 | s32 | parameter word B |
| 0x08 | variable | file name: one text line (§2.5; the LF is removed) |

The entries fill audio slots 0, 1, 2, … in order (slot meanings in `00-README.md` and `09-audio-movies.md`:
0 walking, 1 wagon, 2 sled, 3 boat, 4 ship, 5 balloon, 6 airship, 7 encounter, 8 battle, 9 victory,
10 level-up, 11 inn). When only 11 entries are present, slot 11 stays without a name (see below).

Parameter words as stored (exact use in `09-audio-movies.md`):
* A: bits 0–15 = pan 0..100 (50 = centre), bits 16–31 = attenuation 0..100 (0 = full volume).
* B: bits 0–1 = type (0 none, 1 WAV, 2 MIDI, 3 CD audio), bit 2 = exclusive sound effect, bits 8–15 = used
  only in event commands, bits 16–31 = WAV playback frequency in units of 70 Hz (0 = original frequency). Other
  bits are ignored.

Before the file is read every slot has **no name** (not even an empty one) and A = B = 0. When only 11
entries are present, slot 11 therefore stays without a name: the ordinary slot playback does nothing for a
slot without a name, so a pre-0.96 game has no inn sound (the inn's own test of the name is UNCERTAIN in
this case, `09-audio-movies.md` §1.2). When such a game is
saved, the original writes the text `(null)` as that slot's name (`10-save-files.md` §1.4) **(quirk)**. An
implementation may instead give slot 11 the name `演奏停止` with A = B = 0 (no sample game is older than
0.96). (Default values sometimes quoted for these fields, such as the names `WALK.wav` … `REST.wav`,
transitions 5, 12, 14, message speed 30, item maximum 20 and coin name `ｺﾞｰﾙﾄﾞ`, are never used by runtime
1.23 and must not be applied.)

In the sample games the "no sound" entries carry the name `演奏停止` (Shift_JIS `89 89 91 74 92 E2 8E 7E`) or
`NOSOUND.WAV` with type 0; names of real entries are usually absolute paths (reduced as in §2.4).

### 3.4 Transitions, coin name, item maximum, flags

| field | size | content |
|---|---|---|
| transitions | 3 × s32 | the transitions table, index 0 map change, 1 battle start, 2 battle screen (`00-README.md`, `03-display.md`) |
| coin name | 21 bytes | Shift_JIS, NUL-terminated (up to 20 bytes of text); bytes after the NUL are garbage and ignored. The name of the currency, shown after amounts of money. If the 21 bytes contain no NUL, the name is all 21 bytes. |
| item maximum | s32 | the maximum count of each item in the bag (used by item gain commands, shops, battle drops). Sample values 8..100. |
| flags | u32 | bits 0–15: **message speed** (unsigned, `00-README.md`); bit 16: **transparent windows** (initial value, `06-windows-text.md`); bit 17: **saving enabled** (initial value at program start; an event command can change it, `07-menus-shop-inn.md`, `05-events.md`); all other bits are editor settings and are ignored. |

Fields not present in a truncated file stay 0 (transitions 0, 0, 0; message speed 0; item maximum 0; flags 0;
coin name empty); there are no other defaults.

Sample games: message speed 1..145 (20 is most common); upper flag halves 0x02, 0x23, 0x40–0x43, 0x53, 0x63,
0x65.

### 3.5 Actor records (8 × 0x3A5 bytes)

Actor a (0..7) is the a-th record. Layout of one record:

| off | type | content |
|---|---|---|
| 0x000 | s32 | map graphic: character index 0..63 in `CHARA.BMP` (`04-map-movement.md`) |
| 0x004 | s32 | start party member: non-zero = in the party at a new game |
| 0x008 | char[18] | name, Shift_JIS, NUL-terminated (sample games: at most 18 bytes of text, the NUL then lies in the next field) |
| 0x01A | s16 | unused (garbage) |
| 0x01C | s32 | start level 1..100 |
| 0x020 | s32 | start HP (also start maximum HP) |
| 0x024 | s32 | start MP (also start maximum MP) |
| 0x028 | s32 | start 体力 (vitality) |
| 0x02C | s32 | start 力 (power) |
| 0x030 | s32 | start 知力 (intelligence) |
| 0x034 | s32 | start 素早さ (agility) |
| 0x038 | s32 | start 運 (luck) |
| 0x03C | s32 | start 攻撃力 (attack) |
| 0x040 | s32 | start 防御力 (defence) |
| 0x044 | u8[100] | growth curve 力 (power): value at level L is byte L−1 |
| 0x0A8 | u8[100] | growth curve 体力 (vitality) |
| 0x10C | u8[100] | growth curve 知力 (intelligence) |
| 0x170 | u8[100] | growth curve 素早さ (agility) |
| 0x1D4 | u8[100] | growth curve 運 (luck) |
| 0x238 | 5 × 4 × (s32 x, s32 y) | editor curve control points (power, vitality, intelligence, agility, luck); ignored |
| 0x2D8 | s8[5] | start equipment: weapon, armour, shield, helmet, other ("その他"); ITEM.DAT index 0..99, −1 = nothing |
| 0x2DD | u8[100] | learn level of spell i (MAGIC.DAT index i): the actor knows spell i when 0 < value ≤ current level |
| 0x341 | u8[100] | magic damage rate of spell i on this actor, in percent (0..255; 100 = normal) |
| 0x3A5 | | end |

Curve values are 0..255 (the editor writes 0..100). Level-up rules are in `08-battle.md` (Experience and level
up).

**The runtime uses the start values as stored and does not recompute them.** (Sample games, all 248 actors:
the start values of 体力/力/知力/素早さ/運 equal the curve value at the start level; HP = 体力 × 10;
MP = (知力 − 1) × 10; attack = 力 + weapon power; defence = 体力 + armour + shield + helmet power — with one
exception, an actor edited by hand. An implementation must not "repair" such records.)

How these fields initialise the 8 battler records and the per-actor equipment and learn-level tables at
program start is described in `01-runtime.md`, Party initialisation at program start. All values are stored
into the 16-bit battler fields by keeping their low 16 bits.

### 3.6 Start items, money and position

| field | type | content |
|---|---|---|
| start items | s16[100] | count of item i (ITEM.DAT index) at a new game; not clamped to the item maximum |
| start money | s32 | money at a new game |
| start map | s32 | map id where the party starts |
| start x, start y | s32, s32 | start tile |

### 3.7 Map records

After the start position, the file contains map records until its end. Each record is read as follows:

1. Try to read a 4-byte s32 **map id**. If fewer than 4 bytes remain, the list ends (no error).
2. Read the **0x51-byte map record** (table below) into slot *map id*. (The original also notes that a record
   was read, but nothing uses that note. Wherever the existence of a map matters, for example for a
   teleport, a map counts as existing when its width is non-zero, `04-map-movement.md` §1.1.)
3. If the record's type is not 2 (area), read the map's tile data (§3.8) directly after it.

Map ids are 0..499 (the runtime keeps 500 map slots). The original does not check the range; an id outside
0..499 corrupts its memory. An implementation should stop reading (or ignore the record) and may warn.
Sample games: every game has map 0; ids appear in ascending order; 2 918 records (2 758 normal, 27 looping,
133 areas).

Before reading, every map slot is "empty": type 0, area position (−1, −1), size 0 × 0, parent −1, sibling
and child 0, exit (0, 0), encounter table all 0, no chipset names, no tiles.

**The 0x51-byte map record:**

| off | type | content | used for |
|---|---|---|---|
| 0x00 | char[21] | map name, Shift_JIS, NUL-terminated (see below) | teleport destination list (`07-menus-shop-inn.md`) |
| 0x15 | s32 | depth in the editor's map tree (0 = root); ignored | — |
| 0x19 | s32 | map type: 0 normal, 1 looping (wraps in both directions), 2 area | `04-map-movement.md` |
| 0x1D | s32 | area only: left x of the area rectangle in the parent map (other maps: −1 or 0) | area id per tile |
| 0x21 | s32 | area only: top y of the area rectangle in the parent map | |
| 0x25 | u16 | width in tiles (area: rectangle width) | |
| 0x27 | u16 | height in tiles (area: rectangle height) | |
| 0x29 | s32 | parent map id, −1 = root map | escape / teleport magic |
| 0x2D | u16 | next sibling map id (same parent), 0 = none | area id per tile |
| 0x2F | u16 | first child map id, 0 = none | area id per tile |
| 0x31 | s32 | editor tree display state; ignored | — |
| 0x35 | s32 | exit x: for an area, the tile in the **parent** map to which escape/teleport magic moves the party (the editor stores a value for every map; only areas use it) | `04-map-movement.md`, `08-battle.md` |
| 0x39 | s32 | exit y (same) | |
| 0x3D | u16[10] | encounter table, see below | `04-map-movement.md` (Random encounters) |
| 0x51 | | end of record | |

**Map name:** the name is the NUL-terminated string starting at offset 0x00. Names normally end before 0x15
(sample games: 2 911 of 2 918), but the editor allows a name to fill all 21 bytes; the string then continues
into the depth field at 0x15 **(quirk)**: for a root map its low byte is 0 and ends the string, for a deeper map
the string gets one more byte with the depth value (for example 0x02) before the following NUL. Implementations
must keep that byte (it is drawn like any other character, `06-windows-text.md`).

**Encounter entry** (each of the 10 u16 values v): troop number = v & 0x1FF (1-based EPARTY.DAT record
number, 0 = empty entry); average steps = v >> 9 (0..127). Sample values of the steps field: 8..60 mostly.

**Map tree:** the parent / first-child / next-sibling links form the editor's tree. Every child link points
to a map whose parent is this map, and the sibling chain lists the other children of the same parent (sample
games: consistent in all but one record). Areas (type 2) are always children of a normal or looping map, their
rectangle lies inside the parent and the exit point lies inside the parent (sample games: all 133 areas).

### 3.8 Tile data of normal and looping maps

Directly after a record whose type is not 2:

1. **Chipset names:** 4 text lines (§2.5, LF removed). Name k (0..3) is the chipset used for tile ids
   k×256 … k×256+255. The names are file names without extension (images `name.BMP`, attributes `name.ATR`).
   Sample games: always `MCHIP0`, `MCHIP1`, `MCHIP2`, `MCHIP3`.
2. **Tiles:** W × H cells (W, H = width and height of the record), stored row by row (row 0 first, each row
   from x = 0 to W−1), run-length coded with u16 values. Read values until all W × H cells are filled; for each
   value v:
   * tile id = v & 0x3FF (0..1023);
   * repeat count = (v >> 10) + 1 (1..64): the tile id fills that many consecutive cells.
   The editor always writes repeat count 1 (bits 10–15 are 0 in all sample games). A run that would go past
   the last cell is not guarded in the original; an implementation must ignore the excess.
3. **Event placement:** a second run-length stream of u16 values, read until the cell position (starting at 0
   again) has reached or passed W × H; for each value v:
   * if bit 15 is clear: v (0..0x7FFF) is an **event id** placed in the current cell; advance one cell. (v = 0
     places nothing but still advances; it does not occur in the sample games.)
   * if bit 15 is set: skip (v & 0x7FFF) + 1 cells (no events there).
   The editor terminates the stream with a skip that covers the remaining cells (sample games: 2 711 of 2 785
   maps; the others end with an event in the last cell). A skip past the end simply ends the stream.

Each cell therefore holds a tile id (0..1023) and an event id (0 = none, else 1..32767). Event ids refer to
event files (§4). How events are created from these ids is in `05-events.md`.

If the file ends inside the tile or event stream, the original keeps reusing the last value read until the map
is filled; this is not reproduced by any sample game, and an implementation may treat it as an error.

### 3.9 Errors

* If the game file cannot be opened, the original shows a message box with the text `Can't open file!!!`
  (no icon, OK button) with the path of the file as caption. All game data stay zero and the map table is
  not even created; the original then fails (it crashes while setting up the start map). An implementation
  should refuse to start.
* The original checks nothing else (header, version, ranges). Implementations should guard array accesses
  (map ids, tile counts) but must accept every file that the original accepts.

---------------------------------------------------------------------------------------------------------------

## 4. Event files (eveNNNNN.dat)

### 4.1 Names and special ids

The event with id n is stored in `eve` + n written with 5 digits + `.dat` in the game folder (case-insensitive;
sample games use `eve00001.dat`, `eve00001.DAT` and `EVE00001.DAT`). Ids:

* 1..32767: map events, referenced by the event placement of map cells (§3.8), and vehicles (`04-map-movement.md`);
* **0**: the start event `eve00000.dat`, run once at a new game (`05-events.md`);
* **99999**: the game-over event `eve99999.dat` (`05-events.md`, `08-battle.md`).

All sample games contain both special files. No other per-event files exist.

### 4.2 Header (0x88 bytes)

| off | type | content |
|---|---|---|
| 0x00 | s32 | header size, must be 0x88 (136) |
| 0x04 | 8 bytes | unused (0 in all sample files) |
| 0x0C | u8[4] | trigger of page p (p = 0..3 at 0x0C + p) |
| 0x10 | u32[4] | condition flags of page p (at 0x10 + 4p) |
| 0x20 | u8[4] | display mode of page p |
| 0x24 | u8[4] | layer of page p |
| 0x28 | u8[4] | move type of page p |
| 0x2C | u8[4] | animation type of page p |
| 0x30 | u8[4] | speed of page p |
| 0x34 | u8[4] | vehicle (passability) type of page p |
| 0x38 | u16[4] | condition item of page p (at 0x38 + 2p) |
| 0x40 | u8[4] | condition party mask of page p |
| 0x44 | u32[4] | condition money of page p (at 0x44 + 4p) |
| 0x54 | u16[4] | condition switch of page p (at 0x54 + 2p) |
| 0x5C | u16[4] | graphic of page p (at 0x5C + 2p) |
| 0x64 | 16 bytes | garbage (editor memory contents); ignored |
| 0x74 | s32[4] | number of commands in page p's command list (at 0x74 + 4p), including the final End command |
| 0x84 | s32 | size in bytes of the string list including its terminating 0x1A byte |
| 0x88 | | variable part (§4.4) |

Pages 0..3 are the editor's pages 1..4.

### 4.3 Page attribute values

Meaning and use of these values: `05-events.md` (pages, conditions, triggers) and `04-map-movement.md`
(movement and drawing).

| field | values |
|---|---|
| trigger | 0 action button, 1 touch, 2 autorun |
| condition flags | bit 0 item, bit 1 money, bit 2 switch, bit 3 party; other bits unused. A page whose flags enable no condition is never chosen except page 0 (`05-events.md`). |
| display mode | 0x19 normal (visible), 0x1A invisible, 0x1B removed |
| layer | 0 below the characters, 1 same level, 2 above |
| move type | 0 fixed, 1 approach the hero, 2 move away from the hero, 3 random |
| animation type | 0x12 normal (stepping animation, faces the movement direction), 0x13 and 0x14 spinning (opposite directions), 0x15 random direction, 0x16 fixed direction, 0x17 no stepping animation, 0x18 fixed direction without stepping animation (the most common value) |
| speed | 1..20, larger is slower |
| vehicle type | 0 walking, 1 wagon, 2 sled, 3 boat, 4 ship, 5 balloon, 6 airship |
| condition item | ITEM.DAT index 0..99 |
| condition party mask | bit n set = actor n must be in the party |
| condition money | compared unsigned with the party's money |
| condition switch | switch number 0..1023 (the editor uses 0..999) |
| graphic | below 0x100: bits 0–5 = character index 0..63 in `CHARA.BMP`, bits 6–7 = initial facing direction (0 up, 1 right, 2 down, 3 left); 0x100 and above: the event is drawn as the map tile with tile id = graphic − 0x100 (0..1023) |

Sample files: triggers 0/1/2; display 0x19/0x1A/0x1B; layers 0..2; move types 0..3; animation types
0x12..0x18; speeds 1..20; vehicle types 0..6; graphic up to 0x4FF.

### 4.4 Variable part

Directly after the header, in this order:

1. **Page move routes**, 4 blocks (pages 0..3): each block is one u8 length n followed by n route bytes
   (n may be 0). Route byte meanings: `05-events.md` (Move routes).
2. **Command lists**, page 0 first, then pages 1, 2, 3: page p has exactly the number of commands given in the
   header at 0x74 + 4p. Each command is 20 bytes = 5 × s32: code, argument 1, argument 2, argument 3,
   argument 4. Command codes and arguments: `05-events.md`. Unused arguments contain editor garbage and must
   not be validated. In all sample files every page has at least one command and its last command has code 0
   (End); commands after the first End are never executed.
3. **String list**: a sequence of entries, each = one u8 **string index** followed by a NUL-terminated
   Shift_JIS string, terminated by a single byte 0x1A. Strings are referenced by event commands by index
   (0..255). Line breaks inside strings are CR LF; the item and price lists of shop and inn commands use bare
   LF (`07-menus-shop-inn.md`). Exact parsing below.
4. **Command move routes** until the end of the file: blocks of one u8 length n followed by n route bytes. The
   i-th block (0-based) is command move route i, referenced by the hero/event move-route commands
   (`05-events.md`). Sample games: up to 80 blocks per file; the original has room for 100 (more corrupts its
   memory; an implementation should accept any number).

**String list parsing (exact).** Let *size* be the header value at 0x84. Keep a counter *consumed* = 0 and
*previous index* = none. All 256 string slots start empty. Repeat:

1. Read one byte b.
2. If b = 0x1A:
   * if the previous index is not 25 (0x19): the list ends (stop);
   * else if *consumed* + 1 ≥ *size*: the list ends (stop);
   * else b is the string index 26 (0x1A) and *consumed* is increased by 1 extra **(quirk: such a string is
     counted one byte too long)**.
3. Read bytes up to and including the next NUL; the string is the bytes before the NUL. Add 1 (index byte) +
   string length + 1 (NUL) to *consumed*.
4. If slot b already holds a string, the list ends here **without failure** **(quirk: everything after this
   point — the remaining strings, the 0x1A byte and the routes — is then read as command move routes)**.
5. Store the string in slot b; previous index = b.
6. If *consumed* > *size*, the file is marked as **failed** (reading continues, but the event will not run).

**(quirk)** When the event is not run because its list failed, the strings already stored are **kept**: the
256 slots are emptied only at the end of an event run that actually executed (§4.5). The next event that
runs therefore finds those slots occupied: its string list stops at the first index that is still occupied
(rule 4, without failure; the rest of its list is read as command move routes), and its commands that use
that index or the indices it did not get to store see the old strings of the failed file. An implementation
that wants exact behaviour keeps the 256 slots across a failed run in the same way; otherwise it should at
least not run failed files. No sample file fails (UNCERTAIN whether any game depends on this).

So 0x1A is ambiguous with index 26: it is the terminator unless it directly follows the string with index 25
and the list is not yet exhausted according to *size*. If the end of the file is reached inside the string
list, the original hangs or crashes; an implementation should treat the file as failed. In all 64 631 sample
files the list parses without failure and *size* = (sum of 1 + length + 1 over all strings) + 1.

A command that refers to a string index that has no string: the original uses an invalid reference; an
implementation should use an empty string (`05-events.md`).

### 4.5 Reading events, missing and invalid files

An event file is read in two situations:

* **Placement** (when the events of a map are created, `05-events.md`): open the file; read the first s32; if
  the file cannot be opened or the value is not 0x88, the event is **not created** (and its id is removed
  from the map cell). Otherwise the page attributes (header 0x0C..0x63) and the 4 page move routes are read.
  Commands and strings are not read at this time. Nothing else is checked: the file length is not tested.
  If the file ends inside these parts, the missing values are undefined in the original (UNCERTAIN; no
  sample file is that short); an implementation should then not create the event. (If the file can be opened for the size check but not a
  second time, the original shows a message box `ｲﾍﾞﾝﾄﾌｧｲﾙを開けません` with caption `RPGﾂｸｰﾙ95`; this
  practically never happens.)
* **Running** (every time the event, the start event or the game-over event starts): the whole file is read
  again: header, page routes, the 4 command lists, the string list, the command move routes. If the file is
  missing, its first s32 is not 0x88, or the string list is marked failed, the event is **not run** (the
  runner does nothing; for a failed string list see the quirk in §4.4). All data read for the run is
  discarded when the run ends or is suspended.
  A command count that is negative, or command lists that reach past the end of the file, leave the
  original in an undefined state (it then reaches the end of the file inside the string list and hangs);
  an implementation treats such a file as failed.

The files never change during play, so an implementation may cache them, provided the observable behaviour
(including failure) stays the same.

---------------------------------------------------------------------------------------------------------------

## 5. strings.dat

`strings.dat` in the game folder holds the 150 built-in message texts strings[0..149] (list of the default
texts and their uses: `11-appendix.md`). It is read once at start-up, after the game file.

* If the file **cannot be opened**, all 150 strings take the built-in defaults.
* Otherwise the runtime performs **150 line reads** (§2.5, 255-byte limit, last byte removed, CR kept, NUL
  ends the string), one per string, in order. Consequences beyond §2.5:
  * a missing line (the file has fewer than 150 lines): the failed read leaves the previous buffer in place,
    and its last byte is removed again, so each further string is the previous one shortened by one byte,
    until it is empty **(quirk)**;
  * a line longer than 255 bytes shifts all later strings by one entry **(quirk)**;
  * when the file is present, no string falls back to its default, even an empty one.
  * if the file is empty (0 bytes) the original fills the strings with undefined contents; an implementation
    should use empty strings (UNCERTAIN).

Sample games: all 31 files have exactly 150 LF-terminated lines (no CR, no line longer than 99 bytes).

---------------------------------------------------------------------------------------------------------------

## 6. Battle databases

All database files are read from the game folder with the fixed names of §2.3. Sizes in all sample games:
CHARA.DAT 18 400, ITEM.DAT 2 800, MAGIC.DAT 2 800, EPARTY.DAT 32 000 bytes. A shorter file fills only the
records it contains (the rest is undefined; use zeros); extra bytes are ignored.

### 6.1 Battler record (0xB8 bytes)

The same 0xB8-byte record is used for the monster database (CHARA.DAT), for the enemies of the current battle
(10 records, filled from CHARA.DAT at battle start) and for the party (8 records, `00-README.md`). Party and
enemy records also appear in save files (`10-save-files.md`). All 16-bit fields are signed unless stated.

| off | type | party member | CHARA.DAT / enemy |
|---|---|---|---|
| 0x00 | s16 | level 1..100 | unused (file: 0) |
| 0x02 | s16 | HP | enemy: HP (set to maximum HP at battle start); file: unused (editor writes 50) |
| 0x04 | s16 | **magic defence** = power of the item in the "other" equipment slot | unused (0) |
| 0x06 | s16 | maximum HP | **maximum HP** (the monster's HP) |
| 0x08 | s16 | unused | unused (0) |
| 0x0A | s16 | MP | enemy: MP (set to maximum MP); file: unused (editor writes 50) |
| 0x0C | s16 | maximum MP | **maximum MP** |
| 0x0E | s16 | 体力 vitality | unused (editor writes 10) |
| 0x10 | s16 | 力 power | unused (editor writes 10) |
| 0x12 | s16 | 素早さ agility | **agility** |
| 0x14 | s16 | 知力 intelligence | unused (editor writes 10) |
| 0x16 | s16 | 運 luck | unused (editor writes 10) |
| 0x18 | s16 | 防御力 defence | **defence** |
| 0x1A | s16 | 攻撃力 attack | **attack** |
| 0x1C | s16 | sleep counter (always 0, `08-battle.md`) | same |
| 0x1E | s32 | experience points | **experience** given when defeated |
| 0x22 | s32 | unused | **money** given when defeated |
| 0x26 | u8 | unused | **dropped item**: ITEM.DAT index (0-based) |
| 0x27 | u8 | unused | **drop probability** in percent (0 = never) |
| 0x28 | u16 | status bits | status bits (0 in the file; set to 0 at battle start) |
| 0x2A | s16 | unused | unused (0) |
| 0x2C | char[21] | name, NUL-terminated | **name**, NUL-terminated |
| 0x41 | u8[100] | magic damage rate (%) of spell i on this battler | **magic damage rate** of spell i on this monster (0..255) |
| 0xA5 | u8[10] | unused | **action pattern** (below) |
| 0xAF | s8 | actor index 0..7 of this party record | unused (0) |
| 0xB0 | s32 | 0 | 0 |
| 0xB4 | s32 | 0 | 0 |
| 0xB8 | | end | |

**Name field:** a NUL-terminated string starting at 0x2C. Actor names are at most 18 bytes, but monster
names in CHARA.DAT may use up to 20 bytes (the NUL then lies at 0x40; sample games: 18 names of exactly 20
bytes). Bytes after the NUL up to 0x40 are meaningless (editor data in CHARA.DAT, leftover contents in party
records and save files) and must be ignored.

**Status bits** (details `08-battle.md`, Status effects): 0x01 poison (毒), 0x02 paralysis (麻痺), 0x04 silence
(沈黙), 0x08 confusion (混乱), 0x10 sleep (眠る), 0x20 knocked out (気絶, display only), 0x40 unused.

**Action pattern** (monsters, `08-battle.md` Enemy action): 10 bytes; the entries before the first 0 are the
candidate actions: 1 attack, 2 critical attack, 3 double attack, 4 watch, 5 flee, 10 + n cast spell n
(MAGIC.DAT index n). All zero means "attack". Sample games: values 0..5 and 10..109; no non-zero entry follows
a zero.

### 6.2 CHARA.DAT (monsters)

100 battler records (§6.1) of 0xB8 bytes, record i = monster i (troop entries refer to it, and its picture is
MONSTPIC.DAT record i). Fields used for enemies: maximum HP (0x06), maximum MP (0x0C), agility (0x12), defence
(0x18), attack (0x1A), experience (0x1E), money (0x22), dropped item and probability (0x26, 0x27), name, magic
damage rates, action pattern (and the unused 16-bit value at 0x2A, which is copied). Everything else is
ignored. Editor default values seen in the sample files: the s32 at 0x00 = 0x00320000 (level 0, HP 50), 0x0A
= 50, 0x0E/0x10/0x14/0x16 = 10; unused records are all zero.

### 6.3 ITEM.DAT

100 records of 28 bytes, record i = item i:

| off | type | content |
|---|---|---|
| 0x00 | s16 | price (shops; selling price rules in `07-menus-shop-inn.md`) |
| 0x02 | s16 | item type (table below) |
| 0x04 | s16 | power (bonus or effect amount) |
| 0x06 | s16 | equippable-by mask: bit a set = actor a may equip it (only bits 0–7 are used) |
| 0x08 | char[20] | name, NUL-terminated (sample games: at most 18 bytes of text) |

| type | meaning | usable in battle |
|---|---|---|
| 0 | weapon (attack bonus) | no |
| 1 | armour (defence bonus) | no |
| 2 | shield (defence bonus) | no |
| 3 | helmet (defence bonus) | no |
| 4 | other / accessory (magic defence) | no |
| 5 | special / key item (using it has no effect) | no |
| 6 | HP recovery | yes |
| 7 | MP recovery | yes |
| 8 | detoxify | yes |
| 9 | status recovery | yes |
| 10 | revive (from 気絶) | yes |
| 11 | maximum HP up | yes |
| 12 | maximum MP up | yes |
| 13 | 力 up | yes |
| 14 | 体力 up | yes |
| 15 | 知力 up | yes |
| 16 | 素早さ up | yes |
| 17 | 運 up | yes |
| 18 + n | casts spell n (MAGIC.DAT index n) | yes |

Equipment slot k (0 weapon, 1 armour, 2 shield, 3 helmet, 4 other) accepts items whose type equals k. Effects
in `07-menus-shop-inn.md` and `08-battle.md`.

### 6.4 MAGIC.DAT

100 records of 28 bytes, record i = spell i:

| off | type | content |
|---|---|---|
| 0x00 | s16 | spell type (table below) |
| 0x02 | s16 | animation number: 0 in all files; at start-up the runtime combines it (bitwise OR) with an animation number derived from the spell types (`08-battle.md` §5.4, Animation number of each spell) |
| 0x04 | s16 | MP cost |
| 0x06 | s16 | power |
| 0x08 | char[20] | name, NUL-terminated |

| type | effect | type | effect |
|---|---|---|---|
| 0 | damage, one target | 11 | heal HP, one |
| 1 | damage, all targets | 12 | heal HP, all |
| 2 | poison | 13 | heal MP, one |
| 3 | paralyse | 14 | detoxify |
| 4 | silence | 15 | status recovery |
| 5 | confuse | 16 | revive |
| 6 | sleep | 17 | attack up |
| 7 | knock out (instant) | 18 | defence up |
| 8 | attack down | 19 | agility up |
| 9 | defence down | 20 | teleport |
| 10 | agility down | 21 | escape |

Types 0..10 are offensive (the party aims them at enemies; not usable from the field menu), types 11..21 are
supportive (aimed at allies; usable from the field menu). Effects in `08-battle.md` and `07-menus-shop-inn.md`.

### 6.5 EPARTY.DAT (troops)

32 000 bytes = 250 records of 128 bytes. Troop numbers in the editor, in event commands and in encounter
tables are 1-based: troop n is record n − 1.

| off | type | content |
|---|---|---|
| 0x00 | s16 | background: the battle background file is `bg` + (value as u16 + 1) with at least 2 digits + `.bmp` (value 0 → `bg01.bmp`) |
| 0x02 | s16 | terrain: a random encounter with this troop happens only on a tile whose attribute (§7) equals this value |
| 0x04 | s32 | number of monsters 0..10; 0 = empty troop (a battle with it does nothing) |
| 0x08 | 10 × {s32 monster, s32 x, s32 y} | monster = CHARA.DAT index = MONSTPIC.DAT index; (x, y) = screen position (640 × 480 screen) of the top-left corner of the monster's picture |

Only the first *count* monster entries are meaningful; the others often contain garbage (sample games: 1 744
troops). Sample games: background values 0..19 and others, terrain values 0, 2..4, 6..11.

If EPARTY.DAT cannot be opened at start-up, the runtime shows a message box with text
`ﾌｧｲﾙ[EPARTY.DAT]が見つかりません` and caption `RPGﾂｸｰﾙ95`, and the program ends.

### 6.6 MONSTPIC.DAT (monster pictures)

A sequence of records until the end of the file; record i is the picture of CHARA.DAT monster i:

| off | type | content |
|---|---|---|
| 0x00 | u16 | width w |
| 0x02 | u16 | height h |
| 0x04 | u8[w × h] | pixels, 8 bits per pixel (palette indices), rows **bottom-up**: the first stored row is the bottom row of the picture; rows are not padded |
| 0x04 + w×h | 256 × {u8 blue, u8 green, u8 red, u8 reserved} | the picture's colour table (reserved = 0 normally; ignored) |

Record size = w × h + 0x404.

**Start-up scan:** offset[0] = 0. Repeatedly: try to read the 2-byte width at the current offset; if fewer
than 2 bytes can be read, the scan ends; otherwise read the height, set offset[i+1] = offset[i] + w × h +
0x404, and continue at offset[i+1]. (A record whose data is cut off by the end of the file still counts.) The
sample games contain about 100 records per file.

**Dummy files:** two sample games without battles (脱出, RinRin!) ship a MONSTPIC.DAT that is really a copy of
an event file; the scan finds one record (width 0x88, height 0) and stops. Such files must be accepted.

**Use at battle start** (`08-battle.md`, `03-display.md`): for each monster of the troop, its record is read
from offset[monster index] (no range check in the original; an implementation should treat a missing record as
"no picture"). The picture is built as an 8-bit image with the 256-colour table; its colours are matched to
the current screen palette like any image (`03-display.md`); the transparent colour is the palette index of
the picture's top-left pixel. All sample pictures have widths that are multiples of 4. **(quirk)** For other
widths the original lays the w × h bytes into rows padded to a multiple of 4 bytes, which shears the picture
and leaves the last bytes undefined (UNCERTAIN; no sample data).

If MONSTPIC.DAT cannot be opened at start-up: message box `ﾌｧｲﾙ[MONSTPIC.DAT]が見つかりません`, caption
`RPGﾂｸｰﾙ95`, and the program ends. If it cannot be opened at a battle start: the same message, and the battle
is skipped: the map is shown again (`08-battle.md`).

---------------------------------------------------------------------------------------------------------------

## 7. Tile attribute files (.ATR)

The attribute of tile id t (0..1023) is byte t & 0xFF of the attribute table of chipset t >> 8. Each chipset k
(0..3) has one 256-byte table, read from a file whose name is formed from chipset name k of the **start map**
(§3.8):

* if the second byte of the name is `:` (a drive letter, as in `C:\...\MCHIP0`), the file is the name +
  `.ATR`, used as an absolute path **as it is** (not in the game folder);
* otherwise the file is `<game folder>\` + name + `.ATR` (sample games: `MCHIP0.ATR` … `MCHIP3.ATR`, each 256
  bytes).

The first 256 bytes of the file are the table. If the file cannot be opened, the table is 256 zero bytes; if
it is shorter, the missing bytes are 0. The tables are read **once** (at start-up, after the map graphics are loaded and before the logos)
and never again, even when a later map uses other chipset names (`04-map-movement.md`).

Attribute values 0..13 (sample games: only these occur); their meaning and the passability table are in
`04-map-movement.md`.

An absolute name refers to the author's machine; the original then normally finds no file and uses zeros. An
implementation may, as an option, also try the base name in the game folder (UNCERTAIN whether any game needs
this; none of the sample games has such a name).

---------------------------------------------------------------------------------------------------------------

## 8. Image files

### 8.1 Image file names

| file | content | layout described in |
|---|---|---|
| `CHARA.BMP` | all character sprites (64 characters); also the map palette | `04-map-movement.md`, `03-display.md` |
| `<chipset name>.BMP` (normally `MCHIP0.BMP` … `MCHIP3.BMP`) | map tiles, normally 8 × 32 tiles of 32 × 32 per chipset | `04-map-movement.md` |
| `title.bmp` | title picture | `01-runtime.md`, `03-display.md` |
| `bgNN.bmp` | battle background, also the battle palette | `08-battle.md`, `03-display.md` |
| `AnmNN.bmp` | battle animation frames (128 × 128 each, side by side) | `08-battle.md` |
| `BG01.BMP` | palette source when the palette is reloaded during a battle **(quirk)** | `03-display.md` |
| any name from an event string | pictures shown by the Show picture command (name reduced as in §2.4) | `03-display.md`, `05-events.md` |

The chipset image is `<game folder>\` + chipset name + `.BMP` (no base-name reduction; a name with a drive
letter therefore cannot be loaded by the original).

Image sizes are not checked; every image is used with its own width and height (`03-display.md` §1.1).
`CHARA.BMP` is normally 1024 × 512 (16 × 4 character cells of 64 × 128 pixels, `04-map-movement.md` §6.1) and a chipset image
256 × 1024, but other sizes occur (sample games: `CHARA.BMP` 1024 × 512 in 27 games, also 1024 × 391,
1024 × 256, 960 × 512 and 640 × 384; chipsets 256 × 1024 in 113 files, also 256 × 200/320/500/750/810,
320 × 1024, 370 × 1024, 512 × 1024 and three 1 × 1 dummies). Tiles and sprites whose 32 × 32 source rectangle
is not completely inside the image are not drawn (`03-display.md`).

### 8.2 BMP variants

All images are Windows bitmap files. The original's bitmap reader accepts:

1. The file starts with `BM` (14-byte file header; the s32 at 0x0A is the pixel data offset).
2. The info header size (s32 at 0x0E) is **12** (OS/2 core header: u16 width, u16 height, u16 planes, u16 bit
   count; colour table entries are 3 bytes blue, green, red) or **40** (Windows header: s32 width, s32 height,
   u16 planes, u16 bit count, u32 compression, u32 image size, two s32 resolutions, u32 colours used, u32
   important colours; colour table entries are 4 bytes blue, green, red, reserved). Other header sizes (the
   108- and 124-byte versions) are rejected.
3. Planes must be 1.
4. Colour table length: for the 12-byte header 2^bitcount entries for 1, 4 and 8 bits, none for 24 bits; for
   the 40-byte header "colours used" entries if that field is non-zero, else 2^bitcount for 1, 4 and 8 bits and
   none for 24 bits. For 16 and 32 bits with "colours used" = 0 (and for any other bit count) the reader fails
   **(quirk)**.
5. The pixel data is read from the data offset if that offset lies beyond the colour table, else directly
   after the colour table. Its length is the uncompressed size (row length rounded up to 4 bytes × height) when
   the compression is 0 or the header is the 12-byte one, else the "image size" field.
6. A negative height (top-down bitmap) makes the size computation fail, so **top-down bitmaps are rejected**
   **(quirk)**.

Accepted bitmaps are then drawn with the full Windows bitmap semantics: 1, 4, 8 and 24 bits per pixel,
uncompressed or RLE4 / RLE8 compressed (compression 2 / 1), bottom-up rows padded to 4 bytes.

Requirements for an implementation:
* must decode: 1, 4, 8 and 24 bits per pixel, uncompressed, bottom-up, with 40-byte headers (all images of
  the sample games are of this kind: 8 bits in most files, 24 bits for 115 pictures, 4 bits and 1 bit for some
  pictures, animations, backgrounds and tiny dummy chipsets); RLE8 and RLE4; 12-byte headers;
* should also decode 16- and 32-bit bitmaps (with the standard 5-5-5 and 8-8-8 layouts or bit-field masks) and
  top-down bitmaps, which the original rejects. Accepting them is harmless because no game can rely on them
  failing; an implementation that wants exact behaviour treats them as unreadable (UNCERTAIN which is better).

### 8.3 The palette of a bitmap

When a bitmap is used as **palette source** (CHARA.BMP, bgNN.bmp, title.bmp, pictures with "own palette",
`03-display.md`), its palette is its colour table: entries 0 … n−1 with n as in §8.2 rule 4, as (red, green,
blue). Only bitmaps with a 40-byte header and at most 8 bits per pixel supply a palette; for other bitmaps no
entry is changed. Entries n … 255 keep their previous values. Colour matching of non-palette-source images to
the screen palette is described in `03-display.md`.

### 8.4 Missing or unreadable images

* `title.bmp` missing: the title is shown without a picture (`01-runtime.md`).
* `bgNN.bmp` missing: message box `ﾌｧｲﾙ[<path>]が見つかりません` (caption `RPGﾂｸｰﾙ95`, `<path>` = the full path
  of the file), and the battle is skipped: the map is shown again (`08-battle.md`).
* `AnmNN.bmp` missing: the same message with that path, and the animation is skipped (`08-battle.md`).
* pictures from events: missing → the command does nothing.
* `CHARA.BMP` or a chipset image missing or unreadable, or any existing image that the reader rejects (§8.2):
  the original does not check and fails with an error of its window library (UNCERTAIN: probably an error
  message and/or termination). An implementation should report the problem and continue with a blank
  (all-index-0) image of the normal size (CHARA.BMP 1024 × 512, chipset 256 × 1024).

---------------------------------------------------------------------------------------------------------------

## 9. Other files

* **Audio files** (WAV, MIDI): names from the audio slots (§3.3), from event strings, and the built-in sound
  effect names (`11-appendix.md`), all reduced as in §2.4 and looked up in the game folder. The stored names
  include their extension, and for music and jingles the player is selected from the **name**, not from the
  type field: a name containing `.mid` (case-sensitive) is played as MIDI, one containing `.wav` as WAV, and
  anything else is treated as a CD audio track (`09-audio-movies.md` §3.1). Sound effects are always WAV
  files (`09-audio-movies.md` §5.2).
* **Movies** (AVI): names from event strings, reduced as in §2.4 (`09-audio-movies.md`).
* **Save files**: `10-save-files.md`.

---------------------------------------------------------------------------------------------------------------

## Quirks in this chapter

* §2.1 With several `.rpg` files the last one of the directory enumeration is used.
* §2.4 Base names are found by a byte-wise search for 0x5C, so Shift_JIS trail bytes 0x5C cut names
  (`カーソル1.wav` → `ル1.wav`); games ship files under such cut names.
* §2.4 Built-in sound effects are looked up in the game folder, not in a SYS\SE folder.
* §2.5 Line reads remove the last byte whatever it is: CR of CR LF stays; a last line without LF loses a
  character; lines over 255 bytes are split.
* §3.2 The game file's header is not checked; two header spellings exist.
* §3.2 The version text continues into the header bytes at 0x05..0x1C if the 5 version bytes contain no NUL.
* §3.3 Pre-0.96 files have 11 audio entries; slot 11 then has no name (nothing is played; a save writes `(null)`).
* §3.5 Actor start values are used as stored, never recomputed from the curves.
* §3.7 A 21-byte map name runs on into the depth field.
* §3.7 Map ids are not range-checked by the original.
* §3.8 Tile runs past the last cell are not guarded; at end of file the last value is reused.
* §3.9 A game file that cannot be opened produces a message and then a failure (there are no defaults).
* §4.4 0x1A in the string list is a terminator unless it follows index 25 and the size is not exhausted; a
  string with index 26 is counted one byte too long.
* §4.4 A duplicate string index ends the list without failure; the rest is read as command move routes.
* §4.4 A string list larger than the header size makes the event unrunnable (it is still placed).
* §4.4 The strings of a failed list stay stored and disturb the string list of the next event run.
* §5 Missing lines of strings.dat give progressively shortened copies of the previous string.
* §5 A long line in strings.dat shifts all later strings.
* §6.1 Monster names may use up to 20 bytes (into the bytes after the 18-byte name area).
* §6.3 ITEM.DAT missing prevents MAGIC.DAT from being read.
* §6.6 Dummy MONSTPIC.DAT files (event-file copies) are accepted; truncated records still count.
* §6.6 Monster picture widths not divisible by 4 are sheared.
* §7 Attribute tables are read once, from the start map's chipset names; a name with a drive letter is used as
  an absolute path.
* §8.1 The battle palette reload uses `BG01.BMP` regardless of the current background.
* §8.1 Chipset image names are not reduced to base names (a drive letter makes them unloadable).
* §8.1 Image sizes are not checked; tiles and sprites reaching outside a smaller image are not drawn.
* §9 Music and jingles choose MIDI / WAV / CD audio from the file name, not from the type field.
* §8.2 Top-down bitmaps, 108/124-byte headers and 16/32-bit bitmaps without colour table are rejected by the
  original.

## Open questions

* Order of the directory enumeration when several `.rpg` files are present (§2.1).
* Whether full-width Latin letters in file names are matched case-insensitively (§2.2).
* Exact visible behaviour of the original when CHARA.BMP, a chipset image or an existing but unreadable
  bitmap cannot be loaded (§8.4); likewise for an existing picture file that the bitmap reader rejects.
* Contents of strings when `strings.dat` exists but is empty (§5), and of the database records when CHARA.DAT,
  ITEM.DAT or MAGIC.DAT is missing (§6) — the original leaves them undefined; zeros / empty strings are
  recommended.
* Monster picture handling for widths that are not multiples of 4 (§6.6).
* Values seen by an event placement whose file ends inside the header or the page routes (§4.5).
* Whether an implementation should accept bitmaps that the original rejects (§8.2) and fall back to the game
  folder for absolute attribute-file names (§7).
