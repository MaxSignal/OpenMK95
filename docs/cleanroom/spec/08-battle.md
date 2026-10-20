# 08 — Battles

This chapter describes the complete battle system: how a battle starts, the battle screen and its
windows, command input, turn execution, all formulas (physical attacks, damage, the 22 spell types,
items), enemy behaviour, battle animations, victory with experience and level up, escape, defeat and
the return to the map. The order of random-number calls is given everywhere it matters; reproducing it
exactly is required so that the same inputs give the same battle as in the original.

Related chapters: data layouts of the battler record, CHARA.DAT (monster database), EPARTY.DAT
(troops), ITEM.DAT, MAGIC.DAT, MONSTPIC.DAT and the actor block of the game file are in
`02-data-formats.md`; palettes, colour matching, palette effects (tints, flashes, shakes) and screen
transitions in `03-display.md`; random encounters and teleport destinations in `04-map-movement.md`;
the battle event command and the game-over event in `05-events.md`; windows, list windows, the
pop-up notice and `Message(...)` in `06-windows-text.md`; item and magic use outside battle in
`07-menus-shop-inn.md`; music, jingles and sound effects in `09-audio-movies.md`; built-in strings and
sound-effect names in `11-appendix.md`.

---------------------------------------------------------------------------------------------------

## 0. Notation and battle state

### 0.1 Combatant numbers

A **combatant number** identifies a battler during a battle:

* 0..7 — the party battler record at that party position (see 00-README.md, "Party"). Only positions
  0..F−1 take part in the fight, where **F = min(party size, 4)** is the *fighting size*.
* 100..109 — enemy slot 0..9 (combatant number − 100).

A **target value** stored in an action uses the same numbering: t < 100 means party record t, t ≥ 100
means enemy slot t − 100. The value −1 means "no target". For a teleport spell the target value
instead carries a map id (see 4.4, type 20).

"Living" means HP > 0 (HP is the battler-record field at 0x02). "Dead" means HP ≤ 0.

### 0.2 Random numbers

`rand(n)` denotes one value from the global pseudo-random generator, reduced modulo n
(`01-runtime.md`, "Random numbers"). When n = 0 the generator is **not** called and the result is 0;
for every n ≥ 1 (including n = 1) exactly one call is made. The generator output is never negative.
Every listed rand() call happens in exactly the stated order; no other calls happen during a battle
except those of the palette shake (3 calls, 03-display.md) and of the screen transitions that use
random patterns (03-display.md §6.4; for the enemy removal dissolve see 5.7). The map redraw at the end
of a battle (section 7 step 11, and 1.9) is an ordinary character draw with advancing animation and may
take random numbers as described in `04-map-movement.md` §6.2.

### 0.3 Messages and waits

All battle texts are shown with `Message(text, f)` (06-windows-text.md): wait 1 tenth, replace the
text of the message window (slot 0), redraw it, then `Wait(max(1, trunc(messageSpeed × f)))` tenths
(for f > 0). The product is that of the message speed and the factor f as an IEEE double value,
formed exactly (80-bit extended precision, not rounded to double) and then truncated. For the factors
used in this chapter this gives: speed × f for 0.5, 1.0 and 2.0; trunc(speed / 5) for 0.2 (the double
0.2 is slightly above 0.2); and for **0.7** (the double 0.7 is slightly below 0.7): trunc(7 × speed /
10) if speed is not a multiple of 10, otherwise 7 × speed / 10 − 1 (speed 20 → 13, speed 30 → 20)
**(quirk)**. In battle the message window already exists at its battle position (1.7) and is **not**
closed after the wait. Waits end early on Return/Space and while **Shift is held during a battle**
(battle fast-forward; 01-runtime.md, "Waiting"). Durations below are given as the factor f.

Text is built by concatenation. `NAME` stands for the name field (offset 0x2c) of the battler concerned.
Numbers are written in decimal without padding unless stated. Quoted Japanese text is hard-coded in the
runtime (UTF-8 here, Shift_JIS in the runtime); strings[n] are the built-in table entries.

### 0.4 Battle state

The following state exists while a battle runs (names are descriptive):

| state | meaning |
|---|---|
| in-battle flag | set for the whole battle; many shared routines behave differently while it is set (01-runtime.md §1) |
| current troop | the troop record (EPARTY.DAT) being fought |
| enemy count | troop monster count (0..10) |
| enemy battlers | 10 battler records, slot j |
| escape forbidden flag | 1 = escaping is impossible; set from the "no escape" argument of event command 4 (Battle), 0 for random encounters |
| battle exit code | 0 while running; −1 = the party escaped; > 0 = a teleport/escape spell succeeded, the value is the destination area map id |
| battle-lost flag | set when the party was defeated; read by the main loop's game-over check (01-runtime.md §1, §10) |
| auto-battle flag | 1 while auto battle runs |
| experience total, gold total | sums of the rewards of enemies killed by damage |
| dropped item | ITEM.DAT index of the item dropped in this battle, −1 = none |
| current input member | party position whose command is being entered |
| action records | one per party position 0..7: command, target, item, spell. **They are never cleared** (they persist across battles) |
| saved stats | attack (0x1a), defence (0x18), agility (0x12) of all 8 party records, saved at battle start |
| living-enemy list | slot numbers of living enemies, sorted by the troop x position (ascending, stable: equal x keep slot order), and its count |
| party list | positions of party members, and its count (contents depend on who built it, see below) |
| battle area | the rectangle (0, 102, 640, 362): from the bottom of the message window to the top of the status window |

The party list and its count are shared: they are rebuilt (all fighting members, dead ones included)
at the start of every turn round and at battle setup, and every party or enemy action that gets past
its paralysis/sleep checks overwrites the count with the number of *living* fighting members
(3.3, 3.6). This matters for one window position (2.5, quirk).

### 0.5 Redrawing the battle scene

The battle screen consists only of the background picture and the monster pictures; there are no party
sprites and no on-screen damage numbers. The background is kept in a background buffer; effects work
on the back buffer (03-display.md) and present only the battle area. A **scene redraw** is:

1. copy the whole background buffer to the back buffer;
2. draw every *living* enemy in slot order 0..enemy count−1 at its troop position with its colour key
   (later slots over earlier ones). A monster whose rectangle at the position used in that frame
   (including the 30 px lowering and the jump offsets of 5.6) is not completely inside 640 × 480 is not
   drawn at all (`03-display.md` §1.2, §10; UNCERTAIN there, recommended) **(quirk)**. The **acting enemy** (the attacker or caster, when it is an enemy)
   is drawn 30 pixels lower than its troop position where this chapter says so — it is lowered by its
   jump (5.6) and stays lowered until its return jump;
3. present the battle area rectangle (synchronised to the display refresh).

---------------------------------------------------------------------------------------------------

## 1. Starting a battle

### 1.1 Where battles come from

* **Random encounters** (04-map-movement.md, "Random encounters"): the troop chosen by the encounter
  check is fought with the escape forbidden flag = 0.
* **Event command 4, Battle** (05-events.md): arguments from, to, no escape. The escape forbidden
  flag := the "no escape" argument. If to = 0, to := from (the command's stored "to" argument is
  overwritten with from). If from = to the troop is from − 1 and no random number is taken. Otherwise
  n := to − from + 1: if n > 0 the troop is from − 1 + rand(n); if n = 0 (to = from − 1) no random
  number is taken and the troop is from − 1; if n < 0 (to < from − 1) one random number r is taken and
  the troop is from − 1 + (r mod |n|) (the remainder of a non-negative number by a negative divisor).
  Troop numbers in the data are 1-based; record n−1 is used. The battle runs to completion before the
  event continues.

Troop numbers outside 1..250 are not checked by the original (UNCERTAIN; an implementation should
treat them as an empty troop).

### 1.2 Empty troops

If the troop's monster count (EPARTY.DAT offset 0x04) is 0 the battle does nothing at all: no music
change, no flash, no transition; the caller continues immediately.

### 1.3 Start sequence

1. Set the in-battle flag. Reset the mouse-drag movement counters of the leader (04-map-movement.md,
   mouse movement).
2. Close the message window (slot 0) if it is open.
3. Save the current BGM (name and parameters) for later restoring (09-audio-movies.md).
4. Stop the music (play the special name "演奏停止").
5. If the name of audio slot 7 (encounter) contains ".mid" (case-sensitive substring), set the MIDI
   "play once" flag.
6. Play audio slot 7 as a one-shot jingle (MIDI played once; WAV as a sound effect;
   09-audio-movies.md).
7. Palette flash mode 1 with colour (200, 200, 200), grey flag 0 — two white flashes of 20 ms each
   separated by 20 ms (03-display.md, "Palette effects").
8. Hide the screen with **transition table entry 1** (blackening), then hide with transition 0
   (instant black). Screen state := ERASED.
9. Wait for the encounter jingle to end, as described in `09-audio-movies.md` §4.2 (MIDI and WAV
   jingles, and how holding Space or Return aborts the wait).
10. Play audio slot 8 (battle) as BGM.
11. Battle exit code := 0.
12. Battle setup (1.4–1.8). If it fails, see 1.9.
13. Discard all pending keyboard input, then run the battle: the program keeps processing input (the
    battle is driven by the command windows) until the program quits, all enemies are dead, the party
    is defeated (3.8) or the battle exit code becomes non-zero. Then the battle ends (section 7).

### 1.4 Setup: state reset

At the start of setup: auto-battle flag := 0, experience total := 0, gold total := 0, dropped item :=
−1, current input member := 0. The attack, defence and agility of all 8 party records are copied to
the saved stats. The current troop := the troop record; enemy count := its count.

### 1.5 Enemy records

For each slot j < enemy count, with c = CHARA.DAT record number `id` of troop monster j (the id
equals the MONSTPIC picture number), the enemy battler record j is filled as follows (offsets of the
battler record):

| field | value |
|---|---|
| HP (0x02) | c max HP (0x06) |
| max HP (0x06) | c max HP (0x06) |
| MP (0x0a) | c max MP (0x0c) |
| max MP (0x0c) | c max MP (0x0c) |
| agility (0x12) | c agility (0x12) |
| field 0x2a | c field 0x2a (unused) |
| experience (0x1e, 32 bit) | c 0x1e |
| gold (0x22, 32 bit) | c 0x22 |
| drop item and drop probability (0x26, 0x27) | c 0x26, 0x27 |
| defence (0x18) | c 0x18 |
| attack (0x1a) | c 0x1a |
| magic rates (0x41..0xa4, 100 bytes) | copied |
| attack pattern (0xa5..0xae, 10 bytes) | copied |
| status (0x28) | 0 |
| sleep counter (0x1c) | 0 |
| name (0x2c) | copied as a zero-terminated string |

