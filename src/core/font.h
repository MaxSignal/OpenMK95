// Window font: Shinonome 16px bitmap glyphs with synthetic emboldening, metrics of
// 06-windows-text.md §2 (text height 16, ascent 14, W = 17, LineH = 18).
#pragma once
#include <cstdint>
#include "core/common.h"
#include "core/gfx.h"

namespace font {

const int TextH = 16;
const int Ascent = 14;
const int MaxW = 17;  // W
const int LineH = TextH + 2;

// Advance width of one Shift_JIS character code (single byte or lead<<8|trail).
int advance(int code);
// Draw one character with its cell top-left at (x, top) into an 8-bit buffer (the
// baseline is at top + Ascent). bold = synthetic emboldening of §2 (the window font);
// bold = false draws the regular-weight glyph (used for the page-button captions, §4.5).
void drawChar(uint8_t* buf, int bw, int bh, const Rect& clip, int x, int top, int code, uint8_t color,
              bool bold = true);
// Advance of the regular (non-bold) weight: one pixel less than advance() for drawn glyphs.
int advanceRegular(int code);
// Width in pixels of a run of text (no prefix processing).
int textWidth(const sjis& s);

}  // namespace font
