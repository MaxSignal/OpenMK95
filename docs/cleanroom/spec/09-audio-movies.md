# 09 — Audio and movies

This chapter describes music (BGM), jingles, sound effects, their parameters, and movie playback
(event command 14 Play movie). The event commands that use these functions are listed in `05-events.md`;
this chapter defines what they do. Battle, inn and vehicle sequences are described in their own
chapters; here only their audio part is given, with cross-references.

Terms used in this chapter:

* **Sound device** — the wave-sound output (used for WAV music and all sound effects). The original
  may run without one (no sound card): every WAV operation then silently does nothing.
* **MIDI output** — the MIDI sequencer used for `.mid` music.
* **CD output** — audio-CD playback (red book tracks of a CD in the drive).
* **Wave channels** — 4 slots that can each hold one loaded WAV sound: **channel 0** = WAV music,
  **channels 1, 2, 3** = sound effects. A channel is either *free* or holds one loaded sound (which may be
  playing or finished). A sound can stay loaded after its channel has become free; the list of loaded
  sounds and their sound numbers are described in §4.3.
* **Current BGM** — a record {name, a, b} of the music most recently requested (see §3). At program
  start the current name is `演奏停止` and a = b = 0.
* **Saved BGM** — a second record {name, a, b} plus a "present" flag, used to resume music after a
  battle, an inn stay, a movie or a screen-mode switch (§3.6).
* **Play-once flag** — a flag meaning "the MIDI music now playing is a jingle and must not loop" (§3.3).

---------------------------------------------------------------------------------------------------

## 1. Audio slots

### 1.1 The 12 slots

The game has 12 **audio slots** ("system sound settings"). Each slot is a record {name, a, b}: a file
name (Shift_JIS text, normally the full path the editor stored, e.g.
`B:\RPGﾂｸｰﾙ95\Sys\Bgm\S002.mid`) and two signed 32-bit integers **a** and **b** whose encoding is given in
§1.3. The slots are read from the game file (`02-data-formats.md`, §3.3 "Audio entries"), can be replaced by event
command 49 Set audio slot (§6.3) and are stored in save files (`10-save-files.md`).

| slot | meaning | how it is played |
|---|---|---|
| 0 | field music while walking (vehicle type 0) | as BGM |
| 1 | field music in a wagon (vehicle type 1) | as BGM |
| 2 | field music in a sled (vehicle type 2) | as BGM |
| 3 | field music in a boat (vehicle type 3) | as BGM |
| 4 | field music in a ship (vehicle type 4) | as BGM |
| 5 | field music in a balloon (vehicle type 5) | as BGM |
| 6 | field music in an airship (vehicle type 6) | as BGM |
| 7 | encounter jingle (battle start) | as jingle (§4) |
| 8 | battle music | as BGM |
| 9 | victory music | as BGM (loops until the battle ends) |
| 10 | level-up music | as BGM |
| 11 | inn jingle | as jingle (§4) |

The "vehicle type" of slots 0..6 is the movement class of the leader's map character (the vehicle while
riding), see `04-map-movement.md`, "Vehicles".

### 1.2 Initial state

Before the game file is read, all 12 slots have **no name** and a = b = 0. (The runtime contains a
table of default names `WALK.wav` … `REST.wav` with a = 0x000A0032, b = 0x000000A0, but version 1.23
never uses it.) The game file then sets slots 0..11 (`02-data-formats.md` §3.3). A game file whose data
version is lower than 0.96 contains only 11 slot records, so slot 11 (the inn jingle) keeps no name.
A slot without a name is never played (§3.8, §4.1); how such a slot is written to a save file is in
`10-save-files.md`. UNCERTAIN: the inn (and the battle start for slot 7) also searches
the slot's name for `.mid` before playing it; with no name the original is expected to fail at that point
(no sample is older than 0.96, so this was never observed). Recommendation: treat a slot without a name
as "not MIDI" and play nothing; the jingle wait then returns at once only if there is no sound device,
otherwise it ends as soon as no sound numbered 1..3 is playing.

The editor writes the name `演奏停止` ("stop playing"; Shift_JIS bytes 89 89 91 74 92 E2 8E 7E) for a
slot set to "none", and some editor versions write `NOSOUND.WAV` with b = 0xA0. Both are ordinary names
for the runtime: `演奏停止` has the special meaning of §3.1, `NOSOUND.WAV` is treated by the rules of §3
(it has an uppercase extension, so as BGM it is taken for a CD track — see §3.1 step 4).

### 1.3 Encoding of a and b

The same two integers are used by the audio slots, by the current/saved BGM records and by the event
commands 6 Play music, 21 Play sound effect and 49 Set audio slot, where a = command argument a2 and
b = argument a3.

**a** = (attenuation << 16) | pan:

