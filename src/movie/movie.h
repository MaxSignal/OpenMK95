// AVI movie decoding for event command 14 (09-audio-movies.md §7).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace movie {

class Movie {
public:
    virtual ~Movie() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    // Duration of one frame in microseconds (from the AVI header).
    virtual int64_t usPerFrame() const = 0;
    virtual int frameCount() const = 0;
    // Decode the next frame (frames are decoded in order starting at 0).
    // Output: width*height pixels, 0x00RRGGBB, top row first. Returns false at the
    // end of the stream or on an unrecoverable error.
    virtual bool nextFrame(std::vector<uint32_t>& rgb) = 0;
    // Optional audio track as interleaved signed 16-bit PCM (empty if none).
    virtual bool audio(std::vector<int16_t>& pcm, int& rate, int& channels) { return false; }
};

// Open an AVI file image. Returns nullptr (and a reason in *err) if the file is
// not an AVI or its video codec is not supported.
std::unique_ptr<Movie> openAvi(std::vector<uint8_t> file, std::string* err);

}  // namespace movie
