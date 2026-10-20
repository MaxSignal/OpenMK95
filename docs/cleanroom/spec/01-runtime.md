# 01 — Runtime: program structure, main loop, input, waiting, random numbers, start-up

This chapter describes how the runtime is put together: how it finds and loads a game, how its single
thread of execution is organised into blocking loops, how input is received and interpreted, the basic
wait primitive, the random-number generator, the map main loop pass by pass, and the start-up, title,
new game, continue, game over and quit sequences. Terms such as screen state, window slots, Wait(n),
Message(...), transitions table, audio slots and battler record are defined in `00-README.md` §5.

---------------------------------------------------------------------------------------------------

## 1. Global run-time state used in this chapter

The following pieces of state are referred to throughout this chapter (and by the other chapters). The
names are descriptive; implementers choose their own. The first six flags of the table below are the
"shared run-time flags" of `00-README.md` §5; all chapters use exactly these names, and this section is
their definition.

| name | values | meaning |
|---|---|---|
| quit flag | 0/1 | set when the program must end (system menu, EndGame command, window close confirmed, Ctrl+Break). Every blocking loop checks it and returns. |
| pass-abort flag | 0/1 | "abandon the rest of the current main-loop pass" (§8: the pass goes on with its POST step). Cleared at the start of every pass (§8 step 2). Does **not** stop a running event script (only the quit flag does). The complete list of what sets it follows this table. |
| arrival flag | 0/1 | "the map was just entered": while set, hero input (action button, walking, hero route steps, §8 step 7) is skipped in **every** pass, and no vehicle or map event is updated in the tick (§8 step 11.2); the hero's own sub-step advance in the tick still happens. Set at start-up (§9.1) and by every teleport (`04-map-movement.md` §11.1); cleared after the autorun step of the next tick (§8 step 11.5). |
| post-pass action | none / teleported / boarding done / alighting done | work to finish at the end of the pass (§8, step 12). Reset to none at the start and end of every pass. "Teleported" is set by event command 5 and has no effect at step 12. |
| battle-lost flag | 0/1 | the last battle ended in defeat: set by the battle's turn loop on defeat and by the battle-end sequence when the party is defeated (`08-battle.md` §3.1, §7); consumed by the game-over check (§10). |
| confirm flag | 0/1 | "the player confirmed": ends Wait(n) (§5.1). Everything that sets it is listed in §5.1. |
| release latch | 0/1 | "all keys have been released since the last confirm" (§4.3). Initially 1. |
| last key | key code or −1 | the last key pressed while the map has control (§4.5). |
| menu-request flag | 0/1 | the field menu must be opened (or is open). |
| event-running flag | 0/1 | an event script is executing, or the title screen or the version dialog is up (UI lock). |
| in-battle flag | 0/1 | a battle is running. |
| display-switch flag | 0/1 | a screen-mode switch is in progress (§12). |
| page-refresh request | 0/1 | event pages must be re-evaluated at the end of the pass (`05-events.md`). |
| last tick time | ms | real time of the last logic tick; initially 0. |
| tick length | 20 ms | constant. |
| movement-control mode | 0..3 | set by event commands (`04-map-movement.md` §9.7, Movement-control modes): ≥ 2 means the player may not move or use the action button. |

**What sets the pass-abort flag** — exactly these, and nothing else:

1. Event command 5 (teleport) (`05-events.md`); it also sets the post-pass action "teleported".
2. A successful escape procedure (`04-map-movement.md` §1.4) outside a battle, i.e. a teleport or escape
   spell or an item that casts one, used from the field menu (`07-menus-shop-inn.md`). It is set before
   the teleport is made.
3. The start of a successful alighting (`04-map-movement.md` §12.4): by the action button, by a double
   click or right+left click (§4.9), or by event command 48. Boarding never sets it.
4. Loading a game from the system menu (`07-menus-shop-inn.md`, System menu; `10-save-files.md` §2.4).
   Loading from the title does not set it (the main loop has not started yet).
5. The system-menu item `終了する` (§11).
6. Event command 30 EndGame (§11, `05-events.md`).
7. Ctrl+Break (§4.2 step 3).
8. The end of a lost battle (`08-battle.md` §7): when the battle-lost flag is set, the battle-end sequence
   sets the pass-abort flag and returns without bringing the map back.

**(quirk)** An escape or teleport effect used **inside** a battle does not set the flag: it only records
the destination, and the battle-end sequence then teleports (`08-battle.md` §7) without setting the
pass-abort flag. The pass that started the battle therefore continues after the battle returns. If that
battle was started by an event that the pass was running from its loop over the map events (§8 steps
11.2 and 11.4), the original then continues that loop with the event list of the map that was just left,
whose events have been deleted (undefined behaviour). An implementation should end the loop over the map
events in that case as if the flag had been set (**UNCERTAIN**: what the original visibly does; it may
crash).

---------------------------------------------------------------------------------------------------

## 2. Program start

### 2.1 Locating the game

1. The **game folder** is the folder that contains the runtime program file itself (not the current
   working directory). The original sets the working directory to this folder. An implementation
   normally receives the game folder as a parameter (or lets the user choose it); it must then treat
   that folder exactly as the original treats its own folder: every data file, image, sound and movie is
   looked up there (file names case-insensitive).
2. The runtime enumerates all files matching `*.RPG` in the game folder (case-insensitive). If there are
   several, the **last one in the folder's enumeration order** is used. **UNCERTAIN**: the enumeration
   order is that of the file system (directory-entry order on FAT); all 31 sample games contain exactly
   one `.RPG` file. An implementation should use the last match in the order the operating system
   lists the folder, and may warn.
   File names of any length work (sample names have up to 28 bytes).
3. The program window's caption is the **name of the game folder** (the last path component of the
   folder, not the `.RPG` name). The same text is the caption of the quit confirmation (§11).

### 2.2 Refusal conditions

* If no `.RPG` file exists, **or if a file named `suicide.exe` exists in the game folder**, the runtime
  loads nothing further except `strings.dat` (§2.4 step 5) and, when its window first appears, shows a
  message box with the text
  `このプログラムは単独では動作しません。` ("this program cannot run on its own"), caption
  `RPGﾂｸｰﾙ95 - Game.exe` (half-width katakana as written), an OK button and no icon, and then ends.
  (The `suicide.exe` rule is a deliberate kill-switch of the original; it must be reproduced.)
* If `EPARTY.DAT` cannot be opened, a message box `ﾌｧｲﾙ[EPARTY.DAT]が見つかりません` (caption
  `RPGﾂｸｰﾙ95`, error icon, OK) is shown and the program ends. The same for `MONSTPIC.DAT`
  (`ﾌｧｲﾙ[MONSTPIC.DAT]が見つかりません`). Both checks happen in the load order of §2.4.
* If the display cannot be initialised: message box `DirectDrawの作成に失敗しました。` + line break +
  `ｹﾞｰﾑの実行を中止します` (caption `RPGﾂｸｰﾙ95`, OK, no icon), then the program ends. (An
  implementation shows this text only if it cannot create its output window/surface.)
* If the `.RPG` file exists but cannot be opened: see `02-data-formats.md` §3.9 (the original shows a
  message and then fails; an implementation should refuse to start).
