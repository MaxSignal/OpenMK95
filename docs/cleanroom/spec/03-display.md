# 03 — Display: the 8-bit screen model, palettes, effects, transitions, pictures, map composition

This chapter describes everything that ends up as pixels on the game screen outside the window system:
the surfaces and the 256-entry palette, how images are converted into palette indices, the tint and
palette effects, when and how the screen is presented, all screen transitions, the screen states and
the drawing part of the picture/erase/tone commands, and the pixel-level composition of the map and
of the battle screen. Window frames, fonts and texts are in `06-windows-text.md`; the logic of the event
commands is in `05-events.md`; camera, movement, animation frames and sprite selection are in
`04-map-movement.md`; battle logic is in `08-battle.md`; movies are in `09-audio-movies.md`.

All integer arithmetic follows `00-README.md` §4 (C semantics, division truncates toward zero).
"The random generator" is the single global generator of `01-runtime.md` (Random numbers); every
call mentioned here consumes exactly one number, and the number r it returns is in 0..2^31−1.

---------------------------------------------------------------------------------------------------

## 1. Screen model

### 1.1 Surfaces

The original runs in a 640 × 480 display mode with 8 bits per pixel. Every surface below holds **palette
indices** (0..255), never RGB colours. The colour seen on screen is always the current hardware palette
entry of the index (§2).

| name used here | size | content |
|---|---|---|
| **visible screen** | 640 × 480 | what the player sees. Changes only by presentation (§5), by transitions (§6) and by direct black fills (§5.4). |
| **back buffer** | 640 × 480 | the off-screen frame that all game drawing goes to (map tiles, sprites, pictures, battle scene). Never shown directly; it is copied to the visible screen. |
| **background cache** | 640 × 480 | on the map: the tile layer for the current camera tile origin, without characters (§8.2). In battle: the battle background (§10). |
| **chara sheet** | the size of the `CHARA.BMP` image (1024 × 512 in 27 sample games; the corpus also has 1024 × 391, 1024 × 256, 960 × 512 and 640 × 384) | the character sprite sheet, converted to indices (§3). |
| **chipset sheets 0..3** | the size of each chipset image (normally 256 × 1024 = 8 columns × 32 rows of 32 × 32 tiles; the corpus also has 256 × 200, 256 × 320, 256 × 500, 256 × 750, 256 × 810, 320 × 1024, 370 × 1024, 512 × 1024 and 1 × 1) | the four chipsets of the map. |
| **monster sheets 0..9** | per monster | the monster pictures of the current battle (§10). |
| **temporary image** | per image | a picture, the title image, a battle animation sheet; converted, drawn into the back buffer, then discarded. |

Windows (message boxes, menus) are separate layers that are always visually above the visible screen
(§5.5). Movies are drawn in their own rectangle above the screen as well (`09-audio-movies.md`).

### 1.2 Copy operations

Three kinds of copy are used between surfaces. A copy is done only if its source rectangle lies
completely inside the source sheet and its destination rectangle lies completely inside the destination
surface; otherwise it copies **nothing** — not even the part that would fit **(quirk)**. Where this chapter
says that a rectangle is clipped (sprites §8.4, presents §5.1, the min() limits of transitions §6.2), the
clipping is done before the copy as described there, so those copies always succeed. Copies that are not
clipped by hand and can fail this way: map tiles and strips taken from a chipset image that is smaller
than the tile position needs (§8.2, §8.3), sprites whose source lies outside a small chara sheet (§8.4)
and monster pictures that extend beyond the screen (§10).

UNCERTAIN: this all-or-nothing rule is the documented behaviour of the display system the original uses
for its surface copies (in full-screen mode and for all off-screen surfaces no automatic clipping is
attached); it was not observed on the original itself. The corpus contains cases where it matters: a
troop of ninomai places a monster 160 px wide at x 488, and 04favor has chipsets shorter than 1024 px.
The alternative would be ordinary clipping (the visible part is drawn). **Recommendation:** implement
the all-or-nothing rule.

* **Opaque copy** — every source pixel is copied.
* **Keyed copy** — source pixels whose index equals the source sheet's **colour key** (§3.3) are skipped;
  the destination keeps its pixel there.
* **Stretched copy** — a source rectangle of size sw × sh is copied into a destination rectangle of size
  dw × dh (opaque or keyed). The original uses nearest-neighbour stretching without filtering. Use
  destination pixel (dx, dy) relative to the destination rectangle ← source pixel
  (sx0 + dx × sw / dw, sy0 + dy × sh / dh) (integer division). UNCERTAIN: the exact sample positions of
  the original stretcher; the formula given is the recommended one. When dw = sw and dh = sh it is an
  ordinary copy.

Images are put into their sheets differently: the conversion of an image file into a sheet (§3.1) and
the background image of a battle (§10 step 2) are drawn with ordinary clipping (an image larger than the
target keeps its top-left part).

### 1.3 Full screen and windowed mode

The original starts full screen (windowed when Shift is held at start-up, and F4 toggles; see
`01-runtime.md`). The two modes produce the same pixels. They differ only in:

* palette fades: in full screen every fade has twice the number of steps and waits 30 ms per step; in
  windowed mode fades have no waits at all (§4.4);
* the block size of the random dissolves inside a rectangle that is not 640 px wide (§6.2, types 1–3):
  2 px in full screen, 4 px windowed;
* semi-transparent windows are switched off in windowed mode when the desktop is not 256 colours
  (`06-windows-text.md`);
* loading a palette source does not force the 20 system colours into entries 0..9 and 246..255 in
  windowed mode (§2.3 step 2).

**Recommendation:** an implementation should always use the full-screen behaviour (fade timings, block
size, transparency as set by the game), whatever its own window mode is. The rest of this chapter gives
the full-screen behaviour and mentions the windowed variant where it differs.

---------------------------------------------------------------------------------------------------

## 2. Palettes

### 2.1 The 20 system colours

Entries 0..9 and 246..255 of the hardware palette always hold the 20 standard system colours of the
256-colour display mode. The runtime writes them into every palette it loads (§2.3), and no tint or fade
ever changes them (with one momentary exception, §6.3 type 14). Window frames, window texts and the
poison flash use only these colours, so they are not affected by tints.

| index | RGB (hex) | | index | RGB (hex) |
|---|---|---|---|---|
| 0 | 00 00 00 (black) | | 246 | FF FB F0 (cream) |
| 1 | 80 00 00 (dark red) | | 247 | A0 A0 A4 (medium grey) |
| 2 | 00 80 00 (dark green) | | 248 | 80 80 80 (dark grey) |
| 3 | 80 80 00 (dark yellow) | | 249 | FF 00 00 (red) |
| 4 | 00 00 80 (dark blue) | | 250 | 00 FF 00 (green) |
| 5 | 80 00 80 (dark magenta) | | 251 | FF FF 00 (yellow) |
| 6 | 00 80 80 (dark cyan) | | 252 | 00 00 FF (blue) |
| 7 | C0 C0 C0 (light grey) | | 253 | FF 00 FF (magenta) |
| 8 | C0 DC C0 (money green) | | 254 | 00 FF FF (cyan) |
| 9 | A6 CA F0 (sky blue) | | 255 | FF FF FF (white) |

### 2.2 The palettes kept by the runtime

The display keeps four colour tables of 256 RGB entries and one tint value:

* **Work palette** — the raw colours of the current *palette source* image (§2.5), with the system
  colours in 0..9/246..255. It is never tinted. All tinted palettes are computed from it.
* **Committed palette** — the palette the screen "normally" shows: usually the work palette with the
  stored tint applied. Fades start from it, flashes return to it. Other chapters call it the **base
  palette**; the two names mean the same table. The palette stored in a save file is this one (or the
  remembered map palette, §7.4), and loading a save file puts the stored palette here
  (`10-save-files.md`).
* **Realised palette** — the entries currently in the hardware palette, i.e. the colours actually on
  screen. It usually equals the committed palette but differs during flashes and fades and after an
  uncommitted palette load. **Image conversion and nearest-colour lookups use the realised palette.**
* **Tint** — four bytes (R%, G%, B%, grey), initially (100, 100, 100, 0) at program start. It is applied
  to every reload of the map palette and is saved in the save file (`10-save-files.md`).

At program start all palettes are black except the system colours; this is not observable because a
palette source is loaded before anything is shown.

A change of the realised palette is visible **immediately** on the whole screen, including pixels drawn
earlier and windows (there is no presentation step for palette changes). An implementation that
composes frames in true colour must re-compose and show the frame whenever the realised palette changes,
also inside the busy waits of effects.

