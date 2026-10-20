// Cinepak ('cvid') video decoder.
//
// Frame:  flags(1) length(3, BE) width(2) height(2) strips(2), then the strips.
// Strip:  id(2) size(2, includes the 12-byte header) y1(2) x1(2) y2(2) x2(2), then chunks.
//         y1 == 0 means the strip starts below the previous one and y2 is its height;
//         otherwise y1/y2 are absolute.
// Chunk:  id(2) size(2, includes the 4-byte header), then data.
//   0x20xx..0x27xx codebooks: bit 0x0200 = V1 (else V4), 0x0100 = partial update
//                  (32-bit BE flag word per 32 entries), 0x0400 = 4-byte greyscale
//                  entries (else 6-byte Y0 Y1 Y2 Y3 U V entries).
//   0x30xx image data, blocks with V1/V4 flag bits; 0x31xx inter (coded bit, then
//          V1/V4 bit); 0x32xx all V1.
// Each strip keeps its own V1/V4 codebooks across frames. When frame flag bit 0
// is clear, strip i>0 starts from a copy of strip i-1's codebooks.
#include "movie/movie_codecs.h"

#include <algorithm>
#include <cstring>

namespace movie {
namespace detail {
namespace {

struct Codebooks {
    // Each entry is a 2x2 pixel block: [0]=top-left [1]=top-right [2]=bottom-left [3]=bottom-right.
    uint32_t v1[256][4];
    uint32_t v4[256][4];
};

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

class CinepakDecoder : public VideoDecoder {
public:
    CinepakDecoder(int w, int h, int bitCount) : VideoDecoder(w, h), palettised_(bitCount == 8) {}

    bool decode(const uint8_t* data, size_t size, std::vector<uint32_t>& fb) override {
        if (size < 10) return false;
        const uint8_t* end = data + size;
        unsigned flags = data[0];
        size_t len = rd24be(data + 1);
        if (len >= 10 && len < size) end = data + len;
        unsigned numStrips = rd16be(data + 8);
        const uint8_t* p = data + 10;
        int y0 = 0;
        bool ok = true;
        for (unsigned i = 0; i < numStrips; i++) {
            if (end - p < 12) { ok = false; break; }
            if (i >= strips_.size()) {
                if (i >= 64) { ok = false; break; }
                strips_.emplace_back();
                std::memset(&strips_.back(), 0, sizeof(Codebooks));
            }
            if (i > 0 && !(flags & 1)) strips_[i] = strips_[i - 1];
            size_t ssize = rd16be(p + 2);
            if (ssize < 12 || ssize > size_t(end - p)) {
                ssize = size_t(end - p);
                ok = false;
            }
            int sy1 = int(rd16be(p + 4)), sx1 = int(rd16be(p + 6));
            int sy2 = int(rd16be(p + 8)), sx2 = int(rd16be(p + 10));
            int top, bottom;
            if (sy1 == 0) {
                top = y0;
                bottom = y0 + sy2;
            } else {
                top = sy1;
                bottom = sy2;
            }
            if (sx2 == 0) sx2 = w_;
            y0 = bottom;
            if (!decodeStrip(strips_[i], p + 12, p + ssize, sx1, top, sx2, bottom, fb)) ok = false;
            p += ssize;
        }
        return ok;
    }

private:
    bool palettised_;
    std::vector<Codebooks> strips_;

    uint32_t yuv(int y, int u, int v) const {
        int r = clamp255(y + 2 * v);
        int g = clamp255(y - u / 2 - v);
        int b = clamp255(y + 2 * u);
        return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }

    void readEntry(const uint8_t* e, bool grey, uint32_t out[4]) const {
        if (palettised_) {
            // palette indices, resolved when drawing
            for (int k = 0; k < 4; k++) out[k] = e[k];
        } else if (grey) {
            for (int k = 0; k < 4; k++) {
                uint32_t y = e[k];
                out[k] = (y << 16) | (y << 8) | y;
            }
        } else {
            int u = int8_t(e[4]), v = int8_t(e[5]);
            for (int k = 0; k < 4; k++) out[k] = yuv(e[k], u, v);
        }
    }

    bool decodeCodebook(uint32_t (*cb)[4], unsigned id, const uint8_t* p, const uint8_t* end) const {
        bool partial = (id & 0x0100) != 0;
        bool grey = (id & 0x0400) != 0;
        size_t esz = grey ? 4 : 6;
        uint32_t flag = 0, mask = 0;
        for (int i = 0; i < 256; i++) {
            if (partial) {
                mask >>= 1;
                if (!mask) {
                    if (end - p < 4) return true;
                    flag = rd32be(p);
                    p += 4;
                    mask = 0x80000000u;
                }
                if (!(flag & mask)) continue;
            }
            if (size_t(end - p) < esz) return true;
            readEntry(p, grey, cb[i]);
            p += esz;
        }
        return true;
    }

