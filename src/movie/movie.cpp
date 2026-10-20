// AVI movie decoding (09-audio-movies.md §7.5).
//
// Container: RIFF 'AVI ' (plus OpenDML 'AVIX' continuation RIFFs). The movi list
// is read sequentially; idx1 is not needed. Video codecs: uncompressed DIB
// (1/4/8/16/24/32 bit, BI_RGB and BI_BITFIELDS, bottom-up and top-down), BI_RLE8,
// BI_RLE4, Microsoft Video 1 (8 and 16 bit), Cinepak (movie_cinepak.cpp).
// Audio: PCM only.
#include "movie/movie.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "movie/movie_codecs.h"

namespace movie {

using namespace detail;

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
           (uint32_t(uint8_t(d)) << 24);
}

std::string fourccName(uint32_t f) {
    char s[5];
    bool printable = true;
    for (int i = 0; i < 4; i++) {
        s[i] = char((f >> (8 * i)) & 0xFF);
        if (uint8_t(s[i]) < 0x20 || uint8_t(s[i]) > 0x7E) printable = false;
    }
    s[4] = 0;
    if (printable) return std::string("'") + s + "'";
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", unsigned(f));
    return buf;
}

// ---------------------------------------------------------------------------
// Uncompressed DIB
// ---------------------------------------------------------------------------
class RawDecoder : public VideoDecoder {
public:
    RawDecoder(int w, int h, int bpp, bool topDown, uint32_t rmask, uint32_t gmask, uint32_t bmask)
        : VideoDecoder(w, h), bpp_(bpp), topDown_(topDown) {
        if (bpp == 16 && !(rmask | gmask | bmask)) {
            rmask = 0x7C00;
            gmask = 0x03E0;
            bmask = 0x001F;
        }
        if (bpp == 32 && !(rmask | gmask | bmask)) {
            rmask = 0xFF0000;
            gmask = 0x00FF00;
            bmask = 0x0000FF;
        }
        setMask(rmask, rs_, rb_);
        setMask(gmask, gs_, gb_);
        setMask(bmask, bs_, bb_);
        rm_ = rmask;
        gm_ = gmask;
        bm_ = bmask;
    }

    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& fb) override {
        size_t stride = ((size_t(w_) * bpp_ + 31) / 32) * 4;
        bool ok = n >= stride * h_;
        for (int r = 0; r < h_; r++) {
            size_t off = stride * r;
            if (off + stride > n) break;
            const uint8_t* s = d + off;
            int y = topDown_ ? r : h_ - 1 - r;
            uint32_t* out = &fb[size_t(y) * w_];
            switch (bpp_) {
            case 1:
                for (int x = 0; x < w_; x++) out[x] = pal_[(s[x >> 3] >> (7 - (x & 7))) & 1];
                break;
            case 4:
                for (int x = 0; x < w_; x++) out[x] = pal_[(x & 1) ? (s[x >> 1] & 15) : (s[x >> 1] >> 4)];
                break;
            case 8:
                for (int x = 0; x < w_; x++) out[x] = pal_[s[x]];
                break;
            case 16:
                for (int x = 0; x < w_; x++) out[x] = maskColor(rd16le(s + 2 * x));
                break;
            case 24:
                for (int x = 0; x < w_; x++)
                    out[x] = (uint32_t(s[3 * x + 2]) << 16) | (uint32_t(s[3 * x + 1]) << 8) | s[3 * x];
                break;
            case 32:
                for (int x = 0; x < w_; x++) out[x] = maskColor(rd32le(s + 4 * x));
                break;
            }
        }
        return ok;
    }