* Missing `CHARA.DAT`, `ITEM.DAT`, `MAGIC.DAT`, `strings.dat` are **not** errors (see §2.4).

All texts are Shift_JIS in the original; they are given here in UTF-8.

### 2.3 Full screen or window

* At program start the runtime checks whether a **Shift key is held**. If not, it runs **full screen**
  (the display is switched to 640 × 480 with 256 colours); if Shift is held it runs in a **window** whose
  client area is 640 × 480, centred on the desktop.
* An implementation on a modern system may default to a window (scaled), but must offer both modes and
  the run-time toggle of §12. All coordinates in all chapters are relative to the 640 × 480 game area.
* Windowed mode on a desktop that is not in a 256-colour mode: the original **disables transparent
  message windows** (the game-file flag is ignored; windows are drawn opaque) for as long as it runs in
  a window; switching to full screen restores the flag from the game file. An implementation emulates an
  8-bit display in both modes and should therefore keep the game-file setting in both modes (behave as
  the original on an 8-bit desktop). **UNCERTAIN** whether any game depends on the opaque fallback.
* The original switches off the operating system's input-method editor for its window, so key presses
  always arrive as plain keys (no Japanese text input is ever needed).

### 2.4 What is loaded at start, in order

1. Initial state (before any file is read): all 1024 switches off; party-member mask empty; teleport
   destination list empty (100 entries 0), teleport-forbidden flag 0, escape destination 0; vehicle
   table empty (10 entries); all 13 window slots empty; event-running flag 1; menu-request flag 0;
   in-battle flag 0; release latch 1; quit flag 0; current map −1; page-refresh request 0; running event
   id −1; encounter area −1.
2. Locate the game (§2.1) and check the refusal conditions (§2.2 first item).
3. **The .RPG file** (`02-data-formats.md`, The .RPG game file): header and version, audio slots,
   transitions table, currency name, item maximum, flags (message speed, transparent windows, saving
   enabled), the 8 actors, the start item counts, start money, start map/x/y, and all map records. The
   audio slots and transitions table become the current ones.
4. If windowed and the desktop is not 256-colour: transparent windows off (§2.3).
5. **strings.dat** (150 lines, built-in defaults when missing; `02-data-formats.md`, strings.dat). This
   happens even when step 2 failed.
6. (Audio mixer volume control is opened and CD audio presence detected — `09-audio-movies.md`.)
7. **EPARTY.DAT** (troops, 32000 bytes). Missing → error and end (§2.2).
8. **MONSTPIC.DAT**: the file is scanned record by record to the end (offsets of up to 100 pictures).
   Missing → error and end (§2.2).
9. Joystick detection (§4.7); if a joystick is present, its 100 ms poll timer is started.
10. **CHARA.DAT** (100 monster battler records of 0xB8 bytes). Missing → silently ignored (the records
    stay empty, **UNCERTAIN**: their content is then undefined in the original; use all zero).
11. **ITEM.DAT** (100 × 28 bytes), then **MAGIC.DAT** (100 × 28 bytes). If ITEM.DAT is missing,
    **MAGIC.DAT is not read either** **(quirk)**; missing files leave the tables empty (all zero).
12. Battle animation tables are built and the spell animation numbers are derived from MAGIC.DAT
    (`08-battle.md` §5.3 Animation numbers, §5.4 Animation number of each spell).
13. **Party initialisation** (§2.5).
14. Font set-up and font metrics (§2.6).
15. When the window is first painted, the main routine starts (§9.1), which creates the display, loads
    `CHARA.BMP` and the four chipset images of the start map (`03-display.md`, `04-map-movement.md`),
    the tile attribute files, the sound output, and then shows the logos and the title.

### 2.5 Party initialisation at program start

This happens **once per program run** (there is only one game per run: the program never returns to the
title). Loading a save file later overwrites all of it (`10-save-files.md`).

Let A[a] be actor a (a = 0..7) of the game file (`02-data-formats.md`, Actor records), with fields
graphic, start-member flag, name, start level L, HP, MP, vitality, power, intelligence, agility, luck,
attack, defence, equipment[5], magic learn levels[100], magic effect rates[100].

1. n = number of actors whose start-member flag is non-zero.
2. The 8 battler records (`02-data-formats.md`, Battler record) are filled: first the start members in
   actor order (records 0..n−1), then the other actors in actor order (records n..7). For actor a placed
   in record r:
   * actor id (+0xaf) = a; level (+0x00) = L;
   * HP (+0x02) = max HP (+0x06) = A[a].HP; MP (+0x0a) = max MP (+0x0c) = A[a].MP;
   * vitality (+0x0e), power (+0x10), agility (+0x12), intelligence (+0x14), luck (+0x16),
     defence (+0x18), attack (+0x1a) = the actor's stored start values (copied, not recomputed);
   * sleep counter (+0x1c) = 0; status bits (+0x28) = 0;
   * experience (+0x1e) = (L − 1) × (L − 1) × 10;
   * name (+0x2c) = the actor's name; magic effect rates (+0x41, 100 bytes) = the actor's;
   * the two trailing 32-bit fields (+0xb0, +0xb4) = 0;
   * magic defence (+0x04) = power of item `equipment[4]` ("other" slot) of ITEM.DAT, or 0 if that
     entry is −1. (**UNCERTAIN**: values other than −1 and 0..99 are not guarded in the original.)
   * Per actor (not per record): the 5 equipment entries = the actor's equipment bytes (signed, −1 =
     none); the 100 magic learn levels = the actor's.
3. Number of fighting members = min(n, 4).
4. The 100 item counts = the start item counts of the game file; money = start money.
5. (In the main routine, §9.1:) the 8 party map characters (one per actor index) get the actor's
   graphic; each start member's map character is placed at the start x/y of the game file; party slot i
   (i = 0..n−1) = the i-th start member in actor order; party size = n; the party-member mask has bit a
   set for every start member. The leader is party slot 0.

A game with **no start member** (n = 0) leaves the leader undefined in the original (it would crash);
an implementation should use actor 0's map character as the leader in that case. Note that the
game-over check (§10) treats an empty party as defeated, so event 99999 then runs on every pass
**(quirk)**.

### 2.6 Font and UI metrics

One font is used for every window (title menu, messages, menus, battle). The original requests it with
these parameters: face `ＭＳ Ｐゴシック` (MS PGothic, full-width letters in the name), **character height
16 pixels** (the requested height is the em height of the glyphs without internal leading, not the cell
height; see `06-windows-text.md` §2), width 0 (default), **bold** (weight 700), not italic, underlined
or struck out, default character set, proof quality, proportional (variable) pitch. Glyphs are drawn
as 1-bit glyphs without anti-aliasing. Text drawing itself is specified in `06-windows-text.md`.

Two metrics are used for all window geometry. The original does not contain them as constants: after
creating the font it asks the system for the font's text metrics and sets

* **LineH** (line height of window rows) = the font's cell height (ascent + descent) + 2;
* **W** (used for menu widths `(len + 1) × W`) = the font's **maximum character width** as reported by
  the system.

Reference values, to be used by an implementation for all layout computations whatever font it renders
with:

| metric | reference value |
|---|---|
| cell height (ascent + descent) | 16 |
| LineH | **18** |
| W | **17** |
| ascent (baseline offset from the top of a text line) | 14 (used only by text drawing, `06-windows-text.md`) |

**UNCERTAIN**: these numbers were not measured with MS PGothic on a Japanese Windows 95 system. They are
the values expected for MS PGothic at 16 pixels with synthetic bold (cell height 16 with no internal
leading, ascent 14, widest glyph 16 plus 1 pixel for the emboldening). The main alternative is **W = 16**
(if the system does not count the bold pixel in the maximum width); LineH = 18 is considered reliable.
(A test with a metric-compatible substitute font gave LineH 19 and W 24; those values are not
authentic.) Because every window width and height depends on LineH and W, an implementation should keep
both in one place so they can be changed.

Examples: a 2-row menu is LineH × 2 + 6 = 42 pixels high; the title menu is 160 × 42 (§9.3).

---------------------------------------------------------------------------------------------------

## 3. Execution model

### 3.1 One thread, blocking loops

The runtime is single-threaded. Its control flow is a set of nested **blocking loops**; every loop that
waits for something keeps the program responsive by repeatedly running a **message-processing pass**
(§3.2) and checking the quit flag:

* the **main map loop** (§8), entered after the title and left only when the program ends;
* the **event runner**: an event script runs to completion (or suspension) inside one call
  (`05-events.md`); its commands block in their own loops (messages, choices, Wait, battle, shop, inn,
  movie, transitions);
* **Wait(n)** (§5.1) and every selection loop built on it (choices, menus, title menu);
* the **field menu** loop (`07-menus-shop-inn.md`), which runs while the menu-request flag is set;
* the **battle** loop (`08-battle.md`);
* **file dialogs** for saving/loading and **message boxes** (modal; nothing else runs meanwhile);
* **busy delays** (§5.3), which do *not* process messages.

Consequences that must be reproduced:

* While any loop other than the main map loop runs, **the map is frozen**: no logic tick happens, no
  character moves or animates, no autorun or touch event can start. (Commands that animate the map
  themselves are described in `05-events.md`.)
* No catch-up: after a long block, the next main-loop pass runs **one** tick immediately (more than
  20 ms have passed) and then continues at the normal rate.
* Only one event script executes at a time; no event can start from inside another (`05-events.md`).
* Input that arrives during a busy delay is queued and processed afterwards; polled key states (§4.4)
  always reflect the real current state.

### 3.2 The message-processing pass

The original keeps one queue of pending messages. A **message-processing pass** repeatedly takes the
next pending message and handles it, until none is left. The order in which pending messages are taken
is:

1. **internal notifications** posted by the runtime itself (window selection made, cancel, window
   activate/deactivate, list page changed, end of list, close movie window; see `06-windows-text.md`),
   oldest first;
2. **input events** (key press, key release, mouse button, mouse move, double click), in the order they
   occurred;
3. lowest priority: repaint requests (§3.4) and the joystick timer (§4.7) — taken only when nothing
   else is pending; at most one of each is pending at a time.

After every handled message the queue is examined again from the top, so notifications posted while
handling an input event are processed before the next input event.

**(quirk)** The key-press handler (§4.2) itself starts with a complete message-processing pass before it
acts on its own key. If several key presses are queued, the later ones are therefore handled (nested)
before the earlier one completes; the effect is normally invisible because the queue is drained very
frequently.

The handling of the window-close request is described in §11.

### 3.3 Program end

When the quit flag becomes set, every loop returns at its next check; the main map loop then ends, the
screen is blacked out (hide without transition), and the program terminates (audio stops). There is no
return to the title and no "game over" screen.

### 3.4 Repaint requests

When the operating system asks for (part of) the game area to be repainted (window uncovered, mode
switch), the runtime copies the frame buffer to the screen only if the screen state is BEFORE_START,
PICTURE_BEFORE_MAP, MAP, PICTURE_OVER_MAP or PICTURE_ON_BLACK (that is, not ERASED and not MOVIE) and no
movie is playing, or while a logo/title picture is shown; otherwise nothing is drawn. Nothing is drawn
while a screen-mode switch is in progress, and in full-screen mode nothing is drawn while a save or load
file dialog is open. Details: `03-display.md` §5.5.

---------------------------------------------------------------------------------------------------

## 4. Input model

### 4.1 Keys used

| key | role |
|---|---|
| Up / Down / Left / Right arrows, numeric keypad 8 / 2 / 4 / 6 | walking (polled, §4.4); cursor movement in windows; list pages (Left/Right) |
| Enter, Space | confirm (§4.2, §4.3); action button on the map (§4.5); select in windows |
| Escape | cancel in windows; opens the field menu on the map; also confirms in some states (§4.2) |
| Shift | held at program start → windowed mode (§2.3); held during a battle → fast-forwards every Wait (§5.1) |
| F4 | toggle full screen / window (§12) |
| F3 | debug key: realises the committed palette (`03-display.md` §2.3), then is handled like an ordinary key |
| Ctrl+Break | immediate quit, no confirmation (§11) |

Enter on the numeric keypad counts as Enter. No other keys have a function. An implementation may add
Alt+Enter as an alias for F4 (the original has no such key). The keyboard is never used for text entry.

### 4.2 Key-press handling (exact)

On every key press (including the operating system's auto-repeat presses, §4.6), in this order:

1. Run a message-processing pass (§3.2, quirk).
2. F3: realise the committed palette (`03-display.md` §2.3); continue with step 5 as for any other key.
3. Ctrl+Break: set the quit flag and the pass-abort flag; done.
4. F4: screen-mode switch (§12); done.
5. If the key is Enter or Space and the release latch is set: set the confirm flag.
6. If the leader's map character is in mouse auto-walk (§4.9): end auto-walk (its move type goes back
   to player control).
7. **Map state** — if the menu-request flag, the event-running flag and the in-battle flag are all 0 and
   the key is not Escape: if the key is neither Enter nor Space, or the last key is −1, store the key as
   the last key (so Enter/Space never overwrite an already stored key, other keys do). Done — note that
   the release latch is **not** cleared in this state **(quirk)**.
8. **Otherwise** (menu, event, title, battle or version dialog active, or the key is Escape):
   1. If the key is Escape, Enter or Space and the release latch is set: set the confirm flag.
      (Escape therefore also ends waits, e.g. a timed message or Wait(n), while an event runs.)
   2. If the key is Escape and window slot 2 is empty (no main menu / battle command window):
      * if in battle and auto-battle is on: auto-battle is switched off (`08-battle.md`, Auto battle);
      * if the menu-request flag, in-battle flag and event-running flag are all 0 and the leader's
        sub-step counter is 0: set the menu-request flag (the field menu opens in the next main-loop
        pass);
      * otherwise: clear the menu-request flag (and close window slot 2, which is empty).
   3. Clear the release latch.
   4. If the **focused window slot** (the slot of the window most recently created or activated,
      `06-windows-text.md`) is empty: if the key is not Enter/Space, no event is running, a message window
      (slot 0) exists and no battle runs, close the message window; clear the release latch. Otherwise
      (a focused window exists): **queue** an "activate" notification for that window, then pass the key
      to its key handler immediately (`06-windows-text.md` §8.2, Keys inside a window). The activation
      takes effect only at the next message-processing pass, after the key, so a key given to an
      inactive window is ignored but re-activates it **(quirk, `06-windows-text.md` §8.3)**.

