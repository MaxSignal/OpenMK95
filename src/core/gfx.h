// 8-bit surfaces, palettes, bitmap decoding and copy operations (03-display.md §1-§3).
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "core/common.h"

struct RGB { uint8_t r, g, b; };
inline bool operator==(const RGB& a, const RGB& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
inline bool operator!=(const RGB& a, const RGB& b) { return !(a == b); }

struct Palette {
    RGB c[256];
};

struct Rect {
    int l = 0, t = 0, r = 0, b = 0;
    Rect() {}
    Rect(int l_, int t_, int r_, int b_) : l(l_), t(t_), r(r_), b(b_) {}
    static Rect xywh(int x, int y, int w, int h) { return Rect(x, y, x + w, y + h); }
    int w() const { return r - l; }
    int h() const { return b - t; }
};

struct Surface {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    int key = -1;   // colour key index or -1
    Surface() {}
    Surface(int w_, int h_, uint8_t fill = 0) { create(w_, h_, fill); }
    void create(int w_, int h_, uint8_t fill = 0) { w = w_; h = h_; px.assign((size_t)w * h, fill); key = -1; }
    uint8_t* row(int y) { return &px[(size_t)y * w]; }
    const uint8_t* row(int y) const { return &px[(size_t)y * w]; }
    bool empty() const { return w <= 0 || h <= 0; }
    void fill(uint8_t v) { std::fill(px.begin(), px.end(), v); }
    void fillRect(Rect r, uint8_t v);
};

// Decoded Windows bitmap.
struct Bitmap {
    int w = 0, h = 0, bpp = 0;
    bool header40 = false;
    int nTable = 0;                 // colour-table entries (palette source N, see paletteCount())
    std::vector<RGB> table;
    std::vector<uint8_t> idx;       // bpp <= 8: palette indices, top row first
    std::vector<uint32_t> rgb;      // bpp > 8: 0xRRGGBB, top row first
    bool indexed() const { return bpp <= 8; }
    // N of 03-display.md §2.3 / 02-data-formats.md §8.3.
    int paletteCount() const { return (header40 && bpp <= 8) ? nTable : 0; }
};

bool decodeBmp(const std::vector<uint8_t>& file, Bitmap& out);

// Nearest palette index (squared distance, lowest index on ties).
int nearestIndex(const Palette& pal, int r, int g, int b);
// Convert a bitmap into a surface matching its colours against `pal`.
void convertBitmap(const Bitmap& bmp, const Palette& pal, Surface& out);
// The same for a raw 8-bit image with its own 256-entry table.
void convertIndexed(int w, int h, const uint8_t* idxTopDown, const RGB* table, int ntable, const Palette& pal, Surface& out);

// ---- copies (03-display.md §1.2) ----
bool rectInside(const Rect& r, int w, int h);
// All-or-nothing copies: nothing is copied if a rectangle is not completely inside.
void blitOpaque(const Surface& src, Rect sr, Surface& dst, int dx, int dy);
void blitKeyed(const Surface& src, Rect sr, Surface& dst, int dx, int dy, int key);
// Stretched copy (nearest neighbour), key < 0 = opaque.
void blitStretch(const Surface& src, Rect sr, Surface& dst, Rect dr, int key);
// Copy with ordinary clipping (image loading, battle background).
void blitClipped(const Surface& src, Surface& dst, int dx, int dy);

extern const RGB kSystemColors[20];  // indices 0..9 and 246..255
