# 04 — Maps, characters and movement

This chapter specifies everything that happens on the map screen except the event commands themselves:
map types and areas, chipsets and tile attributes, map characters and their drawing, the movement core,
event movement types and move routes, player control, followers, the camera, teleporting, vehicles,
random encounters and the per-step poison damage.

Related chapters: `01-runtime.md` (ticks, input model, random numbers, the outer program flow),
`02-data-formats.md` (the .RPG map records, event files, ATR files, EPARTY.DAT, the battler record),
`03-display.md` (palette, colour keys, screen transitions, composition of the frame), `05-events.md`
(event objects, pages, triggers, the runner and all commands; it refers to this chapter for movement),
`06-windows-text.md` (`Message(...)`, windows), `07-menus-shop-inn.md` (escape/teleport magic use),
`08-battle.md` (battles), `09-audio-movies.md` (music playback), `10-save-files.md` (what is saved).

Conventions of `00-README.md` apply (directions 0 up, 1 right, 2 down, 3 left; dx = (0, +1, 0, −1),
dy = (−1, 0, +1, 0); 32-px tiles, 4 sub-steps of 8 px; C integer arithmetic). "R" denotes one call of the
global random generator (`01-runtime.md`, Random numbers); it returns a non-negative integer, so "R & 3"
and "R % n" are unambiguous. The order of R calls given here is mandatory.

---------------------------------------------------------------------------------------------------------

## 1. Maps

### 1.1 Map records and cells

The game file holds up to 500 map records (map ids 0..499; layout in `02-data-formats.md`, "Map records").
The fields used by the runtime are (record offsets in parentheses):

| field | meaning |
|---|---|
| name (0x00) | NUL-terminated name; shown in the teleport destination list (`07-menus-shop-inn.md`) |
| type (0x19, s32) | 0 normal, 1 looping, 2 area |
| area left / top (0x1D / 0x21, s32) | areas only: top-left cell of the area inside its parent map |
| width W / height H (0x25 / 0x27, u16) | map size in tiles; for an area the size of its rectangle |
| parent (0x29, s32) | parent map id, −1 for a root map |
| next sibling (0x2D, u16) | next map with the same parent, 0 = none |
| first child (0x2F, u16) | first child map, 0 = none |
| exit x / exit y (0x35 / 0x39, s32) | areas only: the cell of the **parent** map used by escape/teleport magic |
| encounter table (0x3D, u16[10]) | entry e: troop = e & 0x1FF (1-based EPARTY index, 0 = empty), rate = e >> 9 (0..127) |
| chipset names | 4 names (normal/looping maps only) |
| cells | W × H cells (normal/looping maps only) |

A map "exists" when its width is non-zero (this is the test made by the teleport, §11.1; there is no separate
"exists" flag). Each cell holds a tile id (0..1023; from the tile stream) and an event id (0 = none; from the
event-placement stream, `02-data-formats.md` §3.8). Map ids are used as indices without range checks when
reading the file (ids ≥ 500 are invalid data).

All sample games use map sizes 20..256 × 15..256 for normal/looping maps. Maps smaller than 20 × 15 are not
supported by the original (the camera computations assume at least 20 × 15; behaviour is undefined).

### 1.2 Map types

* **Normal (0).** Nobody can step outside the map: a step whose destination lies outside is cancelled
  without turning (§7.3). The camera is clamped to the map (§10).
* **Looping (1).** Positions wrap modulo W and H on every step; the camera wraps and is never clamped.
* **Area (2).** Has no tiles, events or chipsets of its own. It is a rectangle inside its parent map with
  its own encounter table and an exit point in the parent. An area is never entered as a map; a teleport to
  an area id would access a map without cells (not guarded in the original; an implementation should
  refuse it — see Open questions).

### 1.3 Area table (per cell)

On every map change (§11.1) the runtime builds an **area table** of W × H 16-bit entries for the current map:

1. Set every entry to the current map id.
2. Walk the current map's direct children: start with its first child c; while c ≠ 0: if map c is of type 2
   and its left < W and its top < H, set the entries of rows top .. min(top + height, H) − 1 and columns
   left .. min(left + width, W) − 1 to c; then c = next sibling of c.

Later siblings overwrite earlier ones where they overlap. Grandchildren are ignored. Negative area
origins are not guarded in the original (an implementation should clip at 0).

The area table is used by random encounters (§13.3) only.

### 1.4 Parent maps, exit points and the escape procedure

The **escape procedure** with a target area id A (used by escape-type and area-teleport magic/items, see
`07-menus-shop-inn.md`) does the following:

1. Fail (return "not done") if map A is not of type 2, if the party is riding a vehicle, if the parent P of
   A is not a valid map, if P's type is not 0 or 1, or if A's exit x ≥ P's width or exit y ≥ P's height
   (negative exit coordinates are not checked).
2. Play the sound effect file `wibble.wav` (`09-audio-movies.md`, sound effects with default parameters).
3. If not in battle: set the pass-abort flag (§9.1), close the message window, remember the map-change
   transition (transitions table index 0), set it to 13, teleport (§11.1) to (P, exit x, exit y), restore the
   remembered transition.
4. If in battle: remember A as the "escape after battle" target; when the battle ends, the battle-end
   sequence teleports to (P, exit x, exit y) with transition 13 instead of returning to the map normally and
   then restores the BGM (`08-battle.md` §7). The pass-abort flag is **not** set in this case
   (**quirk**, `01-runtime.md` §1).
5. Return "done".

There is no other way to leave a map to its parent: walking off the edge of a map never changes the map.

---------------------------------------------------------------------------------------------------------

## 2. Chipsets and tile attributes

### 2.1 Chipset bitmaps and tile source rectangles

There are 4 chipsets, normally `MCHIP0.BMP` .. `MCHIP3.BMP`: 8-bit bitmaps, normally of 256 × 1024 pixels = 8
columns × 32 rows of 32 × 32 tiles. Other sizes occur in the sample games (256 × 500, 256 × 750, 256 × 810,
256 × 200, 256 × 320, 320 × 1024, 370 × 1024, 512 × 1024 and 1 × 1); the source rectangle below does not
depend on the image size, and a rectangle that does not lie inside the image is handled as described in
`03-display.md` §1.2 (copy operations). For a tile id t (0..1023):

* chipset = t >> 8 (0..3),
* index n = t & 0xFF,
* source rectangle = x (n & 7) × 32, y (n >> 3) × 32, 32 × 32 (from the top-left of the bitmap).

There is exactly **one tile layer**: no upper-layer tiles, no animated tiles, no autotiles. Map tiles are
copied opaque (no colour key). Visual overlays are made with events whose graphic is a tile (§6.1) on layer 2.

### 2.2 Loading

* `CHARA.BMP` (8-bit, normally 1024 × 512, §6.1) and the four chipsets are loaded at the start of the map scene, after every
  battle, whenever display surfaces are re-created (screen mode switch) and when lost display surfaces are
  restored (`01-runtime.md` §8 step 10; `03-display.md` §8.1). The chipset names are those of the
  **current** map (the start map before the first teleport); the file is `<game folder>\<name>.BMP`. They are
  **not** reloaded on a map change. Every map of every sample game uses `MCHIP0`..`MCHIP3`, so an
  implementation may simply load the four chipsets once (falling back to `MCHIPn.BMP` if a name cannot be
  found).
* Colour key: the transparent colour of `CHARA.BMP` is the palette index of its pixel (0, 0); the **same
  palette index** is used as the colour key of all four chipsets (used when a tile is drawn as a character
  graphic). The map palette is the palette of `CHARA.BMP` (`03-display.md`).

### 2.3 Tile attributes (ATR)

An attribute table of 1024 bytes, indexed directly by tile id, is loaded **once**, when the map scene starts,
from the chipset names of the start map: for chipset k, the file `<name>.ATR` (if the name's second character
is `:` the name is an absolute path, otherwise the file is in the game folder) supplies the 256 attribute
bytes of tile ids k × 256 .. k × 256 + 255 (format: `02-data-formats.md`, §7 "Tile attribute files (.ATR)"). A missing file
gives zeros. It is never reloaded.

