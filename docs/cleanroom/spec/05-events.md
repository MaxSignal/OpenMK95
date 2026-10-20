# 05 — Events

This chapter describes event objects and their pages, page conditions, the triggers that start events,
the event runner (including suspension and resumption), every event command, and how events use move
routes. Terms such as screen state, window slot, Wait(n), message speed, battler record, transitions
table and audio slots are defined in `00-README.md` §5.

Things described elsewhere and only referred to here:

* the byte layout of event files and the string-list parsing algorithm: `02-data-formats.md`, Event files;
* movement, the occupancy grid, passability, followers, route byte meanings, vehicle boarding/alighting
  mechanics and teleport: `04-map-movement.md`;
* window drawing, message and choice window layout and text drawing: `06-windows-text.md`;
* pictures, tints, screen transitions and showing the map: `03-display.md`;
* shops and inns: `07-menus-shop-inn.md`; battles: `08-battle.md`; music, sounds and movies:
  `09-audio-movies.md`; saving: `10-save-files.md`; the main loop, input, Wait(n) and random numbers:
  `01-runtime.md`.

---------------------------------------------------------------------------------------------------------

## 1. Terms and shared state

**Event id** — a number from 0 to 99999. The event with id *n* is stored in the file `eveNNNNN.dat` (five
decimal digits with leading zeros, e.g. `eve00012.dat`) in the game folder, matched case-insensitively.
Ids 1..32767 are placed on map tiles; id 0 is the **start event** (§6.6) and id 99999 the **game-over
event** (§6.7). Every sample game has both special files.

**Event file** — a fixed header of 0x88 bytes with the attributes of up to 4 **pages** (page 0..3,
shown as pages 1..4 in the editor), followed by the 4 page move routes, the 4 command lists, a string
list and the command move routes (`02-data-formats.md`, Event files). In this chapter:

* *cmd* = one command, five signed 32-bit numbers **{code, a1, a2, a3, a4}**;
* *str[i]* = the string with index *i* of the event's string list (not the built-in strings[n] table). A
  string index that is referenced but not present in the list is treated as the empty string **(the
  original uses an invalid text in that case; never present in the sample games)**;
* *route[i]* = the *i*-th command move route (0-based) of the event file;
* *page route p* = the move route stored for page *p*.

**Event object** — the map character created for an event placed on the current map (§3). An event object
has all the state of a map character (`04-map-movement.md`, Characters: position, destination, facing,
one-draw facing override, move type, motion, graphic, sub-step, animation timer and frame, layer, speed,
speed counter, vehicle type, animation type, display mode, move route with its position, mode and step
direction) plus:

| state | values | initial value |
|---|---|---|
| event id | 1..32767 | from the map |
| page index | −1 (none yet), 0..3 | −1 |
| page attributes | the 4 × 22 bytes of header offsets 0x0C..0x63, copied verbatim | from the file |
| page routes | the 4 page move routes | from the file |
| trigger cooldown | integer (§5.7) | 1 |
| resume index | 0 = not waiting for its own route; > 0 = command index to resume at (§6.5) | 0 |
| on-screen flag | 1 if the object was registered in the occupancy grid by the last redraw that visited its map row (§5.5) | 0 |

The other initial values of a new map character are: facing 2 (down), one-draw facing override 2,
motion idle, sub-step 0, layer 1, speed 2, vehicle type 0, animation type 0x12, display 0x19 (normal),
no move route. Page selection (§4) overwrites most of them immediately; the facing is only overwritten
when the page graphic is a character graphic (§4.2), so an event whose graphic is a map chip keeps facing
2 (down).

Global state used by the event system (all names are descriptive):

* **switches** 0..1023, **item counts** (100 signed 16-bit counters), **money**, the **party** (list of
  actor ids, party size, party bitmask: bit *a* set if actor *a* is a member), the 8 battler records and the
  per-actor equipment and learn tables (`00-README.md` §5, `02-data-formats.md`);
* **page refresh request** flag (§4.3);
* **event running** flag — set while the runner executes;
* **hero-wait record** — {waiting event id (−1 = none), saved page, saved resume index, saved object (an
  event object or "none")}; describes the one event that is suspended until the hero finishes a move route
  or a vehicle change (§6.5);
* **suspend flag** — tells the runner to stop after the current command;
* **movement control mode** 0..3 (§8, codes 36..39; saved in save files);
* the shared run-time flags of `00-README.md` §5 (defined in `01-runtime.md` §1), used here under exactly
  these names:
  * **pass-abort flag** — makes the main loop abandon the rest of the current loop pass; set in this chapter
    by code 5 (teleport), code 30 (end game) and the start of alighting (code 48); the complete list of what
    sets it is in `01-runtime.md` §1;
  * **arrival flag** — set by every teleport; until it is cleared after the autorun step of the next tick,
    hero input (in every pass) and the event/vehicle updates are skipped (`04-map-movement.md` §9.1);
  * **post-pass action** — code 5 sets it to "teleported" (no further effect);
  * **battle-lost flag**, **confirm flag**, **release latch**;
* **quit flag** (`01-runtime.md` §1);
* **current hero** — the leader's map character, or the vehicle object while the party rides one;
* the **vehicle table** (§3.3).

---------------------------------------------------------------------------------------------------------

## 2. What is read from an event file, and when

There are two separate reads.

**Map read (creating an event object).** Reads only the header and the page routes: the file must exist
and have the signed 32-bit value 0x88 at offset 0x00; otherwise the event is not created (§3.1). The file
length is not checked (a shorter file would give undefined header and route contents; such files never occur
— an implementation should treat a file shorter than 0x88 bytes, or one whose page routes are cut off by the
end of the file, as not readable). The 88 bytes at 0x0C..0x63 are copied into the object as its page attributes, and the
four page move routes (each a length byte followed by that many route bytes, starting at 0x88) are stored
in the object. Commands and strings are not read.

The page attributes, per page *p* = 0..3 (offsets are file offsets; see `02-data-formats.md` for the exact
layout):

| file offset | type | field | meaning |
|---|---|---|---|
| 0x0C + p | u8 | trigger | 0 action button, 1 touch, 2 autorun |
| 0x10 + 4p | u32 | condition flags | bit 0 item, bit 1 money, bit 2 switch, bit 3 party; other bits ignored |
| 0x20 + p | u8 | display | 0x19 normal, 0x1A invisible, 0x1B removed |
| 0x24 + p | u8 | layer | 0 below the hero, 1 same as the hero, 2 above the hero |
| 0x28 + p | u8 | move type | 0 fixed, 1 approach the hero, 2 flee from the hero, 3 random |
| 0x2C + p | u8 | animation type | 0x12..0x18 (`04-map-movement.md`, Animation) |
| 0x30 + p | u8 | speed | 1..20 (ticks per 8-pixel frame; larger = slower) |
| 0x34 + p | u8 | vehicle type | 0 walking, 1 wagon, 2 sled, 3 boat, 4 ship, 5 balloon, 6 airship |
| 0x38 + 2p | u16 | condition item | item number 0..99 |
| 0x40 + p | u8 | condition party mask | bit *a* = actor *a* must be in the party |
| 0x44 + 4p | u32 | condition money | money must be ≥ this value (unsigned comparison) |
| 0x54 + 2p | u16 | condition switch | switch number (0..999 in practice) |
| 0x5C + 2p | u16 | graphic | < 0x100: bits 0..5 = character number in CHARA.BMP, bits 6..7 = initial direction; ≥ 0x100: map chip number + 0x100 |

**Run read (starting an event).** Every time an event is started or resumed, the whole file is read
again: header, page routes (read and ignored), the four command lists (the counts are the four signed
32-bit numbers at 0x74..0x83), the string list (its byte size is at 0x84) and then command move routes
until the end of the file. The run read fails — and the event does not run at all (in particular the
trigger cooldown is not changed, §5.7) — if the file is missing, the header size is not 0x88, or the string
list is not accepted (`02-data-formats.md` §4.4). (The original does not check the command counts or the file
length; files with a negative command count or a command list cut off by the end of the file never occur —
an implementation should treat them as a failed read.) After every run that executed (also after a
suspension) everything that was read is discarded; after a failed read, strings already stored are kept
(`02-data-formats.md` §4.4).

The original keeps at most 100 command routes (the sample games use at most 80); an implementation
should accept any number. Command lists always end with a command of code 0; nothing after the first
code 0 of a page is executed. Arguments that a command does not use contain editor garbage and must be
ignored.

---------------------------------------------------------------------------------------------------------

## 3. Event objects on a map

### 3.1 Creating the events of a map

When the current map changes (a teleport to a different map, the start of a new game, loading a save;
`04-map-movement.md`, Teleport), after the old map's event objects have been deleted:

1. Scan the new map's cells row by row (y = 0 .. height−1, and within a row x = 0 .. width−1). A cell
   whose event id (from the event-placement stream, `02-data-formats.md` §3.8; `04-map-movement.md` §1.1) is
   non-zero places that event.
2. If the id is present in the vehicle table (§3.3; searched by id up to the first entry with id 0), skip
   it: the vehicle object exists already.
3. Otherwise read the event file (map read, §2). If that fails, the event id is **erased from the tile**
   (for the rest of the session; the map is not re-read from the game file) and nothing is created.
4. Otherwise create an event object with the event id, position = destination = (x, y), the page
   attributes and page routes, and the initial values of §1, and append it to the map's **event list**.

