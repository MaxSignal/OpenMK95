// Internal interface between the AVI container (movie.cpp) and the video
// decoders (movie.cpp, movie_cinepak.cpp). Not used outside the movie module.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace movie {
namespace detail {

// A video decoder writes into a persistent frame buffer of width*height pixels
// (0x00RRGGBB, top row first). Pixels a frame does not update keep the value
// from the previous frame (delta frames, skipped blocks).
class VideoDecoder {
public:
    VideoDecoder(int w, int h) : w_(w), h_(h), pal_(256, 0) {}
    virtual ~VideoDecoder() = default;
    // Decode one compressed frame. Returns false if the data was damaged; the
    // frame buffer then holds whatever could be decoded.
    virtual bool decode(const uint8_t* data, size_t size, std::vector<uint32_t>& fb) = 0;
    // Palette for palettised formats (entries 0x00RRGGBB).
    void setPaletteEntry(int i, uint32_t rgb) {
        if (i >= 0 && i < 256) pal_[i] = rgb & 0xFFFFFF;
    }

protected:
    int w_, h_;
    std::vector<uint32_t> pal_;
};

// Cinepak ('cvid'). bitCount is the strf biBitCount (8 = palettised variant).
std::unique_ptr<VideoDecoder> makeCinepakDecoder(int w, int h, int bitCount);

inline uint32_t rd16be(const uint8_t* p) { return (uint32_t(p[0]) << 8) | p[1]; }
inline uint32_t rd24be(const uint8_t* p) { return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]; }
inline uint32_t rd32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint32_t rd16le(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }
inline uint32_t rd32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

}  // namespace detail
}  // namespace movie
