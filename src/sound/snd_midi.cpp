// Standard MIDI File parser, SoundFont search helper and the Windows native
// (winmm) MIDI output for the sound module.
#include <algorithm>
#include <cstring>

#include "sound/snd_internal.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <SDL.h>
#else
#include <dirent.h>
#endif

namespace snd {
namespace detail {

// ---------------------------------------------------------------------------------
// SMF parsing
// ---------------------------------------------------------------------------------
namespace {

inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

struct RawEv {
    uint64_t tick;
    uint32_t order;     // global sequence number (track order, then position) for stable sorting
    uint8_t kind;       // 0 = channel msg, 1 = sysex, 2 = tempo, 3 = end of track
    uint8_t status, d1, d2;
    uint32_t val;       // tempo (us per quarter) or sysex offset
    uint32_t len;       // sysex length
};

bool readVar(const uint8_t* p, size_t end, size_t& pos, uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= end) return false;
        uint8_t b = p[pos++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return true;
    }
    return true;  // over-long quantity: accept what we have
}

}  // namespace

bool parseSmf(const uint8_t* data, size_t len, MidiSong& out) {
    out = MidiSong();
    if (!data || len < 14) return false;
    // RIFF RMID wrapper
    if (len >= 20 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "RMID", 4)) {
        size_t pos = 12;
        bool found = false;
        while (pos + 8 <= len) {
            uint32_t sz = (uint32_t)data[pos + 4] | ((uint32_t)data[pos + 5] << 8) |
                          ((uint32_t)data[pos + 6] << 16) | ((uint32_t)data[pos + 7] << 24);
            if (!memcmp(data + pos, "data", 4)) {
                size_t avail = len - pos - 8;
                data = data + pos + 8;
                len = std::min((size_t)sz, avail);
                found = true;
                break;
            }
            pos += 8 + (size_t)sz + (sz & 1);
        }
        if (!found || len < 14) return false;
    }
    if (memcmp(data, "MThd", 4) != 0) return false;
    uint32_t hlen = be32(data + 4);
    if (hlen < 6 || 8 + (size_t)hlen > len) return false;
    int ntracks = be16(data + 10);
    uint16_t division = be16(data + 12);
    if (division == 0 || ntracks == 0) return false;
    double secPerTickFixed = 0;  // SMPTE timing
    int tpq = 0;
    if (division & 0x8000) {
        int fps = -(int8_t)(division >> 8);
        int tpf = division & 0xFF;
        if (fps <= 0 || tpf <= 0) return false;
        double realFps = fps == 29 ? 29.97 : fps;
        secPerTickFixed = 1.0 / (realFps * tpf);
    } else {
        tpq = division;
    }