The event list order (row-major tile order) is the order in which events are updated, drawn within a
row, checked for autorun and re-evaluated. After creation all pages are selected (§4.3, "refresh all
pages"). A teleport to the **same** map does not recreate or reset any event.

### 3.2 Display modes

* **0x19 normal** — drawn and registered in the occupancy grid.
* **0x1A invisible** — not drawn, but registered: it blocks movement and can be triggered.
* **0x1B removed** — neither drawn nor registered, does not move, does not block, cannot be triggered
  (action button, touch, step-on and autorun all need the grid or the on-screen flag; a removed event on
  a map row visited by the draw gets on-screen flag 0, see §5.5 for unvisited rows), but it still takes
  part in page re-evaluation, so a page change can bring it back.

There is no "erase event" command. An event disappears through a page whose display is 0x1B or through
route byte 0x1B (display "removed"). Nothing about events persists when the map is re-entered (see §3.4).

### 3.3 Vehicles as events

A vehicle is an ordinary event whose page has a vehicle type and whose script uses command 47 (board).
The **vehicle table** has 10 entries {event id, map id, object}; entries are used from entry 0 upward
without gaps, entry 0 being the vehicle boarded most recently.

* Boarding (command 47) moves the object to entry 0 (§8, code 47); it is then removed from the map's event
  list and persists across map changes. While the party rides it, the object **is** the current hero: its
  own event id field is set to 0 and the real id is kept in entry 0 of the table.
* On map creation, ids found in the vehicle table are not created again (§3.1).
* Vehicles are updated each tick from entry (riding ? 1 : 0) upward, **stopping at the first empty entry or
  at the first entry whose map is not the current map** **(quirk)**: a vehicle in a later entry on the
  current map is frozen while an earlier entry holds a vehicle on another map. They are drawn for every
  entry on the current map, and checked for autorun with the same stop rule as for updates (§5.5).
* Page refresh includes every occupied vehicle entry (§4.3), also the ridden vehicle — but only while the
  current map's event list is not empty **(quirk)**.
* "Empty entry" means an entry without an object; searches by event id (map creation, command 47) instead stop
  at the first entry with id 0. The two differ only while boarding is in progress (entry 0 then holds the
  vehicle with id 0; `04-map-movement.md` §12.1).
* Command 54 deletes all vehicles that are not ridden (§8, code 54); afterwards their ids are no longer in
  the table, so they reappear at their original placement the next time their map is created.

### 3.4 Persistence

Ordinary map events are not saved. Loading a save file re-creates the events of the saved map from the
event files and selects their pages, so routes, positions changed by movement, cooldowns, graphics or move
types changed by commands, and display changes made by routes are all lost. Vehicles are saved with their
full state (`10-save-files.md`). The hero-wait record is not saved (it is "none" after loading).

---------------------------------------------------------------------------------------------------------

## 4. Pages

### 4.1 Selecting a page

Input: an event object, a table of 100 item counts, the money. (The caller decides which item counts are
used, §4.3.)

1. For p = 3, 2, 1 (in this order):
   1. Let c = the condition flags of page p.
   2. If none of these is true, skip page p: c bit 0; c bit 1; c bit 2; (c bit 3 and the page's party mask
      is not 0). *A page without any condition is never chosen.*
   3. If c bit 0 is set and the item count of the page's condition item is 0, skip page p.
   4. If c bit 1 is set and money (as unsigned) < the page's condition money (unsigned), skip page p.
   5. If c bit 2 is set and the page's condition switch is OFF, skip page p.
   6. If c bit 3 is set and (party mask of the page AND party bitmask) ≠ party mask of the page, skip
      page p. (An empty mask passes this test.)
   7. Otherwise choose page p and stop.
2. If no page 3..1 was chosen, choose page 0. **Page 0 is the default page; its conditions are never
   evaluated.**
3. If the chosen page equals the object's current page index, **nothing happens** (the page is not
   re-applied) and the result is "unchanged".
4. Otherwise set the page index and apply the page (§4.2); the result is "changed".

All set conditions of a page must hold together (logical AND). The highest-numbered qualifying page wins.
Item numbers ≥ 100 and switch numbers ≥ 1024 never occur in the sample data (UNCERTAIN what the original
does with them; treat the condition as not satisfied).

### 4.2 Applying a page

In this order:

1. **Graphic** g = page graphic. If g < 0x100: graphic = g AND 0x3F, facing = g >> 6, and the one-draw
   facing override = g >> 6. Else graphic = g (a map chip; facing unchanged).
2. **Trigger cooldown** = 1 (this arms autorun, §5.7).
3. If the object is **not** the current hero: layer = page layer, move type = page move type. (For the
   ridden vehicle these two are not changed.)
4. Animation type, display mode, speed and vehicle type = the page's values.
5. Resume index = 0.
6. **Move route**: the previous route is discarded. If the page route has length > 0, it is installed as
   the object's route (route mode "running", position 0, step direction +1; `04-map-movement.md` §8.3, Move
   routes); otherwise the object has no route (mode "none").

Position, destination, motion, sub-step, speed counter and animation frame/timer are kept (a character in
the middle of a step finishes it).

The same page attributes are re-applied (steps 3..5 only, layer/move type only if not the hero) when the
party alights from a vehicle (§8, code 48).

### 4.3 When pages are re-evaluated

**Refresh all pages** is this procedure:

1. Make a copy of the 100 item counts. For every party member (members only, in party order), for each
   of the 5 equipment slots of that member's actor that is not empty, add 1 to the copied count of the
   equipped item. **Equipped items satisfy item conditions.**
2. For every object of the map's event list (in list order); then, **only if the map's event list is not
   empty (quirk)**, for every vehicle-table entry 0..9 that holds an object (empty entries are skipped, the
   scan does not stop at them): skip the object if its resume index ≠ 0 (it is waiting for its own route), or
   if it is the saved object of the hero-wait record and the record's saved resume index is ≥ 1 (the test
   uses the record's saved object and index, not its id; see §6.5 for a failed code 48). Otherwise select its
   page (§4.1) with the copied counts and the money.

   So on a map whose event list is empty (no ordinary events, e.g. a map whose only event is the vehicle now
   being ridden — a boarded vehicle leaves the event list), vehicle pages are never re-evaluated, neither by
   the end-of-pass refresh nor by the immediate refreshes below.

Refresh all pages is run:

* **at the end of every main-loop pass, if the page refresh request flag is set** (the flag is then
  cleared, `01-runtime.md` §8, The main map loop). The flag is set by commands 10 (money), 15 (switch, only when it
  counts as changed), 20 (item), 23 (party, only when applied) and by a shop visit in which at least one
  item was bought or sold (`07-menus-shop-inn.md`);
* immediately on map creation (§3.1) and after loading a save;
* immediately when an item count drops to 0 by using the item (field menu or battle,
  `07-menus-shop-inn.md`, `08-battle.md`);
* immediately when a battle drop makes an item count become exactly 1 (`08-battle.md`).

Consequences: page changes requested by event commands take effect after the event has finished (after
the main-loop pass), never while it runs. The two "immediate" cases can happen during an event (an item
used up in a battle started by the event, a battle drop); they can change the page of the running event's
object, but the running command list is not affected (the runner keeps its own page number, §6.2). The
event-start code at the end of a run uses the object's then-current page for the cooldown (§6.4).
Equipment changes, HP changes etc. do not request a refresh by themselves.

Switch storage: switch n is bit (n mod 32) of 32-bit word (n div 32), i.e. bit (n mod 8) of byte (n div 8)
of the 128-byte switch block (`10-save-files.md`). Reading a switch in the original yields the masked bit
value (2^(n mod 32) or 0), not 0/1; this matters only for command 15 (§8, code 15).

---------------------------------------------------------------------------------------------------------

## 5. Triggers

All event starts go through the runner (§6). The only sources are:

1. the action button (§5.2),
2. touch by bumping (§5.3),
3. touch by stepping on (§5.4),
4. autorun (§5.5),
5. resumption of a suspended event (§6.5),
6. the special events 0 (new game, §6.6) and 99999 (game over, §6.7).

No event can start while another runs (§6.1). Event commands never start other events.

### 5.1 Facing each other

Used by the action button and by bumping, before the event runs. Let h = the leader's map character (the
current hero) and e the event object.

1. (Action button only) If e's layer ≠ h's layer, skip the rest.
2. Let (ex, ey) = e's position, or e's destination if e's sub-step ≥ 2. If (ex, ey) equals h's position,
   skip the rest.
3. d = 3 (left) if ex < h.x; else 1 (right) if ex > h.x; else 2 (down) if h.y ≤ ey; else 0 (up).
   (Map coordinates, no wrapping on looping maps.)
4. If h's vehicle type is not 1 or 2: h's one-draw facing override = d. If e's vehicle type is not 1 or
   2: e's one-draw facing override = (d + 2) mod 4.
5. Interim redraw with present (`04-map-movement.md` §10.4).

The override affects only the next draw (and is ignored by animation types 0x16 and 0x18): the event is
shown facing the hero while it runs — the map is not redrawn during the event — and returns to its own
facing at the next normal redraw.

### 5.2 Action button

Pressed when the main loop sees the confirm key (Space, Return, joystick OK button; `01-runtime.md`, Input)
while: the hero's motion is idle, the arrival flag is clear, the movement control
mode is ≤ 1, the hero has no move route, and the release latch is set (`01-runtime.md` §4.3). The mouse
equivalents (double click, right+left click, `01-runtime.md` §4.9) start the same procedure without the
movement-control, route, arrival-flag and latch conditions **(quirk)**.
If the party rides a vehicle and the hero's move type is not 7 (alighting), the press means "alight"
(`04-map-movement.md`, Vehicles) and no event is looked for. Otherwise:

1. Let h = the current hero. Stop if h's vehicle type is 5 or 6, or h's move type is 7, or h's sub-step
   ≥ 1.
2. (sx, sy) = h's screen cell = ((h.x − scroll x + W) mod W, (h.y − scroll y + H) mod H) with the map size
   W × H and the scroll origin in tiles; (dx, dy) = the step offset of h's facing.
3. **Front:** if (sx+dx, sy+dy) lies within columns −1..20 and rows −1..15, e = the occupancy-grid entry
   of the **same layer** (layer 1) at that cell; if e is a party member or the ridden vehicle (event id
   0), e = none.
