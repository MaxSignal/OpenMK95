# Documentation

- [architecture/](architecture/README.md): how OpenMK95 is structured, for developers.
  - [Overview](architecture/README.md): layers, program structure, display, threads, determinism
  - [Core and platform layer](architecture/platform.md): text, files, bitmaps, font, random numbers, SDL host,
    headless mode
  - [Engine](architecture/engine.md): main loop, display model, windows, events, battle, save files
  - [Sound and movies](architecture/sound-movie.md): wave channels, MIDI, AVI decoding and playback
- [cleanroom/](cleanroom/README.md): records of the clean-room development process.
  - [spec/](cleanroom/spec/README.md): the clean-room specification
  - [process rules](cleanroom/process-rules.md) and [observation under Wine](cleanroom/wine-observation.md)

See also the [README](../README.md) and [CONTRIBUTING.md](../CONTRIBUTING.md).