private:
    int bpp_;
    bool topDown_;
    uint32_t rm_, gm_, bm_;
    int rs_ = 0, rb_ = 0, gs_ = 0, gb_ = 0, bs_ = 0, bb_ = 0;

    static void setMask(uint32_t m, int& shift, int& bits) {
        shift = 0;
        bits = 0;
        if (!m) return;
        while (!(m & 1)) {
            m >>= 1;
            shift++;
        }
        while (m & 1) {
            m >>= 1;
            bits++;
        }
    }
    static uint32_t expand(uint32_t v, int bits) {
        if (bits <= 0) return 0;
        if (bits >= 8) return (v >> (bits - 8)) & 0xFF;
        // replicate the bits to fill 8 bits
        uint32_t r = 0;
        int have = 0;
        while (have < 8) {
            r = (r << bits) | v;
            have += bits;
        }
        return (r >> (have - 8)) & 0xFF;
    }
    uint32_t maskColor(uint32_t c) const {
        uint32_t r = expand((c & rm_) >> rs_, rb_);
        uint32_t g = expand((c & gm_) >> gs_, gb_);
        uint32_t b = expand((c & bm_) >> bs_, bb_);
        return (r << 16) | (g << 8) | b;
    }
};

// ---------------------------------------------------------------------------
// BI_RLE8 / BI_RLE4 (bottom-up; untouched pixels keep the previous frame)
// ---------------------------------------------------------------------------
class RleDecoder : public VideoDecoder {
public:
    RleDecoder(int w, int h, bool rle4) : VideoDecoder(w, h), rle4_(rle4) {}

    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& fb) override {
        size_t p = 0;
        int x = 0, row = 0;  // row counts DIB rows from the bottom
        auto put = [&](unsigned idx) {
            if (x < w_ && row < h_) fb[size_t(h_ - 1 - row) * w_ + x] = pal_[idx & 0xFF];
            x++;
        };
        while (p + 2 <= n) {
            unsigned c = d[p], v = d[p + 1];
            p += 2;
            if (c > 0) {
                if (rle4_) {
                    for (unsigned i = 0; i < c; i++) put((i & 1) ? (v & 15) : (v >> 4));
                } else {
                    for (unsigned i = 0; i < c; i++) put(v);
                }
                continue;
            }
            switch (v) {
            case 0:  // end of line
                x = 0;
                row++;
                break;
            case 1:  // end of bitmap
                return true;
            case 2:  // delta
                if (p + 2 > n) return false;
                x += d[p];
                row += d[p + 1];
                p += 2;
                break;
            default: {  // absolute run of v pixels
                size_t bytes = rle4_ ? (v + 1) / 2 : v;
                if (p + bytes > n) return false;
                if (rle4_) {
                    for (unsigned i = 0; i < v; i++) {
                        unsigned b = d[p + i / 2];
                        put((i & 1) ? (b & 15) : (b >> 4));
                    }
                } else {
                    for (unsigned i = 0; i < v; i++) put(d[p + i]);
                }
                p += bytes + (bytes & 1);
                break;
            }
            }
            if (row >= h_) return true;
        }
        return true;
    }

private:
    bool rle4_;
};