4. **Counter:** if e is none and (sx+2dx, sy+2dy) lies within −1..20 / −1..15 and the terrain attribute of
   the map tile in front of h (map position (h.x+dx, h.y+dy), wrapped, the tile itself, not a chip event)
   is 13 (counter), e = the same-layer grid entry at (sx+2dx, sy+2dy), again ignoring event id 0.
5. **Above:** if e is none, e = the grid entry of layer 2 (above) at the hero's own cell (sx, sy).
6. **Below:** if e is none, e = the grid entry of layer 0 (below) at (sx, sy).
7. Stop if e is none, e is h, e's resume index ≥ 1, or e's trigger cooldown > 0.
8. Face each other (§5.1, with the layer test). Run the event (§6) with e's id and object e.
9. Afterwards flush keys and **wait for release** (§5.8).

The trigger type is **not checked**: the action button starts action-button, touch and autorun pages
alike, provided the cooldown allows it (autorun pages after their run have cooldown −1, which allows it,
§5.7). Same-layer events are activated from the adjacent cell in front (or across a counter cell);
below- and above-layer events only when the hero stands on their tile.

### 5.3 Touch by bumping

* **The hero bumps into an event:** when the hero, at the first frame of a step, finds a same-layer event
  (event id ≠ 0) in the occupancy grid at its destination, the hero's step is cancelled (`04-map-movement.md`,
  Advancing a motion) and that event is reported, provided the hero's display is not 0x1B and its vehicle
  type is not 5 or 6.
* **An event bumps into the hero:** when an event, at the half of its step (sub-step 2), finds the hero at
  its destination, it reverses or pauses (`04-map-movement.md`) and reports itself.

In both cases the touch check runs with the reported event e:

1. Requirements: the current hero h's vehicle type is not 5 or 6; h's move type ≠ 7; h's sub-step < 1;
   e ≠ h; **e's trigger (of its current page) = 1**; e's trigger cooldown = **0** exactly; e's resume index
   < 1. Otherwise nothing happens.
2. Face each other (§5.1, without the layer test). Run the event (§6).
3. Flush keys and wait for release (§5.8).

### 5.4 Touch by stepping on

After every completed step of the hero (the step-completion stage of the tick, `01-runtime.md` §8, The main
map loop; also after alighting, §8 code 48):

1. Let h = the current hero. Stop if h's vehicle type is 5 or 6, h's move type is 7, or h's sub-step ≥ 1.
2. e = the grid entry of layer 2 (above) at the hero's screen cell; if none, the entry of layer 0 (below).
   Stop if none or e is h.
3. Stop if e's trigger ≠ 1 or e's resume index ≥ 1. **The cooldown is not checked.**
4. Face each other (never has an effect here, the layers differ). Run the event (§6).
5. Flush keys **without** waiting for release (§5.8).

Same-layer events can never be stepped on (they block the hero).

### 5.5 Autorun

In every tick, after the redraw that rebuilt the occupancy grid and the on-screen flags:

1. For every object of the map's event list in list order: if its trigger cooldown = 0, the trigger of its
   current page = 2 and its on-screen flag is 1, run it (§6). If the pass-abort flag is then set, stop the
   pass (the rest of the tick is abandoned).
2. Then for the vehicle-table entries from (riding ? 1 : 0) upward, stopping at the first empty entry or
   the first entry on another map: the same test and run. The pass-abort flag is tested after each
   vehicle too, but **(quirk)** a set flag only ends this vehicle loop: the tick continues with clearing
   the arrival flag and with the step-completion stage (scroll flags, key flush and the step-on check of
   §5.4 still run; poison and encounters are skipped because the flag is set), and only then reaches the
   post-pass actions (`01-runtime.md` §8 steps 11.4–11.6).

No other test is made (neither the display mode nor the position). An autorun page runs only when the
event's on-screen flag is 1. The draw sets the flag for every event on the 17 map rows it visits (1 if the
event was registered in the occupancy grid, else 0); an event on a row that is not visited keeps the flag
from the last draw that visited its row (`04-map-movement.md` §5.2, §6.6). So an autorun event that has
scrolled out vertically (more than one row above or below the screen) still qualifies if it was registered
when its row was last drawn **(quirk)** — e.g. an autorun page activated by a switch while the player is
elsewhere on a tall map. Removed events and events on visited rows but horizontally outside the screen
window do not qualify.

### 5.6 Concurrency of triggers within a tick

Several events can run one after the other in the same tick (e.g. two autorun events, or a bump during
the event updates followed by an autorun). Each run is complete (or suspended) before the next starts.

### 5.7 Trigger cooldown

* Applying a page sets the cooldown to 1 (§4.2).
* Every event update (once per tick per updated object, `04-map-movement.md`, per-event update) first
  decrements the cooldown by 1 if it is > 0. This happens even in movement control mode 1 (events
  stopped) but not in a tick where event updates are skipped (arrival flag), and not for vehicles that the
  update scan does not reach (§3.3).
* After every run of an event that has an object — normal end or suspension — the cooldown is set to
  **−1 if the trigger of the object's current page is 2 (autorun), else 10**.
* If the run read fails (§2) the cooldown is not changed.

Consequences:

* An autorun event runs **once per page activation**: map entry or a page change sets the cooldown to 1;
  the first tick with event updates makes it 0, and the autorun pass of that same tick runs it; afterwards
  it stays −1 until the page changes again. On map arrival the first tick skips event updates (arrival
  flag), so an autorun event starts in the second tick after the arrival.
* An action-button event cannot be re-triggered by the action button for 10 ticks after it ran; a touch
  event cannot be re-triggered by bumping until its cooldown is exactly 0 again; stepping on ignores the
  cooldown.
* An autorun page can be started again by the action button (cooldown −1 is not > 0).
* **(quirk)** If an event teleports (code 5), the runner forgets its object (§6.3) and does not set the
  cooldown: an autorun event that teleports to the same map runs again in the next tick in which it
  qualifies, and an action-button event that teleports within the map can be re-triggered at once.

### 5.8 Key flush after a triggered event

After an event started by the action button or by bumping: the pressed-state of the arrow keys, Space and
Return is cleared, the runtime waits until Escape is not held, then waits until the release latch is set
(`01-runtime.md` §4.3, §5.2) and discards all pending keyboard input. After an
event started by stepping on, only the first part (clear, wait for Escape release) is done.

---------------------------------------------------------------------------------------------------------

## 6. The event runner

### 6.1 Concurrency

Exactly one event executes at a time, and execution is **synchronous**: the runner executes the command
list inside one call; commands that take time (messages, waits, choices, battles, shops, inns, movies,
transitions, the save dialog) block in their own waiting loops (Wait(n), `01-runtime.md`). While an event
runs, the main loop does not run: **no map tick happens, nothing on the map moves or animates, and the map
is not redrawn** except where a command explicitly redraws it. Nesting is impossible; no command starts
another event.

"Parallel" behaviour exists only through **suspension**: a command that gives a move route to the hero or
to the event itself (codes 33, 35) or a vehicle command (47, 48) makes the runner return to the main loop;
the event is restarted later from the saved command (§6.5). At any time there is at most one event
waiting for the hero (the hero-wait record) and any number of event objects waiting for their own routes
(resume index > 0). While suspended the world runs normally; other events may start (autorun, touch, the
action button is blocked only while the hero has a route).

### 6.2 Starting a run

Input: an event id and an event object (or none).

1. Set the event running flag; clear the suspend flag.
2. Perform the run read (§2). If it fails: clear the event running flag and return. Nothing else happens
   (strings already read stay stored, §2).
3. Choose (page, index):
   * **id = 0** (start event): if the hero-wait record's id is 0: page and index from the record, then set
     the record's id to −1 (none). Otherwise page 0, index 0.
   * **id = 99999** (game over): if the record's id is 99999: page and index from the record, record id =
     −1. Otherwise create a temporary event object from the file's header (map read of event 99999),
     select its page (§4.1) with the **plain item counts (no equipment bonus)** and the money, take that
     page, index 0, and discard the temporary object.
   * **other id, no object** (resumption after a hero or vehicle wait, or an event that lost its object):
     page and index from the record; record id = −1.
   * **other id, with object**: page = the object's page index; index = the object's resume index; set the
     object's resume index to 0; if the object's event id equals the record's id, record id = −1.
4. If the record's saved object equals the given object (this includes "none = none"), clear the whole
   record (id −1, page 0, index 0, object none).
5. Begin executing at command *index* of page *page*.

### 6.3 Executing

Repeat while the current command's code is not 0 and the quit flag is clear:

1. Increase *index* by 1 (it now names the command after the current one). Let *code* = the current
   command's code and *next* = the code of the command that physically follows it in the list.