Nothing else is written: level, magic defence (0x04), vitality, power, intelligence and luck of an enemy
keep whatever the record held before (unused by the battle formulas except as noted). For slots j ≥
enemy count only HP := 0 is written.

### 1.6 Background and monster pictures

1. Background file: `bgNN.bmp` in the game folder, NN = (troop background field as an unsigned 16-bit
   value) + 1, written with at least two digits (`bg01.bmp` for background 0). If the file does not
   exist: error box (1.9) and setup fails.
2. The screen palette is set from the bitmap's colour table (03-display.md, "Palettes"); the bitmap is
   drawn into the background buffer.
3. `MONSTPIC.DAT` is opened from the game folder (missing: error box "ﾌｧｲﾙ[MONSTPIC.DAT]が見つかりません",
   setup fails). For each slot j < enemy count the picture record of monster id j is read (offsets of
   the records were indexed at program start; 02-data-formats.md). The 8-bit, bottom-up picture is
   converted with its own 256-colour table into a w × h image in the **current screen palette** (the
   background's palette) by colour matching (03-display.md, "Colour matching").
4. **Colour key**: the palette index of the **top-left pixel of the displayed picture** (= the first
   pixel of the last stored row) after colour matching. Pixels with that index are transparent; only
   the 8-bit index is compared.
5. The back buffer := background buffer; then monster j (slot order) is drawn with its colour key with
   its top-left corner at the troop position (x, y) of slot j (EPARTY offsets 0x0c + 12j and 0x10 + 12j,
   640 × 480 screen coordinates). The monster's rectangle is (x, y, x + w, y + h).
6. The current screen tone (the tint set by event commands, 03-display.md) is applied to the new
   palette at once.
7. Show the screen with **transition table entry 2**. Screen state := MAP (the internal value; the
   battle scene is visible).

### 1.7 Windows

After 1.6 the setup does, in this order:

1. `Wait(3)` (300 ms).
2. **Message window**, slot 0, outer rectangle x 92, y 10, 456 × 92 (layout of the message window in
   06-windows-text.md). Its text is set to strings[70] (default "モンスターがあらわれた！") and drawn at
   once, with no wait.
3. **Status window**, slot 1, outer x 20, y 362, 600 × 100 (frame 3 px; the cells below are in client
   coordinates, i.e. relative to (23, 365)). Its fixed texts are set, then the values (status refresh,
   below) and the names.
4. **Command window**, slot 2 (1.7.2).
5. First input member (1.8).
6. Battle area := (0, bottom of the message window = 102, 640, top of the status window = 362).

#### 1.7.1 Status window cells

| content | x, y, w × h | alignment |
|---|---|---|
| "Name" | 0, 0, 200 × 20 | centred |
| "Hp" | 200, 0, 100 × 20 | centred |
| "Mp" | 300, 0, 100 × 20 | centred |
| "Status" | 450, 0, 100 × 20 | centred |
| separator line | filled rectangle (6, 18)–(589, 19), i.e. 1 px high, x 6..588 | black (opaque windows) / white (transparent windows) |
| selection mark | 0, 20, 20 × 100 | right |
| names of the F fighting members, one per line | 0, 20, 200 × 100 | centred |
| HP, one line per member, each formatted right-aligned in 3 characters ("%3d") | 200, 20, 45 × 100 | right |
| "/" per member | 245, 20, 20 × 100 | left |
| max HP ("%3d") | 248, 20, 45 × 100 | right |
| MP ("%3d") | 300, 20, 45 × 100 | right |
| "/" per member | 345, 20, 20 × 100 | left |
| max MP ("%3d") | 348, 20, 45 × 100 | right |
| status name per member | 450, 20, 100 × 100 | centred, colour GRAY 808080 (opaque) / LTGRAY C0C0C0 (transparent) |

All cells are multi-line text cells (lines separated by line feeds, drawn as in 06-windows-text.md);
the headers are ASCII. The names are set once at setup and never updated. Header and line colours
otherwise follow the normal window text colour (06-windows-text.md).

**Selection mark**: if the current input member m is 0..3 and m < F, the text is m line feeds
followed by "*" (so the mark appears on member m's line); otherwise a single space. It is updated
whenever the current input member changes.

**Status refresh** (done at setup, after every physical attack involving the party, after every action
in a turn, and after poison damage/waking of party members): HP, "/", max HP, MP, "/", max MP are
rewritten for members 0..F−1; the status cell's colour is set again (GRAY / LTGRAY); then:

1. every fighting member with HP < 1 gets status bit 0x20 set;
2. the status text = for each member 0..F−1: strings[20 + k] + line feed, where k = position of the
   highest set bit among bits 0..7 of the status word + 1 (0 if none of these bits is set; bits 8..15
   are ignored) — so a dead member shows strings[26]
   (気絶) and a member with several bits shows the highest one;
3. every fighting member with HP < 1 gets status := 0 (all bits cleared).

#### 1.7.2 Command window

Slot 2, outer position x 10, y 280; width (maxLen + 1) × W, where maxLen is the largest character
length of strings[0], strings[1], strings[5], strings[6], strings[7] and W is the maximum character
width of the UI font; height 5 × LineH + 6 (LineH = line height of the UI font, 06-windows-text.md).
It is a
single-column selection list of 5 rows, left-aligned, cursor on row 0, with the texts:

| row | text (default) | command |
|---|---|---|
| 0 | strings[5] (戦う) | attack |
| 1 | strings[1] (魔法) | magic |
| 2 | strings[0] (アイテム) | item |
| 3 | strings[6] (逃げる) | escape |
| 4 | strings[7] (自動戦闘) | auto battle |

Row 1 is disabled (grey, not selectable) while the current input member is silenced (status bit 0x04).
There is no defend command. The command window may overlap the top-left of the status window,
depending on the font; this is how the original looks.

### 1.8 First input member

The current input member is advanced from 0 to the first fighting member that **can input**: HP > 0
and neither paralysed (0x02) nor asleep (0x10). Confused, silenced and poisoned members can input. The
selection mark is updated; if that member is silenced, row 1 of the command window is disabled. (If no
member can input, the current input member ends at F; this only happens when all living members are
paralysed, which is an immediate defeat, 3.8.)

### 1.9 Setup failure

A missing background file or MONSTPIC.DAT shows a message box with the stop icon, the text
"ﾌｧｲﾙ[" + file + "]が見つかりません" (for the background: the full path game folder + "\" + file name;
for the monster file: "MONSTPIC.DAT") and the title "RPGﾂｸｰﾙ95", and waits for OK. The battle is then
skipped: no battle-end processing (section 7 steps 1–8) is done; the map is redrawn and shown with
transition table entry 1 (unless the pass-abort flag is set, as in section 7 step 11), screen state :=
MAP, the in-battle flag is cleared, the leader's mouse-drag counters are reset and the leader's move
type is set to 5 (player control). **(quirk)** No music is stopped, no sound is unloaded and no BGM is
resumed: the battle BGM started in 1.3 keeps playing on the map and the saved map BGM is not restored
(the saved record stays present; `09-audio-movies.md` §3.6). The map graphics are not reloaded either
(section 7 step 7 is skipped); if the background file existed (only MONSTPIC.DAT was missing), the
background's untinted palette loaded in 1.6 stays on screen until the next palette realisation
(`03-display.md` §10 step 9).

---------------------------------------------------------------------------------------------------

## 2. Command input

### 2.1 Order of input, Next and Back

The current input member walks over the fighting members 0..F−1, skipping members who cannot input
(1.8). Members who cannot input keep whatever their action record contains (**(quirk)**, see 3.3).

**Next** (after a command and, where needed, its target was chosen):

1. Close the windows in slots 7, 11, 5, 4 and 6 (those that are open); re-activate the command window
   if it exists.
2. Current input member += 1, then skip forward over members who cannot input.
3. If no member is left (current input member reaches F): close the command window, update the
   selection mark (it becomes a space), current input member := 0, and **execute the turn**
   (section 3). Afterwards, if the battle exit code is non-zero, the battle-lost flag is set or all
   enemies are dead, stop here.
4. If there is a member who can input (searching from the current input member): if the command
   window exists, enable or disable its row 1 according to that member's silence bit and redraw it;
   update the selection mark.

**Back** (cancel in the command window): the current input member moves to the nearest previous
fighting member who can input; if there is none it stays. The selection mark is updated. **(quirk)**
Row 1 (magic) is *not* re-enabled or disabled on Back, so it keeps the state set for the member that
was current before. The previous member's earlier choice is simply overwritten when they choose again.

### 2.2 Choosing a command

A selection in the command window is ignored entirely if all enemies are dead, the party is defeated
or the battle exit code is non-zero. Otherwise:

1. The remembered "last chosen command" := the row (used by Cancel in slot 11, 2.9).
2. The reference y for target windows, **Y0**, is the y of the selection mouse position
   (`06-windows-text.md` §8.4: the pointer position stored when the selection was made with the mouse,
   in game-screen coordinates 0..479) if there is one; otherwise 250.
3. The living-enemy list is rebuilt.
4. If the magic list (slot 4) or the item list (slot 5) is open, nothing more happens.
5. The current member's action command := row, then:

| row | behaviour |
|---|---|
| 0 attack | open the attack target window (2.4) |
| 1 magic | build the battle magic list (2.5); if it is empty show the pop-up strings[94] (2.10) |
| 2 item | build the battle item list (2.6); if it is empty show the pop-up strings[92] |
| 3 escape | Next. The escape is attempted when the member acts (3.3) — it is always selectable, even when escape is forbidden |
| 4 auto | auto battle (2.11) |

### 2.3 Target window geometry

All target windows are selection lists created like this (06-windows-text.md, §6.2 "The list-window helper"):
given x, a requested width w, the number of rows n and Y0 (2.2; for windows opened from a list window
Y0 is likewise the selection mouse y of the selection that chose the list entry, else 250):

* width: (trunc(w / 32) + 1) × 32 (160 → 192, 240 → 256);
* height h = LineH × n + 6;
* y = Y0 − trunc(LineH × n / 2) − 10; if y + h > 480 then y = 440 − h;
* single column, left-aligned, cursor on row 0, text = the n names separated by line feeds.

### 2.4 Attack target window

Slot 6 at x 90, w 160 (→ 192), one row per entry of the living-enemy list (sorted by troop x), showing
the enemy names. Choosing row k: target := (enemy list entry k) + 100, then Next.

### 2.5 Magic in battle

**List**: the member whose list is open := current input member. The list contains every spell
s = 0..99 with 0 < learnLevel[actor][s] ≤ member level (learn levels of the member's actor, compared as
signed bytes), of any type, in spell order. Slot 4 list window at x 80, y 170, 240 × 210, 10 rows per
page, page indicator (06-windows-text.md §5.3, "Paged list windows"); each row shows the spell name and, right-
aligned, the MP cost followed by "pts.". Rows whose cost is greater than the member's current MP are
disabled.

**Choosing spell s** (slot 4 is deactivated): the action's spell := s, target := 0, then by spell type:

* type < 11 (offensive): target := 100. Type 1 (damage all): Next immediately. Otherwise the
  living-enemy list is rebuilt and an enemy target window, slot 11, x 180, w 160 (→ 192), one row per
  living enemy, is opened.
* type ≥ 11 (supportive): target := 0. Type 12 (heal all) or 21 (escape): Next immediately. Type 20
  (teleport): teleport destination list (2.7, owner slot 4). Otherwise a party target window, slot 11,
  x 180, w 160 (→ 192), listing **all fighting members** (dead ones included). Each name is drawn in
  the normal text colour if that member's status is 0 and HP > 0, otherwise in RED (FF0000).
  **(quirk)** Its y position is computed with the party-list count *before* the list is rebuilt (the
  count left by the last action of the previous turn = the number of living fighting members at that
  moment, or F if no action happened since the last rebuild), while its height uses the rebuilt count F:
  y = Y0 − trunc(LineH × n_old / 2) − 10; then, with h = LineH × F + 6, if y + h > 480 then
  y = 440 − h.

### 2.6 Items in battle

**List**: all items with count > 0 and item type > 5, in item order. Slot 5 list window at x 80, y 170,
240 × 210, 10 rows per page; each row shows the item name and, right-aligned, "(" + count + ")".

**Choosing item i** with item type T; let S be the spell type of the spell cast by the item (spell
T − 18) when T ≥ 18, else −1. The action's item := i and target := −1, then:

* T < 6: Next (cannot occur in battle: such items are not listed).
* S = 12: target := 0, Next. S = 1: target := 100, Next. S = 21: target := 0, Next.
* S = 20: teleport destination list (2.7, owner slot 5).
* Otherwise slot 5 is deactivated and a target window, slot 11, x 180, is opened:
  * if 0 ≤ S ≤ 10: target := 100; enemy target window (w 160 → 192) with the living enemies;
  * else (S ≥ 11, or a plain item with S = −1): target := 0; party target window, w 240 (→ 256), all
    fighting members (dead included), names coloured as in 2.5, and on each row a value column:
    "MP", current MP, "/", max MP if S = 13 or T = 7 or T = 15, else "HP", current HP, "/", max HP
    (column layout in 06-windows-text.md). Here the list is rebuilt before the position is computed.

### 2.7 Teleport destination list

Used by teleport spells and items (owner = slot 4 or 5). If teleport is disabled (by event command) or
the party is in a vehicle: pop-up strings[96] on the owner. Otherwise the list consists of the stored
teleport destinations (area map ids) up to the first entry ≤ 0; if it is empty: pop-up strings[97].
Otherwise the owner is deactivated and slot 7 opens at x 180, y 140, 240 × 210, 10 rows per page, rows
= the map names (`05-events.md`, command 45; `07-menus-shop-inn.md` §3.5).

### 2.8 Choosing a target

* Slot 6 (attack target): see 2.4.
* Slot 11 row k: if the action's target is 0, target := (party list entry k) (the position of that
  fighting member); otherwise target := (enemy list entry k) + 100. Then Next.
* Slot 7 row k: target := teleport destination k (an area map id). Then Next.

### 2.9 Cancel

Cancel (Esc in the active window, right click on it; 06-windows-text.md) in battle:

| window | effect |
|---|---|
| slot 2 command menu | Back (2.1); the command window stays open |
| slot 6, 5 or 4 | re-activate the command window; close the cancelled window |
| slot 11 | re-activate slot 4 if the last chosen command (2.2) was magic, otherwise slot 5; close slot 11 |
| slot 7 | re-activate slot 5 and slot 4 (whichever exist); close slot 7 |
| slot 12 (pop-up) | close the pop-up at once (the cancel also ends its wait); the pop-up procedure then re-activates its owner as usual (2.10) |
| slot 1 | ignored |
| slot 0 | ignored if slot 6 holds a window or the last chosen command (2.2) was item; otherwise the message window is closed (see below) |

The message window is display-only and is never the focused slot during a battle, so a cancel for slot
0 does not normally occur. If one did, the original would close the message window, and the next
`Message` would create a new one at the top position (06-windows-text.md §9) instead of the battle
position (UNCERTAIN whether reachable; an implementation may follow the table).

The action's spell and item fields are not reset by cancelling in battle.

### 2.10 Pop-up notices

Pop-up notice (06-windows-text.md §11, "Pop-up notice (slot 12)"), slot 12, owned by the window
named: position x = owner's left + 60, y = the selection mouse y (game-screen coordinates,
06-windows-text.md §8.4) if the triggering selection was made with the mouse, else the owner's top +
10; width = character count × W; height LineH + 14; the owner is deactivated, the text shown for
`Wait(15)` (1.5 s, skippable), then the pop-up closes and the owner is re-activated. In battle the
owner of the "no magic" / "no items" pop-ups is the command window (at the default position: (70, 290)).

