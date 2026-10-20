# 07 — Field menu, sub-menus, item and magic use outside battle, shops and inns

This chapter describes the menu the player opens on the map (the **field menu**) and all its
sub-menus, what happens when items and spells are used outside battle, the save/load file selection,
and the shop and inn event commands. Window geometry rules, label layouts, the cursor, keyboard and
mouse handling, the notifications "selected"/"cancelled"/"activate"/"deactivate", `Message`, and the
pop-up are defined in `06-windows-text.md` and used here by name. The effect formulas of items and
spells (amounts, random rolls, status changes) are shared with battle and are defined in
`08-battle.md`; this chapter states only the field-specific paths, restrictions and messages.

Conventions used below:
* LineH and W are the font metrics of `06-windows-text.md` (reference 18 and 17).
* "helper" = the list-window helper, "party-list helper" = the variant with HP/MP columns
  (`06-windows-text.md`, Creating windows). Width values written "160 → 192" are requested → rounded.
* **Close all windows** = destroy the windows of all 13 slots (with screen update). **Close the menu**
  = close all windows and clear the field-menu request. Item and magic use first close all windows but
  keep the menu request set (so `Message` does not close its window between messages), and close the
  menu at the end — except heal-all and escape cast as spells, which close the menu first (§3.3 quirk).
* "Party member i" = battler record i (party order). The **fighting size** is min(party size, 4).
* `Message(t, f, top|bottom)` as in `06-windows-text.md` §9; `Message(t, f)` without a position means
  top (the original passes top everywhere in this chapter except the two equipment messages of §4).
* The **condition word** is the battler record's status bits (`08-battle.md`): 0x01 poison,
  0x02 paralysis, 0x04 silence, 0x08 confusion, 0x10 sleep, 0x20 knocked out (display bit set by the
  battle status refresh for dead fighting members, `08-battle.md`), 0x40 and 0x80 unused.
* **Action records** are the battle action records of `08-battle.md` §0.4 (one per party position
  0..7, fields command, target, item, spell; never cleared, not saved and not reset by loading,
  `10-save-files.md`). The field menu writes into
  them (§1.7), which matters for the stale-action rule of `08-battle.md` §3.3.
* Two different "acting member" values are used, and they are **not** the same:
  * the **action member** a — the party position whose action record receives the chosen command and
    item. Set to 0 by every main-menu selection (§1.3) and by the shop (§8), and to m by the equipment
    candidate list (§4); in battle it is the member entering a command (`08-battle.md`);
  * the **menu actor** m — the party position used by the target and destination lists and by the
    cancel rules. Set to 0 when the main menu window is created (§1.2) and to k when caster k is chosen
    in the magic sub-menu (§3.2); nothing else changes it while the menu stays open.
  Normally both are 0 when an item is used; they differ only after a caster other than member 0 was
  chosen earlier during the same opening of the menu (§2.2 quirk).
* A member is **able** if HP > 0 and not paralysed (condition bit 0x02 clear). The **first able
  member** is the first party member (in party order) who is able, or member 0 if there is none.

---------------------------------------------------------------------------------------------------

## 1. The field menu

### 1.1 Opening

The field menu is requested:
* by **Esc** when no main-menu window exists, the menu is not already open, no battle and no event is
  running, and the leader's sub-step counter is 0 (the leader is standing on a tile, not between
  tiles) — `06-windows-text.md` §8.3 (Key routing by the program), `01-runtime.md` §4.2;
* by a **right click** outside all windows (or on an inactive window, `06-windows-text.md` §8.7) under
  the conditions of `01-runtime.md` §4.9 (Right button press, step 5: no battle, no event, step 4
  (left button held and leader idle) not applying, the menu request clear and the leader under player
  control — without the sub-step condition of Esc). `01-runtime.md` §4.9 is the definition.

While an event is running (including shops, inns, the title screen and the version box) the menu
cannot be opened.

The request is acted upon by the main loop at its next iteration (`01-runtime.md`): it creates the
main menu window and then runs a nested loop that only processes input and notifications (and audio)
until the request is cleared or the program quits. **The map is frozen while the menu is open**:
no map updates, no event processing, no character movement or animation. When the loop ends, slot 2 is
destroyed (with screen update) and the main loop continues.

### 1.2 Main menu window (slot 2)

Created directly (no rounding) at **(40, 110)**, width **(max(4, L) + 1) × W**, where L is the
largest character length of strings[0..4], height **6 × LineH + 6**. List layout 6 rows, item format
left-wrap (pad 10), item count 6. Rows (`06-windows-text.md`, Generic list layout):

| row | text | source |
|---|---|---|
| 0 | アイテム | strings[0] |
| 1 | 魔法 | strings[1] |
| 2 | 装備 | strings[2] |
| 3 | ステータス | strings[3] |
| 4 | 並び替え | strings[4] |
| 5 | `システム` | hard-coded (not in strings.dat) |

(The defaults of strings[n] are in `11-appendix.md`; games may change them, e.g. 魔法 → 特殊技.)
The menu actor m is set to 0.

