# Sound and movies

The game-level rules (which file plays when, saved BGM, jingles, audio slots, the MIDI loop position) are in
`src/engine/audio.cpp`. They use the device-level modules described here, which know nothing about games.

## Wave sounds (`src/sound/snd.cpp`, `snd_wav.cpp`)

`snd::open()` opens one SDL audio device. There are four wave channels: channel 0 plays WAV music, channels 1–3
sound effects. Every loaded sound gets the lowest free *sound number* 1–255, and channels are freed and numbers
reused exactly as described in spec 09 §4.3, because games observe the effects of this numbering (for example
which sound a later command stops). `snd::close()` releases the device and resets the numbering, as the original
does around movies and screen-mode switches.

`snd_wav.cpp` decodes RIFF/WAVE files: PCM 8/16/24/32-bit, IEEE float, IMA ADPCM and Microsoft ADPCM. Sounds are
resampled to the device rate while mixing.

## MIDI (`src/sound/snd_midi.cpp`, `snd_synth.cpp`, `snd_tsf.cpp`)

`snd_midi.cpp` parses Standard MIDI Files (formats 0–2, also inside RIFF RMID) into a time-ordered event list.
The sequencer reports a natural end of a piece once (`snd::midiConsumeEndNotification`), which the engine uses for
the loop rules of spec 09; stopping or replacing a piece never produces an end notification.

Three synthesizers can play the events, chosen with `--midi`:

| Backend | Where | Notes |
|---|---|---|
| `sf2` | all systems | TinySoundFont (`third_party/tinysoundfont`) with a General MIDI SoundFont; rendered in the audio callback |
| `native` | Windows | The system MIDI output (winmm), sequenced on its own thread |
| `builtin` | all systems | A small built-in synthesizer, used when there is no SoundFont |

`auto` picks `sf2` when a SoundFont is found, otherwise `native` on Windows and `builtin` elsewhere.

## Movies (`src/movie`, `src/engine/movieplay.cpp`)

`movie::openAvi()` reads RIFF AVI files (including OpenDML continuation RIFFs) sequentially and decodes video
with one of:

- uncompressed DIB (1/4/8/16/24/32-bit, BI_RGB and BI_BITFIELDS, bottom-up and top-down),
- BI_RLE8 / BI_RLE4,
- Microsoft Video 1 (8 and 16-bit),
- Cinepak (`movie_cinepak.cpp`).

The demuxer can also extract a PCM audio track, but movie playback currently shows the video only (see the
limitations in the README). `movieplay.cpp` implements the "play movie" event command (spec
09 §7): it saves and stops the BGM, closes the wave device, shows the frames at the AVI frame rate in an overlay
that `disp::compose()` draws over the screen, and ends on confirm, Escape or the end of the movie. Movies in a
codec that is not supported (Intel Indeo 4 is the only one among the sample games) are skipped.