### 2.11 Auto battle

Choosing row 4: auto-battle flag := 1; the mouse pointer changes to the program's busy pointer
(UNCERTAIN exact shape); the command window is closed. Then, while the battle exit code is 0, the
program is not quitting and the auto-battle flag is set:

1. Rebuild the living-enemy list (n entries).
2. Current member's command := 0 (attack).
3. r = rand(100).
   * r < 50: target = the **lowest-HP** enemy of the list: scan the list in order with a threshold of
     9999; an enemy qualifies if its HP ≤ threshold, and then becomes the candidate and the threshold
     := its HP (so ties select the **last** one in the list; enemies with HP > 9999 never qualify).
   * 50 ≤ r < 70: target = the **highest-HP** enemy: same scan with "HP ≥ threshold", starting
     threshold −1 (ties → last).
   * r ≥ 70: target = list entry rand(n).
   If no enemy qualified in the first two cases (only possible when every HP > 9999), the result is
   rand(n) used directly as an **enemy slot number** (not as a list index) **(quirk)**.
   The action target := result + 100.
4. Next (this may execute a whole turn).
5. Stop if all enemies are dead, the party is defeated or the battle exit code is non-zero.

Members who already chose commands before auto battle was selected keep those commands for this
turn. While the auto-battle flag is set, the heal override (3.5) applies to **every** member's action
at execution time, including commands chosen manually earlier in the same turn **(quirk)**.

### 2.12 Stopping auto battle

