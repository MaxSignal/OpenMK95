// RIFF/WAVE decoding for the sound module.
#include <algorithm>
#include <cstring>

#include "sound/snd_internal.h"

namespace snd {
namespace detail {

namespace {

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline int16_t clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }

const int kImaIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct ImaState {
    int pred = 0;
    int index = 0;
    int16_t decode(int nib) {
        int step = kImaStep[index];
        int diff = step >> 3;
        if (nib & 1) diff += step >> 2;
        if (nib & 2) diff += step >> 1;
        if (nib & 4) diff += step;
        if (nib & 8) pred -= diff; else pred += diff;
        if (pred < -32768) pred = -32768;
        if (pred > 32767) pred = 32767;
        index += kImaIndex[nib & 15];
        if (index < 0) index = 0;
        if (index > 88) index = 88;
        return (int16_t)pred;
    }
};

bool decodeIma(const uint8_t* d, size_t len, int ch, int blockAlign, int spb, WaveData& out) {
    if (blockAlign < 4 * ch) return false;
    if (spb <= 0) spb = (blockAlign - 4 * ch) * 2 / ch + 1;
    out.samples.reserve((len / (size_t)blockAlign + 1) * (size_t)spb * (size_t)ch);
    std::vector<int16_t> blk((size_t)spb * (size_t)ch);
    size_t pos = 0;
    while (pos + 4u * (size_t)ch <= len) {
        size_t bl = std::min((size_t)blockAlign, len - pos);
        const uint8_t* b = d + pos;
        ImaState st[2];
        for (int c = 0; c < ch; ++c) {
            st[c].pred = (int16_t)rd16(b + 4 * c);
            st[c].index = std::min<int>(b[4 * c + 2], 88);
            blk[(size_t)c] = (int16_t)st[c].pred;
        }
        int got = 1;  // frames decoded in this block
        size_t p = 4u * (size_t)ch;
        if (ch == 1) {
            while (p < bl && got < spb) {
                uint8_t v = b[p++];
                blk[(size_t)got++] = st[0].decode(v & 15);
                if (got < spb) blk[(size_t)got++] = st[0].decode(v >> 4);
            }
        } else {
            // stereo: 4 bytes (8 samples) of left, then 4 bytes of right, repeated
            while (p + 8 <= bl && got < spb) {
                for (int c = 0; c < 2; ++c) {
                    int f = got;
                    for (int k = 0; k < 4; ++k) {
                        uint8_t v = b[p++];
                        if (f < spb) blk[(size_t)f * 2 + (size_t)c] = st[c].decode(v & 15);
                        ++f;
                        if (f < spb) blk[(size_t)f * 2 + (size_t)c] = st[c].decode(v >> 4);
                        ++f;
                    }
                }
                got = std::min(got + 8, spb);
            }
        }
        out.samples.insert(out.samples.end(), blk.begin(), blk.begin() + (ptrdiff_t)got * ch);
        pos += (size_t)blockAlign;
    }
    return true;
}

const int kMsAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614,
                          768, 614, 512, 409, 307, 230, 230, 230};

bool decodeMsAdpcm(const uint8_t* d, size_t len, int ch, int blockAlign, const uint8_t* extra,
                   size_t extraLen, WaveData& out) {
    int spb = 0;
    std::vector<int> c1, c2;
    if (extraLen >= 4) {
        spb = rd16(extra);
        int n = rd16(extra + 2);
        for (int i = 0; i < n && 4 + (size_t)i * 4 + 4 <= extraLen; ++i) {
            c1.push_back((int16_t)rd16(extra + 4 + i * 4));
            c2.push_back((int16_t)rd16(extra + 6 + i * 4));
        }
    }
    if (c1.empty()) {
        c1 = {256, 512, 0, 192, 240, 460, 392};
        c2 = {0, -256, 0, 64, 0, -208, -232};
    }
    int hdr = 7 * ch;
    if (blockAlign < hdr) return false;
    if (spb <= 0) spb = (blockAlign - hdr) * 2 / ch + 2;
    size_t pos = 0;
    while (pos + (size_t)hdr <= len) {
        size_t bl = std::min((size_t)blockAlign, len - pos);
        const uint8_t* b = d + pos;
        int pred[2], delta[2], s1[2], s2[2];
        for (int c = 0; c < ch; ++c) {
            pred[c] = std::min<int>(b[c], (int)c1.size() - 1);
            delta[c] = (int16_t)rd16(b + ch + 2 * c);
            s1[c] = (int16_t)rd16(b + 3 * ch + 2 * c);
            s2[c] = (int16_t)rd16(b + 5 * ch + 2 * c);
        }
        for (int c = 0; c < ch; ++c) out.samples.push_back((int16_t)s2[c]);
        int got = 1;
        if (spb > 1) {
            for (int c = 0; c < ch; ++c) out.samples.push_back((int16_t)s1[c]);
            got = 2;
        }
        size_t p = (size_t)hdr;
        int c = 0;
        std::vector<int16_t> frame;
        while (p < bl && got < spb) {
            uint8_t v = b[p++];
            for (int h = 0; h < 2; ++h) {
                int nib = h == 0 ? (v >> 4) : (v & 15);
                int snib = nib >= 8 ? nib - 16 : nib;
                int pr = (s1[c] * c1[(size_t)pred[c]] + s2[c] * c2[(size_t)pred[c]]) / 256;
                pr += snib * delta[c];
                int16_t smp = clamp16(pr);
                s2[c] = s1[c];
                s1[c] = smp;
                delta[c] = kMsAdapt[nib] * delta[c] / 256;
                if (delta[c] < 16) delta[c] = 16;
                frame.push_back(smp);
                if (++c == ch) {
                    c = 0;
                    out.samples.insert(out.samples.end(), frame.begin(), frame.end());
                    frame.clear();
                    if (++got >= spb) break;
                }
            }
        }
        pos += (size_t)blockAlign;
    }
    return true;
}

}  // namespace

