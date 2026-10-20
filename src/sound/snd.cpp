// Low-level sound device of the RM95 runtime (see snd.h): SDL2 audio output,
// wave channels with the loaded-sound list and sound numbering of
// 09-audio-movies.md §4.3, and the MIDI player (sequencer inside the audio
// callback for the software synthesizers, own thread for the Windows native output).
#include "sound/snd.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "sound/snd_internal.h"

namespace snd {

using detail::MidiSong;
using detail::MidiSynth;
using detail::NativeMidi;
using detail::WaveData;

namespace {

const int kWaveChannels = 4;
const int kMaxSoundNumber = 255;
const int kChunk = 512;  // frames processed per mixing step

enum class Backend { None, Sf2, Native, Builtin };

struct Sound {
    std::shared_ptr<const WaveData> wave;
    int number = 0;  // 0 = no number
    bool playing = true;
    bool loop = false;
    double pos = 0;   // position in source frames
    int freq = 0;     // playback rate, 0 = file rate
    int volume = 0;   // hundredths of dB (<= 0)
    int pan = 0;      // hundredths of dB
    float tgtL = 1, tgtR = 1, curL = 1, curR = 1;
    bool started = false;
};

struct State {
    SDL_mutex* mtx = nullptr;
    SDL_AudioDeviceID dev = 0;
    bool weInitedAudio = false;
    int outRate = 44100;

    // wave
    bool waveOpen = false;
    std::vector<std::unique_ptr<Sound>> sounds;  // loaded sounds, in load order
    Sound* chan[kWaveChannels] = {nullptr, nullptr, nullptr, nullptr};
    // copies of sounds that were cut off; mixed for one more chunk while their
    // gain ramps down to 0 (avoids clicks), then discarded
    std::vector<std::unique_ptr<Sound>> fading;

    // configuration
    std::string sf2Path;
    std::string backendName = "auto";
    int masterPercent = 100;
    float masterGain = 1.0f;

    // MIDI
    bool midiInitDone = false;
    Backend backend = Backend::None;
    std::string backendDesc = "none";
    std::unique_ptr<MidiSynth> synth;
    std::unique_ptr<NativeMidi> native;
    std::shared_ptr<const MidiSong> song;
    size_t nextEvent = 0;
    uint64_t midiPos = 0;  // samples since the start of the piece
    bool midiPlaying = false;
    bool midiEnded = false;
    float midiGainL = 1.0f, midiGainR = 1.0f;

    // output limiter
    float limGain = 1.0f;
    float limRelease = 0.0f;