    std::vector<RawEv> evs;
    uint32_t order = 0;
    size_t pos = 8 + hlen;
    int tracksFound = 0;
    while (pos + 8 <= len && tracksFound < ntracks) {
        uint32_t clen = be32(data + pos + 4);
        bool isTrack = !memcmp(data + pos, "MTrk", 4);
        size_t start = pos + 8;
        size_t end = start + std::min((size_t)clen, len - start);
        pos = (size_t)clen > len - start ? len : start + (size_t)clen;
        if (!isTrack) continue;
        ++tracksFound;
        size_t p = start;
        uint64_t tick = 0;
        uint8_t running = 0;
        bool sawEot = false;
        while (p < end) {
            uint32_t delta;
            if (!readVar(data, end, p, delta)) break;
            tick += delta;
            if (p >= end) break;
            uint8_t b = data[p];
            uint8_t st;
            if (b & 0x80) {
                st = b;
                ++p;
            } else {
                if (!running) break;  // data byte without running status: corrupt track
                st = running;
            }
            if (st >= 0x80 && st < 0xF0) {
                running = st;
                int need = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
                if (p + (size_t)need > end) break;
                RawEv e{};
                e.tick = tick;
                e.order = order++;
                e.kind = 0;
                e.status = st;
                e.d1 = data[p] & 0x7F;
                e.d2 = need == 2 ? (data[p + 1] & 0x7F) : 0;
                p += (size_t)need;
                evs.push_back(e);
            } else if (st == 0xFF) {
                if (p >= end) break;
                uint8_t type = data[p++];
                uint32_t mlen;
                if (!readVar(data, end, p, mlen)) break;
                if (p + mlen > end) break;
                if (type == 0x51 && mlen >= 3) {
                    RawEv e{};
                    e.tick = tick;
                    e.order = order++;
                    e.kind = 2;
                    e.val = ((uint32_t)data[p] << 16) | ((uint32_t)data[p + 1] << 8) | data[p + 2];
                    evs.push_back(e);
                } else if (type == 0x2F) {
                    RawEv e{};
                    e.tick = tick;
                    e.order = order++;
                    e.kind = 3;
                    evs.push_back(e);
                    sawEot = true;
                }
                p += mlen;
                if (sawEot) break;
            } else if (st == 0xF0 || st == 0xF7) {
                uint32_t mlen;
                if (!readVar(data, end, p, mlen)) break;
                if (p + mlen > end) break;
                RawEv e{};
                e.tick = tick;
                e.order = order++;
                e.kind = 1;
                e.val = (uint32_t)out.sysex.size();
                if (st == 0xF0) out.sysex.push_back(0xF0);
                out.sysex.insert(out.sysex.end(), data + p, data + p + mlen);
                e.len = (uint32_t)out.sysex.size() - e.val;
                p += mlen;
                if (e.len > 0) evs.push_back(e);
            } else {
                break;  // system common/realtime bytes are not valid in a file
            }
        }
        if (!sawEot) {
            // missing end-of-track: the track ends at its last event
            RawEv e{};
            e.tick = tick;
            e.order = order++;
            e.kind = 3;
            evs.push_back(e);
        }
    }
    if (tracksFound == 0) return false;

    std::sort(evs.begin(), evs.end(), [](const RawEv& a, const RawEv& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    double t = 0;
    uint64_t lastTick = 0;
    double usPerQ = 500000.0;
    double endTime = 0;
    out.events.reserve(evs.size());
    for (const RawEv& e : evs) {
        uint64_t dt = e.tick - lastTick;
        if (tpq) t += (double)dt * usPerQ / 1e6 / tpq;
        else t += (double)dt * secPerTickFixed;
        lastTick = e.tick;
        switch (e.kind) {
            case 0: {
                MidiEvent m;
                m.time = t;
                m.status = e.status;
                m.d1 = e.d1;
                m.d2 = e.d2;
                out.events.push_back(m);
                break;
            }
            case 1: {
                MidiEvent m;
                m.time = t;
                m.status = 0xF0;
                m.sysexOff = e.val;
                m.sysexLen = e.len;
                out.events.push_back(m);
                break;
            }
            case 2:
                if (e.val > 0) usPerQ = e.val;
                break;
            case 3:
                break;
        }
        endTime = std::max(endTime, t);
    }
    out.endTime = endTime;
    return true;
}

bool isResetSysex(const uint8_t* m, size_t n) {
    // GM System On: F0 7E <dev> 09 01 F7 ; GM2: F0 7E <dev> 09 03 F7
    if (n >= 6 && m[0] == 0xF0 && m[1] == 0x7E && m[3] == 0x09 && (m[4] == 0x01 || m[4] == 0x03))
        return true;
    // GS reset: F0 41 <dev> 42 12 40 00 7F 00 41 F7
    if (n >= 10 && m[0] == 0xF0 && m[1] == 0x41 && m[3] == 0x42 && m[4] == 0x12 && m[5] == 0x40 &&
        m[6] == 0x00 && m[7] == 0x7F)
        return true;
    // XG system on: F0 43 1n 4C 00 00 7E 00 F7
    if (n >= 8 && m[0] == 0xF0 && m[1] == 0x43 && (m[2] & 0xF0) == 0x10 && m[3] == 0x4C &&
        m[4] == 0x00 && m[5] == 0x00 && m[6] == 0x7E)
        return true;
    return false;
}

// ---------------------------------------------------------------------------------
// SoundFont directory listing
// ---------------------------------------------------------------------------------
static bool endsWithSf2(const std::string& n) {
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    for (char& c : e) c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    return e == ".sf2";
}

std::vector<std::string> listSoundFonts(const std::string& dirIn) {
    std::vector<std::string> res;
    if (dirIn.empty()) return res;
    std::string dir = dirIn;
#ifdef _WIN32
    if (dir.back() != '\\' && dir.back() != '/') dir += '\\';
    std::string pat = dir + "*";
    int wn = MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, nullptr, 0);
    if (wn <= 0) return res;
    std::wstring wpat((size_t)wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, &wpat[0], wn);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return res;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        int n = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1) continue;
        std::string name((size_t)n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, &name[0], n, nullptr, nullptr);
        if (endsWithSf2(name)) res.push_back(dir + name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    if (dir.back() != '/') dir += '/';
    DIR* d = opendir(dir.c_str());
    if (!d) return res;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (endsWithSf2(name)) res.push_back(dir + name);
    }
    closedir(d);
#endif
    std::sort(res.begin(), res.end());
    return res;
}