Disabled rows:
* 並び替え (row 4) if the party has fewer than 2 members, or the leader's sub-step counter is not 0;
* アイテム, 魔法, 装備 and 並び替え (rows 0, 1, 2, 4) if the **menu is locked**: the event-running
  flag is set (this cannot be the case when the menu was opened normally, §1.1; the test exists in the
  original but has no effect), or the leader's character is executing a move route (`05-events.md`),
  or an event is suspended waiting for the hero (until the hero's move route or vehicle action
  finishes, `05-events.md`);
* ステータス and システム are never disabled.

With the default strings: L = 5 (ステータス) → width 6 × 17 = 102, height 114.

### 1.3 Selecting in the main menu

Selecting row r: the **sub-menu mode** becomes r (it stays until the next main-menu selection), the
action member a becomes 0 and the **command field of party position 0's action record** is set to 0 if
r = 0, otherwise to 1 (§1.7); the main menu is deactivated, then:

| r | sub-menu |
|---|---|
| 0 | items (section 2) |
| 1 | magic: caster list (section 3.1) |
| 2 | equipment: member list (section 4) |
| 3 | status: member list (section 5) |
| 4 | party order (section 6) |
| 5 | system menu (section 7) |

### 1.4 Selection dispatch (field menu open, not in battle)

After the common "selected" handling of `06-windows-text.md`, a selection with index k is dispatched:

| slot | sub-menu mode | action |
|---|---|---|
| 2 | any | open the sub-menu k (section 1.3) |
| 6 | 3 status | open the status screen for member k (section 5) |
| 6 | 1 magic | caster k chosen → spell list (section 3.2) |
| 6 | 2 equipment | member k chosen → slot list (section 4) |
| 6 | 5 system | system action k (section 7) |
| 6 | 0, 4 | nothing (party order reads the selection itself) |
| 11 | 2 equipment | slot k chosen → candidate list (section 4) |
| 11 | 0 items | use the item of position m's action record, user m, on member k (sections 2.2, 2.3) |
| 11 | 1 magic | cast the spell of position m's action record, caster m, on member k (section 3.4) |
| 7 | 0 items | use the item of position m's record, user m, with destination = destination list entry k (section 3.5) |
| 7 | 1 magic | cast the spell of position m's record, caster m, with destination = entry k |
| 5 | 0 items | item k of the list chosen (section 2.2) |
| 5 | 2 equipment | candidate k chosen → equip (section 4) |
| 4 | any | spell k of the list chosen (section 3.3) |
| 9 | any | **close the menu** (Enter on the status screen returns to the map) |
| 0, 8, 10, 12 | — | nothing |

### 1.5 Cancel dispatch (field menu open, not in battle)

After the common "cancelled" handling of `06-windows-text.md` §8.4 (which already ignores slot 0 while
slot 6 exists, slot 1, and slot 0 in equipment mode). The rows are tested from top to bottom and the
first matching row applies (so cancelling slot 2 always closes the menu, even while slot 10 exists):

| cancelled slot | condition | window activated | then |
|---|---|---|---|
| 2 | — | — | **close the menu** |
| any | the party order display (slot 10) exists | — | nothing more; the cancelled window is not destroyed (the party-order loop handles it, section 6) |
| 6 | — | 2 | destroy slot 6 |
| 4 | — | 6 | destroy slot 4; spell field of **position m's** action record = −1 |
| 9 | — | 6 | destroy slot 9 |
| 11 | mode 2 (equipment) | 6 | destroy slot 11 |
| 5 | mode 2 (equipment) | 11 | destroy slot 5; item field of **position m's** action record = −1 |
| 5 | other modes | 2 | destroy slot 5; item field of **position m's** action record = −1 |
| 11 or 7 | mode 0 (items) | 5 | destroy the cancelled slot |
| 11 or 7 | other modes | 4 | destroy the cancelled slot |
| 0, 8, 12 (others) | — | — | destroy the cancelled slot |

(m = the menu actor.) The activation is posted before the window is destroyed. If the window to
activate does not exist, nothing is activated (for example cancelling the item target list while no
item list exists cannot happen in practice).

### 1.6 Leaving the menu

The menu is closed by: Esc (or a right click on an active window) in the main menu; selecting in the
status screen; using an item or a spell; completing the party order; any system-menu action; a **left
click outside all windows** (`01-runtime.md` §4.9, `06-windows-text.md` §8.7) — except that in
equipment mode while a message window exists, such a click only destroys the message window and
activates slot 11. **(quirk)** A left click on an **inactive** window counts as a click outside all
windows (`06-windows-text.md` §8.7): clicking on the main menu while a sub-menu is open, or on the item
list while its target list is open, closes the whole menu.

### 1.7 Action records written by the field menu

The field menu writes into the battle action records (`08-battle.md` §0.4); nothing resets them
afterwards, so the values are seen by the next battle (stale actions, `08-battle.md` §3.3). The
complete list of writes (a = action member, m = menu actor, see Conventions):

| moment | write |
|---|---|
| main-menu selection of row r (§1.3) | position 0: command = 0 if r = 0, else 1 |
| item chosen in the item list and accepted (§2.2 step 2) | position a (= 0): item = the item number, target = −1 |
| item chosen that needs the target list (§2.2, last case) | position 0: target = 0 (after the write above) |
| spell chosen in the spell list (§3.3) | position m: spell = the spell number |
| item list (slot 5) cancelled (§1.5) | position m: item = −1 |
| spell list (slot 4) cancelled (§1.5) | position m: spell = −1 |

