// Built-in General-MIDI-ish software synthesizer (no SoundFont needed).
// 32 voices; each GM instrument family has a simple patch (FM, band-limited
// saw/square, additive organ, filtered and enveloped); channel 10 uses a small
// synthesized drum kit. Output level is kept low so that typical pieces do not clip.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "sound/snd_internal.h"

namespace snd {
namespace detail {

namespace {

const double kPi = 3.14159265358979323846;
const double kTwoPi = 2.0 * kPi;

enum Osc : uint8_t { SINE, TRI, SAW, SQUARE, PULSE, FM, ORGAN, NOISE };

struct Patch {
    Osc osc;
    float a, d, s, r;     // attack/decay/release in seconds, sustain level 0..1
    float fmRatio, fmIndex, fmDecay;  // FM modulator ratio, index, index decay time
    float cutoff;         // low-pass cutoff as a multiple of the note frequency (0 = off)
    float fltEnv;         // additional cutoff multiple at note-on, decaying with `d`
    float detune;         // cents of a second detuned oscillator (0 = none)
    float gain;
    float vib;            // vibrato depth in semitones (delayed)
};

//                     osc    a      d     s     r     fmR  fmI   fmD   cut   fEnv det  gain  vib
const Patch kPiano   {FM,     0.002f, 1.6f, 0.0f, 0.25f, 1.0f, 1.6f, 0.6f, 0,    0,   0,   1.0f, 0};
const Patch kEPiano  {FM,     0.002f, 2.0f, 0.0f, 0.3f, 1.0f, 0.9f, 0.25f, 0,   0,   0,   1.0f, 0};
const Patch kHarpsi  {SAW,    0.001f, 1.2f, 0.0f, 0.15f, 0,   0,    0,    3.0f, 12.f, 0,   0.6f, 0};
const Patch kClav    {PULSE,  0.001f, 0.8f, 0.0f, 0.08f, 0,   0,    0,    2.5f, 10.f, 0,   0.6f, 0};
const Patch kBell    {FM,     0.001f, 2.5f, 0.0f, 0.8f, 3.5f, 2.0f, 1.2f, 0,    0,   0,   0.8f, 0};
const Patch kMallet  {FM,     0.001f, 0.6f, 0.0f, 0.3f, 4.0f, 1.2f, 0.1f, 0,    0,   0,   0.9f, 0};
const Patch kOrgan   {ORGAN,  0.01f,  0.1f, 1.0f, 0.06f, 0,   0,    0,    0,    0,   0,   0.55f, 0};
const Patch kGuitar  {SAW,    0.002f, 1.5f, 0.0f, 0.2f, 0,    0,    0,    2.0f, 8.f,  0,   0.75f, 0};
const Patch kEGuitar {SAW,    0.002f, 2.0f, 0.25f, 0.2f, 0,   0,    0,    3.0f, 6.f,  0,   0.6f, 0.1f};
const Patch kDistGtr {SQUARE, 0.003f, 2.0f, 0.5f, 0.15f, 0,   0,    0,    5.0f, 4.f, 6.f,  0.4f, 0.1f};
const Patch kBass    {FM,     0.002f, 1.0f, 0.35f, 0.08f, 1.0f, 1.1f, 0.3f, 0,  0,   0,   1.1f, 0};
const Patch kSynBass {SAW,    0.002f, 0.4f, 0.5f, 0.08f, 0,   0,    0,    2.0f, 10.f, 0,   0.8f, 0};
const Patch kStrings {SAW,    0.12f,  0.4f, 0.85f, 0.35f, 0,  0,    0,    3.0f, 0,   7.f,  0.4f, 0.12f};
const Patch kPizz    {TRI,    0.002f, 0.35f, 0.0f, 0.1f, 0,   0,    0,    0,    0,   0,   1.0f, 0};
const Patch kHarp    {FM,     0.002f, 1.5f, 0.0f, 0.4f, 2.0f, 0.6f, 0.2f, 0,    0,   0,   0.9f, 0};
const Patch kTimpani {FM,     0.002f, 1.2f, 0.0f, 0.4f, 1.41f, 1.0f, 0.2f, 0,   0,   0,   1.2f, 0};
const Patch kChoir   {TRI,    0.15f,  0.4f, 0.85f, 0.4f, 0,   0,    0,    0,    0,   8.f,  0.6f, 0.15f};
const Patch kBrass   {SAW,    0.04f,  0.3f, 0.75f, 0.12f, 0,  0,    0,    2.0f, 4.f,  4.f,  0.45f, 0.1f};
const Patch kReed    {SQUARE, 0.03f,  0.3f, 0.8f, 0.1f, 0,    0,    0,    2.5f, 2.f,  0,   0.45f, 0.12f};
const Patch kPipe    {SINE,   0.05f,  0.3f, 0.85f, 0.12f, 0,  0,    0,    0,    0,   0,   0.9f, 0.15f};
const Patch kLead    {SQUARE, 0.005f, 0.3f, 0.8f, 0.1f, 0,    0,    0,    4.0f, 2.f,  5.f,  0.35f, 0.1f};
const Patch kLeadSaw {SAW,    0.005f, 0.3f, 0.8f, 0.1f, 0,    0,    0,    4.0f, 2.f,  5.f,  0.35f, 0.1f};
const Patch kPad     {SAW,    0.35f,  1.0f, 0.8f, 0.8f, 0,    0,    0,    1.5f, 0,   9.f,  0.4f, 0.08f};
const Patch kFx      {FM,     0.1f,   1.5f, 0.5f, 0.8f, 2.01f, 1.0f, 2.0f, 0,   0,   0,   0.7f, 0.1f};
const Patch kEthnic  {FM,     0.002f, 1.0f, 0.0f, 0.2f, 2.0f, 1.0f, 0.15f, 0,   0,   0,   0.9f, 0};
const Patch kWoodDrm {FM,     0.001f, 0.25f, 0.0f, 0.1f, 1.5f, 1.5f, 0.05f, 0,  0,   0,   1.0f, 0};
const Patch kSfx     {NOISE,  0.05f,  1.0f, 0.4f, 0.4f, 0,    0,    0,    4.0f, 0,   0,   0.4f, 0};

const Patch& patchFor(int prog) {
    switch (prog) {
        case 0: case 1: case 2: case 3: return kPiano;
        case 4: case 5: return kEPiano;
        case 6: return kHarpsi;
        case 7: return kClav;
        case 8: case 9: case 10: case 14: return kBell;
        case 11: case 12: case 13: case 15: return kMallet;
        case 24: case 25: return kGuitar;
        case 26: case 27: case 28: return kEGuitar;
        case 29: case 30: return kDistGtr;
        case 31: return kGuitar;
        case 38: case 39: return kSynBass;
        case 45: return kPizz;
        case 46: return kHarp;
        case 47: return kTimpani;
        case 52: case 53: case 54: return kChoir;
        case 55: return kTimpani;
        case 80: case 84: case 85: case 86: case 87: return kLead;
        case 81: case 82: case 83: return kLeadSaw;
        case 112: case 113: case 114: return kBell;
        case 115: case 116: case 117: case 118: case 119: return kWoodDrm;
        default: break;
    }
    switch (prog / 8) {
        case 2: return kOrgan;
        case 4: return kBass;
        case 5: case 6: return kStrings;
        case 7: return kBrass;
        case 8: return kReed;
        case 9: return kPipe;
        case 11: return kPad;
        case 12: return kFx;
        case 13: return kEthnic;
        case 15: return kSfx;
        default: return kPiano;
    }
}

// ---- drum kit --------------------------------------------------------------------
struct Drum {
    float t0, t1, sweep;  // tone start/end frequency (Hz), sweep time constant (s)
    float tdec, tamp;     // tone decay (time to -60 dB), amplitude
    float ndec, namp;     // noise decay, amplitude
    float nhp, nlp;       // noise high-pass / low-pass cutoff (Hz, 0 = off)
    uint8_t toneOsc;      // 0 sine, 1 square (metallic)
    float t2;             // second tone frequency (0 = none)
    int muteGroup;        // 1 = hi-hat
};

Drum drumFor(int key) {
    //            t0     t1     sweep  tdec  tamp  ndec  namp  nhp    nlp    osc t2   grp
    switch (key) {
        case 35: return {110, 42, 0.05f, 0.5f, 1.1f, 0.02f, 0.15f, 1500, 0, 0, 0, 0};
        case 36: return {150, 48, 0.04f, 0.45f, 1.1f, 0.02f, 0.2f, 2000, 0, 0, 0, 0};
        case 37: return {820, 620, 0.01f, 0.06f, 0.4f, 0.05f, 0.35f, 2000, 9000, 0, 0, 0};
        case 38: return {230, 185, 0.02f, 0.15f, 0.55f, 0.28f, 0.7f, 900, 9000, 0, 0, 0};
        case 39: return {0, 0, 0, 0, 0, 0.2f, 0.9f, 900, 3500, 0, 0, 0};
        case 40: return {260, 200, 0.02f, 0.12f, 0.5f, 0.22f, 0.75f, 1300, 11000, 0, 0, 0};
        case 41: return {120, 82, 0.1f, 0.6f, 0.9f, 0.05f, 0.1f, 800, 0, 0, 0, 0};
        case 43: return {135, 95, 0.1f, 0.55f, 0.9f, 0.05f, 0.1f, 800, 0, 0, 0, 0};
        case 45: return {155, 110, 0.1f, 0.5f, 0.9f, 0.05f, 0.1f, 800, 0, 0, 0, 0};
        case 47: return {180, 130, 0.1f, 0.45f, 0.9f, 0.05f, 0.1f, 800, 0, 0, 0, 0};
        case 48: return {205, 150, 0.1f, 0.42f, 0.9f, 0.05f, 0.1f, 800, 0, 0, 0, 0};
        case 50: return {235, 175, 0.1f, 0.4f, 0.9f, 0.05f, 0.1f, 800, 0, 0, 0, 0};
        case 42: return {0, 0, 0, 0, 0, 0.07f, 0.45f, 7000, 0, 0, 0, 1};
        case 44: return {0, 0, 0, 0, 0, 0.08f, 0.35f, 6000, 0, 0, 0, 1};
        case 46: return {0, 0, 0, 0, 0, 0.6f, 0.4f, 6500, 0, 0, 0, 1};
        case 49: return {0, 0, 0, 0, 0, 1.8f, 0.45f, 3500, 0, 0, 0, 0};
        case 57: return {0, 0, 0, 0, 0, 1.8f, 0.45f, 3000, 0, 0, 0, 0};
        case 51: return {3100, 3100, 0, 0.9f, 0.04f, 1.2f, 0.25f, 5000, 0, 1, 0, 0};
        case 59: return {2900, 2900, 0, 0.9f, 0.04f, 1.2f, 0.25f, 4500, 0, 1, 0, 0};
        case 52: return {0, 0, 0, 0, 0, 1.4f, 0.45f, 2200, 0, 0, 0, 0};
        case 53: return {1250, 1250, 0, 1.0f, 0.3f, 0.5f, 0.15f, 5000, 0, 0, 2950, 0};
        case 54: return {0, 0, 0, 0, 0, 0.25f, 0.4f, 6000, 0, 0, 0, 0};
        case 55: return {0, 0, 0, 0, 0, 0.9f, 0.4f, 4500, 0, 0, 0, 0};
        case 56: return {560, 560, 0, 0.35f, 0.25f, 0, 0, 0, 0, 1, 845, 0};
        case 58: return {0, 0, 0, 0, 0, 0.8f, 0.3f, 2000, 5000, 0, 0, 0};
        case 60: return {420, 390, 0.02f, 0.18f, 0.8f, 0.02f, 0.05f, 2000, 0, 0, 0, 0};
        case 61: return {310, 285, 0.02f, 0.22f, 0.8f, 0.02f, 0.05f, 2000, 0, 0, 0, 0};
        case 62: return {340, 330, 0.01f, 0.1f, 0.8f, 0.02f, 0.05f, 2000, 0, 0, 0, 0};
        case 63: return {340, 320, 0.02f, 0.35f, 0.8f, 0.02f, 0.05f, 2000, 0, 0, 0, 0};
        case 64: return {255, 240, 0.02f, 0.4f, 0.8f, 0.02f, 0.05f, 2000, 0, 0, 0, 0};
        case 65: return {470, 450, 0.01f, 0.3f, 0.6f, 0.15f, 0.2f, 3000, 0, 0, 0, 0};
        case 66: return {360, 340, 0.01f, 0.35f, 0.6f, 0.15f, 0.2f, 3000, 0, 0, 0, 0};
        case 67: return {920, 920, 0, 0.35f, 0.35f, 0, 0, 0, 0, 0, 1380, 0};
        case 68: return {660, 660, 0, 0.35f, 0.35f, 0, 0, 0, 0, 0, 990, 0};
        case 69: return {0, 0, 0, 0, 0, 0.1f, 0.3f, 5000, 0, 0, 0, 0};
        case 70: return {0, 0, 0, 0, 0, 0.07f, 0.3f, 6000, 0, 0, 0, 0};
        case 71: return {2300, 2350, 0.05f, 0.25f, 0.3f, 0, 0, 0, 0, 0, 0, 0};
        case 72: return {2300, 2350, 0.2f, 0.9f, 0.3f, 0, 0, 0, 0, 0, 0, 0};
        case 73: return {0, 0, 0, 0, 0, 0.12f, 0.35f, 2500, 6000, 0, 0, 0};
        case 74: return {0, 0, 0, 0, 0, 0.4f, 0.35f, 2500, 6000, 0, 0, 0};
        case 75: case 31: return {2500, 2500, 0, 0.08f, 0.5f, 0, 0, 0, 0, 0, 0, 0};
        case 76: return {1650, 1600, 0.005f, 0.1f, 0.6f, 0, 0, 0, 0, 0, 0, 0};
        case 77: return {1200, 1150, 0.005f, 0.12f, 0.6f, 0, 0, 0, 0, 0, 0, 0};
        case 78: return {650, 950, 0.03f, 0.15f, 0.4f, 0, 0, 0, 0, 0, 0, 0};
        case 79: return {520, 720, 0.08f, 0.4f, 0.4f, 0, 0, 0, 0, 0, 0, 0};
        case 80: return {4200, 4200, 0, 0.15f, 0.22f, 0, 0, 0, 0, 0, 0, 0};
        case 81: return {4200, 4200, 0, 1.6f, 0.22f, 0, 0, 0, 0, 0, 0, 0};
        case 33: case 32: return {1800, 1800, 0, 0.04f, 0.4f, 0, 0, 0, 0, 0, 0, 0};
        case 34: return {2600, 2600, 0, 0.5f, 0.3f, 0, 0, 0, 0, 0, 0, 0};
        case 82: return {0, 0, 0, 0, 0, 0.12f, 0.3f, 5000, 0, 0, 0, 0};
        case 83: return {3200, 3200, 0, 0.5f, 0.12f, 0.3f, 0.15f, 6000, 0, 1, 4700, 0};
        case 84: return {3600, 3600, 0, 1.5f, 0.12f, 0, 0, 0, 0, 0, 5200, 0};
        case 85: return {0, 0, 0, 0, 0, 0.05f, 0.4f, 3000, 8000, 0, 0, 0};
        case 86: return {100, 90, 0.02f, 0.15f, 0.9f, 0.02f, 0.1f, 1500, 0, 0, 0, 0};
        case 87: return {100, 85, 0.05f, 0.6f, 0.9f, 0.02f, 0.1f, 1500, 0, 0, 0, 0};
        default: return {0, 0, 0, 0, 0, 0.12f, 0.35f, 2500, 8000, 0, 0, 0};
    }
}

inline float polyBlep(double t, double dt) {
    if (dt <= 0) return 0;
    if (t < dt) {
        t /= dt;
        return (float)(t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return (float)(t * t + t + t + 1.0);
    }
    return 0;
}

// -60 dB decay coefficient per sample for a time t (seconds)
inline float decayCoef(float t, int rate) {
    if (t <= 0) return 0;
    return (float)std::exp(-6.907755 / ((double)t * rate));
}

struct Chan {
    int prog = 0;
    int bankMsb = 0;
    float vol = 100 / 127.0f, expr = 1.0f;
    int pan = 64;
    bool sustain = false;
    int bend = 8192;
    float bendRange = 2.0f;
    int rpnMsb = 127, rpnLsb = 127;
    float mod = 0;
    bool drums = false;
};

enum EnvStage : uint8_t { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

struct Voice {
    bool active = false;
    bool drum = false;
    int ch = 0, key = 0;
    float velGain = 0;
    uint32_t age = 0;
    bool released = false;  // note-off received
    bool held = false;      // note-off received while the sustain pedal was down
    // melodic
    const Patch* p = nullptr;
    double phase = 0, phase2 = 0, modPhase = 0;
    double baseFreq = 0;
    EnvStage stage = ENV_OFF;
    float env = 0;
    float attackInc = 0, decayCoef = 0, releaseCoef = 0;
    float fmIdx = 0, fmIdxCoef = 0;
    float fltEnv = 0, fltCoef = 0;
    float lp1 = 0, bp1 = 0;  // SVF state
    float t = 0;             // time since note-on (s)
    // drum
    Drum dr{};
    float toneEnv = 0, noiseEnv = 0, toneC = 0, noiseC = 0;
    double toneFreq = 0;
    float sweepC = 0;
    float hpState = 0, nlpState = 0;
    float relC = 0;  // quick release multiplier (choke)
};

class Builtin : public MidiSynth {
public:
    explicit Builtin(int rate) : rate_(rate) { reset(); }

    void reset() override {
        for (Voice& v : voices_) quickRelease(v);  // fade out instead of cutting (no click)
        for (int c = 0; c < 16; ++c) {
            ch_[c] = Chan();
            ch_[c].drums = (c == 9);
        }
    }
    void allSoundOff() override {
        for (Voice& v : voices_) quickRelease(v);
        for (Chan& c : ch_) c.sustain = false;
    }
    void shortMsg(uint8_t st, uint8_t d1, uint8_t d2) override {
        int c = st & 15;
        Chan& C = ch_[c];
        switch (st & 0xF0) {
            case 0x80: noteOff(c, d1); break;
            case 0x90:
                if (d2 == 0) noteOff(c, d1);
                else noteOn(c, d1, d2);
                break;
            case 0xB0:
                switch (d1) {
                    case 0: C.bankMsb = d2; break;
                    case 1: C.mod = d2 / 127.0f; break;
                    case 6:
                        if (C.rpnMsb == 0 && C.rpnLsb == 0) C.bendRange = (float)d2;
                        break;
                    case 38:
                        if (C.rpnMsb == 0 && C.rpnLsb == 0) C.bendRange = std::floor(C.bendRange) + d2 / 100.0f;
                        break;
                    case 7: C.vol = d2 / 127.0f; break;
                    case 10: C.pan = d2; break;
                    case 11: C.expr = d2 / 127.0f; break;
                    case 64: {
                        bool on = d2 >= 64;
                        if (C.sustain && !on) {
                            for (Voice& v : voices_)
                                if (v.active && v.ch == c && v.held) {
                                    v.held = false;
                                    release(v);
                                }
                        }
                        C.sustain = on;
                        break;
                    }
                    case 98: case 99: C.rpnMsb = C.rpnLsb = 127; break;
                    case 100: C.rpnLsb = d2; break;
                    case 101: C.rpnMsb = d2; break;
                    case 120:
                        for (Voice& v : voices_)
                            if (v.active && v.ch == c) quickRelease(v);
                        break;
                    case 121:
                        C.expr = 1.0f;
                        C.bend = 8192;
                        C.mod = 0;
                        C.rpnMsb = C.rpnLsb = 127;
                        if (C.sustain) shortMsg((uint8_t)(0xB0 | c), 64, 0);
                        break;
                    case 123: case 124: case 125: case 126: case 127:
                        for (Voice& v : voices_)
                            if (v.active && v.ch == c && !v.released) {
                                if (C.sustain) {
                                    v.released = true;
                                    v.held = true;
                                } else {
                                    release(v);
                                }
                            }
                        break;
                    default: break;
                }
                break;
            case 0xC0: C.prog = d1; break;
            case 0xE0: C.bend = d1 | (d2 << 7); break;
            default: break;
        }
    }
    void sysex(const uint8_t* m, size_t n) override {
        if (isResetSysex(m, n)) {
            for (Voice& v : voices_) quickRelease(v);
            for (int c = 0; c < 16; ++c) {
                ch_[c] = Chan();
                ch_[c].drums = (c == 9);
            }
        }
    }

    void render(float* out, int frames) override {
        while (frames > 0) {
            int n = frames > kBlock ? kBlock : frames;
            for (Voice& v : voices_) {
                if (!v.active) continue;
                if (v.drum) renderDrum(v, out, n);
                else renderMelodic(v, out, n);
            }
            out += n * 2;
            frames -= n;
        }
    }

private:
    static const int kVoices = 32;
    static const int kBlock = 32;
    const float kOutGain = 0.24f;
    int rate_;
    Voice voices_[kVoices];
    Chan ch_[16];
    uint32_t ageCounter_ = 0;
    uint32_t rng_ = 0x12345678u;

    float noise() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (float)((int32_t)rng_) * (1.0f / 2147483648.0f);
    }

    void release(Voice& v) {
        v.released = true;
        if (v.drum) return;  // drums ring out
        if (v.stage != ENV_RELEASE) {
            v.stage = ENV_RELEASE;
            v.releaseCoef = decayCoef(v.p->r, rate_);
        }
    }
    void quickRelease(Voice& v) {
        if (!v.active) return;
        v.released = true;
        v.held = false;
        if (v.drum) {
            v.relC = decayCoef(0.03f, rate_);
        } else {
            v.stage = ENV_RELEASE;
            v.releaseCoef = decayCoef(0.03f, rate_);
        }
    }
    void noteOff(int c, int key) {
        for (Voice& v : voices_) {
            if (v.active && v.ch == c && v.key == key && !v.released) {
                if (ch_[c].sustain) {
                    v.released = true;
                    v.held = true;
                } else {
                    release(v);
                }
            }
        }
    }
    Voice& allocVoice() {
        Voice* best = nullptr;
        for (Voice& v : voices_)
            if (!v.active) return v;
        // steal: a released voice with the lowest level, else the oldest
        float bestLvl = 1e9f;
        for (Voice& v : voices_) {
            if (v.released) {
                float lvl = v.drum ? (v.toneEnv + v.noiseEnv) : v.env;
                if (lvl < bestLvl) {
                    bestLvl = lvl;
                    best = &v;
                }
            }
        }
        if (best) return *best;
        uint32_t oldest = 0;
        for (Voice& v : voices_) {
            uint32_t a = ageCounter_ - v.age;
            if (!best || a > oldest) {
                oldest = a;
                best = &v;
            }
        }
        return *best;
    }
    void noteOn(int c, int key, int vel) {
        Chan& C = ch_[c];
        // retrigger of the same key: release the old voice
        for (Voice& v : voices_)
            if (v.active && v.ch == c && v.key == key) {
                if (C.drums) quickRelease(v);
                else if (!v.released || v.held) quickRelease(v);
            }
        if (C.drums) {
            Drum d = drumFor(key);
            if (d.muteGroup) {
                for (Voice& v : voices_)
                    if (v.active && v.drum && v.ch == c && v.dr.muteGroup == d.muteGroup)
                        quickRelease(v);
            }
        }
        Voice& v = allocVoice();
        v = Voice();
        v.active = true;
        v.ch = c;
        v.key = key;
        float vn = vel / 127.0f;
        v.velGain = vn * vn;
        v.age = ++ageCounter_;
        if (C.drums) {
            v.drum = true;
            v.dr = drumFor(key);
            v.toneEnv = v.dr.tamp;
            v.noiseEnv = v.dr.namp;
            v.toneC = decayCoef(v.dr.tdec, rate_);
            v.noiseC = decayCoef(v.dr.ndec, rate_);
            v.toneFreq = v.dr.t0;
            v.sweepC = v.dr.sweep > 0 ? (float)std::exp(-1.0 / (v.dr.sweep * rate_)) : 0.0f;
            v.relC = 1.0f;
            return;
        }
        const Patch& p = patchFor(C.prog);
        v.p = &p;
        v.baseFreq = 440.0 * std::pow(2.0, (key - 69) / 12.0);
        v.stage = ENV_ATTACK;
        v.env = 0;
        v.attackInc = p.a > 0 ? 1.0f / (p.a * rate_) : 1.0f;
        v.decayCoef = decayCoef(p.d, rate_);
        v.fmIdx = p.fmIndex * (0.5f + 0.5f * vn);
        v.fmIdxCoef = p.fmDecay > 0 ? (float)std::exp(-1.0 / (p.fmDecay * rate_)) : 1.0f;
        v.fltEnv = p.fltEnv;
        v.fltCoef = decayCoef(p.d * 0.5f, rate_);
        v.phase = 0;
        v.phase2 = 0.25;
        v.modPhase = 0;
    }

    void panGains(int ch, float& gl, float& gr) {
        const Chan& C = ch_[ch];
        float pan = (C.pan <= 0 ? 0.0f : (C.pan - 1) / 126.0f);
        if (C.pan == 64) pan = 0.5f;
        if (pan < 0) pan = 0;
        if (pan > 1) pan = 1;
        float g = C.vol * C.vol * C.expr * C.expr * kOutGain;
        gl = g * (float)std::cos(pan * kPi * 0.5) * 1.41421356f;
        gr = g * (float)std::sin(pan * kPi * 0.5) * 1.41421356f;
        if (gl > g * 1.0f) gl = g;  // centre is unity, sides max unity
        if (gr > g * 1.0f) gr = g;
    }

    void renderMelodic(Voice& v, float* out, int n) {
        const Patch& p = *v.p;
        const Chan& C = ch_[v.ch];
        float gl, gr;
        panGains(v.ch, gl, gr);
        gl *= v.velGain * p.gain;
        gr *= v.velGain * p.gain;
        double semis = (C.bend - 8192) / 8192.0 * C.bendRange;
        // vibrato: modulation wheel plus the patch's delayed vibrato
        float vibDepth = C.mod * 0.5f + (v.t > 0.35f ? p.vib * std::min(1.0f, (v.t - 0.35f) * 2.0f) : 0.0f);
        if (vibDepth > 0) semis += vibDepth * std::sin(kTwoPi * 5.5 * v.t);
        double freq = v.baseFreq * std::pow(2.0, semis / 12.0);
        double dt = freq / rate_;
        if (dt > 0.45) dt = 0.45;
        double dt2 = p.detune != 0 ? dt * std::pow(2.0, p.detune / 1200.0) : 0;
        // filter coefficient for this block
        float fc = 0, fq = 0;
        bool filt = p.cutoff > 0;
        if (filt) {
            double cutoff = freq * (p.cutoff + v.fltEnv) * (0.6 + 0.4 * std::sqrt(v.velGain));
            if (cutoff > rate_ * 0.16) cutoff = rate_ * 0.16;
            if (cutoff < 30) cutoff = 30;
            fc = (float)(2.0 * std::sin(kPi * cutoff / rate_));
            fq = 1.0f;
        }
        for (int i = 0; i < n; ++i) {
            // envelope
            switch (v.stage) {
                case ENV_ATTACK:
                    v.env += v.attackInc;
                    if (v.env >= 1.0f) {
                        v.env = 1.0f;
                        v.stage = ENV_DECAY;
                    }
                    break;
                case ENV_DECAY:
                    v.env = p.s + (v.env - p.s) * v.decayCoef;
                    if (p.s <= 0 && v.env < 1e-4f) v.stage = ENV_OFF;
                    break;
                case ENV_SUSTAIN: break;
                case ENV_RELEASE:
                    v.env *= v.releaseCoef;
                    if (v.env < 1e-4f) v.stage = ENV_OFF;
                    break;
                case ENV_OFF: break;
            }
            if (v.stage == ENV_OFF) {
                v.active = false;
                return;
            }
            float s;
            double ph = v.phase;
            switch (p.osc) {
                case SINE: s = (float)std::sin(kTwoPi * ph); break;
                case TRI: s = (float)(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
                case SAW: s = (float)(2.0 * ph - 1.0) - polyBlep(ph, dt); break;
                case SQUARE: {
                    double p2 = ph + 0.5;
                    if (p2 >= 1.0) p2 -= 1.0;
                    s = (ph < 0.5 ? 1.0f : -1.0f) + polyBlep(ph, dt) - polyBlep(p2, dt);
                    s *= 0.7f;
                    break;
                }
                case PULSE: {
                    double p2 = ph + 0.75;
                    if (p2 >= 1.0) p2 -= 1.0;
                    s = (ph < 0.25 ? 1.0f : -1.0f) + polyBlep(ph, dt) - polyBlep(p2, dt);
                    s = (s + 0.5f) * 0.7f;
                    break;
                }
                case FM: {
                    double m = std::sin(kTwoPi * v.modPhase);
                    s = (float)std::sin(kTwoPi * ph + v.fmIdx * m);
                    v.modPhase += dt * p.fmRatio;
                    if (v.modPhase >= 1.0) v.modPhase -= std::floor(v.modPhase);
                    v.fmIdx *= v.fmIdxCoef;
                    break;
                }
                case ORGAN: {
                    double a = kTwoPi * ph;
                    s = (float)(0.6 * std::sin(a) + 0.3 * std::sin(2 * a) + 0.2 * std::sin(3 * a) +
                                (dt * 4 < 0.45 ? 0.12 * std::sin(4 * a) : 0.0));
                    break;
                }
                case NOISE:
                default: s = noise() * 0.6f + (float)std::sin(kTwoPi * ph) * 0.3f; break;
            }
            if (dt2 > 0) {
                double q = v.phase2;
                float s2;
                if (p.osc == SAW) s2 = (float)(2.0 * q - 1.0) - polyBlep(q, dt2);
                else if (p.osc == SQUARE) {
                    double q2 = q + 0.5;
                    if (q2 >= 1.0) q2 -= 1.0;
                    s2 = ((q < 0.5 ? 1.0f : -1.0f) + polyBlep(q, dt2) - polyBlep(q2, dt2)) * 0.7f;
                } else if (p.osc == TRI) s2 = (float)(q < 0.5 ? 4.0 * q - 1.0 : 3.0 - 4.0 * q);
                else s2 = (float)std::sin(kTwoPi * q);
                s = (s + s2) * 0.6f;
                v.phase2 += dt2;
                if (v.phase2 >= 1.0) v.phase2 -= 1.0;
            }
            v.phase += dt;
            if (v.phase >= 1.0) v.phase -= 1.0;
            if (filt) {
                float hp = s - v.lp1 - fq * v.bp1;
                v.bp1 += fc * hp;
                v.lp1 += fc * v.bp1;
                s = v.lp1;
            }
            float a = s * v.env;
            out[2 * i] += a * gl;
            out[2 * i + 1] += a * gr;
        }
        v.fltEnv *= std::pow(v.fltCoef, (float)n);
        v.t += (float)n / (float)rate_;
    }

    void renderDrum(Voice& v, float* out, int n) {
        float gl, gr;
        panGains(v.ch, gl, gr);
        gl *= v.velGain * 1.2f;
        gr *= v.velGain * 1.2f;
        const Drum& d = v.dr;
        float hpA = d.nhp > 0 ? (float)std::exp(-kTwoPi * d.nhp / rate_) : 0.0f;
        float lpA = d.nlp > 0 ? 1.0f - (float)std::exp(-kTwoPi * d.nlp / rate_) : 1.0f;
        for (int i = 0; i < n; ++i) {
            float s = 0;
            if (v.toneEnv > 1e-5f) {
                double ph = v.phase;
                float t;
                if (d.toneOsc == 1) t = ph < 0.5 ? 0.5f : -0.5f;
                else t = (float)std::sin(kTwoPi * ph);
                if (d.t2 > 0) {
                    float t2 = (float)std::sin(kTwoPi * v.phase2);
                    t = (t + t2) * 0.6f;
                    v.phase2 += d.t2 / rate_;
                    if (v.phase2 >= 1.0) v.phase2 -= 1.0;
                }
                s += t * v.toneEnv;
                v.toneEnv *= v.toneC;
                v.phase += v.toneFreq / rate_;
                if (v.phase >= 1.0) v.phase -= 1.0;
                if (v.sweepC > 0) v.toneFreq = d.t1 + (v.toneFreq - d.t1) * v.sweepC;
            } else {
                v.toneEnv = 0;
            }
            if (v.noiseEnv > 1e-5f) {
                float x = noise();
                // high-pass (one pole) then low-pass (one pole)
                v.hpState = hpA * v.hpState + (1.0f - hpA) * x;
                float h = d.nhp > 0 ? x - v.hpState : x;
                v.nlpState += lpA * (h - v.nlpState);
                s += v.nlpState * v.noiseEnv;
                v.noiseEnv *= v.noiseC;
            } else {
                v.noiseEnv = 0;
            }
            if (v.relC < 1.0f) {
                v.toneEnv *= v.relC;
                v.noiseEnv *= v.relC;
            }
            out[2 * i] += s * gl;
            out[2 * i + 1] += s * gr;
        }
        if (v.toneEnv <= 1e-5f && v.noiseEnv <= 1e-5f) v.active = false;
    }
};

}  // namespace

std::unique_ptr<MidiSynth> createBuiltinSynth(int sampleRate) {
    return std::unique_ptr<MidiSynth>(new Builtin(sampleRate));
}

}  // namespace detail
}  // namespace snd