// ---------------------------------------------------------------------------
// Microsoft Video 1 ('CRAM' / 'MSVC'), 8-bit palettised or 16-bit RGB555.
// 4x4 blocks, block rows from the bottom of the image, pixel rows within a
// block from its bottom row. Two-byte block code (a, b):
//   (b & 0xFC) == 0x84      skip ((b - 0x84) << 8 | a) blocks (this one included)
//   8-bit:  b < 0x80 2 colours, b >= 0x90 8 colours (2 per quadrant), else 1 colour a
//   16-bit: b < 0x80 2 colours (8 if bit 15 of the first colour is set), else 1 colour
// Flag bits (b << 8 | a), LSB first: 1 = first colour of the pair, 0 = second.
// ---------------------------------------------------------------------------
class MsVideo1Decoder : public VideoDecoder {
public:
    MsVideo1Decoder(int w, int h, int bpp) : VideoDecoder(w, h), sixteen_(bpp == 16) {}

    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& fb) override {
        int bw = (w_ + 3) / 4, bh = (h_ + 3) / 4;
        size_t p = 0;
        long skip = 0;
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                if (skip > 0) {
                    skip--;
                    continue;
                }
                if (p + 2 > n) return false;
                unsigned a = d[p], b = d[p + 1];
                p += 2;
                if ((b & 0xFC) == 0x84) {
                    skip = long(((b - 0x84) << 8) + a) - 1;
                    continue;
                }
                uint32_t cols[8];
                unsigned flags = (b << 8) | a;
                int mode;  // 1, 2 or 8 colours
                if (sixteen_) {
                    if (b < 0x80) {
                        if (p + 4 > n) return false;
                        uint32_t c0 = rd16le(d + p), c1 = rd16le(d + p + 2);
                        p += 4;
                        cols[0] = rgb555(c0);
                        cols[1] = rgb555(c1);
                        if (c0 & 0x8000) {
                            if (p + 12 > n) return false;
                            for (int i = 2; i < 8; i++, p += 2) cols[i] = rgb555(rd16le(d + p));
                            mode = 8;
                        } else {
                            mode = 2;
                        }
                    } else {
                        cols[0] = rgb555(flags);
                        mode = 1;
                    }
                } else {
                    if (b < 0x80) {
                        if (p + 2 > n) return false;
                        cols[0] = pal_[d[p]];
                        cols[1] = pal_[d[p + 1]];
                        p += 2;
                        mode = 2;
                    } else if (b >= 0x90) {
                        if (p + 8 > n) return false;
                        for (int i = 0; i < 8; i++) cols[i] = pal_[d[p + i]];
                        p += 8;
                        mode = 8;
                    } else {
                        cols[0] = pal_[a];
                        mode = 1;
                    }
                }
                for (int py = 0; py < 4; py++) {
                    int y = h_ - 1 - (by * 4 + py);
                    for (int px = 0; px < 4; px++, flags >>= 1) {
                        uint32_t c;
                        if (mode == 1) {
                            c = cols[0];
                        } else {
                            int base = (mode == 8) ? ((py & 2) << 1) + (px & 2) : 0;
                            c = cols[base + ((flags & 1) ? 0 : 1)];
                        }
                        int x = bx * 4 + px;
                        if (y >= 0 && x < w_) fb[size_t(y) * w_ + x] = c;
                    }
                }
            }
        }
        return true;
    }

private:
    bool sixteen_;
    static uint32_t rgb555(uint32_t c) {
        uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        return (r << 16) | (g << 8) | b;
    }
};

// ---------------------------------------------------------------------------
// AVI container
// ---------------------------------------------------------------------------
struct StreamInfo {
    uint32_t type = 0;     // 'vids', 'auds', ...
    uint32_t handler = 0;  // strh fccHandler
    uint32_t scale = 0, rate = 0;
    const uint8_t* strf = nullptr;
    size_t strfSize = 0;
};

struct Event {
    enum Kind { Frame, Palette, Audio } kind;
    size_t off, size;
};

class AviMovie : public Movie {
public:
    explicit AviMovie(std::vector<uint8_t> file) : file_(std::move(file)) {}

    bool open(std::string* err);

    int width() const override { return w_; }
    int height() const override { return h_; }
    int64_t usPerFrame() const override { return us_; }
    int frameCount() const override { return frames_; }

    bool nextFrame(std::vector<uint32_t>& rgb) override {
        while (pos_ < events_.size()) {
            const Event& e = events_[pos_++];
            if (e.kind == Event::Palette) {
                applyPaletteChange(&file_[e.off], e.size);
                continue;
            }
            if (e.kind != Event::Frame) continue;
            if (e.size > 0) dec_->decode(&file_[e.off], e.size, fb_);
            // zero-size chunk: dropped frame, show the previous one again
            rgb = fb_;
            return true;
        }
        return false;
    }

    bool audio(std::vector<int16_t>& pcm, int& rate, int& channels) override;

private:
    std::vector<uint8_t> file_;
    int w_ = 0, h_ = 0;
    int64_t us_ = 0;
    int frames_ = 0;
    std::vector<StreamInfo> streams_;
    int vidStream_ = -1, audStream_ = -1;
    std::vector<Event> events_;
    size_t pos_ = 0;
    std::vector<uint32_t> fb_;
    std::unique_ptr<VideoDecoder> dec_;
    uint32_t avihUs_ = 0, avihW_ = 0, avihH_ = 0;

