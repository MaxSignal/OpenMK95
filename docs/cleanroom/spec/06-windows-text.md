# 06 — Windows, fonts, text, messages and input in windows

This chapter defines the window system used by every menu, message and list of the runtime: the
font and its metrics, how text is laid out and drawn, how windows look (frame, background, colours,
cursor), the exact label layout of every window slot 0..12, the generic list-window rules, keyboard
and mouse handling inside windows with the internal notifications they produce, and the message
functions (`Message(text, factor, position)`, the event message, the choice window, the pop-up).
The menus that use these windows are in `07-menus-shop-inn.md`; battle windows are listed here for
layout only, their behaviour is in `08-battle.md`. The palette arithmetic behind transparent window
backgrounds and the colour-matching rule are in `03-display.md`. The `$` text codes are in
`05-events.md`. `Wait(n)` and the keyboard/mouse model of the whole program are in `01-runtime.md`.

Throughout this chapter **LineH** is the line height and **W** the "maximum character width" defined
in section 2. With the reference metrics LineH = 18 and W = 17; numeric examples use these values.

---------------------------------------------------------------------------------------------------

## 1. The window model

### 1.1 Windows, slots and layering

* A **window** is a rectangle drawn over the game screen (map, picture, battle screen or black screen).
  It consists of a 3-pixel frame and a **client area** inside the frame. Every window has an
  **outer rectangle** (x, y, w × h) in screen coordinates; its client area is
  (x + 3, y + 3, (w − 6) × (h − 6)). All coordinates inside a window ("client coordinates") are
  relative to the client area's top-left corner.
* There are 13 **window slots** (0..12, meaning listed in `00-README.md`). A slot holds at most one
  window. The slot number decides the label layout (section 5), the darkening factor in transparent
  mode, the cursor redraw rules and how a selection or cancel in it is dispatched (`07-menus-shop-inn.md`,
  `08-battle.md`).
* Creating a window always puts it **on top** of all other windows. Windows never move or resize after
  creation. Destroying a window removes it from the screen; the screen area below is redrawn at once
  ("with screen update") or at the next normal screen refresh, as stated per use.
* Windows are composed over the game screen in creation order (newest on top). The game screen below
  keeps animating normally where the program is not blocked, but a window's own pixels never change
  except when the window repaints itself (label text change, cursor move, activation).
* Most creation helpers create a window only if its slot is empty; when the slot is occupied they
  leave the existing window unchanged (stated per use). Where a slot is "re-created", the old window is
  destroyed first.

### 1.2 Display-only and selectable windows

A window is created either **display-only** (no item list: message, status screen, party order display,
pop-up, battle status) or **selectable** (it has a list layout with rows and an item count). A
selectable window has a cursor (section 7); a display-only window has none.

### 1.3 Labels

All text in a window is held in **labels**. A window has 60 label positions, numbered 0..59; each
position is either absent or holds a label with:
* a rectangle in client coordinates (left, top, right, bottom; right/bottom exclusive),
* a text (Shift_JIS, initially empty),
* a text colour (initially the window's normal text colour, section 4.3),
* a format (alignment and line-breaking mode, section 3.2),
* a **disabled** flag (initially clear). A disabled label is drawn in gray (section 4.3) and, for item
  labels, cannot be selected.

Setting a label's text, colour or disabled flag repaints the window.

Each label also had a fixed text capacity in the original (20, 40, 100 or 1024 bytes, given in the
layout tables). Texts longer than the capacity never occur with valid game data (names are at most
19 bytes) and were not guarded; an implementation should simply accept longer texts (they are clipped
by the rectangle anyway).

---------------------------------------------------------------------------------------------------

## 2. The font

One font is used for every window (title menu, field menus, messages, shops, battle): **MS PGothic**
(face name `ＭＳ Ｐゴシック`, Shift_JIS bytes `82 6C 82 72 20 82 6F 83 53 83 56 83 62 83 4E`),
**bold** (weight 700), requested character height 16 pixels (a negative height of −16: the height of
the glyphs without internal leading), not italic, not underlined, default character set, proportional
pitch, no anti-aliasing.

The original does not use fixed numbers: after creating the font it reads the font's text metrics
and sets **LineH = the font's text height (cell height, ascent + descent) + 2** and **W = the font's
maximum character width**. The values below are the expected metrics of MS PGothic with character
height 16 and synthetic bold (+1 pixel); an implementation must use exactly these numbers.
**UNCERTAIN**: they were not measured on a Japanese Windows 95 system (the side-by-side comparison
ran with a substitute font, see Open questions); W = 16 is the main alternative.

Metrics used by the layout (reference values for MS PGothic, character height 16, bold):

| metric | value | used for |
|---|---|---|
| text height (ascent + descent) | 16 | line pitch inside one label; vertical centring |
| ascent | 14 | baseline position: baseline = line top + 14 |
| maximum character width | 17 | **W**: window widths computed from character counts |
| line height **LineH** = text height + 2 | 18 | row pitch of every list, window heights, cursor height |

How these metrics affect layout:
* A list of n rows is `LineH × n + 6` pixels high (6 = two frame widths) and row r of a list occupies
  client rows `LineH × r` .. `LineH × (r + 1) − 1`.
* Window widths computed from text are `(number of characters + 1) × W` (characters counted as
  Shift_JIS characters: a double-byte character counts 1, a half-width character also counts 1),
  possibly rounded (section 6). Because MS PGothic is proportional, the actual text is narrower
  than this width.
* Inside a label, consecutive lines are `text height` = 16 pixels apart (not LineH).

Glyph shapes: the original draws the font's 16-pixel glyphs without smoothing; bold is produced by
the system's synthetic emboldening: every set pixel of a glyph is also set one pixel to its right
and every character advance is one pixel larger than the regular weight's advance (so the ideographic
space U+3000, regular advance 16, advances 17). **UNCERTAIN**: whether some sizes of MS PGothic carry
dedicated bold bitmaps; the side-by-side comparison matched synthetic emboldening. An implementation
may use any metric-compatible font (for example a free "P Gothic" font) but must use the metrics of
the table for all layout computations. If no suitable font is available, advances of 8 + 1 pixels for
single-byte and half-width katakana characters and 16 + 1 pixels for double-byte characters are a
reasonable fallback.

---------------------------------------------------------------------------------------------------

## 3. Text drawing

### 3.1 General

* Texts are Shift_JIS. Decoding: bytes 0x81–0x9F and 0xE0–0xFC are lead bytes of a two-byte
  character; 0xA1–0xDF are single-byte half-width katakana; other bytes are single-byte characters.
* Text is drawn in the label's colour with a **transparent background**: only glyph pixels are
  written, the window background (or the transparent-mode snapshot) shows between them.
* Glyph pixels are written as palette indices (the 8-bit screen model of `03-display.md`); the window
  colours are the fixed system colours listed in section 4.3.