### 4.3 Key release and the release latch

* Any key release sets the release latch to 1.
* Any mouse movement over the game area sets the release latch to 1 **(quirk)**.
* Every poll of a direction key that finds the key **not** held sets the release latch to 1 (§4.4)
  **(quirk)**.
* The joystick timer sets it to 1 when the joystick is idle (§4.7) **(quirk)**.
* It is cleared by the key-press handler in the non-map state (§4.2 step 8.3) and by every joystick
  timer event in which an axis is deflected or a button is held (§4.7).

"Confirm only counts after the key was released": a held Enter/Space produces auto-repeat presses, but
the confirm flag is only set by a press while the latch is 1. Because of the extra ways the latch is set,
a held confirm key can be re-accepted after any of them (e.g. holding Enter on the map while the walking
code polls the arrow keys re-arms the latch every pass, so the next auto-repeat press of Enter counts
again). This must be reproduced as described; it is why holding Enter while talking to a character may
skip text and re-trigger the conversation.

### 4.4 Polled keys

Walking, the battle fast-forward (Shift) and some waits read the **current held state** of keys at the
moment they need it (not the event queue). Polling a direction key that is not held sets the release
latch (§4.3). The joystick state is read directly at the same moments (§4.7).

### 4.5 Last-key memory and the action button

* The last key (§4.2 step 7) is set only while the map has control. It is reset to −1 after the
  hero-control step of every main-loop pass and again at the end of every pass (§8). A key press is
  therefore only seen by the pass that processed it: pressing Enter while the hero is in the middle of a
  step does nothing (the press is forgotten).
* The action button fires in a main-loop pass (§8 step 7) when the hero is idle, the arrival flag is
  clear, the movement-control mode is ≤ 1, the hero has no move route, the release latch is 1 and
  (the last key is Space or Enter, or the joystick OK button is held). What it does is described in
  `04-map-movement.md` (Action button, Alighting).

### 4.6 Key repeat

The runtime has no repeat timer of its own:

* Walking continues for as long as a direction key is held (polled); a short tap still produces one full
  step.
* In windows, each key-press event moves the cursor once; holding a key moves it at the operating
  system's auto-repeat rate. An implementation should generate repeated key presses like the target
  system (initial delay about 500 ms, then about 30 presses per second — **UNCERTAIN**, user settings
  apply).
* Selections never auto-repeat: after a selection the runtime waits for the release (§5.2).

### 4.7 Joystick (as in the original)

* Detection at start: a joystick is present if the system reports at least one joystick device and
  joystick 1 is connected. Only joystick 1 is used. If present, a timer delivers a joystick event every
  **100 ms** (lowest priority, §3.2).
* Reading joystick 1 gives axes x, y in 0..65535 and buttons 1..4:
  left = x < 10000, right = x > 50000, up = y < 10000, down = y > 50000;
  **OK** = button 2 or button 4; **cancel** = button 1 or button 3.
* **Polled use**: walking treats "up" etc. like the corresponding held arrow key (same priority order
  as the keys, §8 step 7.2); the action-button check accepts a held OK button instead of the last key
  (§4.5). These read the joystick at the moment of the check.
* **Joystick timer event** (ignored while the display-switch flag is set):
  1. Read the joystick. Determine a synthetic key: the first true of up → Up, down → Down, left → Left,
     right → Right (note: up/down take priority over left/right here, unlike walking);
     otherwise, if OK is held: when the release latch is 1 the synthetic key is Space, the confirm flag
     is set and the version dialog is closed if open (`07-menus-shop-inn.md`); otherwise, if cancel is
     held and the release latch is 1: the synthetic key is Escape, the version dialog is closed if open,
     and if window slot 2 is empty the Escape menu logic of §4.2 step 8.2 is applied.
  2. Run a message-processing pass.
  3. If no axis is deflected and no button is held: set the release latch to 1. Otherwise: if a
     focused window exists and (the release latch is 1, or the key is not Enter, Space or Escape),
     queue an "activate" notification for it and pass the key to its key handler immediately, as in
     §4.2 step 8.4; then clear the release latch. The key is the synthetic key of step 1. When only
     buttons were held while the latch was 0, step 1 produced no synthetic key and the value used
     instead is the button state as a bit set (OK = 16, cancel = 32, both = 48): OK alone is passed as
     key code 16 (the Shift key), which windows ignore; cancel alone equals the code of Space and is
     therefore not passed (the latch is 0).
* Consequences: a held direction moves a window cursor every 100 ms; buttons act once per press.
* Before and after running the field menu, the main loop waits (processing messages) until the next
  joystick timer event has passed if the last joystick event produced Escape (§8 step 5).

### 4.8 Game controllers (recommended for implementations)

Modern controllers are not part of the original. Recommended mapping, so that controller play behaves
like the original joystick plus keyboard: D-pad (and left stick) → the four directions both for polling
and as key presses; south button ("A") → Enter; east button ("B") → Escape; west button ("X") → Shift
(battle fast-forward). Controller buttons go through the same key-press/release handling as keys (§4.2,
§4.3).

### 4.9 Mouse

Mouse coordinates are those of the 640 × 480 game area. Clicks and movement **on an active window** are
handled by that window (`06-windows-text.md` §8.7, Mouse in windows: hovering moves the cursor, a left
click selects, a right click cancels). This section covers the game area outside windows. A window that
has been deactivated does not take mouse input at all: movement and clicks over it are handled as
described here, exactly as if the pointer were over the game area outside all windows
(`06-windows-text.md` §8.1, §8.7) **(quirk)**.

State: the **drag flag** (left button held after a press on the game area), the **remembered point**,
the **auto-walk direction** (0..3, or 5 = none; initially 5) and the **same-direction counter**.

**Left button press**:
1. Set the confirm flag (always, without regard to the release latch) **(quirk)**: a click anywhere on
   the game area ends any Wait(n).
2. If not in battle:
   1. If the menu-request flag is set: if the sub-menu mode is 2 (equipment, `07-menus-shop-inn.md`
      §1.3) and a message window (slot 0) exists, close the message window and queue an "activate"
      notification for window slot 11; otherwise close all windows and
      the menu (a click outside the menu cancels the whole menu).
   2. If the leader is in auto-walk, end auto-walk (move type back to player control).
   3. If the drag flag is clear: remember the point, set the drag flag (the mouse is captured: movement
      and release are received even outside the game area). Otherwise: same-direction counter = 0.

**Mouse movement** (any): set the release latch. Then, only if no event runs, no battle runs, the leader
character exists, its move type is player control or auto-walk, it has no move route, the drag flag is
set and the movement-control mode is < 2:
1. dx, dy = current point − remembered point. If |dx| ≤ 1 and |dy| ≤ 1: nothing (the remembered point is
   kept).
2. Direction d = if |dy| < |dx|: (dx < 1 ? left (3) : right (1)); else (dy < 1 ? up (0) : down (2)).
3. If the leader is idle: start a motion in direction d (`04-map-movement.md`, Starting a motion).
4. If d equals the auto-walk direction: increment the same-direction counter (if its value before the
   increment was greater than 10000 it becomes 10000). Otherwise: the auto-walk direction = d, and if the
   leader is under player control (not already auto-walking) the counter = 0.