    void parseChunks(size_t off, size_t end, int level);
    void parseHdrl(size_t off, size_t end);
    void parseStrl(size_t off, size_t end);
    void parseMovi(size_t off, size_t end, int depth);
    void applyPaletteChange(const uint8_t* d, size_t n);
};

// Iterate over the chunks in [off, end). Calls f(id, dataOffset, dataSize).
template <class F>
void forEachChunk(const std::vector<uint8_t>& file, size_t off, size_t end, F f) {
    end = std::min(end, file.size());
    while (off + 8 <= end) {
        uint32_t id = rd32le(&file[off]);
        size_t size = rd32le(&file[off + 4]);
        size_t data = off + 8;
        if (size > end - data) size = end - data;  // truncated file
        f(id, data, size);
        size_t next = data + size + (size & 1);
        if (next <= off) break;
        off = next;
    }
}

void AviMovie::parseChunks(size_t off, size_t end, int level) {
    forEachChunk(file_, off, end, [&](uint32_t id, size_t data, size_t size) {
        if ((id == fourcc('L', 'I', 'S', 'T') || id == fourcc('R', 'I', 'F', 'F')) && size >= 4) {
            uint32_t type = rd32le(&file_[data]);
            if (id == fourcc('L', 'I', 'S', 'T') && type == fourcc('m', 'o', 'v', 'i'))
                parseMovi(data + 4, data + size, 0);
            else if (id == fourcc('R', 'I', 'F', 'F') && level == 0 &&
                     (type == fourcc('A', 'V', 'I', 'X') || type == fourcc('A', 'V', 'I', ' ')))
                parseChunks(data + 4, data + size, 1);
        }
    });
}

void AviMovie::parseHdrl(size_t off, size_t end) {
    forEachChunk(file_, off, end, [&](uint32_t id, size_t data, size_t size) {
        if (id == fourcc('a', 'v', 'i', 'h') && size >= 40) {
            avihUs_ = rd32le(&file_[data]);
            avihW_ = rd32le(&file_[data + 32]);
            avihH_ = rd32le(&file_[data + 36]);
        } else if (id == fourcc('L', 'I', 'S', 'T') && size >= 4 &&
                   rd32le(&file_[data]) == fourcc('s', 't', 'r', 'l')) {
            parseStrl(data + 4, data + size);
        }
    });
}

void AviMovie::parseStrl(size_t off, size_t end) {
    StreamInfo si;
    forEachChunk(file_, off, end, [&](uint32_t id, size_t data, size_t size) {
        if (id == fourcc('s', 't', 'r', 'h') && size >= 32) {
            si.type = rd32le(&file_[data]);
            si.handler = rd32le(&file_[data + 4]);
            si.scale = rd32le(&file_[data + 20]);
            si.rate = rd32le(&file_[data + 24]);
        } else if (id == fourcc('s', 't', 'r', 'f')) {
            si.strf = size ? &file_[data] : nullptr;
            si.strfSize = size;
        }
    });
    streams_.push_back(si);
}

void AviMovie::parseMovi(size_t off, size_t end, int depth) {
    forEachChunk(file_, off, end, [&](uint32_t id, size_t data, size_t size) {
        if (id == fourcc('L', 'I', 'S', 'T')) {
            if (size >= 4 && depth < 4) parseMovi(data + 4, data + size, depth + 1);  // 'rec ' groups
            return;
        }
        char c0 = char(id & 0xFF), c1 = char((id >> 8) & 0xFF);
        if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9') return;  // JUNK, ix##, ...
        int stream = (c0 - '0') * 10 + (c1 - '0');
        uint32_t tag = id >> 16;
        if (stream == vidStream_) {
            if (tag == fourcc('d', 'c', 0, 0) || tag == fourcc('d', 'b', 0, 0)) {
                events_.push_back({Event::Frame, data, size});
                frames_++;
            } else if (tag == fourcc('p', 'c', 0, 0)) {
                events_.push_back({Event::Palette, data, size});
            }
        } else if (stream == audStream_ && tag == fourcc('w', 'b', 0, 0)) {
            events_.push_back({Event::Audio, data, size});
        }
    });
}