* Each label is drawn clipped to the intersection of its own rectangle and the window's client area.
  Pixels outside are never written. A line that starts inside the label but extends below its bottom
  is drawn partially (its upper pixel rows are visible).

### 3.2 Label formats

A label format combines a horizontal alignment with a line mode. The formats used by the runtime:

| name used in this spec | horizontal | lines | vertical |
|---|---|---|---|
| **left-wrap** | left | multi-line, explicit breaks and automatic wrapping | from the top |
| **centre-wrap** | centred | multi-line, explicit breaks and wrapping | from the top |
| **right-wrap** | right | multi-line, explicit breaks and wrapping | from the top |
| **left-plain** | left | multi-line at explicit breaks only, no wrapping | from the top |
| **right-plain** | right | multi-line at explicit breaks only, no wrapping | from the top |
| **single-vc** | left | exactly one line | vertically centred |
| **single-vc-right** | right | exactly one line | vertically centred |

(The original's single-line formats also carried the wrapping bit; it has no effect on a single line.)

### 3.3 Layout algorithm

**Prefix characters (`&`).** None of the label formats switches off the system's "prefix"
processing, so `&` (byte 0x26, which is never a Shift_JIS trail byte) is a mnemonic marker in **every
window label** — messages, choices, list rows, names, pop-ups, disabled (gray) labels included. Before
lines are measured and drawn, the text is processed: a single `&` is removed and the character after it
is marked **underlined**; `&&` becomes one literal `&` (not underlined); a `&` at the very end of the
text is removed. Removed `&` bytes have no width and take no part in wrapping. An underlined character
is drawn normally plus a 1-pixel horizontal line in the label's colour under its advance width, on the
pixel row line top + ascent + 1 (**UNCERTAIN**: the exact row and length of the underline). Character
counts used for window widths (sections 6.2, 10.2, 11) are taken from the unprocessed text, so they
count the `&`. Sample games contain such texts (an event choice option `はまった&おかしいぞ` shows as
`はまったおかしいぞ` with お underlined; a message `裕樹&和の部屋` shows as `裕樹和の部屋` with 和
underlined) **(quirk)**. The version information box (section 12) is not a window label and shows `&`
literally.

Given a label rectangle (left, top, right, bottom), width = right − left, height = bottom − top:

1. **Split into lines.**
   * Single-line formats: carriage returns (0x0D) and line feeds (0x0A) are removed and the whole
     text is one line.
   * Other formats: the text is split at line feed, carriage return, or the pair carriage return +
     line feed (one break). A trailing break produces a final empty line.
2. **Wrap** (wrapping formats only). Each line is processed character by character, accumulating the
   advances. When adding the next character would make the line wider than the label width and the
   current line is not empty:
   * if the character is a single-byte non-space character (part of an ASCII word) and the current
     line contains a space at a position other than its first character, the line is broken after the
     last such space: the text after that space moves to the next line (the space itself is dropped);
   * otherwise (Japanese characters, or a single ASCII word longer than the line) the line is broken
     before the character; spaces at the start of the new line are dropped.
   The character is then added to the (new) current line. A single character wider than the label is
   placed alone on a line.
   Japanese text therefore breaks between any two characters; there is no rule that keeps
   punctuation (。、」) off the start of a line. **UNCERTAIN**: the original's exact rule for mixed
   ASCII/Japanese words; the rule above matched every compared screen.
3. **Vertical position.** Multi-line formats: the first line's top is the label top; each further
   line is 16 pixels (text height) lower. Single-line vertically centred formats: line top =
   label top + (height − 16) / 2 (integer division).
4. **Horizontal position** of each line, with line width = sum of its character advances:
   left: x = label left; centred: x = left + (width − line width) / 2 (integer division, may be
   less than left when the line is too wide, the excess is clipped); right: x = right − line width.
5. **Draw** each character with its glyph origin at (x, line top + ascent 14) and advance x.
   Drawing stops after the first line whose top is at or below the clip bottom.

Characters with no glyph (control characters other than the breaks, e.g. tab) have zero width.
**UNCERTAIN**: the original may draw a visible box for a tab; games do not use tabs in texts.

---------------------------------------------------------------------------------------------------

## 4. Window appearance

### 4.1 Frame

The frame is 3 pixels wide, identical in opaque and transparent mode, made of three rings (colours as
RGB; palette indices of the Windows static colours in parentheses):

| ring (from outside) | top row and left column | bottom row and right column |
|---|---|---|
| 0 (outermost: x = 0 / w−1, y = 0 / h−1) | light gray C0C0C0 (7) | black 000000 (0) |
| 1 (x = 1 / w−2, y = 1 / h−2) | white FFFFFF (255) | gray 808080 (248) |
| 2 (x = 2 / w−3, y = 2 / h−3) | light gray C0C0C0 (7) | light gray C0C0C0 (7) |

The bottom/right lines include the corner pixels: in ring 0 the pixels (w−1, 0) and (0, h−1) are
black; in ring 1 the pixels (w−2, 1) and (1, h−2) are gray. Verified pixel by pixel on reference
screens (for a window spanning x = 240..399: x = 240 C0C0C0, 241 FFFFFF, 242 C0C0C0, …,
397 C0C0C0, 398 808080, 399 000000).

### 4.2 Background

* **Opaque mode**: the whole client area is filled with light gray C0C0C0 (index 7).
* **Transparent mode**: when the window is created, a snapshot is taken of what is currently visible
  on the screen under the client area — the game screen **and all windows already shown there** — and
  every pixel of the snapshot is replaced by a darker colour. Every repaint draws this snapshot as the
  client background. The snapshot is never refreshed: if the scene below changes later, the window
  keeps showing the old darkened picture. The darkening factors depend on the slot:

  | slot | red | green | blue |
  |---|---|---|---|
  | 0 (message) | 0.7 | 0.7 | 0.7 |
  | 1 (battle status) | 0.6 | 0.6 | 0.8 |
  | all others | 0.5 | 0.5 | 0.5 |

  The pixel arithmetic — the exact per-channel integer rules for these factors (which are **not**
  plain truncated products for 0.7 and 0.6, **quirk**), the nearest-entry matching and the treatment of
  pixels outside the screen — is defined only in `03-display.md` §9 (Semi-transparent windows) and
  must be taken from there.
* Ring 2 of the frame stays light gray in transparent mode, so a transparent window shows a light
  gray inner border around the darkened picture.
* Transparent mode is a game setting (game file flag, `02-data-formats.md`). The original forces
  opaque mode when it runs in a window on a desktop that is not in 8-bit colour mode; an
  implementation that emulates the 8-bit screen should honour the game flag in both screen modes.
  Transparent mode is decided at window creation. (The original also re-applies transparent mode to an
  existing message window after a movie, but the movie command closes the message window first, so
  this never has a visible effect.)

### 4.3 Colours

| element | opaque mode | transparent mode |
|---|---|---|
| background | C0C0C0 | darkened snapshot |
| **normal text colour** (every label's initial colour) | black 000000 (0) | white FFFFFF (255) |
| disabled label text | gray 808080 (248), regardless of the label colour | same |
| party member not in normal condition (lists coloured by state) | red FF0000 (249) | red FF0000 |
| separator lines (status screen, battle status) | black | white |
| cursor | inversion of the cell (section 7.2) | black bar, text redrawn gray or blue (section 7.3) |

### 4.4 Separator lines

Two windows draw a 1-pixel horizontal line in the separator colour after the background and before
the labels:
* slot 1 (battle status): client rectangle (6, 18) – (589, 19), i.e. row 18, x = 6..588;
* slot 9 (status screen): client rectangle (16, 198) – (430, 199), i.e. row 198, x = 16..429.

### 4.5 Page buttons

The paged list windows (slots 4, 5, 7, 8) contain two push buttons, drawn on top of the labels:
* `<<` at client (145, 182), 40 × 20; created **disabled**;
* `>>` at client (190, 182), 40 × 20; created enabled.

A button is drawn like a standard Windows 95 push button: face C0C0C0; outer top/left line white,
outer bottom/right line black, inner bottom/right line gray 808080; the caption `<<` or `>>` centred
horizontally and vertically. An enabled caption is black; a disabled caption is drawn embossed: once in
white offset by (+1, +1), then in gray 808080 at the normal position. The caption uses the system's
standard (non-bold) control font, not the window font (**UNCERTAIN** in size; drawing it with the
window font in regular weight is acceptable). The buttons are drawn the same way in transparent mode
(they are not transparent).

### 4.6 Paint order

A full repaint draws, in this order: frame; background; separator line (slots 1 and 9); all present
labels in index order 0..59; the page buttons; then, if label 0 exists and the window has a cursor
position, the cursor (section 7). In opaque mode the cursor is therefore always an inversion of the
already drawn text.

---------------------------------------------------------------------------------------------------

## 5. Label layouts

### 5.1 List layout parameters

A selectable window is created with a **list layout**: rows, columns (always 1), a label format
`fmt` for the item labels, and optionally an initial item text (lines separated by line feeds).
Derived values:
* **column width** colW = outer width − 6 (the full client width);
* **pad** = 0 if `fmt` is centred, otherwise 10.

### 5.2 Generic list layout (slots 2, 6, 11, and the base of 4, 5, 7, 8)

* Label 0: (0, 0) colW × LineH, format `fmt`, 100 bytes. It is never given text; its presence means
  "this window has a cursor".
* **Slots 2, 6, 11** — item label k = 1 + r for row r = 0..rows−1:
  rectangle x = pad, y = LineH × r + 1, width colW, height LineH; format `fmt`; 40 bytes.
  (With pad 10 the rectangle extends 10 pixels past the client area's right edge; it is clipped.)
* **Slots 6 and 11** additionally have four labels per row r, numbered continuing after the items:
  label `rows + 4r + 1 + j` for j = 0..3, at y = LineH × r + 1, height LineH, format **right-wrap**,
  20 bytes, with

  | j | content | x | width | right edge |
  |---|---|---|---|---|
  | 0 | `HP` or `MP` | 130 | 30 | 160 |
  | 1 | current value | 160 | 37 | 197 |
  | 2 | `/` | 197 | 8 | 205 |
  | 3 | maximum value | 205 | 37 | 242 |

  These labels exist in every slot-6/11 window but are only given text by the party-list helper
  (section 6.2); otherwise they stay empty.
* **Slots 4, 5, 7, 8** (two labels per row, 10 rows): name label k = 1 + r at x = pad,
  y = LineH × r + 1, width colW / 2 + 20, height LineH, 20 bytes; value label k = 11 + r at
  x = pad + colW / 2, same y, width colW / 2 − 20, height LineH, 20 bytes. With the fixed list width
  240 (colW = 234, pad 10): name labels span x = 10..146, value labels x = 127..223. The two overlap;
  a long name is clipped at x = 147 and may be overdrawn by the value text.
* If an initial item text is given, its lines are assigned in order to labels 1, 2, 3, … up to the
  last created label (for slots 6/11 a text with more lines than rows would continue into the HP/MP
  labels; this never happens).

Item label text starts 1 pixel below the row top (y = LineH × r + 1) for every multi-line format.

### 5.3 Paged list windows (slots 4, 5, 7, 8)

Outer size always **240 × 210**, list layout 10 rows, `fmt` = left-wrap, item count = number of
entries in the whole list (all pages). In addition to section 5.2:

| label | rectangle (client) | format | text |
|---|---|---|---|
| 45 | (24, 182) 20 × 20 | right-wrap | `(` |
| 46 | (40, 182) 20 × 20 | right-wrap | current page + 1 (`%d`) |
| 47 | (60, 182) 40 × 20 | centre-wrap | `/N )` where N = (count − 1) / 10 + 1 |

and the two page buttons (section 4.5). The visible indicator reads `( 1/2 )`.

When the list content is filled (for the first page at creation, and after every page change, see
section 8.6), the filler sets, for each row r of the page, the name label's format to **left-plain**
and the value label's format to **right-plain**, so names never wrap. Content per slot:

| slot | name label | value label | disabled |
|---|---|---|---|
| 4 magic list | spell name | `%dpts.` (MP cost) | both labels of a row disabled when the caster's current MP < cost (re-evaluated per row at every fill; the caster is the menu actor of `07-menus-shop-inn.md` in the field, the member entering a command in battle) |
| 5 item list / equipment candidates | item name | `(%d)` (current count) | never |
| 7 teleport destinations | map name | (empty) | never |
| 8 shop list | item name | price (`%d`, see `07-menus-shop-inn.md`) | never |

Fill rule: for row i = 0..9 of page p, entry index = p × 10 + i: set both texts; if this entry is the
last one (count − 1 ≤ p × 10 + i), set the name and value texts of all remaining rows i+1..9 to empty
and send "end of list" (section 8.5); stop.

### 5.4 Slot 0 — message window

Display-only. One label:

| label | rectangle (client) | format | capacity |
|---|---|---|---|
| 1 | (10, 10), (w − 20) × (4 × LineH − 8) | left-wrap | 1024 |

With the standard outer size 456 × 92: client 450 × 86, label 436 × 64 (four lines of 16 pixels fit
exactly; a fifth line would be below the label and invisible). Text origin on the screen for a window
at (92, 32): (105, 45).

### 5.5 Slot 1 — battle status window

Display-only, outer (20, 362) 600 × 100 (contents: `08-battle.md`). Labels (client coordinates, w × h,
capacity 100 each):

| label | rectangle | format | content |
|---|---|---|---|
| 12 | (0, 0) 200 × 20 | centre-wrap | `Name` |
| 13 | (200, 0) 100 × 20 | centre-wrap | `Hp` |
| 14 | (300, 0) 100 × 20 | centre-wrap | `Mp` |
| 15 | (450, 0) 100 × 20 | centre-wrap | `Status` |
| 9 | (0, 20) 20 × 100 | right-wrap | cursor mark column |
| 10 | (0, 20) 200 × 100 | centre-wrap | member names, one per line |
| 6 | (200, 20) 45 × 100 | right-wrap | current HP per line |
| 16 | (245, 20) 20 × 100 | left-wrap | `/` per line |
| 18 | (248, 20) 45 × 100 | right-wrap | maximum HP per line |
| 7 | (300, 20) 45 × 100 | right-wrap | current MP per line |
| 17 | (345, 20) 20 × 100 | left-wrap | `/` per line |
| 19 | (348, 20) 45 × 100 | right-wrap | maximum MP per line |
| 8 | (450, 20) 100 × 100 | centre-wrap | condition name per line |

plus the separator line of section 4.4. The lines inside these labels are 16 pixels apart.

### 5.6 Slot 9 — status screen

Display-only, outer (136, 80) 450 × 230 (client 444 × 224). All labels 100 bytes; "single-vc" texts
are vertically centred in their 20-pixel rows (text top = row top + 2). Content is defined in
`07-menus-shop-inn.md`.

| label | rectangle (client) | format | content |
|---|---|---|---|
| 21 | (30, 10) 140 × 20 | single-vc | name |
| 22 | (160, 10) 90 × 20 | single-vc-right | `Level %d` |
| 44 | (270, 10) 160 × 20 | single-vc | condition name |
| 13 | (40, 30) 60 × 20 | single-vc-right | `Hp:` |
| 6 | (100, 30) 40 × 20 | single-vc-right | HP |
| 16 | (140, 30) 20 × 20 | single-vc | `/` |
| 18 | (150, 30) 40 × 20 | single-vc-right | max HP |
| 14 | (40, 50) 60 × 20 | single-vc-right | `Mp:` |
| 7 | (100, 50) 40 × 20 | single-vc-right | MP |
| 17 | (140, 50) 20 × 20 | single-vc | `/` |
| 19 | (150, 50) 40 × 20 | single-vc-right | max MP |
| 11 | (0, 70) 100 × 20 | single-vc-right | `経験値:` |
| 12 | (130, 70) 60 × 20 | single-vc-right | experience |
| 48 | (0, 90) 100 × 20 | single-vc-right | `ﾚﾍﾞﾙUPまで:` |
| 49 | (130, 90) 60 × 20 | single-vc-right | experience to next level |
| 50 | (0, 110) 100 × 20 | single-vc-right | `知力:` |
| 51 | (130, 110) 60 × 20 | single-vc-right | intelligence |
| 24 | (0, 130) 100 × 20 | single-vc-right | `攻撃力:` |
| 27 | (130, 130) 60 × 20 | single-vc-right | attack |
| 25 | (0, 150) 100 × 20 | single-vc-right | `防御力:` |
| 28 | (130, 150) 60 × 20 | single-vc-right | defence |
| 26 | (0, 170) 100 × 20 | single-vc-right | `すばやさ:` |
| 29 | (100, 170) 90 × 20 | single-vc-right | agility |
| 32 | (200, 30) 110 × 20 | single-vc-right | equipment slot 0 name + `:` |
| 45 | (200, 50) 110 × 20 | single-vc-right | slot 1 name + `:` |
| 33 | (200, 70) 110 × 20 | single-vc-right | slot 2 name + `:` |
| 34 | (200, 90) 110 × 20 | single-vc-right | slot 3 name + `:` |
| 35 | (200, 110) 110 × 20 | single-vc-right | slot 4 name + `:` |
| 36 | (310, 30) 140 × 20 | single-vc | item in slot 0 |
| 46 | (310, 50) 140 × 20 | single-vc | item in slot 1 |
| 37 | (310, 70) 140 × 20 | single-vc | item in slot 2 |
| 38 | (310, 90) 140 × 20 | single-vc | item in slot 3 |
| 39 | (310, 110) 140 × 20 | single-vc | item in slot 4 |
| 30 | (200, 200) 110 × 20 | single-vc-right | currency name + `:` |
| 31 | (310, 200) 90 × 20 | single-vc-right | money |

Labels 36..39 and 46 extend to x = 450, past the client width 444 (clipped). Separator line: section
4.4.

### 5.7 Slot 10 — party order display

Display-only. Labels 40..47 (k = 0..7): rectangle (10, 10 + 20k) 130 × 20, format single-vc,
100 bytes. Geometry and use: `07-menus-shop-inn.md`.

### 5.8 Slot 12 — pop-up

Display-only. Label 1: top-left (4, 4), size (w − 4) × (h − 4) where w, h are the window's **outer**
size (so its right/bottom edges are w and h), format left-wrap, 1024 bytes. It is clipped to the
client area (w − 6) × (h − 6), so text wider than w − 10 pixels wraps or is cut off. Geometry:
section 11.

### 5.9 Slot 3

Never used.

---------------------------------------------------------------------------------------------------

## 6. Creating windows

### 6.1 Direct creation

Windows created directly use exactly the rectangle given by their user (no rounding, no clamping):
message windows, the field main menu, the battle command menu, the paged lists, the status screen, the
party order display, the battle status window and pop-ups. Creating a window makes its slot the
**focused slot** (section 8.1).

### 6.2 The list-window helper

Most small lists (slots 6 and 11) are created by a helper with parameters: x, y, requested width w,
rows, item text (one line per row), maximum text length L (characters, 0 = use w), item format `fmt`,
slot, a disable mask, and an optional "colour by state" switch. The helper:

1. Clears the stored selection result (sets the selected index to −1, section 8.4).
2. Height H = LineH × rows + 6.
3. If L > 0: w = (L + 1) × W.
4. **Width rounding**: W' = (w / 32 + 1) × 32 (integer division), so a width that is already a
   multiple of 32 still grows by 32: 140 → 160, 160 → 192, 240 → 256, 119 → 128, 136 → 160.
5. **Vertical clamp**: if y + H > 480 then y = 440 − H. (There is no clamp at the top; y may be
   negative.)
6. **Centring**: if x < 0 (callers pass −1), x = (640 − W') / 2.
7. If the slot is empty: create the window at (x, y) W' × H with a list layout of `rows` rows,
   format `fmt`, the item text, item count = rows. Set label 0's format to `fmt`. For every bit i set
   in the disable mask, disable item label i + 1. If "colour by state" is on, item label i + 1 (party
   member i, i = 0..rows−1) gets the normal text colour if that member's condition word is 0 **and**
   HP > 0, otherwise red. Repaint.
   If the slot is occupied, nothing is created or changed.

The **party-list helper** is the same with "colour by state" always on and additionally fills the
four column labels of each row i (section 5.2) with `HP` (or `MP`), the member's current HP (or MP),
`/`, and the member's maximum HP (or maximum MP), as decimal numbers. The caller chooses HP or MP.

Example widths with the reference metrics: event choice with a 6-character option → (6+1)×17 = 119 →
128 wide, x = 256; party-list width 240 → 256.

---------------------------------------------------------------------------------------------------

## 7. The cursor

### 7.1 Position and cell

A selectable window has a cursor position (column, row), initially (0, 0) — even if row 0 is
disabled or empty. Display-only windows have no cursor. The **cursor cell** of (c, r) is the client
rectangle (c × colW, r × LineH) – (c × colW + colW, r × LineH + LineH), clipped to the client area: the
full client width, one line high, starting at the row top (1 pixel above the text top). For the paged
lists the cell covers both the name and the value of the row. The cursor keeps its row when the page
changes.

### 7.2 Opaque mode

Drawing the cursor replaces every pixel index p inside the cell by 255 − p (bitwise complement of the
8-bit index); erasing applies the same operation again. With the Windows static colours in palette
entries 0–9 and 246–255 (required, see `03-display.md`) this gives: background C0C0C0 → 808080 (dark
gray bar), black text → white, gray (disabled) text → C0C0C0, red text → dark cyan 008080. Pixels of
the window that come from other palette entries (none in opaque mode) would map to entry 255 − p.

### 7.3 Transparent mode

Drawing: the cell is filled black; then the item label of that row (label `cols × r + c + 1`) is
redrawn with a temporary colour — **gray 808080** if its colour is the window's normal colour (white),
otherwise **blue 0000FF** (e.g. a red party member) — and its colour is then restored. For slots 4, 5
and 8 the value label of the row (label 11 + r) is redrawn the same way (slot 7 is not). For slots 6 and
11 the four HP/MP column labels of the row (labels `rows + 4r + 1` .. `rows + 4r + 4`) are redrawn the
same way. Disabled labels are drawn gray as always. Erasing: the cell is restored from the background
snapshot, then **all** labels of the window are redrawn.

### 7.4 Movement

Cursor movement erases the cursor at the old cell and draws it at the new one (section 8.2 for the
keys). A target row is valid if it differs from the current row and is less than the row count. In
the paged lists all 10 rows are reachable, including empty rows on the last page; disabled items can
be passed over and highlighted.

---------------------------------------------------------------------------------------------------

## 8. Input in windows

### 8.1 Focus, activation and deactivation

* The **focused slot** is the slot of the window most recently created or activated. Keys pressed
  while windows are being used go to the window in the focused slot (section 8.3). If that slot is
  empty (for example after a pop-up closed and before its owner was re-activated), keys reach no
  window.
* A window is **active** or **inactive**. Every window is active when created. *Deactivating* a window
  (done by the game when a sub-window opens on top of it, or a pop-up belongs to it) makes it ignore
  all keys; mouse movement and clicks over it are not received by it at all but go to the game screen
  behind it (section 8.7). *Activating* a window makes it active, clears its "selection pending" flag
  (section 8.2) and makes its slot the focused slot.
* "Activate" and "deactivate" are internal notifications to the window (section 8.5).

### 8.2 Keys inside a window

A window handles a key only while active. Keys (with their numeric-keypad equivalents):

* **Esc**: sends "cancel" (slot) to the game.
* **Enter** or **Space**:
  * display-only window: sends "selected" (slot, index 0, **by mouse** — see the quirk in 8.4);
  * selectable window: index = page × 10 + row × columns + column. If 0 ≤ index < item count, and the
    item label `index mod 10 + 1` exists and is not disabled: set the window's **selection pending**
    flag and send "selected" (slot, index, by keyboard). Otherwise nothing happens.
* **Up** (or keypad 8): ignored if a selection is pending or the window has no label 0. Target row =
  row − 1, or if row < 1, row + rows − 1 (**wrap to the bottom**). Erase the cursor; if the target is
  valid (section 7.4) move there; draw the cursor.
* **Down** (or keypad 2): same with target = row + 1, or if row ≥ rows − 1, row − (rows − 1) (**wrap
  to the top**).
* **Left** (or keypad 4): only in slots 4, 5, 7, 8 and only if no selection is pending: if page > 0,
  change to the previous page (section 8.6).
* **Right** (or keypad 6): only in slots 4, 5, 7, 8, no selection pending: if page < 9 and the
  "last page reached" flag is clear, change to the next page.
* Other keys are ignored by windows.

There is no key-repeat logic of the program's own: every key-down event, including the operating
system's auto-repeat, is one key press (holding Down moves the cursor at the keyboard repeat rate).

### 8.3 Key routing by the program

This is the same procedure as `01-runtime.md` §4.2 (Key-press handling), seen from the windows' side;
the two must agree. On every key press the program does, in order (the message-processing pass and
the global keys F3, F4 and Ctrl+Break are handled before and are described in `01-runtime.md`):

1. If the key is Enter or Space and the **release latch** (`01-runtime.md` §4.3) is set, set the
   **confirm flag** (`01-runtime.md` §1), which ends the current `Wait`. If the leader is in mouse
   auto-walk, auto-walk ends (`01-runtime.md` §4.9).
2. If the field menu is not open, no event is running and no battle is running, and the key is not
   Esc: the key is a map key (`04-map-movement.md`) and processing ends here.
3. If the key is Esc, Enter or Space and the release latch is set: set the confirm flag.
4. If the key is Esc and slot 2 is empty: in battle with auto-battle on, auto-battle is switched off
   (`08-battle.md`). Then, if the field menu is not open, not in battle, no event is running and the
   leader's sub-step counter is 0, the field menu is requested (it opens at the next main-loop
   iteration, `07-menus-shop-inn.md`); otherwise the menu request is cleared.
5. Clear the release latch.
6. If the focused slot holds a window: **post** an "activate" notification to it (it is processed
   later, by the next message processing), then give the key to its key handler immediately (section
   8.2). Because the activation is only processed after the key, a key given to an inactive focused
   window is ignored but re-activates it **(quirk)**.
   If the focused slot is empty: if the key is not Enter/Space, no event is running, not in battle and
   a message window (slot 0) exists, that message window is destroyed. (The original also intended
   Enter/Space to do this, but tests the release latch it has just cleared, so Enter/Space never
   close it **(quirk)**.) In practice a message window rarely exists at this point: event messages are
   closed at the end of the event (`05-events.md` §6.4) and `Message` closes its window when nothing is
   running (section 9); the step is reached by Esc on the map or by keys while the field menu is open
   and the focused slot is empty.

### 8.4 What the game does with "selected" and "cancel"

**Selected (slot, index, by keyboard or mouse)**, processed in queue order:
1. If chosen by mouse: the pointer position at this moment, in game-screen coordinates (0..639,
   0..479), is stored as the **selection mouse position** (used to place following target windows and
   pop-ups). If chosen by keyboard the stored position is cleared ("none").
2. If the slot is 0 and slot 6 holds a window, stop (but step 1 has already happened). If the slot is
   10, stop.
3. Store `index` as the **selected index**; set the confirm flag (ends the current wait).
4. **Flush keys (wait)** (`01-runtime.md` §5.2): wait until Esc is not held, then until the release
   latch is set (if it is already set, for example after a mouse movement, there is no wait; one key
   release is enough even if other keys are still held), then discard all pending keyboard events
   (so a selection never auto-repeats into the next window). Set the confirm flag again.
5. If neither the field menu is open nor a battle is running, stop: event choices, shops and inns
   read the selected index after their wait (section 10 of this chapter; `07-menus-shop-inn.md` §8, §9).
6. Otherwise dispatch by slot: field menu tables in `07-menus-shop-inn.md`, battle in `08-battle.md`.

**Cancel (slot)**:
1. If the slot is 0 and slot 6 holds a window, or the slot is 1: ignore completely.
2. Set the confirm flag (ends the current wait).
3. If the slot is 0 and the sub-menu mode is 2 (equipment, `07-menus-shop-inn.md` §1.3): stop.
4. If neither the field menu is open nor a battle is running: stop. (So in an event choice Esc only
   ends the current wait; shop and inn waits end with selected index −1.)
5. Otherwise dispatch: `07-menus-shop-inn.md` (field), `08-battle.md` (battle).

**Quirk** (selection by Enter in a display-only window): the window reports index 0 as a mouse
selection, so step 1 stores the current pointer position as the selection mouse position even though
the player used the keyboard. A later target window or pop-up in the same menu is then placed at the
pointer's y instead of the fixed y (e.g. skipping an equipment message with Enter and then getting the
"no equippable item" pop-up).

### 8.5 Internal notifications

Windows and the game communicate through queued notifications, processed first-in first-out by the
program's message processing, which runs inside every `Wait`, inside the menu loop and in the main
loop. Waiting code that reads the selected index must see the "selected" notification produced by the
same key press before it looks at the index (the key press that sets the confirm flag also queues the
selection; both must be processed before the wait returns).

| notification | direction | parameters | effect |
|---|---|---|---|
| selected | window → game | slot, index, keyboard/mouse | section 8.4 |
| cancelled | window → game | slot | section 8.4 |
| activate | game → window | — | section 8.1; posted to the focused window before every key (processed after that key), and by the menu code |
| deactivate | game → window | — | section 8.1 |
| page changed | window → game | slot, new page, direction (back / forward) | the game refills the list for the new page (section 5.3) |
| end of list | game → window | — | disables `>>` and sets "last page reached" |

### 8.6 Page change (slots 4, 5, 7, 8)

Previous page (Left key or `<<` button): enable `>>`; page = page − 1; queue "page changed"; if page is
now 0 disable `<<`; clear "last page reached"; set label 46 to `page + 1` and label 47 to `/N )`, N being
re-read from the number after the first character of label 47's previous text.
Next page (Right key or `>>` button): enable `<<`; page = page + 1; queue "page changed"; if page is
now 9 disable `>>`; labels 46/47 as above.
The game, on "page changed", refills the rows for the new page (section 5.3); the refill sends "end
of list" when the new page holds the last entry, which disables `>>`. A page therefore never goes past
the last one, and at most 10 pages exist. The first fill at creation also sends "end of list" when the
whole list fits on page 1.

### 8.7 Mouse in windows

Mouse coordinates below are client coordinates of the window under the pointer.

* **Hover**: when the pointer moves over an active selectable window that has been painted at least
  once and has no pending selection: row = y / LineH (integer division); if that row is valid
  (section 7.4) the cursor moves to it. The column stays 0.
* **Left button down** on an active window:
  * display-only window: "selected" (slot, index 0, by mouse);
  * selectable window: index = page × 10 + current cursor row; if 0 ≤ index < item count, the item
    label `index mod 10 + 1` exists and is not disabled, and the click's y < 180: set "selection
    pending" and send "selected" (slot, index, by mouse). (The cursor is normally already on the
    clicked row because of hover.) Clicks at y ≥ 180 never select, which keeps the page-indicator line
    of the paged lists unselectable.
* **Clicking `<<` / `>>`**: exactly like the Left / Right key, except that the "selection pending" check
  is not made; keyboard focus returns to the main screen.
* **Right button down** on an active window: "cancelled" for the **focused slot** (not necessarily the
  window that was clicked).
* **Inactive windows do not receive mouse input at all** (the original disables them as
  operating-system windows, and the system then passes their mouse input to the game screen behind
  them). Mouse movement, left, right and double clicks over an inactive window are handled exactly as
  if the pointer were over the game screen outside all windows (next bullet), with the same
  coordinates **(quirk)**. Consequences: a left click on an inactive window sets the confirm flag and,
  while the field menu is open, **closes the whole menu** (or, in equipment mode with a message window,
  only destroys the message window and re-activates slot 11) — so clicking back on the main menu while
  the item list is open closes the menu; a right click on an inactive window in battle only switches
  auto-battle off instead of cancelling. **UNCERTAIN**: this follows from the operating system's rule
  for disabled child windows and was not confirmed by a side-by-side test; recommended default as
  described here.
* Clicks and mouse movement outside all windows: `01-runtime.md` §4.9 (Mouse) defines them completely
  (left button press, mouse movement, double click, right button press); in particular a left click
  outside the windows while the field menu is open closes the menu (`07-menus-shop-inn.md` §1.6).

---------------------------------------------------------------------------------------------------

## 9. Message(text, factor, position)

`Message` is the runtime's "system message" used by shops, inns, item and magic use, equipment,
battle, poison damage on the map and similar. `position` is top or bottom. Every caller in the
original passes top except the two equipment messages (`07-menus-shop-inn.md` §4), which pass
bottom; where this specification writes `Message(text, factor)` without a position, the position is
**top**. Steps:

1. `Wait(1)` (100 ms; ended early by the confirm flag).
2. If slot 0 is empty, create the message window: outer (92, 32) for top or (92, 360) for bottom,
   456 × 92, display-only. If slot 0 already holds a window it is **reused wherever it is** (its
   position is not changed, even if a different position was requested).
3. Set label 1 to `text` (no `$` code expansion is done by `Message`; callers that expand say so) and
   update the screen.
4. n = trunc(message speed × factor), where the factor is the nearest double-precision value of the
   decimal constant and the product is formed **exactly** (the original computes it in 80-bit extended
   precision) before truncation; if factor > 0 and n = 0, n = 1. `Wait(n)` (ended early by
   Enter/Space, a selection, Esc or a click).
5. If the field menu is not open, no event is running and no battle is running, destroy the message
   window (with screen update). Otherwise it stays, and the next `Message` replaces its text.

Factors used: 0.1 for prompts that are followed by a choice (shop, inn), 0.5 for most results,
1.0 for final results and multi-line reports (this chapter and `07-menus-shop-inn.md`); battle also
uses 0.2, 0.7 and 2.0 (`08-battle.md`). Resulting n for message speed s ≥ 0 (before the minimum of 1):

| factor | n |
|---|---|
| 0.1 | s / 10 (integer division; the stored constant is slightly above 0.1) |
| 0.2 | s / 5 (integer division; the stored constant is slightly above 0.2) |
| 0.5, 1.0, 2.0 | trunc(s × f), exact |
| 0.7 | 7s / 10 (integer division) if s is not a multiple of 10; **7s / 10 − 1** if s is a positive multiple of 10 (the stored constant is slightly below 0.7, so the exact product falls just below the integer) **(quirk)** |

Examples with message speed 30: 0.1 → 3, 0.5 → 15, 1.0 → 30, 0.7 → 20 (not 21); speed 20
with 0.7 → 13. (Plain double-precision arithmetic gives 21 for 30 × 0.7 and must not be used.)

---------------------------------------------------------------------------------------------------

## 10. Event messages and choices

### 10.1 Show message (event command 1)

Parameters (`05-events.md`): text string, flags (bit 0 = wait for a key, bit 1 = bottom position).

1. Target y = 360 if flag bit 1 is set, else 32. If a message window exists whose top is not at that y,
   it is destroyed (with screen update).
2. If slot 0 is empty: create the message window at x = (640 − 456) / 2 = 92, y, 456 × 92,
   display-only. (No 100 ms pre-wait, unlike `Message`.)
3. The text is expanded (`$` codes, `05-events.md`); if the expansion fails because the result would
   exceed 1024 bytes, the unexpanded text is shown instead. Label 1 gets the text; the window is
   repainted.
4. Wait:
   * if the text contained a `$W` wait code with value nn: `Wait(nn)` (nn = 0 means until a key);
   * else, if the **next** event command is a choice (command 2): no wait at all — the choice window
     appears while the message stays visible;
   * else `Wait(0)` (until a key) if flag bit 0 is set, otherwise `Wait(message speed)`.
5. The window is **not** closed. It stays visible until replaced by a message at another position,
   until a command that closes it (wait, movie, show picture, screen tone, erase screen, hero/event move
   route — `05-events.md`), or until the event ends (`05-events.md` §6.4).

### 10.2 Choice (event command 2)

Parameters: a string of up to four options separated by commas, the option count, the packed branch
labels (`05-events.md`).

1. Wait until Esc is released (no general key flush).
2. Split the string at commas (leading spaces and tabs of each option skipped), take `count` options,
   L = the largest option length in characters, measured **before** `$` expansion **(quirk)**. Join the
   options with line feeds and expand `$` codes in the joined text (if expansion fails, the unexpanded
   text is used).
3. Create the list with the list-window helper (section 6.2): x centred, y = 100, requested width 140
   (ignored because L > 0 unless all options are empty), rows = count, L, format **centre-wrap** (so pad
   0: option texts are centred in the full client width), slot 6, nothing disabled, no state colours.
   If slot 6 is already occupied the existing window is used unchanged.
4. `Wait(2)` (200 ms). Then set the selected index to −1 **(quirk: a selection made during these
   200 ms is discarded; the window keeps its "selection pending" flag, so the next arrow key is
   ignored (it only re-activates the window), while Enter/Space select again normally)**.
5. Repeat `Wait(0)` until the selected index ≠ −1. Esc and right clicks cannot end a choice.
6. Destroy slot 6 (with screen update). The result for option s is byte (3 − s) of the packed labels
   (`05-events.md`).

The choice window at y = 100 is drawn over a top message window (which spans y = 32..123) when both
are shown; this is the original look.

There is no numeric-input or name-input window in this runtime.

---------------------------------------------------------------------------------------------------

## 11. Pop-up notice (slot 12)

A pop-up shows a short notice (one line) belonging to an **owner** window, typically "you have no
items". Parameters: owner slot, text.

1. Position: x = owner's outer x + 60; y = owner's outer y + 10 — or, if a selection mouse position is
   stored (section 8.4), y = the stored pointer y itself (game-screen coordinates; **no** +10 in this
   case).
2. Size: width = (number of characters of the text) × W, no padding and no rounding; height =
   LineH + 14 (= 6 for the frame + LineH + 8). Example: 12 characters → 204 × 32.
3. Post "deactivate" to the owner; create the pop-up (it becomes the focused slot); set label 1
   (section 5.8).
4. `Wait(15)` (1.5 s, ended early by Enter/Space, Esc, a selection or a click). The deactivation is
   processed during this wait.
5. Destroy the pop-up (with screen update), then post "activate" to the owner.

If the owner slot is empty (not expected) the position is computed from (0, 0).

---------------------------------------------------------------------------------------------------

## 12. Version information box

Opened from the system menu (`07-menus-shop-inn.md`). In the original it is a small box of the
operating system's dialog kind, not one of the 13 window slots; an implementation may draw it as a
window (for example in slot 12) as long as the contents and behaviour match.

* The box has a dialog frame and no title bar, light gray C0C0C0 background, and uses MS PGothic at
  12 points in regular weight (the dialog font, not the window font).
* Template (dialog units; the operating system converts them to pixels from the dialog font's average
  character size): position (50, 33) relative to the game screen, size 143 × 89. Contents:

  | element | position | size | alignment | text |
  |---|---|---|---|---|
  | icon (the program icon) | (8, 10) | 16 × 16 | — | — |
  | static text | (46, 8) | 60 × 8 | left | `ＲＰＧツクール９５` |
  | static text | (29, 22) | 95 × 8 | centred | `Runtime Routine  ver 1.23` (note: **two** spaces before `ver`) |
  | static text | (2, 40) | 140 × 28 | centred | `Copyright (c) 1997 by ASCII Corporation` / `Copyright (c) 1997 by You & Tsune` / `All Rights Reserved.` (three lines) |
  | static text | (106, 55) | 66 × 8 | right | empty |
  | default push button | (52, 72) | 40 × 14 | centred | `OK` |

  The second and third static texts are filled when the box opens: the second is product name
  `Runtime Routine`, a space, the template's own (empty) text, a space and the version `ver 1.23` — hence
  the two spaces; the copyright block replaces the template's text (which differs slightly and is never
  seen). The centred static texts show `&` literally (no prefix processing, unlike window labels,
  section 3.3).