### 2.3 Loading a palette source image

"Load palette from image I (commit or not)":

1. Let N be the number of colour-table entries of I: for images with 1, 4 or 8 bits per pixel, the
   "colours used" count of the file header, or 2^bits when that count is 0; for images with more than
   8 bits per pixel (and for files with the old 12-byte header) N = 0.
2. For i = 0 .. N−1 (and i ≤ 255): work[i] := the system colour of index i if i ≤ 9 or i ≥ 246, else
   colour-table entry i of I. In windowed mode the system-colour substitution is not done (the image's
   own entries are taken); see §1.3.
3. Work entries ≥ N are **not changed**: they keep the colours of the previously loaded palette source
   **(quirk)**. Consequently a 2-colour (1 bit) or 16-colour image changes only its first 2 or 16 entries
   (which are system colours anyway), and a true-colour image changes nothing. 17 battle backgrounds of
   the sample games are 1-bit images.
4. Realised palette := work palette (all 256 entries).
5. If committed: committed palette := work palette. The tint is **not** applied by this step.

"Realise the committed palette": realised := committed (used when the map comes back, §7.2, after
movies, and after the screen-mode switch). "Re-apply the base palette", as other chapters write it, is
this operation. It never reloads an image and never applies the tint again; it only puts the committed
palette back on screen, so it undoes the visible part of a flash or of an uncommitted load.

"Set realised palette P (commit or not)": realised := P; if commit and P is not the committed palette
itself, committed := P.

### 2.4 Reloading the map palette

"Reload map palette with tint":

1. Load palette from image (committed) `CHARA.BMP` of the game folder — or, while a battle is in
   progress, `BG01.BMP` of the game folder **(quirk: always background 01, not the current battle's
   background)**.
2. Apply the stored tint instantly: palette effect mode 4 with the stored tint, tint not re-stored (§4).

### 2.5 Which image is the palette source

| situation | palette source | loaded with |
|---|---|---|
| map (start-up, after each battle, after the screen-mode switch, after lost surfaces are restored) | `CHARA.BMP` | uncommitted load, then mode 4 with the stored tint (§8.1) |
| map comes back after an own-palette picture, title, logos, loading a game from the system menu | `CHARA.BMP` | "reload map palette with tint" (§2.4) |
| map after loading a game from the title | none: the committed palette read from the save file | realised as it is; the stored tint read from the file is not applied until the next palette reload (`10-save-files.md`) **(quirk)** |
| battle | `bgNN.bmp` of the battle | uncommitted load, then mode 4 with the stored tint (§10) |
| picture with "own palette" flag | the picture | committed load, **no tint** (§7.4) |
| title image `title.bmp`, start-up logos | the image | committed load, no tint (§7.8) |

All other images (chipsets, monster pictures, battle animation sheets, pictures without the own-palette
flag) never change the palette; they are colour-matched into it (§3).

Because of the "uncommitted load then mode 4" pattern: mode 4 only changes the hardware when the newly
tinted palette differs from the *committed* palette (§4.3). If the new source has the same colours as
the previous one and a non-neutral tint is stored, the tinted result equals the old committed palette,
nothing is set, and the screen keeps showing the **untinted** raw colours just loaded **(quirk)**. This
happens for example when the battle background has the same palette as `CHARA.BMP` (90 of the 362
8-bit backgrounds of the sample games): the battle and then the map after the battle are shown
untinted until the next palette realisation.

---------------------------------------------------------------------------------------------------

## 3. Images and colour matching

### 3.1 Converting an image into palette indices

Every image is converted into a sheet of palette indices at load time by matching its colours against
the **realised palette at that moment**:

* For an image with a colour table (1, 4 or 8 bits per pixel): for each table entry c, find the palette
  index p (0..255) that minimises (R_p − R_c)² + (G_p − G_c)² + (B_p − B_c)²; on equal distance the
  **lowest index** wins. Every pixel of table entry c becomes p.
* For a true-colour image (16, 24, 32 bits): the same search for every pixel colour.

Consequences: an image whose colour table equals the realised palette keeps its indices, except that
duplicate colours collapse to their lowest index; colours not present in the palette are approximated.
UNCERTAIN: the exact tie-breaking of the original's colour matcher; lowest index is the usual behaviour
and is required here.

Images are converted against whatever palette is realised at load time, which may be a **tinted** or
partly faded palette **(quirk)**: a picture without own palette shown while a tint is stored, or a
battle animation sheet loaded during a tinted battle, is matched against the tinted colours (and then
shown through the same tint); a picture shown after "fade screen to black" (§4.8) is matched against a
palette whose entries 10..245 are black, so it can only use the 20 system colours. Map chipsets, the
chara sheet and monster pictures are always converted right after their own uncommitted palette load,
i.e. against untinted colours.

The image file formats and the file-name rules are in `02-data-formats.md`.

### 3.2 Nearest-index lookup for a single colour

Where this chapter says "the index nearest to RGB(r, g, b)" (poison flash fill, black fills), the same
search as in §3.1 is done in the realised palette. Black RGB(0,0,0) is always index 0. Red RGB(255,0,0)
is index 249 (or a lower index holding exact red).

### 3.3 Colour keys

A colour key is one palette index of a sheet; keyed copies skip pixels of that index (§1.2).

| sheet | colour key |
|---|---|
| chara sheet | the index of its **top-left pixel** (x 0, y 0 of the image as displayed) after conversion |
| chipset sheets 0..3 | **the same index as the chara sheet** (not their own top-left pixel). Only used when a tile is drawn as an event graphic; map tiles are drawn opaque. |
| monster sheets | each its own top-left pixel index |
| battle animation sheets | each its own top-left pixel index |
| pictures | the top-left pixel index, only when the picture's "transparent" flag is set; otherwise opaque |
| title image, logos | none (opaque) |

Since the key is an index, every pixel whose colour was matched to the same index is transparent as well.

---------------------------------------------------------------------------------------------------

## 4. Tint and palette effects

### 4.1 The palette effect

All tints, flashes, shakes and palette fades are done by one operation, called here
**palette effect(mode, colour, store)**, where *colour* is four bytes (R%, G%, B%, grey) or "none"
(none = use the stored tint), and *store* says whether the colour becomes the stored tint.

Step 1 — compute the **target palette T** from the work palette (§4.2) and the flag *changed* (§4.3).
Step 2 — act according to *mode* (§4.5). Step 3 — store the tint (§4.6).

### 4.2 Target palette formulas

For every entry i: if i ≤ 9 or i ≥ 246, T[i] = work[i] (unchanged). For 10 ≤ i ≤ 245, with the work
colour (r, g, b) and the colour bytes (pR, pG, pB, grey), each 0..255:

**Colour mode (grey = 0)**, per channel c ∈ {r, g, b} with its percentage p:

* if p < 100: c' = p × c / 100
* if p ≥ 100: c' = c + (255 − c) × (p − 100) / 100

c' is stored into an 8-bit entry, i.e. **modulo 256 (quirk)**. For p ≤ 200 the result is at most 255.
For p > 200 it exceeds 255 exactly when (255 − c) × (p − 200) ≥ 100, and then wraps around to a dark value.
Example: the battle poison flash uses (255, 0, 0, 0); a colour with red 0 gets red
0 + 255 × 155 / 100 = 395 → 139, red 100 gets 100 + 155 × 155 / 100 = 340 → 84, red 200 gets 285 → 29.
So p = 100 leaves a channel unchanged, 0 makes it black, 200 makes it 255 (white).

**Grey mode (grey ≠ 0, any non-zero value)**:

1. Luminance L of the work colour: let S = 12·r + 15·g + 10·b. L = S / 30 (integer division), **except**
   that when S > 0 and S is a multiple of 30, L = S / 30 − 1 **(quirk)**. Examples: (0, 0, 3) → 0,
   (0, 2, 0) → 0, (0, 0, 255) → 84, (255, 255, 255) → 314, (10, 0, 0) → 3.
   (The original computes ((r × 1.2) + g × 1.5) + b, multiplies the sum by the constant 1/3 and
   truncates toward zero. The constants 1.2 and 1/3 are stored in double precision, and both are slightly
   smaller than the true values; the arithmetic runs in the 64-bit-mantissa extended precision that the
   program selects at start-up. When S/30 is an integer, the product therefore lies just below it and
   truncates to one less, whatever the value of r. An exhaustive emulation of this arithmetic over all
   2^24 colours agrees with the integer rule for every colour. Double-precision arithmetic would not
   reproduce it: implementations must use the integer rule, not floating point.) L ranges 0..314.
