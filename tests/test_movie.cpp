// Tests for src/movie: AVI container and the uncompressed / RLE8 decoders (spec 09 §7).
#include <cstring>

#include "movie/movie.h"
#include "test.h"

namespace {

struct Riff {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint32_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void tag(const char* t) { for (int i = 0; i < 4; i++) u8(uint8_t(t[i])); }
    void raw(const std::vector<uint8_t>& d) { b.insert(b.end(), d.begin(), d.end()); }
    // Write a chunk; returns its bytes padded to an even length.
    static std::vector<uint8_t> chunk(const char* id, const std::vector<uint8_t>& data) {
        Riff r;
        r.tag(id);
        r.u32(uint32_t(data.size()));
        r.raw(data);
        if (data.size() & 1) r.u8(0);
        return r.b;
    }
    static std::vector<uint8_t> list(const char* type, const std::vector<uint8_t>& body, const char* id = "LIST") {
        Riff r;
        r.tag(type);
        r.raw(body);
        return chunk(id, r.b);
    }
};

// An AVI with one video stream: w x h, the given BITMAPINFOHEADER compression/bit depth,
// an optional palette and the given frame chunks ("00db"/"00dc").
std::vector<uint8_t> makeAvi(int w, int h, uint32_t comp, int bpp, const std::vector<uint32_t>& pal,
                             const std::vector<std::vector<uint8_t>>& frames, uint32_t usPerFrame) {
    Riff avih;
    avih.u32(usPerFrame);
    for (int i = 0; i < 7; i++) avih.u32(0);
    avih.u32(uint32_t(w));
    avih.u32(uint32_t(h));
    for (int i = 0; i < 4; i++) avih.u32(0);
    Riff strh;
    strh.tag("vids");
    strh.u32(comp);
    for (int i = 0; i < 3; i++) strh.u32(0);
    strh.u32(1);        // scale
    strh.u32(15);       // rate
    for (int i = 0; i < 9; i++) strh.u32(0);
    Riff strf;
    strf.u32(40);
    strf.u32(uint32_t(w));
    strf.u32(uint32_t(h));  // positive: bottom-up
    strf.u16(1);
    strf.u16(uint32_t(bpp));
    strf.u32(comp);
    for (int i = 0; i < 3; i++) strf.u32(0);
    strf.u32(uint32_t(pal.size()));
    strf.u32(0);
    for (uint32_t c : pal) strf.u32(c);  // stored as B, G, R, 0
    Riff strl;
    strl.raw(Riff::chunk("strh", strh.b));
    strl.raw(Riff::chunk("strf", strf.b));
    Riff hdrl;
    hdrl.raw(Riff::chunk("avih", avih.b));
    hdrl.raw(Riff::list("strl", strl.b));
    Riff movi;
    for (auto& f : frames) movi.raw(Riff::chunk(comp ? "00dc" : "00db", f));
    Riff body;
    body.raw(Riff::list("hdrl", hdrl.b));
    body.raw(Riff::list("movi", movi.b));
    return Riff::list("AVI ", body.b, "RIFF");
}

constexpr uint32_t fcc(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

}  // namespace

TEST(avi_uncompressed_24bit) {
    // 2x2, bottom-up BGR rows padded to 4 bytes: bottom row = blue, green; top row = red, white.
    std::vector<uint8_t> f = {0xFF, 0, 0, 0, 0xFF, 0, 0, 0,  // bottom
                              0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0};  // top
    std::string err;
    auto m = movie::openAvi(makeAvi(2, 2, 0, 24, {}, {f, f}, 50000), &err);
    CHECK(m != nullptr);
    CHECK_EQ(m->width(), 2);
    CHECK_EQ(m->height(), 2);
    CHECK_EQ(m->usPerFrame(), (int64_t)50000);
    CHECK_EQ(m->frameCount(), 2);
    std::vector<uint32_t> px;
    CHECK(m->nextFrame(px));
    CHECK_EQ(px.size(), (size_t)4);
    CHECK_EQ(px[0], 0xFF0000u);  // top-left red
    CHECK_EQ(px[1], 0xFFFFFFu);
    CHECK_EQ(px[2], 0x0000FFu);  // bottom-left blue
    CHECK_EQ(px[3], 0x00FF00u);
    CHECK(m->nextFrame(px));
    CHECK(!m->nextFrame(px));
}

TEST(avi_rle8_keeps_previous_pixels) {
    std::vector<uint32_t> pal = {0x000000, 0xFF0000, 0x00FF00};  // RGBQUADs read as 0x00RRGGBB: black, red, green
    // frame 1: both rows "4 x colour 1" ; frame 2: only the bottom row, 2 x colour 2, then end of bitmap
    std::vector<uint8_t> f1 = {4, 1, 0, 0, 4, 1, 0, 1};
    std::vector<uint8_t> f2 = {2, 2, 0, 1};
    std::string err;
    auto m = movie::openAvi(makeAvi(4, 2, 1, 8, pal, {f1, f2}, 66667), &err);
    CHECK(m != nullptr);
    std::vector<uint32_t> px;
    CHECK(m->nextFrame(px));
    for (uint32_t c : px) CHECK_EQ(c, 0xFF0000u);
    CHECK(m->nextFrame(px));
    CHECK_EQ(px[4], 0x00FF00u);  // bottom row starts at index 4 (top row first)
    CHECK_EQ(px[5], 0x00FF00u);
    CHECK_EQ(px[6], 0xFF0000u);
    CHECK_EQ(px[0], 0xFF0000u);
}

TEST(avi_unsupported_codec_is_rejected) {
    std::string err;
    auto m = movie::openAvi(makeAvi(4, 4, fcc('I', 'V', '4', '1'), 24, {}, {{1, 2, 3}}, 66667), &err);
    CHECK(m == nullptr);
    CHECK(err.find("IV41") != std::string::npos);
    CHECK(movie::openAvi(std::vector<uint8_t>(16, 0), &err) == nullptr);
}
