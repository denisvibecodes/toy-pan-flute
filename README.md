# Toy pan flute

A pan flute as a physical model, in plain C, played in your browser from two MIDI controllers:
a keyboard for the notes, and a breath controller for the breath, the bend (the bite) and the
vibrato (a head tilt).

The page: <https://denisvibecodes.github.io/toy-pan-flute/>.

Each of the 25 pipes (G4 to G6) is a stopped cylindrical tube with a lip resting over its open
end and an air jet blown across it, in pascals, metres and seconds: the jet's deflection grows as
it travels to the far rim, its flow drives the tube, the tube's wave returns through its wall and
radiation losses and deflects the jet again. Nothing is added for the attack or the release: the
tone starts when the jet's flow rises and dies when it stops. The breath sets the blowing
pressure, the bend covers the end with the lip and bends the pitch down, the vibrato wobbles the
covering.

## Folders

| what | where |
| --- | --- |
| the synth: model, gestures, shaping; plain C99 | `src/panflute.h`, `src/panflute.c` |
| the player's settings: the MIDI defaults, the maps, the times | `src/config.h` |
| the 25 cut pipes and the tone curve, as numbers | `src/coefficients.h` |
| the listening reverb, Freeverb, on or off | `src/reverb.h`, `src/reverb.c` |
| the web page, the AudioWorklet, the Web MIDI glue | `web/` |
| a native program that renders a phrase to a WAV | `harness/` |
| the page served over https for a phone | `tools/serve_https.py` |

## Running it

The page needs a browser with Web MIDI: Chrome or Edge. Firefox asks to install a permission
add-on on first use; Safari has none. `just serve` serves it at <http://localhost:8000>. Plug in
the two controllers, open the page, allow MIDI, click anywhere on the page (a browser starts sound
only after a click or a key), blow.

The Inputs box says which MIDI input and channel the notes come from, and which input, controller
number and channel the breath, the bend and the vibrato come from; it starts from `config.h`'s
defaults (the TEControl's: breath CC 2, bend CC 1, vibrato CC 13) and remembers changes in the
browser. The Calibration box fits the maps to the player: where the breath's pp, mf and ff fall
(384, 480 and 960 Pa on the lowest pipe), where the bend starts, finds and holds the semitone and
reaches the whole tone, and how deep the vibrato goes. A marker is dragged on its graph, typed, or
set from a gesture held for a second; each graph shows the curve, the controller's value on it and
what the synth applies now. The Monitor shows each control over the last 5 seconds and the output's
level. The reverb, Freeverb, is on (the default) or off. The volume starts at −6 dB: the high pipes
at ff go over full scale, and CLIP lights when the output clips.

Without controllers, the computer keyboard plays: the lower row Z S X D C V G B H N J M is C to
B (then , L . ; / continue), the upper row Q 2 W 3 E R 5 T 6 Y 7 U is the octave above (then I 9
O 0 P), ↑ and ↓ shift the octave, and a held key blows a steady breath at the mf marker.

### On a phone

`phone.html`, served beside the page, plays the flute on a phone held like a recorder: the bottom
edge, where the microphone is, under the lower lip, the screen towards the player, the phone
pointing away and down. The breath is the level of the wind noise the blowing makes in the
microphone, low-passed at 150 Hz (the pipes start at G4, 392 Hz, and a phone's speaker gives
little under 300 Hz, so the flute heard back is mostly filtered out), with a gain beside its
gauge, then smoothed, rising within 15 ms and falling over 60 ms (Settings sets both); the bend
is the phone tilted further down from its rest, the lip covering the pipe (tilted up, nothing),
the Rest button beside its gauge taking the last second's angle as the rest; the vibrato is the
phone's left-right wobble, the accelerometer's sideways reading in g, band-passed at 2.5 to 8 Hz
(a hand vibrato's rates), through a compressor that keeps a wide wobble near ±9 c (it turns the
depth down within 0.2 s and gives it back over 3 s). The 25 keys are a
piano's keyboard stood on end on the right of the screen, the low notes at the far end (Settings
turns them round); a finger lifted goes back to the latest key still held. Settings sets the
breath's floor, pp, mf and ff in dB and the bend's rest and whole tone, each from a second held,
and the vibrato's depth and ceiling. The volume, beside the output's gauge, starts at −9 dB: the
high pipes peak just under full scale at ff and touch it past ff. Wired headphones keep the flute
out of the microphone; Bluetooth ones add a fifth of a second.

The latency is mostly the phone's: its speaker's buffer and its microphone's. Settings measures
the two together, the round trip: the phone beeps and times the beep's return in the microphone.
The breath skips Web Audio's input, which holds the microphone's samples in a buffer of its own
(about 50 ms on Denis's phone at its own rate, 200 ms at 44.1 kHz): the page reads the
microphone's track directly (Chrome's `MediaStreamTrackProcessor`) and posts its chunks to the
worklet, which filters them as it would its input. Settings can switch back to Web Audio's input,
which also takes over where the direct route is missing.
The sound runs at the phone's own rate (48 kHz on most), the synth resampled from 44.1 kHz in the
worklet, since a context forced to 44.1 kHz plays through the browser's resampler with a longer
buffer; Settings can force 44.1 kHz to compare.

The microphone and the motion sensors work only on a secure page, so the phone page is served
over https: `just serve-https` (`tools/serve_https.py`) makes a self-signed certificate for the
PC's addresses into `build/https/`, serves `web/` on port 8443 and prints the address to open on
the phone, `https://<the PC's address>:8443/phone.html`, on the same Wi-Fi. The phone warns about
the certificate on the first visit: Advanced, then Proceed. On its first run Windows asks whether
Python may take the port: allow it on private networks. Chrome on the PC (`chrome://inspect`,
with USB debugging on the phone) shows the phone page's console.

Building the page needs Emscripten (the `justfile` loads emsdk from `%LOCALAPPDATA%\emsdk`);
building the harness needs MSVC (or any C99 compiler). `just render` writes three WAVs to
`build/`: a phrase (a swell, hops, a bend, a vibrato, ff, the release), the 25 pipes in turn at
mf, and G4, D5 and G6 at pp, mf and ff. `just --list` shows the commands.

## Licence

MIT, see `LICENSE`.
