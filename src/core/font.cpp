#include "core/font.h"
#include "core/sjis.h"

namespace {
#include "core/gen/font_data.inc"
}

namespace font {

// Shift_JIS double-byte code -> JIS X 0208 row/cell (1-based), or false.
static bool sjisToJis(int code, int& row, int& cell) {
    int s1 = code >> 8, s2 = code & 0xFF;
    if (s2 < 0x40 || s2 == 0x7F || s2 > 0xFC) return false;
    int r;
    if (s1 >= 0x81 && s1 <= 0x9F) r = (s1 - 0x81) * 2 + 1;
    else if (s1 >= 0xE0 && s1 <= 0xEF) r = (s1 - 0xE0) * 2 + 63;
    else return false;
    int c;
    if (s2 >= 0x9F) { r += 1; c = s2 - 0x9E; }
    else { c = s2 - 0x3F; if (s2 > 0x7F) c -= 1; }
    if (r < 1 || r > 94 || c < 1 || c > 94) return false;
    row = r; cell = c;
    return true;
}

int advance(int code) {
    if (code > 0xFF) return 17;
    if (code < 0x20 || code == 0x7F) return 0;
    return 9;
}

int advanceRegular(int code) {
    int a = advance(code);
    return a > 0 ? a - 1 : 0;
}

static inline void plot(uint8_t* buf, int bw, int bh, const Rect& clip, int x, int y, uint8_t color) {
    if (x < clip.l || x >= clip.r || y < clip.t || y >= clip.b) return;
    if (x < 0 || y < 0 || x >= bw || y >= bh) return;
    buf[(size_t)y * bw + x] = color;
}

void drawChar(uint8_t* buf, int bw, int bh, const Rect& clip, int x, int top, int code, uint8_t color, bool bold) {
    if (code > 0xFF) {
        int row, cell;
        if (!sjisToJis(code, row, cell)) return;
        int gi = (row - 1) * 94 + (cell - 1);
        if (!kFullPresent[gi]) return;
        const unsigned char* g = kFullGlyphs[gi];
        for (int y = 0; y < 16; y++) {
            unsigned v = ((unsigned)g[2 * y] << 8) | g[2 * y + 1];
            // synthetic bold: every set pixel is also set one pixel to its right
            unsigned bv = bold ? (v | (v >> 1)) : v;
            int n = bold ? 17 : 16;
            for (int b = 0; b < n; b++)
                if (b < 16 ? ((bv >> (15 - b)) & 1) : (v & 1)) plot(buf, bw, bh, clip, x + b, top + y, color);
        }
    } else {
        if (!kHalfPresent[code & 0xFF]) return;
        const unsigned char* g = kHalfGlyphs[code & 0xFF];
        for (int y = 0; y < 16; y++) {
            unsigned v = g[y];
            unsigned bv = bold ? (v | (v >> 1)) : v;
            int n = bold ? 9 : 8;
            for (int b = 0; b < n; b++)
                if (b < 8 ? ((bv >> (7 - b)) & 1) : (v & 1)) plot(buf, bw, bh, clip, x + b, top + y, color);
        }
    }
}

int textWidth(const sjis& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) w += advance(sj::next(s, i));
    return w;
}

}  // namespace font