* **pan** = a & 0xFFFF (unsigned): 0..100, 50 = centre, 0 = full left, 100 = full right. The audio slots
  use 50 (one slot uses 30); event commands 6 and 21 also use 0, 10, 20, 30, 40, 60, 70, 80 and 100
  (about 110 commands), so the pan law of §5.1 matters for real games.
* **attenuation** ("volume" in the editor, inverted) = a >> 16 (arithmetic shift, signed): 0..100,
  0 = full volume. Typical values 0, 10, 20, 30, 40; event commands use every multiple of 10 up to 100.

**b**:

| bits | meaning |
|---|---|
| 0–1 | **type**: 0 = none, 1 = WAV, 2 = MIDI, 3 = CD audio. The type selects **which volume control** the parameters are applied to (§5); it does **not** select how the file is played (that is decided by the file name, §3.1). |
| 2 | **exclusive** flag: when a sound effect is played with this record, all sound-effect channels are stopped first (§5.2). Only meaningful for sound effects. |
| 3–7 | ignored (the defaults contain 0xA0 here). |
| 8–15 | **target slot** 0..11 — used only by event command 49 Set audio slot to say which slot to replace. Ignored elsewhere (the corpus has non-zero values here in a few slot records, e.g. b = 0x801; they have no effect). |
| 16–31 | **frequency** in units of 70 Hz (unsigned): the playback sample rate of a WAV is (b >> 16) × 70 Hz; 0 = the file's own rate. Applied to WAV sounds only. Corpus values 10..500 (700 Hz .. 35 000 Hz). |

Examples from real files: a = 0x001E0032 (attenuation 30, pan 50); b = 0x00000002 (MIDI);
b = 0x019A0005 (WAV, exclusive, frequency 410 × 70 = 28 700 Hz).

---------------------------------------------------------------------------------------------------

## 2. File name resolution

All music, jingle, sound-effect and movie names are resolved the same way:

1. Take the **base name**: the part of the name after the last backslash byte (0x5C); the whole name if it
   has no backslash. The search is done byte by byte from the end of the string, **without** regard to
   Shift_JIS character boundaries **(quirk)**: a Shift_JIS character whose trail byte is 0x5C (for example
   `ソ` = 83 5C, `表` = 95 5C, `能` = 94 5C) splits the name there. Implementations must reproduce this,
   otherwise they would find files that the original does not find.
2. The file is looked for as `<game folder>\<base name>` (case-insensitive file-name match). The drive and
   directories stored in the data (`A:\Program Files\RPGﾂｸｰﾙ95\Sys\Bgm\GM\Prison.mid`,
   `D:\gamedisk\refresh1.wav`) are thus ignored. (Strictly, the original uses the folder that contains the
   runtime program, which is the game folder in every normal installation.)
3. If the file cannot be opened for reading, the requested operation does nothing (silently). For music
   the previous music has usually already been stopped at that point, see §3.1.

Forward slashes are not separators for step 1; a base name containing `/` would be looked up as a path
relative to the game folder (never occurs in the corpus).

The **built-in sound effects** (§5.4) are internally named as if they lived in a `SYS\SE` sub-folder of the
runtime's folder, but they go through the same base-name reduction, so they too are looked up **directly
in the game folder** (e.g. `<game folder>\punch.wav`). Games that use them ship these files.

---------------------------------------------------------------------------------------------------

## 3. Music (BGM)

### 3.1 Playing a BGM — PlayBGM(name, params, force)

`params` is an {a, b} pair or *none*; `force` is a boolean. The procedure:

1. **Same-name check.** If the current BGM name exists, `force` is false and `name` equals the current
   BGM name (byte-exact comparison of the complete stored strings, case-sensitive), nothing is
   restarted: go to step 5.
2. If `name` is exactly `演奏停止`: **stop all music** (§3.5), set the current name to `演奏停止` and
   finish (params are neither applied nor remembered).
3. **Stop all music** (§3.5). (The old music is now stopped even if the new one fails to start.)
4. Decide the device **by the name** (case-sensitive substring search):
   * name contains `.mid` → **MIDI** (§3.2);
   * otherwise, name contains `.wav` → **WAV music** on wave channel 0 (§3.4);
   * otherwise → **CD audio** (§3.7), track number = the decimal number found at byte offset 8 of the
     name (parsed as a decimal integer: leading whitespace skipped, optional sign, digits up to the first
     non-digit; 0 if there are none). **(quirk)** An
     uppercase extension such as `.MID`, `.WAV` or `NOSOUND.WAV` is therefore treated as a CD track
     (`NOSOUND.WAV` gives track 0, which never plays).

   The start **fails** if the file does not exist (MIDI, WAV), if there is no sound device (WAV), or if no
   audio CD is available / the CD command fails (CD).