2. Per channel with its percentage p: q = p × 255 / 200 (integer), c' = L × q / 255 (integer),
   c' = min(c', 255).

So in grey mode (200, 200, 200, g) gives the plain luminance grey, (100, 100, 100, g) half of it,
and a sepia look is obtained with different percentages per channel.

### 4.3 The *changed* flag

*changed* is true if for **any** entry i (0..255) any channel of T differs from the committed palette;
the comparison uses the value **before** the 8-bit wrap of §4.2 (so an overflowing channel always counts as
changed). Entries 0..9/246..255 are compared too (they normally agree).

### 4.4 The palette fade

**Palette fade(target, from, n, commit)**:

1. In full screen n := 2n (windowed: n unchanged).
2. cur := copy of *from*. Per entry 10..245 and channel, the step is d = (target − from) / n (integer
   division, toward zero, computed from the 8-bit values of both palettes).
3. Repeat n − 1 times: add d to every channel of entries 10..245 of cur (8-bit arithmetic); realised :=
   cur; in full screen busy-wait 30 ms (windowed: no wait).
4. Realised := target (all 256 entries of target). If commit: committed := target.

So a full-screen fade with n = 5 shows 9 intermediate palettes, 30 ms each (270 ms), then the target;
n = 8 shows 15 intermediates (450 ms); n = 3 shows 5 (150 ms); n = 0 sets the target at once.

### 4.5 Modes

| mode | name | behaviour |
|---|---|---|
| −1 | commit only | committed := T. The hardware is not changed; T becomes visible at the next realisation of the committed palette (§2.3) or as the start of the next fade. |
| 0 | flash once | only if a colour was given (not "none") and *changed*: realised := T; wait 20 ms; realise the committed palette; wait 20 ms. |
| 1 | flash twice | as 0, done twice (T 20 ms, normal 20 ms, T 20 ms, normal 20 ms). |
| 2 | small shake | screen shake with amplitude A = 8 (below); colour ignored, runs always. |
| 3 | large shake | screen shake with A = 16. |
| 4 | instant | if *changed*: palette fade(T, committed, 0, commit = yes) — i.e. realised := T, committed := T. |
| 5 | fade | if *changed*: palette fade(T, committed, 5, commit = yes). |
| 6 | flash with fade | if *changed*: palette fade(T, committed, 3, commit = no), then immediately realise the committed palette (fade towards T over 150 ms, then snap back). |
| 7 | fade | identical to 5. |
| other | — | nothing happens. |

The waits are busy waits (`01-runtime.md`): no game logic runs, the program stays responsive.

**Screen shake** (modes 2 and 3), four frames k = 0, 1, 2, 3:

1. dx = +A for k = 0 and 2, −A for k = 1, 0 for k = 3.
2. dy = 0 for k = 3; otherwise take one number r from the random generator and dy = (r mod 2A) − A
   (range −A .. A−1). So exactly **three** random numbers are consumed, at k = 0, 1, 2, after dx is known.
3. Present the back buffer shifted by (dx, dy): source rectangle (max(−dx,0), max(−dy,0)) of size
   (640 − |dx|) × (480 − |dy|), copied to the visible screen at (max(dx,0), max(dy,0)), after waiting for
   the vertical blank (§5.2). The strips of the visible screen not covered by the copy are **not
   cleared**; they keep the pixels of the previous frame.
4. Busy-wait 70 ms.

Frame k = 3 is the unshifted full present. The shake shows whatever is in the back buffer (the map, or
the battle scene in battle) regardless of the screen state.

### 4.6 Storing the tint

After the mode action: if mode is 4, 5 or 7, *store* is set and a colour was given (not "none"), the
stored tint := colour. This happens even when *changed* was false. Modes −1, 0, 1, 2, 3, 6 never store.

### 4.7 Uses of the palette effect

