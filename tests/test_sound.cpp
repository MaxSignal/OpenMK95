// Tests for src/sound: WAV decoding and the Standard MIDI File parser (spec 09).
#include <cmath>

#include "sound/snd_internal.h"
#include "test.h"

using namespace snd::detail;

namespace {
void put16(std::vector<uint8_t>& b, uint32_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

std::vector<uint8_t> pcmWav(int channels, int rate, int bits, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> b;
    tag(b, "RIFF");
    put32(b, uint32_t(4 + 8 + 16 + 8 + data.size()));
    tag(b, "WAVE");
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, 1);
    put16(b, uint32_t(channels));
    put32(b, uint32_t(rate));
    put32(b, uint32_t(rate * channels * bits / 8));
    put16(b, uint32_t(channels * bits / 8));
    put16(b, uint32_t(bits));
    tag(b, "data");
    put32(b, uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}
}  // namespace

TEST(wav_pcm8_mono) {
    auto w = pcmWav(1, 11025, 8, {0x80, 0xFF, 0x00, 0x80});
    WaveData d;
    CHECK(decodeWav(w.data(), w.size(), d));
    CHECK_EQ(d.channels, 1);
    CHECK_EQ(d.rate, 11025);
    CHECK_EQ(d.frames(), (size_t)4);
    CHECK_EQ((int)d.samples[0], 0);
    CHECK(d.samples[1] > 32000);
    CHECK(d.samples[2] <= -32000);
}

TEST(wav_pcm16_stereo) {
    auto w = pcmWav(2, 22050, 16, {0x34, 0x12, 0xCC, 0xED, 0x00, 0x00, 0xFF, 0x7F});
    WaveData d;
    CHECK(decodeWav(w.data(), w.size(), d));
    CHECK_EQ(d.channels, 2);
    CHECK_EQ(d.frames(), (size_t)2);
    CHECK_EQ((int)d.samples[0], 0x1234);
    CHECK_EQ((int)d.samples[1], -0x1234);
    CHECK_EQ((int)d.samples[3], 0x7FFF);
}

TEST(wav_rejects_garbage) {
    std::vector<uint8_t> junk(64, 0x55);
    WaveData d;
    CHECK(!decodeWav(junk.data(), junk.size(), d));
}

TEST(smf_format0_timing) {
    std::vector<uint8_t> trk = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,  // tempo 500000 us per quarter
        0x00, 0x90, 60, 100,                       // note on
        0x83, 0x60, 0x80, 60, 0,                   // 480 ticks later: note off
        0x83, 0x60, 0xFF, 0x2F, 0x00,              // 480 ticks later: end of track
    };
    std::vector<uint8_t> b;
    tag(b, "MThd");
    b.insert(b.end(), {0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0});  // format 0, 1 track, 480 ticks/quarter
    tag(b, "MTrk");
    b.insert(b.end(), {0, 0, 0, uint8_t(trk.size())});
    b.insert(b.end(), trk.begin(), trk.end());
    MidiSong s;
    CHECK(parseSmf(b.data(), b.size(), s));
    CHECK_EQ(s.events.size(), (size_t)2);
    CHECK_EQ((int)s.events[0].status, 0x90);
    CHECK(std::fabs(s.events[0].time) < 1e-9);
    CHECK(std::fabs(s.events[1].time - 0.5) < 1e-6);
    CHECK(std::fabs(s.endTime - 1.0) < 1e-6);
}

TEST(sysex_reset_detection) {
    const uint8_t gm[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    const uint8_t other[] = {0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x01, 0xF7};
    CHECK(isResetSysex(gm, sizeof gm));
    CHECK(isResetSysex(other, sizeof other));  // XG system on
    const uint8_t notReset[] = {0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x7F, 0xF7};  // master volume
    CHECK(!isResetSysex(notReset, sizeof notReset));
}