    float midiBuf[kChunk * 2];
};

State* g = nullptr;

State& S() {
    if (!g) {
        g = new State();  // intentionally never deleted (audio thread may outlive statics)
        g->mtx = SDL_CreateMutex();
    }
    return *g;
}

struct Lock {
    State& s;
    explicit Lock(State& st) : s(st) {
        if (s.mtx) SDL_LockMutex(s.mtx);
    }
    ~Lock() {
        if (s.mtx) SDL_UnlockMutex(s.mtx);
    }
};

// ---------------------------------------------------------------------------------
// mixing (audio thread, state locked)
// ---------------------------------------------------------------------------------
void updateGains(Sound& s) {
    int left = s.volume - (s.pan > 0 ? s.pan : 0);
    int right = s.volume + (s.pan < 0 ? s.pan : 0);
    s.tgtL = (float)std::pow(10.0, left / 2000.0);
    s.tgtR = (float)std::pow(10.0, right / 2000.0);
    if (!s.started) {
        s.curL = s.tgtL;
        s.curR = s.tgtR;
    }
}

void mixSound(State& st, Sound& s, float* out, int frames) {
    const WaveData& w = *s.wave;
    const size_t n = w.frames();
    if (n == 0) {
        s.playing = false;
        return;
    }
    if (!s.started) {
        s.started = true;
        s.curL = s.tgtL;
        s.curR = s.tgtR;
    }
    const double rate = s.freq > 0 ? s.freq : w.rate;
    const double step = rate / st.outRate;
    const float k = 1.0f / 32768.0f;
    float gl = s.curL * k, gr = s.curR * k;
    const float dl = (s.tgtL * k - gl) / (float)frames;
    const float dr = (s.tgtR * k - gr) / (float)frames;
    const int16_t* d = w.samples.data();
    const double dn = (double)n;
    double pos = s.pos;
    for (int i = 0; i < frames; ++i) {
        size_t i0 = (size_t)pos;
        if (i0 >= n) i0 = n - 1;
        float fr = (float)(pos - (double)i0);
        size_t i1 = i0 + 1;
        if (i1 >= n) i1 = s.loop ? 0 : i0;
        float l, r;
        if (w.channels == 1) {
            float a = d[i0], b = d[i1];
            l = r = a + (b - a) * fr;
        } else {
            float a0 = d[2 * i0], b0 = d[2 * i1];
            float a1 = d[2 * i0 + 1], b1 = d[2 * i1 + 1];
            l = a0 + (b0 - a0) * fr;
            r = a1 + (b1 - a1) * fr;
        }
        gl += dl;
        gr += dr;
        out[2 * i] += l * gl;
        out[2 * i + 1] += r * gr;
        pos += step;
        if (pos >= dn) {
            if (s.loop) {
                pos = std::fmod(pos, dn);
            } else {
                s.playing = false;
                break;
            }
        }
    }
    s.pos = pos;
    s.curL = s.tgtL;
    s.curR = s.tgtR;
}

inline uint64_t toSample(double t, int rate) {
    if (t <= 0) return 0;
    return (uint64_t)(t * rate + 0.5);
}

void dispatch(State& st, const MidiSong& song, const detail::MidiEvent& e) {
    if (e.status == 0xF0) st.synth->sysex(song.sysex.data() + e.sysexOff, e.sysexLen);
    else st.synth->shortMsg(e.status, e.d1, e.d2);
}

void releaseAllNotes(MidiSynth& syn) {
    for (int c = 0; c < 16; ++c) {
        syn.shortMsg((uint8_t)(0xB0 | c), 64, 0);
        syn.shortMsg((uint8_t)(0xB0 | c), 123, 0);
    }
}

// Renders MIDI into buf (zeroed by the caller), running the sequencer sample-accurately.
void renderMidi(State& st, float* buf, int frames) {
    MidiSynth* syn = st.synth.get();
    if (!syn) return;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (st.midiPlaying && st.song) {
            const MidiSong& song = *st.song;
            const size_t ne = song.events.size();
            while (st.nextEvent < ne && toSample(song.events[st.nextEvent].time, st.outRate) <= st.midiPos)
                dispatch(st, song, song.events[st.nextEvent++]);
            uint64_t endS = toSample(song.endTime, st.outRate);
            if (st.nextEvent >= ne && st.midiPos >= endS) {
                // natural end of the piece
                st.midiPlaying = false;
                st.midiEnded = true;
                st.song.reset();
                releaseAllNotes(*syn);
            } else {
                uint64_t until = st.nextEvent < ne ? toSample(song.events[st.nextEvent].time, st.outRate) : endS;
                uint64_t left = until - st.midiPos;  // > 0 here
                if (left < (uint64_t)n) n = (int)left;
            }
        }
        syn->render(buf + 2 * done, n);
        if (st.midiPlaying) st.midiPos += (uint64_t)n;
        done += n;
    }
}

void SDLCALL audioCallback(void*, Uint8* stream, int len) {
    State& st = *g;
    float* out = (float*)stream;
    const int total = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    SDL_LockMutex(st.mtx);
    for (int off = 0; off < total; off += kChunk) {
        int n = std::min(kChunk, total - off);
        float* o = out + 2 * off;
        // wave sounds
        for (auto& sp : st.sounds)
            if (sp->playing) mixSound(st, *sp, o, n);
        if (!st.fading.empty()) {
            for (auto& sp : st.fading)
                if (sp->playing) mixSound(st, *sp, o, n);
            st.fading.clear();
        }
        // MIDI
        if (st.synth) {
            memset(st.midiBuf, 0, sizeof(float) * 2 * (size_t)n);
            renderMidi(st, st.midiBuf, n);
            const float gl = st.midiGainL, gr = st.midiGainR;
            for (int i = 0; i < n; ++i) {
                o[2 * i] += st.midiBuf[2 * i] * gl;
                o[2 * i + 1] += st.midiBuf[2 * i + 1] * gr;
            }
        }
        // master volume and a peak limiter (instant attack, smooth release)
        const float mg = st.masterGain;
        const float thr = 0.94f;
        float lg = st.limGain;
        for (int i = 0; i < n; ++i) {
            float l = o[2 * i] * mg, r = o[2 * i + 1] * mg;
            float peak = std::max(std::fabs(l), std::fabs(r));
            float target = peak > thr ? thr / peak : 1.0f;
            if (target < lg) lg = target;
            else lg += (target - lg) * st.limRelease;
            l *= lg;
            r *= lg;
            o[2 * i] = l > 1.0f ? 1.0f : (l < -1.0f ? -1.0f : l);
            o[2 * i + 1] = r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
        }
        st.limGain = lg;
    }
    // Purge finished sounds without a number (only possible when 1..255 were all
    // taken): nothing can refer to them any more. The last sound in the list is
    // kept because stopAllAndUnloadButLast() keeps the last loaded sound.
    for (size_t i = 0; i + 1 < st.sounds.size();) {
        Sound* s = st.sounds[i].get();
        bool referenced = false;
        for (Sound* c : st.chan) referenced |= (c == s);
        if (s->number == 0 && !s->playing && !referenced) st.sounds.erase(st.sounds.begin() + (ptrdiff_t)i);
        else ++i;
    }
    SDL_UnlockMutex(st.mtx);
}

// ---------------------------------------------------------------------------------
// device
// ---------------------------------------------------------------------------------
bool ensureDevice(State& st) {
    if (st.dev) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SDL_Log("snd: SDL audio init failed: %s", SDL_GetError());
            return false;
        }
        st.weInitedAudio = true;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCallback;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev) {
        SDL_Log("snd: cannot open audio device: %s", SDL_GetError());
        return false;
    }
    {
        Lock lk(st);
        st.dev = dev;
        st.outRate = have.freq > 0 ? have.freq : 44100;
        st.limGain = 1.0f;
        st.limRelease = 1.0f - (float)std::exp(-1.0 / (0.2 * st.outRate));
    }
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