Attribute values (names from the editor's terrain list; only the numbers matter to the runtime):

| a | name | walk 0 | wagon 1 | sled 2 | boat 3 | ship 4 | balloon 5 | airship 6 |
|---|---|---|---|---|---|---|---|---|
| 0 | flat | ✔ | ✔ | | | | ✔ | ✔ |
| 1 | marsh | ✔ | | | | | ✔ | ✔ |
| 2 | snow | ✔ | | ✔ | | | ✔ | ✔ |
| 3 | forest | ✔ | | | | | ✔ | ✔ |
| 4 | river | | | | ✔ | | ✔ | ✔ |
| 5 | shallows / rapids | | | | ✔ | ✔ | ✔ | ✔ |
| 6 | sea | | | | | ✔ | ✔ | ✔ |
| 7 | mountain | ✔ | ✔ | | | | ✔ | ✔ |
| 8 | steep mountain | | | | | | ✔ | ✔ |
| 9 | tower | | | | | | | ✔ |
| 10 | floor | ✔ | ✔ | | | | ✔ | ✔ |
| 11 | entrance | ✔ | | | | | ✔ | ✔ |
| 12 | obstacle | | | | | | ✔ | ✔ |
| 13 | desk / counter | | | | | | ✔ | ✔ |

As bit masks (bit a set = attribute a passable): walk 0x0C8F, wagon 0x0481, sled 0x0004, boat 0x0030,
ship 0x0060, balloon 0x3DFF, airship 0x3FFF. The test is "bit (a & 31) of mask[vehicle type]"; attribute
values ≥ 14 are therefore impassable for everyone (values ≥ 32 wrap; sample games use 0..13 only).

The **vehicle type** of a character (0..6, §4.2) selects the mask. "Passable for type v" below always means
this test.

Other uses of attributes (complete list): attribute 13 lets the action button reach across it (§9.3); the
encounter terrain test (§13.3); the balloon/airship landing test (attributes passable for wagon or sled, i.e.
0, 2, 7, 10; §12.4). There are no terrain effects on speed, damage, encounter rate or sprite drawing.

---------------------------------------------------------------------------------------------------------

## 3. Map characters

### 3.1 Kinds

* **Party characters**: one per actor (8, indexed by actor index), placed at the start position at a new
  game, graphic = the actor's map graphic from the database. The characters of the party positions
  0..(number shown − 1) are on the map; position 0 is the **leader**. The **number of shown party
  characters** is the party size, or 1 while riding a vehicle. All party characters have event id 0.
* **Map events**: one character per event placed on the current map (created on map change in row-major
  cell order; `05-events.md`). This **event list order** is the update, draw and autorun order.
* **Vehicles**: event characters kept in the vehicle table (§12.1); they survive map changes.

The **hero** is the character the player controls: the leader, or the ridden vehicle while riding. It is
re-determined at the start of every main-loop pass (= party position 0's character) and when boarding
completes.

### 3.2 Character state

Every map character has this state (initial values of a newly created character in brackets; events then get
most fields from their active page, `05-events.md`):

| state | meaning |
|---|---|
| position (x, y) [−1, −1] | the cell the character stands on, or the cell a step started from (events get their cell at creation, party characters at the first teleport) |
| destination (destX, destY) [−1, −1] | the target cell of the current step; equal to the position when idle, except after a step cancelled by §7.3 step 8.1, which leaves the attempted cell (the destination is only read while the sub-step is ≥ 2, so this has no visible effect) |
| on-screen flag [0] | set when the character was registered in the occupancy grid by the last draw (§5) |
| event id [0] | 0 for party characters and for the vehicle being ridden |
| facing override [2] | direction used by the next draw only; −1 = none (§6.3) |
| facing [2] | 0..3 |
| move type [5] | events: 0 fixed, 1 approach, 2 avoid, 3 random; hero: 5 player control, 6 boarding, 7 alighting, 9 mouse auto-walk; 3 random is also honoured for the hero (unreachable in practice) |
| motion [−1] | current motion: −1 idle, 0..3 step, 4..7 turn to (m & 3), 17 wait one step (§7.1) |
| graphic [0] | < 0x100: CHARA.BMP character 0..63; ≥ 0x100: tile id + 0x100 (§6.1) |
| sub-step [0] | 0..4 progress of the motion (may exceed 4 in a quirk, §7.4) |
| animation timer [0] / animation frame [0] | §6.2; frame 0..3 |
| layer [1] | 0 below the hero, 1 same as the hero, 2 above the hero |
| speed [2] | ticks per sub-step, 1 (fastest) .. 20 |
| speed counter [0] | counts ticks up to the speed |
| vehicle type [0] | passability class 0..6 (§2.3) and two-part drawing for 1/2 (§6.5) |
| animation type [0x12] | 0x12..0x18 (§6.2) |
| display mode [0x19] | 0x19 visible, 0x1A invisible, 0x1B removed/"through" (§3.3) |
| trigger cooldown [1] | ticks until the event may be triggered again (`05-events.md`) |
| route [none] | move route bytes, length, position, mode, step (+1/−1) (§8.3) |
| resume index [0] | events only: > 0 while its script waits for its own route (`05-events.md`) |

All 8 party characters share speed (the hero-speed command copies it to all 8), vehicle type (the
hero-vehicle-type command sets all 8), and while the hero runs a move route its display mode and animation
type are copied to all 8 (§9.2).

### 3.3 Display modes

* **0x19 visible**: drawn and registered in the occupancy grid.
* **0x1A invisible**: not drawn, but registered (blocks, can be talked to or touched). Followers become
  invisible while boarding/alighting (§9.6).
* **0x1B removed**: for an event (event id ≠ 0) the character does nothing at all: no motion can be started
  or advanced, it is not drawn, not registered, its on-screen flag is 0 (so it cannot autorun). It still
  takes part in page re-evaluation. For a party character / the hero (event id 0) 0x1B means **through**: not
  drawn, not registered by drawing, every destination is passable, and it neither bumps nor turns.

### 3.4 Layers

Layer 0 characters are drawn below layer 1, layer 1 below layer 2 (§6.6). Party characters are on layer 1;
a ridden vehicle is on layer 1 (wagon, sled, boat, ship) or 2 (balloon, airship). Collision is decided per
layer (§7.6): same-layer characters block each other; layer-0 characters only matter through their tile
graphic or as boats/ships; layer-2 characters never block anyone.

---------------------------------------------------------------------------------------------------------

## 4. Screen cells

### 4.1 Camera origin

The **camera origin** (ox, oy) is the map cell shown at the top-left screen tile; the screen shows 20 × 15
tiles. The **camera pixel origin** (cpx, cpy) is the pixel position of the screen's top-left corner in map
pixels, including a scroll offset during scrolling (§10.3). Both are set on teleport.

### 4.2 Map cell → screen cell

For a map cell (x, y) the screen cell (c, r) is:

1. x' = x + W if x < ox, else x; y' = y + H if y < oy, else y.
2. c = x' − ox, r = y' − oy.
3. On **looping** maps only: if c = W − 1 and W > 21 then c = −1; if r = H − 1 and H > 16 then r = −1.

The cell is "inside the screen window" when −1 ≤ c ≤ 20 and −1 ≤ r ≤ 15 (the visible 20 × 15 tiles plus one
extra column on each side and one extra row above and below; only column/row −1 can be produced on the
left/top by step 3, and only on looping maps). On normal maps a cell left of or above the camera maps to a
large positive c or r (**quirk**: when ox = W − 20 the map column 0 maps to screen column 20, and when
oy = H − 15 the map row 0 maps to screen row 15; nothing is ever registered there, see §5.2).

The occupant/landing query of §12.4 applies the step-3 rule on **all** map types (quirk, harmless in
practice).

### 4.3 Hero screen cell

Where the hero's own screen cell is needed (action button §9.3, scroll flags §10.2) it is computed as
hx = (x − ox + W) % W, hy = (y − oy + H) % H (no step 3).

---------------------------------------------------------------------------------------------------------

## 5. The occupancy grid

### 5.1 Size

The **occupancy grid** holds, for each layer 0..2, each screen row −1..15 and each screen column −1..20, a
reference to one character or nothing (3 × 17 × 22 entries). All collision, talking and touching is decided
through it, so **only characters inside the screen window interact**.

### 5.2 Rebuilding

The grid is cleared once per tick immediately before the tick's frame render, and rebuilt while the
characters are drawn (§6.7). A drawn character registers itself if all of these hold:

* its display mode is not 0x1B (so neither removed events nor a "through" hero are registered);
* its main sprite position is not beyond the right or bottom screen edge (§6.4);
* its registration cell — the position if sub-step < 2, else the destination — maps to a screen cell with
  c ≤ 20 and r ≤ 15 (§4.2).

It is written into `grid[its layer][r][c]`, overwriting what an earlier-drawn character wrote there; a
character whose move type is 6 (boarding hero) writes **nothing** (it clears the entry). On registration
the on-screen flag is set to 1 (the draw procedure first sets it to 0). Wagons and sleds also register the
cell behind them (§6.5).

Characters on map rows that the draw loop does not visit (§6.6) keep their previous on-screen flag (so an
autorun event that scrolled out vertically can still qualify for autorun, `05-events.md` §5.5).

### 5.3 Writes during movement

Between two renders the grid is also written by the movement core (§7.4): a party character or ridden
vehicle writes itself at its destination's screen cell on every sub-step it advances (also with display
0x1B), and an event writes itself at its destination when it passes the half-step without collision. These
writes are visible to characters processed later in the same tick. All other movement decisions use the grid
as left by the previous tick's render.

---------------------------------------------------------------------------------------------------------

## 6. Drawing characters

### 6.1 CHARA.BMP layout and sprite source rectangles

`CHARA.BMP` is normally 1024 × 512 = 16 × 4 character cells of 64 × 128 pixels. Each cell has 2 animation
columns of 32 px and 4 direction rows of 32 px: row 0 back (up), 1 right, 2 front (down), 3 left. (The sample
games also have 1024 × 391, 1024 × 256, 960 × 512 and 640 × 384; the source rectangles below are computed the
same way regardless of the image size, and one that does not lie inside the image is handled as described in
`03-display.md` §1.2.)

* Graphic g < 0x100: source rectangle x (g & 15) × 64 + col × 32, y (g >> 4) × 128 + dir × 32, 32 × 32, where
  col (0/1) and dir (0..3) are chosen by the animation rules (§6.2, §6.3). Sprites are drawn with the colour
  key of `CHARA.BMP`.
* Graphic g ≥ 0x100: the character is drawn as tile t = g − 0x100 of chipset t >> 8 (§2.1), with the colour
  key (§2.2). Such characters have no direction and no animation frame.

Graphic values set from event pages: a page graphic word < 0x100 gives graphic = word & 0x3F and initial
facing = facing override = word >> 6; ≥ 0x100 gives graphic = word. Graphic-change commands store g & 0x3F
for g < 0x100 (direction bits ignored). In the stock `CHARA.BMP` cells 60 balloon, 61 airship, 62 wagon cart,
63 sled; 62 and 63 are hard-wired as the trailing part of wagons and sleds (§6.5).

### 6.2 Animation types and timers

Each draw of a character with animation enabled (§6.7, freeze conditions) advances its timer:

1. timer += 9 if the animation type is 0x13 or 0x14, else timer += 2.
2. If timer > speed × 10 + 5: timer = 0, and frame = (R & 3) for type 0x15, else frame = (frame + 1) & 3.

So the frame advances every 5·s + 3 draws for types 0x12, 0x15..0x18 and every ⌊(10·s + 5) / 9⌋ + 1 draws
for 0x13/0x14 (s = speed; s = 2: 13 draws and 3 draws). The timer runs whether or not the character moves:
**all characters step in place continuously** unless the type suppresses the column change. The R call of
type 0x15 is made on every frame change of every processed character and is part of the global random
sequence.

| type | name | direction shown (dir) | column |
|---|---|---|---|
| 0x12 | normal | step direction while stepping, else facing | frame & 1 |
| 0x13 | spin (clockwise) | frame (0, 1, 2, 3 = up, right, down, left) | frame & 1 |
| 0x14 | spin (anticlockwise) | (4 − frame) & 3 | frame & 1 |
| 0x15 | random direction | frame (a random value) | frame & 1 |
| 0x16 | fixed direction | facing (steps in place) | frame & 1 |
| 0x17 | no stepping | as 0x12 | 0 when dir comes from this table, else frame & 1 (§6.3) |
| 0x18 | fixed graphic | facing | 0 when dir comes from this table, else frame & 1 (§6.3) |

Types 0x16 and 0x18 also ignore the facing override and never turn when a step is blocked (§7.3).

### 6.3 Displayed direction

For a visible character with graphic < 0x100, dir is chosen as follows (otherwise dir = facing):

1. If the facing override ≠ −1 and the animation type is not 0x16/0x18: dir = facing override.
2. Else if the motion is a turn (4..7): dir = motion & 3.
3. Else by the table of §6.2 ("stepping" = motion 0..3; motion −1 and 17 count as not stepping).

The column is frame & 1 in all cases, except that types 0x17 and 0x18 use column 0 when dir is taken from the
table (step 3). When the facing override (step 1, possible for type 0x17 only) or a turn motion (step 2, both
types) gives the direction, these types also show column frame & 1 for that draw **(quirk)**: for example a
0x17 character being talked to, or a 0x18 event executing a turn route byte, can show its stepping column.

Then the facing override is reset to −1 (only for visible characters with graphic < 0x100; for others it
stays set). After the sprite is drawn and the character registered (§5.2), **if its sub-step is 3, facing =
dir** — the facing follows the drawn direction at the last sub-step of a step or wait (this also makes
spinning characters change their facing, and makes a facing override drawn at sub-step 3 permanent). This
facing update is skipped for characters whose main sprite is beyond the right/bottom edge and for display
0x1B; for invisible characters dir = facing, so nothing changes.

### 6.4 Screen position and clipping

Before positioning: if sub-step ≥ 4, sub-step = 0. Then:

* px = x × 32 − cpx, py = y × 32 − cpy, plus (dx × 8 × sub-step, dy × 8 × sub-step) of the motion if the
  motion is a step 0..3 (x, y = position, i.e. the cell the step started from);
* if px < −31: px += W × 32; if py < −31: py += H × 32 (on every map type);
* if px > 639 or py > 479 the main sprite is neither drawn nor registered (wagons/sleds still process their
  trailing part, §6.5); the draw procedure returns for other characters.

Clipping: if px < 0 the source rectangle's left edge moves right by −px and the sprite is drawn at x = 0
with width 32 + px; if px > 608 the width is 640 − px; the same with py/480/448 vertically.

### 6.5 Wagons and sleds (vehicle type 1 or 2)

Characters with vehicle type 1 or 2 (in practice mostly the party after the hero-vehicle-type command; every
party character then gets a cart; two event pages of the sample games also use these types) are drawn in two parts within the same draw call:

1. The main sprite as usual (unless beyond the right/bottom edge).
2. Then, on top, graphic 62 (type 1) or 63 (type 2) using the character's **facing** row (the facing before
   the sub-step-3 update of §6.3) and the same column as the main sprite, at
   px2 = x × 32 − fdx × 8 × (4 − sub-step) − cpx, py2 = y × 32 − fdy × 8 × (4 − sub-step) − cpy (fdx, fdy from
   the facing), i.e. exactly one tile behind the main sprite while moving straight. Only on looping maps:
   px2 < −31 → += W × 32, py2 < −31 → += H × 32. If px2 < −31, py2 < −31, px2 > 639 or py2 > 479 the part is
   skipped.