No other field of an action record is written by the field menu (in particular the spell choice does
not write a target, and the equipment, status, party-order and system menus write nothing except the
command of row r).

---------------------------------------------------------------------------------------------------

## 2. Items

### 2.1 Item list

The list = the numbers of all items whose count is > 0, in item-number order (0..99). Equipped items
are not counted (they are not in the bag).
* Empty list: pop-up strings[90] (アイテムを持っていません) owned by the main menu (slot 2); the main
  menu is re-activated afterwards.
* Otherwise, if slot 5 is empty: paged list window slot 5 at **(120, 80)** 240 × 210, item count = list
  length, filled with name and `(%d)` count per row, page indicator `( 1/N )`
  (`06-windows-text.md`, Paged list windows).

### 2.2 Choosing an item

Let t = the item's type and st = the type of the spell it casts (spell number t − 18) if t ≥ 18,
otherwise −1 (item types: `02-data-formats.md` and `08-battle.md`).

1. If 0 ≤ st ≤ 10 (an offensive, battle-only spell): pop-up strings[93] (戦闘時しか使用できません) owned
   by slot 5; nothing is used.
2. Otherwise the item number is stored in the item field of position a's action record (a = the action
   member, 0 here) and its target field is set to −1 (§1.7), and:
   * **t < 6** (equipment and special/key items): close all windows (keep the menu); use the item
     with user = first able member and **no target**; close the menu.
   * **st = 12** (heal all) or **st = 21** (escape): use the item at once with user = first able member,
     target 0.
   * **st = 20** (teleport): open the destination list owned by slot 5 (section 3.5).
   * Otherwise: deactivate slot 5 and open the **target list**: party-list helper, slot 11, x = 180,
     y = y0 − (LineH × n) / 2 − 10 with n = party size and y0 = the stored selection mouse y if the item
     was chosen with the mouse, else 100; requested width 240 (→ 256); one row per party member (all
     members, reserve members included, names coloured by state); the column shows **MP** if st = 13
     (MP heal) or t = 7 (MP recovery) or t = 15 (intelligence up), otherwise **HP**. The target field
     of position 0's action record is set to 0.
     Choosing member k: use the item **found in the item field of position m's action record**, with
     **user = party member m** and target k (m = the menu actor). Normally m = 0, so the item just
     chosen is used by party member 0 even if that member is down, not by the first able member
     **(quirk)**. **(quirk)** If a caster m ≠ 0 was chosen earlier during the same opening of the menu
     (§3.2), the item used is whatever position m's record holds: the item member m last chose in
     battle (it is consumed, its effect applied, and the messages name member m), or −1 if the item
     list was cancelled since (§1.5). For a value outside 0..99 the original reads outside its tables
     (**UNCERTAIN**, recommended: nothing is used, no message, and the menu is closed).

With the reference metrics and 3 members the target list is at (180, 63) 256 × 60.

### 2.3 Using an item outside battle

Steps (the item effect formulas are in `08-battle.md`, item use and item effects):

