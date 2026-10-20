// SoundFont (TinySoundFont) MIDI synthesizer backend for the sound module.
#include <SDL.h>

#include <cmath>
#include <cstring>

#include "sound/snd_internal.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnull-pointer-subtraction"
#endif
#define TSF_IMPLEMENTATION
#define TSF_STATIC
#define TSF_NO_STDIO
#include "tinysoundfont/tsf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace snd {
namespace detail {

namespace {

int rwRead(void* data, void* ptr, unsigned int size) {
    return (int)SDL_RWread((SDL_RWops*)data, ptr, 1, size);
}
int rwSkip(void* data, unsigned int count) {
    return SDL_RWseek((SDL_RWops*)data, (Sint64)count, RW_SEEK_CUR) >= 0 ? 1 : 0;
}

class TsfSynth : public MidiSynth {
public:
    tsf* f_ = nullptr;
    int rate_;
    explicit TsfSynth(tsf* f, int rate) : f_(f), rate_(rate) {
        // Overall level: -9 dB keeps typical GM pieces below full scale (measured on the corpus).
        tsf_set_output(f_, TSF_STEREO_INTERLEAVED, rate_, -9.0f);
        tsf_set_max_voices(f_, 256);  // pre-allocate: no allocation while rendering
        reset();
    }
    ~TsfSynth() override { tsf_close(f_); }

    void initChannel(int c) {
        if (c == 9) tsf_channel_set_presetnumber(f_, c, 0, 1);
        else tsf_channel_set_bank_preset(f_, c, 0, 0);
        tsf_channel_midi_control(f_, c, 7, 100);
        tsf_channel_midi_control(f_, c, 11, 127);
        tsf_channel_midi_control(f_, c, 10, 64);
        tsf_channel_set_pitchwheel(f_, c, 8192);
        tsf_channel_set_pitchrange(f_, c, 2.0f);
        tsf_channel_set_tuning(f_, c, 0.0f);
        tsf_channel_set_sustain(f_, c, 0);
        tsf_channel_midi_control(f_, c, 101, 127);
        tsf_channel_midi_control(f_, c, 100, 127);
    }
    void reset() override {
        tsf_reset(f_);  // ends all voices quickly and drops the channel table
        for (int c = 0; c < 16; ++c) initChannel(c);
    }
    void allSoundOff() override {
        for (int c = 0; c < 16; ++c) {
            tsf_channel_set_sustain(f_, c, 0);
            tsf_channel_sounds_off_all(f_, c);
        }
    }
    void shortMsg(uint8_t st, uint8_t d1, uint8_t d2) override {
        int c = st & 15;
        switch (st & 0xF0) {
            case 0x80:
                tsf_channel_note_off(f_, c, d1);
                break;
            case 0x90:
                if (d2 == 0) tsf_channel_note_off(f_, c, d1);
                else tsf_channel_note_on(f_, c, d1, d2 / 127.0f);
                break;
            case 0xB0:
                switch (d1) {
                    case 120:
                        tsf_channel_sounds_off_all(f_, c);
                        break;
                    case 121:  // reset all controllers (RP-015: not volume, pan, bank, program)
                        tsf_channel_midi_control(f_, c, 11, 127);
                        tsf_channel_set_pitchwheel(f_, c, 8192);
                        tsf_channel_set_sustain(f_, c, 0);
                        tsf_channel_midi_control(f_, c, 101, 127);
                        tsf_channel_midi_control(f_, c, 100, 127);
                        break;
                    case 123: case 124: case 125: case 126: case 127:
                        tsf_channel_note_off_all(f_, c);
                        break;
                    default:
                        tsf_channel_midi_control(f_, c, d1, d2);
                        break;
                }
                break;
            case 0xC0:
                tsf_channel_set_presetnumber(f_, c, d1, c == 9 ? 1 : 0);
                break;
            case 0xE0:
                tsf_channel_set_pitchwheel(f_, c, d1 | (d2 << 7));
                break;
            default:
                break;  // aftertouch: not supported
        }
    }
    void sysex(const uint8_t* m, size_t n) override {
        if (isResetSysex(m, n)) {
            for (int c = 0; c < 16; ++c) {
                tsf_channel_set_sustain(f_, c, 0);
                tsf_channel_note_off_all(f_, c);
                initChannel(c);
            }
        }
    }
    void render(float* out, int frames) override { tsf_render_float(f_, out, frames, 1); }
};

}  // namespace

std::unique_ptr<MidiSynth> createTsfSynth(const std::string& path, int rate) {
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) return nullptr;
    tsf_stream s;
    s.data = rw;
    s.read = rwRead;
    s.skip = rwSkip;
    tsf* f = tsf_load(&s);
    SDL_RWclose(rw);
    if (!f) return nullptr;
    if (tsf_get_presetcount(f) <= 0) {
        tsf_close(f);
        return nullptr;
    }
    return std::unique_ptr<MidiSynth>(new TsfSynth(f, rate));
}

}  // namespace detail
}  // namespace snd
