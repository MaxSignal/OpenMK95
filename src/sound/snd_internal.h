// Internal interfaces of the sound module (snd.cpp, snd_midi.cpp, snd_synth.cpp,
// snd_wav.cpp). Not used by the rest of the program.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snd {
namespace detail {

// ---- WAV ----------------------------------------------------------------------
// A decoded wave sound: interleaved signed 16-bit samples at the file's rate.
struct WaveData {
    int channels = 1;  // 1 or 2
    int rate = 22050;  // file sample rate (Hz)
    std::vector<int16_t> samples;  // frames * channels
    size_t frames() const { return channels ? samples.size() / (size_t)channels : 0; }
};
// Parses a RIFF/WAVE image (PCM 8/16/24/32 bit, IEEE float, IMA ADPCM, MS ADPCM).
// Returns false if the data is not a playable WAV.
bool decodeWav(const uint8_t* data, size_t len, WaveData& out);

// ---- Standard MIDI File -------------------------------------------------------
struct MidiEvent {
    double time = 0;        // seconds from the start of the piece
    uint8_t status = 0;     // channel message status (0x80..0xEF) or 0xF0 = SysEx
    uint8_t d1 = 0, d2 = 0;
    uint32_t sysexOff = 0;  // for status 0xF0: complete message (F0 .. F7) in MidiSong::sysex
    uint32_t sysexLen = 0;
};
struct MidiSong {
    std::vector<MidiEvent> events;  // sorted by time
    std::vector<uint8_t> sysex;
    double endTime = 0;  // time of the last end-of-track (or last event) of all tracks
};
// Parses an SMF (format 0/1/2, also inside a RIFF RMID wrapper). Returns false
// if the data cannot be parsed.
bool parseSmf(const uint8_t* data, size_t len, MidiSong& out);

// ---- software MIDI synthesizers (rendered inside the audio callback) ---------
class MidiSynth {
public:
    virtual ~MidiSynth() = default;
    // GM reset: every sound stops at once, programs/controllers back to defaults.
    virtual void reset() = 0;
    // Quick release of every sounding note on every channel (stop).
    virtual void allSoundOff() = 0;
    virtual void shortMsg(uint8_t status, uint8_t d1, uint8_t d2) = 0;
    virtual void sysex(const uint8_t* msg, size_t len) = 0;
    // Adds `frames` stereo frames (interleaved L/R floats) into `out`.
    virtual void render(float* out, int frames) = 0;
};
std::unique_ptr<MidiSynth> createBuiltinSynth(int sampleRate);
// Loads a SoundFont (path in UTF-8). Returns nullptr on failure.
std::unique_ptr<MidiSynth> createTsfSynth(const std::string& sf2Path, int sampleRate);

// True if the SysEx message is a GM/GS/XG system reset.
bool isResetSysex(const uint8_t* msg, size_t len);

// ---- Windows native MIDI output (winmm) ---------------------------------------
// Owns its own sequencer thread. Only functional on Windows; elsewhere open() fails.
class NativeMidi {
public:
    virtual ~NativeMidi() = default;
    virtual void play(std::shared_ptr<const MidiSong> song) = 0;
    virtual void stop() = 0;
    virtual bool isPlaying() = 0;
    // Returns true once after a natural end.
    virtual bool consumeEnd() = 0;
    virtual void clearEnd() = 0;
    // Linear gains 0..1 per side.
    virtual void setVolume(double left, double right) = 0;
};
std::unique_ptr<NativeMidi> createNativeMidi();  // nullptr if unavailable

// Lists *.sf2 files (case-insensitive extension) in a directory (UTF-8 path),
// sorted by name. Returns full paths.
std::vector<std::string> listSoundFonts(const std::string& dir);

}  // namespace detail
}  // namespace snd