5. If params are given and the start did not fail (or nothing was restarted, step 1): **apply the
   parameters** to the music: volume and pan according to b's type bits (§5.1) — type 1 to wave channel 0,
   type 2 to the MIDI output, type 3 to the CD output, type 0 nothing; and set the frequency of wave
   channel 0 to (b >> 16) × 70 Hz (0 = original). (When channel 0 is free, the wave part has no effect.)
6. Under the same condition: the current BGM's a and b := params. (After a failed start the recorded a, b
   keep their old values.)
7. The current BGM name := `name` (this happens also when the start failed **(quirk)**: a later request
   for the same name is then treated as "already playing" and is not retried unless forced).

So a repeated request for the music that is already playing does not restart it but re-applies volume, pan
and frequency (and records the new parameters). Requests with `force` = true always restart the music.

Callers:

| caller | name | params | force |
|---|---|---|---|
| event command 6 Play music | the command's string | (a2, a3) | no |
| field music (vehicle) — §3.8 | slot name | slot (a, b) | no |
| battle music, victory, level-up (slots 8, 9, 10) | slot name | slot (a, b) | no |
| "stop" requests (battle start/end, inn, movie, jingle skip) | `演奏停止` | none | no |
| resume of the saved BGM after battle/inn/movie | saved name | saved (a, b) | no |
| resume after the screen-mode switch | saved name | saved (a, b) | **yes** |
| after loading a save file (`10-save-files.md`) | current name from the file | its (a, b) | **yes** |

### 3.2 MIDI music

Starting a MIDI BGM: if the resolved file exists, all MIDI and CD playback is closed, the file is opened
on the MIDI sequencer and played from the beginning. (If the sequencer itself refuses the file, the start
still counts as successful for §3.1.)

**Looping:** when a MIDI piece reaches its end (only a complete, uninterrupted playback counts; stopping or
replacing the music never triggers this):

* if a movie is currently shown, nothing is done with the music (this end notification belongs to the movie
  mechanism, §7);
* otherwise, if the play-once flag is set, the flag is cleared and the music stays stopped;
* otherwise the same MIDI is played again **from the beginning** (whole-file loop; there are no loop
  points; the restart happens as soon as the end is reported).

Volume/pan of MIDI music is applied to the MIDI output as a whole (§5.1, type 2).

### 3.3 The play-once flag

The play-once flag is set by the battle start and by the inn just before they play their jingle (slot 7 or
11), but only if that slot's name contains `.mid` (§4). It is cleared: when the MIDI ends (§3.2), when the
jingle wait is skipped (§4.2), when the jingle MIDI file does not exist (§4.1), or when the jingle wait finds
no MIDI output (§4.2). While it is set, the MIDI does not loop.

### 3.4 WAV music

Starting a WAV BGM requires a sound device; the resolved file must exist. The file is loaded into wave
channel 0 and played **looping** (the whole sample data repeats seamlessly). It starts at full volume,
centre pan and its own sample rate; §3.1 step 5 then applies the parameters.

WAV files: RIFF/WAVE with a `fmt ` chunk and a `data` chunk; the sound plays with the format given in the
file. The sample games use uncompressed PCM, 8 or 16 bit, mono or stereo, 8 000 to 44 100 Hz (one file in
the corpus is IMA ADPCM; the original plays only formats its sound system accepts directly, so such a file
is probably silent — UNCERTAIN; implementations may play it).

### 3.5 Stopping

**Stop all music** = stop and close every MIDI and CD playback, then stop the sound in wave channel 0 and
free that channel. Sound effects (channels 1–3) are not affected. Stop-all-music by itself does **not**
change the current BGM name; requesting the name `演奏停止` (§3.1 step 2) stops and also sets the
name. The places where the runtime stops music without changing the name are: the end-game command
(code 30), the screen-mode switch, and the load sequence of `10-save-files.md`.

### 3.6 Saving and resuming the current BGM

**Save BGM**: if a current BGM name exists (always true after start-up), the saved BGM := copy of the current
BGM {name, a, b}, and the saved record is marked present (an older saved record is replaced).

**Resume BGM(force)**: if a saved record is present: PlayBGM(saved name, saved {a, b}, force); then the saved
record is discarded. If none is present nothing happens.

Because the saving features first request `演奏停止`, the resume call normally restarts the music from
the beginning (the name differs from `演奏停止`). If the saved name was itself `演奏停止`, the resume
does nothing audible.

Where this is used:

* **Battle** (`08-battle.md`, §1.3 "Start sequence" and §7 "Battle end sequence"): at the start: save BGM, request `演奏停止`, play the
  encounter jingle and wait for it (§4), then PlayBGM(slot 8) (battle music); the battle screen is set
  up only after that. Victory: PlayBGM(slot 9)
  (loops until the battle ends). Level-up message: PlayBGM(slot 10) for each member who levels up (not
  restarted for the second member because the name is the same). At the end of the battle: request
  `演奏停止`, then stop every loaded wave sound and unload all of them except the last loaded one (§4.3);
  all **four** wave channels become free. Then, unless the battle was lost or the main loop is being
  aborted, resume BGM (force no) — for a normal end or an escape before the map is shown again; for a
  battle ended by an escape-to-exit effect, after the teleport to the exit. A lost battle does not resume
  the music (the game-over event runs; if it returns and the screen is erased the leader's field music is
  played, `01-runtime.md` §10, "Game over").
  A **failed battle set-up** (missing background or `MONSTPIC.DAT`, `08-battle.md` §1.9) does none of
  this: no stop, no unloading, no resume. The battle music started before the set-up keeps playing on
  the map, and the saved BGM record stays present until the next save replaces it **(quirk)**.
* **Inn** (`07-menus-shop-inn.md`, "Inn"): after paying: close the message window, fade the screen to black,
  save BGM, request `演奏停止`, set the play-once flag if slot 11's name contains `.mid`, play slot 11 as a
  jingle, restore HP/MP/status, Wait(10) (1.0 s, ended early by the confirm key), wait for the jingle (§4.2),
  resume BGM (force no), then redraw and fade the map in.
* **Movie** (§7): save BGM, request `演奏停止`; at the end resume BGM (force no).
* **Screen-mode switch** (`01-runtime.md`): save BGM, stop all music (name unchanged), sound device
  recreated (all wave sounds end), and after the switch resume BGM with **force = yes** (needed because the
  current name still equals the saved one).

The saved record is a single slot: nested use (e.g. a movie during an inn) is not possible because these
features cannot run inside each other.

### 3.7 CD audio

At program start the runtime checks for an audio CD: if one is present it notes the number of tracks;
otherwise CD audio is unavailable and every CD request fails.

Playing track N: stop and close any previous CD playback, then play track N to its end (if N is the last
track, to the end of the disc). When the track ends normally it is played again from its start (CD music
always loops; the play-once flag does not apply). Volume/pan for type-3 parameters go to the CD output.

The name format the editor uses for CD tracks is not present in the sample games: the runtime reads the
track number from byte offset 8 of the name (UNCERTAIN what text precedes it; a name shorter than 9
bytes gives undefined results in the original). An implementation without CD support may treat every CD
request as failed; note that the previous music has already been stopped (§3.1 step 3).

### 3.8 Field (vehicle) music

"Play the leader's field music" = take the vehicle type v (0..6) of the leader's map character (the vehicle
when riding; it can also be set by event command 53) and, if slot v has a name, PlayBGM(slot v name,
slot v {a, b}, force no). This happens:

