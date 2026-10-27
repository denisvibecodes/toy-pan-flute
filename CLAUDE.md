# Pan flute

`README.md` for what this is, how to run it and where each part lives.

Working with you is Denis. He plays a MIDI keyboard for the keys and a TEControl breath
controller for the breath, the bend and the vibrato; he checks the sound by ear.

## What this is

A pan flute as a physical model in plain C, compiled to WebAssembly and played in a web page
over Web MIDI. One preset, one instrument, no slides between pipes, no oversampling,
single-precision floats. It is written so that one reader can take in the whole synth in a
sitting and carry it to another target.

## How the model evolves

- The C in `src/` is plain C99, free of any platform or Emscripten-specific code: the web page,
  the native harness and a future firmware all include the same files.
- Static state, no allocation, no I/O.
- Floats are `float` throughout. Transcendentals come from the platform's `<math.h>`; builds may
  differ in the last bits.
- `src/config.h` is the one file a player edits: the MIDI defaults, the maps, the times.
- `src/coefficients.h` is generated, not edited: the 25 cut pipes, the constants and the tone
  curve as numbers.
- The comments describe a cut instrument and its physics, with literature citations in short
  form (Fletcher & Rossing; Verge 1997; Auvray 2012, 2014, 2016; de la Cuadra 2005, 2008;
  Magron 2013).
- Every text file wraps at 100 columns: the C, the JavaScript, the Markdown (`just check-md`;
  `just fmt-md` rewraps). A table row or a URL may run longer.
- Denis is named, in docs and comments alike; never "the user".
- A commit message is one line of what and why.

## Where each piece lives

| what | where |
| --- | --- |
| the model: bore, mouth, jet, pipe, the raft of 25; the gestures; the shaping; the calibration | `src/panflute.h`, `src/panflute.c` |
| the player's settings | `src/config.h` |
| the cut pipes, the constants, the equalizer's biquads | `src/coefficients.h` |
| the listening reverb, Freeverb, on or off | `src/reverb.h`, `src/reverb.c` |
| the page, the AudioWorklet, the Web MIDI glue; the phone page | `web/` |
| the synth and the reverb chained for the page | `web/page.c` |
| a native program that renders phrases to WAVs | `harness/render.c` |
| the page served over https for a phone | `tools/serve_https.py` |
| the builds and the servers | `justfile` (`just --list`) |
| the page built and published on every push | `.github/workflows/pages.yml` |