// ---------------------------------------------------------------------------------
// MIDI backend selection
// ---------------------------------------------------------------------------------
bool fileReadable(const std::string& p) {
    SDL_RWops* rw = SDL_RWFromFile(p.c_str(), "rb");
    if (!rw) return false;
    SDL_RWclose(rw);
    return true;
}

std::vector<std::string> soundFontCandidates(const State& st) {
    std::vector<std::string> c;
    if (!st.sf2Path.empty()) c.push_back(st.sf2Path);
    if (const char* e = std::getenv("OPENMK95_SOUNDFONT"))
        if (*e) c.push_back(e);
    if (char* base = SDL_GetBasePath()) {
        std::string b = base;
        SDL_free(base);
        for (auto& p : detail::listSoundFonts(b)) c.push_back(p);
        for (auto& p : detail::listSoundFonts(b + "soundfonts")) c.push_back(p);
    }
    const char* fixed[] = {"/usr/share/sounds/sf2/default-GM.sf2", "/usr/share/sounds/sf2/FluidR3_GM.sf2",
                           "/usr/share/sounds/sf2/TimGM6mb.sf2", "/usr/share/soundfonts/default.sf2",
                           "/usr/share/soundfonts/FluidR3_GM.sf2"};
    for (const char* f : fixed) c.push_back(f);
    return c;
}

void teardownMidi(State& st) {
    std::unique_ptr<MidiSynth> oldSynth;
    std::unique_ptr<NativeMidi> oldNative;
    {
        Lock lk(st);
        oldSynth = std::move(st.synth);
        oldNative = std::move(st.native);
        st.song.reset();
        st.midiPlaying = false;
        st.midiEnded = false;
        st.midiInitDone = false;
        st.backend = Backend::None;
        st.backendDesc = "none";
    }
    // destroyed outside the lock
}

void applyNativeVolume(State& st) {
    if (st.native) st.native->setVolume(st.midiGainL * st.masterGain, st.midiGainR * st.masterGain);
}

void ensureMidi(State& st) {
    if (st.midiInitDone) return;
    st.midiInitDone = true;
    std::string want = st.backendName;
    if (want == "none") {
        st.backend = Backend::None;
        st.backendDesc = "none";
        return;
    }
    bool trySf2 = (want == "auto" || want == "sf2");
    bool tryNative = (want == "auto" || want == "native");
#ifndef _WIN32
    if (want == "native") SDL_Log("snd: native MIDI output is only available on Windows; using builtin synth");
    tryNative = false;
#endif
    if (trySf2 && ensureDevice(st)) {
        int rate;
        {
            Lock lk(st);
            rate = st.outRate;
        }
        for (const std::string& p : soundFontCandidates(st)) {
            if (!fileReadable(p)) continue;
            std::unique_ptr<MidiSynth> syn = detail::createTsfSynth(p, rate);
            if (!syn) {
                SDL_Log("snd: cannot load SoundFont %s", p.c_str());
                continue;
            }
            Lock lk(st);
            st.synth = std::move(syn);
            st.backend = Backend::Sf2;
            st.backendDesc = "sf2:" + p;
            return;
        }
        if (want == "sf2") SDL_Log("snd: no SoundFont found; using builtin synth");
    }
    if (tryNative) {
        std::unique_ptr<NativeMidi> nm = detail::createNativeMidi();
        if (nm) {
            st.native = std::move(nm);
            st.backend = Backend::Native;
            st.backendDesc = "native";
            applyNativeVolume(st);
            return;
        }
        SDL_Log("snd: cannot open the system MIDI output; using builtin synth");
    }
    if (ensureDevice(st)) {
        int rate;
        {
            Lock lk(st);
            rate = st.outRate;
        }
        std::unique_ptr<MidiSynth> syn = detail::createBuiltinSynth(rate);
        Lock lk(st);
        st.synth = std::move(syn);
        st.backend = Backend::Builtin;
        st.backendDesc = "builtin";
        return;
    }
    st.backend = Backend::None;
    st.backendDesc = "none";
}