    uint32_t px(uint32_t v) const { return palettised_ ? pal_[v & 0xFF] : v; }

    void putV1(const uint32_t e[4], int x, int y, int x2, int y2, std::vector<uint32_t>& fb) const {
        for (int dy = 0; dy < 4; dy++) {
            int yy = y + dy;
            if (yy >= y2 || yy >= h_) break;
            uint32_t* row = &fb[size_t(yy) * w_];
            for (int dx = 0; dx < 4; dx++) {
                int xx = x + dx;
                if (xx >= x2 || xx >= w_) break;
                row[xx] = px(e[(dy >> 1) * 2 + (dx >> 1)]);
            }
        }
    }

    void putV4(const Codebooks& cb, const uint8_t idx[4], int x, int y, int x2, int y2,
               std::vector<uint32_t>& fb) const {
        for (int dy = 0; dy < 4; dy++) {
            int yy = y + dy;
            if (yy >= y2 || yy >= h_) break;
            uint32_t* row = &fb[size_t(yy) * w_];
            for (int dx = 0; dx < 4; dx++) {
                int xx = x + dx;
                if (xx >= x2 || xx >= w_) break;
                const uint32_t* e = cb.v4[idx[(dy >> 1) * 2 + (dx >> 1)]];
                row[xx] = px(e[(dy & 1) * 2 + (dx & 1)]);
            }
        }
    }

    bool decodeVectors(const Codebooks& cb, unsigned id, const uint8_t* p, const uint8_t* end, int x1,
                       int y1, int x2, int y2, std::vector<uint32_t>& fb) const {
        unsigned kind = (id >> 8) & 0x03;  // bit0: inter (coded flags), bit1: all V1
        uint32_t flag = 0, mask = 0;
        for (int y = y1; y < y2; y += 4) {
            for (int x = x1; x < x2; x += 4) {
                if (kind & 1) {
                    mask >>= 1;
                    if (!mask) {
                        if (end - p < 4) return false;
                        flag = rd32be(p);
                        p += 4;
                        mask = 0x80000000u;
                    }
                    if (!(flag & mask)) continue;  // block unchanged
                }
                bool v4 = false;
                if (!(kind & 2)) {
                    mask >>= 1;
                    if (!mask) {
                        if (end - p < 4) return false;
                        flag = rd32be(p);
                        p += 4;
                        mask = 0x80000000u;
                    }
                    v4 = (flag & mask) != 0;
                }
                if (v4) {
                    if (end - p < 4) return false;
                    putV4(cb, p, x, y, x2, y2, fb);
                    p += 4;
                } else {
                    if (end - p < 1) return false;
                    putV1(cb.v1[*p], x, y, x2, y2, fb);
                    p += 1;
                }
            }
        }
        return true;
    }

    bool decodeStrip(Codebooks& cb, const uint8_t* p, const uint8_t* end, int x1, int y1, int x2, int y2,
                     std::vector<uint32_t>& fb) const {
        x1 = std::max(0, x1);
        y1 = std::max(0, y1);
        bool ok = true;
        while (end - p >= 4) {
            unsigned id = rd16be(p);
            size_t csize = rd16be(p + 2);
            if (csize < 4) return false;
            if (csize > size_t(end - p)) {
                csize = size_t(end - p);
                ok = false;
            }
            const uint8_t* c = p + 4;
            const uint8_t* ce = p + csize;
            switch (id >> 8) {
            case 0x20: case 0x21: case 0x24: case 0x25:
                decodeCodebook(cb.v4, id, c, ce);
                break;
            case 0x22: case 0x23: case 0x26: case 0x27:
                decodeCodebook(cb.v1, id, c, ce);
                break;
            case 0x30: case 0x31: case 0x32:
                if (!decodeVectors(cb, id, c, ce, x1, y1, x2, y2, fb)) ok = false;
                break;
            default:
                break;  // unknown chunk: ignore
            }
            p += csize;
        }
        return ok;
    }
};

}  // namespace

std::unique_ptr<VideoDecoder> makeCinepakDecoder(int w, int h, int bitCount) {
    return std::unique_ptr<VideoDecoder>(new CinepakDecoder(w, h, bitCount));
}

}  // namespace detail
}  // namespace movie
