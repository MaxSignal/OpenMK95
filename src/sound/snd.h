// Low-level sound device for the RM95 runtime: wave channels with the sound
// numbering rules of 09-audio-movies.md §4.3, and a MIDI player.
// The higher-level rules (PlayBGM, PlaySE, jingles, saved BGM) live in audio.cpp
// and only use this interface.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace snd {

// ---- configuration (call before open()) -------------------------------------
// Path of a SoundFont (.sf2) used for MIDI. Empty = search default locations.
void setSoundFontPath(const std::string& path);
// "auto" (default), "sf2", "native" (Windows midiOut), "builtin" (simple synth), "none"
void setMidiBackend(const std::string& name);
// Master volume 0..100 for the whole output (user setting, not part of the spec).
void setMasterVolume(int percent);

// ---- device lifetime ---------------------------------------------------------
// Create the wave sound device. Returns false if no audio output is available
// ("no sound device": every wave operation then silently does nothing).
bool open();
// Shut the wave device down: every wave sound ends, all channels become free and
// the sound numbering starts again (movie, screen-mode switch). MIDI is not affected.
void close();
bool isOpen();

// ---- wave channels 0..3 (0 = WAV music, 1..3 = sound effects) ----------------
// Load a RIFF/WAVE file image into channel `ch` and start playing it (once, or
// looping). The loaded sound is appended to the list of loaded sounds and gets the
// lowest free sound number >= 1 (none if 1..255 are all taken; the channel is then
// recorded as free at once but the sound still plays). If the channel held a sound
// it must have been freed before (caller's responsibility).
// Returns false (channel stays free) if there is no device or the data is not a
// playable WAV.
bool channelLoadAndPlay(int ch, const uint8_t* data, size_t len, bool loop);
// Stop the channel's sound, unload it (its number becomes free) and free the channel.
void channelStopAndUnload(int ch);
// Mark the channel free without unloading its sound (a finished sound effect stays
// loaded and keeps its number).
void channelMarkFree(int ch);
bool channelIsFree(int ch);
// True if the channel holds a sound that is still playing.
bool channelIsPlaying(int ch);
// Volume / pan in hundredths of a decibel (both <= 0 means attenuation):
// volume: attenuation applied to both sides; pan: P > 0 attenuates the left side by
// P/100 dB, P < 0 attenuates the right side by -P/100 dB.  Out-of-range values
// (volume < -10000 or > 0, |pan| > 10000) are ignored (previous value kept).
void channelSetVolume(int ch, int hundredthsDb);
void channelSetPan(int ch, int hundredthsDb);
// Playback sample rate in Hz; 0 = the file's own rate. Values outside 100..100000
// are ignored.
void channelSetFrequency(int ch, int hz);
// True if any loaded sound whose number is n is playing.
bool soundNumberPlaying(int n);
// Battle end: stop every loaded wave sound; unload all of them except the last one
// in the list (it stays loaded, stopped, keeping its number); all 4 channels free.
void stopAllAndUnloadButLast();

// ---- MIDI ----------------------------------------------------------------------
// True if a MIDI output (synthesizer) is available.
bool midiAvailable();
// Start playing a Standard MIDI File image from the beginning (replaces anything
// playing). Returns false if the data cannot be parsed (nothing plays then).
bool midiPlay(const uint8_t* data, size_t len);
void midiStop();
bool midiIsPlaying();
// Returns true exactly once after a piece has reached its natural end (complete,
// uninterrupted playback). Stopping/replacing never produces this. Polled by the
// main thread's message processing.
bool midiConsumeEndNotification();
// Linear gain of the whole MIDI output, per side, 0.0 .. 1.0 (persistent).
void midiSetGain(double left, double right);

// ---- additions (not needed by audio.cpp) ---------------------------------------
// Description of the MIDI synthesizer in use, e.g. "sf2:/path/x.sf2", "builtin",
// "native", "none" (initialises MIDI if that has not happened yet).
std::string midiBackendDescription();
// Release everything (MIDI, wave sounds, audio device). Optional; call before
// SDL_Quit at program exit. The module can be used again afterwards.
void shutdown();

}  // namespace snd