// Cutting a sound off: a copy fades out over the next mixing chunk.
void fadeOut(State& st, const Sound& s) {
    if (!s.playing || !s.started || !st.dev) return;
    std::unique_ptr<Sound> f(new Sound(s));
    f->tgtL = f->tgtR = 0;
    st.fading.push_back(std::move(f));
}

Sound* chanSound(State& st, int ch) {
    if (ch < 0 || ch >= kWaveChannels) return nullptr;
    return st.chan[ch];
}

void unloadSound(State& st, Sound* s) {
    for (Sound*& c : st.chan)
        if (c == s) c = nullptr;
    for (size_t i = 0; i < st.sounds.size(); ++i)
        if (st.sounds[i].get() == s) {
            fadeOut(st, *s);
            st.sounds.erase(st.sounds.begin() + (ptrdiff_t)i);
            break;
        }
}

}  // namespace

// =================================================================================
// public interface
// =================================================================================
void setSoundFontPath(const std::string& path) {
    State& st = S();
    if (st.sf2Path == path) return;
    st.sf2Path = path;
    if (st.midiInitDone) teardownMidi(st);
}

void setMidiBackend(const std::string& name) {
    State& st = S();
    std::string n = name.empty() ? "auto" : name;
    if (n != "auto" && n != "sf2" && n != "native" && n != "builtin" && n != "none") {
        SDL_Log("snd: unknown MIDI backend '%s', using auto", name.c_str());
        n = "auto";
    }
    if (st.backendName == n) return;
    st.backendName = n;
    if (st.midiInitDone) teardownMidi(st);
}

void setMasterVolume(int percent) {
    State& st = S();
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    float p = percent / 100.0f;
    {
        Lock lk(st);
        st.masterPercent = percent;
        st.masterGain = p * p;  // roughly perceptual
    }
    applyNativeVolume(st);
}

bool open() {
    State& st = S();
    if (!ensureDevice(st)) return false;
    Lock lk(st);
    st.waveOpen = true;
    return true;
}

void close() {
    State& st = S();
    Lock lk(st);
    for (auto& sp : st.sounds) fadeOut(st, *sp);
    st.sounds.clear();
    for (Sound*& c : st.chan) c = nullptr;
    st.waveOpen = false;
}

bool isOpen() {
    State& st = S();
    Lock lk(st);
    return st.waveOpen;
}

bool channelLoadAndPlay(int ch, const uint8_t* data, size_t len, bool loop) {
    State& st = S();
    if (ch < 0 || ch >= kWaveChannels) return false;
    {
        Lock lk(st);
        if (!st.waveOpen) return false;
    }
    auto wave = std::make_shared<WaveData>();
    if (!detail::decodeWav(data, len, *wave)) return false;
    std::unique_ptr<Sound> s(new Sound());
    s->wave = wave;
    s->loop = loop;
    s->playing = true;
    updateGains(*s);
    Lock lk(st);
    if (!st.waveOpen) return false;
    bool used[kMaxSoundNumber + 1] = {};
    for (auto& o : st.sounds)
        if (o->number > 0) used[o->number] = true;
    int num = 0;
    for (int i = 1; i <= kMaxSoundNumber; ++i)
        if (!used[i]) {
            num = i;
            break;
        }
    s->number = num;
    // If the channel still holds a sound (caller did not free it), that sound
    // stays loaded and keeps playing; the channel simply forgets it.
    st.chan[ch] = num ? s.get() : nullptr;
    st.sounds.push_back(std::move(s));
    return true;
}

void channelStopAndUnload(int ch) {
    State& st = S();
    Lock lk(st);
    Sound* s = chanSound(st, ch);
    if (s) unloadSound(st, s);
    if (ch >= 0 && ch < kWaveChannels) st.chan[ch] = nullptr;
}