Registration: besides the normal registration (§5.2, only if the main sprite was not beyond the edge), the
cell (c − fdx, r − fdy) relative to the registration screen cell (c, r) is registered with the character
itself (also for move type 6) if −1 ≤ c − fdx ≤ 20 and −1 ≤ r − fdy ≤ 15. Wagons and sleds never turn in
place (a blocked step leaves them unchanged) and are not turned by the face-each-other logic (§9.5).

### 6.6 Draw order

After the tile layer (§10.3), characters are processed in this order:

For layer L = 0, 1, 2; for screen row r = −1 .. 15, with map row m = (oy + r + H) % H:

1. every map event, in event list order, whose layer is L and whose row (y if sub-step < 2, else destY) is m;
2. every vehicle table slot k from (riding ? 1 : 0) to 9 that holds a character on the current map, whose
   layer is L and whose row is m;
3. if party position 0's character has layer L and row m: the shown party characters from the last one down
   to position 0 (followers first, leader last = on top).

Followers are drawn in the leader's row slot regardless of their own row. Characters whose row is never
visited are not processed at all (no animation, no sub-step reset, no grid registration, on-screen flag
unchanged).

**Quirk:** the row loop visits 17 map rows: the screen rows r = −1..15 map to map rows (oy + r) mod H. So with
H = 16 the row (oy − 1) mod H is processed twice, and with H = 15 the rows (oy − 1) mod H and oy are processed
twice (for a normal 20 × 15 map, where oy = 0: rows 14 and 0). Characters on those rows are processed twice per frame: their animation
advances twice as fast and type 0x15 consumes R twice. Likewise the map row oy − 1 (mod H) is always processed
even on normal maps, where it is the bottom row of the map when oy = 0 (processed but not drawn).

### 6.7 Per-character draw and freezing

Each processed character: set its on-screen flag to 0; stop if it is an event with display 0x1B; advance the
animation (§6.2) unless frozen; compute dir (§6.3, done during the visible-sprite step); position (§6.4);
draw if display is 0x19 (sprite or tile, and the wagon/sled part); register (§5.2); apply the sub-step-3
facing rule.

Animation is **frozen** (step 1 of §6.2 skipped) when:

* the whole draw is an interim redraw without presenting (§10.4) — e.g. the redraw before a screen transition
  shows the map;
* for map events and vehicles: the movement-control mode is 1 (§9.7) and the character's script is not
  waiting for its own route (resume index 0);
* for the party character of position i: the battler record of party position i has HP < 1 or status bit
  0x02 (paralysis).

---------------------------------------------------------------------------------------------------------

## 7. Movement core

### 7.1 Motion codes

| motion | meaning | duration |
|---|---|---|
| −1 | idle | — |
| 0..3 | step one cell in direction m | 4 sub-steps |
| 4..7 | turn to face m & 3 | 1 sub-step period, no displacement |
| 17 (0x11) | wait the duration of one step | 4 sub-steps |

There are no jumps, no diagonal moves and no other motion codes. Boarding and alighting use only these codes
(wait 17 for the hero, steps for followers and the leader, §12). Route bytes 8..16 are resolved to one of
these codes before the motion is started (§8.3). The value 5 used as "no direction" by mouse auto-walk is a
turn (to the right) when started (quirk, §9.2).

### 7.2 Timing model

All movement runs in ticks (`01-runtime.md`; nominally one tick per 20+ ms). A character with speed s
advances one sub-step every s ticks: a step and a wait take 4 × s ticks, a turn takes one s-tick period.
Default speed 2 → 8 ticks per tile.

* **Hero**: every tick: speed counter += 1; if speed counter ≥ speed: speed counter = 0 and the hero advances
  one sub-step (§7.4). (The test is "≥", not "=": a counter that is already above a lowered speed, e.g. after
  event command 41, triggers at the next tick.) The counter runs while idle, so a new step begins 0..s−1 ticks after
  the input is detected. A motion is *started* in the loop pass (input or route), which runs between ticks, so
  continuous walking costs exactly 4 × s ticks per cell.
* **Followers** advance in the hero's speed ticks (they share the speed).
* **Events and vehicles** (each tick, §8.1): first, if E's speed equals the hero's speed and the hero's speed
  counter is 0 after the hero's part of this tick, E's counter := E's speed; then E's counter += 1; if E's
  counter ≥ E's speed: E's counter = 0 and E acts. So in lock-step (counter = speed + 1 ≥ speed) E acts in the
  same tick as an equally fast hero. (A hero tick that is skipped because the counter has not reached the
  speed leaves the counter non-zero, so no lock-step happens in that tick.)

### 7.3 Starting a motion

Starting motion m for character C:

1. If C is an event (event id ≠ 0) with display 0x1B: nothing happens.
2. C.motion = m. If m = −1: done.
3. If m is a turn (4..7): C.facing = m & 3; done (the motion stays 4..7 until advanced).
4. If m = 17: done.
5. Step (0..3). Destination d = (x + dx, y + dy). On a normal map, if d is outside the map, d = (x, y) per
   axis; otherwise d is wrapped: ((x + dx + W) % W, (y + dy + H) % H). C.destination = d.