During a battle the auto-battle flag is cleared, and the normal mouse pointer restored, by:
the Escape key while the command window does not exist, the joystick cancel button while the command
window does not exist, and the right mouse button pressed on the game screen outside the windows (a
right click on a window is that window's cancel, 2.9). Because input is only
processed during waits, this takes effect inside a turn: the turn finishes; at its end the command
window is created again (3.1) and the loop of 2.11 ends, so normal input resumes.

---------------------------------------------------------------------------------------------------

## 3. Turn execution

### 3.1 The turn loop

1. Destroy the command window (slot 2).
2. Repeat:
   1. Rebuild the party list (all fighting members, count F) and the living-enemy list.
   2. Compute the turn order (3.2).
   3. For each entry in order: party entries act (3.3), enemy entries act (3.6). After each action:
      status refresh (1.7.1); if the battle exit code is non-zero or the program quits, the turn ends
      immediately (no further checks); if the party is defeated (3.8), leave the action loop.
   4. If the party is defeated: `Wait(messageSpeed × 2)` tenths, set the battle-lost flag, restore the
      normal mouse pointer, end the turn (6.4).
   5. If all enemies are dead: **victory** (6.1), end the turn.
   6. End-of-turn effects (3.7).
   7. Set the message window text to strings[131] (default "Go Go Go!") directly (no wait).
   8. If no fighting member can input (HP > 0, not paralysed, not asleep), repeat from 2.1; otherwise
      leave the loop.
3. If the auto-battle flag is not set, create the command window again (same geometry and contents as
   in 1.7.2; cursor on row 0).

Victory is checked only after all actions of the round. Combatants acting after the last enemy died do
nothing (3.3/3.6 return silently once one side has no living member, after their paralysis/sleep
messages and, for an enemy, its confusion message).

**(quirk)** Victory is detected only in step 2.5. If the last enemy dies from poison in step 2.6 and a
party member can input, the loop ends normally (the "Go Go Go!" text and the command window appear),
and the battle then ends because all enemies are dead — **without** victory processing: no victory
music, no gold, no experience, no drop, no restoring of the saved stats. If instead nobody can input,
the next round finds no enemies and victory processing happens normally.

**(quirk)** If poison at the end of a turn kills the last living, non-paralysed party member, a new
round starts: the turn order is rolled (random calls for every living combatant), the first combatant
in order "acts" (it may show its paralysed/asleep/confused message, then does nothing), and only then
the defeat is detected.

### 3.2 Turn order

1. For each **living** fighting member i = 0..F−1 in order: key = agility + rand(10), raised to −1000
   if lower; entry (party i).
2. Then for each living enemy slot j = 0..enemy count−1: key = agility + rand(10) (same clamp); entry
   (enemy j).
3. The entries are inserted one by one, in the order generated, into an ordered list: each new entry
   is placed **before** the first entry whose key is ≤ its key. Result: descending keys; on equal keys
   the entry generated later goes first (so an enemy goes before a party member with the same key, and
   a higher slot or position before a lower one).

Combatants who die before their turn are skipped by their action (it checks HP). Nobody is added during
a round: a member or enemy revived during the round does not act in it.

### 3.3 Party member action

For party position i with action record a (command, target, item, spell):

1. If a.command = −1 or HP ≤ 0: nothing.
2. If paralysed (0x02): `Message(NAME + strings[49], 0.5)` (は麻痺していて動けない); end.
3. If asleep (0x10): `Message(NAME + strings[50], 0.5)` (は眠っている); end.
4. P := living fighting members (positions, in order), E := living enemies (slots, in order). The
   shared party-list count := |P|, enemy-list count := |E|. If P or E is empty: end (no message).
5. **Confusion** (0x08): `Message(NAME + strings[51], 0.5)` (は混乱している). Then if rand(100) < 75:
   * a.command := (rand(100) < 40) ? 1 (magic) : 0 (attack);
   * if a.command = 1: choose a random known spell: K = the list of spells the member knows (as in
     2.5, any type, spell order); if K is empty, a.command := 0 (no random call); else a.spell :=
     K[trunc(rand(|K| × 10) / 10)]. If the command became 0 (by the first roll), the original
     nevertheless overwrites a.spell with an undefined value **(quirk)**; implementations should set
     a.spell := −1 in that case (the stale-action rule below then treats a later magic command with
     it as no action);
   * a.target := (rand(100) < 40) ? P[rand(|P|)] : E[rand(|E|)] + 100;
   * mark this action as "confused action".
   (The confused member may target itself or allies, and may cast any known spell on anyone.)
6. **Escape** (a.command = 3): `Message(NAME + strings[123], 0.5)` if F < 2, else
   `Message(NAME + "たち" + strings[123], 0.5)` (は逃げ出した！). Then, if the escape forbidden flag is
   set: `Message(strings[124], 0.5)` (逃げられない！). Otherwise r = rand(100) is drawn **first**, then
   the escape chance c (3.4) is computed; if r < c the battle exit code := −1 (the battle ends after
   this action), else `Message(strings[125], 0.5)` (しかし敵に回り込まれた！). End.
7. t := a.target.
8. **Heal override**: if the auto-battle flag is set, this is not a confused action and the member is
   not silenced: compute the auto heal (3.5); if it returns a spell s: a.command := 1, a.spell := s,
   t := its target.
9. The target side is the party (count F) if t < 100, else the enemies (count = enemy count); k :=
   t mod 100 (t − 100 for enemies).
10. Let ST be: the spell type of a.spell if a.command = 1; the spell type of spell (T − 18) if
    a.command = 2 and the item's type T > 17; otherwise −1.
11. **Retargeting**: if ST is not 20, 21, 1, 12 or 16, and the target record k of that side is dead,
    and not (a.command = 2 and the item type is 10), then k := the living member of that side with the
    largest (max HP − HP), scanning indices 0..count−1 with "strictly greater than the best so far"
    starting from −1 (ties → the first; a living member with no damage qualifies); if no member of
    that side is alive, k := 0. t := k (+ 100 for the enemy side).
    (For an action with target −1 the result is undefined; only key items have target −1 and they
    do nothing anyway, 4.6; implementations skip retargeting for t = −1.)
12. Execute:
    * command 0: `Message(NAME + "の攻撃", 0.5)`; if t ≥ 100 play animation 99 on enemy k with no
      acting enemy (5.5); physical attack by i on t (4.1, mode 0).
    * command 1: cast spell a.spell by i on t (4.3, not from an item).
    * command 2: use item a.item by i on t (4.6).
    * any other command value: nothing.

**(quirk) Stale actions.** A member who could not input this turn (asleep or paralysed) but is cured
by someone earlier in the round executes whatever its action record still holds: the action entered the
last time a command was chosen for that party position — possibly in an earlier battle — or the initial
all-zero record (attack on party position 0, retargeted only if that member is dead). The field menu
also writes into these records (exact rules in `07-menus-shop-inn.md`): opening a field sub-menu sets
position 0's command to 0 (item menu) or 1 (any other sub-menu), choosing an item or a spell there
stores it in an item or spell field, and cancelling the item or magic list sets the item or spell field
of the menu's acting member to −1. The records are not saved in save files and are not reset by a new
game or by loading. A stale magic command with spell −1 (or an item −1) has an undefined result in the
original (UNCERTAIN; implementations should treat an invalid spell or item index as "no action").

### 3.4 Escape chance

1. minP := 99; for each fighting member with HP ≠ 0: minP := min(minP, agility).
2. maxE := 0; for each enemy slot 0..enemy count−1 with HP ≠ 0: maxE := max(maxE, agility).
3. c := 95 if maxE < minP, else trunc(minP × 100 / maxE) − 10.
4. c := c + trunc(maxLuck / 10), where **maxLuck** = the largest luck (0x16) among **all party members**
   (positions 0..party size−1, reserve members included) with HP ≠ 0, starting from 0.
5. Result max(c, 0). Escape succeeds when the earlier drawn r < result.

If maxE = 0 and minP ≤ 0 the original divides by zero (UNCERTAIN; treat the quotient as 0).

### 3.5 Auto-battle heal override

For acting member i:

1. Over the fighting members with HP > 0: cHalf = number with HP ≤ trunc(maxHP / 2); cQuarter =
   number with HP ≤ trunc(maxHP / 4); worst/deficit: starting with deficit 0 and worst 0, a member
   whose (maxHP − HP) is strictly greater than the deficit becomes worst and sets the deficit.
2. A spell s is **castable** if 1 ≤ learnLevel[actor of i][s] ≤ level of i and MP of i ≥ cost of s.
   "Castable spells of type X" = all castable spells with that type, in spell order.
3. Candidates C := empty. If cQuarter > 0: if cQuarter = 1, C := castable type-11 spells; if C is
   still empty, C := castable type-12 spells. Then, if C is empty and cHalf > 0: if cHalf = 1, C :=
   castable type-11 spells; if still empty, C := castable type-12 spells.
4. If C is empty: no override. Otherwise the spell = the candidate with the smallest |power − deficit|
   (first on ties); its target = worst if its type is 11, else 0.

No random numbers are used.

### 3.6 Enemy action

For enemy slot e (combatant e + 100):

1. If HP ≤ 0: nothing.
2. Paralysed: `Message(NAME + strings[49], 1.0)`; end. Asleep: `Message(NAME + strings[50], 1.0)`;
   end.
3. If confused: `Message(NAME + strings[51], 1.0)`.
4. P := living fighting members, E := living enemies over **all 10 slots**; shared counts updated as
   in 3.3. If P or E is empty: end.
5. **Pattern**: m = number of leading non-zero bytes of the attack pattern (0xa5, up to 10). If m = 0
   the action is 1 (no random call), else action := pattern byte rand(m).
6. t := 0. If confused: t := (rand(100) < 50) ? P[rand(|P|)] : E[rand(|E|)] + 100.
7. Execute the action:
   * **1 attack, 2 critical attack, 3 double attack**: hits = 2 for action 3, else 1. For hit h = 0,1,…:
     1. h = 0: enemy jump phase 0 (5.6, plays zap64.wav), then `Message(NAME + "の攻撃", 0.5)`.
        h > 0: `Message(strings[126], 0.5)` (連続攻撃！).
     2. If not confused and h = 0, choose the target: r = rand(100);
        r < 30 → t := P[0]; else if r < 60 and |P| ≥ 2 → t := P[1]; else if r < 90 → t := P[rand(|P|)]
        (repeated until the chosen member is alive — always on the first try); else (r ≥ 90) → the
        lowest-HP member of P (scan as in 2.11: threshold 9999, "≤", ties → last; if none qualifies,
        rand(|P|) used directly as a position).
     3. If t ≥ 100 (only when confused): animation 99 on enemy t − 100 with acting enemy e (5.5).
     4. Physical attack by e + 100 on t with mode = the action number (4.1; mode 2 = critical).
     5. Continue with the next hit only while (t ≥ 100 or party member t has HP > 0) **and** X > 0,
        where X = the HP of enemy t − 100 if t ≥ 100, and for t < 100 the max-HP field (0x06) of
        **monster database (CHARA.DAT) record number t** **(quirk)**; and h + 1 < hits.
   * **4 watch**: `Message(NAME + strings[127], 0.5)` (は様子を見ている).
   * **5 flee**: `Message(NAME + strings[128], 0.5)` (は逃げ出した); HP := 0; enemy removal with
     transition 0 (5.7). No experience, gold or drop.
   * **any other value v** (spell actions; the editor writes 10 + spell number): s := v − 10.
     If not confused: if the spell type of s ≥ 11, t := (the lowest-HP enemy among E, scanned in slot
     order with threshold 9999, "≤", ties → last; if none qualifies rand(|E|) used directly as a slot)
     + 100; else t := P[rand(|P|)].
     If MP < cost of s: the action becomes 1 and the attack branch above is executed instead (for a
     non-confused enemy its target is chosen there; a confused enemy keeps t).
     Otherwise: enemy jump phase 0, then cast spell s by e + 100 on t (4.3, not from an item).
8. Unless the final action is 4 or 5: enemy jump phase 1 (return).

Pattern values 6..9 would give spell numbers −4..−1 and values above 109 spell numbers above 99; the
editor never produces them (UNCERTAIN; treat as action 1).

### 3.7 End of turn

1. For each fighting member i = 0..F−1 with HP > 0, in order:
   * poisoned (0x01): `Message(NAME + strings[132], 0.5)` (は毒に冒されている); one red palette flash
     (mode 0, colour (255, 0, 0), grey 0; 03-display.md); apply damage 5 to i with attacker 0 (4.2:
     sound, shake, damage message, possibly KO); status refresh.
   * then, if asleep (0x10) (re-checked after the poison damage): if the sleep counter (0x1c) = 0: if
     rand(3) = 0, clear the sleep bit, `Message(NAME + strings[44], 1.0)` (は目を醒ました), status
     refresh. If the counter is not 0 it is decremented instead (never happens: the counter is always 0).
2. For each enemy slot 0..9 with HP > 0, in order:
   * poisoned: `Message(NAME + strings[132], 1.0)`; apply damage 5 to the enemy with attacker 0 (blink,
     message, possible kill with rewards).
   * then, if asleep: counter 0 → if rand(2) = 0, clear the bit and `Message(NAME + strings[44], 1.0)`.
     **(quirk)** Enemy status bits are not cleared when an enemy dies, so an enemy killed by the poison
     damage just before still makes this roll and may show the waking message.

The poison damage is the constant 5 (limited to the current HP by the damage routine).

### 3.8 Battle end conditions

* **All enemies dead**: no enemy slot < enemy count has HP > 0.
* **Party defeated**: no fighting member has HP > 0 and is not paralysed. All living members being
  paralysed counts as defeat **(quirk)** — also at the very start of a battle, since paralysis persists
  outside battles; in that case the battle ends at once without any message or wait.
* **Exit code** non-zero (escape, teleport).

---------------------------------------------------------------------------------------------------

## 4. Formulas and effects

All values are the current battle values of the 16-bit battler fields (attack, defence and agility
include equipment and in-battle changes and may be negative; no clamping in battle unless stated).
Results stored into 16-bit fields wrap (00-README.md, Conventions).

### 4.1 Physical attack

Attacker A, target T (combatant numbers a and t), mode (0 = party attack or normal enemy attack,
1/2/3 = enemy pattern action).

1. If T is dead: `Message(T.NAME, 0.5)` — only the name **(quirk)**; end. (Normally prevented by
   retargeting.)
2. **Evasion**: if T is not asleep and T.agility − A.agility > 10: if rand(2) = 0 the attack misses:
   `Message(T.NAME + strings[base + rand(3)], 0.5)` with base 73 for a party target
   (はヒラリと身をかわした / は素早くよけた / は敵の攻撃を見切った), 80 for an enemy target
   (はヒラリと身をかわした / は素早くよけた / はすばしっこく逃げ回る); end.
3. dmg := A.attack − trunc(T.defence / 2).
4. v := trunc(dmg / 5); if v < 1 then v := 1. dmg := dmg + rand(2v + 1) − v.
5. If T is asleep: if dmg < 1 then dmg := rand(3); then dmg := trunc(dmg × 1.5) (the factor is exactly
   1.5; dmg is never negative here).
6. If dmg < 1: `Message(T.NAME + strings[base + rand(3)], 0.5)` with base 76 for a party target
   (は完全に防御した / は攻撃を撥ね返した / はダメージを受けない), 83 for an enemy target
   (には歯が立たない！ / には全く効いていない！ / にダメージを与えられない！); end.
7. **Critical hit**:
   * attacker is a party member: if rand(100) < trunc(A.luck / 10) + 5: message strings[71]
     (会心の一撃！) if T is an enemy, strings[72] (痛恨の一撃！) if T is a party member, f 0.5; dmg × 2.
   * attacker is an enemy and mode = 2: same message choice (strings[71] for an enemy target,
     strings[72] for a party target), f 0.5; dmg × 2. (No random call; enemies never get random
     criticals.)
8. Apply damage dmg to T with attacker A (4.2).
9. If the attacker or the target is a party member: status refresh.

Random calls in order: [evasion rand(2) [+ rand(3)]], spread rand(2v+1), [sleep rand(3)],
[zero-damage rand(3)], [party critical rand(100)], then those of the damage routine.

### 4.2 Damage application

Target T (number t), amount dmg, attacker number a (0 is passed for poison):

1. Play the sound effect "punch.wav" (09-audio-movies.md, built-in sound effects).
2. Party target: screen shake mode 2 (amplitude 8 px, 4 steps of 70 ms, 3 random calls;
   03-display.md, "Palette effects"). The windows are not changed by the shake: in full-screen mode
   the original repaints the message and status windows afterwards because the shifted picture was
   drawn over them, and they reappear exactly as they were (transparent windows keep their snapshot,
   06-windows-text.md §4.2). An implementation that keeps windows as separate layers above the screen
   needs no action here.
   Enemy target: **blink**: for k = 0..3: scene redraw (0.5) in which the target is drawn only when k
   is odd, and the attacker, if it is an enemy, is drawn 30 px lower; present the battle area; busy-wait
   40 ms.
3. dmg := min(dmg, T.HP).
4. Party target: `Message(T.NAME + "は" + dmg + strings[86], 0.5)` (ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞを受けた).
   Enemy target: `Message(T.NAME + "に" + dmg + strings[87], 0.5)` (ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞを与えた).
5. T.HP := T.HP − dmg (never below 0).
6. If T.HP < 1:
   * party: T.status := 0; `Message(T.NAME + strings[45], 0.5)` (は気絶した).
   * enemy: experience total += T.experience; gold total += T.gold. Let prob = the drop probability
     byte (0x27) as a **signed** byte. If the dropped item is −1 and prob > 0: if rand(100) < prob +
     trunc(maxLuck / 10) (maxLuck as in 3.4), dropped item := the drop item byte (0x26, ITEM.DAT index,
     0-based). Then `Message(T.NAME + strings[88], 0.2)` (をやっつけた！) and enemy removal with
     transition 3 (5.7).

Only the first successful drop of a battle is kept. A party member hit while asleep stays asleep.

Random calls in order: for a party target the 3 of the shake (step 2); for a killed enemy the drop
roll rand(100) (only if no item was dropped yet and prob > 0), then those of the removal dissolve
(5.7).

### 4.3 Casting a spell

Spell s with record M (type, animation number A, MP cost, power, name; 02-data-formats.md and 5.4),
caster number c, target value t, flag fromItem:

1. Outside battle and not from an item: close all windows first (07-menus-shop-inn.md).
2. Type 20 (teleport): dest := t; t := 0. Type 21 (escape): t := 0.
3. If not fromItem:
   1. `Message(caster.NAME + "は" + M.name + strings[60], 0.5)` (の呪文を唱えた).
   2. If the caster is silenced: `Message(strings[61], 0.5)` (しかし魔法は封じられている); end
      (no MP spent).
   3. If caster MP < cost: `Message(strings[62], 0.5)` (ＭＰが足りない！); end.
   4. Caster MP −= cost.
4. Side: party (count F) if t < 100, else enemies (count = enemy count); k := t mod 100; X := record k
   of that side.
5. Presentation:
   * t < 100 (party side): unless the type is 20 or 21, play built-in sound effect sound[file] of
     animation A's Anm file (5.2/5.3). Then, for **every** type including 20 and 21, apply the palette
     flash of animation A (5.3: mode, R, G, B, grey; 03-display.md). No Anm animation is drawn.
   * t ≥ 100 and type 12 or 1: play animation A on all enemies (5.5, target "all") with acting enemy c
     (if c is an enemy).
   * otherwise: play animation A on enemy k with acting enemy c.
6. `Wait(2)` (200 ms).
7. **Rate and amount** (single-target types, i.e. all except 1 and 12):
   1. rate := X's magic rate for spell s (byte 0x41 + s, 0..255; 100 = normal, 0 = immune).
   2. If t < 100 and **0 ≤ s ≤ 10** (the spell *number*, not the type **(quirk)**): rate := rate −
      X.magic defence (0x04), then clamped to 0..200.
   3. For types other than 20 and 21:
      * if the type is not 16 and X.HP < 1: `Message(strings[65], 0.5)` (しかし効果はなかった); end;
      * amount := trunc(power × rate / 100); v := max(1, trunc(amount / 5)); if rate > 0:
        amount := amount + rand(2v + 1) − v;
      * if amount < 1 and the type is not 7 or 15: `Message(X.NAME + strings[66], 0.5)`
        (には効果がなかった); end.
8. The type-specific effect (4.4).
9. Outside battle and not from an item: close all windows, the field menu is closed.

The spell's power thus acts as a gate for status spells: they need trunc(power × rate / 100) ± 20 % ≥ 1
before their success roll.

### 4.4 The 22 spell types

X, k, rate and amount as computed in 4.3. "Each living j" means every record j = 0..count−1 of the
side with HP > 0, in index order, evaluated one after the other (a record killed earlier in the same
loop is skipped).

| type | effect |
|---|---|
| 0 damage one | apply damage amount to t with attacker c (4.2; kills give rewards) |
| 1 damage all | for each living j: rate_j := magic rate of j for s; if t < 100: rate_j := max(0, rate_j − magic defence of j) (for **every** spell number, no upper cap **(quirk)**); amount_j := trunc(power × rate_j / 100); v := max(1, trunc(amount_j / 5)); if rate_j > 0: amount_j += rand(2v+1) − v; if amount_j < 1: `Message(NAME_j + strings[66], 0.5)` else apply damage amount_j to j (j + 100 for enemies) with attacker c |
| 2 poison | status spell with bit 0x01, messages strings[30] (は毒に冒された) / strings[31] (はすでに毒に冒されている) |
| 3 paralyse | bit 0x02, strings[33] / strings[34] |
| 4 silence | bit 0x04, strings[36] / strings[37] |
| 5 confuse | bit 0x08, strings[39] / strings[40] |
| 6 sleep | bit 0x10, strings[42] / strings[43] |
| 7 faint | if rand(100) < rate: X.HP := 0; `Message(X.NAME + strings[45], 0.5)`; if X is an enemy: enemy removal with transition 3 — **no experience, gold or drop** **(quirk)**, and X's status is not cleared; else `Message(X.NAME + strings[66], 0.5)` |
| 8 attack down | X.attack −= amount; `Message(X.NAME + "の攻撃力が下がった", 0.5)` |
| 9 defence down | X.defence −= amount; `Message(X.NAME + "の防御力が下がった", 0.5)` |
| 10 agility down | X.agility −= amount; `Message(X.NAME + "の素早さが下がった", 0.5)` |
| 11 heal one | old := X.HP; X.HP := min(X.HP + amount, X.max HP); `Message(old = X.HP ? X.NAME + strings[67] : X.NAME + "のHPが回復した", 0.5)` (strings[67] = には意味がなかった) |
| 12 heal all | for each living j: amount_j := trunc(power × rate_j / 100) with rate_j = j's magic rate (no magic defence); v := max(1, trunc(amount_j / 5)); amount_j += rand(2v+1) − v (**always**, even for rate 0); if amount_j < 1: `Message(NAME_j + strings[66], 0.5)`; else HP_j := min(HP_j + amount_j, maxHP_j) and, only if HP_j changed, `Message(NAME_j + "のHPが回復した", 0.5)` (members at full HP get no message) |
| 13 MP heal | as type 11 on MP / max MP, text X.NAME + "のMPが回復した" |
| 14 detox | if rand(100) < rate: if X is poisoned: clear bit 0x01, `Message(X.NAME + strings[32], 0.5)` (の体から毒が消えた), else `Message(X.NAME + strings[67], 0.5)`; else `Message(X.NAME + strings[66], 0.5)` |
| 15 status cure | if rand(100) < rate: status cure of X (4.8); else `Message(X.NAME + strings[66], 0.5)` |
| 16 revive | if X.HP < 1: if rand(100) < rate: p := min(power, 100); hp := trunc(X.maxHP × p / 100) + rand(7) − 3; if hp < 0 then hp := 1, else if hp > X.maxHP then hp := X.maxHP (**hp = 0 stays 0** **(quirk)**); X.HP := hp; if X is an enemy: scene redraw into the back buffer without presenting (it appears with the next presentation); `Message(X.NAME + strings[46], 0.5)` (は復帰した); else `Message(strings[47] + X.NAME + strings[48], 0.5)` (失敗！ … は気絶している). If X.HP ≥ 1: `Message(X.NAME + strings[67], 0.5)`. X's status is not changed (a revived enemy keeps its old status bits) |
| 17 attack up | X.attack += amount; `Message(X.NAME + "の攻撃力が上がった", 0.5)` |
| 18 defence up | X.defence += amount; `Message(X.NAME + "の防御力が上がった", 0.5)` |
| 19 agility up | X.agility += amount; `Message(X.NAME + "の素早さが上がった", 0.5)` |
| 20 teleport | if not in battle or the escape forbidden flag is 0: if teleport to dest (4.5) fails, `Message(strings[64], 0.5)` (しかし何も起こらなかった); else (in a battle with escape forbidden) `Message(strings[63], 0.5)` (しかし不思議な力で呪文はかき消された！) |
| 21 escape | if the stored escape destination (area map id, set by event command) is > 0: if in battle and escape is forbidden: `Message(strings[63], 0.5)` and end; else ok := teleport to the escape destination (4.5). If the destination is ≤ 0 or the teleport failed: `Message(strings[64], 0.5)` |

**Status spell** (types 2..6) with bit b and messages NEW / ALREADY: if rand(100) < rate: if X has
bit b: `Message(X.NAME + strings[ALREADY], 0.5)`, else set bit b and `Message(X.NAME + strings[NEW],
0.5)`; otherwise `Message(X.NAME + strings[66], 0.5)`.

Notes:

* The target's magic rate scales every spell (damage, healing, status chance, revive chance).
* Damage spells (types 0, 1) give rewards for kills through 4.2; the faint spell does not.
* Single-target healing of a dead target fails with strings[65]; revive is exempt from that check.
* Attack/defence/agility changes have no limits and on party members last until the next **victory**
  (6.1); they survive escapes, teleports, defeat and the field. On enemies they last for the battle.
* Enemies can cast all types. A confused party member can cast any known spell on any target (e.g.
  heal an enemy). A confused member's or enemy's teleport spell uses the target value (a party position
  0..3 or 100 + enemy slot) as the destination map id, which normally fails.

### 4.5 Teleport in battle

Teleport to area map id d succeeds only if d ≤ 500, the map d is an area map (type 2), the party is not
in a vehicle, the area's parent map has type < 2, and the area's exit point (x, y) lies inside the
parent map (x < parent width and y < parent height) (04-map-movement.md, "Teleport"). On success the
sound effect "wibble.wav" is played; in battle the battle exit code := d, and the battle ends after
the current action (6.3, section 7); outside battle the party is moved at once (07-menus-shop-inn.md).

### 4.6 Using an item

Item i (record: cost, type T, power p, equip mask, name), user position u, target value t:

1. Outside battle: close all windows first.
2. S := spell type of spell T − 18 if T ≥ 18, else −1.
3. If the item count of i < 1 (used up by someone earlier in the same round):
   `Message(user.NAME + "は" + item name + strings[122], 1.0)` (を使おうと思ったが、どこにも見当たらなかった);
   go to 6.
4. `Message(user.NAME + "は" + item name + strings[121], 0.5)` (を使った).
5. * If T < 6: `Message(strings[64], 0.5)`; the item is **not** consumed.
   * Otherwise the count of i is decreased by 1 (if it becomes < 1 it is set to 0 and the event page
     conditions of the current map are re-evaluated at once, "Refresh all pages" of `05-events.md`
     §4.3; no open list window is refilled). Then:
     * outside battle, if S ≥ 0 and (S < 11 or 17 ≤ S ≤ 19): `Message(strings[64], 0.5)` (wasted);
     * else if T > 17: if t = −1, `Message(strings[64], 0.5)`; else cast spell T − 18 by u on t with
       fromItem set (4.3: no casting message, no silence check, no MP);
     * else the item effect of type T on party position t with power p (4.7).
6. Outside battle: close all windows, the field menu is closed.

Items work while silenced.

### 4.7 Item effects (types 0..17)

| type | meaning (ITEM.DAT) | effect when used |
|---|---|---|
| 0–4 | weapon, armour, shield, helmet, accessory (equipment; 07-menus-shop-inn.md) | (T < 6) strings[64], not consumed |
| 5 | key item | strings[64], not consumed |
| 6..17 | consumables | see below |
| 18 + n | cast spell n | see 4.6 |

For T = 6..17, X = party record t:

1. If X.HP < 1 and T ≠ 10: `Message(strings[65], 0.5)`; end.
2. * **6 HP recovery**: old := HP; HP := min(HP + p, maxHP); `Message(HP changed ? X.NAME +
     "のHPが回復した" : X.NAME + strings[67], 0.5)`.
   * **7 MP recovery**: the same on MP, "のMPが回復した".
   * **8 detox**: text := X.NAME + strings[32] if poisoned, else X.NAME + strings[67]; clear bit 0x01;
     `Message(text, 0.5)`.
   * **9 status recovery**: status cure (4.8).
   * **10 revive**: if HP < 1: HP := max(1, trunc(maxHP × min(p, 100) / 100)); status := 0;
     `Message(X.NAME + strings[46], 0.5)`; else `Message(X.NAME + strings[67], 0.5)`.
   * **11..17 stat items**: field and display name:
     11 max HP (0x06) 最大ＨＰ; 12 max MP (0x0c) 最大ＭＰ; 13 power (0x10) 力; 14 vitality (0x0e) 体力;
     15 intelligence (0x14) 知力; 16 agility (0x12) 素早さ; 17 luck (0x16) 運.
     text := X.NAME + "の" + name + "が" + p + "ポイント上がった".
     field := field + p (16-bit); if the result is > 9999 it becomes 9999 (no lower limit).
     Additionally:
     * 13: text += line feed + "攻撃力が" + p + "ポイント上がった"; attack := clamp(attack + p, 0, 9999).
     * 14: text += line feed + "防御力が" + p + "ポイント上がった"; defence := clamp(defence + p, 0,
       9999); text += line feed + "最大ＨＰが" + (p × 10) + "ポイント上がった"; maxHP := clamp(maxHP +
       10p, 1, 9999).
     * 15: text += line feed + "最大ＭＰが" + (p × 10) + "ポイント上がった"; maxMP := clamp(maxMP + 10p,
       0, 9999).
     `Message(text, 1.0)`. The texts always say 上がった, also for negative p. Current HP/MP are not
     raised. (The additions are computed in 16 bits before clamping.)

In battle, attack, defence and agility raised by stat items are reset by a victory (6.1) while power,
vitality, max HP etc. keep their increase **(quirk)**.

### 4.8 Status cure

For record X: if (status & 0x1e) = 0: `Message(X.NAME + strings[67], 0.5)`. Otherwise build one text
of lines, each present only if its bit is set, in this order:

* paralysis: X.NAME + "は" + strings[35] + line feed — with the default strings[35] (は麻痺から回復した)
  this gives "…はは麻痺から回復した" **(quirk)**;
* silence: X.NAME + strings[38] + line feed (の沈黙が解けた);
* confusion: X.NAME + strings[41] + line feed (は正気を取り戻した);
* sleep: X.NAME + strings[44] + line feed (は目を醒ました).

Then status := status AND 0xffe1 (bits 0x02, 0x04, 0x08, 0x10 cleared; poison stays) and
`Message(text, 1.0)`.

---------------------------------------------------------------------------------------------------

## 5. Animations, sounds and screen effects

### 5.1 Sounds used by battles

| sound | when |
|---|---|
| "punch.wav" | every damage application (4.2) |
| "zap64.wav" | every enemy jump phase 0 (5.6) |
| "wibble.wav" | successful teleport/escape (4.5) |
| built-in sound effect sound[f] | first frame of an Anm sub-animation (5.5); party-side spell effects (4.3) |
| audio slot 7, 8, 9, 10 | encounter jingle, battle BGM, victory BGM, level-up BGM |

"Built-in sound effect n" is the n-th name of the built-in sound-effect name table (11-appendix.md); it
is played as a sound effect without volume, pan or frequency parameters (09-audio-movies.md).

### 5.2 Animation files

Animation pictures are files `AnmNN.bmp` (NN = file number 00..41, two digits) in the game folder: a
horizontal strip of 128 × 128 frames, **frames = trunc(bitmap width / 128)** (0 for a bitmap
narrower than 128 pixels, see 5.5; standard files have 3 frames; Anm32 and Anm35 have 4, Anm34 has 5). The picture is colour-matched to the current palette like
the monster pictures; its colour key is the index of its top-left pixel.

Per-file constants (index = file number f):

| f | sound | position | repeat | start |
|---|---|---|---|---|
| 0 | 0 | −2 | 1 | 0 |
| 1 | 18 | −2 | 1 | −1 |
| 2 | 19 | −4 | 1 | 0 |
| 3 | 15 | −1 | 2 | 0 |
| 4 | 16 | −4 | 1 | 0 |
| 5 | 28 | −4 | 1 | 0 |
| 6 | 24 | −1 | 2 | 0 |
| 7 | 32 | −3 | 1 | 0 |
| 8 | 21 | −3 | 1 | −1 |
| 9 | 23 | −3 | 1 | −1 |
| 10 | 22 | −3 | 1 | −1 |
| 11 | 8 | −1 | 1 | −1 |
| 12 | 8 | −1 | 1 | −1 |
| 13 | 25 | −2 | 2 | 0 |
| 14 | 48 | −4 | 1 | 0 |
| 15 | 48 | −4 | 2 | 0 |
| 16 | 48 | −2 | 1 | 0 |
| 17 | 52 | −2 | 1 | 0 |
| 18 | 33 | −2 | 1 | 0 |
| 19 | 27 | −2 | 2 | −1 |
| 20 | 29 | −2 | 1 | −1 |
| 21 | 14 | −3 | 1 | −1 |
| 22 | 43 | −2 | 1 | −1 |
| 23 | 3 | −1 | 1 | −1 |
| 24 | 34 | −2 | 1 | −1 |
| 25 | 18 | −2 | 2 | −1 |
| 26 | 33 | −2 | 1 | −1 |
| 27 | 52 | −2 | 1 | 0 |
| 28 | 16 | −4 | 1 | 0 |
| 29 | 16 | −4 | 1 | 0 |
| 30 | 16 | −2 | 2 | −1 |
| 31 | 46 | −3 | 2 | 0 |
| 32 | 46 | −2 | 1 | −1 |
| 33 | 46 | −4 | 1 | 0 |
| 34 | 46 | −2 | 1 | 0 |
| 35 | 34 | −3 | 1 | −1 |
| 36 | 25 | −1 | 2 | 0 |
| 37 | 46 | −3 | 2 | 0 |
| 38 | 13 | −3 | 1 | −1 |
| 39 | 8 | −2 | 1 | 0 |
| 40 | 18 | −4 | 1 | 0 |
| 41 | 2 | −2 | 1 | −1 |

* **sound**: built-in sound-effect number (5.1).
* **position**: −1 random, −2 centred on the target enemy, −3 above it, −4 at its feet (5.5).
* **repeat**: the strip is played this many times; the length of a sub-animation is
  **len = frames × repeat** frames.
* **start**: start frame of the sub-animation; −1 = computed (5.5).

### 5.3 Animation numbers

There are 100 animation numbers A = 0..99. Each has:

* **count** = 10 if 24 ≤ A ≤ 47, else 1 (number of sub-animations; all use the same file);
* **file** = ANMTAB[A mod 66] for A < 99, and file 0 for A = 99, with
  ANMTAB[0..65] =
  1, 40, 4, 2, 35, 6, 7, 5, 8, 10, 29, 9, 11, 12, 36, 13, 21, 20, 27, 32, 16, 16,
  15, 14, 30, 28, 3, 2, 35, 6, 7, 5, 23, 10, 29, 9, 11, 12, 36, 13, 21, 20, 27, 32,
  16, 16, 15, 14, 19, 22, 34, 18, 24, 26, 20, 20, 20, 14, 33, 16, 31, 14, 14, 14, 37, 37;
* **frame delay** = 1 ms for A = 99, else 100 ms;
* **minimum length** = 10 if the file's start is −1 and count ≥ 2, else 3;
* **flash** (mode, R, G, B, grey), used when a spell with this animation affects the party side
  (4.3; palette effect modes in 03-display.md):

| A | flash |
|---|---|
| A < 48 and A mod 24 in 0..3 | (6, 180, 0, 0, 0) |
| A < 48 and A mod 24 in 4..7 | (6, 0, 0, 180, 0) |
| A < 48 and A mod 24 in 8..11 | (1, 180, 180, 0, 0) |
| A < 48 and A mod 24 in 12..15 | (1, 180, 180, 180, 0) |
| A < 48 and A mod 24 in 16..19 | (6, 130, 0, 0, 0) |
| A < 48 and A mod 24 in 20..23 | (6, 180, 180, 180, 0) |
| 48..56 | (1, 130, 130, 130, 1) |
| 57..99 | (6, 180, 180, 180, 0) |

### 5.4 Animation number of each spell

At program start (after MAGIC.DAT is loaded) every spell s = 0..99 in order gets an animation number
from its type, using four counters that start at 0, 24, 20 and 44:

* type 0: the next value of a counter cycling 0, 1, …, 19, 0, 1, …;
* type 1: cycling 24, 25, …, 47, 24, …;
* type 11: cycling 20, 21, 22, 23, 20, …;
* type 12: cycling 44, 45, 46, 47, 48, 44, …;
* other types t < 11 (2..10): A = t + 46 (48..56);
* other types t > 12 (13..21): A = t + 44 (57..65).

The type is taken as an unsigned 16-bit value (a negative stored type falls into "t > 12" and gives
A = (type mod 65536) + 44). The spell's animation number is the bitwise OR of the stored animation
field of the MAGIC.DAT record (offset 2; 0 in every record of the 31 known games) and this value. If the
resulting animation number is 100 or more (only possible with a non-zero stored field or a type above
55), the original reads outside its animation table (UNCERTAIN; implementations should play no
animation, no sound and no flash for it). The normal physical attack on an enemy uses A = 99 (file 0,
1 ms per frame).

### 5.5 Playing an animation

Play animation A on target (an enemy slot, or "all"), with acting enemy slot q (or none):

**Preparation** — T := minimum length of A; e := 0 for "all", else the target slot. For each
sub-animation k = 0..count−1:

1. f := file of A. If `AnmNN.bmp` for f does not exist: error box "ﾌｧｲﾙ[" + path + "]が見つかりません"
   (1.9) and the whole animation is skipped (nothing is drawn). frames_f := trunc(width / 128).
2. mode := position of f; if count = 1 and mode = −1, mode := −2.
3. * mode −1: y := rand(120) + 120, then x := rand(512) (in this order); rectangle (x, y) 128 × 128,
     drawn; no clamping.
   * mode −2, −3, −4: if e > enemy count (note: greater than, not greater or equal) or enemy e is dead,
     sub-animation k is not drawn. Otherwise, with the enemy rectangle (ex, ey, w, h):
     x := ex + trunc((w − 128) / 2); y := ey + trunc((h − 128) / 2) for −2, ey − 32 for −3,
     ey + h + 8 − 128 for −4. Then x is clamped to 0..512 and y to 0..352; then, if e = q, y += 30.
     Afterwards, for target "all": e := e + 1, and 10 becomes 0 (this happens whether or not the
     sub-animation is drawn).
4. len := frames_f × repeat_f; T := max(T, len).
5. start_k := start of f; if it is −1: start_k := 0 for k = 0; T − len for k = count − 1; else
   rand(T − len + 1).

Steps 2 to 5 are performed for **every** k, also when sub-animation k is not drawn (dead or absent
enemy): the random numbers of mode −1 and the random start value of step 5 are drawn and discarded,
and T grows as usual. So the random calls of a preparation depend only on the animation number and
the file, never on which enemies are alive.

(So a "damage all" animation (count 10) plays one copy per enemy slot 0..9, skipping dead or empty
slots, randomly staggered when the file's start is −1; an animation with count 1 used with target
"all" — only possible for enemy heal-all spells with A = 48 — is drawn on slot 0 only.)

**Playback** — for frame t = 0, 1, …, T (inclusive):

1. If t > 0, copy the background buffer to the back buffer (at t = 0 the back buffer keeps its previous
   content).
2. Draw every living enemy in slot order, enemy q 30 px lower.
3. If t < T: for each drawn sub-animation k with 0 ≤ t − start_k < len: if t = start_k and no sound
   has been started in this frame yet, play built-in sound effect sound[f] (at most one sound per
   frame); draw frame ((t − start_k) mod frames_f) of the strip (source x = frame × 128) at its
   rectangle with the colour key.
4. Present the battle area (synchronised to the display refresh); busy-wait the frame delay.

Only the part inside the battle area becomes visible. The animation pictures are released afterwards.

**Placeholder files.** A file narrower than 128 pixels has frames_f = 0 and is played like any other:
the preparation (including its random numbers) and all T + 1 playback frames with their presentation
and frame delay take place, but since len = 0 the test 0 ≤ t − start_k < len never holds, so no frame
of it is ever drawn and its sound is never played **(quirk)**. Several sample games ship such tiny
placeholder files (1 × 1 up to 32 × 128 pixels) to switch animations off, some of them for Anm00 (the
normal attack). They must not be skipped: skipping would remove random calls and shorten every
affected action by T + 1 frame delays.

### 5.6 Enemy jump

Jump of enemy e, phase 0 (before acting) or 1 (return): phase 0 first plays "zap64.wav". Then 5 frames
j = 0..4: copy the background buffer to the back buffer, draw every living enemy in slot order, enemy
e with vertical offset OFF[phase × 5 + j], present the battle area. OFF = −8, −12, −6, 6, 30 (phase 0)
and 34, 28, 18, 7, 0 (phase 1). There is no explicit delay; each frame takes one presentation
(one display refresh, UNCERTAIN). Between the two phases the acting enemy is drawn 30 px lower in
blinks and animations (0.5).

### 5.7 Enemy removal

Enemy removal of slot e with transition number r: scene redraw of the background and all living enemies
(the removed enemy is now dead; nobody is lowered) into the back buffer only, then show the back buffer
with transition r restricted to the enemy's rectangle (03-display.md, "Screen transitions" with a
rectangle): r = 3 (dissolve from the bottom upwards) for kills, including the faint spell (4.4 type 7),
r = 0 (instant) for fleeing.