// ---------------------------------------------------------------------------------
// Windows native MIDI output
// ---------------------------------------------------------------------------------
#ifdef _WIN32
namespace {

class WinMidi : public NativeMidi {
public:
    HMIDIOUT out_ = nullptr;
    SDL_mutex* mtx_ = nullptr;
    SDL_Thread* thread_ = nullptr;
    std::atomic<bool> quit_{false};
    std::shared_ptr<const MidiSong> song_;
    size_t next_ = 0;
    bool playing_ = false;
    bool ended_ = false;
    uint64_t startCounter_ = 0;
    double freq_ = 1;
    bool timer_ = false;

    bool init() {
        if (midiOutOpen(&out_, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            out_ = nullptr;
            return false;
        }
        mtx_ = SDL_CreateMutex();
        if (!mtx_) return false;
        freq_ = (double)SDL_GetPerformanceFrequency();
        timeBeginPeriod(1);
        timer_ = true;
        resetDevice();
        thread_ = SDL_CreateThread(&WinMidi::threadMain, "openmk95-midi", this);
        return thread_ != nullptr;
    }
    ~WinMidi() override {
        quit_ = true;
        if (thread_) SDL_WaitThread(thread_, nullptr);
        if (out_) {
            silence();
            midiOutReset(out_);
            midiOutClose(out_);
        }
        if (timer_) timeEndPeriod(1);
        if (mtx_) SDL_DestroyMutex(mtx_);
    }
    void sendShort(uint8_t st, uint8_t d1, uint8_t d2) {
        midiOutShortMsg(out_, (DWORD)st | ((DWORD)d1 << 8) | ((DWORD)d2 << 16));
    }
    void sendLong(const uint8_t* p, size_t n) {
        if (n < 2 || p[0] != 0xF0) return;  // only complete SysEx messages
        std::vector<char> buf(p, p + n);
        MIDIHDR h;
        memset(&h, 0, sizeof h);
        h.lpData = buf.data();
        h.dwBufferLength = (DWORD)n;
        h.dwBytesRecorded = (DWORD)n;
        if (midiOutPrepareHeader(out_, &h, sizeof h) != MMSYSERR_NOERROR) return;
        if (midiOutLongMsg(out_, &h, sizeof h) == MMSYSERR_NOERROR) {
            for (int i = 0; i < 500 && !(h.dwFlags & MHDR_DONE); ++i) Sleep(1);
        }
        for (int i = 0; i < 100 && midiOutUnprepareHeader(out_, &h, sizeof h) == MIDIERR_STILLPLAYING; ++i)
            Sleep(1);
    }
    void silence() {
        for (int c = 0; c < 16; ++c) {
            sendShort((uint8_t)(0xB0 | c), 64, 0);   // sustain off
            sendShort((uint8_t)(0xB0 | c), 123, 0);  // all notes off
            sendShort((uint8_t)(0xB0 | c), 120, 0);  // all sound off
        }
    }
    void resetDevice() {
        silence();
        for (int c = 0; c < 16; ++c) {
            uint8_t cc = (uint8_t)(0xB0 | c);
            sendShort(cc, 121, 0);  // reset all controllers
            sendShort(cc, 0, 0);    // bank select
            sendShort(cc, 32, 0);
            sendShort((uint8_t)(0xC0 | c), 0, 0);
            sendShort(cc, 7, 100);
            sendShort(cc, 10, 64);
            sendShort(cc, 11, 127);
            sendShort(cc, 101, 0);  // pitch bend range 2 semitones
            sendShort(cc, 100, 0);
            sendShort(cc, 6, 2);
            sendShort(cc, 38, 0);
            sendShort(cc, 101, 127);
            sendShort(cc, 100, 127);
            sendShort((uint8_t)(0xE0 | c), 0, 64);
        }
    }
    static int SDLCALL threadMain(void* self) {
        static_cast<WinMidi*>(self)->run();
        return 0;
    }
    void run() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
        while (!quit_) {
            SDL_LockMutex(mtx_);
            if (playing_ && song_) {
                double now = (double)(SDL_GetPerformanceCounter() - startCounter_) / freq_;
                const MidiSong& s = *song_;
                while (next_ < s.events.size() && s.events[next_].time <= now) {
                    const MidiEvent& e = s.events[next_++];
                    if (e.status == 0xF0) {
                        const uint8_t* p = s.sysex.data() + e.sysexOff;
                        sendLong(p, e.sysexLen);
                    } else {
                        sendShort(e.status, e.d1, e.d2);
                    }
                }
                if (next_ >= s.events.size() && now >= s.endTime) {
                    playing_ = false;
                    ended_ = true;
                    silence();
                }
            }
            SDL_UnlockMutex(mtx_);
            Sleep(1);
        }
    }
    void play(std::shared_ptr<const MidiSong> song) override {
        SDL_LockMutex(mtx_);
        if (playing_) silence();
        resetDevice();
        song_ = std::move(song);
        next_ = 0;
        playing_ = song_ != nullptr;
        ended_ = false;
        startCounter_ = SDL_GetPerformanceCounter();
        SDL_UnlockMutex(mtx_);
    }
    void stop() override {
        SDL_LockMutex(mtx_);
        if (playing_) silence();
        playing_ = false;
        ended_ = false;
        song_.reset();
        SDL_UnlockMutex(mtx_);
    }
    bool isPlaying() override {
        SDL_LockMutex(mtx_);
        bool r = playing_;
        SDL_UnlockMutex(mtx_);
        return r;
    }
    bool consumeEnd() override {
        SDL_LockMutex(mtx_);
        bool r = ended_;
        ended_ = false;
        SDL_UnlockMutex(mtx_);
        return r;
    }
    void clearEnd() override {
        SDL_LockMutex(mtx_);
        ended_ = false;
        SDL_UnlockMutex(mtx_);
    }
    void setVolume(double l, double r) override {
        auto cv = [](double g) -> DWORD {
            if (g < 0) g = 0;
            if (g > 1) g = 1;
            return (DWORD)(g * 65535.0 + 0.5);
        };
        midiOutSetVolume(out_, cv(l) | (cv(r) << 16));
    }
};

}  // namespace

std::unique_ptr<NativeMidi> createNativeMidi() {
    std::unique_ptr<WinMidi> m(new WinMidi());
    if (!m->init()) return nullptr;
    return std::unique_ptr<NativeMidi>(m.release());
}
#else
std::unique_ptr<NativeMidi> createNativeMidi() { return nullptr; }
#endif

}  // namespace detail
}  // namespace snd
