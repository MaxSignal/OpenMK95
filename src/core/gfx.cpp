#include "core/gfx.h"
#include <algorithm>
#include <unordered_map>

const RGB kSystemColors[20] = {
    {0x00, 0x00, 0x00}, {0x80, 0x00, 0x00}, {0x00, 0x80, 0x00}, {0x80, 0x80, 0x00}, {0x00, 0x00, 0x80},
    {0x80, 0x00, 0x80}, {0x00, 0x80, 0x80}, {0xC0, 0xC0, 0xC0}, {0xC0, 0xDC, 0xC0}, {0xA6, 0xCA, 0xF0},
    {0xFF, 0xFB, 0xF0}, {0xA0, 0xA0, 0xA4}, {0x80, 0x80, 0x80}, {0xFF, 0x00, 0x00}, {0x00, 0xFF, 0x00},
    {0xFF, 0xFF, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0xFF}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
};

void Surface::fillRect(Rect r, uint8_t v) {
    r.l = std::max(r.l, 0); r.t = std::max(r.t, 0);
    r.r = std::min(r.r, w); r.b = std::min(r.b, h);
    for (int y = r.t; y < r.b; y++) memset(row(y) + r.l, v, (size_t)std::max(0, r.r - r.l));
}

// ------------------------------------------------------------------ BMP
bool decodeBmp(const std::vector<uint8_t>& f, Bitmap& out) {
    out = Bitmap();
    if (f.size() < 26 || f[0] != 'B' || f[1] != 'M') return false;
    const uint8_t* d = f.data();
    uint32_t offBits = rd32(d + 10);
    uint32_t hsize = rd32(d + 14);
    int w, h, planes, bpp;
    uint32_t comp = 0, clrUsed = 0, imageSize = 0;
    size_t tablePos = 14 + hsize;
    int entrySize;
    uint32_t masks[3] = {0, 0, 0};
    bool hasMasks = false;
    if (hsize == 12) {
        if (f.size() < 26) return false;
        // OS/2 core header: u16 width, u16 height (02-data-formats.md §8.2 rule 2)
        w = rd16(d + 18); h = rd16(d + 20); planes = rd16(d + 22); bpp = rd16(d + 24);
        entrySize = 3;
    } else if (hsize >= 40) {
        if (f.size() < 54) return false;
        w = rds32(d + 18); h = rds32(d + 22); planes = rd16(d + 26); bpp = rd16(d + 28);
        comp = rd32(d + 30); imageSize = rd32(d + 34); clrUsed = rd32(d + 46);
        entrySize = 4;
        out.header40 = (hsize == 40);
        if (comp == 3) {  // bit fields
            if (hsize >= 52 && f.size() >= 14 + 52) { masks[0] = rd32(d + 54); masks[1] = rd32(d + 58); masks[2] = rd32(d + 62); }
            else if (f.size() >= 14 + hsize + 12) { masks[0] = rd32(d + 14 + hsize); masks[1] = rd32(d + 18 + hsize); masks[2] = rd32(d + 22 + hsize); tablePos += 12; }
            hasMasks = true;
        }
    } else return false;
    if (planes != 1 || w <= 0 || h == 0) return false;
    bool topDown = h < 0;
    if (topDown) h = -h;
    // colour table length (02-data-formats.md §8.2 rule 4)
    uint32_t tableLen = 0;
    if (hsize == 12) tableLen = bpp <= 8 ? (1u << bpp) : 0;
    else if (clrUsed) tableLen = clrUsed;
    else tableLen = bpp <= 8 ? (1u << bpp) : 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return false;
    size_t tableEnd = tablePos + (size_t)tableLen * entrySize;  // the whole table, also beyond 256 entries
    int ntable = (int)std::min<uint32_t>(tableLen, 256);        // palette entries i <= 255 (03-display.md §2.3)
    out.w = w; out.h = h; out.bpp = bpp;
    out.nTable = bpp <= 8 ? ntable : 0;
    // colour table
    if (bpp <= 8)
        for (int i = 0; i < ntable; i++) {
            size_t p = tablePos + (size_t)i * entrySize;
            RGB c{0, 0, 0};
            if (p + 3 <= f.size()) c = RGB{d[p + 2], d[p + 1], d[p]};
            out.table.push_back(c);
        }
    size_t dataPos = offBits > tableEnd ? offBits : tableEnd;
    if (dataPos > f.size()) dataPos = f.size();
    const uint8_t* pix = d + dataPos;
    size_t avail = f.size() - dataPos;
    (void)imageSize;
    int stride = ((w * bpp + 31) / 32) * 4;
    auto dstRow = [&](int y) { return topDown ? y : h - 1 - y; };  // file row -> image row
    if (bpp <= 8) {
        out.idx.assign((size_t)w * h, 0);
        if (comp == 0 || hsize == 12) {
            for (int y = 0; y < h; y++) {
                size_t rp = (size_t)y * stride;
                uint8_t* o = &out.idx[(size_t)dstRow(y) * w];
                for (int x = 0; x < w; x++) {
                    uint8_t v = 0;
                    if (bpp == 8) { if (rp + x < avail) v = pix[rp + x]; }
                    else if (bpp == 4) { size_t q = rp + x / 2; if (q < avail) v = (x & 1) ? (pix[q] & 15) : (pix[q] >> 4); }
                    else { size_t q = rp + x / 8; if (q < avail) v = (pix[q] >> (7 - (x & 7))) & 1; }
                    o[x] = v;
                }
            }
        } else if (comp == 1 || comp == 2) {  // RLE8 / RLE4 (always bottom-up)
            int x = 0, y = 0;
            size_t p = 0;
            auto put = [&](int v) {
                if (x < w && y < h) out.idx[(size_t)(h - 1 - y) * w + x] = (uint8_t)v;
                x++;
            };
            while (p + 1 < avail) {
                int a = pix[p], b = pix[p + 1];
                p += 2;
                if (a > 0) {
                    for (int i = 0; i < a; i++) put(comp == 1 ? b : ((i & 1) ? (b & 15) : (b >> 4)));
                } else if (b == 0) { x = 0; y++; }
                else if (b == 1) break;
                else if (b == 2) { if (p + 1 < avail) { x += pix[p]; y += pix[p + 1]; } p += 2; }
                else {
                    if (comp == 1) {
                        for (int i = 0; i < b; i++) put(p + i < avail ? pix[p + i] : 0);
                        p += (size_t)b + (b & 1);
                    } else {
                        int bytes = (b + 1) / 2;
                        for (int i = 0; i < b; i++) { uint8_t v = p + i / 2 < avail ? pix[p + i / 2] : 0; put((i & 1) ? (v & 15) : (v >> 4)); }
                        p += (size_t)bytes + (bytes & 1);
                    }
                }
            }
        } else return false;
    } else {
        out.rgb.assign((size_t)w * h, 0);
        if (comp != 0 && comp != 3) return false;
        if (!hasMasks && bpp == 16) { masks[0] = 0x7C00; masks[1] = 0x03E0; masks[2] = 0x001F; }
        if (!hasMasks && bpp == 32) { masks[0] = 0xFF0000; masks[1] = 0xFF00; masks[2] = 0xFF; }
        auto ext = [](uint32_t v, uint32_t m) -> int {
            if (!m) return 0;
            int sh = 0; while (!(m & 1)) { m >>= 1; sh++; }
            int bits = 0; uint32_t mm = m; while (mm & 1) { mm >>= 1; bits++; }
            uint32_t x = (v >> sh) & m;
            if (bits >= 8) return (int)(x >> (bits - 8));
            return (int)((x * 255 + (m / 2)) / m);
        };
        for (int y = 0; y < h; y++) {
            size_t rp = (size_t)y * stride;
            uint32_t* o = &out.rgb[(size_t)dstRow(y) * w];
            for (int x = 0; x < w; x++) {
                uint32_t r = 0, g = 0, b = 0;
                if (bpp == 24) {
                    size_t q = rp + (size_t)x * 3;
                    if (q + 2 < avail) { b = pix[q]; g = pix[q + 1]; r = pix[q + 2]; }
                } else if (bpp == 16) {
                    size_t q = rp + (size_t)x * 2;
                    if (q + 1 < avail) { uint32_t v = rd16(pix + q); r = ext(v, masks[0]); g = ext(v, masks[1]); b = ext(v, masks[2]); }
                } else {
                    size_t q = rp + (size_t)x * 4;
                    if (q + 3 < avail) { uint32_t v = rd32(pix + q); r = ext(v, masks[0]); g = ext(v, masks[1]); b = ext(v, masks[2]); }
                }
                o[x] = (r << 16) | (g << 8) | b;
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------ colour matching
int nearestIndex(const Palette& pal, int r, int g, int b) {
    int best = 0, bd = 0x7fffffff;
    for (int i = 0; i < 256; i++) {
        int dr = pal.c[i].r - r, dg = pal.c[i].g - g, db = pal.c[i].b - b;
        int dd = dr * dr + dg * dg + db * db;
        if (dd < bd) { bd = dd; best = i; if (!dd) break; }
    }
    return best;
}

void convertIndexed(int w, int h, const uint8_t* idx, const RGB* table, int ntable, const Palette& pal, Surface& out) {
    out.create(w, h);
    uint8_t map[256];
    for (int i = 0; i < 256; i++) {
        RGB c = i < ntable ? table[i] : RGB{0, 0, 0};
        map[i] = (uint8_t)nearestIndex(pal, c.r, c.g, c.b);
    }
    for (size_t i = 0; i < (size_t)w * h; i++) out.px[i] = map[idx[i]];
}

void convertBitmap(const Bitmap& bmp, const Palette& pal, Surface& out) {
    if (bmp.indexed()) {
        convertIndexed(bmp.w, bmp.h, bmp.idx.data(), bmp.table.data(), (int)bmp.table.size(), pal, out);
        return;
    }
    out.create(bmp.w, bmp.h);
    std::unordered_map<uint32_t, uint8_t> cache;
    for (size_t i = 0; i < bmp.rgb.size(); i++) {
        uint32_t c = bmp.rgb[i];
        auto it = cache.find(c);
        if (it == cache.end()) {
            uint8_t v = (uint8_t)nearestIndex(pal, (c >> 16) & 255, (c >> 8) & 255, c & 255);
            it = cache.emplace(c, v).first;
        }
        out.px[i] = it->second;
    }
}

// ------------------------------------------------------------------ copies
bool rectInside(const Rect& r, int w, int h) {
    return r.l >= 0 && r.t >= 0 && r.r <= w && r.b <= h && r.l <= r.r && r.t <= r.b;
}

void blitOpaque(const Surface& src, Rect sr, Surface& dst, int dx, int dy) {
    if (!rectInside(sr, src.w, src.h)) return;
    Rect dr = Rect::xywh(dx, dy, sr.w(), sr.h());
    if (!rectInside(dr, dst.w, dst.h)) return;
    for (int y = 0; y < sr.h(); y++) memmove(dst.row(dy + y) + dx, src.row(sr.t + y) + sr.l, (size_t)sr.w());
}

void blitKeyed(const Surface& src, Rect sr, Surface& dst, int dx, int dy, int key) {
    if (!rectInside(sr, src.w, src.h)) return;
    Rect dr = Rect::xywh(dx, dy, sr.w(), sr.h());
    if (!rectInside(dr, dst.w, dst.h)) return;
    for (int y = 0; y < sr.h(); y++) {
        const uint8_t* s = src.row(sr.t + y) + sr.l;
        uint8_t* o = dst.row(dy + y) + dx;
        for (int x = 0; x < sr.w(); x++)
            if (s[x] != key) o[x] = s[x];
    }
}

void blitStretch(const Surface& src, Rect sr, Surface& dst, Rect dr, int key) {
    if (!rectInside(sr, src.w, src.h) || !rectInside(dr, dst.w, dst.h)) return;
    int sw = sr.w(), sh = sr.h(), dw = dr.w(), dh = dr.h();
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    std::vector<uint8_t> tmp;
    const Surface* s = &src;
    Surface copy;
    if (&src == &dst) { copy = src; s = &copy; }
    for (int y = 0; y < dh; y++) {
        int syy = sr.t + (int)((int64_t)y * sh / dh);
        const uint8_t* srow = s->row(syy);
        uint8_t* o = dst.row(dr.t + y) + dr.l;
        for (int x = 0; x < dw; x++) {
            uint8_t v = srow[sr.l + (int)((int64_t)x * sw / dw)];
            if (key < 0 || v != key) o[x] = v;
        }
    }
}

void blitClipped(const Surface& src, Surface& dst, int dx, int dy) {
    for (int y = 0; y < src.h; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= dst.h) continue;
        int x0 = std::max(0, -dx), x1 = std::min(src.w, dst.w - dx);
        if (x1 > x0) memcpy(dst.row(ty) + dx + x0, src.row(y) + x0, (size_t)(x1 - x0));
    }
}