5. If the counter > 20: the leader's move type becomes **auto-walk**.
6. Remember the current point.

So dragging the mouse walks the hero one step per idle moment; dragging steadily in one direction for
more than 20 movement events switches to auto-walk, which continues after the mouse stops.

**Auto-walk** (main loop, §8 step 7.2, leader idle): start a motion in the auto-walk direction; if the
leader's motion is then not that direction (blocked and turned instead, or map edge), auto-walk ends:
auto-walk direction = 5, move type back to player control. Auto-walk also ends (move type back to player
control; the auto-walk direction is **not** changed) on any key press and on any left or right button
press on the game area.
A change of drag direction while auto-walking changes the auto-walk direction without resetting the
counter.

**Teleport and auto-walk (quirk)**: every teleport (`04-map-movement.md` §11.1) sets the auto-walk
direction to 5 and clears the drag flag, but does **not** end auto-walk. A hero that was auto-walking
when teleported therefore keeps starting motion 5 (a turn to the right, `04-map-movement.md` §7.1); as
that motion equals the auto-walk direction, auto-walk does not end, and the hero turns to the right on
every idle pass until a key or mouse button is pressed (`04-map-movement.md` §9.2).

**Left button release**: drag flag cleared, same-direction counter = 0, mouse no longer captured.

**Double click** (the second press of a double click arrives as a double-click event instead of a
press): if no event runs, no battle runs and the leader is idle: if riding a vehicle and not alighting,
attempt to alight; otherwise the action button (`04-map-movement.md`). The movement-control mode, move
routes and the release latch are **not** checked **(quirk)**.

**Right button press**:
1. If the leader is in auto-walk, end auto-walk.
2. If in battle: switch auto-battle off (`08-battle.md`); done.
3. If an event is running: nothing.
4. If the left button is held at the same time and the leader is idle: alight if riding and not
   alighting, otherwise the action button (no movement-control, route or latch check, **quirk**); done.
5. Else if the menu-request flag is clear and the leader is under player control: set the menu-request
   flag — **without** the "sub-step counter is 0" condition of Escape **(quirk)**; done.
6. Else: post a cancel notification to the focused window slot (as Escape in that window).

The mouse wheel and middle button have no function. **UNCERTAIN**: whether the mouse pointer is visible
in full-screen mode on the original.

---------------------------------------------------------------------------------------------------

## 5. Waiting

### 5.1 Wait(n)

Wait(n) waits n tenths of a second (n × 100 ms of real time) while keeping the program responsive.

1. Clear the confirm flag. Note the start time t0 (millisecond clock). elapsed = 0.
2. Loop:
   1. If the confirm flag is set: end. If n ≠ 0 and n ≤ elapsed: end.
   2. If a Shift key is held (polled) and a battle is running: end.
   3. Run a message-processing pass (§3.2).
   4. If the quit flag is set: end.
   5. If n > 0: elapsed = floor((now − t0) / 100).

Properties:
* **n = 0** waits until confirm, Shift-in-battle or quit — no timeout.
* **n < 0** returns immediately (the timeout test 1 ≤ ... is already true) **(quirk)**.
* The confirm flag is set by: Enter/Space presses while the release latch is 1; Escape presses while the
  latch is 1 when not in the map state (§4.2); any left click on the game area (§4.9); the joystick OK
  button (§4.7); a selection in a window (`06-windows-text.md`). So a timed wait is cut short by any of
  these.
* Selection loops call Wait(0) and afterwards read the selected index; a wait ended by something other
  than a selection leaves the index at −1 (`06-windows-text.md`).
* Movie variant (used only by the movie command, `09-audio-movies.md`): with n = 0 it waits until the
  movie window has closed; if it ends because of the confirm flag, it requests the movie window to close
  and clears the confirm flag.

The Shift fast-forward applies to every wait inside a battle (messages, pauses), not to busy delays.

### 5.2 Flush keys (wait for release)

"Flush keys" has two forms; both are used at fixed points of other algorithms.

* **Flush keys (no wait)**: busy-wait (without processing messages) until Escape is not held. (It also
  discards the "pressed since last check" state of the arrows, Space and Enter, which has no visible
  effect.)
* **Flush keys (wait)**: as above, then run message-processing passes until the release latch is 1, then
  **discard all pending keyboard events** (presses and releases that are still queued).
  Implementation hint: also treat "no relevant key (arrows, Enter, Space, Escape) is held any more" as
  released, to avoid hanging when a release event was lost.

Uses: after every action-button or bump-touch event and after every selection (wait form); after every
completed hero step, after every teleport, before and after the field menu, at the start of the main
loop (no-wait form). A consequence of the no-wait form: holding Escape while walking freezes the game at
the end of the step until Escape is released **(quirk)**.

### 5.3 Busy delays

Fixed short delays (palette flash steps, screen shake, transitions, see `03-display.md`) are **busy
waits on the millisecond clock without message processing**: the program does not react to input during
them (input is queued). An implementation must keep its window alive with the operating system during
such delays but must not handle game input in them.

---------------------------------------------------------------------------------------------------

## 6. Random numbers

There is one global pseudo-random generator (a linear congruential generator of the original's C
library):

* **State**: one unsigned 64-bit integer S. Initial value **S = 1** at program start. It is never
  re-seeded, never written to or restored from save files, and continues across loading a game.
* **Step**: S ← (S × 0x000015A400004E35 + 1) mod 2^64.
  (The multiplier has high half 0x000015A4 and low half 0x00004E35. A value of 0x015A4E35 found in some
  notes is wrong.)
* **Output**: R = (S shifted right by 32) AND 0x7FFFFFFF, i.e. bits 32..62 of the new state, a number
  0..2^31 − 1.
* The first ten outputs after program start are: 5540, 221838220, 695693412, 1410949376, 822182988,
  1990930872, 236698092, 2108515212, 1428137313, 1164941360.

Usage rules (every chapter refers to these):

* "random(n)" means R % n with C semantics. R is non-negative, so for negative n the result is
  R mod |n| (0..|n| − 1).
* **Where the modulus can be 0, no number is drawn** and the result is 0. (The original guards every
  such place; an implementation must not consume a number in that case.)
  Example: the troop choice of event command 4 with first troop a and last troop b uses the modulus
  n = b − a + 1; for b = a no number is drawn (that command handles this case first), for n = 0
  (b = a − 1) no number is drawn and the offset is 0, for n < 0 (b < a − 1) one number is drawn and
  the offset is R mod |n| (`05-events.md` code 4, `08-battle.md` §1.1).
* "R AND 3", "R AND 1" etc. are also used (equal to % 4, % 2).
* Each chapter states exactly where numbers are drawn and in which order (battle formulas, encounter
  checks, random movement of events and the hero, random animation directions in every drawn frame, inn,
  event commands). Draws happen in map ticks as well, so the sequence depends on how many ticks elapsed;
  the order within a tick (§8) must still be exact.

---------------------------------------------------------------------------------------------------

## 7. Ticks and timing