6. If d = position: C.motion = −1; done (map edge: **no step and no turn**).
7. Compute the screen cell (c, r) of d (§4.2); "offscreen" = c < −1 or r < −1 or c > 20 or r > 15.
8. Only if C is an event or C is the hero (followers skip all of step 8):
   1. If C is an event without a route and offscreen: C.motion = −1; done (**wandering events never leave
      the screen window**).
   2. below = offscreen ? nothing : grid[0][r][c].
   3. t = tile id of cell d; if below exists and below.graphic ≥ 0x100: t = below.graphic − 0x100 (a tile
      event on layer 0 replaces the terrain).
   4. blocked = not passable(C.vehicle type, ATR[t]).
   5. If below exists and below.vehicle type is 3 or 4 (a boat/ship lying on layer 0): blocked = (C.vehicle
      type is not 0, 5 or 6) — walkers and fliers may always enter its cell, others never.
   6. If C.display = 0x1B: blocked = false.
   7. If blocked: if C.vehicle type is 1 or 2, or C.animation type is 0x16 or 0x18: C.motion = −1; otherwise
      C.facing = m and C.motion = m + 4 (it turns toward the obstacle instead; the turn costs one speed
      period). In both cases C.destination = C.position.
   8. If C.display ≠ 0x1B, not offscreen, C.vehicle type is 1 or 2, and grid[1][r][c] holds a character other
      than C with graphic < 0x100: C.motion = −1, C.destination = C.position (wagons/sleds are stopped by any
      layer-1 sprite at the destination, without turning).

The terrain test does not depend on C's own layer, and same-layer characters do not block here (except for
wagons/sleds); they are handled while advancing. Layer-0 characters with a sprite graphic never block a
start, except boats/ships (vehicle type 3 or 4) by step 8.5 (that test does not look at the graphic); layer-2
characters never block a start.

### 7.4 Advancing a motion

Advancing character C by one sub-step; the result is either nothing or a character to run the touch check
with (§9.4):

1. If C is an event with display 0x1B, or C.motion = −1: nothing.
2. If C.motion is 0..17: sub-step + 1.
3. Turn (4..7): facing override = motion & 3; sub-step = 0; motion = −1; done.
4. Wait (17): if sub-step = 4: motion = −1. Done.
5. Step: compute the screen cell (c, r) of the destination (§4.2). If offscreen: if sub-step = 4 then
   position = destination and motion = −1; done (no collisions outside the screen window).
6. o = grid[C.layer][r][c].
7. If C has event id 0 (hero, follower, ridden vehicle):
   * If C is the hero, sub-step = 1, o exists with event id ≠ 0, C.display ≠ 0x1B and C.vehicle type is not 5
     or 6: sub-step = 0; if C.vehicle type is 1 or 2: motion = −1; else facing = motion and motion += 4 (it
     turns toward the event; the turn completes one speed period later). Result: o (bump). Done.
   * Otherwise write C into grid[C.layer][r][c].
8. If C is an event and sub-step = 2 (half-way):
   * if o is nothing or o's event id equals C's: write C into grid[C.layer][r][c];
   * else: if C has no route, it **bounces back**: motion = (motion + 2) & 3, position and destination are
     swapped, sub-step = 4 − sub-step (= 2), so it walks back to its start cell in the remaining sub-steps;
     if C has a route it **waits**: sub-step − 1 (= 1), and the half-step test is repeated at its next speed
     tick. In both cases, if o is the hero the result is C (the event touched the hero).
9. If sub-step = 4: position = destination, motion = −1.

The sub-step is reset from 4 to 0 by the next draw of the character (§6.4), not here; the hero's step
completion is detected through the scroll sub-step value captured before that draw (§9.1).

Consequences:

* The hero is stopped only by same-layer **events**; party characters and the ridden vehicle have event id 0
  and are walked through. The test happens once, at the first sub-step, so the hero is never drawn displaced
  toward an event. Balloon and airship ignore events entirely.
* Events are blocked by anything registered in their layer, including the hero and followers, at the
  half-step: a wandering event visibly moves 16 px toward the obstacle and returns.
* **Quirk (sub-step stall):** a character that completes a step while its row is not visited by the draw loop
  (§6.6) keeps sub-step 4. Its next step or wait then counts 5, 6, … and never ends (turns are unaffected)
  until its row is visited again, which resets the sub-step to 0 and lets the motion run normally. This only
  affects route-driven events far above/below the screen.

### 7.5 Map edges and looping maps

* Normal maps: steps to outside cells are cancelled for everyone, including followers and route steps (no
  turn). There is no edge transition to another map.
* Looping maps: every position, destination and camera coordinate wraps. Distances used by the movement types
  and the face-each-other rule use raw coordinates without wrap handling.

### 7.6 Who blocks whom

| mover \ thing at the destination | terrain | layer-0 sprite character | layer-0 tile character | layer-0 boat/ship | same-layer event | same-layer party char / ridden vehicle | layer-2 character |
|---|---|---|---|---|---|---|---|
| hero on foot (type 0) | walk mask | never | its tile's attribute | always passable | stops at sub-step 1 + bump | never | never |
| hero in boat/ship (3/4) | mask 3/4 | never | its tile's attribute | blocks | stops + bump | never | never |
| hero in balloon/airship (5/6) | mask 5/6 | never | its tile's attribute | passable | never (passes through) | never | (same layer: never) |
| hero as wagon/sled (1/2) | mask 1/2 | never | its tile's attribute | blocks | layer-1 sprite blocks at start; event stops at sub-step 1 + bump | layer-1 sprite blocks at start | never |
| hero with display 0x1B | nothing | never | never | never | never | never | never |
| event | mask of its vehicle type | never | its tile's attribute | blocks unless its type is 0/5/6 | bounce (no route) / wait (route) at half-step | bounce / wait | never |
| follower | nothing is checked | — | — | — | — | — | — |

Events whose destination is outside the screen window: no AI steps (cancelled), route steps proceed with the
terrain test but without any collision.

---------------------------------------------------------------------------------------------------------

## 8. Event movement

### 8.1 Per-tick update of an event or vehicle

Once per tick (not while the arrival flag is set, §9.1), first for the vehicles (§12.1) and then for the map
events in list order, each character E is updated:

1. If E's trigger cooldown > 0: decrement it (this happens in every movement-control mode).
2. If the movement-control mode is 1 and E's resume index is 0: stop here (no motion, no AI, no route).
3. Speed counter (§7.2): if E's speed equals the hero's speed and the hero's speed counter is 0, E's counter
   := E's speed; then E's counter += 1; if E's counter ≥ E's speed: E's counter = 0 and continue with step 4;
   otherwise stop (E does not act this tick).
4. If E is idle (motion −1): take the next move (§8.2 without a route, §8.3 with a route). If that call
   reports "route ended": if the screen state is MAP, close all windows and do an interim redraw with present
   (§10.4); then, if E's resume index > 0, resume E's suspended script (`05-events.md`); if that sets the
   pass-abort flag, E's update ends here (step 5 is not done for E). If E is a map event, the remaining map
   events are then skipped this tick (the test of §9.1 step 8). After a vehicle's update the flag is not
   tested: the remaining vehicles and all map events are still updated (in full), and the test after the
   first map event then ends the pass.
5. Advance E (§7.4) — in the same tick a new motion was started; if the result is a character, run the bump
   check (§9.4) with E as the touched event.

A new decision is therefore taken on the same tick the previous motion ended: wandering is continuous.

### 8.2 Movement types (no route)

Target (tx, ty) = the hero's position.

* **0 fixed**: nothing.
* **1 approach**: h = 1 (right) if x − tx ≤ 0, else 3 (left); v = 2 (down) if y − ty ≤ 0, else 0 (up).
  Draw R; if R % 3 = 0 start step h; if 1 start step v; if 2 draw a second R and start step R & 3.
* **2 avoid**: h = 1 if x − tx ≥ 0, else 3; v = 2 if y − ty ≥ 0, else 0; same choice as approach.
* **3 random**: start step R & 3.

A blocked attempt turns (one speed period) and the next decision follows immediately afterwards. The hero
never runs these except type 3 (main loop, R & 3; unreachable in practice).

### 8.3 Move routes

A character's route state: bytes b[0..len−1], position p, mode (none, running, repeat, back-once, ping-pong;
saved in save files as 0x1C none, 0 running, 0x1D repeat, 0x1E back-once, 0x1F ping-pong, `10-save-files.md`)
and step direction (+1/−1). Initial: none, p = 0, step +1. "The character has a route" means its mode is not
none. A route is installed (a) on page activation if the
page route is non-empty (`05-events.md`), (b) by the hero/event move-route commands (event commands 33 and
35). Installing copies the bytes, sets mode running, p = 0, step +1; it replaces any previous route but does
not touch the current motion. After commands 33/35 the runner applies **leading display bytes** at once:
while b[p] is 0x19..0x1B, display mode = b[p] and p + 1 (for command 33 the hero's display mode is then copied
to all 8 party characters).

**Route byte table** (`05-events.md` refers here):

| byte | meaning | timed |
|---|---|---|
| 0–3 | step up / right / down / left | yes |
| 4–7 | turn up / right / down / left | yes |
| 8 | step in the hero's facing direction | yes |
| 9 | step opposite to the hero's facing direction | yes |
| 10 | step forward (own facing) | yes |
| 11 | step backward (own facing + 2) | yes |
| 12 | turn to the hero's facing direction | yes |
| 13 | turn opposite to the hero's facing direction | yes |
| 14 | turn to own facing (no visible change; costs one speed period) | yes |
| 15 | turn around (180°) | yes |
| 16 | step forward (own facing; never mirrored) | yes |
| 17 | wait one step duration | yes |
| 18–24 (0x12–0x18) | set animation type to the byte | no |
| 25–27 (0x19–0x1B) | set display mode to the byte (visible / invisible / removed) | no |
| 28 (0x1C) | end of route | no |
| 29 (0x1D) | repeat from the start forever | no |
| 30 (0x1E) | walk the route back once (mirrored), then end | no |
| 31 (0x1F) | ping-pong forever | no |

