// Music, jingles and sound effects (09-audio-movies.md §1-§6).
#include <cstdio>
#include "core/fsys.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "engine/runtime.h"
#include "core/sjis.h"
#include "sound/snd.h"

namespace mov { bool movieShowing(); }

namespace audio {

BgmRec cur;
BgmRec saved;
static bool g_playOnce = false;
static std::vector<uint8_t> g_midiData;
static sjis g_stop;

static const char* kBuiltinSE[53] = {
    "flamer.wav", "sword.wav", "metal.wav", "sword3.wav", "arrowhit.wav", "ropewhip.wav", "fastarw.wav", "kyudo.wav",
    "ninjahit.wav", "shot1.wav", "knock.wav", "punch.wav", "whack.wav", "banger.wav", "boom.wav", "noizehit.wav",
    "snarehit.wav", "powerup.wav", "electro.wav", "bomb.wav", "explos3.wav", "electrix.wav", "thunder.wav", "thunder2.wav",
    "monster.wav", "monster2.wav", "crunch.wav", "magic.wav", "paku.wav", "sludge.wav", "arrrg.wav", "monster3.wav",
    "swordhai.wav", "glsmash.wav", "mizu.wav", "close.wav", "creak.wav", "creak2.wav", "creakdoa.wav", "doashut3.wav",
    "doorshut.wav", "footstep.wav", "footstp3.wav", "latch.wav", "obakedoa.wav", "warpout.wav", "wibble.wav",
    "hitpoint.wav", "healer.wav", "refresh1.wav", "zap64.wav", "zapper.wav", "error.wav",
};

void init() {
    g_stop = sj::S("演奏停止");
    cur.has = true;
    cur.name = g_stop;
    cur.a = cur.b = 0;
    if (!snd::open()) fprintf(stderr, "audio: no sound device\n");
}

static bool readSound(const sjis& name, std::vector<uint8_t>& out) {
    return fsys::read(sj::baseName(name), out);
}

static void applyMidiGain(int att, int pan) {
    const uint32_t R = 65535;
    uint32_t V = (uint32_t)(100 - att) * R / 100;
    uint32_t l, r;
    if (pan < 50) { l = V; r = 2 * (V * (uint32_t)pan / 100); }
    else { l = 2 * ((uint32_t)(100 - pan) * V / 100); r = V; }
    snd::midiSetGain(std::min(1.0, l / (double)R), std::min(1.0, r / (double)R));
}

static void applyParams(const int32_t* ab, int ch) {
    int att = ab[0] >> 16;
    int pan = ab[0] & 0xFFFF;
    int type = ab[1] & 3;
    if (type == 1 && ch >= 0) {
        snd::channelSetVolume(ch, -40 * att);
        snd::channelSetPan(ch, (pan - 50) * 20);
    } else if (type == 2) applyMidiGain(att, pan);
    if (ch >= 0) snd::channelSetFrequency(ch, (int)(((uint32_t)ab[1] >> 16) * 70));
}

void stopAllMusic() {
    snd::midiStop();
    snd::channelStopAndUnload(0);
}

void playBGM(const sjis& name, const int32_t* ab, bool force) {
    bool failed = false;
    if (!(cur.has && !force && name == cur.name)) {
        if (name == g_stop) {
            stopAllMusic();
            cur.name = g_stop;
            cur.has = true;
            return;
        }
        stopAllMusic();
        std::vector<uint8_t> d;
        if (name.find(".mid") != sjis::npos) {
            if (!readSound(name, d)) failed = true;
            else { g_midiData = d; snd::midiPlay(g_midiData.data(), g_midiData.size()); }
        } else if (name.find(".wav") != sjis::npos) {
            // The start fails only for a missing file or no sound device (§3.1 step 4);
            // a file the sound system cannot load leaves channel 0 free, the parameters
            // are then applied to nothing but still recorded.
            if (!snd::isOpen() || !readSound(name, d)) failed = true;
            else snd::channelLoadAndPlay(0, d.data(), d.size(), true);
        } else failed = true;  // CD audio track (number at byte offset 8): no CD support
    }
    if (ab && !failed) {
        applyParams(ab, 0);
        cur.a = ab[0];
        cur.b = ab[1];
    }
    cur.name = name;
    cur.has = true;
}

void saveBGM() {
    if (cur.has) { saved = cur; saved.has = true; }
}

void resumeBGM(bool force) {
    if (!saved.has) return;
    BgmRec s = saved;
    saved.has = false;
    int32_t ab[2] = {s.a, s.b};
    playBGM(s.name, ab, force);
}

void playSE(const sjis& name, const int32_t* ab) {
    std::vector<uint8_t> d;
    if (!readSound(name, d)) return;
    if (ab && (ab[1] & 4)) for (int ch = 1; ch <= 3; ch++) snd::channelStopAndUnload(ch);
    for (int ch = 1; ch <= 3; ch++)
        if (!snd::channelIsFree(ch) && !snd::channelIsPlaying(ch)) snd::channelMarkFree(ch);
    int use = -1;
    for (int ch = 1; ch <= 3; ch++) if (snd::channelIsFree(ch)) { use = ch; break; }
    if (use < 0) { snd::channelStopAndUnload(1); use = 1; }
    if (snd::channelLoadAndPlay(use, d.data(), d.size(), false) && ab) applyParams(ab, use);
}

void playBuiltinSE(int n) {
    if (n < 0 || n >= 53) return;
    playSE(kBuiltinSE[n], nullptr);
}

void playSlotBGM(int slot) {
    if (slot < 0 || slot >= 12 || !G.slots[slot].hasName) return;
    int32_t ab[2] = {G.slots[slot].a, G.slots[slot].b};
    playBGM(G.slots[slot].name, ab, false);
}

void playFieldMusic() {
    Chara* h = hero();
    int v = h ? h->vehicleType : 0;
    if (v < 0 || v > 6) return;
    playSlotBGM(v);
}

void setPlayOnce(bool on) { g_playOnce = on; }

void playJingle(int slot) {
    const AudioRec& s = G.slots[slot];
    if (!s.hasName) return;
    int32_t ab[2] = {s.a, s.b};
    if (s.name.find(".mid") != sjis::npos) {
        int type = s.b & 3;
        if (type == 2) applyMidiGain(s.a >> 16, s.a & 0xFFFF);
        std::vector<uint8_t> d;
        if (!readSound(s.name, d)) { g_playOnce = false; return; }
        snd::midiStop();
        g_midiData = d;
        if (!snd::midiPlay(g_midiData.data(), g_midiData.size())) g_playOnce = false;
    } else playSE(s.name, ab);
}

void waitJingle(int slot) {
    const AudioRec& s = G.slots[slot];
    bool isMidi = s.hasName && s.name.find(".mid") != sjis::npos;
    if (!isMidi && !snd::isOpen()) return;
    if (isMidi && !snd::midiAvailable()) { g_playOnce = false; return; }
    for (;;) {
        rt::processMessages();
        if (G.quit) return;
        if (plat::keyHeld(VK_SPACE_) || plat::keyHeld(VK_RETURN_)) {
            playBGM(g_stop, nullptr, false);
            for (int ch = 1; ch <= 3; ch++) if (snd::channelIsPlaying(ch)) snd::channelStopAndUnload(ch);
            g_playOnce = false;
            return;
        }
        if (isMidi) { if (!g_playOnce) return; }
        else if (!(snd::soundNumberPlaying(1) || snd::soundNumberPlaying(2) || snd::soundNumberPlaying(3))) return;
        plat::idle();
    }
}

void battleEndUnload() {
    playBGM(g_stop, nullptr, false);
    snd::stopAllAndUnloadButLast();
}

void recreateDevice() {
    snd::close();
    snd::open();
}

void shutdownDevice() { snd::close(); }

void poll() {
    if (!snd::midiConsumeEndNotification()) return;
    if (mov::movieShowing()) return;
    if (g_playOnce) { g_playOnce = false; return; }
    if (!g_midiData.empty()) snd::midiPlay(g_midiData.data(), g_midiData.size());
}

}  // namespace audio