* On the reference screen this came out at screen pixels (125, 70) – (482, 258) (about 358 × 189
  pixels: 2.5 pixels per horizontal and 2.125 per vertical dialog unit); first line of text at
  y ≈ 89, second at y ≈ 119, the copyright block at y ≈ 157..205, the OK button at (255, 223) –
  (354, 252). **UNCERTAIN**: exact pixel geometry on Windows 95 with the real font.
* While the box is open the program behaves as if an event were running (no map input, Esc does not
  open the menu). Clicking OK or pressing Enter (OK is the default button) closes it; the game then
  continues on the map. **UNCERTAIN**: whether Esc also closes it in the original (standard dialog
  behaviour suggests it does).

---------------------------------------------------------------------------------------------------

## 13. Summary of all window uses

Outer rectangles (x, y, w × h). "rounded" = width rounding of section 6.2; LineH = 18, W = 17 in
the examples. `y0` = the stored selection mouse y if the window was opened by a mouse selection,
otherwise 100 in the field and 250 in battle.

| slot | use | rectangle | created by |
|---|---|---|---|
| 0 | event message | (92, 32 or 360) 456 × 92 | section 10.1 |
| 0 | `Message` | (92, 32 or 360) 456 × 92, only if none exists | section 9 |
| 0 | battle messages | (92, 10) 456 × 92 | `08-battle.md` |
| 1 | battle status | (20, 362) 600 × 100 | `08-battle.md` |
| 2 | field main menu | (40, 110) (max(4, L) + 1) × W × (6 × LineH + 6), not rounded; L = longest of strings[0..4] | `07-menus-shop-inn.md` |
| 2 | battle command menu | (10, 280) (L + 1) × W × (5 × LineH + 6), not rounded; L = longest of strings[0, 1, 5, 6, 7] | `08-battle.md` |
| 4 | magic list (field) | (140, 80) 240 × 210 | `07` |
| 4 | magic list (battle) | (80, 170) 240 × 210 | `08` |
| 5 | item list (field) | (120, 80) 240 × 210 | `07` |
| 5 | item list (battle) | (80, 170) 240 × 210 | `08` |
| 5 | equipment candidates | (180, 130) 240 × 210 | `07` |
| 6 | title menu | helper: x centred, y from the title picture, width 140 → 160, 2 rows, centre-wrap | `01-runtime.md` |
| 6 | event choice | helper: centred, y 100, (L + 1) × W rounded, centre-wrap | section 10.2 |
| 6 | shop command | helper: centred, y 102, (max(6, L) + 1) × W rounded, centre-wrap | `07` |
| 6 | inn command | helper: centred, y 100, (L + 1) × W rounded, centre-wrap | `07` |
| 6 | magic caster | party-list helper (90, 105), 240 → 256, MP column | `07` |
| 6 | equipment member | helper (80, 105), 160 → 192, coloured by state | `07` |
| 6 | status member | helper (80, 105), 140 → 160, coloured by state | `07` |
| 6 | party order member | helper (80, 180), 160 → 192, coloured by state | `07` |
| 6 | system menu | helper (80, 180), 160 → 192, 5 rows | `07` |
| 6 | battle enemy target (attack) | helper (90, y0 − LineH × n / 2 − 10), 160 → 192 | `08` |
| 7 | teleport destinations | (180, 140) 240 × 210 | `07`, `08` |
| 8 | shop list | (200, 102) 240 × 210 | `07` |
| 9 | status screen | (136, 80) 450 × 230 | `07` |
| 10 | party order display | (200, 190 − h) 200 × h, h = LineH × (n + 2) + 6 | `07` |
| 11 | item / magic target (field) | party-list helper (180, y0 − (LineH × n) / 2 − 10), 240 → 256, HP or MP column | `07` |
| 11 | equipment slot | helper (110, 115), 160 → 192, 5 rows | `07` |
| 11 | battle target lists | helper (180, y0 − (LineH × n) / 2 − 10) | `08` |
| 12 | pop-up | section 11 | |