"The hero" for bytes 8, 9, 12, 13 is the current hero character, even when the route belongs to the hero
(then 8 ≡ 10 etc.).

**Taking the next move with a route** (one call; returns "route ended" or not):

Let seenLoop = false. Repeat:

1. If p < 0 and the mode is ping-pong: p = 0, step = +1. Otherwise, if p < 0 or p ≥ len: go to END.
2. Let b = b[p].
3. If b < 18 (timed): compute the motion code d:
   * b = 8..15: k = b − 8; reference = own character if bit 1 of k is set (10, 11, 14, 15), else the hero;
     d = (reference.facing + (k & 1) × 2) & 3; if k > 3, d += 4 (turns).
   * otherwise d = b.
   * If b = 16: d = own facing. Else if step = −1: d is mirrored: d = ((d + 2) & 3) + (4 if d > 3 else 0).
     **Quirk:** this maps 17 (wait) to 7 (turn left) on mirrored playback.
   * Start motion d (§7.3); p += step; return "not ended".
4. b = 18..24: animation type = b; p += step; continue.
5. b = 25..27: display mode = b; p += step; continue.
6. b = 28: go to END.
7. b = 29: if seenLoop go to END; mode = repeat; p = 0; seenLoop = true; continue.
8. b = 30 or 31: if seenLoop go to END; mode = back-once (30) or ping-pong (31); step = −1; p += step (= one
   before the control byte); seenLoop = true; continue.
9. b ≥ 32: the original loops forever (never present in data); an implementation should go to END.

END: mode = none, p = 0, step = +1, the route bytes are discarded, return "ended".

Notes:

* Ping-pong: playing backwards reaches p < 0, which restarts forwards at p = 0 (step +1); reaching the end
  again executes byte 31 again, which reverses again. Back-once: playing backwards reaches p < 0 → END.
  Repeat: byte 29 jumps to 0. The seenLoop guard ends a route that would loop without any timed byte.
* A blocked route step is not retried: the character turns (§7.3) and the route proceeds with the next byte.
  Route events that collide at the half-step wait instead of bouncing (§7.4). Routes run even outside the
  screen window (terrain is still checked, collisions are not).
* A route ends ("route ended") only through END; that is when a suspended script waiting for this route
  resumes (`05-events.md`). After a page route has ended the movement type (§8.2) takes over.
* Hero routes run in the main loop (§9.2), one call per loop pass while the hero is idle.

---------------------------------------------------------------------------------------------------------

## 9. Player control

### 9.1 The map scene loop

The map scene is a loop of **passes**; a pass is cheap and runs continuously, a tick runs inside a pass only
when due (`01-runtime.md`). Flags used below:

* **pass-abort flag** (`00-README.md` §5; full list of what sets it in `01-runtime.md` §1) — set for
  example by a teleport (event command 5, escape/teleport magic and items, the escape procedure of §1.4), the
  start of alighting (§12.4; boarding never sets it), loading a game, the EndGame command and quitting; makes
  the current pass skip to its post-pass actions (step 12);
* **arrival flag** — set by every teleport (and at the start of the map scene); while set, hero input is
  skipped in **every** pass and the vehicle/map-event updates are skipped in the tick; it is cleared after the
  autorun step of the next tick (the hero's own sub-step advance in that tick still happens);
* **scroll direction** D (−1 or 0..3, kept across passes; initially −1) and **scroll sub-step value** S;
* **post-pass action** — none / teleported / boarding done / alighting done, reset at the start and at the end
  of every pass. "Teleported" is set by event command 5 and has no effect at step 12.

One pass:

1. If quitting: leave. Clear the pass-abort flag and the post-pass action. Hero = party position 0's character.
2. Process all pending window messages (keyboard, mouse; mouse actions of §9.2 happen here). If quitting:
   leave; if the pass-abort flag is set: go to 12.
3. If the field menu was requested: run it (`07-menus-shop-inn.md`); if the pass-abort flag is set: go to 12.
4. While an event, a battle or a display-mode switch is in progress, the pass does nothing else (back to 1).
5. **Hero input** (only if the hero is idle and the arrival flag is clear): action button (§9.3,
   or alighting while riding), then walking or hero route (§9.2). If the pass-abort flag became set: go to 12.
6. Forget the last stored key. If no tick is due: go to 13.
7. **Hero tick**: hero speed counter + 1. If it is still < the hero's speed, continue with step 8.
   Otherwise (counter ≥ speed) reset it to 0, advance the hero (§7.4), and then:
   * If the advance reports a bump: D = −1; run the bump check (§9.4); if the pass-abort flag is set go to 12;
     otherwise continue with step 8 (the followers are not advanced in this tick).
   * Else, if the hero's sub-step is now 1 (a step or wait really started): D = hero motion if it is 0..3, else
     −1; push the trail and start the followers (§9.6); if the hero's move type is 6 (boarding) and the last
     shown party character's motion is 17: post-pass action = boarding done, go to 12; if it is 7 (alighting) and the
     last shown party character's motion is 17: post-pass action = alighting done, go to 12.
   * Advance the followers at positions 1 .. shown − 1 in that order (§9.6).
8. **World tick** (unless the arrival flag is set): update the vehicles (§12.1), then every map event
   in list order (§8.1). The pass-abort flag is tested only after each map event's update (not after vehicle
   updates); when it is set, go to 12.
9. **Render**: S = 0 if D = −1, else S = hero sub-step. Clear the occupancy grid; render the frame with
   direction D and present (§10.3).
10. **Autorun** pass for map events, then vehicles (`05-events.md` §5.5). After each map event, if the
    pass-abort flag is set: go to 12. After each vehicle the flag is tested too, but **(quirk)** a set flag only
    ends the vehicle loop: the pass continues with "clear the arrival flag" and with step 11 (on the new map if
    an autorun vehicle teleported) and reaches step 12 normally (`01-runtime.md` §8 step 11.4). Clear the
    arrival flag.
11. **Step completion**: if S = 4 (the hero completed a step this tick): D = −1; recompute the scroll flags
    (§10.2); discard key states (`01-runtime.md`, key flushing without waiting); S = 0; step-on check (§9.4);
    if the pass-abort flag is not set: poison (§13.1); if the party is down (§13.2) go to 13; random encounter
    (§13.3) if the hero's display mode is 0x19 and it has no route; (if the party is down go to 13).
    In all other cases (no step completed, the pass-abort flag set by the step-on event, or no party-down)
    continue with step 12 — so a step-on event that ends the game (code 30) leaves the loop at once at step 12.
12. **Post-pass actions**: if quitting: leave. Boarding done → §12.2 finalisation; alighting done → §12.4
    finalisation; teleported or none → nothing.
13. Game-over check (`05-events.md`), page refresh if requested (`05-events.md`); forget the last stored key;
    next pass.

Order within a tick: hero → followers → vehicles → map events → render/grid → autorun → step completion
(step-on, poison, encounter) → vehicle finalisation → game over → page refresh.

### 9.2 Walking, hero routes and mouse

**In the pass (step 5)**, when the hero is idle and the arrival flag is clear:

1. Action button: if the movement-control mode ≤ 1, the hero has no route, the release latch
   (`01-runtime.md`) is set and the last stored key-down is Space or Enter, or the joystick OK button is held:
   if riding and the hero's move type is not 7, try to alight (§12.4); otherwise run the action button
   (§9.3). If the pass-abort flag is then set, the pass skips to its post-pass actions (§9.1 step 12).