2. Suspension marking, before the command executes:
   * if (code = 33 and next ≠ 35) or code = 47 or code = 48: set the hero-wait record to {id = this
     event's id, page, index, object = none if code = 47, else the current object (possibly none)}; set the
     suspend flag; if there is an object, set its resume index to 0;
   * else if there is an object and code = 35 and next ≠ 33: set the object's resume index = index; set the
     suspend flag.
3. Execute the command (§8). The command returns 0, −1 (a ChoiceOption was reached by normal flow) or a
   positive choice label.
4. Branching:
   * result −1: let d = the high 16 bits (signed) of a4 of this ChoiceOption. Move forward from it (each
     step increasing *index*) until a command with code 0, or a command with code 3 whose high 16 bits of
     a4 equal d − 1, is current;
   * result L > 0: move forward from the ShowChoice until a command with code 0, or a command with code 12
     whose low 16 bits of a4 (signed) equal L, is current;
   * result 0: nothing.
5. If code = 5 (teleport) and the object is not the current hero: **forget the object** (the rest of the
   run has no object). This happens whether or not the teleport succeeded.
6. If code = 33: apply leading display bytes of the hero's route (§9) and copy the hero's display mode to
   all 8 party map characters. If code = 35 and there is an object: apply leading display bytes of the
   object's route.
7. Stop executing if (code = 4 and the battle-lost flag is set) or the suspend flag is set.
8. Advance to the command after the current one (after the found ChoiceOption / EndChoice, or simply the
   next command).

If a branch search of step 4 stops at the code-0 command, the run ends there (the original moves past the
end of the list in that case — undefined; never happens with editor-made data, where every label and every
EndChoice exists).

The run ends when the current command has code 0, when the quit flag is set (code 30 or closing the
program), after a lost battle (code 4) or on suspension.

### 6.4 Ending a run

1. Discard everything that was read from the file. If the message window (slot 0) exists, it is
   destroyed (without waiting). If a movie window still exists it is closed and the sound system is
   re-initialised (`09-audio-movies.md`).
2. If the run has an object (not forgotten): set its trigger cooldown (§5.7) from the trigger of its
   **current** page index.
3. "Close the message window" again. Because step 1 has already destroyed it, this does nothing at all: in
   particular nothing is presented and the back buffer is **not** copied to the visible screen here. (The
   destruction in step 1 only uncovers the window's area, which is repainted as described in
   `03-display.md` §5.5.) So an erased screen or a picture stays as it is unless step 4 shows the map.
4. Let *busy* = (screen state is ERASED) and (the current hero has a move route, or the object exists and
   has a move route). If the quit flag is clear, the party is not defeated (§6.7) and not *busy*: **show the
   map** (`03-display.md` §7.2; `04-map-movement.md` §11.2): if the screen state is not MAP, the map is redrawn
   and shown with the map-change transition (transitions table index 0), the screen state becomes MAP. In particular a picture shown
   over the map (PICTURE_OVER_MAP) or on black (PICTURE_ON_BLACK) and an erased screen are replaced by the
   map at the end of the event.
5. Clear the event running flag.

The *busy* rule lets an event erase the screen, start a hero or event route and suspend without the map
being shown; the map comes back at the end of the resumed run.

### 6.5 Suspension and resumption

Suspending commands (marked in §6.3 step 2, executed, then the runner stops after them):

| command | waits for | record / state | resumed by |
|---|---|---|---|
| 33 hero move route (next command not 35) | the hero's route to end | hero-wait record {id, page, index, object} | main loop, hero route ended |
| 35 event move route (next command not 33, object exists) | the object's own route to end | object's resume index = index | the object's update, route ended |
| 47 board vehicle | boarding to complete | hero-wait record with object none | boarding finalisation |
| 48 leave vehicle (if alighting starts) | alighting to complete | hero-wait record with the object | alighting finalisation |

A 33 immediately followed by 35 (or a 35 immediately followed by 33) does not suspend at the first of the
two: both routes are started, and the suspension happens at the second command (waiting for the hero if
it is 33, for the event if it is 35). This applies to any chain: in 33, 35, 33 only the last command
suspends. A 35 without an object does nothing and does not suspend. A 47 that is not executed (no object,
or already riding) still suspends **(quirk)**: the record is set and the suspend flag stops the run; since
no boarding finalisation will come, the event is resumed only if some other hero route completes later
(the record then names it) — UNCERTAIN whether this ever happens in practice; never in the sample games.
The same holds for a 48 executed while the party is not riding **(quirk)**. A 48 whose alighting fails sets
the record's id to −1 and clears the suspend flag, and the event simply continues; the record's saved object,
page and index are kept, so that object is still excluded from page re-evaluation (§4.3) until the record is
cleared or overwritten (the object starts another run, another event suspends for the hero, or the map
changes) **(quirk)**.

**Resumption points:**

* **Hero route ended** (main loop, `04-map-movement.md`, the hero's route step reports the end): if the
  record's id ≠ −1: if the record has an object, set that object's resume index = the record's index and
  its page index = the record's page; then run (record id, record object).
* **Own route ended** (per-event update of an object whose resume index > 0, when the route step reports
  the end): run (object's id, object). If the pass-abort flag is then set, the update stops
  (`04-map-movement.md` §8.1 step 4).
* **Boarding finalisation** (`04-map-movement.md`, Vehicles): at its end, if the record's id ≠ −1, run
  (record id, none).
* **Alighting finalisation**: after the step-on check (§5.4), if the record's id ≠ −1: same as "hero route
  ended".

On resumption the file is read again and execution continues at the saved index with the saved page,
**regardless of the event's current page conditions** (page re-evaluation is suppressed for waiting
events, §4.3).

**Losing a resumption:**

* Every map change clears the hero-wait record (`04-map-movement.md`, Teleport); events waiting for their
  own routes on the old map are deleted with it.
* **(quirk)** A second event that suspends for the hero (or for a vehicle) overwrites the record; the first
  one is never resumed.
* **(quirk)** An event waiting for the hero (record set, its own resume index 0) can be triggered again
  after its cooldown (e.g. touch by an event bumping into the hero during the hero's route). It then starts
  at its page's resume index 0 (from the beginning), the record is cleared (id matches, object matches),
  and the pending resumption is lost.
* Loading a save clears the record.

### 6.6 The start event (id 0)

At "new game" (`01-runtime.md`, New game): the screen state is BEFORE_START, the current map is −1, the
party is teleported to the start position from the game file (this creates the map's events and selects
their pages; because the previous map is −1 the map is not shown yet). Then:

1. Run event 0 with no object: always page 0 from command 0 (page conditions are not evaluated). Codes
   35, 37, 42, 43 and 44 do nothing; code 47 does nothing but still suspends the run (§6.5 quirk), so the
   rest of the start event is not executed then.
2. Play the music of the audio slot whose index is the current hero's vehicle type (normally slot 0,
   walking), if that slot has a name (`09-audio-movies.md`; this replaces music started by the start event
   only if the names differ).
3. Show the map (`03-display.md` §7.2, Show map).

A teleport inside the start event shows the map immediately (the previous map is no longer −1).

### 6.7 The game-over event (id 99999)

At the end of every main-loop pass (`01-runtime.md` §8, The main map loop), if the battle-lost flag is set or **the
party is defeated** — every party member (all members, in party order) has HP < 1 or the paralysis bit
(status 0x02) set:

1. Run event 99999 with no object (page chosen by the conditions of its header with plain item counts, or
   the saved position if it was suspended).
2. If the quit flag is set, exit the main loop.
3. If the battle-lost flag is set and the screen state is still ERASED: play the vehicle music
   (as in §6.6 step 2), redraw the map tiles and characters (a full tile redraw and a character draw with
   animation advancing, without presenting), show the screen with the battle transition (transitions table
   index 1), screen state MAP. If the event already brought the map back, nothing is done here.
4. Clear the battle-lost flag.

If the game-over event neither ends the game (code 30) nor revives the party, it runs again at the end of
every main-loop pass.

### 6.8 Order of event processing in a tick (summary)

The authoritative main loop is in `01-runtime.md`. The event-relevant order within one tick is: hero
frame (bump → touch) → followers → vehicle updates → map event updates in list order (cooldown countdown,
own-route resumption, event bumps → touch) → redraw (rebuilds the grid and on-screen flags) → autorun pass
(map events, then vehicles) → step-completion checks (step-on touch, poison, encounters) → boarding or
alighting finalisation → game-over check (event 99999) → page refresh if requested. Action-button input
and the hero's route step (hero-route resumption) are processed on every loop pass before the tick.

---------------------------------------------------------------------------------------------------------

## 7. Value conventions used by the commands

### 7.1 Random range

**RangeValue(lo, hi)**: if lo < hi: lo + (R mod (hi − lo + 1)), where R is **one** draw from the global
random generator (`01-runtime.md`, Random numbers; R is a non-negative 31-bit number) and mod is the C
remainder; otherwise lo, **without** a draw. Every "random range" below is RangeValue, unless stated
otherwise (battle, code 4, differs).

### 7.2 Changing a value

**Change32(v, a1, a2, relative, min, max)** for 32-bit values: r = RangeValue(a1, a2); v = relative ? v + r
: r; then if v < min: v = min, else if v > max: v = max.

**Change16(v, a1, a2, relative, min, max)** for 16-bit fields: a1, a2, min and max are first truncated to
signed 16-bit; r = RangeValue(a1', a2') truncated to 16 bits; v = (relative ? v + r : r) truncated to
signed 16 bits (wrapping); then clamped as above with the 16-bit min/max.

### 7.3 Target selection

Commands 11, 16, 17, 19 encode the target in a3: **relative = a3 bit 0**, **t = a3 >> 1** (unsigned shift).
Commands 22 and 24 use **t = a1**. In both cases:

* t = 0: every party member k = 0 .. party size − 1, in party order;
* t ≥ 1: the battler record whose actor id is t − 1, found by searching records 0..7 in order. Because
  every actor always has one of the 8 records (members first, then the non-members, `00-README.md` §5),
  the record is found **also when the actor is not in the party** **(quirk)**: the command then changes the
  data of an actor who is not a member, and the change remains when that actor joins later. (Only with corrupt
  actor ids would no record match; the original would then write past the records — an implementation
  must then do nothing.)

For "every party member" commands, members are processed in party order; random draws happen per member
in that order.

### 7.4 Battler record fields used

Battler record fields (layout in `02-data-formats.md`, Battler record; offsets in the record): level
(s16, +0x00), HP (+0x02), max HP (+0x06), MP (+0x0A), max MP (+0x0C), vitality = defence base (+0x0E),
power = attack base (+0x10), agility (+0x12), intelligence (+0x14), luck (+0x16), defence incl. armour
(+0x18), attack incl. weapon (+0x1A), experience (s32, +0x1E), status bits (u16, +0x28), name (+0x2C),
actor id (s8, +0xAF). Status bits: 0x01 poison, 0x02 paralysis; the others are battle-only (`08-battle.md`).

---------------------------------------------------------------------------------------------------------

## 8. Commands

"Object" means the running event's object (absent for events 0 and 99999, after a teleport, after a
boarding resumption). "Close the message window" means: destroy the slot-0 window if it exists and update
the screen. "Close all windows" means: destroy all 13 window slots and cancel a pending field-menu request
(`06-windows-text.md`).

### 8.1 Summary table

| code | command | a1 | a2 | a3 | a4 | suspends | page refresh |
|---|---|---|---|---|---|---|---|
| 0 | End | — | — | — | — | | |
| 1 | Show message | str | flags | — | — | | |
| 2 | Show choice | str | count | labels | depth<<16 | | |
| 3 | End of choice | — | — | — | depth<<16 | | |
| 4 | Battle | troop min | troop max | no escape | — | | |
| 5 | Teleport | map | x | y | — | | |
| 6 | Play music | str | volume/pan | type/freq | — | | |
| 7, 8, 9 | (none) | | | | | | |
| 10 | Change money | min | max | relative | — | | yes |
| 11 | Change experience | min | max | target<<1 \| rel | — | | |
| 12 | Choice option | str | position | — | label \| (depth+1)<<16 | | |
| 13 | Wait | tenths | — | — | — | | |
| 14 | Play movie | str | flags | — | — | | |
| 15 | Change switch | switch | value | — | — | | if changed |
| 16 | Change HP | min | max | target<<1 \| rel | — | | |
| 17 | Change MP | min | max | target<<1 \| rel | — | | |
| 18 | Learn magic | actor+1 / 0 | spell | — | — | | |
| 19 | Change level | min | max | target<<1 \| rel | — | | |
| 20 | Change item count | min | max | item<<1 \| rel | — | | yes |
| 21 | Play sound effect | str | volume/pan | type/stop/freq | — | | |
| 22 | Change condition | actor+1 / 0 | operation | — | — | | |
| 23 | Change party | actor mask | mode | — | — | | if applied |
| 24 | Recover | actor+1 / 0 | amount | — | — | | |
| 25 | (none) | | | | | | |
| 26 | Show picture | str | flags | — | — | | |
| 27 | Inn | str | price | flags | — | | |
| 28 | Shop | str (items) | str (texts) | buy only | — | | if traded |
| 29 | Screen tone | mode | colour bytes | — | — | | |
| 30 | End game | — | — | — | — | | |
| 31 | Erase screen | transition | — | — | — | | |
| 32 | Play vehicle music | — | — | — | — | | |
| 33 | Hero move route | route | — | — | — | yes | |
| 34 | Set escape destination | map | — | — | — | | |
| 35 | Event move route | route | — | — | — | yes | |
| 36 | Disable player control | — | — | — | — | | |
| 37 | Disable player control (variant) | — | — | — | — | | |
| 38 | Stop all events | — | — | — | — | | |
| 39 | Restore movement | — | — | — | — | | |
| 40 | Change hero graphic | (unused) | graphic | — | — | | |
| 41 | Change hero speed | min | max | relative | — | | |
| 42 | Change event graphic | (unused) | graphic | — | — | | |
| 43 | Change event speed | min | max | relative | — | | |
| 44 | Change event move type | type | — | — | — | | |
| 45 | Add/remove teleport destination | map | add | — | — | | |
| 46 | Allow/forbid teleport | flag | — | — | — | | |
| 47 | Board vehicle | — | — | — | — | yes | |
| 48 | Leave vehicle | — | — | — | — | yes | |
| 49 | Set audio slot | str | volume/pan | type/slot/freq | — | | |
| 50 | Set transition | index | type | — | — | | |
| 51 | Reset hero graphic | — | — | — | — | | |
| 52 | Save menu / enable / disable | op | — | — | — | | |
| 53 | Change hero vehicle type | type | — | — | — | | |
| 54 | Remove vehicles | map | — | — | — | | |

Codes 7, 8, 9 and 25 do nothing (code 25 occurs 36 times in the sample games, always with the same
arguments). Any code > 54 or negative does nothing. Code 0 is not executed; it ends the list. Codes 34 and
37 never occur in the sample games. There are **no conditional-branch, loop, jump or label commands**: the
only branching is the choice (codes 2, 12, 3); all other conditions exist only as page conditions (§4).

### 8.2 Commands in detail

**0 End.** Ends the command list (§6.3).

**1 Show message {1, str, flags}.** a2 bit 1 = bottom position, a2 bit 0 = wait for the confirm key.
(Sample data: 0x1, 0x3, 0x0, 0x2.)

1. y = 360 if a2 bit 1 is set, else 32.
2. If a message window (slot 0) exists whose top (in screen coordinates) differs from y, close the message
   window.
3. If no message window exists, create it: slot 0 at x = (640 − 456) / 2 = 92, y, width 456, height 92
   (`06-windows-text.md`, Message window, for its look and the text area).
4. Expand the text codes of str[a1] (§8.3). If the expansion fails (too long), use the unexpanded text.
   Set it as the window's text (replacing the previous text; the window shows it at once) and process
   pending window messages once.
5. Wait:
   * if the text contained a `$W` code: Wait(w) with its value w (0..99; 0 = until confirmed), **even if
     the next command is a choice**;
   * otherwise, if the command physically following this one in the list has code 2 (show choice): no
     wait, continue immediately (the choice appears together with the message);
   * otherwise: Wait(0) (until confirmed) if a2 bit 0 is set, else Wait(message speed).

The window stays open after the command; the next message reuses it (at the same position). It is closed
by: a message with the other position; codes 4, 5 (all windows), 13, 14, 26, 29, 31, 33, 35 (see each);
codes 40, 42, 51, 47, 48 (all windows, see each); every teleport; the end of the run (§6.4). A choice does
not close it.

**2 Show choice {2, str, count, labels, depth<<16}.** count = a2 (1..4); labels: the label of option i
(i = 0..count−1) is byte (3 − i) of a3, i.e. option 0 = bits 24..31, option 1 = bits 16..23, option 2 =
bits 8..15, option 3 = bits 0..7. a4 is not used by the command.

1. Clear the pressed-state of the arrow keys, Space and Return and wait until Escape is not held (no
   wait for release).
2. str[a1] is a comma-separated list. For i = 0 .. count−1: option i = the i-th field (fields separated by
   `,`; spaces and tabs at the start of each field are skipped; spaces at the end are kept). The choice
   text = option 0 + `\n` + option 1 + `\n` + … (each option followed by a line break). maxLen = the largest
   option length in characters (Shift_JIS characters). If the string has fewer than count fields, a missing
   option repeats the text of the last field that was parsed and does not count for maxLen **(quirk;
   UNCERTAIN, never in the sample data)**.
3. Expand text codes (§8.3) in the joined choice text (unexpanded on failure).
4. Open the choice window in slot 6: horizontally centred, top y = 100, one row per option, text centred in
   each row, not cancellable (`06-windows-text.md` §10.2, Choice; the width follows the width rule of the
   list helper, `06-windows-text.md` §6.2; it also gives the height and the y correction when the window
   would leave the screen).
5. Wait(2); then set the selected index to −1 (a selection made during these 200 ms is discarded,
   **quirk**, `06-windows-text.md` §10.2); then repeat Wait(0) while the selected index is −1. Escape and
   the right mouse button do not cancel.
6. Close slot 6 and return the label of the selected option.

The runner then continues after the ChoiceOption (code 12) carrying that label (§6.3). A label 0 makes the
runner continue with the next command, which is the first ChoiceOption; it returns −1, so the whole choice
block is skipped **(quirk)**.

**12 Choice option {12, str, position, —, label | (depth+1)<<16}.** a1 = the option's text (not used),
a2 = option position 0..3. When reached by normal flow (the end of the previous branch), it returns −1:
the runner jumps to the command after the matching EndChoice (the next code 3 whose depth equals this
option's depth − 1).

**3 End of choice {3, —, —, —, depth<<16}.** No effect; it is the target of the skip.

Block structure written by the editor (labels are unique within a page, numbered across all choices of the
page; nested choices have depth + 1):

| command | a3 / a4 | meaning |
|---|---|---|
| 2 ShowChoice | a3 = 0x02010000, a4 = 0x00000 | 2 options, option 0 → label 2, option 1 → label 1; depth 0 |
| 12 Option | a2 = 0, a4 = 0x10002 | label 2, depth 1 |
| … | | branch of option 0 (may contain a nested choice with depth 1, its options depth 2, its EndChoice depth 1) |
| 12 Option | a2 = 1, a4 = 0x10001 | label 1 |
| … | | branch of option 1 |
| 3 EndChoice | a4 = 0x00000 | depth 0 |

**4 Battle {4, troop min, troop max, no escape}.**

1. Set the "escape forbidden" flag of the battle = a3 (non-zero: the escape command and escape items are
   not allowed; `08-battle.md`).
2. If a2 = 0, a2 := a1 (the command is changed in place for the rest of this run).
3. If a1 = a2: troop = a1 − 1, no random draw. Otherwise n = a2 − a1 + 1; if n = 0 (a2 = a1 − 1) the troop is
   a1 − 1 **without** a draw (`01-runtime.md` §6 rule); else **one** draw R and troop = (R mod n) + a1 − 1, mod
   being the C remainder, so for n < 0 (a2 < a1 − 1) it is R mod |n| **(quirk: a draw is made although
   max < min)**. (The sample games never have a2 < a1 with a2 ≠ 0.)
4. Start the battle with that troop (troops numbered from 0; `08-battle.md` §1, Starting a battle). If the troop
   is empty (monster count 0), nothing happens (`08-battle.md` §1.2). The original does not check the troop
   number against 0..249 (UNCERTAIN; an implementation should treat a number outside that range as an
   empty troop, `08-battle.md` §1.1). The battle closes the message window,
   blocks until it ends and restores the map afterwards (`08-battle.md`).
5. If the party lost, the battle-lost flag is set: the runner stops after this command and the game-over
   event runs at the end of the main-loop pass (§6.7).

**5 Teleport {5, map, x, y}.** Set the post-pass action to "teleported" (no further effect), teleport the
party to map a1, position (a2, a3) (`04-map-movement.md` §11.1, Teleport: invalid targets do nothing; a
valid one closes all windows, erases the screen with the map-change transition (transitions table index 0),
moves the party and the ridden vehicle, recreates events if the map differs, and — since the previous map is
not −1 — shows the new map with the map-change transition (transitions table index 0) before returning),
then set the pass-abort flag (also when the target was invalid). The event **continues** on the new map, without its object
(§6.3 step 5), even if the target was invalid or the same map. A different map also clears the hero-wait
record. The map music is not changed.

**6 Play music {6, str, a2, a3}.** Play the music named str[a1] with the parameter words a2 (volume and
pan) and a3 (device type and frequency) (`09-audio-movies.md` §3.1, Playing a BGM). If the name equals the
currently playing music nothing restarts, only the parameters are applied. The name `演奏停止` stops the
music. No wait.

**10 Change money {10, min, max, relative}.** Change32(money, a1, a2, a3 ≠ 0, 0, 100000000). Set the page
refresh request flag (always).

**11 Change experience {11, min, max, target<<1 | rel}.** For each target (§7.3): Change32(experience, a1,
a2, rel, 0, 100000), one RangeValue per member. **The level is not recomputed** (no level-up, no message, no
stat change): this command does not use the experience/level-up routine of battles (`08-battle.md` §6.2). A
member whose experience now lies in a higher level's range keeps the old level; the level catches up only
at a later battle victory, when that routine compares the experience with the level (`08-battle.md` §6.2).
Lowering the experience never lowers the level.

**13 Wait {13, n}.** Close the message window, then Wait(a1): a1 tenths of a second, ended early by the
confirm key; a1 = 0 waits for the confirm key. (Sample values: 1..9999, mostly 5 and 10; a few hundred are
above 30.)

**14 Play movie {14, str, flags}.** If the file str[a1] (looked up in the game folder by its name only,
`09-audio-movies.md`) does not exist, nothing happens. Otherwise close the message window, remember and
stop the music, stop all sound effects; if a2 bit 2 is set, fade out (if the screen state is MAP), clear
the screen to black and set the screen state MOVIE; play the movie (blocking; confirm or Escape stops it;
a2 bit 0 and bits 4..7 give the size and position); afterwards restore the screen (and, with bit 2, the
previous screen state) and the music. Details: `09-audio-movies.md`, Movies.

**15 Change switch {15, switch, value}.** a2 = 1 means ON, anything else OFF. Let cur = the masked reading
of switch a1: 2^(a1 mod 32) if ON, 0 if OFF. If cur ≠ a2: set switch a1 to (a2 = 1), and set the page
refresh request flag. Consequence **(quirk)**: turning ON a switch that is already ON counts as a change
(an extra page refresh, no other effect) unless a1 is a multiple of 32; turning OFF a switch that is
already OFF is not a change. (Sample data use a2 ∈ {0, 1}.)

**16 Change HP {16, min, max, target<<1 | rel}.** For each target: Change16(HP, a1, a2, rel, 0, max HP);
then if HP < 1: status = 0 (this also clears the status of a member already at 0 HP).

**17 Change MP {17, min, max, target<<1 | rel}.** For each target: Change16(MP, a1, a2, rel, 0, max MP).
No status change.

**18 Learn magic {18, actor, spell}.** The learn table holds, per actor id and spell 0..99, the level at
which the spell becomes known (known when 1 ≤ value ≤ the actor's level; initial values from the actor
database; `07-menus-shop-inn.md`). If a1 = 0: for every party member, learn-table[member's actor id][a2] =
1. Else learn-table[a1 − 1][a2] = 1 (the actor need not be in the party). A value of 1 means "known from
level 1", so the spell is known at once. Nothing is ever unlearned, no message. (For what happens to these
entries when a save is loaded see `10-save-files.md`.)

**19 Change level {19, min, max, target<<1 | rel}.** For each target member, in order, with A = the
member's actor record of the game file (`02-data-formats.md`, Actor record):

1. L = RangeValue(a1, a2); if rel: L = L + level. Clamp L to 1..100.
2. If L > level: experience = (L − 1) × (L − 1) × 10. (Lowering the level keeps the experience.)
3. Remember the old power, vitality and intelligence. Set level = L, and set from the actor record's
   per-level tables: power = A[0x44 + (L − 1)], vitality = A[0xA8 + (L − 1)], intelligence = A[0x10C +
   (L − 1)], agility = A[0x170 + (L − 1)], luck = A[0x1D4 + (L − 1)] (unsigned bytes).
4. If power changed: attack = power + power value of the item in the actor's equipment slot 0 (weapon; if
   not empty), capped at 9999.
5. If intelligence changed: max MP = (R mod 5) + (intelligence − 1) × 10 (one draw R).
6. If vitality changed: max HP = (R mod 5) + vitality × 10 (one draw R); then defence = vitality + the power
   values of the items in equipment slots 1, 2 and 3 (those not empty), capped at 9999.
7. If HP > max HP: HP = max HP. If MP > max MP: MP = max MP.

Random draws per member in this order: level range, max MP, max HP. No message is shown. (Item power is
the signed 16-bit field at offset 4 of the 28-byte item record, `02-data-formats.md`.)

**20 Change item count {20, min, max, item<<1 | rel}.** item = a3 >> 1 (0..99), relative = a3 bit 0.
Change16(count[item], a1, a2, rel, 0, maximum count per item from the game file). Set the page refresh
request flag (always).

**21 Play sound effect {21, str, a2, a3}.** If the file str[a1] exists (looked up by name only): if a3 bit 2
is set, stop all playing sound effects first; play it on a free sound-effect channel (3 channels; if none
is free, channel 1 is replaced) with the volume/pan of a2 and the frequency of a3. No wait. Details:
`09-audio-movies.md`, Sound effects.

**22 Change condition {22, target, operation}.** Target from a1 (§7.3). For each target **with HP > 0
only**: operation a2 = 0: status = 0; 1: status OR= 0x01 (poison); 2: status OR= 0x02 (paralysis); 3: HP =
0 and status = 0 (knocked out). Other operations: nothing.

**23 Change party {23, actor mask, mode}.** m = a1 AND 0xFF; cur = the party bitmask.

1. new = cur OR m for mode 0 (add); cur AND NOT m for mode 1 (remove); m for mode 2 (set). Any other mode
   behaves like mode 0.
2. If new = 0: nothing happens (the party can never become empty; no refresh).
3. New order: the current members that are in new, in their current order, followed by the added actors
   in ascending actor id.
4. The 8 battler records are rearranged: the new members' records first (new order), then the records of
   all other actors in ascending actor id. Records keep their contents.
5. If not riding: the party map characters become those of the new members (each actor has its own map
   character). Every new member's character is placed on the old leader's position, with facing and one-draw
   facing override both set to the old leader's **facing**, and the old leader's move type. The **new leader's character** then
   receives the complete movement state of the old leader's character (position, destination, motion,
   sub-step, speed, timers, move route, display, layer etc.) but keeps its own graphic. The other members'
   characters get sub-step 0 and idle motion; **(quirk)** their destination x is set to the old leader's
   y and their destination y is left unchanged (no visible effect, the destination is recomputed by the
   next step).