void channelMarkFree(int ch) {
    State& st = S();
    Lock lk(st);
    if (ch >= 0 && ch < kWaveChannels) st.chan[ch] = nullptr;
}

bool channelIsFree(int ch) {
    State& st = S();
    Lock lk(st);
    return chanSound(st, ch) == nullptr;
}

bool channelIsPlaying(int ch) {
    State& st = S();
    Lock lk(st);
    Sound* s = chanSound(st, ch);
    return s && s->playing;
}

void channelSetVolume(int ch, int hundredthsDb) {
    if (hundredthsDb < -10000 || hundredthsDb > 0) return;
    State& st = S();
    Lock lk(st);
    if (Sound* s = chanSound(st, ch)) {
        s->volume = hundredthsDb;
        updateGains(*s);
    }
}

void channelSetPan(int ch, int hundredthsDb) {
    if (hundredthsDb < -10000 || hundredthsDb > 10000) return;
    State& st = S();
    Lock lk(st);
    if (Sound* s = chanSound(st, ch)) {
        s->pan = hundredthsDb;
        updateGains(*s);
    }
}

void channelSetFrequency(int ch, int hz) {
    if (hz != 0 && (hz < 100 || hz > 100000)) return;
    State& st = S();
    Lock lk(st);
    if (Sound* s = chanSound(st, ch)) s->freq = hz;
}

bool soundNumberPlaying(int n) {
    if (n < 1 || n > kMaxSoundNumber) return false;
    State& st = S();
    Lock lk(st);
    for (auto& s : st.sounds)
        if (s->number == n && s->playing) return true;
    return false;
}

void stopAllAndUnloadButLast() {
    State& st = S();
    Lock lk(st);
    for (Sound*& c : st.chan) c = nullptr;
    if (st.sounds.empty()) return;
    for (auto& sp : st.sounds) fadeOut(st, *sp);
    std::unique_ptr<Sound> last = std::move(st.sounds.back());
    st.sounds.clear();
    last->playing = false;
    st.sounds.push_back(std::move(last));
}

// ---- MIDI ------------------------------------------------------------------------
bool midiAvailable() {
    State& st = S();
    ensureMidi(st);
    return st.backend != Backend::None;
}

bool midiPlay(const uint8_t* data, size_t len) {
    State& st = S();
    ensureMidi(st);
    auto song = std::make_shared<MidiSong>();
    bool ok = st.backend != Backend::None && detail::parseSmf(data, len, *song);
    if (st.backend == Backend::Native) {
        if (ok) st.native->play(song);
        else st.native->stop();
        return ok;
    }
    Lock lk(st);
    if (!st.synth) return false;
    if (st.midiPlaying) st.synth->allSoundOff();
    st.midiEnded = false;
    if (!ok) {
        st.midiPlaying = false;
        st.song.reset();
        return false;
    }
    st.synth->reset();
    st.song = song;
    st.nextEvent = 0;
    st.midiPos = 0;
    st.midiPlaying = true;
    return true;
}

void midiStop() {
    State& st = S();
    if (st.backend == Backend::Native) {
        st.native->stop();
        return;
    }
    Lock lk(st);
    if (st.synth && st.midiPlaying) st.synth->allSoundOff();
    st.midiPlaying = false;
    st.midiEnded = false;
    st.song.reset();
}

bool midiIsPlaying() {
    State& st = S();
    if (st.backend == Backend::Native) return st.native->isPlaying();
    Lock lk(st);
    return st.midiPlaying;
}

bool midiConsumeEndNotification() {
    State& st = S();
    if (st.backend == Backend::Native) return st.native->consumeEnd();
    Lock lk(st);
    bool r = st.midiEnded;
    st.midiEnded = false;
    return r;
}

void midiSetGain(double left, double right) {
    State& st = S();
    auto cl = [](double v) { return (float)(v < 0 ? 0 : (v > 1 ? 1 : v)); };
    {
        Lock lk(st);
        st.midiGainL = cl(left);
        st.midiGainR = cl(right);
    }
    applyNativeVolume(st);
}

std::string midiBackendDescription() {
    State& st = S();
    ensureMidi(st);
    return st.backendDesc;
}

void shutdown() {
    if (!g) return;
    State& st = *g;
    teardownMidi(st);
    SDL_AudioDeviceID dev;
    {
        Lock lk(st);
        dev = st.dev;
        st.dev = 0;
        st.sounds.clear();
        st.fading.clear();
        for (Sound*& c : st.chan) c = nullptr;
        st.waveOpen = false;
    }
    if (dev) SDL_CloseAudioDevice(dev);
    if (st.weInitedAudio) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        st.weInitedAudio = false;
    }
}

}  // namespace snd