Transition 3 takes one random number per block before anything is drawn (`03-display.md` §6.2, §6.4).
The block size is 8 if the monster picture is 640 pixels wide, otherwise 2 in the reference (full-screen)
behaviour, which an implementation must use whatever its own window mode (`03-display.md` §1.3; the
original uses 4 when it runs in a window). So a kill of a w × h monster takes trunc(w / 2) ×
trunc(h / 2) random numbers, e.g. 4096 for a 128 × 128 monster. Transition 0 (fleeing) takes none.

### 5.8 Party-side effects

Effects on the party are shown only by palette effects and the shake:

* spell effects on the party side: the sound and the flash of the spell's animation number (4.3, 5.3);
* damage to a party member: shake mode 2 (4.2);
* poison at the end of the turn: one red flash (3.7).

---------------------------------------------------------------------------------------------------

## 6. Victory, level up, escape, defeat

### 6.1 Victory

When all enemies are dead after a round (3.1):

1. Restore the normal mouse pointer. Play audio slot 9 (victory) as BGM (it loops until replaced).
2. Rebuild the party list (all fighting members, count F).
3. text := ""; if gold total > 0: text := gold total + coin name + "獲得" + line feed (coin name from
   the game file).
4. If experience total > 0, append:
   * F = 1: party member 0's NAME + "は" + experience total + "pointの経験値を獲得";
   * else "それぞれ " + experience total + "pointの経験値を獲得".
   (A third text, "生き残ったﾒﾝﾊﾞｰはそれぞれ " + total + "pointの経験値を獲得", exists for a party
   list shorter than F but is unreachable because dead members are included in the list **(quirk)**.)