1. Close all windows (keep the menu).
2. If the item's count is < 1 (cannot normally happen in the field): `Message(user + "は" + item name +
   strings[122], 1.0, top)` and go to step 4.
3. `Message(user + "は" + item name + strings[121], 0.5, top)` — "<user>は<item>を使った". Then:
   * t < 6: `Message(strings[64], 0.5, top)` (しかし何も起こらなかった); the item is **not consumed**.
   * otherwise the count is decreased by 1; if it reaches 0 it is set to 0 and the event page
     conditions are re-evaluated at once (`05-events.md`). Then:
     * if st ≥ 0 and (st ≤ 10 or 17 ≤ st ≤ 19): `Message(strings[64], 0.5, top)` — the item is used up
       without effect (buff spells from items are wasted in the field; offensive ones cannot get here);
     * else if t ≥ 18: if there is no target, `Message(strings[64], 0.5, top)`; otherwise the spell t − 18 is
       cast **as an item** on the target (section 3.4, without the cast message, silence check and MP
       cost);
     * else (t = 6..17): the item effect on party member `target` (HP/MP recovery, detox, status
       recovery, revive, stat increases) with its messages, as in `08-battle.md`.
4. Close the menu.

Field-specific facts: a dead target makes recovery items fail with strings[65] (しかし効果はなかった),
except revive items; paralysed and poisoned members can be targets; stat-increase items in the field
are permanent (there is no victory reset outside battle); all result messages appear in a message
window at the top (y = 32).

---------------------------------------------------------------------------------------------------

## 3. Magic

### 3.1 Caster list

Party-list helper, slot 6, at **(90, 105)**, width 240 (→ 256), one row per party member (all members),
names coloured by state, **MP** column. Among the **fighting members only** (rows 0..fighting size − 1)
a member is disabled if HP < 1 or (condition word AND 0x7A) ≠ 0 (paralysis, confusion, sleep, the
knocked-out bit 0x20 or bit 0x40). Reserve members are never disabled, even when dead **(quirk)**; silenced members are not
disabled (they fail when casting).

### 3.2 Spell list

Choosing caster k: the menu actor m becomes k — before the list is built, so also when the list then
turns out to be empty and only the pop-up is shown; it stays k until the menu is closed (§2.2 quirk).
The list = all spells s (0..99, in spell order) with
spell type ≥ 11 (support spells) that the member knows: the member's learned-level byte for s, read as a
signed byte, is > 0 and ≤ the member's level (`08-battle.md`).
* Empty: pop-up strings[95] (フィールド上の魔法を覚えていません) owned by slot 6.
* Otherwise deactivate slot 6 and create paged list slot 4 at **(140, 80)** 240 × 210: spell name and
  `%dpts.` (MP cost); both labels of a row are disabled when the caster's current MP is less than the
  cost (`06-windows-text.md`, Paged list windows).

### 3.3 Choosing a spell

Deactivate slot 4; store the spell number in the spell field of position m's action record (§1.7);
by spell type:
* **12** (heal all) or **21** (escape): **close the menu** (all windows destroyed and the menu request
  cleared), then cast with caster m and target 0 (section 3.4). **(quirk)** Because the menu request is
  already cleared, every `Message` of the cast destroys its window after its wait (`06-windows-text.md`
  §9 step 5), so each message of the cast appears in a new window at the top; the same spells used
  from an item (§2.2) keep one window.
* **20** (teleport): open the destination list owned by slot 4 (section 3.5).
* otherwise (11 heal one, 13 MP heal, 14 detox, 15 status recovery, 16 revive, 17–19 attack/defence/
  agility up): target list as in section 2.2 (party-list helper, slot 11, x 180, y from y0, width
  240 → 256, all party members), column **MP** for type 13, otherwise **HP**. Choosing member k casts the
  spell stored in position m's record (the one just chosen) with caster m on k.

### 3.4 Casting outside battle

The cast procedure is the one of `08-battle.md` (magic), with these field properties:
1. Close all windows (keep the menu) — not done when the spell is cast by an item.
2. Unless cast by an item: `Message(caster + "は" + spell name + strings[60], 0.5, top)`
   ("<caster>は<spell>の呪文を唱えた"); if the caster is silenced: `Message(strings[61], 0.5, top)` and
   stop (no MP used); if the caster's MP < cost: `Message(strings[62], 0.5, top)` and stop; else
   MP −= cost.
3. The target side is the party: the spell's animation is not shown. Instead the sound of the
   animation's first picture is played (**not** for types 20 and 21) and the animation's palette flash
   is applied (for **every** type, including 20 and 21, whether cast as a spell or by an item); then
   `Wait(2)`.
4. Effect and messages as in `08-battle.md`. In the field:
   * **heal all** (12) affects only the fighting members (records 0..fighting size − 1); reserve
     members are not healed **(quirk)**;
   * single-target spells can target any party member, including reserve members;
   * single-target spells on a dead target fail with strings[65], except revive (16);
   * attack/defence/agility up (17–19) cast as **spells** work in the field and last until the next
     victory (`08-battle.md`); the same spells from **items** are wasted (section 2.3);
   * teleport (20) and escape (21): section 3.5.
5. Close the menu (not done when cast by an item; the item procedure closes it).

All messages appear at the top (y = 32).

### 3.5 Teleport and escape

**Destination list** (slot 7), opened for a teleport item (owner slot 5) or spell (owner slot 4):
* The **teleport destination list** is the game's list of up to 100 map numbers (changed by event
  commands, saved in the save file); its length is the number of entries before the first entry ≤ 0.
* If teleporting is locked (by the event command that locks it, `05-events.md`) or the party is in a
  vehicle: pop-up strings[96] (ここでは使えません) owned by the owner window.
* Else if the list is empty: pop-up strings[97] (移動先がありません).
* Else deactivate the owner and create paged list slot 7 at **(180, 140)** 240 × 210 with the map
  names of the destinations (no values).
Choosing entry k uses the item / casts the spell with destination = map number of entry k: for an
item, the item and user are taken as in §2.2 (the item in position m's action record, user = party
member m, with the same quirk); for a spell, the spell in position m's record with caster m.

**Teleport effect** (spell type 20, destination area a) and **escape effect** (type 21, area a = the
escape destination set by event command, `05-events.md`; if that is ≤ 0 the escape fails):
the move succeeds only if a ≤ 500, map a is an **area** (map type 2, `02-data-formats.md`), the party
is not in a vehicle, the area's parent map is not an area (type < 2), and the area's exit point lies
inside the parent map (x < width and y < height). On success: play the sound effect `wibble.wav`;
set the pass-abort flag (`01-runtime.md` §1); destroy the message window (with screen update); teleport the party to
the parent map at the exit point (`04-map-movement.md`) using map-change transition **13** for this
teleport only (the transitions table entry 0 is restored afterwards). On failure:
`Message(strings[64], 0.5, top)` (しかし何も起こらなかった).

---------------------------------------------------------------------------------------------------

## 4. Equipment

1. **Member list**: helper, slot 6, at **(80, 105)**, width 160 (→ 192), one row per party member,
   names coloured by state, nothing disabled.
2. Choosing member m (if m < party size): deactivate slot 6; **slot list**: helper, slot 11, at
   **(110, 115)**, width 160 (→ 192), 5 rows: strings[10..14] (武器, 鎧, 盾, 兜, その他), left-wrap.
3. Choosing slot s: deactivate slot 11. Candidates = items with count > 0, item type = s, whose
   equip mask has the bit (1 << actor number of member m) set, in item-number order.
   * None: pop-up strings[91] (装備できるアイテムを持っていません) owned by slot 11 (re-activated after).
   * Otherwise the action member a becomes m (this has no visible effect; the next main-menu
     selection resets it) and paged list slot 5 is created at **(180, 130)**
     240 × 210 with name and `(%d)` count.
4. Choosing candidate item i:
   1. Destroy slot 5; deactivate slot 11.
   2. Remember the old attack and defence of member m, and for slot 4 the "old magic defence" = power of
      the item currently in slot 4 (0 if none).
   3. Remove one i from the bag (count − 1); put i into the actor's equipment slot s; if a previous item
      was in that slot, add it back to the bag (count + 1). (No check against the item maximum.)
   4. `Message(name + "は" + item name + strings[120], 0.5, bottom)` — "<name>は<item>を装備した".
   5. Recompute: slot 0 → attack = power stat + weapon power (cap 9999); slot 4 → magic defence = power
      of the new item; slots 1–3 → defence = vitality stat + powers of the items in slots 1, 2, 3 (cap
      9999) (`08-battle.md`, derived stats). Recomputing discards any temporary attack/defence buffs.
   6. Build the change line: for slot 0 with the name 攻撃力, slot 4 with 魔法防御力, slots 1–3 with
      防御力: if the value rose by d: `"%sが%dポイント上がった\n"`; if it fell by d:
      `"%sが%d ポイント下がった\n"` (note the half-width space before ポイント in the "down" text,
      **quirk**); unchanged: no line. If there is a line: `Message(line, 1.0, bottom)`.
   7. Destroy the message window (with screen update); clear the selected index; activate slot 11 (the
      slot list), so further slots of the same member can be changed.

There is no command to remove equipment without a replacement. Equipping the last copy of an item
leaves its count at 0.

---------------------------------------------------------------------------------------------------

## 5. Status screen

1. **Member list**: helper, slot 6, at **(80, 105)**, width 140 (→ 160), names coloured by state.
2. Choosing member k: deactivate slot 6, create the status window, slot 9, directly at **(136, 80)**
   450 × 230, display-only (labels: `06-windows-text.md`, Slot 9). Texts, with m = member k:

| label | text |
|---|---|
| 21 | name |
| 22 | `Level %d` |
| 44 | condition: if HP < 1: strings[26]; otherwise strings[20 + c] with c = 0 if none of the bits 0x01..0x80 of the condition word is set, else 1 + the number of the highest set bit among bits 0..7 (so with several conditions the highest bit wins; the knocked-out bit 0x20 gives strings[26] also for a member with HP ≥ 1; the upper 8 bits are ignored) |
| 13, 6, 16, 18 | `Hp:`, HP, `/`, max HP |
| 14, 7, 17, 19 | `Mp:`, MP, `/`, max MP |
| 11, 12 | `経験値:`, experience |
| 48, 49 | `ﾚﾍﾞﾙUPまで:` (half-width katakana), max(0, L × L × 10 − experience) with L = level |
| 50, 51 | `知力:`, intelligence |
| 24, 27 | `攻撃力:`, attack |
| 25, 28 | `防御力:`, defence |
| 26, 29 | `すばやさ:`, agility |
| 32, 45, 33, 34, 35 | strings[10] … strings[14] each followed by `:` |
| 36, 46, 37, 38, 39 | name of the item in equipment slot 0 … 4 of the member's actor, or `---` if empty |
| 30, 31 | currency name + `:`, money |

All numbers are plain decimal (`%d`). For level 100 the original first prepares `-----` for label 49
but then overwrites it with the number, so the number is always shown **(quirk)**. The screen does
not change while open.
3. Esc (or right click): activate slot 6 and destroy slot 9 (back to the member list). Enter/Space or a
   left click on the status window: **close the menu**.

---------------------------------------------------------------------------------------------------

## 6. Party order

Available only with at least 2 members (section 1.2). Let n = party size.

1. Deactivate the main menu. Create the **order display**, slot 10, directly at **(200, 190 − h)**,
   200 × h with h = LineH × (n + 2) + 6, display-only; label 40 = `1`. Process pending notifications.
   With the reference metrics and n = 3: h = 96, y = 94.
2. Create the **member list**: helper, slot 6, at **(80, 180)**, width 160 (→ 192), one row per member,
   coloured by state.
3. k = 0 (number of members placed). While k < n − 1:
   1. Set the selected index to −1 and `Wait(0)`.
   2. If the order display no longer exists (the menu was closed by a click): **abort** (step 5).
   3. If a member s was selected (index ≥ 0) and s is not yet placed: label 40 + k = `"%d:%s"` (k + 1,
      name of s); keep a copy of member s's battler record as new position k; k = k + 1; if k < n,
      label 40 + k = `k + 1` (as `%d`); disable member label s + 1. If s was already placed (not
      reachable in practice, because a placed member's label is disabled and cannot be selected,
      `06-windows-text.md` §8.2), continue directly with step 3.1, skipping 3.4 and 3.5.
   4. If the wait ended without a selection (selected index −1: Esc, right click — or Enter/Space on
      a disabled (already placed) member, which ends the wait through the confirm flag without a
      selection **(quirk)**): if k = 0, **abort**; otherwise undo the last placement: label 40 + k =
      `" "` (one space), label 39 + k = `k` (as `%d`), k = k − 1, and re-enable the member label of the
      member placed at position k.
   5. Activate the member list and repaint it.
4. After n − 1 placements: **close the menu**. The one member not placed becomes position n − 1. The
   battler records 0..n − 1 are replaced by the copies in the new order (the records of actors outside
   the party are unchanged). Then the party's actor order is updated, and, **if the party is not in a
   vehicle**, the map characters are re-assigned: for each new position i with actor a, actor a's map
   character takes over from the character that was at position i before the change: its map position,
   its movement target position, its sub-step counter and its movement timer; it becomes the i-th
   party character on the map. Facing direction and graphics stay with each actor's own character.
   Finally — also in a vehicle — the new leader's character gets the move type (`04-map-movement.md`)
   and the movement
   speed that the previous leader's character had. In a vehicle the map characters are not
   re-assigned (no position, target, sub-step or timer transfer), but this final step is still done;
   it becomes visible when the party leaves the vehicle. The fighting members are the first four of
   the new order.
5. **Abort**: destroy slot 6 and slot 10 (with screen update) and activate the main menu.

Esc reaches the party-order loop because the cancel dispatch does nothing else while slot 10 exists
(section 1.5); the cancel still ends the `Wait`.

---------------------------------------------------------------------------------------------------

## 7. System menu

Helper, slot 6, at **(80, 180)**, width 160 (→ 192), 5 rows, left-wrap, hard-coded texts (not in
strings.dat):

| row | text |
|---|---|
| 0 | ゲームを保存する |
| 1 | ゲームを読み込む |
| 2 | スクリーンモードの変更 |
| 3 | バージョン情報 |
| 4 | 終了する |

Row 0 is disabled if saving is disabled (the game file's save flag, changed by event command, see
`05-events.md`) or the leader's character is executing a move route.

Choosing a row first **closes the menu**, then:

* **0 Save**: the save file selection (section 7.1); on OK the save file is written (`10-save-files.md`).
  If writing fails: a message box with the text `データを書き込むことができません`, title `RPGﾂｸｰﾙ95`
  and an OK button, then the save selection appears again. Cancel returns to the map.
* **1 Load**: the load file selection (section 7.1) and the file check (`10-save-files.md`). If a
  file was loaded: the screen fades to black (palette effect 5, `03-display.md`) and is cleared (screen
  state ERASED); the palette of CHARA.BMP is reloaded; music stops; the pass-abort flag is set;
  the current map is set to "none" so that it is reloaded; the saved current BGM is played; the party
  is teleported to the saved map at the leader's saved position (`04-map-movement.md`). If cancelled or
  failed, the game continues on the map.
* **2 Screen mode**: switch between full screen and window, the same as the F4 key (`01-runtime.md`).
* **3 Version**: the version information box (`06-windows-text.md`, Version information box).
* **4 Quit**: the program ends immediately, without confirmation. (Only closing the program window asks
  `ゲームを終了しますか`, `01-runtime.md`.)

### 7.1 Save and load file selection

The original uses the operating system's standard file dialogs (shown over the game screen):

| | save | load |
|---|---|---|
| dialog | "save as" dialog, Explorer style | "open" dialog, Explorer style |
| title | `ゲームを保存する` | `ゲームを読み込む` |
| file type filter (single entry) | `RPGｹﾞｰﾑﾃﾞｰﾀ (*.sav)`, pattern `*.sav` | same |
| default extension | `sav` (added when the typed name has none) | `sav` (a typed name without extension is looked up with `.sav` added) |
| options | read-only checkbox hidden; the system asks for confirmation before overwriting an existing file | read-only checkbox hidden; the file must exist |
| start folder | the current folder (normally the game folder) | same |

The current folder is restored after the dialog. Load errors (message boxes with title `RPGﾂｸｰﾙ95`):
the file cannot be opened → `ファイルを開くことができません` (no icon); the header does not match →
`現バージョンでこのデータを` line break `読み込むことはできません` (exclamation icon); in both cases nothing is
loaded. The same load selection is used by 続きから on the title screen (`01-runtime.md`) and the save
selection by the event command that opens the save screen (`05-events.md`).

An implementation may replace these dialogs by its own file selection (for example a list of the
`.sav` files in the game folder plus a "new file" entry), provided that save files are ordinary files
with the extension `.sav`, any name is accepted, cancelling returns to the game unchanged, and the error
texts above are shown in the error cases. While its own selection is shown, the program must behave as
during an event (no map input, no menu).

---------------------------------------------------------------------------------------------------

## 8. Shop (event command 28)

Parameters (`05-events.md`): a1 = item-list string (100 characters; character i = `X` means item i is
sold), a2 = message string with three lines (line 0 greeting, line 1 thanks, line 2 farewell), a3 =
1 for "buy only", otherwise buy and sell. Lines end at line feeds; all three lines are expected to be
present and each terminated by a line feed (true for all shops and inns of the sample games). The
original does not guard a missing line: it copies bytes up to the next line feed even past the end of
the string (undefined result; **recommended**: treat a missing line as empty).

The shop runs inside the event (the event-running state is set, so `Message` keeps its window open
between texts and the menu cannot be opened). A deal counter starts at 0; the action member a is set
to 0 (no visible effect). All `Message` calls of the shop use position top.

1. **Prompt**: if no deal has been made yet: line 0 of the message string, `$`-expanded (unexpanded if
   the expansion fails), else strings[105] (ほかにもご用がおありでしょうか); shown with
   `Message(text, 0.1, top)` — the existing message window is reused wherever it is, a new one goes to
   the top.
2. **Command window** (only created if slot 6 is empty): helper, slot 6, x centred, y = **102**,
   rows: strings[100] ( 購入する ), strings[101] ( 売却する ), strings[102] (やめる) — or for a buy-only
   shop (a3 = 1) strings[100] and strings[102]; L = the largest character length of strings[100],
   strings[102] and, **only if a3 = 0**, strings[101]; at least 6. (For a3 ∉ {0, 1} three rows are shown
   but strings[101] does not count for L; the sample data uses only 0 and 1.) Format centre-wrap. With
   the defaults (strings 100 and 101
   contain a leading and a trailing space, 6 characters): width 7 × 17 = 119 → 128, x = 256.
   `Wait(0)`. Leave the shop (step 7) if the wait ended without selection (Esc, right click), or やめる
   was chosen (index 2, or index 1 in a buy-only shop).
3. **Buy** (index 0): the list = items whose character in a1 is `X`, in item-number order.
   * Empty: pop-up strings[106] (売るアイテムはありません — sic, the "sell" text) owned by slot 6; back
     to step 1 (the command window stays).
   * Otherwise destroy slot 6 and show `Message(strings[103] + " （所持金: " + money + currency name +
     "）", 0.1)`. The format is `%s （所持金: %d%s）` — a half-width space, a full-width opening
     parenthesis, `所持金:` with a half-width colon and a half-width space, the money, the currency name,
     a full-width closing parenthesis. No `$` expansion.
4. **Sell** (index 1): the list = items with count > 0 (`02-data-formats.md`; equipped items are not in
   the bag).
   * Empty: pop-up strings[90] owned by slot 6; back to step 1.
   * Otherwise destroy slot 6 and show `Message(strings[104] + " (所持金: " + money + currency name +
     ")", 0.1)` — format `%s (所持金: %d%s)` with **half-width** parentheses.
5. **Shop list**: set the selected index to −1; if slot 8 is empty create paged list slot 8 at
   **(200, 102)** 240 × 210 with item count = list length; fill page 0 with name and price, where the
   price is the item's cost when buying and **cost / 2** (integer division) when selling; page
   indicator `( 1/N )`. Item counts are not shown. `Wait(0)`; destroy slot 8.
   **(quirk)** Every page change refills the list with the **full cost**, also in a sell list: pages
   shown after pressing Left/Right (including page 1 when returning to it) display buy prices; the
   money received is still cost / 2.
   * No selection (Esc): back to step 1 (the greeting is repeated if no deal was made yet).
   * Selected entry j (an index outside the list cannot occur; if it did, the original would leave the
     shop without destroying the command window).
6. **Deal** for item i = list entry j:
   * Buying: if count(i) < the item maximum (game file value, `02-data-formats.md`): if money < cost →
     `Message(strings[108], 1.0)` (お金が足りません); else count(i) + 1, money − cost, deal counter + 1
     (no message). If count(i) ≥ item maximum → `Message(strings[107], 1.0)` (そのアイテムはもう持てません).
   * Selling: if the item type is 5 (special item) or its cost < 2 → `Message(strings[110], 1.0)`
     (そのｱｲﾃﾑを売ることはできません); else count(i) − 1, money + cost / 2, money capped at 100,000,000,
     deal counter + 1 (no message).
   * Back to step 1. One deal handles one item; the player returns to the prompt after every deal.
   (The original builds the texts strings[109] and strings[111] after a deal but never shows them.)
7. **Leave**: destroy slot 6 (with screen update). If no deal was made: line 2 (farewell); else line 1
   (thanks) and the event page conditions are marked for re-evaluation (`05-events.md`). The line is
   `$`-expanded and shown with `Message(text, 1.0)`; then close all windows and clear the menu request.

---------------------------------------------------------------------------------------------------

## 9. Inn (event command 27)

Parameters: a1 = message string (line 0 greeting, line 1 after the stay, line 2 refused; three
line-feed-terminated lines as in section 8, with the same unguarded line picking), a2 = price
(32-bit), a3 = flags: bit 0x4000 restore HP, bit 0x2000 restore MP, bit 0x0002 cure paralysis, bit 0x0001
cure poison (bytes: high byte 0x40/0x20, low byte 0x02/0x01).

1. Text = `"%s（一泊%d%s）"` with line 0, the price and the currency name (full-width parentheses
   U+FF08/U+FF09), then `$`-expanded (unexpanded if the expansion fails); `Message(text, 0.1, top)`.
2. Command window: helper, slot 6, x centred, y = **100**, 2 rows strings[112] ( 泊まる ) and strings[113]
   ( 泊まらない ), L = the larger of their character lengths, format centre-wrap. With the defaults
   (7 characters) width 8 × 17 = 136 → 160, x = 240. `Wait(0)`; destroy slot 6.
3. If 泊まる (index 0) was chosen:
   * If money < price: text = strings[114] (気の毒だけどお金が足りないようだね。); nothing else happens.
   * Otherwise:
     1. money −= price;
     2. destroy the message window (with screen update);
     3. fade to black with palette effect 7 and clear the screen to black, screen state ERASED
        (`03-display.md`);
     4. remember the current BGM; stop the music;
     5. if the name of audio slot 11 (inn jingle, `09-audio-movies.md`) contains `.mid`, the MIDI will
        be played once (no loop); play audio slot 11 as a jingle;
     6. for every party member (all members, including reserve and dead members): if 0x4000: HP = max
        HP (this also revives dead members; their condition word is not cleared by this flag); if
        0x2000: MP = max MP; if 0x0002: condition word AND 0x00DD (clears paralysis 0x02 and 0x20);
        if 0x0001: condition word AND 0x00DE (clears poison 0x01 and 0x20); both masks also clear the
        upper 8 bits;
     7. `Wait(10)` (1 s, ended early by Enter/Space);
     8. wait until the jingle has finished (for a MIDI jingle: until it ends; for a wave jingle: while
        any sound effect channel is playing). Holding Enter or Space stops the music and all sound effects
        at once and ends this wait (`09-audio-movies.md`);
     9. restore the remembered BGM;
     10. gather the party: all party characters are placed on the leader's tile with the leader's
         facing, and the follower trail is cleared (`04-map-movement.md`);
     11. redraw the map and fade in with palette effect 7 to the neutral tone (100, 100, 100), which
         also **resets any screen tint** set by an event (`03-display.md`); screen state MAP;
     12. text = line 1.
4. Otherwise (泊まらない, or Esc / right click, which end the wait with index −1): text = line 2.
5. Text `$`-expanded (unexpanded if the expansion fails); `Message(text, 1.0, top)`; close all windows
   and clear the menu request. After a stay the message window was destroyed in step 3.2, so this
   message creates a new window at the **top** (92, 32); otherwise the greeting's window is reused.

The money comparison is a signed 32-bit comparison; the inn does not refuse a price of 0. With the
default strings[112]/[113] the command window is 160 wide at x = 240; most games ship a `strings.dat`
with `泊まる` / `泊まらない` without the surrounding spaces, giving (5 + 1) × 17 = 102 → 128 wide at
x = 256 (`11-appendix.md` §A).

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* Items chosen through the target list are used by party member 0 even if that member is dead (§2.2).
* After a caster m ≠ 0 was chosen in the magic sub-menu, items used through the target or destination list are taken from member m's action record and used by member m (§2.2, §3.5).
* The field menu writes battle action records: command of position 0 on every main-menu selection, item/target/spell fields on choices, −1 on cancels (§1.7).
* A left click on an inactive window closes the whole field menu (§1.6).
* Heal-all and escape cast as spells close the menu before casting, so each message of the cast gets its own window (§3.3).
* Teleport and escape spells skip the animation sound but still apply its palette flash (§3.4).
* The status screen shows 気絶 (strings[26]) for the knocked-out bit 0x20 even at HP ≥ 1 (§5).
* In a vehicle, the party order still gives the new leader's character the previous leader's move type and speed (§6).
* The shop command width ignores strings[101] unless a3 = 0 (§8).
* Shop and inn message lines are picked without an end-of-string check (§8, §9).
* Buff spells (types 17–19) cast by items are wasted in the field, while the same spells work when cast as spells (§2.3, §3.4).
* Heal-all in the field heals only the first four members (§3.4).
* Reserve members are never disabled in the caster list, even when dead (§3.1).
* Enter/Space on an already-placed member in the party order undoes the last placement (§6).
* Party order keeps each character's facing; only position, target, sub-step and timer move (§6).
* Equipment change recomputes attack/defence and discards temporary battle buffs (§4).
* The equipment "down" text has a half-width space before ポイント (§4).
* Status screen shows the remaining experience even at level 100 (§5).
* The shop's empty-buy-list pop-up uses strings[106] 売るアイテムはありません (§8).
* Shop list pages reached by page change show full prices even when selling (§8).
* Buy and sell prompts use full-width vs. half-width parentheses respectively (§8).
* Shop greeting is repeated after cancelling a list if no deal was made (§8).
* The inn's "restore HP" flag revives dead members without clearing their condition word (§9).
* The inn fade-in resets any event screen tint (§9).
* Inn status masks also clear bit 0x20 and the upper byte of the condition word (§9).
* System menu 終了する quits without confirmation (§7).
* The menu cannot be opened by Esc while the leader is between tiles (§1.1).

## Open questions

* Whether the original's standard dialogs start in the game folder in every launch configuration (it
  uses the process's current folder).
* The exact wording of the operating system's overwrite confirmation is system text and not part of
  the runtime; an implementation may use its own wording.
* Behaviour when an item count would exceed the item maximum through equipment changes (no check
  exists; counts can exceed the maximum by one).
* Using an item number outside 0..99 from a stale action record (§2.2 quirk): the original reads
  outside its tables; recommended: nothing happens and the menu is closed.
* A shop or inn message string with fewer than three line-feed-terminated lines (§8): undefined in
  the original; recommended: missing lines are empty.