bool decodeWav(const uint8_t* data, size_t len, WaveData& out) {
    out = WaveData();
    if (!data || len < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
        return false;
    size_t riffEnd = std::min(len, (size_t)rd32(data + 4) + 8);
    if (riffEnd < 12) riffEnd = len;
    const uint8_t* fmt = nullptr;
    size_t fmtLen = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmLen = 0;
    size_t pos = 12;
    while (pos + 8 <= len) {
        uint32_t sz = rd32(data + pos + 4);
        size_t avail = len - pos - 8;
        size_t csz = std::min((size_t)sz, avail);
        if (!memcmp(data + pos, "fmt ", 4) && !fmt) {
            fmt = data + pos + 8;
            fmtLen = csz;
        } else if (!memcmp(data + pos, "data", 4) && !pcm) {
            pcm = data + pos + 8;
            pcmLen = csz;
        }
        if (fmt && pcm) break;
        pos += 8 + (size_t)sz + (sz & 1);
        if (pos > riffEnd && pos > len) break;
    }
    if (!fmt || fmtLen < 14 || !pcm) return false;
    int tag = rd16(fmt);
    int ch = rd16(fmt + 2);
    uint32_t rate = rd32(fmt + 4);
    int blockAlign = rd16(fmt + 12);
    int bits = fmtLen >= 16 ? rd16(fmt + 14) : 8;
    const uint8_t* extra = nullptr;
    size_t extraLen = 0;
    if (fmtLen >= 18) {
        extraLen = std::min((size_t)rd16(fmt + 16), fmtLen - 18);
        extra = fmt + 18;
    }
    if (tag == 0xFFFE && extraLen >= 22) tag = rd16(extra + 6);  // WAVE_FORMAT_EXTENSIBLE
    if (ch < 1 || ch > 8 || rate < 1000 || rate > 200000) return false;
    int outCh = ch >= 2 ? 2 : 1;
    out.channels = outCh;
    out.rate = (int)rate;

    if (tag == 1 || tag == 3) {
        int bytes = (bits + 7) / 8;
        if (tag == 3 && bits != 32 && bits != 64) return false;
        if (bytes < 1 || bytes > 8) return false;
        size_t frameBytes = (size_t)bytes * (size_t)ch;
        if (blockAlign > 0 && (size_t)blockAlign >= frameBytes) frameBytes = (size_t)blockAlign;
        size_t frames = pcmLen / frameBytes;
        out.samples.resize(frames * (size_t)outCh);
        for (size_t f = 0; f < frames; ++f) {
            const uint8_t* fp = pcm + f * frameBytes;
            for (int c = 0; c < outCh; ++c) {
                const uint8_t* s = fp + (size_t)c * (size_t)bytes;
                int v;
                if (tag == 3) {
                    double x;
                    if (bits == 32) {
                        uint32_t u = rd32(s);
                        float fl;
                        memcpy(&fl, &u, 4);
                        x = fl;
                    } else {
                        uint64_t u = (uint64_t)rd32(s) | ((uint64_t)rd32(s + 4) << 32);
                        memcpy(&x, &u, 8);
                    }
                    if (!(x == x)) x = 0;
                    v = (int)(x * 32767.0);
                } else if (bytes == 1) {
                    v = ((int)s[0] - 128) * 256;
                } else {
                    // little-endian signed; take the two most significant bytes
                    v = (int16_t)rd16(s + bytes - 2);
                }
                out.samples[f * (size_t)outCh + (size_t)c] = clamp16(v);
            }
        }
        return true;
    }
    if (tag == 0x11) {  // IMA ADPCM
        if (bits != 4 || ch > 2) return false;
        int spb = extraLen >= 2 ? rd16(extra) : 0;
        return decodeIma(pcm, pcmLen, ch, blockAlign, spb, out);
    }
    if (tag == 0x02) {  // MS ADPCM
        if (bits != 4 || ch > 2) return false;
        return decodeMsAdpcm(pcm, pcmLen, ch, blockAlign, extra, extraLen, out);
    }
    return false;
}

}  // namespace detail
}  // namespace snd