5. If text is not empty: `Message(text, 1.0)`; money += gold total (no upper limit).
6. For all 8 party records: attack, defence and agility := the saved stats (all in-battle changes,
   including stat-item effects on these three values, are removed).
7. Experience and level up for all party members (6.2).
8. If an item was dropped: `Message(strings[129], 1.0)` (モンスターはなにか落としていった);
   `Message(item name + strings[130], 1.0)` (を手に入れた！); item count += 1; if it became exactly 1,
   the event page conditions are re-evaluated at once ("Refresh all pages", `05-events.md` §4.3); if
   count ≥ the item maximum (game file), count := the item maximum.
9. `Wait(30)` (3 s, skippable).

The battle then ends (section 7). The experience is not divided: every living member gets the full
total.

### 6.2 Experience and level up

For each party position i = 0..party size − 1 (reserve members included) with HP > 0, in order:

1. gain := experience total for i < 4, trunc(experience total / 2) for i ≥ 4 (reserve members).
   experience := min(experience + gain, 100000).
2. If level = 100: next member.
3. n := the smallest integer ≥ level with n² × 10 > experience (level n spans experience
   (n−1)² × 10 .. n² × 10 − 1). If n = level: next member. newL := min(n, 100).
4. Play audio slot 10 (level up) as BGM (not restarted if that BGM is already playing).
5. `Message(NAME + "はレベルが上がった!", 0.7)` (ASCII "!").
6. Remember the old vitality, power, intelligence, agility, luck, max HP, max MP, attack, defence.
7. Growth, in this order, with the curves of the member's actor (actor id 0xaf; actor block of the
   game file, value at level L = curve[L − 1]; 02-data-formats.md): power, vitality, intelligence,
   agility, luck: stat := stat + curve[newL − 1] − curve[level − 1] (16-bit result), then if > 9999,
   9999.