void AviMovie::applyPaletteChange(const uint8_t* d, size_t n) {
    // AVIPALCHANGE: bFirstEntry, bNumEntries (0 = 256), wFlags, PALETTEENTRY[] (R, G, B, flags)
    if (n < 4) return;
    int first = d[0];
    int count = d[1] ? d[1] : 256;
    for (int i = 0; i < count && 4 + size_t(i) * 4 + 4 <= n; i++) {
        const uint8_t* e = d + 4 + i * 4;
        dec_->setPaletteEntry(first + i, (uint32_t(e[0]) << 16) | (uint32_t(e[1]) << 8) | e[2]);
    }
}

bool AviMovie::open(std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (file_.size() < 12 || rd32le(&file_[0]) != fourcc('R', 'I', 'F', 'F') ||
        rd32le(&file_[8]) != fourcc('A', 'V', 'I', ' '))
        return fail("not an AVI file");

    // First pass: headers (the RIFF size field is not trusted; read to the end of the file).
    {
        size_t riffEnd = file_.size();
        forEachChunk(file_, 12, riffEnd, [&](uint32_t id, size_t data, size_t size) {
            if (id == fourcc('L', 'I', 'S', 'T') && size >= 4 &&
                rd32le(&file_[data]) == fourcc('h', 'd', 'r', 'l'))
                parseHdrl(data + 4, data + size);
        });
    }
    for (size_t i = 0; i < streams_.size(); i++) {
        if (vidStream_ < 0 && streams_[i].type == fourcc('v', 'i', 'd', 's')) vidStream_ = int(i);
        if (audStream_ < 0 && streams_[i].type == fourcc('a', 'u', 'd', 's')) audStream_ = int(i);
    }
    if (vidStream_ < 0) return fail("AVI has no video stream");
    const StreamInfo& vs = streams_[vidStream_];
    if (!vs.strf || vs.strfSize < 40) return fail("AVI video stream has no format");

    const uint8_t* bi = vs.strf;
    uint32_t biSize = rd32le(bi);
    int32_t biWidth = int32_t(rd32le(bi + 4));
    int32_t biHeight = int32_t(rd32le(bi + 8));
    int bpp = int(rd16le(bi + 14));
    uint32_t comp = rd32le(bi + 16);
    uint32_t clrUsed = rd32le(bi + 32);
    bool topDown = biHeight < 0;
    w_ = biWidth > 0 ? biWidth : int(avihW_);
    h_ = biHeight != 0 ? (biHeight < 0 ? -biHeight : biHeight) : int(avihH_);
    if (w_ <= 0 || h_ <= 0 || w_ > 4096 || h_ > 4096) return fail("bad AVI frame size");

    // Colour table / bit fields following the header.
    if (biSize < 40 || biSize > vs.strfSize) biSize = 40;
    std::vector<uint32_t> pal;
    uint32_t masks[3] = {0, 0, 0};
    if (comp == 3) {  // BI_BITFIELDS
        if (vs.strfSize >= biSize + 12)
            for (int i = 0; i < 3; i++) masks[i] = rd32le(bi + biSize + 4 * i);
    } else if (bpp <= 8) {
        size_t n = clrUsed ? clrUsed : (size_t(1) << bpp);
        if (n > 256) n = 256;
        for (size_t i = 0; i < n && biSize + 4 * i + 4 <= vs.strfSize; i++) {
            const uint8_t* q = bi + biSize + 4 * i;
            pal.push_back((uint32_t(q[2]) << 16) | (uint32_t(q[1]) << 8) | q[0]);
        }
    }

    // Choose the decoder.
    switch (comp) {
    case 0:
    case 3:
    case fourcc('D', 'I', 'B', ' '):
    case fourcc('R', 'G', 'B', ' '):
    case fourcc('R', 'A', 'W', ' '):
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
            return fail("unsupported uncompressed bit depth " + std::to_string(bpp));
        dec_.reset(new RawDecoder(w_, h_, bpp, topDown, masks[0], masks[1], masks[2]));
        break;
    case 1:
    case fourcc('R', 'L', 'E', '8'):
        dec_.reset(new RleDecoder(w_, h_, false));
        break;
    case 2:
    case fourcc('R', 'L', 'E', '4'):
        dec_.reset(new RleDecoder(w_, h_, true));
        break;
    case fourcc('m', 'r', 'l', 'e'):
    case fourcc('R', 'L', 'E', ' '):
        dec_.reset(new RleDecoder(w_, h_, bpp == 4));
        break;
    case fourcc('C', 'R', 'A', 'M'):
    case fourcc('c', 'r', 'a', 'm'):
    case fourcc('M', 'S', 'V', 'C'):
    case fourcc('m', 's', 'v', 'c'):
    case fourcc('W', 'H', 'A', 'M'):
    case fourcc('w', 'h', 'a', 'm'):
        if (bpp != 8 && bpp != 16) return fail("unsupported Microsoft Video 1 bit depth " + std::to_string(bpp));
        dec_.reset(new MsVideo1Decoder(w_, h_, bpp));
        break;
    case fourcc('c', 'v', 'i', 'd'):
    case fourcc('C', 'V', 'I', 'D'):
        dec_ = makeCinepakDecoder(w_, h_, bpp);
        break;
    default:
        return fail("unsupported video codec " + fourccName(comp));
    }
    for (size_t i = 0; i < pal.size(); i++) dec_->setPaletteEntry(int(i), pal[i]);

    // Frame rate: the main header; fall back to the stream rate.
    us_ = avihUs_;
    if (us_ <= 0 && vs.rate) us_ = int64_t(vs.scale) * 1000000 / vs.rate;
    if (us_ <= 0) us_ = 66667;

    // Second pass: the movi lists.
    parseChunks(12, file_.size(), 0);
    fb_.assign(size_t(w_) * h_, 0);
    return true;
}