* The map main loop runs as fast as possible (the original never sleeps; an implementation may yield
  for at most about 1 ms per pass). Input handling and hero-route stepping happen on **every pass**.
* A **logic tick** runs in a pass only when **now − last tick time > 20 ms** (millisecond clock); the
  tick then sets last tick time = now. With a 1 ms clock this gives a tick every **21 ms (≈ 47.6 ticks
  per second)**; this is the rate that was confirmed by side-by-side comparison and must be used.
  Character speed s means one sub-step (8 px) every s ticks: at the default speed 2 a sub-step takes
  42 ms and a tile 168 ms.
* The redraw at the end of each tick waits for the display's vertical retrace before presenting
  (`03-display.md`); with fast rendering this does not change the tick rate.
* **UNCERTAIN**: on a real Windows 95 system the coarse granularity of the clock used by the original
  for this test may have produced longer ticks (e.g. 30–55 ms). The 21 ms value is the reference.
* Timed waits (§5.1) use real time in tenths of a second and are independent of ticks.

---------------------------------------------------------------------------------------------------

## 8. The main map loop — one pass

The main map loop is entered after the title (§9). Before the first pass: flush keys (no wait), scroll
direction = −1 (it persists across passes, see step 11.1), last key = −1. Each pass performs the following steps
in exactly this order. "→ POST" means: skip directly to step 12; "→ END" means skip to step 13.

The **hero** in this section is the map character of party slot 0 (the ridden vehicle while riding). It
is fetched once at step 2 and that same character is used for the whole pass.

1. If the quit flag is set: leave the loop (program end, §3.3).
2. Clear the pass-abort flag; post-pass action = none; fetch the hero.
3. Run a message-processing pass (key presses update the last key, Escape may set the menu-request
   flag, mouse dragging may start hero motions, …).
4. If the quit flag is set: leave the loop. If the pass-abort flag is set: → POST.
5. **Menu**: if the menu-request flag is set: wait while the last joystick event was an Escape (§4.7);
   flush keys (no wait); run the field menu (`07-menus-shop-inn.md`; it blocks until the menu is
   closed); flush keys (no wait); wait for the joystick again; clear the menu-request flag. If the quit
   flag is set: leave the loop. If the pass-abort flag is set (e.g. a game was loaded): → POST.
6. **Busy check**: if the event-running flag, the in-battle flag or the display-switch flag is set,
   restart at step 1 (nothing else happens in this pass). (In practice this happens while the version
   dialog is open.)
7. **Hero control** — only if the hero's motion is idle and the arrival flag is clear:
   1. **Action button**: if the movement-control mode is ≤ 1, the hero has no move route, the release
      latch is 1, and (the last key is Space or Enter, or the joystick OK button is held): if riding a
      vehicle and the hero is not alighting → alighting attempt; otherwise → action button
      (`04-map-movement.md` §9.3 Action button, §12.4 Alighting). If the pass-abort flag is then set:
      → POST; otherwise continue with 7.2.
   2. If the hero has no move route, or is boarding or alighting:
      * player control and movement-control mode < 2: poll in this order Up arrow, numpad 8, joystick
        up, Left arrow, numpad 4, joystick left, Down arrow, numpad 2, joystick down, Right arrow,
        numpad 6, joystick right; stop at the first one held and start a motion in its direction
        (0 up, 3 left, 2 down, 1 right). Every key polled and found not held sets the release latch
        (§4.3). (Priority Up > Left > Down > Right; no diagonals.)
      * boarding or alighting: start a wait motion (`04-map-movement.md`, Vehicles).
      * auto-walk: the auto-walk step of §4.9.
      * random movement (move type 3): start a motion in direction R AND 3 (one random number).
      * any other move type: nothing.
   3. Otherwise (the hero follows a move route): perform one route step for the hero
      (`05-events.md`, Move routes). If the hero is not boarding/alighting, copy the hero's display mode
      and animation type to all 8 party map characters. If the route ended: if the screen state is MAP,
      close all windows (and the menu) and redraw the frame (presenting it); if an event is waiting for
      the hero, resume it (`05-events.md` §6.5, Suspension and resumption); if the pass-abort flag is
      then set: → POST.
8. Last key = −1.
9. **Tick throttle**: if now − last tick time ≤ 20 ms: → END. Otherwise last tick time = now.
10. If the display surfaces were lost (full-screen program switched away), restore them, reload the map
    graphics and redraw the whole map (`03-display.md`).
11. **The tick**:
    1. **Hero and followers**: increment the hero's speed counter. If the counter is now **≥** the
       hero's speed (otherwise nothing more happens in this step; continue with 11.2):
       counter = 0, advance the hero's motion by one sub-step (`04-map-movement.md` §7.4, Advancing a
       motion).
       * If that reported a contact with an event: scroll direction = −1; bump touch
         (`04-map-movement.md` §9.4, Touch triggers); if the pass-abort flag is set → POST; otherwise
         skip the followers and continue with 11.2.
       * Else, if the hero's sub-step counter is now 1 (a step or wait has just begun): scroll direction
         = the hero's motion if it is a step (0..3), else −1; push the hero's motion into the follower
         trail and start the followers' motions (`04-map-movement.md` §9.6, Followers). If the hero is
         boarding and the motion of the last shown party character (party position shown − 1) is a
         wait (17): post-pass action = boarding done → POST. If the hero is alighting and that
         character's motion is a wait: post-pass action = alighting done → POST.
       * Followers i = 1 .. (shown party characters − 1): advance follower i's motion by one sub-step;
         while boarding, if party character i − 1 is invisible, follower i takes the hero's facing.
    2. **Vehicles and events** — only if the arrival flag is clear: update every vehicle from vehicle
       slot (1 if riding, else 0) upward, stopping at the first slot that holds no vehicle object or the
       first vehicle on another map; then update every map event in list order (`04-map-movement.md`
       §8.1, `05-events.md`); after each **map event** (not after a vehicle), if the pass-abort flag is
       set → POST.
    3. **Redraw**: the hero's scroll sub-step = 0 if the scroll direction is −1, else the hero's sub-step
       counter. Clear the occupancy grid and render the frame with the scroll direction, presenting it
       (`04-map-movement.md` §10.3; `03-display.md`). Rendering rebuilds the occupancy grid and the
       "on screen" marks of the characters on the map rows it visits; a character on a row that is not
       visited keeps its mark from the last redraw that visited its row (`04-map-movement.md` §5.2,
       §6.6).
    4. **Autorun**: for each map event in list order: if its cooldown is 0, its current page's trigger is
       autorun and its "on screen" mark is set, run it (`05-events.md` §5.5, Autorun); after each map
       event, if the pass-abort flag is set → POST. Then the same for the vehicles (same slot order and
       stop rule as in 11.2). After each vehicle the pass-abort flag is tested too, but **(quirk)** a set
       flag only ends this vehicle loop: the pass continues with 11.5 and 11.6 (so the arrival flag set
       by a teleport made by an autorun vehicle is cleared at once, and the step completion of 11.6, with
       its step-on check, still runs, on the new map) and reaches POST normally.
    5. Clear the arrival flag.
    6. **Step completion**: if the hero's scroll sub-step is 4 (the hero completed a step this tick):
       scroll direction = −1; recompute the scroll flags (`04-map-movement.md`, Camera); flush keys (no
       wait); hero scroll sub-step = 0; **step-on** touch check (`04-map-movement.md`, Touch triggers).
       If the pass-abort flag is not set: **poison** step (`04-map-movement.md`, Poison while walking);
       if the party is down (§10): → END; if the hero's display mode is normal and the hero has no move
       route: **random encounter** step (`04-map-movement.md`, Random encounters); if the party is down:
       → END.
    7. Continue with step 12.