6. The follower trail is reset to "none" for every follower; trail entry 0 is kept if the old leader was in
   the middle of a step (sub-step ≠ 0) (`04-map-movement.md`, Followers).
7. Party size = number of members; fighting members = min(size, 4). If not riding: the number of map party
   characters = size and the current hero = the new leader. Party bitmask = new. Set the page refresh
   request flag.

There is no redraw; the change becomes visible at the next redraw. While riding only the party data
change (the vehicle stays the hero).

**24 Recover {24, target, amount}.** Target from a1 (§7.3), for every target, including members with 0 HP
(status untouched): HP = HP + ((100 − 25 × a2) × max HP) / 100 (C integer division, result truncated to
16 bits); if HP > max HP: HP = max HP. The same for MP with max MP. a2 = 0, 1, 2, 3 → 100 %, 75 %, 50 %,
25 %. There is no lower clamp **(quirk)**: a2 ≥ 5 would lower HP/MP (never in the sample data).

**26 Show picture {26, str, flags}.** If the file str[a1] does not exist (looked up by name only), nothing.
Otherwise close the message window and show the picture (`03-display.md` §7.4, Show picture, for loading, the flag
bits, placement, the transition a2 >> 16 and drawing). Screen state changes:

* a2 bit 2 clear (map palette): MAP → PICTURE_OVER_MAP; other states unchanged.
* a2 bit 2 set (picture's own palette): from MAP or PICTURE_OVER_MAP the screen is first faded to black,
  which sets the state to ERASED; then any state > 0 (MAP and PICTURE_OVER_MAP via ERASED, ERASED, MOVIE,
  PICTURE_ON_BLACK) becomes PICTURE_ON_BLACK; BEFORE_START and PICTURE_BEFORE_MAP are not changed by this
  step. (This agrees with the table of `03-display.md` §7.3.)
* afterwards, if the state is BEFORE_START it becomes PICTURE_BEFORE_MAP.
* a2 bit 3 set: Wait(0) after showing.

The picture stays until code 31, until something redraws the map, or until the event ends (§6.4 step 4).

**27 Inn {27, str, price, flags}.** str[a1] holds 3 lines each ending with `\n` (welcome, after staying,
declined); a2 = price; a3 bits: 0x4000 restore HP to max, 0x2000 restore MP to max, 0x0002 clear status
bits 0x02 and 0x20, 0x0001 clear status bits 0x01 and 0x20. The inn dialogue, prices, messages, fades and
music: `07-menus-shop-inn.md`, Inn. (Sample a3 values: 0x6003, 0x6002, 0x6000.)

**28 Shop {28, item list, texts, buy only}.** str[a1] = a string of 100 characters, character i = `X`
means item i is sold; str[a2] = 3 lines each ending with `\n` (welcome, farewell after a trade, farewell
without trade); a3 = 1: buy only (2 commands), otherwise buy/sell/exit. Sets the page refresh request flag
when at least one item was bought or sold. Details: `07-menus-shop-inn.md`, Shop.

**29 Screen tone {29, mode, colour}.** a2 bytes: byte 0 = red %, byte 1 = green %, byte 2 = blue %, byte
3 = grey (0 colour, non-zero grey scale); 100 = unchanged, range 0..200 (sample: 0x646464 normal, 0xC8C8C8
white). First run one message-processing pass, then close the message window (closing updates the screen,
which runs another pass); this order is the original's. Then, if the screen state is ERASED apply the
tone instantly (mode 4), else apply it with mode a1 (flash, shake, instant or fade; sample a1 values 0..7).
With the instant and fade modes (4, 5, 7) the tone persists as the current tint (it is applied to later
palettes and saved); flash and shake modes do not change the tint. Details: `03-display.md` §4, Tint and
palette effects.

**30 End game {30}.** Stop the music, fade the screen to black (palette fade mode 5; screen state ERASED),
set the quit flag and the pass-abort flag. The runner stops; the main loop exits and **the program
terminates** — it does not return to the title screen (`01-runtime.md`, Quitting).

**31 Erase screen {31, transition}.** Close the message window, then by screen state, with t = a1:

| state | effect | new state |
|---|---|---|
| BEFORE_START | nothing | BEFORE_START |
| PICTURE_BEFORE_MAP | hide the screen with transition t (to black) | PICTURE_BEFORE_MAP |
| PICTURE_OVER_MAP | redraw the map into the back buffer without presenting it, then reveal it with transition t (if t = 13 followed by the show pattern 13) | MAP |
| PICTURE_ON_BLACK | hide with t, reload the map palette, then show the map (map-change transition, transitions table index 0) | MAP (via ERASED) |
| MAP, ERASED, MOVIE | hide with transition t | ERASED |

So "erase screen" removes a picture over the map without erasing the map; in the other states the screen
becomes black until the map is shown again (normally at the end of the event, §6.4). Transition details:
`03-display.md`, Screen transitions. (Sample a1 values 0..14.)

**32 Play vehicle music {32}.** Play the music of the audio slot whose index is the current hero's vehicle
type (0 walking … 6 airship), if that slot has a name, with the slot's parameters (as code 6).

**33 Hero move route {33, route}** and **35 Event move route {35, route}.** Close the message window. The
target is the current hero (33) or the object (35; if there is no object, nothing more happens and the
command does not suspend). The target's route is replaced by a copy of route[a1] (its length byte is not
part of the route): route mode "running", position 0, step direction +1. The current motion is not
interrupted. After the command, the runner applies the route's leading display bytes (§9) and, for 33,
copies the hero's display mode to all 8 party map characters. Suspension: §6.5. Route execution:
`04-map-movement.md`, Move routes.