bool AviMovie::audio(std::vector<int16_t>& pcm, int& rate, int& channels) {
    pcm.clear();
    if (audStream_ < 0) return false;
    const StreamInfo& as = streams_[audStream_];
    if (!as.strf || as.strfSize < 16) return false;
    const uint8_t* wf = as.strf;
    unsigned tag = rd16le(wf);
    unsigned ch = rd16le(wf + 2);
    unsigned sr = rd32le(wf + 4);
    unsigned bits = rd16le(wf + 14);
    if (tag == 0xFFFE) {  // WAVE_FORMAT_EXTENSIBLE: sub-format GUID starts with the format tag
        if (as.strfSize < 40) return false;
        tag = rd16le(wf + 24);
    }
    if (tag != 1 || ch == 0 || ch > 8 || sr == 0) return false;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
    size_t bytes = bits / 8;
    for (const Event& e : events_) {
        if (e.kind != Event::Audio) continue;
        const uint8_t* d = &file_[e.off];
        for (size_t i = 0; i + bytes <= e.size; i += bytes) {
            int16_t s;
            if (bits == 8)
                s = int16_t((int(d[i]) - 128) << 8);
            else
                s = int16_t(rd16le(d + i + bytes - 2));
            pcm.push_back(s);
        }
    }
    size_t frame = ch;
    pcm.resize(pcm.size() / frame * frame);
    rate = int(sr);
    channels = int(ch);
    return !pcm.empty();
}

}  // namespace

std::unique_ptr<Movie> openAvi(std::vector<uint8_t> file, std::string* err) {
    std::unique_ptr<AviMovie> m(new AviMovie(std::move(file)));
    if (!m->open(err)) return nullptr;
    return std::unique_ptr<Movie>(m.release());
}

}  // namespace movie