8. If intelligence changed: max MP := min(rand(5) + (intelligence − 1) × 10, 9999).
   If power changed: attack := power + power of the equipped weapon (if any), then if > 9999, 9999.
   If vitality changed: max HP := min(rand(5) + vitality × 10, 9999); defence := vitality + powers of
   the equipped armour, shield and helmet (those equipped), then if > 9999, 9999.
   (Random order: max MP before max HP. Equipment is per actor; 07-menus-shop-inn.md.)
9. HP := min(HP, max HP); MP := min(MP, max MP). HP and MP are not refilled.
10. Messages, where D(name, old, new) = "" if unchanged, name + "が" + (new − old) + "ポイント上がった" +
    line feed if increased, name + "が" + (old − new) + " ポイント下がった" + line feed (note the space)
    if decreased:
    * `Message("レベルが " + level + " から " + newL + " に上がった" + line feed + D(最大ＨＰ) + D(最大ＭＰ)
      + D(力), 2.0)`;
    * t := D(体力) + D(知力) + D(素早さ) + D(運); if t is not empty, `Message(t, 2.0)`;
    * t := D(攻撃力) + D(防御力); if not empty, `Message(t, 2.0)`.
11. Magic learning: for each level l = level + 1 .. newL, for each spell s = 0..99 with
    learnLevel[actor][s] = l: if strings[133] equals "未使用", `Message(NAME + "は " + spell name +
    " の魔法を覚えた", 2.0)`, else `Message(NAME + "は " + spell name + " " + strings[133], 2.0)`.