2. If the hero has no route, or its move type is 6 or 7:
   * move type 5 and movement-control mode < 2: test the directions in the priority order **up, left, down,
     right**; up = Up arrow or numeric-keypad 8 or joystick up; left = Left or keypad 4 or joystick left; down =
     Down or keypad 2 or joystick down; right = Right or keypad 6 or joystick right (keys tested as "currently
     held"; testing a key that is not held sets the release latch, `01-runtime.md` §4.3). Start a step in the first held
     direction (only one direction; no diagonals, no dash);
   * move type 6 or 7: start motion 17 (wait) — boarding/alighting progress (§12);
   * move type 9: mouse auto-walk step (below);
   * move type 3: start step R & 3.
3. Otherwise (the hero has a route): take the next route move (§8.3). Unless the hero's move type is 6 or 7,
   copy the hero's display mode and animation type to all 8 party characters. If the route ended: if the
   screen state is MAP, close all windows and do an interim redraw with present; then, if a script is waiting
   for the hero (`05-events.md`), resume it.

Holding a direction simply keeps starting steps; a short tap still produces one full step. Walking into an
obstacle repeatedly turns toward it (one speed period per attempt, no sound). At the edge of a normal map
nothing happens.

**Joystick** (only if present, `01-runtime.md`): x < 10000 = left, x > 50000 = right, y < 10000 = up,
y > 50000 = down; buttons 2/4 = OK, buttons 1/3 = cancel.

**Mouse** (on the map, outside windows; window clicks are in `06-windows-text.md`):

* Left button down: (closes the menu if one is requested, `07-menus-shop-inn.md`); if the hero's move type
  is 9 it becomes 5; if no drag is in progress, remember the pointer as the drag origin and start a drag;
  otherwise reset the same-direction counter.
* Pointer move with the left button held, while no event/battle runs, the hero's move type is 5 or 9, the
  hero has no route, a drag is in progress and the movement-control mode < 2: let (ddx, ddy) = pointer −
  origin; if |ddx| ≤ 1 and |ddy| ≤ 1 nothing; else dir = |ddy| < |ddx| ? (ddx > 0 ? 1 : 3) : (ddy > 0 ? 2 : 0);
  if the hero is idle start step dir; if dir equals the remembered mouse direction, the same-direction counter
  + 1 (if its value before the increment was greater than 10000 it becomes 10000, so it never exceeds 10001;
  only "> 20" is ever tested); else the remembered mouse direction = dir and, if the move type is 5, the counter =
  0; if the counter > 20: hero move type = 9 (auto-walk); the origin = the pointer.
* Left button up: end the drag; counter = 0. (Auto-walk continues after the button is released.)
* Auto-walk (move type 9, in the pass): start step (remembered mouse direction); if the resulting motion is
  not exactly that step (blocked → turn, map edge → idle): mouse direction = 5 ("none"), move type = 5.
* Any key-down sets move type 9 back to 5 (the remembered mouse direction is not changed).
* Escape requests the field menu when no menu, battle or event is active and the leader's sub-step is 0
  (`07-menus-shop-inn.md`); the key is seen during the message processing of a pass (§9.1 step 2) and the
  menu runs at step 3 of that pass.
* Double click (no event/battle running, hero idle): alight if riding and move type ≠ 7, else action button.
* Right button: sets move type 9 back to 5; in battle see `08-battle.md`; otherwise if no event runs: with
  the left button also held and the hero idle → alight / action button as for the double click; else if no
  menu is requested and the hero's move type is 5 → request the field menu.

**Quirks:** the mouse action paths (double click, right+left) do not check the movement-control mode, the
hero's route or the arrival flag; auto-walk (type 9) does not check the movement-control mode.
Teleport sets the remembered mouse direction to 5 and ends the drag but does **not** end auto-walk (move type 9
stays), so a hero that was auto-walking when teleported keeps "auto-walking" with direction 5: each idle pass
starts motion 5, a turn to the right, which counts as "exactly that motion", so auto-walk does not end by
itself and the hero repeatedly turns to face right until any key or a left or right mouse button is pressed.

### 9.3 Action button

Ignored when the hero's vehicle type is 5 or 6 (flying), its move type is 7, or its sub-step ≥ 1.

1. (hx, hy) = hero screen cell (§4.3); (fdx, fdy) from the hero's facing; front = (hx + fdx, hy + fdy),
   two-ahead = (hx + 2fdx, hy + 2fdy). A cell is usable if −1 ≤ column ≤ 20 and −1 ≤ row ≤ 15.
2. e = grid[1][front] if front is usable; if e has event id 0 (follower, vehicle being ridden): e = nothing.
3. If no e and two-ahead is usable and the attribute of the **map tile** at ((x + fdx + W) % W,
   (y + fdy + H) % H) is 13 (counter; the map coordinates wrap even on normal maps): e = grid[1][two-ahead],
   again ignoring event id 0.
4. If no e: e = grid[2][hero cell] (an event above the hero on its own cell); if still none: e =
   grid[0][hero cell] (below the hero).
5. Reject if no e, e is the hero, e's resume index ≥ 1 (its script is waiting), or e's trigger cooldown > 0.
6. Face each other with layer test (§9.5); run event e (`05-events.md`); then wait until the confirm key is
   released (key flushing with waiting, `01-runtime.md`).

The trigger type of e is **not** checked: the action button starts pages with any trigger (action, touch,
autorun) in front, across a counter, or on the hero's own cell in layers 0/2. Same-layer events are reached
only from the adjacent cell (or across a counter); layer-0/2 events only when the hero stands on their cell.

### 9.4 Touch triggers

* **Bump check** (event e reported by an advance: the hero walked into a same-layer event at sub-step 1, or an
  event reached the hero at its half-step): requires the hero's vehicle type not 5/6, move type ≠ 7, hero
  sub-step < 1, e ≠ hero, e's active page trigger = 1 (touch), e's trigger cooldown = 0 and e's resume index
  < 1. Then face each other **without** layer test (§9.5), run e, wait for the confirm-key release.
* **Step-on check** (after every completed hero step, §9.1 step 11; also after alighting): with the same
  initial conditions as the action button (vehicle 5/6, move type 7, sub-step ≥ 1 → nothing), e =
  grid[2][hero cell], else grid[0][hero cell] (no front/counter search); requires e ≠ hero, trigger = 1 and
  resume index < 1 (the cooldown is **not** checked); face each other with layer test (never applies, the
  layers differ); run e; discard key states without waiting.

Same-layer events can never be stepped on (they block). Followers never trigger anything.

### 9.5 Face each other

With the hero H and event e: if the layer test is requested and e.layer ≠ H.layer: nothing. Let (ex, ey) =
e's destination if its sub-step ≥ 2, else its position. If (ex, ey) equals H's position: nothing. Else
d = 3 if ex < H.x, 1 if ex > H.x, otherwise 2 if H.y ≤ ey, else 0 (raw map coordinates, no wrap). If H's
vehicle type is not 1/2: H.facing override = d. If e's vehicle type is not 1/2: e.facing override =
(d + 2) & 3. Then an interim redraw with present (§10.4).

The override lasts one draw (the facing fields are not changed), and events with animation type 0x16/0x18
ignore it. The map is normally not redrawn while the event's messages are shown, so the characters appear to
face each other during the conversation and revert at the next frame afterwards.

### 9.6 Followers (party trail)

The **trail** is a list of 8 motion codes, initially −1. Whenever the hero's sub-step becomes 1 (§9.1 step 7):

1. Shift: trail[7..1] = trail[6..0]; trail[0] = the hero's motion.
2. For follower i = shown − 1 down to 1: start motion trail[i] for follower i (§7.3; followers skip all
   terrain and collision tests, only the map edge of normal maps cancels a step). Then:
   * if the hero's move type is 6 and trail[i] = 17, or the move type is 7 and trail[i] = −1: follower i
     becomes invisible (0x1A);
   * else if the move type is 7 and trail[i] ≠ −1: follower i becomes visible (0x19).

So follower i repeats what the hero did i steps earlier. Followers advance right after the hero in the hero's
speed ticks (§9.1); while boarding, a follower whose predecessor (position i − 1) is invisible copies the
hero's facing after advancing. Followers register in the grid (they block events but not the hero), never
trigger events and are drawn in the leader's row (§6.6). On teleport, alighting and boarding completion the
trail is reset to −1; on teleport and alighting all party characters are put on the hero's cell.

### 9.7 Movement-control modes

A global mode set by event commands (and saved, `10-save-files.md`):

| mode | set by | effect |
|---|---|---|
| 0 | event command 39; boarding/alighting completion | normal |
| 1 | event command 38; boarding and alighting while in progress | map events and vehicles whose script is not waiting for their own route are frozen: no motion, no route, no AI, animation frozen (trigger cooldowns still count down). The player can still move and act. |
| 2 | event command 36 | the player cannot walk (keyboard, joystick, mouse drag) or use the keyboard/joystick action button; the hero's own route still runs; events run normally |
| 3 | event command 37 (only from an event with an event object) | same as 2 |

---------------------------------------------------------------------------------------------------------

## 10. Camera, scrolling and the tile layer

### 10.1 Camera origin on teleport

* Normal map: ox = x − 9, then if ox < 0 → 0, else if ox > W − 20 → W − 20; oy = y − 6, then < 0 → 0,
  else > H − 15 → H − 15.
* Looping map: ox = (W + x − 9) % W, oy = (y + H − 6) % H.
* cpx = ox × 32, cpy = oy × 32.

The hero thus appears at screen tile (9, 6) = pixel (288, 192) unless the camera is clamped.

### 10.2 Scroll flags

Four flags "scroll when the hero steps in direction d", recomputed on teleport and after every completed
hero step, from the hero screen cell (sx, sy) (§4.3, using party position 0's character):

* up = sy < 8; right = sx > 8; down = sy > 6; left = sx < 10;
* normal maps only: up = 0 if oy = 0; right = 0 if ox = W − 20; down = 0 if oy = H − 15; left = 0 if ox = 0.

When the hero steps in direction d with flag d set, the camera moves with it in 8-px sub-steps (the hero
keeps its screen cell); otherwise the hero moves on the screen and the camera stays. Result: horizontally the
hero stays in column 9 (walking left from column 10 it walks to 9 on screen). Vertically, from the initial
row 6 the first step down is walked on screen (to row 7); from then on the camera follows in both vertical
directions with the hero at row 7, until a step up is taken while at row 6 (only right after a teleport or in
the clamped top zone). Near clamped borders the hero walks freely and the camera follows again once the hero
is back in the band. On looping maps the flags are never cleared by borders.

### 10.3 Frame render (tick render)

Rendering with direction D (−1 or 0..3) and scroll sub-step value S, as done every tick:

1. Let "scrolling" = D ≠ −1 and flag D is set. If scrolling: cpx = (W × 32 + ox × 32 + dx(D) × 8 × S) % (W × 32),
   cpy = (H × 32 + oy × 32 + dy(D) × 8 × S) % (H × 32). (Otherwise cpx/cpy keep their values.)
2. Copy the **map cache** (an off-screen 640 × 480 image of the tile layer at the current camera origin) to the
   back buffer, shifted by s = 8 × S pixels opposite to the scroll direction when scrolling (written as source
   rectangle "x, y, w × h" → destination point): D = 0 (up): cache 0, 0, 640 × (480 − s) → (0, s); D = 2
   (down): cache 0, s, 640 × (480 − s) → (0, 0); D = 3 (left): cache 0, 0, (640 − s) × 480 → (s, 0); D = 1
   (right): cache s, 0, (640 − s) × 480 → (0, 0). Unshifted (whole cache to (0, 0)) otherwise.
3. If scrolling and s > 0: draw the newly exposed strip directly from the chipsets (opaque), all map indices
   modulo W/H: D = 3: for i = 0..14 the right s pixels (source x 32 − s .. 32) of tile (ox − 1, oy + i) at
   screen (0, 32i); D = 1: the left s pixels of tile (ox + 20, oy + i) at (640 − s, 32i); D = 0: for i =
   0..19 the bottom s pixels (source y 32 − s .. 32) of tile (ox + i, oy − 1) at (32i, 0); D = 2: the top s
   pixels of tile (ox + i, oy + 15) at (32i, 480 − s).
4. If S = 4 and D ≥ 0 (and this is a tick render, not an interim redraw): copy the back buffer (tiles only) into
   the map cache; then, if flag D is set, ox = (ox + dx(D) + W) % W, oy = (oy + dy(D) + H) % H.
5. Draw the characters (§6.6) with animation enabled, using cpx/cpy from step 1 (so after a completed scroll
   cpx = new ox × 32).
6. Present the back buffer if presenting was requested and the screen state is MAP (`03-display.md`; in other
   states the frame is composed but not shown).

### 10.4 Interim redraws and full tile redraws

An **interim redraw** (with or without presenting) is requested by many operations: face each other, a
route end, graphic-change commands, poison, the map being shown again, boarding/alighting completion. It:

1. Does **nothing at all** if the hero's sub-step is 4 (a step completed in this tick and not yet drawn).
2. Takes D = the hero's motion if it is a step 0..3 and sub-step % 4 > 0, with S = hero sub-step; otherwise
   D = −1 and S = 0 (this overwrites the scroll sub-step value of §9.1).
3. Renders as in §10.3 but never commits the cache or moves the camera origin (step 4 skipped); characters are
   drawn with animation frozen if not presenting, enabled if presenting.

**Quirk:** because step 2 overwrites S, an interim redraw that happens after the tick render and before the
step-completion step of the same tick (e.g. from an autorun event started in that tick) cancels the
step-completion processing of that step (no scroll-flag update, step-on, poison or encounter for it).

A **full tile redraw** draws the 20 × 15 tiles of cells ((ox + col) % W, (oy + row) % H) at (32 col, 32 row) of
the back buffer and copies the back buffer into the map cache. It happens when the map is shown again (§11.2),
after a battle and after the game-over event returns to a lost-battle map. Tiles never change at runtime.

---------------------------------------------------------------------------------------------------------

## 11. Teleport

### 11.1 Teleport procedure

Teleport to (map M, cell x, y):

1. Fail silently (no effect) if M < 0, M ≥ 500, M's width is 0, x ≥ W(M) or y ≥ H(M) (negative x/y are not
   checked).
2. Set the arrival flag; close all windows (also cancels a menu request).
3. If the screen state is not ERASED and not BEFORE_START: hide the screen with the map-change transition
   (transitions table index 0, `03-display.md`); screen state = ERASED.
4. Current width, height and cells = M's. Camera origin and pixel origin per §10.1.
5. All 8 party characters: motion −1, sub-step 0, position = destination = (x, y) (facing kept). Trail = all
   −1.
6. If riding: the ridden vehicle (vehicle slot 0) gets position = destination = (x, y), motion −1,
   sub-step 0, and its slot's map = M.
7. If M differs from the current map: current map = M; forget any script waiting for the hero
   (`05-events.md`); clear the occupancy grid; delete all map events; create the events of M (skipping event
   ids present in the vehicle table, `05-events.md`); build the area table (§1.3); select the pages of all
   events and vehicles (`05-events.md`).
8. Encounter area = current map id (§13.3); recompute the scroll flags (§10.2).
9. Mouse: remembered mouse direction = 5, drag ended; the hero's move type is not changed, so mouse auto-walk
   is **not** ended (quirk, §9.2). Discard key states without waiting.
10. If the map before this teleport was not "none" (−1): show the map (§11.2).
11. Encounter step counter = 0.

Chipsets and attributes are not reloaded and the music is not changed. A teleport to the same map keeps all
events and their states. The teleport event command additionally sets the pass-abort flag (the rest of the
pass is skipped; `05-events.md`). At a new game the first teleport happens with the "none" previous map, so
the start event runs on a hidden screen (`01-runtime.md`). Loading a save file teleports to the saved map and
leader position with the current map set to a value that matches no map (not −1), so the events are re-created
and the map is shown (`10-save-files.md`).

### 11.2 Showing the map

"Show the map" (`03-display.md` §7.2) does nothing if the screen state is MAP. Otherwise: if the state is
PICTURE_ON_BLACK or PICTURE_BEFORE_MAP: fade the screen to black and "reload map palette with tint"
(`03-display.md` §2.4); if ERASED or BEFORE_START: "realise the committed palette" (`03-display.md` §2.3;
the tint is not re-applied); in PICTURE_OVER_MAP and MOVIE no palette action. Then a full tile redraw (§10.4),
an interim redraw without presenting (animation frozen), the show transition with the map-change transition
(transitions table index 0), screen state = MAP, and any remembered map palette (`03-display.md` §7.4) is
discarded.

---------------------------------------------------------------------------------------------------------

## 12. Vehicles

### 12.1 Representation

A vehicle is an ordinary map event whose active page has vehicle type 3 boat, 4 ship, 5 balloon or 6 airship
(1 wagon and 2 sled also work but are not used for vehicles in the sample games); its page script uses the
board command (event command 47), usually on a touch or action page. The **vehicle table** has 10 slots {event id (16-bit), map id,
character}; slot 0 is the most recently boarded vehicle.

There are two kinds of scans of the table. Scans **by id** stop at the first slot whose event id is 0. Scans
**by character** look at whether a slot holds a character ("occupied"); a slot without a character is
"empty". Normally both agree (the table has no gaps and every occupied slot has a non-zero id; while riding,
slot 0 holds the real id of the ridden vehicle, whose own event id is 0). The exception is boarding in progress (§12.2, hero move type 6):
slot 0 then holds the vehicle with id 0, so it is occupied for the character scans, but the id scans stop at
it and find nothing **(quirk)** — an event-list rebuild during boarding (a teleport to another map before
the finalisation) would create the events of all vehicles again as ordinary map events. (Never seen in the
sample games.)

* Vehicles persist across map changes and are saved (`10-save-files.md`); an event id present in the table is
  not created again as a map event on any map (scan by id).
* Update (§8.1) and autorun (`05-events.md`) each tick: slots from (riding ? 1 : 0) upward, **stopping at the
  first empty slot or the first slot whose map is not the current map** (scan by character; quirk: a vehicle
  parked on another map in an earlier slot hides later vehicles of this map from updates and autorun; they
  are still drawn). During boarding the vehicle being boarded (slot 0) is therefore updated and checked for
  autorun.
* Drawing (§6.6): every slot from (riding ? 1 : 0) to 9 whose character is on the current map.
* Page re-evaluation covers every occupied slot 0..9 (empty slots are skipped, also the ridden vehicle is
  included), but **only while the current map has at least one map event in its event list** **(quirk,
  `05-events.md` §4.3)**: on a map without ordinary events (for example one whose only event was the vehicle
  now being ridden) vehicle pages are never re-evaluated.
* A parked vehicle's own page properties (layer, speed, animation, move type, route, trigger) apply to it.

### 12.2 Boarding

Board command (only from the vehicle's own event, not while riding): close all windows, then:

1. Find k = the index of the first slot whose event id equals the vehicle's id, scanning slots 0..9 while their
   event id is non-zero; if not found, k = the index of the first slot with id 0 (or 10 if all are used) and the
   vehicle is "new".
2. If k > 0: shift slots 0..k−1 to 1..k; if k = 10, the character of slot 9 is deleted first and slots 0..8
   are shifted to 1..9 (the oldest vehicle is lost).
3. Slot 0 = {id 0, map 0, the vehicle}. If the vehicle is new it is removed from the map event list (it
   becomes persistent). Slot 0's map = the current map.
4. Hero move type = 6; start motion 17 (wait) for the hero; hero display = invisible (0x1A); movement-control
   mode = 1. The event script is suspended (resumed later without an event object, `05-events.md`).

Then each pass the hero repeats waits (§9.2) and the followers walk up the trail and vanish (§9.6). When the
hero's sub-step becomes 1 and the last shown party character's motion is 17 (immediately for a party of one),
the boarding is finalised after the tick (§9.1 step 12):

1. The hero's sub-step = 0; movement-control mode = 0.
2. For every party member position: start motion −1, trail entry = −1.
3. Shown party characters = 1; party position 0's character = hero = the vehicle; vehicle move type = 5.
4. Slot 0's id = the vehicle's event id; the vehicle's event id = 0.
5. Vehicle layer = 2 if its vehicle type > 4, else 1; animation type = 0x12; riding = yes.
6. Interim redraw with present; play the music of audio slot [vehicle type] (§12.5).
7. If a script is waiting for the hero, resume it.

The camera is not re-centred on the vehicle (the scroll flags are recomputed at the next completed step). The
hidden party characters stay where they were. While riding, the hero character **is** the vehicle: its speed,
passability mask, layer, graphic and animation are the vehicle's.

### 12.3 Riding rules

* Passability: the vehicle's mask (boat: river, shallows; ship: shallows, sea; balloon: everything but 9 and
  ≥ 14; airship: 0..13).
* Boat/ship (layer 1): bump into same-layer events (touch triggers work), step-on works, encounters normal.
* Wagon/sled: encounter threshold doubled (§13.3), never turn in place.
* Balloon/airship (layer 2, drawn above everything): ignore all events (no bump, no talking, no step-on), no
  encounters (the step counter still counts).
* The action button (keyboard, double click, right+left click) means **alight** instead of talking.
* Walking onto a boat or ship lying on layer 0 is allowed for walkers and fliers regardless of terrain; such
  a vehicle usually has a touch page, so stepping onto it boards it.

### 12.4 Alighting

Triggered by the action button while riding (move type ≠ 7) or by the alight command (event command 48,
only while riding; on failure the script simply continues). Let V be the vehicle (the hero), (x, y) its
position and F = (x + fdx, y + fdy) the cell in front of its facing.

The **occupant query** for a cell: compute its screen cell as in §4.2 but applying step 3 on every map type;
if outside the screen window: "none". Else if grid[1] has a character there: "occupied". Else if grid[0] has a
character there: "occupied" if its graphic < 0x100, else its tile id (graphic − 0x100). Else "none".

1. If V's vehicle type is 5 or 6: q = occupant query at (x, y); fail if "occupied"; t = q's tile if q is a tile,
   else the map tile at (x, y); fail unless ATR[t] is passable for wagon **or** sled (attributes 0, 2, 7, 10).
2. On a normal map: fail if F is outside the map. F is wrapped modulo W/H.
3. q = occupant query at F; t = q's tile if q is a tile, else the map tile at F; fail unless ATR[t] is passable
   for walking (type 0); fail if q is "occupied".
4. Success: shown party characters = party size; party position i = the actor's party character, with position
   = destination = (x, y), facing = V's facing, trail entry i = −1. Set the pass-abort flag. Leader move type
   = 7; start a step in V's facing for the leader (the leader is not the hero yet, so no tests are made);
   leader display = visible; V's move type = 0; movement-control mode = 1; riding = no. (The command suspends
   the script.)