| user | mode | colour | store |
|---|---|---|---|
| event command 29 Screen tone (§7.6) | from the command; 4 if the screen is ERASED | from the command | yes |
| fade screen to black (§4.8) | 5 or 7 | (0, 0, 0, 0) | no |
| battle start (encounter) | 1 | (200, 200, 200, 0) = white, twice | no |
| party member hit in battle | 2 | none | no |
| poison damage in battle | 0 | (255, 0, 0, 0) (wraps, §4.2) | no |
| item / magic effect on the party side | from the animation record (`08-battle.md`, `07-menus-shop-inn.md`) | from the record | no |
| inn: fade back after the stay | 7 | (100, 100, 100, 0) | **yes — the inn resets the tint to normal** |
| map/battle palette loads (§2.4, §8.1, §10) | 4 | none (stored tint) | no |
| movie with black background, end (§7.7) | 4 then 5 | (0,0,0,0) then none | no |
| screen-mode switch (the tint saved before the switch is re-applied) | 4 | the saved tint | yes |
| loading a save game | — | the tint and the committed palette are read from the file (`10-save-files.md`); when loading from the system menu they are then replaced by "reload map palette with tint" (the file's tint applied to `CHARA.BMP`); when loading from the title the file's committed palette is shown as it is (§2.5) | — |
| show type 14 (§6.2) | 5 | (0, 0, 0, 0) | no |

Modes −1, 0, 1, 2, 3, 6 and the effects of items/magic change the committed palette at most temporarily;
the next "reload map palette with tint" restores the stored tint.

### 4.8 Fade screen to black

"Fade screen to black(mode)" (mode 5 or 7): palette effect(mode, (0,0,0,0), no store) — the committed
palette becomes black in entries 10..245 —, then the visible screen is filled with index 0 (§5.4), and
the screen state becomes ERASED. The back buffer is not changed.

---------------------------------------------------------------------------------------------------

## 5. Presentation

### 5.1 Present operations

* **Full present**: wait for the vertical blank (§5.2), then copy the whole back buffer to the visible
  screen.
* **Partial present(source rectangle, destination point)**: if the rectangle's right ≤ 0, bottom ≤ 0,
  left ≥ 640 or top ≥ 480, nothing happens. Otherwise clamp the rectangle to (0, 0, 640, 480), wait for
  the vertical blank and copy that part of the back buffer to the visible screen at the destination point
  (with the clamped size). "Present rectangle R" means destination point = R's top-left.
* **Block copy** (used by transitions): copy a rectangle of the back buffer to the same position of the
  visible screen, without waiting for the vertical blank. Rectangles that are empty or outside the
  screen copy nothing.

### 5.2 Vertical blank pacing

"Wait for the vertical blank" waits until the start of the next display refresh (60 Hz on the reference
system; on a modern system: wait until the next 1/60 s boundary or use the display's vsync). The map tick
ends with a full present, so the map's frame rate is tied to the refresh (`01-runtime.md` §7, Ticks
and timing). Transitions wait for the vertical blank only where stated.

### 5.3 When the screen is updated

The visible screen changes only in these cases:

1. The map tick (`01-runtime.md`, `04-map-movement.md`) redraws the back buffer and does a full present
   **only if the screen state is MAP**. In every other state the back buffer is still redrawn every tick,
   but nothing is presented.
2. Other map redraws that request presentation (after a completed hero route, vehicle boarding,
   talking to an event, poison) — again only in state MAP.
3. Battle frames present the battle area rectangle (`08-battle.md`); shakes and the hit blink present
   directly.
4. Every transition (§6), the shake (§4.5), the poison flash (§8.5), and the explicit full presents
   named in this chapter.
5. Direct black fills of the visible screen (§5.4).
6. Palette changes (§2.2) change colours without changing indices.

### 5.4 Direct black fill

Several operations fill the **visible screen** directly with index 0 (black) without touching the back
buffer: fade screen to black (§4.8), hide type 14 (§6.3), the movie start and end (§7.7). Windows are
not affected (they are separate layers).

### 5.5 Windows and exposed areas

Windows are drawn above the visible screen and are never overwritten by presents, transitions or
fills; when a window closes, the visible screen under it becomes visible again. (In the original,
uncovered areas are re-copied from the back buffer when the screen state is BEFORE_START,
PICTURE_BEFORE_MAP, MAP, PICTURE_OVER_MAP or PICTURE_ON_BLACK — that is, not ERASED and not MOVIE — and no
movie is playing, or while the title or a logo is displayed; in full-screen mode never while a save or
load file dialog is open. Because pictures are removed when the event that showed them ends (§7.2), this
gives the same result as simply revealing the visible screen in practically all cases; UNCERTAIN in the
rare case that a window closes over a picture after map ticks have redrawn the back buffer.)

---------------------------------------------------------------------------------------------------

## 6. Screen transitions

### 6.1 The two engines

There are 15 transition types, 0..14. Two operations use them:

* **Show(type, rectangle)** — reveals the current back buffer on the visible screen with a pattern. The
  rectangle defaults to the full screen (0, 0, 640, 480). Types 0, 1, 2, 3 and 13 work inside the
  rectangle; all other types ignore it and work on the whole screen.
* **Hide(type, blacken)** — makes the screen disappear. Types 12, 13 and 14 have their own hide effects
  (§6.3). Any other type: if *blacken*, fill the back buffer with index 0, then Show(type, full screen) —
  i.e. the black frame is revealed with the same pattern. With blacken false the current back buffer is
  revealed instead (used by Erase screen over a picture, §7.5). Hide type 0 with blacken = instant black.

The transitions are synchronous: nothing else (game logic, input handling) runs until they end. The
waits are busy waits. "Wait n ms" below always means a busy wait; "no delay" means no wait at all
between the block copies (see §6.5 for pacing).

### 6.2 Show types

Notation: R = the rectangle (left, top, right, bottom), w × h its size. "Copy block (x0, y0, x1, y1)"
= block copy of §5.1.

**Type 0 — instant.** Present rectangle R (partial present, one vertical-blank wait).

**Types 1, 2, 3 — dissolves** (inside R).

1. Block size b = 8 if w = 640, otherwise 2 in full screen (4 windowed). Because one random number is
   taken per block, the block size also decides how many random numbers the dissolve consumes (for a
   128 × 128 rectangle 4096 in full screen, 1024 windowed). The full-screen size is the reference
   behaviour; an implementation running in a window should still use b = 2 (§1.3).
2. cols = w / b, rows = h / b, N = cols × rows (integer divisions; pixels of a remainder strip at the
   right or bottom of R, w mod b and h mod b wide, are **never copied (quirk)**).
3. Order array a[0..N−1]: a[i] = i for types 1 and 2; a[i] = N − 1 − i for type 3.
4. Shuffle, for i = 0 .. N−1 in increasing order:
   * type 1: take r from the random generator; j = r mod N;
   * types 2 and 3: take r from the random generator; j = i + (r mod (cols × 8)); if j ≥ N then
     j = N − 1;
   * swap a[i] and a[j].
   (No random number is taken when N = 0, resp. cols × 8 = 0.)
5. For i = 0 .. N−1: copy block x = left + (a[i] mod cols) × b, y = top + (a[i] / cols) × b, size b × b.
   No delay.

Type 1 is a uniform random dissolve; type 2 dissolves roughly from top to bottom (each block is swapped
with one at most 8 rows further on); type 3 roughly from bottom to top. Full screen: 80 × 60 = 4800
blocks of 8 × 8 and 4800 random numbers.

**Type 4 — 8 × 8 checkerboard dissolve, same pattern in every 64 × 64 square** (full screen).

1. perm[0..63] = 0..63. For i = 0..63: take r from the random generator, j = r mod 64, swap perm[i] and
   perm[j] (64 random numbers).
2. For step k = 0..63: for every 64 × 64 square (square row sy = 0..7 outer loop, square column
   sx = 0..10 inner loop): block column cx = 8·sx + (perm[k] mod 8), block row cy = 8·sy + perm[k] / 8;
   if cx < 80 and cy < 60, copy block (8·cx, 8·cy, 8·cx + 8, 8·cy + 8). After each step wait 5 ms.

64 steps, ≈ 320 ms plus copy time.

**Type 5 — horizontal blinds** (full screen). For k = 0..15: for j = 0..30: y = 16·j + k; if y + 1 > 480
stop this pass; copy block (0, y, 640, y + 1). After each pass wait 60 ms. (16 passes of 30 one-pixel
lines; ≈ 960 ms.)

**Type 6 — vertical blinds** (full screen). For k = 0..15: for j = 0..40: x = 16·j + k; if x + 1 > 640
stop this pass; copy block (x, 0, x + 1, 480). After each pass wait 60 ms. (16 passes of 40 one-pixel
columns; ≈ 960 ms.)

**Type 7 — bands closing in from top and bottom** (full screen). For i = 0, 2, 4, …, 58: copy block
(0, 8i, 640, min(8i + 8, 480)), then copy block (0, 472 − 8i, 640, min(480 − 8i, 480)); wait 30 ms.
(30 steps, ≈ 900 ms; the top series fills the 8-px bands at y ≡ 0 mod 16 going down, the bottom series
the bands at y ≡ 8 mod 16 going up.)

**Type 8 — vertical 8-px stripes, two passes left to right** (full screen). Pass 1: for i = 0, 2, …, 78:
copy block (8i, 0, min(8i + 8, 640), 480), wait 10 ms. Pass 2: for i = 1, 3, …, 79: the same. (80 steps,
≈ 800 ms.)

**Type 9 — as 8 but pass 2 right to left**: pass 1 as type 8, pass 2 for i = 79, 77, …, 1.

**Type 10 — spiral of 64 × 48 cells** (full screen; 10 × 10 grid of cells, cell (x, y) = rectangle
(64x, 48y, min(64x + 64, 640), min(48y + 48, 480))).

1. Copy cell (4, 4); wait 3 ms. Position (x, y) = (4, 4), direction d = 1.
2. For n = 2 .. 21: repeat n / 2 times (leg lengths 1, 1, 2, 2, …, 10, 10): move one cell in direction d
   (d 0 = up: y − 1; 1 = right: x + 1; 2 = down: y + 1; 3 = left: x − 1); if x ≥ 0 and y ≥ 0: copy cell
   (x, y) and wait 3 ms. After the leg, d = (d + 1) mod 4.

This is a clockwise spiral starting to the right of the centre cell: (4,4) (5,4) (5,5) (4,5) (3,5) (3,4)
(3,3) (4,3) (5,3) (6,3) (6,4) … . It visits 111 positions; 100 of them have x ≥ 0 and y ≥ 0 and cover
every cell exactly once (11 positions with x = −1 or y = −1 are skipped without wait). ≈ 300 ms.

**Type 11 — 8 × 8 spirals inside every 64 × 64 square, no delays** (full screen). A step "copy block
position (x, y)" (x, y in 0..7) means: for every square (by = 0..7 outer, bx = 0..10 inner) with
px = 64·bx + 8x and py = 64·by + 8y: if px < 640 and py < 480, copy block (px, py, min(px + 8, 640),
min(py + 8, 480)).

1. Copy block position (3, 3). Position (x, y) = (3, 3), direction d = 0.
2. For n = 2 .. 17: first d = (d + 1) mod 4 (so the first leg goes right); then repeat n / 2 times: move
   one position in direction d (same direction encoding as type 10); if x ≥ 0 and y ≥ 0, copy block
   position (x, y).

73 positions, 64 of them inside 0..7, covering every position once. No waits at all.

**Type 12 — box opening from the centre** (full screen). For i = 1..16: wait for the vertical blank;
copy block (320 − 20i, 240 − 15i, 320 + 20i, 240 + 15i); wait 40 ms. (≈ 16 × (vblank + 40 ms).)

**Type 13 — mosaic zoom-in** (inside R).

1. Let hw = w / 2, hh = h / 2. Two scratch sheets: S1 of w × h and S2 of hw × hh.
2. Source rectangle Q = R shrunk by 1 px on every side.
3. Start size: if hh < hw: y = 8, x = hw × 8 / hh; otherwise x = 8, y = hh × 8 / hw.
4. Repeat (at least once):
   a. stretched copy of Q from the back buffer into S2 (0, 0, x, y) (shrink);
   b. stretched copy of S2 (0, 0, x, y) into S1 (0, 0, w, h) (enlarge, giving x × y coarse blocks);
   c. wait for the vertical blank; copy S1 to the visible screen at R;
   d. x = x × 3 / 2, y = y × 3 / 2; wait 2 ms;
   while x ≤ hw and y ≤ hh.
5. If x < w or y < h (always the case in practice): **full present** of the whole back buffer (not
   just R).

For the full screen the frames are (x, y) = (10, 8), (15, 12), (22, 18), (33, 27), (49, 40), (73, 60),
(109, 90), (163, 135), (244, 202): 9 frames, then the full present. UNCERTAIN: very small rectangles
(hw or hh equal to 0) are not handled by the original; show them instantly.

**Type 14 — palette fade-in** (full screen).

1. Remember the committed palette as P.
2. Palette effect(5, (0,0,0,0), no store): the committed palette fades to black (entries 10..245); this
   is normally invisible because the screen is already black after a hide.
3. Full present (the back buffer appears, but with a black palette — only pixels using system colours
   are visible at once).
4. Palette fade(P, black committed palette, 8, commit) (450 ms in full screen); committed := P.

### 6.3 Hide types with their own effects

**Hide type 12 — box closing to the centre.**

1. Scratch sheet S of 640 × 480: if blacken, S is all index 0; otherwise S is a copy of the back buffer.
2. For i = 0..15: copy from S into the back buffer the four rectangles
   top (20i, 15i, 640 − 20i, 15i + 15), bottom (20i, 465 − 15i, 640 − 20i, 480 − 15i),
   left (20i, 15i, 20i + 20, 480 − 15i), right (620 − 20i, 15i, 640 − 20i, 480 − 15i);
   then full present; wait 40 ms.

With blacken the black frame grows inward ring by ring until the screen is black. With blacken false the
back buffer is unchanged by the copies, so the **first** full present already shows the whole back
buffer, and the remaining 15 presents only add ≈ 15 × (vblank + 40 ms) of delay **(quirk)**.

**Hide type 13 — mosaic zoom-out.**

1. Scratch sheets S1 640 × 480 and S2 320 × 240; source Q = (1, 1, 639, 479) of the back buffer;
   x = 320, y = 240.
2. Repeat: stretched copy Q → S2 (0, 0, x, y); stretched copy S2 (0, 0, x, y) → S1 (0, 0, 640, 480);
   wait for the vertical blank; copy S1 to the whole visible screen; x = 2x / 3, y = 2y / 3; stop when
   x < 4 or y ≤ 3. Frames: (320, 240) (213, 160) (142, 106) (94, 70) (62, 46) (41, 30) (27, 20) (18, 13)
   (12, 8) (8, 5) = 10 frames, no extra waits.
3. If blacken: fill the back buffer with index 0 and Show(0) (instant black). Otherwise the last, coarsest
   mosaic frame stays on screen.

**Hide type 14 — palette fade-out.**

1. Palette fade(all-black palette, committed palette, 8, no commit): 15 intermediate palettes (entries
   10..245 only), 30 ms each, then the final step sets **all 256 entries** to black (the system colours
   and therefore windows go black for a moment). UNCERTAIN whether entries 0 and 255 really changed on
   the reference hardware; the effect lasts only until step 3.
2. Fill the visible screen and the back buffer with index 0; full present.
3. Realise the committed palette (colours come back while the screen is black).

The back buffer is black afterwards **even when blacken was false** (§7.5).

### 6.4 Random numbers used by transitions

Only types 1, 2, 3 (N numbers, N = number of blocks) and 4 (64 numbers) use the random generator, in the
shuffle loops given above, before any block is copied. Hides of these types use the same numbers (via
Show). No other transition uses it. Elsewhere in this chapter the shake (§4.5) takes three numbers, and
the character drawing of a map redraw (§8.3 step 7) can take numbers through the animation rules of
`04-map-movement.md` whenever the animation is not frozen. That includes the redraw inside the poison
flash (§8.5) and the redraws that show the map after a battle (§10 step 8, §6.7).

### 6.5 Durations and pacing

Waits are exact busy waits as stated. Types 1, 2, 3 and 11 (and the block-copy parts of all types) have
no waits; on the reference hardware their speed was the speed of the copies, a few hundred ms for a
full-screen dissolve. **Recommendation:** spread the blocks of types 1–3 and the 64 positions of type 11
evenly over about 400 ms, showing the progress at least every 1/60 s. UNCERTAIN: the true speed on period
hardware.

Approximate full-screen durations: 0 one frame; 1–3 ≈ 400 ms (paced); 4 ≈ 320 ms; 5, 6 ≈ 960 ms; 7 ≈
900 ms; 8, 9 ≈ 800 ms; 10 ≈ 300 ms; 11 ≈ 400 ms (paced); 12 ≈ 16 frames + 640 ms; 13 ≈ 9 frames; 14 ≈
270 + 450 ms; hide 12 like show 12; hide 13 ≈ 10 frames; hide 14 ≈ 450 ms.

### 6.6 Out-of-range types

The game data only contain 0..14. For robustness: in Show, a negative type behaves like type 2 (the
dissolve branch takes all types below 4; anything that is not 1 or 3 uses the type-2 shuffle) and a
type above 14 copies nothing. In Hide, such types first blacken (if requested) and then behave as in
Show.

### 6.7 Where transitions are used

The transitions table (three numbers, `00-README.md` §5; read from the game file, `02-data-formats.md` §3.4,
with no built-in defaults; changed by event command 50 Set transition, `05-events.md`; saved in the save file):

| use | engine and type |
|---|---|
| leaving a map (teleport), unless the state is ERASED or BEFORE_START | Hide(table[0], blacken), state ERASED |
| arriving on a map (show map, §7.2) | Show(table[0]) |
| battle start | flash, Hide(table[1], blacken), Hide(0, blacken), state ERASED (§10) |
| battle screen appears | Show(table[2]) (§10) |
| battle end | Hide(table[2], blacken) — or Hide(13, blacken) if the battle ends by an escape/return warp — state ERASED |
| map after the battle | Show(table[1]) after a full tile redraw (§10); also after a failed battle set-up (`08-battle.md` §1.9) |
| map after a lost battle that does not end the game | when the game-over event returns and the screen is still ERASED: the leader's field music is played (`09-audio-movies.md` §3.8), the tiles and characters are redrawn (animation advancing) and the map is shown with Show(table[1]); state MAP. If the event already brought the map back, nothing more is done (`01-runtime.md` §10) |
| return/escape warp (magic or item) | the map change uses type 13 for both its hide and its show (table[0] is temporarily replaced by 13); a warp cast in battle is done at the end of the battle with Hide(13) and Show(13) |
| Show picture (command 26) | Show(type from the command, picture rectangle) (§7.4) |
| Erase screen (command 31) | Hide(type from the command, …) (§7.5) |
| monster death in battle | Show(3, monster rectangle) after removing the monster from the back buffer (§10) |
| start-up, ending the game, closing | Hide(0, blacken) (instant black) |

---------------------------------------------------------------------------------------------------

## 7. Screen states and the screen commands

### 7.1 States

The screen state (`00-README.md` §5) records what is visible:

| state | value | meaning | set by |
|---|---|---|---|
| BEFORE_START | −1 | nothing shown yet in a new game | new game from the title, before the start event (`01-runtime.md`) |
| PICTURE_BEFORE_MAP | −2 | a picture was shown during BEFORE_START (start event) | Show picture in BEFORE_START |
| MAP | 0 | the map is visible and presented every tick | show map (§7.2), battle screen shown, battle end, Erase screen over a picture, inn |
| PICTURE_OVER_MAP | 1 | a picture is drawn over the map; the map is no longer presented | Show picture without own palette in MAP |
| PICTURE_ON_BLACK | 2 | a picture with its own palette on a black screen | Show picture with own palette when the state is ≥ 0 |
| ERASED | 3 | the screen is black (hidden) | every hide on map change / battle, fade screen to black, Erase screen, start-up |
| MOVIE | 4 | a movie with black background is playing | Play movie with black-background flag |

While the state is not MAP the map tick keeps redrawing the back buffer (with the map) but does not
present it. When an event run ends, the map is brought back by "show map" unless the game ended or the
screen is ERASED while the hero or the event still has a pending move route (`05-events.md`, end of an
event run). So pictures and erased screens normally last only until the end of the event that made them.

### 7.2 Show map (bring the map back)

1. If the state is MAP: do nothing at all.
2. If the state is PICTURE_ON_BLACK or PICTURE_BEFORE_MAP: fade screen to black (mode 5, §4.8), then
   reload map palette with tint (§2.4).
3. If the state is ERASED or BEFORE_START: realise the committed palette.
   (States PICTURE_OVER_MAP and MOVIE: no palette action.)
4. Full tile redraw (§8.2) and draw the characters without presenting and with animation frozen
   (§8.3 with "no present").
5. Show(table[0]) (full screen).
6. State := MAP. A remembered map palette (§7.4 step 2) is forgotten.

### 7.3 Summary of state changes made by the screen commands

| command (logic in `05-events.md`) | BEFORE_START | PICTURE_BEFORE_MAP | MAP | PICTURE_OVER_MAP | PICTURE_ON_BLACK | ERASED |
|---|---|---|---|---|---|---|
| Show picture, normal | → PICTURE_BEFORE_MAP | stays | → PICTURE_OVER_MAP | stays | stays | stays |
| Show picture, own palette | → PICTURE_BEFORE_MAP | stays | → PICTURE_ON_BLACK | → PICTURE_ON_BLACK | stays | → PICTURE_ON_BLACK |
| Erase screen | nothing | hide, stays | hide, → ERASED | reveal map, → MAP | hide, then show map → MAP | hide, stays |
| Screen tone | effect | effect | effect | effect | effect | instant (mode 4) |

### 7.4 Show picture (event command 26) — drawing part

Command arguments (`05-events.md`): a file name and a flags word F:

| bits | meaning |
|---|---|
| 0 | fit to screen (forced on when the image is wider than 640 or taller than 480) |
| 1 | transparent (colour key = top-left pixel); together with bit 0 also "keep aspect ratio" |
| 2 | use the picture's own palette |
| 3 | wait for the OK key after showing |
| 4–7 | position p = 0..8: horizontal index ph = p mod 3 (0 left, 1 centre, 2 right), vertical index pv = p / 3 (0 top, 1 centre, 2 bottom) |
| 16–31 | transition type for Show (signed 16-bit) |

Steps:

1. If the file does not exist, do nothing (`02-data-formats.md` for the file lookup). Close the message
   window. Read the image (width w, height h).
2. Without own palette (bit 2 clear): if the state is MAP it becomes PICTURE_OVER_MAP; other states are
   kept. The realised palette is not changed.
   With own palette (bit 2 set): if the state is MAP or PICTURE_OVER_MAP, fade screen to black (mode 5,
   §4.8; state becomes ERASED). Fill the back buffer with index 0. If the state is now ≥ 1 (i.e. 1, 2, 3
   or 4) it becomes PICTURE_ON_BLACK; BEFORE_START and PICTURE_BEFORE_MAP are kept. Load palette from the
   picture, **committed, without tint** (§2.3). Before this load, if no map palette is remembered yet,
   the committed palette is remembered as the **remembered map palette**; it is forgotten when the map is
   shown again (§7.2 step 6). It is never put back on screen, but a save made while it exists stores
   the remembered map palette instead of the committed one (`10-save-files.md`).
3. Convert the image against the realised palette (§3.1). If w > 640 or h > 480, set bit 0. If bit 1 is
   set, the colour key is the converted index of the top-left pixel.
4. Destination rectangle D, using the position functions below:
   * bit 0 clear: W' = min(w, 640), H' = min(h, 480) (in this case w ≤ 640 and h ≤ 480 anyway);
     D = (X(ph, W'), Y(pv, H'), size W' × H'); the image is drawn unscaled.
   * bit 0 set, bit 1 clear: D = (0, 0, 640, 480); the image is stretched to the full screen, aspect ratio
     not kept.
   * bit 0 set, bit 1 set (keep aspect): if (h × 4) / 3 < w: W' = 640, H' = h × 640 / w, D = (0, Y(pv, H'),
     640 × H'); else H' = 480, W' = w × 480 / h, D = (X(ph, W'), 0, W' × 480).
   **(quirk)**: "keep aspect ratio" is tied to the transparency bit; an opaque fitted picture is always
   stretched to 640 × 480.
5. Stretched copy of the whole image (0, 0, w, h) into D of the back buffer, keyed if bit 1 is set,
   otherwise opaque (§1.2). The converted image is then discarded.
6. Show(transition type, D): types 0, 1, 2, 3 and 13 reveal only D (13 ends with a full present), the
   other types reveal the whole back buffer — which in PICTURE_OVER_MAP contains the map as of the last
   tick plus the picture.
7. If bit 3: wait for OK (Wait(0), `01-runtime.md`).
8. If the state is BEFORE_START it becomes PICTURE_BEFORE_MAP.

Position functions (the screen is 640 × 480; size s = W' or H'):

* X(0, s) = 20, or 0 if 640 − s < 40. X(1, s) = (640 − s) / 2. X(2, s) = 640 − s − m with m = 20, or 0 if
  640 − s < 40.
* Y(v, s): the same with 480 instead of 640.
* Then: if pos + s > 640 (resp. 480), pos = 640 − s (480 − s); if pos < 0, pos = 0.
* Vertical index values 3..5 (p = 9..15) do not occur in game data; UNCERTAIN, treat them as 0.

There is only one picture at a time; a second Show picture simply draws into the back buffer again. In
PICTURE_OVER_MAP the back buffer is restored from the map by every map tick, so a later Show picture after
ticks have run draws over a fresh map frame, and its transition (if it is a full-screen type) also
replaces the previously visible picture.

### 7.5 Erase screen (event command 31) — drawing part

Argument: a transition type t. First the message window is closed. Then, by state:

* **BEFORE_START**: nothing.
* **PICTURE_BEFORE_MAP**: Hide(t, blacken). State unchanged.
* **PICTURE_OVER_MAP**: redraw the map into the back buffer without presenting (§8.3, frozen animation),
  then Hide(t, **no** blacken) — this reveals the map over the picture with the pattern of t; if t is 13,
  additionally Show(13) (full screen). State := MAP. Per type the visible result is:
  * 0..11: the map is revealed with the show pattern of t;
  * 12: the map appears at once, followed by ≈ 0.6 s of delay (§6.3) **(quirk)**;
  * 13: the picture pixelates out to the coarsest mosaic (hide 13), then the map zooms in (show 13);
  * 14: the screen fades to black and stays black (the back buffer is blackened, §6.3); the map appears
    abruptly at the next map tick after the event **(quirk)**.
* **PICTURE_ON_BLACK**: Hide(t, blacken); reload map palette with tint (§2.4); state := ERASED; then show
  map (§7.2: realise committed palette, redraw, Show(table[0]), state MAP).
* **MAP, ERASED, MOVIE**: Hide(t, blacken); state := ERASED. (The map comes back by "show map" at the end
  of the event, §7.1.)

### 7.6 Screen tone (event command 29)

Arguments: a mode m and a word V; colour = (V and 255, (V >> 8) and 255, (V >> 16) and 255, V >> 24)
= (R%, G%, B%, grey). Typical data: 0x00646464 = normal (100,100,100), 0x00C8C8C8 = white. First one
message-processing pass is run (pending window messages are processed, `01-runtime.md` §3.2), then the
message window is closed. Then:

* if the state is ERASED: palette effect(4, colour, store) — instant, the requested mode is ignored;
* otherwise: palette effect(m, colour, store).

The tint is therefore stored for modes 4, 5, 7 (and always in ERASED). Mode −1 only changes the
committed palette; modes 0, 1, 6 flash; 2, 3 shake.

### 7.7 Play movie (event command 14) — screen part

(Playback itself is in `09-audio-movies.md`.) Flags bit 2 = black background:

* Start, with black background: if the state is MAP, palette effect(5, (0,0,0,0), no store) (the
  committed palette fades to black); then fill the visible screen with index 0 and set state MOVIE.
* End, without black background: realise the committed palette.
* End, with black background: fill the visible screen with index 0; palette effect(4, (0,0,0,0), no
  store) (instant black); full present (the back buffer, i.e. the map, invisible under the black
  palette); palette effect(5, none, no store) (fade in to the stored tint); the state returns to the
  value it had before the movie.

### 7.8 Title image and start-up logos

(The start-up flow is in `01-runtime.md`.) Drawing an image I of size w × h for the title or a logo:

1. Fill the back buffer with index 0.
2. Load palette from I, committed, no tint (§2.3); convert I against it; no colour key.
3. Size W' = min(w, 640), H' = min(h, 480); position x = (640 − w) / 2 if w < 640 else 0,
   y = (480 − h) / 2 if h < 480 else 0 (centred). Stretched copy (§1.2, opaque) of the whole image
   (0, 0, w, h) into (x, y, W' × H') of the back buffer. For an image that fits the screen this is an
   ordinary copy; an image wider than 640 or taller than 480 is shrunk to fit in that dimension, each
   axis independently (the aspect ratio is not kept) **(quirk)**. No title in the sample games is larger
   than 640 × 480.
4. The title menu window is placed at y = top + H' − H' / 4 (`06-windows-text.md`); without `title.bmp`
   the menu y is 250.
5. The image is made visible by a full present (or partial present of the screen).

When the title is left, the program presents, fades screen to black (mode 5), fills the back buffer black
and reloads the map palette with tint.

---------------------------------------------------------------------------------------------------

## 8. Map composition

Camera position, scroll flags, sprite selection (graphic id, animation column, direction row), animation
timing, draw order and the screen occupancy grid are defined in `04-map-movement.md`. This section gives
the pixel operations.

### 8.1 Map graphics

"Load map graphics":

1. Load palette from `CHARA.BMP` of the game folder, **uncommitted** (§2.3).
2. Convert `CHARA.BMP` into the chara sheet; colour key := index of its top-left pixel.
3. For k = 0..3: convert the chipset image `<name k>.BMP` of the game folder (names from the map record,
   `02-data-formats.md`) into chipset sheet k; colour key := the chara sheet's key. The names are those of
   the **current** map (at start-up: the start map).
4. Palette effect(4, none, no store) (apply the stored tint).

This is done at start-up, after **every battle**, after the screen-mode switch and when lost display
surfaces are restored (full-screen program switched away and back, `01-runtime.md` §8 step 10). It is **not** done on
a map change or when a saved game is loaded: a map whose chipset names differ from those of the map where
graphics were last loaded is drawn with the old chipsets until the next battle **(quirk)**. All maps of the
sample games use the chipsets `MCHIP0`..`MCHIP3`, so this is invisible in practice.
UNCERTAIN: behaviour when `CHARA.BMP` or a chipset file is missing (does not occur in the sample games;
recommended: an empty, black sheet).

### 8.2 Tiles and the full tile redraw

A tile value t (16 bits of a map cell, `02-data-formats.md`) is drawn from chipset sheet c = (t >> 8) and 3
(tile values are 0..1023), tile index n = t and 255, source rectangle ((n mod 8) × 32, (n / 8) × 32,
32 × 32). Map tiles are drawn **opaque**. There is only one tile layer. A tile whose source rectangle is
not completely inside its chipset sheet (a chipset image narrower than 256 or shorter than 1024 pixels,
§1.1) is not drawn at
all; the back buffer keeps whatever pixels it had there **(quirk, §1.2)**. The same holds for the strips
of §8.3 step 5.

**Full tile redraw**: for screen row y = 0..14 and column x = 0..19, draw the tile of map cell
((camera x + x) mod W, (camera y + y) mod H) at (32x, 32y) of the back buffer (camera = the camera tile
origin, W × H = map size). Then copy the whole back buffer into the background cache.

### 8.3 The map redraw

"Map redraw(direction d, present)", called every tick and after various actions (`04-map-movement.md`):

1. If d is "automatic" and the hero's sub-step counter is 4, return without drawing anything
   (`04-map-movement.md` explains why this happens).
2. Automatic direction: if the hero is in a step (motion 0..3) with sub-step (s mod 4) > 0, d := the
   step direction and the scroll sub-step s := the hero's sub-step; otherwise s := 0 and there is no
   scrolling direction. (For an explicit d the scroll sub-step set by the tick is used.)
3. If there is a direction d and scrolling in d is enabled (scroll flag of d, `04-map-movement.md`):
   the camera pixel origin := ((W × 32 + camera x × 32 + dx × 8 × s) mod (W × 32),
   (H × 32 + camera y × 32 + dy × 8 × s) mod (H × 32)) with (dx, dy) of d. Otherwise the camera pixel
   origin keeps its value (camera tile × 32).
4. Restore the background: o = 8 × s if scrolling in d, else 0. Opaque copy of the background cache into
   the back buffer shifted by o against the scroll direction: for d up: cache (0, 0, 640, 480 − o) → back
   buffer at (0, o); down: cache (0, o, 640, 480) → (0, 0); left: cache (0, 0, 640 − o, 480) → (o, 0);
   right: cache (o, 0, 640, 480) → (0, 0). With o = 0 the whole cache is copied unshifted.
5. If scrolling in d, draw the newly exposed strip of o pixels directly from the chipsets (opaque), for
   i = 0..14 (left/right) or i = 0..19 (up/down), with all map coordinates modulo W resp. H:
   * left: tile of cell (camera x − 1, camera y + i), source columns 32 − o .. 31 of the tile, to back
     buffer (0, 32i), size o × 32;
   * right: cell (camera x + 20, camera y + i), source columns 0 .. o − 1, to (640 − o, 32i);
   * up: cell (camera x + i, camera y − 1), source rows 32 − o .. 31, to (32i, 0), size 32 × o;
   * down: cell (camera x + i, camera y + 15), source rows 0 .. o − 1, to (32i, 480 − o).
6. If s = 4 (the scrolling step completes), there is a direction, and d was given explicitly (the tick's
   call, not the automatic form): copy the back buffer (tiles only, no characters yet) into the
   background cache; if scrolling in d is enabled, camera tile += (dx, dy) modulo the map size.
7. Draw the characters (§8.4) in the order of `04-map-movement.md` (layers 0, 1, 2; screen rows −1..15;
   events, vehicles, party with the leader last). Their animation is frozen when d is automatic and
   present is false (the redraw before a transition), otherwise it advances (`04-map-movement.md`).
8. If present is requested and the screen state is MAP: full present.

### 8.4 Character sprites

For each drawn character (selection rules and the graphic/direction/frame choice: `04-map-movement.md`):

1. Screen position: px = x × 32 + dx × 8 × f − camera pixel x, py = y × 32 + dy × 8 × f − camera pixel y,
   where (x, y) is the tile the character stands on (or started its step from), f its sub-step and
   (dx, dy) the step direction if it is in a step (motion 0..3), else (0, 0). If px < −31, px += W × 32;
   if py < −31, py += H × 32 (wrap).
2. If px > 639 or py > 479 the sprite is not drawn.
3. Only characters whose display mode is "visible" are drawn ("invisible" ones are registered but not
   drawn; `04-map-movement.md`).
4. Source: graphic g < 256: the chara sheet, rectangle ((g mod 16) × 64 + column × 32,
   (g / 16) × 128 + row × 32, 32 × 32); graphic g ≥ 256: tile t = g − 256 of the chipsets (rectangle as in
   §8.2). (Column 0/1 = animation frame, row = direction 0 up, 1 right, 2 down, 3 left.)
5. Clipping: width = height = 32; if px < 0: source left −= px, width += px, px = 0; else if px > 608:
   width = 640 − px. Same vertically with 448 / 480.
6. **Keyed** copy into the back buffer at (px, py) (chara key, or the shared chipset key for tile
   graphics). A source rectangle (after the clipping of step 5) that is not completely inside the sheet
   draws nothing (§1.2): for example graphic column 15 of a 960-pixel-wide chara sheet, graphic rows 3
   and below of a 391-pixel-high one, or a tile graphic from a short chipset.
7. Wagon and sled (two-tile vehicles) additionally draw their trailing part with graphic 62 resp. 63
   one tile behind, as specified in `04-map-movement.md`, with the same clipping and key.

### 8.5 Poison step flash

After a completed hero step, for the first party member that loses HP by poison (`04-map-movement.md`,
`08-battle.md` for the HP logic): fill the whole back buffer with the index nearest to RGB(255, 0, 0)
(§3.2; normally 249, a system colour, so the red is not tinted), full present, then map redraw
(automatic direction, with present). The red frame is visible for one refresh period plus the redraw
time. Only one flash per step, however many members are poisoned.

---------------------------------------------------------------------------------------------------

## 9. Semi-transparent windows — pixel part

When the game's "transparent windows" flag is set (`02-data-formats.md`, `06-windows-text.md`), a window
does not have a grey background; it shows a darkened copy of what was on screen behind it. Window geometry
and when the snapshot is taken are in `06-windows-text.md`. The pixel part:

1. **Snapshot**: at the moment the window is created (or switched to transparent), copy the screen
   content under the window's client area — the area starting 3 px right of and below the window's outer
   top-left corner, of the client size — as palette indices. "Screen content" means what is visible:
   the visible screen plus any windows already open underneath. Pixels outside the screen: index 0.
   The snapshot is taken once and is not updated while the window exists.
2. **Darkening map**: for every palette index i (0..255) with realised colour (r, g, b), compute the
   darkened colour (r', g', b') with factors depending on the window slot:

   | window slot | factors (R, G, B) | per-channel integer rule (c = 0..255) |
   |---|---|---|
   | 0 (message window) | 0.7, 0.7, 0.7 | c' = 7c / 10, minus 1 if c > 0 and c is a multiple of 10 |
   | 1 (battle status) | 0.6, 0.6, 0.8 | R, G: c' = 3c / 5, minus 1 if c > 0 and c is a multiple of 5; B: c' = 4c / 5 |
   | all other slots | 0.5, 0.5, 0.5 | c' = c / 2 |

   (The original multiplies each channel by the factor stored as a double-precision constant and
   truncates toward zero, in the extended precision the program selects at start-up. The constants 0.7
   and 0.6 are stored slightly too small, so exact products fall just below the integer; 0.8 is stored
   slightly too large and 0.5 exactly, so those products are exact. An exhaustive check of all 256 channel
   values against the emulated arithmetic confirms the integer rules for all four factors. A
   double-precision computation would instead round most of these products up to the exact integer
   (for example 7 for c = 10 with factor 0.7), so implementations must use the integer rules. **(quirk)**) Example: for c = 10 the message window gives
   6, not 7; for c = 255 it gives 178.
   Then map[i] = the index nearest to (r', g', b') in the realised palette (§3.1 search over all 256
   entries, lowest index on ties).
3. Replace every snapshot pixel index p by map[p].
4. The window paints this darkened snapshot as its background every time it is drawn.

The palette used is the realised palette at snapshot time; later palette changes recolour the snapshot's
indices like any other pixels (so the darkened background follows tints and fades).

---------------------------------------------------------------------------------------------------

## 10. Battle graphics — pixel level

(Battle sequence and logic: `08-battle.md`.)

1. **Start**: palette effect(1, (200,200,200,0), no store) (white double flash, only if the palette is
   not already white); Hide(table[1], blacken); Hide(0, blacken); state ERASED. After the encounter sound
   (`09-audio-movies.md`) the battle screen is set up.
2. **Background**: file `bgNN.bmp` of the game folder with NN = two-digit decimal (troop record's
   background number + 1) (`02-data-formats.md`). If it is missing the battle does not start (error box,
   `08-battle.md`). Load palette from it, **uncommitted** (§2.3). Convert it and copy it **unscaled at
   (0, 0)** into the background cache; where the image is smaller than 640 × 480 the cache keeps its
   previous content (the map tiles) **(quirk)**. This copy is clipped normally: of an image larger than
   640 × 480 only the top-left 640 × 480 pixels are used.
3. **Monsters**: each monster picture of the troop (`MONSTPIC.DAT`, format in `02-data-formats.md`;
   8-bit pixel rows stored bottom-up, own 256-entry colour table) is converted against the realised
   (background, untinted) palette; colour key := its top-left pixel (top of the displayed image).
4. Back buffer := background cache; each monster i in troop order is drawn keyed with its top-left
   corner at the position (x, y) stored in the troop record. A monster whose rectangle (x, y, x + w,
   y + h) is not completely inside 640 × 480 is not drawn at all, here and in every later battle frame
   (§1.2, UNCERTAIN there) **(quirk)**. The sample data contain one such troop entry (ninomai: x 488,
   picture width 160). The same rule applies to every monster position used while the battle runs
   (`08-battle.md`, for example the shifted positions of an attacking enemy).
5. Palette effect(4, none, no store) (stored tint); Show(table[2]) (full screen); state MAP.
6. During the battle a frame is drawn as: back buffer := background cache, living monsters keyed,
   effects; then the battle area is presented (`08-battle.md`). Battle animation sheets `AnmNN.bmp` are
   converted when played, against the then realised (tinted) palette, keyed by their top-left pixel.
   A defeated monster disappears by redrawing background and living monsters, then
   Show(3, monster rectangle) (bottom-to-top dissolve with 2 px blocks in full screen; one random
   number per block, §6.2).
7. Hits: a hit party member triggers palette effect(2, none) (small shake, §4.5); battle poison damage
   triggers palette effect(0, (255,0,0,0)) (one 20 ms red-ish flash with channel wrap-around, §4.2).
8. **End**: Hide(table[2] or 13, blacken), state ERASED, load map graphics (§8.1), then (if the battle
   did not end the game or warp) full tile redraw, characters drawn (animation advancing),
   Show(table[1]), state MAP.
9. **Failed set-up** (missing background or `MONSTPIC.DAT`, `08-battle.md` §1.9): none of step 8 except
   its last part: no hide and no reload of the map graphics; the full tile redraw, the characters
   (animation advancing) and Show(table[1]) are done, state MAP. The palette loaded in step 2 (if the
   background existed) stays in effect until the next palette realisation **(quirk)**.

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* §1.2 — A surface copy whose source or destination rectangle is not completely inside its surface copies nothing at all (UNCERTAIN, display-system behaviour); affects small chipset/chara images and monsters beyond the screen edge.
* §2.3 — A palette source with fewer than 256 colours (1-, 4-bit images, 255-colour tables) changes only its first N work entries; the rest keep the previous source's colours; true-colour images change nothing.
* §2.4 — While a battle is in progress, "reload map palette" loads `BG01.BMP`, not the current battle background.
* §2.5 — After an uncommitted palette load, mode 4 sets nothing when the tinted result equals the old committed palette, leaving untinted colours on screen (battles/maps with the same palette as `CHARA.BMP` under a stored tint).
* §2.5 — After loading a game from the title, the save file's committed palette is shown as it is; the stored tint is not re-applied until the next palette reload.
* §3.1 — Images are converted against the realised palette at load time, which may be tinted or faded to black.
* §3.3 — Chipsets use the chara sheet's colour key, not their own top-left pixel.
* §4.2 — Colour-mode tint channels above 255 wrap modulo 256 (percentages above 200), e.g. the battle poison flash (255,0,0,0).
* §4.2 — Grey-mode luminance is one less than S/30 when S is a positive multiple of 30 (floating-point truncation with constants stored too small).
* §4.3 — The *changed* test compares the unwrapped values, so overflowing channels always count as changed.
* §4.5 — Shakes do not clear the uncovered strip of the screen.
* §4.6 — Modes 4/5/7 store the tint even when the palette did not change.
* §6.2 — Dissolves 1–3 never copy the remainder strip of a rectangle that is not a multiple of the block size.
* §6.2 — Show 13 ends with a full-screen present even for a picture rectangle.
* §6.2 — Show 14 shows system-colour pixels at full brightness before the fade-in.
* §6.3 — Hide 12 without blacken shows the whole back buffer at the first ring and then only delays.
* §6.3 — Hide 14 sets all 256 entries (including system colours) to black for a moment and always blackens the back buffer.
* §7.4 — "Keep aspect ratio" of fitted pictures is tied to the transparency bit; opaque fitted pictures are stretched.
* §7.5 — Erase screen 12 over a picture reveals the map at once then waits; Erase screen 14 over a picture leaves a black screen in state MAP until the next tick.
* §7.8 — A title or logo image larger than the screen is shrunk to fit per axis, not cropped.
* §8.1 — Chipsets are reloaded only at start-up, after battles, on screen-mode switch and on surface restore, never on a map change or game load.
* §8.2 — A tile or strip whose source lies outside a small chipset image is not drawn; the back buffer keeps its old pixels there.
* §9 — Window darkening factors 0.7 and 0.6 give one less for exact multiples (truncation in extended precision).
* §10 — A battle background smaller than 640 × 480 leaves map tiles visible around it.
* §10 — A monster whose rectangle extends beyond the screen is never drawn.
* §10 — After a failed battle set-up the map is shown without reloading the map graphics, under the background's palette if it was loaded.

## Open questions

* Exact tie-breaking and matching rules of the original's image colour matcher (lowest index assumed), and whether windowed mode uses the 20 system colours differently.
* Exact sampling positions of the original's stretched copies (pictures, mosaic transitions); nearest neighbour with floor(offset × source / destination) recommended.
* True speed of the undelayed transitions 1, 2, 3, 11 on period hardware (≈ 400 ms recommended).
* Whether entries 0 and 255 visibly changed during the final step of hide 14 on the reference hardware.
* Behaviour for picture position indices 3..5, for degenerate show-13 rectangles and for missing `CHARA.BMP`/chipset files.
* The original's repaint of uncovered screen areas from the back buffer (§5.5) could show the map instead of a picture in rare cases after map ticks; not observed.
* §1.2 — Whether copies that reach outside a surface really draw nothing on the original (display-system rule, not observed); checkable with the ninomai troop at x 488 or a 04favor map that uses the lower rows of a short chipset.