12. level := newL.

This routine runs only at a victory. Event command 11 (Change experience) does not use it: it only
changes the experience (limited to 0..100000) and never changes the level, plays no music and shows no
message (`05-events.md`).

### 6.3 Escape and teleport outcomes

* **Escape** (exit code −1): no experience, gold or drops; the saved stats are **not** restored, so
  attack/defence/agility changes made during the battle stay **(quirk)**; at the battle end confusion,
  silence and sleep are removed, poison and paralysis are kept.
* **Teleport / escape spell** (exit code = area id > 0): the same, then the party is moved to the exit
  point of that area on its parent map (section 7).

### 6.4 Defeat

After the round in which the party became defeated (3.1 step 2.4): `Wait(messageSpeed × 2)` tenths, the
battle-lost flag is set, and the battle ends (section 7). At battle start (all members paralysed) the
battle ends at once without that wait; the battle-lost flag is set by the end sequence. The game-over
handling itself (running the game-over event, eve99999.dat, and showing the map again if the screen is
still erased) is done by the main loop (01-runtime.md, "Game over"; 05-events.md). A battle started by an
event command stops that event when it ends in defeat (05-events.md).

---------------------------------------------------------------------------------------------------

## 7. Battle end sequence

When the battle loop of 1.3 step 13 ends:

1. Restore the normal mouse pointer. If the party is defeated (3.8), set the battle-lost flag.
2. Close all windows.
3. Release the monster pictures. For each fighting member: status := status AND 0x0003 (poison and
   paralysis kept; silence, confusion, sleep removed).
4. Hide the screen with transition 13 if the battle exit code > 0, else with **transition table entry
   2** (blackening). Screen state := ERASED.
5. Request the music name "演奏停止" (`09-audio-movies.md` §3.1, as in 1.3 step 4), then stop every
   loaded wave sound (music, effects, and finished effects that are still loaded) and unload all of them
   except the last one in the list of loaded sounds, which stays loaded (stopped) and keeps its sound
   number **(quirk, `09-audio-movies.md` §4.3)**; all four wave channels become free
   (`09-audio-movies.md` §3.6).
6. If the program is quitting: stop here.
7. Reload the map graphics (character set and tile graphics, because the palette changed;
   04-map-movement.md / 03-display.md).
8. If the battle-lost flag is not set, the pass-abort flag (00-README.md §5, set e.g. when an event
   ended the game, 01-runtime.md §1) is not set, and the exit code < 1: resume the saved BGM
   (`09-audio-movies.md` §3.6, force no).
9. For all 8 party records: if HP < 0, status := 0.
10. If the battle-lost flag is set: clear the in-battle flag, set the pass-abort flag, and return (the
    screen stays erased; no music).
11. If the pass-abort flag is not set:
    * exit code < 1 (normal end, escape): redraw the map tiles and characters, show the screen with
      **transition table entry 1**, screen state := MAP;
    * exit code > 0: with map-change transition (table entry 0) temporarily set to 13, move the party to
      the area's parent map at the area's exit point (map change, 04-map-movement.md); restore the
      entry; resume the saved BGM (force no).
12. Clear the in-battle flag; reset the leader's mouse-drag counters; set the leader's move type to 5
    (player control; this also ends a mouse auto-walk, move type 9).

---------------------------------------------------------------------------------------------------

## 8. Status effects summary

Status word = battler field 0x28. The status name shown is strings[20 + k], k = position of the highest
set bit among bits 0..7 + 1 (strings[20] 正常 if none of bits 0..7 is set).

| bit | name | inflicted by | effect | ends |
|---|---|---|---|---|
| 0x01 | 毒 poison | spell type 2, event command | 5 HP at every end of turn (party and enemies); per step on the map (04-map-movement.md) | detox spell/item, status 0 by event, revive item, KO; **persists after battle** |
| 0x02 | 麻痺 paralysis | type 3, event command | cannot input or act (strings[49]); all living members paralysed = defeat | status cure; **persists after battle** |
| 0x04 | 沈黙 silence | type 4 | magic command disabled; casting prints strings[61] (no MP used); no heal override; items still work | status cure, battle end |
| 0x08 | 混乱 confusion | type 5 | party: 75 % random action (3.3); enemies: random target, 50 % own side (3.6) | status cure, battle end (never wears off by itself) |
| 0x10 | 眠る sleep | type 6 | cannot input or act (strings[50]); physical damage × 1.5, never evades; wakes at the end of each turn with probability 1/3 (party) or 1/2 (enemy) | status cure, waking roll, battle end; damage does not wake |
| 0x20 | 気絶 KO | — | display only: set for dead fighting members during the status refresh, then cleared together with all other bits | — |
| 0x40 | 未使用 | — | never set; it would block acting in the field party list (07-menus-shop-inn.md) | — |

KO by damage clears the whole status word of a party member; enemy status words are never cleared by
death. The sleep counter (0x1c) is always 0, so waking is a pure per-turn roll.

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* 0.3 — The 0.7 message wait is one tenth shorter than 7 × speed / 10 when the message speed is a multiple of 10 (extended-precision product, truncated).
* 1.1 — The troop draw of event command 4 with to < from − 1 takes a random number and uses its remainder by a negative divisor; with to = from − 1 it takes none.
* 0.5 — A monster whose rectangle is not completely inside the screen is not drawn at all (`03-display.md` §1.2, §10).
* 1.9 — A failed battle setup (missing bgNN.bmp or MONSTPIC.DAT) leaves the battle BGM playing on the map, does not reload the map graphics and may leave the background palette on screen.
* 7 — The battle end unloads every loaded wave sound except the last one in the list (`09-audio-movies.md` §4.3).
* 2.1 — Back in the command menu does not update the enabled state of 魔法 for the newly selected member.
* 2.5 — The y position of the party target window for magic uses the stale party-list count (living members at the last action).
* 2.11 — Auto battle's lowest-HP choice ignores enemies with HP > 9999 and then uses rand(n) as a slot number.
* 2.11 — While auto battle is on, the heal override also replaces commands chosen manually in the same turn.
* 3.1 — An enemy killed last by end-of-turn poison ends the battle without any victory processing or rewards (if someone can input).
* 3.1 — A party wiped out by end-of-turn poison triggers one more turn-order roll and the first combatant's turn before defeat.
* 3.3 — Members who could not input execute their stale action record (from earlier turns, battles or the field menu) if cured before acting.
* 3.3 — A confused member whose command became attack still gets its spell field overwritten (undefined value; use −1).
* 3.3 — Retargeting picks the living member of the target side with the largest HP deficit, first on ties, not a random one.
* 3.4 — The escape chance divides by the highest enemy agility (division by zero possible).
* 3.6 — The second hit of a double attack on a party member is skipped if the monster database record with that number has max HP ≤ 0.
* 3.7 — Enemies killed by poison still roll for waking up and may show the waking message.
* 3.8 — All living members paralysed counts as defeat, even at battle start.
* 4.1 — Attacking a dead target shows only the target's name.
* 4.3 — Magic defence reduces the rate of single-target spells only for spell numbers 0..10 (not types), capped at 200.
* 4.4 — Damage-all spells subtract magic defence for every spell number, without the 200 cap.
* 4.4 — The faint spell (type 7) and enemy fleeing give no experience, gold or drop.
* 4.4 — Revive can set HP to 0 while reporting success.
* 4.4 — Heal-all always applies the random spread (even at rate 0) and prints nothing for members already at full HP.
* 4.4 — A confused caster's teleport uses the target value as destination map id.
* 4.7 — Stat items used in battle lose their attack/defence/agility part at victory; their messages always say 上がった.
* 4.8 — The paralysis cure line doubles "は" with the default strings.
* 5.4 — A negative MAGIC.DAT type is read as unsigned 16-bit and gives an animation number above 99.
* 5.5 — Anm files narrower than 128 pixels (0 frames) are still prepared and played frame by frame, with all random numbers and delays, but draw nothing and play no sound.
* 5.5 — The random start frames are drawn also for sub-animations of dead or absent enemies.
* 5.7 — Every kill dissolve takes one random number per 2 × 2 block of the monster picture.
* 6.1 — The "生き残ったﾒﾝﾊﾞｰ…" victory text is unreachable.
* 6.2 — Experience is not divided; reserve members get half, dead members nothing.
* 6.3 — In-battle changes of attack/defence/agility persist after escaping, teleporting or losing.
* 1.6 / 5.2 — The colour key of monster and animation pictures is the top-left pixel's palette index.

## Open questions

* Exact shape of the mouse pointer used during auto battle (the original switches between two of its
  own pointer resources; assumed busy/normal).
* Effective frame pacing of enemy jumps and of the 1 ms attack animation (they rely on presentation
  synchronised to the display refresh).
* Behaviour for out-of-range data: troop numbers outside 1..250, troop counts above 10, monster ids
  beyond the MONSTPIC records, enemy pattern values 6..9 or above 109, a stale spell/item index of −1,
  spell animation numbers of 100 or more, teleport destination map id 0 (exit code 0 would not end the
  battle), negative destination ids. The original does not check these; recommended handling is given
  in the text.
* Whether a cancel can ever reach the message window during a battle (2.9).