Then the leader waits (motion 17, §9.2) while the followers step out and become visible (§9.6). When the hero's
sub-step becomes 1 and the last shown party character's motion is 17, the alighting is finalised after the tick:

1. Leader move type = 5, sub-step = 0; movement-control mode = 0.
2. Re-apply V's current page attributes (layer, move type, animation type, display mode, speed, vehicle type;
   resume index 0 — `05-events.md`); V's event id = slot 0's id. V stays where it is, in slot 0.
3. For every shown party character: start motion −1, trail entry = −1.
4. Interim redraw with present; play the music of audio slot [leader's vehicle type] (normally 0, walking).
5. Step-on check (§9.4).
6. If a script is waiting for the hero, resume it (with its event object).

### 12.5 Music per vehicle

Audio slots 0..6 (`00-README.md`, Audio slots): 0 walking, 1 wagon, 2 sled, 3 boat, 4 ship, 5 balloon,
6 airship. "Play the music of audio slot k" plays the slot's file as BGM with the slot's parameters; an empty
name does nothing (playback rules, e.g. not restarting the same file: `09-audio-movies.md`). It happens on
boarding/alighting completion, after the start event of a new game (slot [hero vehicle type]), by event
command 32, and when the map returns after a lost battle. Event command 49 changes slots. The hero-vehicle-type
command does not play anything.

---------------------------------------------------------------------------------------------------------

## 13. Per-step effects

These run after every completed hero step (§9.1 step 11), in this order: step-on check, poison, party-down
check, encounter, party-down check. Turns, waits and steps cancelled by a bump do not count as steps.

### 13.1 Poison

For each party member position 0..size − 1 (all members, not only the fighting ones) whose HP > 0 and whose
status has bit 0x01 (poison):

1. For the first such member only: fill the whole back buffer with the palette colour nearest to red
   RGB(255, 0, 0), present it, then an interim redraw with present (this redraw also advances the animations).
2. HP −= 5.
3. If HP < 1: HP = 0, status = 0, and show `Message(<member name> + strings[45], 0.5, top)` (a timed message
   at the top, `06-windows-text.md` §9; strings[45] defaults to "は気絶した").

### 13.2 Party-down check

The party is **down** when every member of positions 0..size − 1 has HP < 1 or status bit 0x02 (paralysis); an
empty party counts as down. When down after poison, the encounter step is skipped and the game-over event runs
at the end of the pass (`05-events.md`). The same test freezes the animation of down members (§6.7).

### 13.3 Random encounters

Performed only if the hero's display mode is 0x19 and the hero has no route; let cell = hero.y × W + hero.x.

1. Encounter step counter = min(counter + 1, 10000) (counted even when flying).
2. If the hero's vehicle type ≥ 5 or its move type is 7: stop.
3. a = area table[cell] (§1.3). If a ≠ encounter area: counter = 0, encounter area = a, stop (the first step in
   a new area never encounters).
4. Draw R (discarded).
5. order = (0, 1, …, 9); for i = 0..9: j = R % 10; swap order[i] and order[j] (10 R calls).
6. For i = 0..9, with entry e = map a's encounter table[order[i]], troop p = e & 0x1FF, rate r = e >> 9:
   * if p = 0 or r = 0: skip (no R call);
   * d = max(r / 10, 1); threshold = r + (R % (2d + 1)) − d (r ± about 10 %); if the hero's vehicle type is
     1 or 2, threshold × 2;
   * if threshold ≤ counter and ATR[hero's map tile] equals the terrain field of troop p − 1 in EPARTY.DAT
     (`02-data-formats.md`; compared as a signed 16-bit value): set "escape allowed", counter = 0, start a
     battle with troop p − 1 (`08-battle.md`), stop.

The rate is thus the mean number of steps between encounters; the counter is shared by all entries and is
reset only by a battle, an area change or a teleport. A troop without monsters still resets the counter, but
no battle starts. After a battle the map comes back with the battle transition and the hero's move type is
set to 5 (`08-battle.md`). Troop numbers > 250 would read beyond EPARTY.DAT in the original (absent from the
data; treat as no match). Encounters are not suppressed by events running elsewhere; they are suppressed by
flying, alighting, an invisible/removed hero, a hero route, terrain mismatch and the first step in a new area.
There is no global "encounters off" switch.

---------------------------------------------------------------------------------------------------------

## Quirks in this chapter

* Normal maps map cells left of/above the camera to large screen columns/rows; column 0 can map to screen
  column 20 when the camera is at the right edge (§4.2).
* The alight occupant query applies the looping "−1" screen-cell rule on all map types (§4.2, §12.4).
* A boarding hero (move type 6) clears its grid entry instead of registering (§5.2).
* Party characters write themselves into the grid on every advanced sub-step, even with display 0x1B (§5.3).
* The facing override is reset only for visible sprite characters; facing follows the drawn direction at
  sub-step 3, so spinning characters change their facing and an override drawn at sub-step 3 sticks (§6.3).
* Animation types 0x17/0x18 show the stepping column when the direction comes from the facing override or a
  turn motion (§6.3).
* Rows of maps with H < 17 can be processed twice per frame: double animation speed, double R use for
  animation type 0x15 (§6.6).
* Animation type 0x15 consumes R on every frame change of every processed character (§6.2).
* Map-edge steps neither move nor turn (§7.3).
* Blocked steps turn the character toward the obstacle (except wagons/sleds and animation types 0x16/0x18)
  (§7.3).
* Wagons/sleds are stopped by any layer-1 sprite at the destination regardless of their own layer (§7.3).
* Wandering events never step outside the screen window; route events do but without collisions (§7.3).
* Events bounce at the half-step; route events wait at sub-step 1 instead (§7.4).
* Sub-step stall of characters completing a step on unvisited rows (§7.4).
* Mirrored route playback turns wait (17) into "turn left" (7) (§8.3).
* Movement-control mode 1 does not stop trigger cooldowns (§8.1, §9.7).
* Mouse action paths ignore the movement-control mode, routes and the arrival flag; auto-walk ignores
  the mode; a teleport does not end auto-walk, so the hero keeps turning right (§9.2, §11.1).
* The action button ignores the trigger type; the counter test wraps map coordinates on normal maps (§9.3).
* Step-on ignores the trigger cooldown (§9.4).
* The face-each-other override lasts one draw and does not change facings (§9.5).
* An interim redraw does nothing while the hero's sub-step is 4, and one between the tick render and the
  step-completion step cancels the step-completion processing (§10.4).
* Vehicle update/autorun stops at the first slot on another map (§12.1).
* A pass-abort flag set by an autorun vehicle only ends the vehicle autorun loop; the arrival flag is cleared and
  step completion still runs (§9.1 step 10).
* An escape procedure used in battle teleports at the battle end without setting the pass-abort flag (§1.4).
* Vehicle pages are re-evaluated only while the current map has at least one ordinary map event (§12.1).
* While boarding, slot 0 has id 0: id scans of the vehicle table stop there (§12.1).
* The oldest vehicle is deleted when an 11th distinct vehicle is boarded (§12.2).
* The leader's first step out of a vehicle is not tested at all (§12.4).
* The poison flash redraw advances animations (§13.1).
* An empty party counts as down (§13.2).
* The encounter step counter counts while flying; the first step in a new area never encounters (§13.3).

## Open questions

* Exact tick length on real hardware (see `01-runtime.md`); all durations here are in ticks.
* Teleports to area maps (type 2) and maps smaller than 20 × 15 are not guarded in the original; behaviour is
  undefined. Recommended: refuse teleports to areas; for small maps clamp the camera at 0.
* Negative area origins and negative teleport coordinates are not checked by the original; recommended: clip /
  refuse.
* Whether a talked-to character visibly keeps facing the hero after the event depends on whether the map is
  redrawn during the event (§9.5); only the one-draw override is certain.
* The trailing-part registration of an invisible wagon/sled uses an undefined facing value in the original
  (only the visible case is defined; probably the facing of a previously drawn character); recommended: use
  the character's own facing (§6.5). One sample game (月がたり, event 408) has an invisible fixed event with
  vehicle type 2, so the case can occur.
* Route bytes ≥ 32 make the original hang; recommended: end the route (§8.3).
* Attribute names other than the numbers are editor names only; their meaning to players is not certain.