12. **POST**: if the quit flag is set: leave the loop. If the post-pass action is boarding done: finish
    boarding; if alighting done: finish alighting (`04-map-movement.md`, Vehicles). (After a teleport
    nothing is done here; the rest of the pass was merely skipped.)
13. **END**:
    1. **Game-over check** (§10). (Every pass that is not restarted at step 6 or left at a quit test ends
       here, also when no tick was due at step 9.)
    2. If the page-refresh request is set: clear it and re-evaluate the pages of all map events and
       vehicles ("Refresh all pages", `05-events.md` §4.3; the vehicles are included only while the current
       map's event list is not empty **(quirk)**).
    3. Post-pass action = none; last key = −1. Next pass.

Order within one tick, summarised: hero sub-step → followers → vehicles → map events (list order) →
redraw → autorun events → autorun vehicles → step completion (scroll flags, step-on, poison, encounter)
→ boarding/alighting finalisation → game over → page refresh.

---------------------------------------------------------------------------------------------------

## 9. Start-up sequence and title

### 9.1 Main routine start

When the game window first appears (and the refusal conditions did not apply), in this order:

1. Encounter step counter = 0; last tick time = 0; hero scroll sub-step = 0; **arrival flag set**.
2. Create the display (full screen or window, §2.3); on failure the error of §2.2 and end.
3. Load `CHARA.BMP` (its palette becomes the palette source) and the four chipsets of the start map;
   load the tile attribute files (`03-display.md`, `04-map-movement.md`). Create the sound output.
4. Battle-lost flag = 0. Set up the party map characters (§2.5 step 5); hero = party slot 0; number of
   party characters shown on the map = party size; no map events.
5. Blacken the screen (no transition); screen state ERASED. No event waiting for the hero; not riding;
   movement-control mode 0. Current music name = `演奏停止` (the reserved name meaning "no music",
   `09-audio-movies.md`). Saving enabled = the game-file flag.
6. Event-running flag = 1 (the UI is locked for logos and title: Escape does not open the menu, F4 is
   refused).
7. **Logos** (optional for implementations, `00-README.md` §2): the original shows two publisher logos
   centred on a black screen, each for 3.0 s (Wait(30), skippable by confirm), each followed by a fade to
   black. Whatever an implementation does here, at the end the screen must be black (screen state
   ERASED) and the palette must be the `CHARA.BMP` palette with the current tint applied
   (`03-display.md`, Palette source).
8. The title loop (§9.3).

### 9.2 The "fade to black" used here

"Fade to black" = palette effect "fade to black" (`03-display.md`, palette effect 5), then the whole
screen is filled black and the screen state becomes ERASED.

### 9.3 Title loop

The title has no music of its own (whatever the logos left playing — nothing — continues).
A flag "title shown" is false initially. Repeat:

1. **Title picture**: if `title.bmp` exists in the game folder (root only):
   * the frame buffer is cleared to black; the image's palette is loaded and made the current palette
     (`03-display.md`);
   * w′ = min(w, 640), h′ = min(h, 480);
     x = (640 − w) / 2 if w < 640 else 0; y = (480 − h) / 2 if h < 480 else 0 (integer division);
     the whole image (0, 0, w × h) is copied stretched into the rectangle (x, y, w′ × h′), so an image
     larger than the screen is **shrunk** to fit in that dimension, independently per axis (the aspect
     ratio is not kept) **(quirk; no sample title exceeds 640 × 480)**; for images that fit this is an
     ordinary copy; no transparent colour (`03-display.md` §7.8);
   * the screen is presented immediately (no transition);
   * **menu y** = y + h′ − h′ / 4 (integer division). For a 640 × 480 title: 360.
   * If "title shown" is false: **Wait(0)** — the title waits for confirm (Enter/Space, Escape, a click,
     joystick OK) without a timeout; there is no menu yet.
   * Flush keys (wait). "Title shown" = true.
   
   If `title.bmp` does not exist: menu y = 250, nothing is drawn (the screen stays black) and there is
   no wait.
   (In windowed mode the original centres the picture on the desktop and converts to window coordinates,
   which can shift it by a few pixels; implementations use the full-screen geometry above.)
2. Event-running flag = 1. Create the **title menu** in window slot 6 with the list-window helper
   (`06-windows-text.md` §6.2, The list-window helper): x centred, y = menu y, requested width 140 (rounded up to
   **160**), 2 rows, item texts `はじめから` ("from the beginning") and `続きから` ("continue"),
   text format centred with word wrap, no disabled items. Height = LineH × 2 + 6 = **42**; if
   y + 42 > 480 then y = 440 − 42 = 398; x = (640 − 160) / 2 = **240**.
3. **Wait(0)**, i.e. until a selection or another confirm.
4. Close window slot 6 and request a repaint.
5. If "title shown": present, fade to black (§9.2), release the title picture, clear the frame buffer
   to black, request a repaint, and reload the map palette with tint (`03-display.md` §2.4).
6. Event-running flag = 0.
7. If the selected index is not 1 — `はじめから`, **or anything else that ended the wait** (Escape, a
   left click outside the menu, a right click on the menu, joystick cancel: the index is then −1) —
   start a **new game** (§9.4) **(quirk)**.
8. If `続きから` was selected: open the load dialog (`10-save-files.md`, Loading). If it was cancelled
   or loading failed, repeat from step 1 (the title picture is drawn again, but without the first-time
   wait). If loading succeeded: continue (§9.5).

If the quit flag is set while the title is up (window closed and confirmed), the wait ends with index −1,
the new-game steps run (the start event executes no command because the runner stops on the quit flag)
and the program then ends.

### 9.4 New game

1. Screen state = BEFORE_START; current map = −1.
2. **Teleport** to the start map and start x/y of the game file (`04-map-movement.md`, Teleport): this
   creates the map's events and selects their pages; because the previous map is −1 the map is not
   shown yet.
3. Run **event 0** (the start event `eve00000.dat`) with no event object: always page 0 from command 0,
   page conditions not evaluated (`05-events.md`, Start event). Its commands run while the screen is
   still black (messages appear on black), unless it teleports or shows the map itself.
4. Play the music of the audio slot for the leader's vehicle type (walking music for 0;
   `09-audio-movies.md`). This replaces music started by the start event if the names differ.
5. **Show the map** (`03-display.md` §7.2, `04-map-movement.md` §11.2): if the screen state is not
   MAP: for PICTURE_ON_BLACK or PICTURE_BEFORE_MAP first fade to black and reload the map palette with
   tint (`03-display.md` §2.4); for ERASED or BEFORE_START realise the committed palette
   (`03-display.md` §2.3); then redraw all tiles and characters and show the frame with transition
   index 0 of the transitions table; screen state MAP.