* when a new game starts, after the start event (`01-runtime.md`, "New game");
* when boarding a vehicle has finished (slot = the vehicle's type) and when alighting has finished (slot =
  the leader's type, normally 0) — `04-map-movement.md`, "Vehicles";
* on event command 32 Play vehicle music;
* after a lost battle when the game-over event returns with the screen erased.

Changing maps never changes the music by itself.

---------------------------------------------------------------------------------------------------

## 4. Jingles

### 4.1 Playing a slot as a jingle

The encounter (slot 7) and inn (slot 11) sounds are played "as a jingle": if the slot has a name:

* if the name contains `.mid`: apply the slot's volume and pan as in §5.1 for type 2 (MIDI output) or
  type 3 (CD output), according to b's type bits — so type 3 changes the CD volume, not the MIDI volume.
  With type bits 0 or 1 the original looks up an undefined mixer line (there is no wave sound to which
  type 1 could apply); UNCERTAIN, assume no change. All MIDI jingles in the corpus use type 2. This is
  done before the start. Then start the MIDI as in §3.2 (this closes any MIDI/CD playback; it does **not** change the
  current BGM name and does not touch wave channel 0). If the file does not exist, clear the play-once flag.
  The play-once flag was set by the caller, so the jingle does not loop.
* otherwise: play it as a **sound effect** with the slot's {a, b} (§5.2), i.e. on one of the sound-effect
  channels, once, honouring the exclusive flag.

The callers have already saved the BGM and requested `演奏停止` before this.

### 4.2 Waiting for a jingle

After starting a jingle the battle start and the inn wait for it. `isMidi` = the slot's name contains `.mid`.

1. If not isMidi and there is no sound device: return at once.
2. If isMidi and there is no MIDI output (the original checks that the system mixer has a synthesizer
   line): clear the play-once flag and return at once.
3. Loop, keeping the program responsive (window messages processed; this is a busy wait without game
   ticks):
   * if **Space or Enter is currently held down** (a level check of the key state, not a new key press):
     **skip** — request `演奏停止` (§3.1: all music stops and the current name becomes `演奏停止`),
     stop and free every sound-effect channel (1–3) whose sound is still playing, clear the play-once flag,
     return;
   * isMidi: continue looping while the play-once flag is set (it is cleared when the MIDI ends);
   * WAV: continue looping while a sound-effect sound is playing (see the quirk below).

**(quirk)** For WAV jingles the original does not test channels 1–3 but the loaded sounds that have the
**sound numbers** 1, 2 and 3 (§4.3), and waits while any of them is playing. When fewer than three other
wave sounds are loaded at the moment the jingle starts, the jingle itself has number 1, 2 or 3, and the
wait lasts until it has finished (and until any other sound numbered 1–3 that is still playing has
finished). When numbers 1, 2 and 3 are all held by other loaded sounds — typically after three or more
sound effects have played since the last battle end, movie or screen-mode switch, because a sound
effect that simply finishes stays loaded — the wait ends immediately and the battle music starts while
the jingle is still sounding. An implementation must model the sound numbering of §4.3 to get the same
timing.

**(quirk)** If a MIDI jingle file exists but the MIDI output cannot play it (a damaged file, or no
working MIDI device although the mixer has a synthesizer line), the end of the jingle is never reported:
the play-once flag stays set and the wait lasts until Space or Enter is held. Implementations should
treat a MIDI jingle that fails to start as ended (clear the play-once flag) unless they want to reproduce
this.

**(quirk)** Because the key test is a level test, holding Enter or Space when the battle starts (for example
an event started with the action button that immediately starts a battle while the key is still down) skips
the jingle at once.

### 4.3 Sound numbers and loaded wave sounds

The wave channels (Terms, at the start of this chapter) only record *which* loaded sound they hold. Separately from them, the
sound device keeps a list of **loaded wave sounds**, in the order they were loaded, and every loaded
sound has a **sound number**:

1. Loading a wave sound (WAV music, a sound effect or a WAV jingle) appends it to the end of the list and
   gives it the lowest number **≥ 1** that no other sound in the list holds. (Number 0 is never given.)
   Numbers go up to 255; when 1..255 are all held, the new sound still plays but gets **no** number: the
   channel that started it is recorded as free at once, so this sound can never be stopped or replaced
   through its channel **(quirk)**. WAV music started in that state keeps looping, also under later
   music, until the next battle end, movie or screen-mode switch. (This needs more than 250 sound
   effects between two battles; implementations may ignore it.)
2. A sound keeps its number until it is **unloaded**. Unloading happens only:
   * for a sound-effect channel's sound when it is cut off: by the exclusive flag (§5.2 step 2), by the
     replacement of channel 1 (§5.2 step 5) and by the jingle skip (§4.2, only sounds still playing);
   * for WAV music by stop-all-music (§3.5);
   * at the end of a battle (§5.3): every sound in the list is stopped, and every sound except the
     **last one in the list** is unloaded; that last sound stays loaded, stopped, and keeps its number
     **(quirk)**;
   * when the sound device is shut down and recreated (movie, screen-mode switch): all sounds are gone.
3. A sound effect that simply finishes playing is **not** unloaded: its channel is only marked free
   (§5.2 step 3), and the finished sound keeps its number **(quirk)**. Finished sound effects therefore
   accumulate between battle ends, movies and screen-mode switches.

Example: in a game with MIDI field music, after program start three sound effects are played on the map
one after another, each finishing
before the next starts. They get numbers 1, 2 and 3 (each uses channel 1 again, but the finished ones stay
loaded). A following encounter with a WAV jingle gives the jingle number 4, so the jingle wait ends at
once. With only two such sound effects the jingle gets number 3 and the wait lasts until it ends.

---------------------------------------------------------------------------------------------------

## 5. Parameters and sound effects

### 5.1 Applying volume, pan and frequency

Given {a, b}: att = a >> 16 (signed), pan = a & 0xFFFF, type = b & 3.

**type 1 — the WAV sound's own controls** (on the wave channel being configured):

* volume: −40 × att in hundredths of a decibel, i.e. an attenuation of **0.4 × att dB**. Amplitude factor
  = 10^(−att / 50). att 10 → −4 dB (×0.631), 20 → −8 dB (×0.398), 30 → −12 dB (×0.251), 100 → −40 dB (×0.01).
  The sound system accepts 0 down to −100 dB; a value outside (att < 0 or att > 250) is rejected and the
  sound keeps its previous volume (full volume for a newly loaded sound) — UNCERTAIN, derived from the
  limits of the original's sound system, not observed (the corpus uses 0..100).
* pan: P = (pan − 50) × 20 hundredths of a decibel (−1000..+1000 for pan 0..100). P > 0 attenuates the
  **left** channel by P/100 dB (right stays full); P < 0 attenuates the **right** channel by −P/100 dB.
  Pan 0 → right −10 dB; pan 100 → left −10 dB; pan 50 → centre. (Values making |P| > 10 000 are rejected.)
  The pan attenuation adds to the volume attenuation.
* frequency (applied for every type, see below).

**type 2 — MIDI output, type 3 — CD output**: a linear gain on the whole MIDI (or CD) output.
With R = the full scale of the output volume control:
V = (100 − att) × R / 100 (unsigned 32-bit integer arithmetic, truncating);
if pan < 50: left = V, right = 2 × (V × pan / 100); else: left = 2 × ((100 − pan) × V / 100), right = V
(each division truncating). In relative terms: gain g = (100 − att)/100, left = g, right = g × pan/50 for
pan < 50; left = g × (100 − pan)/50, right = g for pan ≥ 50. The original sets the **system-wide**
synthesizer / CD volume of the computer (and restores the user's original volumes at program exit); an
implementation must apply the same gains to its own MIDI or CD output only. The setting persists until the
next type-2/3 parameter application **(quirk)**: a MIDI BGM started with type bits 0 plays at whatever MIDI
volume was set last, and a sound effect whose record has type 2 changes the volume of the MIDI music.

**type 0**: no volume or pan change.

**Frequency**: whenever parameters are applied to a wave sound (BGM channel 0 in §3.1, or a sound-effect
channel), its playback sample rate is set to (b >> 16) × 70 Hz, or to the file's own rate when that is 0 —
independently of the type bits. This changes pitch and speed together (ratio = new rate / file rate). The
original's sound system accepts rates from 100 to 100 000 Hz (UNCERTAIN exact limits); a rejected rate leaves
the previous rate.

A newly loaded wave sound starts at full volume, centre pan and the file's rate; parameters are applied
immediately after playback has started.

### 5.2 Playing a sound effect — PlaySE(name, params)

`params` is an {a, b} pair or *none*. There are **3 sound-effect channels** (wave channels 1, 2, 3). Steps:

1. Resolve the file (§2). If it does not exist: do nothing.
2. If params are given and b has the **exclusive** bit (4): stop and free all three sound-effect channels.
3. Mark free every sound-effect channel whose sound has finished playing. The finished sound itself
   stays loaded and keeps its sound number (§4.3) **(quirk)**.
4. Choose the **lowest-numbered free** channel among 1, 2, 3. Load the file into it and play it once
   (no looping). If params are given and the load succeeded, apply them (§5.1: volume/pan by type, and
   frequency).
5. If no channel was free (all three are playing): stop and free channel 1, load the file into channel 1,
   play it, apply params as in step 4.

If loading fails (no sound device, not a valid WAV — e.g. the 27 sound-effect commands in the corpus that
name a `.mid` file), the channel stays free and nothing is heard. Sound effects never wait; the event or
battle continues at once. WAV music (channel 0) is never used for sound effects.

Callers: event command 21 Play sound effect (params = (a2, a3)); jingles that are not MIDI (§4.1, params = slot);
built-in sound effects (§5.4, params = none → full volume, centre, file rate, never exclusive).

### 5.3 Stopping sound effects

Sound effects end by themselves. They are cut off by: the exclusive flag (§5.2), reuse of channel 1
(§5.2 step 5), the jingle skip (§4.2), the end of a battle (every loaded wave sound is stopped and all
but the last loaded one are unloaded, §4.3; all four wave channels become free), a movie and the
screen-mode switch (the sound device is recreated, every wave sound ends). Requesting `演奏停止` does
not stop sound effects.

### 5.4 Built-in sound effects

The runtime has a table of 53 built-in sound effects, numbered 0..52 ("built-in sound effect n"; file names
in `11-appendix.md`, all `.wav`, looked up in the game folder, §2). They are played with PlaySE(name, none).
Uses:

* built-in sound effect 11 (`punch.wav`): every time damage is applied in battle (`08-battle.md`);
* built-in sound effect 50 (`zap64.wav`): the first phase of an enemy's "jump" before its attack
  (`08-battle.md`);
* built-in sound effect 46 (`wibble.wav`): the return/escape warp effect of items and magic
  (`07-menus-shop-inn.md`, `08-battle.md`);
* the sound number of a battle animation file (table in `08-battle.md`, §5.2 "Animation files"): at the start
  of an item/magic effect (not for spell types 20 and 21, teleport and escape; their palette flash is
  still applied) and for each animation part.

There are **no** sounds for cursor movement, confirmation, cancellation or refusal anywhere in the runtime.

---------------------------------------------------------------------------------------------------

## 6. Event commands (audio)

The argument layout is that of `05-events.md`; str(k) is the event's string number k.

### 6.1 Code 6 — Play music {6, string, a, b}
PlayBGM(str(a1), {a2, a3}, force no) (§3.1). No wait. `演奏停止` stops the music.

### 6.2 Code 21 — Play sound effect {21, string, a, b}
PlaySE(str(a1), {a2, a3}) (§5.2). No wait.

### 6.3 Code 49 — Set audio slot {49, string, a, b}
slot s = (a3 >> 8) & 0xFF; slot s := {copy of str(a1), a2, a3}. Nothing is played or stopped, even if the
slot is the one currently heard. The corpus uses slots 0, 4, 7, 8, 9, 10. A value s ≥ 12 is outside the table
(undefined in the original; implementations should ignore the command).

### 6.4 Code 32 — Play vehicle music
Play the leader's field music (§3.8).

### 6.5 Code 30 — End game
Stop all music (§3.5) before the program ends (`01-runtime.md`).

---------------------------------------------------------------------------------------------------

## 7. Movies — event command 14 Play movie {14, string, flags}

### 7.1 Arguments

* a1: string number of the movie file name (an AVI file), resolved as in §2.
* a2 = flags:

| bits | meaning |
|---|---|
| 0 | **fit to screen**, keeping the aspect ratio (§7.3) |
| 2 | **black background**: the screen is blacked out during the movie |
| 4–7 | **position** p (0..8) when not fitted: column h = p mod 3, row v = p div 3 (§7.3) |
| 1, 3, others | ignored |

Both movies in the sample games use flags 0x45 (fit, black background, centre).

### 7.2 Sequence

1. If the file does not exist: do nothing (the event continues).
2. If the message window is open, close it.
3. Save BGM (§3.6), request `演奏停止` (all music stops), and shut down the sound device: all sound
   effects stop. (The original does this so that the movie's own sound can use the audio hardware.)
4. If **black background**: if the screen state is `MAP` (0), fade the palette to black (palette effect mode
   5 to black, without changing the stored tint — `03-display.md`, §4 "Tint and palette effects"); then fill
   the whole visible screen with black; screen state := `MOVIE` (4). In other screen states there is no fade,
   only the black fill. Without the flag the screen is left as it is and the movie appears over it.
5. Flush keys in the no-wait form (`01-runtime.md` §5.2), run one message-processing pass
   (`01-runtime.md` §3.2) and clear the confirm flag (`01-runtime.md` §1).
6. Open the movie and show it in a rectangle computed as in §7.3; the video is scaled to that rectangle.
   Start playback.
7. Wait until the movie has ended: this is the blocking wait Wait(0) in its movie form (`01-runtime.md`,
   "Waiting"), which keeps the program responsive and ends when the movie is gone. The movie is closed
   (and the wait ends) when:
   * the **confirm** input arrives (Enter or Space key press, or a mouse click — the same input that ends
     Wait(0)); the movie is stopped and closed;
   * **Escape** is pressed: playback is stopped and the movie closed;
   * playback reaches the end of the movie: the movie is closed.
   Nothing else is drawn during playback (events, maps and windows are frozen; the movie is on top).
8. Afterwards:
   * without black background: realise the committed palette (the "base palette", `03-display.md` §2.3)
     instantly, because the movie may have disturbed the 8-bit palette; then run one message-processing
     pass. The area the movie covered is repainted with the
     game screen (UNCERTAIN: in the original the movie area is repainted by the next screen update; an
     implementation should simply show the unchanged game screen again).
   * with black background: fill the screen with black, set the palette to black instantly (no tint
     change), present the back buffer (the map or picture that was on screen before), then fade the palette
     from black to the normal palette with the stored tint (palette effect mode 5, `03-display.md`), and
     restore the screen state that was current before step 4.
9. Recreate the sound device (empty: no wave sound is loaded, all wave channels free, sound numbering
   starts again, §4.3) and resume BGM (force no, §3.6), which restarts the saved music from its
   beginning.
10. The committed palette is selected again for the program's own window drawing (no visible change; an
    implementation needs no action).

The event then continues with its next command. The message window was closed in step 2, so no message
window exists after the movie.

### 7.3 Movie rectangle

Let w × h be the movie's native frame size.

**Not fitted** (bit 0 clear): size w × h, position
* x = 24 (h-column 0), (640 − w) / 2 (column 1), max(0, 616 − w) (column 2);
* y = 24 (row 0), (480 − h) / 2 (row 1), max(0, 456 − h) (row 2) (i.e. 24 px margins to the left/top or
  right/bottom edges; divisions truncate toward zero, so the centred position is negative for movies larger
  than the screen).
* p = 9..15 (rows 3..5): the vertical position is undefined in the original (UNCERTAIN); recommended: treat
  as row 2. The column rule still applies.

**Fitted** (bit 0 set):
* if h < w: h' = h × 640 / w, w' = 640, x = 0, y = (480 − h') / 2;
* else: w' = w × 480 / h, h' = 480, y = 0, x = (640 − w') / 2;
* then **(quirk)** w' := min(w', 639) and h' := min(h', 479), applied after the position has been computed:
  a fitted movie leaves the rightmost pixel column or the bottom pixel row uncovered. For movies wider than
  tall but narrower than 4:3 (w > h and 3·w < 4·h, e.g. 500 × 450), h' exceeds 480, y becomes negative and
  h' is clamped to 479 (top and bottom cut off).
* Integer arithmetic, divisions truncating.

The position flags are ignored when fitting.

### 7.4 Failure cases

If the file exists but cannot be opened as a movie, the original shows a message box with the text
`avi open` (no title; the system's default error title) and then waits as in step 7 with an empty movie
area: an empty child area (initially at 100, 100, 360 × 270) remains on the screen until the player presses
confirm (UNCERTAIN, rarely relevant). An implementation may instead skip the playback (continue at step 8).

### 7.5 Information for implementers: movie formats

The original plays AVI files through the operating system's video player, so any codec installed on the
system works. The sample games contain two movies, both **video only** (no audio stream), 30 frames per
second (33 333 µs per frame):

| game | file | size | codec |
|---|---|---|---|
| soccer | `hanabi.avi` | 160 × 120, 24 bit | Cinepak (`cvid`) |
| Check mate | `002.avi` | 320 × 240, 24 bit | Intel Indeo Video 4.1 (`IV41`) |

An implementation therefore needs at least a Cinepak and an Indeo 4 decoder to play the corpus, plus
uncompressed and RLE AVI for completeness. Frames are shown at the rate given by the AVI header. If a movie
has an audio stream the original plays it too (UNCERTAIN, no example); implementations should play it if
they can.

---------------------------------------------------------------------------------------------------

## Quirks in this chapter

* §2 — base names are cut at the last 0x5C byte even if it is a Shift_JIS trail byte.
* §3.1 — the device is chosen by a case-sensitive search for `.mid` / `.wav` in the name; anything else
  (including `.MID`, `.WAV`, `NOSOUND.WAV`) is taken for a CD track.
* §3.1 — the old music is stopped before the new one is checked; a missing file leaves silence.
* §3.1 — a failed start still sets the current BGM name, so the same name is not retried unless forced.
* §3.1 — the type bits of b select only the volume control, not the device.
* §3.6 — the screen-mode switch and loading a save restart the music (force); other resumes restart it only
  because `演奏停止` was requested before.
* §1.2 — the built-in default slot names are never used; slots start without a name (slot 11 stays so in pre-0.96 games).
* §3.6 — a failed battle set-up leaves the battle music playing on the map and does not resume the saved BGM.
* §4.1 — a MIDI jingle's volume goes to the MIDI or CD control by its type bits (type 3 changes the CD volume).
* §4.2 — the WAV jingle wait tests the sounds numbered 1..3, not the jingle's channel; it ends at once when other loaded sounds hold all three numbers.
* §4.2 — the jingle skip tests whether Enter/Space is held, not pressed.
* §4.2 — a MIDI jingle that exists but cannot be played makes the wait last until Space/Enter is held.
* §4.3 — finished sound effects stay loaded and keep their sound numbers until the next battle end, movie or screen-mode switch.
* §4.3 — the battle end leaves the last loaded wave sound loaded (stopped), keeping its number.
* §4.3 — when all sound numbers 1..255 are in use, a new sound plays but can never be stopped.
* §5.1 — MIDI/CD volume is a persistent output-wide setting; a sound effect with type 2/3 changes the MIDI/CD
  volume and itself plays at full volume.
* §5.1 — frequency is applied to wave sounds regardless of the type bits.
* §5.2 — when all three effect channels are busy, channel 1 is always the one replaced.
* §7.3 — fitted movies are clamped to 639 × 479 after positioning.
* §7.4 — an unopenable movie leaves an empty area until confirm.

## Open questions

* §3.7 — the exact text the editor stores for CD-audio tracks (track number at byte offset 8) is
  unknown; no sample uses CD audio.
* §1.2 — what the original does when the inn or a battle start uses a slot without a name (only possible
  with data older than 0.96, none in the corpus).
* §5.1 — the accepted frequency range of the original sound system and the curve of the system MIDI
  volume control (assumed linear in amplitude).
* §3.4 — whether compressed (ADPCM) WAV files play in the original.
* §7.2 — the original's screen contents in the movie area after a movie without black background.
* §7.5 — whether AVI audio is played (no sample has audio).