**34 Set escape destination {34, map}.** The escape destination (signed 16-bit) = a1. It is used by
escape-type items and magic (`07-menus-shop-inn.md`, `08-battle.md`); a value ≤ 0 means "none". Never used
in the sample games, but see code 54.

**36..39 Movement control.** The movement control mode is saved in save files.

| code | mode | effect |
|---|---|---|
| 36 | 2 | player control off: no walking (keyboard, joystick, mouse) and no action button; events move normally; the hero's own move route still runs |
| 37 | 3 (only if there is an object; otherwise nothing) | same effect as 2 |
| 38 | 1 | all map events and vehicles stop: their updates are skipped (no route, no movement AI, no motion) except for objects waiting for their own route (resume index ≠ 0); their animation is frozen in drawing; the trigger cooldown still counts down; the player can still walk and act |
| 39 | 0 | normal |

Mode 1 is also set during boarding and alighting (codes 47, 48) and reset to 0 at their finalisation.

**40 Change hero graphic {40, —, graphic}.** g = a2. For each party map character (the members' characters
when walking, only the vehicle while riding): graphic = g AND 0x3F if g < 0x100 (direction bits ignored),
else g. If the screen state is MAP: close all windows, redraw the map frame and present it. a1 is editor
data and unused.

**41 Change hero speed {41, min, max, relative}.** v = RangeValue(a1, a2). If a3 bit 0 is clear: the current
hero's speed = v. If set: **(quirk)** the *object's* speed (not the hero's) is increased by v, unclamped;
with no object the original fails (crash) — specified: increase the hero's speed instead (never used in
the sample data). Then clamp the current hero's speed to 1..20 and copy it to all 8 party map characters.