6. If the quit flag is set: end the program. Flush keys (no wait); last key = −1; enter the main map
   loop (§8). The arrival flag is still set, so the first tick moves nothing but the hero's counter.

If the start event file does not exist, step 3 does nothing.

### 9.5 Continue (after loading from the title)

The load (`10-save-files.md`) restores the game state including the current map number and the party.
Then:
1. Remember the loaded current map M; set current map = −2 (forces a full map reload).
2. Teleport to M at the leader's saved position; the map is shown with transition index 0
   (previous map −2 is not −1).
3. Restart the saved music (forced restart from the beginning, with its saved parameters).
4. Realise the committed palette (`03-display.md` §2.3). The committed palette is the one read from the
   save file, so the map is shown with the saved palette, without re-applying the stored tint
   **(quirk, `10-save-files.md` §2.4)**.
5. Enter the main map loop as in §9.4 step 6 (the quit check, flush keys (no wait), last key = −1).

---------------------------------------------------------------------------------------------------

## 10. Game over

**Party down** = every party member (records 0 .. party size − 1) has HP ≤ 0 or status bit 0x02
(paralysis) set. An empty party counts as down.

At the END step of every main-loop pass (§8 step 13), and only there:

1. If the battle-lost flag is set or the party is down: run **event 99999** (`eve99999.dat`) with no
   event object; its page is chosen by the page conditions (`05-events.md`, Game-over event).
2. If the quit flag is now set (the event used EndGame): end the program.
3. If the battle-lost flag is set and the screen state is still ERASED: play the music for the leader's
   vehicle type, redraw all tiles and characters and show the screen with transition index 1 of the
   transitions table; screen state MAP.
4. Clear the battle-lost flag.

Behaviour that follows from this:
* There is no built-in game-over screen and no return to the title. Typical games show a message and use
  EndGame in event 99999, which ends the program.
* If event 99999 neither ends the game nor revives the party, it is run again on **every** pass (its
  messages repeat endlessly).
* If `eve99999.dat` does not exist, nothing happens except step 3: the game simply continues with the
  party as it is; random encounters stay suppressed while the party is down (§8 step 11.6).
* Defeat in a battle started by an event also stops that event script (`05-events.md` §8.2, code 4 Battle).

---------------------------------------------------------------------------------------------------

## 11. Quitting

Ways to end the program:

* System menu item `終了する` (`07-menus-shop-inn.md`, System menu): sets the quit flag and the
  pass-abort flag; the program ends immediately, **no confirmation**.
* Event command **EndGame** (`05-events.md`): stops the music, fades to black (§9.2), sets the quit
  flag and the pass-abort flag; the running script stops at once.
* **Ctrl+Break**: sets the quit flag and the pass-abort flag, no confirmation.
* **Closing the window** (window close button or the system's close shortcut): a message box asks
  `ゲームを終了しますか` ("quit the game?") with Yes/No buttons and a question icon; its caption is
  the game folder name (§2.1). While the box is open nothing else runs. **Yes** sets the quit flag (the
  window itself is not closed directly; the loops end and the program terminates as in §3.3). **No**
  continues the game unchanged.

On quitting, every blocking loop returns at its next check; nothing is saved.

---------------------------------------------------------------------------------------------------

## 12. Screen-mode toggle

Triggered by **F4** or the system-menu item `スクリーンモードの変更` (`07-menus-shop-inn.md`).
It is **refused (does nothing) while a battle runs, while the event-running flag is set** (any event
script, messages, choices, the title screen, the version dialog) **or while the menu-request flag is
set**. (The system-menu item works because the menu is closed before the action.)

Effect: the display-switch flag is set during the switch; the current music is remembered and stopped,
the sound output is recreated (playing sound effects stop), the display is recreated in the other mode
(full screen ↔ window, §2.3, including the transparent-window rule), the graphics are reloaded, the
remembered music is **restarted from its beginning**, the current tint is re-applied and the screen is
redrawn. No game state changes. An implementation may switch its presentation without recreating
anything, but should reproduce the music restart **(quirk)**.

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* §1 — an escape/teleport effect used inside a battle teleports at the battle end without setting the pass-abort flag.
* §2.1 — with several `.RPG` files the last one in folder order is used.
* §2.2 — a file `suicide.exe` in the game folder makes the runtime refuse to run.
* §2.4 — if ITEM.DAT is missing, MAGIC.DAT is not read either.
* §2.5 — an empty start party is treated as defeated: event 99999 runs on every pass.
* §3.2 — the key-press handler runs a full message-processing pass before acting on its own key.
* §4.2 — in the map state a key press does not clear the release latch.
* §4.2 — Escape sets the confirm flag (ends waits) whenever the map does not have control.
* §4.2, §4.7 — the activation of the focused window is queued behind the key, so an inactive window ignores the key but becomes active.
* §4.3 — mouse movement, polling an un-held direction key and an idle joystick all re-arm the release latch.
* §4.5 — an Enter/Space press made while the hero is mid-step is forgotten.
* §4.7 — the joystick's synthetic keys prefer up/down over left/right; walking prefers up > left > down > right.
* §4.9 — a left click anywhere on the game area sets the confirm flag regardless of the latch.
* §4.9 — a left click outside the menu windows closes the whole field menu.
* §4.9 — double click and right+left click use the action button without movement-control, route or latch checks.
* §4.9 — right click opens the menu even while the hero is mid-step.
* §4.9 — mouse input over a deactivated window is handled as input on the game area outside windows.
* §4.9 — a teleport does not end mouse auto-walk; the hero then keeps turning right until a key or button is pressed.
* §5.1 — Wait(n) with n < 0 returns immediately.
* §5.2 — holding Escape freezes the game at the end of each hero step until it is released.
* §8 — in the autorun step, a pass-abort flag set by an autorun vehicle only ends the vehicle loop; step completion still runs.
* §8 — the end-of-pass page refresh skips the vehicles when the current map has no ordinary map events.
* §9.3 — a title image larger than the screen is shrunk to fit (per axis), not cropped.
* §9.3 — any end of the title-menu wait other than choosing `続きから` (Escape, a click outside the menu) starts a new game.
* §9.5 — after loading from the title the saved palette is shown as it is; the stored tint is not re-applied.
* §10 — a game-over event that does not end the game or revive the party runs again on every pass.
* §12 — switching the screen mode restarts the current music from the beginning.

## Open questions

* The effective tick length on a real Windows 95 system (clock granularity). This document uses 21 ms,
  the value confirmed by side-by-side comparison (§7).
* The enumeration order used to choose between several `.RPG` files (§2.1).
* The contents of the monster table when CHARA.DAT is missing (§2.4): this document assumes zero.
* The operating-system key auto-repeat delay and rate to emulate (§4.6).
* Whether the mouse pointer is visible in full-screen mode (§4.9).
* Whether any game relies on the opaque-window fallback of windowed mode on non-256-colour desktops
  (§2.3).
* Behaviour with equipment entries other than −1 and 0..99 in the actor data (§2.5).
* The real font metrics of MS PGothic 16 px bold on Japanese Windows 95: W = 17 (reference) or 16; LineH 18
  and ascent 14 are expected (§2.6).
* What the original visibly does when a battle started from an autorun or resumed event ends with an
  escape-to-exit teleport (§1).