All helper-created windows use item format left-wrap unless stated (centre-wrap for the title menu,
choices, shop and inn commands). Item format of the main menu: left-wrap (pad 10).

Reference outer rectangles measured on the original under a substitute font with LineH 19 and W 24
(useful to check the formulas, not the absolute values): title menu (240, 360) 160 × 44; event message
(92, 32) 456 × 92 and (92, 360); event choice with a 6-character option (224, 100) 192 × 63; shop
command (224, 102) 192 × 63; inn command (240, 100) 160 × 44; main menu (40, 110) 144 × 120; item list
(120, 80) 240 × 210 with buttons at screen (268, 265) and (313, 265); item target (180, 62) 256 × 63;
magic caster (90, 105) 256 × 63; pop-up (150, 115) 360 × 33; equipment member (80, 105) 192 × 63;
equipment slots (110, 115) 192 × 101; status member (80, 105) 160 × 63; status (136, 80) 450 × 230;
order display (200, 89) 200 × 101; shop list (200, 102) 240 × 210.

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* Width rounding always adds at least one pixel and up to 32: an exact multiple of 32 grows by 32 (§6.2).
* `Message` reuses an existing message window at its current position, ignoring the requested position (§9).
* Paged-list name labels overlap the value labels; long names are clipped at x = 147 and overdrawn (§5.2).
* List and HP/MP rectangles may extend past the client edge and are clipped (§5.2, §5.6).
* The choice width is computed from the option lengths before `$` expansion (§10.2).
* A choice made in the first 200 ms of a choice window is discarded (§10.2).
* Esc and right click cannot cancel an event choice (§10.2).
* `&` in any window label is a mnemonic prefix: it is hidden and underlines the next character; `&&` shows `&`; width counts include the `&` (§3.3).
* Window darkening for 0.7 / 0.6 follows the integer rules of `03-display.md` §9, not plain truncated products (§4.2).
* `Message` waits: for factor 0.7 the wait is one tenth shorter when the message speed is a multiple of 10 (§9).
* A key given to an inactive focused window is ignored but re-activates it (§8.3).
* Enter/Space never close a lingering message window; other keys do (§8.3).
* Enter in a display-only window is reported as a mouse selection and stores the pointer position (§8.4).
* A right click on an active window cancels the focused window, not the clicked one (§8.7).
* Mouse input over an inactive window goes to the game screen: a left click on it closes the field menu (§8.7).
* The pop-up is placed at the selection mouse y without the +10 used for the keyboard case (§11).
* The version box shows `Runtime Routine  ver 1.23` with two spaces (§12).
* Clicks with client y ≥ 180 never select (§8.7).
* The transparent cursor redraws the value label for slots 4, 5, 8 but not 7 (§7.3).
* The transparent background is a one-time snapshot including the windows below; it does not follow later scene changes (§4.2).
* Page-indicator total is re-parsed from its own label text on every page change (§8.6).
* The cursor starts on row 0 even if that row is disabled, and may rest on disabled or empty rows (§7.1, §7.4).

## Open questions

* Exact Windows 95 metrics of MS PGothic, character height 16, bold: the original derives LineH and W
  from the font; 16 / 14 / 17 are the expected values but were never measured on the real font (the
  reference screenshots were taken with a substitute font: text height 17, LineH 19, W 24). W = 16 is
  the main alternative (§2).
* Exact row and length of the `&` underline (§3.3).
* Mouse input over inactive windows going to the game screen (§8.7) follows from the operating
  system's rule for disabled child windows; not confirmed side by side.
* Whether the original's line breaking applies any Japanese line-breaking rules or breaks long ASCII
  words differently from §3.3.
* Caption font and size of the `<<` / `>>` buttons.
* Exact pixel geometry of the version box on Windows 95 (§12).
* Texts longer than a label's capacity (only possible with malformed data) were not guarded in the
  original; their effect is undefined.