**42 Change event graphic {42, —, graphic}.** If there is an object: graphic = a2 AND 0x3F if a2 < 0x100,
else a2; if the screen state is MAP: close all windows, redraw and present. Without an object nothing.

**43 Change event speed {43, min, max, relative}.** If there is an object: v = RangeValue(a1, a2); speed =
v, or speed + v if a3 bit 0 is set; clamp to 1..20. Without an object nothing (no random draw).

**44 Change event move type {44, type}.** If there is an object: move type = a1 (0 fixed, 1 approach,
2 flee, 3 random; not validated). It stays until a page change re-applies a page (pages are re-applied only
when the page index changes, §4.1).

**45 Teleport destination list {45, map, add}.** The list holds up to 100 area-map ids (signed 16-bit),
terminated by the first value ≤ 0 (`07-menus-shop-inn.md`, teleport magic).

* a2 = 0 (remove): find the first entry equal to a1 among the 100; if found, set it to 0 and move every
  following positive entry one place down (stopping at the first entry ≤ 0).
* a2 ≠ 0 (add): only if map a1 is an area map (the type field of its map record is 2, `02-data-formats.md`;
  no separate existence test, e.g. of the width, is made, and a1 is not range-checked;
  values outside 0..499 never occur, an implementation should ignore them): if a1 is already among the
  entries before the first value ≤ 0, nothing; else, if fewer than 100 entries are used, store a1 at the first
  free place.

**46 Allow/forbid teleport {46, flag}.** The teleport-forbidden value (signed 16-bit) = a1. Teleport magic
is refused when it is 1 (or while riding) (`07-menus-shop-inn.md`).

**47 Board vehicle {47}.** Only if there is an object and the party is not riding:

1. Close all windows.
2. Vehicle table (move-to-front): search entries 0..9 up to the first entry with event id 0 for the
   object's event id. If found at entry k, entries 0..k−1 move to 1..k. If not found, let k = the index of
   the first entry with id 0 (10 if there is none); if k = 10: delete the vehicle in entry 9 and move entries
   0..8 to 1..9; else move entries 0..k−1 to 1..k; and the object is removed from the map's event list (it
   becomes a persistent vehicle). Entry 0 = {id 0, map =
   current map, the object}.
3. Hero move type = 6 (boarding); the hero starts a "wait one step" motion; hero display = 0x1A
   (invisible); movement control mode = 1.

The runner suspends (§6.5, record with object none). The followers walk into the vehicle and the
boarding finalisation makes the vehicle the hero (`04-map-movement.md`, Vehicles, Boarding), plays the
vehicle music and then resumes the event without object. Any event object can be boarded; its page's
vehicle type decides how it moves.

**48 Leave vehicle {48}.** Only if riding: close all windows and try to alight (`04-map-movement.md`,
Vehicles, Alighting). If alighting is not possible: set the hero-wait record's id to −1 (its saved object,
page and index are kept, §6.5) and clear the suspend flag — the event continues with the next command. If it starts: the event is suspended; the alighting
finalisation re-applies the vehicle's page attributes (§4.2), restores the vehicle's event id, plays the
music of the leader's vehicle type, runs the step-on check (§5.4) and then resumes the event (with the
object recorded at suspension).

**49 Set audio slot {49, str, a2, a3}.** slot s = (a3 >> 8) AND 0xFF (0..11: 0 walking, 1 wagon, 2 sled,
3 boat, 4 ship, 5 balloon, 6 airship, 7 encounter, 8 battle, 9 victory, 10 level up, 11 inn). The slot's
name = a copy of str[a1], its parameter words = a2 and a3. Nothing is played. Values of s ≥ 12 must be
ignored (the original overwrites unrelated data).

**50 Set transition {50, index, type}.** transitions table[a1] = a2 (index 0 map change, 1 battle start and
map return, 2 battle screen; type 0..14, `03-display.md`). Indices outside 0..2 must be ignored. (Sample:
index 0 mostly.)

**51 Reset hero graphic {51}.** Not riding: each party member's map character gets the default graphic of
its actor (the 32-bit value at offset 0x00 of the actor record). Riding: the vehicle's graphic = the
graphic of its current page (AND 0x3F if < 0x100; facing unchanged). If the screen state is MAP: close all
windows, redraw and present.

**52 Save {52, op}.** a1 = 11: saving disabled (the field menu's save entry refuses); a1 = 12: saving
enabled; any other value: open the save screen now (blocking; `10-save-files.md`, `07-menus-shop-inn.md`).
The initial "saving enabled" value comes from the game file flags (bit 17, `02-data-formats.md`).
(Sample a1 values: 0, 11, 12.)

**53 Change hero vehicle type {53, type}.** The vehicle type of all 8 party map characters (the walking
characters, not a ridden vehicle) = a1. This changes their passability class, encounter behaviour and the
music chosen by code 32 (`04-map-movement.md`). Sample values: 0, 3, 4, 5, 6.

**54 Remove vehicles {54, map}.** For entries (riding ? 1 : 0) .. 9 of the vehicle table: if occupied,
delete the vehicle object and clear the entry (id 0, map 0, no object). The ridden vehicle (entry 0 while
riding) is kept. Then **(quirk)** the escape destination is set to a1, exactly as code 34. (Sample a1
values 0 and 4 — so this command can clear or change the escape destination.)

### 8.3 Text codes

Used for messages (code 1), choices (code 2), and the inn and shop texts (`07-menus-shop-inn.md`). The
expansion works byte by byte (it is not Shift_JIS-aware; this is safe because the byte `$` (0x24) cannot be
a Shift_JIS trail byte). Every expansion first resets the "message display time" to "none". A `$` followed by
the character x:

| code | output |
|---|---|
| `$$` | `$` |
| `$0` | name of the first party member (battler record 0 name, the current leader) |
| `$1` .. `$8` | name of actor 0 .. 7 from the actor database of the game file (actor record offset 0x08), whether or not the actor is in the party |
| `$G`, `$g` | money as a decimal number (no separators, `%d`) |
| `$U`, `$u` | the currency name of the game file |
| `$R`, `$r` | a line break `\n` |
| `$W`, `$w` | the next **2 bytes** are taken (whatever they are) and converted to a number like the C string-to-integer conversion (leading spaces skipped, optional sign, then digits up to the first non-digit); the result clamped to 0..99 becomes the message display time in tenths (`$W00` or non-digits = wait for confirm). Nothing is output. |
| `$S` + digit d | only if the party has more than 1 member and 1 ≤ d ≤ 4: d = 1 `たち`, 2 `がた`, 3 `ども`, 4 `ら`; otherwise nothing. The digit is consumed in every case. |
| `$s` + digit d | same with d = 1 `達`, 2 `方`, 3 `共`, 4 `等` |
| `$N` | the party size as a full-width digit `１` .. `８` |
| `$n` | the party size as a kanji numeral `一` `二` `三` `四` `五` `六` `七` `八` |
| any other `$x` | nothing; both bytes are dropped |
| `$` at the very end (or `$W`/`$w`/`$S`/`$s` with fewer bytes left than they consume) | the original always skips the bytes the code consumes (2 after `$`, 3 for `$S`/`$s`; `$W` takes the 2 bytes after the letter whatever they are) and so reads past the end of the text (undefined); an implementation should stop at the end of the text **(UNCERTAIN, never in the sample data)** |

Shift_JIS bytes of the inserted texts: たち 82 BD 82 BF, がた 82 AA 82 BD, ども 82 C7 82 E0, ら 82 E7,
達 92 42, 方 95 FB, 共 8B A4, 等 93 99; １..８ = 82 50 .. 82 57; 一 88 EA, 二 93 F1, 三 8E 4F, 四 8E 6C,
五 8C DC, 六 98 5A, 七 8E B5, 八 94 AA.

Length check: before inserting a name (`$0`..`$8`), money, currency, line break or plural suffix, if
(length of the whole input text in bytes + length of the inserted text − 2) > 1024, the expansion fails
and the caller uses the unexpanded text (with the `$` codes visible). `$N`/`$n` are not checked. The
message display time set by `$W` is used by code 1 only (§8.2); a later expansion (e.g. of a choice)
resets it.

Line breaks in event strings are CR LF; inn and shop strings separate their lines with bare LF (`\n`).
How the window breaks and wraps lines: `06-windows-text.md`.

Usage in the sample games (for prioritising tests): `$1` 3563, `$2` 2969, `$6` 2034, `$3` 1963, `$4` 1496,
`$0` 973, `$5` 935, `$W` 521, `$w` 272, `$U` 46, `$G` 23, `$R` 7, `$8` 5, `$S` 1.

---------------------------------------------------------------------------------------------------------

## 9. Move routes used by events

Route bytes and their execution are specified in `04-map-movement.md`, Move routes (byte table: steps,
turns, relative steps/turns, wait, animation types 0x12..0x18, display modes 0x19..0x1B, end/repeat/
reverse/ping-pong 0x1C..0x1F). Events use routes in three ways:

1. **Page routes.** Installed by applying a page (§4.2). They run once (unless they contain repeat or
   ping-pong bytes); afterwards the page's move type takes over. A route on the page of an object waiting for
   its own route is not installed, because waiting objects are not re-evaluated.
2. **Command routes** from codes 33 (hero) and 35 (the event's object), installed as described in §8.2,
   replacing any route the target had (including a page route), without interrupting the current motion.
3. **Completion.** A route ends only when the route-step procedure reaches its end (`04-map-movement.md`);
   at that moment a suspended event waiting for that route is resumed (§6.5).

**Leading display bytes.** Right after a code 33 or 35 command (for 35 only with an object), while the
target has a route and the byte at the route position is 0x19, 0x1A or 0x1B: set the target's display mode
to that byte and advance the route position by 1. So a route that starts with "invisible" or "removed"
takes effect before the runner continues (and before the map is next drawn). For 33 the hero's display
mode is then copied to all 8 party map characters.

The hero's route is executed on every main-loop pass while the hero is idle (not once per tick); after each
route step the hero's display mode and animation type are copied to all 8 party map characters (unless
boarding or alighting), and when the route ends while the screen state is MAP all windows are closed and the
map is redrawn (`04-map-movement.md`).

The event's own route is executed by its per-event update (once per tick, at its speed); route bytes 8, 9,
12 and 13 refer to the current hero's facing.

---------------------------------------------------------------------------------------------------------

## Quirks in this chapter

* §3.1 — an event whose file cannot be read is erased from the map tile for the rest of the session.
* §3.3 — vehicle updates and autorun checks stop at the first vehicle entry that is on another map.
* §4.1 — pages 1..3 without condition flags are never chosen; page 0 never checks its conditions.
* §4.1 — if the selected page is the current page, nothing (graphic, route, cooldown) is re-applied.
* §4.3 — items equipped by party members count for item conditions (not for event 99999's first page choice).
* §4.3 — page changes from commands take effect only after the event, but item use-up and battle drops refresh immediately.
* §5.2 — the action button ignores the trigger type; autorun pages can be restarted with it.
* §5.3/§5.4 — bumping needs cooldown exactly 0, stepping on ignores the cooldown.
* §5.7 — an autorun event runs once per page activation; it starts in the second tick after map arrival.
* §5.7 — a teleporting event does not get its cooldown set; a same-map teleport makes an autorun event run again.
* §6.3 — after a teleport (successful or not) the running event has no object; codes 35/37/42/43/44 then do nothing, and 47 does nothing but still suspends.
* §2 — a failed run read leaves the strings already read stored (see `02-data-formats.md` §4.4).
* §3.3/§4.3 — vehicle pages are re-evaluated only while the current map's event list is not empty.
* §4.3/§6.5 — after a failed code 48 the hero-wait record keeps its object, which is then excluded from page re-evaluation.
* §5.5 — an autorun event on a map row not visited by the draw keeps its stale on-screen flag and can autorun far off screen.
* §5.5 — a pass-abort flag set by an autorun vehicle only ends the vehicle loop; step completion still runs.
* §5.2 — the mouse equivalents of the action button skip the movement-control, route, arrival-flag and latch tests.
* §8.2 code 2 — a selection made during the first 200 ms of a choice is discarded.
* §6.4 — the end of every event shows the map, removing pictures and an erased screen (unless erased with a route pending).
* §6.5 — 33 directly followed by 35 (or vice versa) suspends only at the second command.
* §6.5 — a 47 that does nothing still suspends the event.
* §6.5 — a second hero-waiting event overwrites the first one's resumption; a re-triggered waiting event restarts from the top.
* §6.5 — resumption ignores the event's current page conditions.
* §6.6 — the start event always runs page 0; the walking music is started after it.
* §6.7 — the game-over event runs again every loop pass while the party stays defeated.
* §8.2 code 1 — `$W` overrides the "no wait before a choice" rule.
* §8.2 code 2 — a choice cannot be cancelled; label 0 skips the whole choice block.
* §8.2 code 4 — the troop range draws a random number even when max < min (except max = min − 1, which draws nothing).
* §8.2 code 11 — changing experience never changes the level (no level-up, no message).
* §8.2 code 15 — setting an already-ON switch counts as a change unless its number is a multiple of 32.
* §8.2 code 16 — HP < 1 after the change clears the status, also for members already at 0 HP.
* §8.2 code 19 — lowering the level keeps the experience; max HP/MP are re-rolled only if vitality/intelligence changed.
* §8.2 code 23 — followers' destination x is set to the old leader's y.
* §8.2 code 24 — no lower clamp on recovery.
* §8.2 code 41 — relative hero speed change modifies the event's speed instead.
* §8.2 code 54 — removing vehicles also sets the escape destination to a1.
* §7.3 — a single-actor target need not be in the party; non-members' records are changed too.
* §6.5 — a 48 executed while not riding still suspends the event.

## Open questions

* §7.3: that non-member actors are affected by single-target commands follows from the record search;
  it has not been verified by observation.
* §4.3: the equipment bonus is computed for party members only (equipment of actors outside the party
  does not count); this has not been confirmed by observation.
* §8.2 code 2: the exact behaviour for a choice string with fewer fields than options (never in the data).
* §6.5: whether a non-executed code 47 (suspending without boarding) occurs in any game and how it then
  resumes.
* §5.5: the autorun of an event that scrolled out vertically (stale on-screen flag) follows from the draw
  and autorun rules but has not been observed in a game.
* Item numbers ≥ 100 in page conditions, switch numbers ≥ 1024, route numbers beyond the stored command
  routes and missing string indices never occur in the sample data; the behaviour specified for them
  (condition false, nothing, empty string) is a safe choice, not the original's (undefined) behaviour.
