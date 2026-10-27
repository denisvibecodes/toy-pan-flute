// The phone page: the phone played like a recorder. The microphone hears the breath (worklet.js
// turns its level into the breath), the accelerometer gives the bend (the phone tilted down
// covers the pipe) and the vibrato (the left-right wobble), the keys on the right side of the
// screen give the notes. The synth is the MIDI page's: the same worklet, the same module.

const CONTROLS = ["breath", "bend", "vibrato"]; // in the order of panflute.h's pf_control
const CAL = { pp: 0, mf: 1, ff: 2, rest: 3, semiLow: 4, semiHigh: 5, full: 6, depth: 7 };
const CAL_ITEMS = 8;
const STATE = { key: 0, pa: 1, paG4: 2, bendCents: 3, vibG: 4, vibCents: 5, covering: 6 };
const FIRST_KEY = 67; // G4, the lowest pipe
const PIPES = 25;
const START_KEY = 72; // C5: the lip starts on a pipe, so a breath sounds before any key is touched
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const BLACK = new Set([1, 3, 6, 8, 10]);
const STORE = "panflute.phone.v1";
// config.h's vibrato scale: g = (value - PF_VIBRATO_CENTRE) * PF_VIBRATO_G_PER_UNIT
const VIBRATO_CENTRE = 61.5;
const VIBRATO_PER_G = 61;
const G = 9.80665;
const TILT_TAU_MS = 15; // the angle's smoothing: the hand's jitter, not the gesture
const HYSTERESIS = 0.7; // a held angle does not flicker between two bend values
const HISTORY_MS = 1000; // a Set takes the median of the last second
const MIC_RANGE = [-80, 0]; // the breath gauge, dB
// The vibrato's band: a pan flute's hand vibrato runs at 2.5 to 8 Hz (a teacher's lesson, 54
// pieces, median 5.2 Hz; a flute's is 5 to 6 Hz, Fletcher 2001). The sideways tilt is
// band-passed there, a second-order Butterworth high-pass and low-pass, before the synth's own
// 0.5 Hz high-pass: a lean, the hand's drift and its tremor bend nothing.
const VIBRATO_LOW_HZ = 2.5;
const VIBRATO_HIGH_HZ = 8;
// The vibrato's compressor. The wobble's size is the peak of the band-passed tilt, held through a
// wobble's cycle; past the ceiling, the depth is scaled so the wobble's cents sit at the ceiling.
// The scale falls within GAIN_ATTACK_S and rises back over GAIN_RELEASE_S, so a wide gesture
// keeps its shape instead of being clipped.
const PEAK_RELEASE_S = 0.3;
const GAIN_ATTACK_S = 0.2;
const GAIN_RELEASE_S = 3;
const PINGS = 5;
const PING_GAP_MS = 250;
const TOUCH_DELAYS = 9; // the last key presses' delays kept, for "This phone"

const $ = (id) => document.getElementById(id);

let wasmBytes = null;
let cal = null; // the synth's calibration: config.h's, the vibrato's depth Denis's
let defaults = null;
let settings = null;
let ctx = null;
let node = null;
let ready = false;
let micStream = null;
let micSource = null;
let micError = null;
let wakeLock = null;

const values = { breath: 0, bend: 0, vibrato: Math.round(VIBRATO_CENTRE) };
const sensor = { theta: null, gx: 0, last: 0, count: 0, rate: 0, rateAt: 0 };
// the vibrato's band-pass (its two filters' states), the compressor's held peak in g and scale
const vib = { hp: svfState(), lp: svfState(), peak: 0, scale: 1, sent: null };
let pong = null; // the pending beep's resolver
let roundTrip = null; // the last round trip measured, ms
const touchDelays = []; // ms from a key's touch, as the screen stamped it, to its handler
let feed = null; // the direct route while it feeds the worklet: { stop }
let feedError = null; // why the direct route failed, Web Audio's input taking over
const mic = { db: -Infinity };
const history = { mic: [], tilt: [] }; // [time in ms, value]
let bendX = 0; // the bend before rounding, for the hysteresis
// The bend's low-pass, a second-order Butterworth below the vibrato's band: the wobble's share
// of the long axis's angle (the wrist's axis off the phone's, the sideways push) bends nothing.
// It starts at the first angle read.
const bendLp = { ic1: 0, ic2: null };
let synthState = null;
const meter = { db: -Infinity, clipUntil: 0 };

const touches = new Map(); // pointer id -> MIDI key
const order = []; // pointer ids, the latest last
let sounding = START_KEY; // the key the lip is on

// --- the synth -----------------------------------------------------------------------------

function post(msg) {
    if (ready) node.port.postMessage(msg);
}

function control(name, value) {
    if (values[name] === value) return;
    values[name] = value;
    post({ t: "ctl", id: CONTROLS.indexOf(name), value });
}

function micOn() {
    return settings.source === "mic" && micSource !== null;
}

// The mic's map for the worklet: Denis's levels in dB onto the synth's pp, mf, ff values. The
// levels are after the gain, the worklet's dB before it.
function sendMicMap() {
    const m = settings.mic;
    const g = settings.gain;
    post({
        t: "mic",
        map: micOn() ? {
            floor: m.floor - g, pp: m.pp - g, mf: m.mf - g, ff: m.ff - g,
            vpp: cal[CAL.pp], vmf: cal[CAL.mf], vff: cal[CAL.ff],
            riseMs: settings.breathRiseMs, fallMs: settings.breathFallMs,
        } : null,
    });
}

async function loadSynth() {
    const res = await fetch("panflute.wasm");
    if (!res.ok) throw new Error(`panflute.wasm: HTTP ${res.status}`);
    wasmBytes = await res.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(wasmBytes, {});
    const ex = instance.exports;
    if (ex._initialize) ex._initialize();
    ex.page_init();
    cal = Array.from({ length: CAL_ITEMS }, (_, i) => ex.pf_calibration(i));
    defaults = {
        source: "mic",
        // Guesses until Denis sets them on his phone: the low-passed wind noise, dB re full scale.
        mic: { floor: -60, pp: -48, mf: -38, ff: -20 },
        gain: 0,
        // The breath's smoothing: it rises within 15 ms, so an attack keeps its edge, and falls
        // over 60 ms, so the wind noise's dips do not chop it.
        breathRiseMs: 15,
        breathFallMs: 60,
        // The recorder's hold is about 35 degrees down; 25 more is the whole tone.
        tilt: { rest: 35, range: 25 },
        // The bend's low-pass, Hz: at 2 Hz the wobble is 16 dB down at 5 Hz, the bend lags
        // about 0.1 s.
        bendHz: 2,
        depth: cal[CAL.depth],
        ceiling: 9, // cents
        rate: "native",
        route: "direct",
        order: "top",
        reverb: "on",
        // The volume beside the output's gauge. The synth peaks at +8.6 dBFS at ff and +9.4 past
        // it (the high pipes, the reverb on): at -9 dB, just under full scale at ff.
        volumeDb: -9,
        version: 2, // 2: the volume 3 dB below the first version's
    };
}

async function openMic() {
    if (micSource || !navigator.mediaDevices) return;
    try {
        // The phone's voice processing off: noise suppression takes the wind noise for noise,
        // the gain control flattens the dynamics, the echo canceller pumps.
        // At the context's rate and the shortest buffer, where the phone allows: no resampler and
        // no longer FIFO between the microphone and the worklet.
        micStream = await navigator.mediaDevices.getUserMedia({
            audio: {
                echoCancellation: false, noiseSuppression: false, autoGainControl: false,
                channelCount: 1, sampleRate: ctx.sampleRate, latency: 0,
            },
        });
        micSource = ctx.createMediaStreamSource(micStream);
        routeMic();
        micError = null;
    } catch (err) {
        micError = err.message || String(err);
    }
}

// The microphone into the worklet by the route the settings name. Web Audio's input holds the
// samples in a buffer of its own, about 50 ms on Denis's phone at its own rate; the direct route
// reads the microphone's track on this thread (Chrome's MediaStreamTrackProcessor) and posts its
// chunks to the worklet, which runs the same filters on them. Where the direct route is missing
// or fails, Web Audio's input takes over.
function routeMic() {
    if (!node || !micSource) return;
    if (feed) feed.stop();
    feed = null;
    try {
        micSource.disconnect();
    } catch {
        // not connected yet
    }
    if (settings.route === "direct" && !("MediaStreamTrackProcessor" in window)) {
        feedError = "the direct route is not in this browser";
    } else if (settings.route === "direct") {
        feedError = null;
        try {
            feed = startFeed();
            return;
        } catch (err) {
            feedError = String(err);
        }
    }
    micSource.connect(node);
}

function startFeed() {
    const track = micStream.getAudioTracks()[0].clone();
    const reader = new MediaStreamTrackProcessor({ track }).readable.getReader();
    const f = { stop: () => {
        reader.cancel().catch(() => {});
        track.stop();
    } };
    (async () => {
        for (;;) {
            const { value: data, done } = await reader.read();
            if (done) return;
            const x = audioSamples(data);
            const rate = data.sampleRate;
            data.close();
            node.port.postMessage({ t: "micdata", x, rate }, [x.buffer]);
        }
    })().catch((err) => {
        if (feed !== f) return;
        feedError = String(err);
        feed = null;
        micSource.connect(node);
    });
    return f;
}

async function start() {
    $("start").disabled = true;
    const step = (text) => setStatus($("cover-status"), "muted", text);
    // Within the tap: the sound may only start from a gesture. At the phone's own rate by
    // default: a context forced to another rate plays through a resampler and a longer buffer;
    // the worklet resamples the synth instead.
    ctx = settings.rate === "44100"
        ? new AudioContext({ sampleRate: 44100, latencyHint: "interactive" })
        : new AudioContext({ latencyHint: "interactive" });
    const resumed = ctx.resume();
    fullscreen();
    if (typeof DeviceMotionEvent !== "undefined"
        && typeof DeviceMotionEvent.requestPermission === "function") {
        step("Asking for the motion sensors…");
        try {
            await DeviceMotionEvent.requestPermission();
        } catch {
            // refused: the bend and the vibrato stay at rest
        }
    }
    try {
        if (settings.source === "mic") {
            step("Asking for the microphone…");
            await openMic();
        }
        step("Loading the synth into the audio thread…");
        await ctx.audioWorklet.addModule("worklet.js");
        node = new AudioWorkletNode(ctx, "panflute", {
            numberOfInputs: 1,
            numberOfOutputs: 1,
            outputChannelCount: [2],
            processorOptions: {
                wasm: wasmBytes.slice(0),
                reverb: settings.reverb === "on" ? 1 : 0,
                calibration: synthCalibration(),
                volumeDb: settings.volumeDb,
            },
        });
        node.port.onmessage = onWorklet;
        node.connect(ctx.destination);
        routeMic();
        step("Starting the sound…");
        await resumed;
    } catch (err) {
        setStatus($("cover-status"), "bad", `The sound could not start: ${err.message || err}`);
        $("start").disabled = false;
        ctx = null;
        return;
    }
    keepAwake();
    $("cover").hidden = true;
    showStatus();
}

function synthCalibration() {
    const c = [...cal];
    c[CAL.depth] = settings.depth;
    return c;
}

async function fullscreen() {
    if (!matchMedia("(pointer: coarse)").matches) return; // a desktop trying the page
    try {
        await document.documentElement.requestFullscreen({ navigationUI: "hide" });
        // a recorder's hold points the phone's top down: without the lock, the screen turns
        await screen.orientation.lock("portrait-primary");
    } catch {
        // not offered (a desktop, iOS): the phone's own rotation lock does it
    }
}

async function keepAwake() {
    try {
        if (navigator.wakeLock && document.visibilityState === "visible") {
            wakeLock = await navigator.wakeLock.request("screen");
        }
    } catch {
        wakeLock = null;
    }
}

function onWorklet(e) {
    const m = e.data;
    if (m.t === "ready") {
        ready = true;
        if (sounding !== null) post({ t: "on", key: sounding });
        CONTROLS.forEach((name, id) => {
            if (name !== "breath" || !micOn()) post({ t: "ctl", id, value: values[name] });
        });
        sendMicMap();
        showStatus();
    } else if (m.t === "level") {
        meter.db = m.peak > 0 ? 20 * Math.log10(m.peak) : -Infinity;
        if (m.clipped) meter.clipUntil = performance.now() + 2000;
        if (m.state) synthState = m.state;
        if (micSource) {
            mic.db = m.micDb;
            remember(history.mic, m.micDb);
        }
        if (micOn()) values.breath = m.breath;
        if (m.bad > 0) setStatus($("side-status"), "bad", `${m.bad} non-finite samples.`);
    } else if (m.t === "pong") {
        if (pong) pong(m);
    } else if (m.t === "error") {
        setStatus($("cover-status"), "bad", `The synth failed to load: ${m.message}`);
        $("cover").hidden = false;
    }
}

// --- the motion ----------------------------------------------------------------------------

// The accelerometer with gravity: the phone's own axes, x to the screen's right, y to its top,
// z out of the screen, the reading pointing up. The angle is the long axis's below level: 0 held
// level, 90 with the top straight down. The sideways reading, in g, is the vibrato's tilt, as
// the TEControl's head tilt; the synth high-passes it.
function onMotion(e) {
    const a = e.accelerationIncludingGravity;
    if (!a || a.x === null || a.y === null || a.z === null) return;
    if (Math.hypot(a.x, a.y, a.z) < 1) return;
    const now = performance.now();
    const deg = (Math.atan2(-a.y, Math.hypot(a.x, a.z)) * 180) / Math.PI;
    const dt = sensor.last ? Math.min(100, Math.max(1, now - sensor.last)) : 16;
    sensor.last = now;
    sensor.theta = sensor.theta === null ? deg
        : sensor.theta + (1 - Math.exp(-dt / TILT_TAU_MS)) * (deg - sensor.theta);
    sensor.gx = a.x / G;
    sensor.count++;
    if (now - sensor.rateAt >= 1000) {
        sensor.rate = (sensor.count * 1000) / (now - sensor.rateAt || 1000);
        sensor.count = 0;
        sensor.rateAt = now;
    }
    remember(history.tilt, sensor.theta);

    const s = dt / 1000;
    if (bendLp.ic2 === null) Object.assign(bendLp, { ic1: 0, ic2: sensor.theta }); // at rest
    const bendTheta = svf(bendLp, sensor.theta, settings.bendHz, s).lp;
    bendX = bendValue(bendTheta);
    const b = Math.round(bendX);
    if (b !== values.bend && Math.abs(bendX - values.bend) > HYSTERESIS) control("bend", b);
    const band = svf(vib.lp, svf(vib.hp, sensor.gx, VIBRATO_LOW_HZ, s).hp, VIBRATO_HIGH_HZ, s).lp;
    const v = Math.round(VIBRATO_CENTRE + VIBRATO_PER_G * band);
    control("vibrato", Math.max(0, Math.min(127, v)));
    compress(band, s);
}

// A second-order Butterworth state-variable filter (Simper's trapezoidal form), one step of dt
// seconds: the motion readings come at an uneven pace, so the coefficients follow each step's dt.
function svfState() {
    return { ic1: 0, ic2: 0 };
}

function svf(st, x, fc, dt) {
    const g = Math.tan(Math.PI * Math.min(fc * dt, 0.45));
    const k = Math.SQRT2; // 1/Q, Q = 1/√2
    const a1 = 1 / (1 + g * (g + k));
    const a2 = g * a1;
    const a3 = g * a2;
    const v3 = x - st.ic2;
    const v1 = a1 * st.ic1 + a2 * v3;
    const v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
    st.ic1 = 2 * v1 - st.ic1;
    st.ic2 = 2 * v2 - st.ic2;
    return { lp: v2, hp: x - k * v1 - v2 };
}

// The vibrato's compressor, one motion reading of dt seconds, on the band-passed tilt: the depth
// sent to the synth is Denis's depth times the scale.
function compress(band, dt) {
    vib.peak = Math.max(Math.abs(band), vib.peak * Math.exp(-dt / PEAK_RELEASE_S));
    const wide = settings.depth * vib.peak; // cents the wobble would reach unscaled
    const target = wide > settings.ceiling ? settings.ceiling / wide : 1;
    const tau = target < vib.scale ? GAIN_ATTACK_S : GAIN_RELEASE_S;
    vib.scale += (1 - Math.exp(-dt / tau)) * (target - vib.scale);
    const depth = Math.round(settings.depth * vib.scale * 10) / 10;
    if (depth !== vib.sent) {
        vib.sent = depth;
        post({ t: "cal", item: CAL.depth, value: depth });
    }
}

// The angle to the bend's value: the rest to 0, the whole tone's angle below it to the synth's
// full marker, on past it; above the rest, 0.
function bendValue(theta) {
    const { rest, range } = settings.tilt;
    const x = ((theta - rest) / range) * cal[CAL.full];
    return Math.max(0, Math.min(127, x));
}

// The angle where the bend's value is v.
function bendAngle(v) {
    const { rest, range } = settings.tilt;
    return rest + (v / cal[CAL.full]) * range;
}

// The Rest button: the last second's angle is the rest; the whole tone stays as far below it.
function setRest() {
    const v = median(history.tilt);
    if (v === null) {
        setStatus($("side-status"), "warn", "No motion reading: the rest is unchanged.");
        return;
    }
    settings.tilt = { ...settings.tilt, rest: Math.round(v * 2) / 2 };
    saveSettings();
    setStatus($("side-status"), "muted", `Rest at ${settings.tilt.rest}°.`);
    frame.statusAt = performance.now() + 2000; // the message stays a while
}

// The round trip, PINGS beeps in turn: the median of those heard, by the route the breath takes.
async function measure() {
    const button = $("measure");
    const out = $("measured");
    if (!ready || !micSource) {
        out.textContent = "The sound and the microphone must be on.";
        return;
    }
    button.disabled = true;
    const heard = [];
    for (let i = 0; i < PINGS; i++) {
        out.textContent = `Beep ${i + 1} of ${PINGS}…`;
        const m = await new Promise((resolve) => {
            pong = resolve;
            post({ t: "ping" });
        });
        pong = null;
        if (m.ms !== null) heard.push(m.ms);
        await new Promise((r) => setTimeout(r, PING_GAP_MS));
    }
    button.disabled = false;
    if (!heard.length) {
        out.textContent = "No beep heard: turn the volume up, take the headphones off.";
        return;
    }
    roundTrip = middle(heard);
    out.textContent = `Round trip ${describeMs(heard)}, by ${routeName()}.`;
}

function routeName() {
    return feed ? "the direct route" : "Web Audio's input";
}

function middle(list) {
    const sorted = [...list].sort((a, b) => a - b);
    return sorted[sorted.length >> 1];
}

function describeMs(list) {
    return `${middle(list).toFixed(0)} ms (${list.map((x) => x.toFixed(0)).join(", ")})`;
}

// An AudioData's first channel as floats, whatever its format.
function audioSamples(data) {
    const n = data.numberOfFrames;
    try {
        const b = new Float32Array(n);
        data.copyTo(b, { planeIndex: 0, format: "f32-planar" });
        return b;
    } catch {
        // an older Chrome converts nothing: read the format it has
    }
    const planar = data.format.endsWith("planar");
    const ch = planar ? 1 : data.numberOfChannels;
    const f32 = data.format.startsWith("f32");
    const raw = f32 ? new Float32Array(n * ch) : new Int16Array(n * ch);
    data.copyTo(raw, { planeIndex: 0 });
    const b = new Float32Array(n);
    for (let i = 0; i < n; i++) b[i] = f32 ? raw[i * ch] : raw[i * ch] / 32768;
    return b;
}

function remember(list, v, keepMs = HISTORY_MS) {
    const now = performance.now();
    list.push([now, v]);
    while (list.length && list[0][0] < now - keepMs) list.shift();
}

function median(list) {
    const vs = list.map(([, v]) => v).filter(Number.isFinite).sort((a, b) => a - b);
    return vs.length ? vs[vs.length >> 1] : null;
}

// --- the keys ------------------------------------------------------------------------------

function noteName(key) {
    return `${NAMES[key % 12]}${Math.floor(key / 12) - 1}`;
}

// A piano's keyboard stood on end: the naturals full width, the sharps over their inner side.
function buildKeys() {
    const box = $("keys");
    box.replaceChildren();
    const naturals = [];
    for (let k = FIRST_KEY; k < FIRST_KEY + PIPES; k++) if (!BLACK.has(k % 12)) naturals.push(k);
    const n = naturals.length;
    const h = 100 / n;
    const top = settings.order === "top";
    naturals.forEach((k, i) => {
        const el = keyElement(k, "white", noteName(k));
        el.style.top = `${(top ? i : n - 1 - i) * h}%`;
        el.style.height = `${h}%`;
        box.append(el);
        if (BLACK.has((k + 1) % 12) && k + 1 < FIRST_KEY + PIPES) {
            const b = keyElement(k + 1, "black", NAMES[(k + 1) % 12]);
            const edge = (top ? i + 1 : n - 1 - i) * h;
            b.style.top = `${edge - 0.31 * h}%`;
            b.style.height = `${0.62 * h}%`;
            box.append(b);
        }
    });
    showKeys();
}

function keyElement(key, kind, label) {
    const el = document.createElement("div");
    el.className = `key ${kind}`;
    el.dataset.key = key;
    el.textContent = label;
    return el;
}

function keyAt(x, y) {
    const el = document.elementFromPoint(x, y);
    const k = el && el.closest("[data-key]");
    return k ? Number(k.dataset.key) : null;
}

// The lip to a key: the synth hops if the breath is held, moves at once if not.
function sound(key) {
    if (key === sounding) return;
    if (sounding !== null) post({ t: "off", key: sounding });
    sounding = key;
    post({ t: "on", key });
    $("note").textContent = noteName(key);
}

function press(id, key) {
    const first = touches.size === 0;
    touches.set(id, key);
    const at = order.indexOf(id);
    if (at >= 0) order.splice(at, 1);
    order.push(id);
    sound(key);
    if (first && !micOn()) control("breath", Math.round(cal[CAL.mf]));
    showKeys();
}

// A finger lifted: the lip goes back to the latest key still held, as a recorder's note does
// when a finger comes off; with none held it stays where it is, the breath ends the note.
function lift(id) {
    if (!touches.has(id)) return;
    touches.delete(id);
    order.splice(order.indexOf(id), 1);
    if (order.length) sound(touches.get(order[order.length - 1]));
    else if (!micOn()) control("breath", 0);
    showKeys();
}

function liftAll() {
    for (const id of [...touches.keys()]) lift(id);
}

function wireKeys() {
    const box = $("keys");
    box.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        touchDelays.push(performance.now() - e.timeStamp);
        if (touchDelays.length > TOUCH_DELAYS) touchDelays.shift();
        const key = keyAt(e.clientX, e.clientY);
        if (key === null) return;
        box.setPointerCapture(e.pointerId);
        press(e.pointerId, key);
    });
    box.addEventListener("pointermove", (e) => {
        if (!touches.has(e.pointerId)) return;
        const key = keyAt(e.clientX, e.clientY);
        if (key !== null && key !== touches.get(e.pointerId)) press(e.pointerId, key);
    });
    for (const type of ["pointerup", "pointercancel"]) {
        box.addEventListener(type, (e) => lift(e.pointerId));
    }
    box.addEventListener("contextmenu", (e) => e.preventDefault());
}

function showKeys() {
    const held = new Set(touches.values());
    for (const el of $("keys").children) {
        const k = Number(el.dataset.key);
        el.classList.toggle("held", held.has(k));
        el.classList.toggle("sounding", k === sounding);
    }
}

// --- the settings --------------------------------------------------------------------------

function loadSettings() {
    let saved = null;
    try {
        saved = JSON.parse(localStorage.getItem(STORE));
    } catch {
        saved = null;
    }
    const s = structuredClone(defaults);
    if (!saved || typeof saved !== "object") return s;
    const finite = (o, keys) => o && keys.every((k) => Number.isFinite(o[k]));
    if (["mic", "touch"].includes(saved.source)) s.source = saved.source;
    if (finite(saved.mic, ["floor", "pp", "mf", "ff"]) && micOrdered(saved.mic)) s.mic = saved.mic;
    if (Number.isFinite(saved.gain)) s.gain = Math.max(-20, Math.min(20, saved.gain));
    if (Number.isFinite(saved.breathRiseMs)) {
        s.breathRiseMs = Math.max(0, Math.min(100, saved.breathRiseMs));
    }
    if (Number.isFinite(saved.breathFallMs)) {
        s.breathFallMs = Math.max(0, Math.min(300, saved.breathFallMs));
    }
    const tilt = saved.tilt;
    if (finite(tilt, ["rest", "full"]) && !finite(tilt, ["range"])) {
        tilt.range = tilt.full - tilt.rest; // the first version's full was an angle
        delete tilt.full;
    }
    if (finite(tilt, ["rest", "range"]) && tiltOk(tilt)) s.tilt = tilt;
    if (Number.isFinite(saved.bendHz)) s.bendHz = Math.max(0.5, Math.min(10, saved.bendHz));
    if (Number.isFinite(saved.depth)) s.depth = Math.max(0, Math.min(300, saved.depth));
    if (Number.isFinite(saved.ceiling)) s.ceiling = Math.max(3, Math.min(40, saved.ceiling));
    if (["native", "44100"].includes(saved.rate)) s.rate = saved.rate;
    if (["direct", "webaudio"].includes(saved.route)) s.route = saved.route;
    if (["top", "bottom"].includes(saved.order)) s.order = saved.order;
    if (["on", "off"].includes(saved.reverb)) s.reverb = saved.reverb;
    if (Number.isFinite(saved.volumeDb)) {
        const db = saved.volumeDb - ((saved.version ?? 1) < 2 ? 3 : 0);
        s.volumeDb = Math.max(-30, Math.min(6, db));
    }
    return s;
}

function saveSettings() {
    try {
        localStorage.setItem(STORE, JSON.stringify(settings));
    } catch {
        // blocked storage: the settings last for this visit only
    }
}

function micOrdered(m) {
    return m.floor < m.pp && m.pp < m.mf && m.mf < m.ff && m.ff <= 0;
}

function tiltOk(t) {
    return t.range >= 3 && t.range <= 90 && Math.abs(t.rest) <= 90;
}

// Each marker's Set: the last second's median as the marker's value. The mic's levels are after
// the gain; the whole tone is the angle below the rest.
const MIC_MARKS = [
    { key: "floor", label: "floor (silence)" }, { key: "pp", label: "pp" },
    { key: "mf", label: "mf" }, { key: "ff", label: "ff" },
].map((m) => ({ ...m, fromNow: (v) => v + settings.gain }));
const TILT_MARKS = [
    { key: "rest", label: "rest (open)", fromNow: (v) => v },
    { key: "range", label: "whole tone, ° below rest", fromNow: (v) => v - settings.tilt.rest },
];

function sheetStatus(text) {
    $("sheet-status").textContent = text;
}

// One marker's row: its name, its value typed, and Set from the last second.
function markRows(box, marks, group, check, help, recent, unit, after) {
    for (const m of marks) {
        const label = document.createElement("span");
        label.textContent = m.label;
        const input = document.createElement("input");
        input.type = "number";
        input.step = "0.5";
        input.dataset.group = group;
        input.dataset.key = m.key;
        const put = (v) => {
            const next = { ...settings[group], [m.key]: v };
            if (!Number.isFinite(v) || !check(next)) {
                sheetStatus(help);
                return false;
            }
            settings[group] = next;
            saveSettings();
            after();
            sheetStatus(`${m.label} at ${v} ${unit}.`);
            return true;
        };
        input.addEventListener("change", () => {
            put(Number(input.value));
            showSheet();
        });
        const set = document.createElement("button");
        set.textContent = "Set";
        set.addEventListener("click", () => {
            const v = median(recent());
            if (v === null) sheetStatus("Nothing heard or felt in the last second.");
            else put(Math.round(m.fromNow(v) * 2) / 2);
            showSheet();
        });
        box.append(label, input, set);
    }
    const now = document.createElement("div");
    now.className = "now muted";
    now.id = `now-${group}`;
    box.append(now);
}

function wireSheet() {
    markRows($("marks-mic"), MIC_MARKS, "mic", micOrdered,
        "The levels go up: floor < pp < mf < ff ≤ 0 dB.", () => history.mic, "dB", sendMicMap);
    markRows($("marks-tilt"), TILT_MARKS, "tilt", tiltOk,
        "The whole tone is 3° to 90° below the rest, the rest within ±90°.", () => history.tilt,
        "°", () => {});
    $("set-rest").addEventListener("click", setRest);
    $("gain").addEventListener("input", () => {
        settings.gain = Number($("gain").value);
        showGain();
        sendMicMap();
    });
    $("gain").addEventListener("change", saveSettings);
    for (const [id, key] of [["rise", "breathRiseMs"], ["fall", "breathFallMs"]]) {
        $(id).addEventListener("input", () => {
            settings[key] = Number($(id).value);
            showSmoothing();
            sendMicMap();
        });
        $(id).addEventListener("change", saveSettings);
    }
    $("ceiling").addEventListener("input", () => {
        settings.ceiling = Number($("ceiling").value);
        $("ceiling-v").textContent = `±${settings.ceiling} c`;
    });
    $("ceiling").addEventListener("change", saveSettings);
    $("bend-hz").addEventListener("input", () => {
        settings.bendHz = Number($("bend-hz").value);
        $("bend-hz-v").textContent = `${settings.bendHz} Hz`;
    });
    $("bend-hz").addEventListener("change", saveSettings);
    $("rate").addEventListener("change", () => {
        settings.rate = $("rate").value;
        saveSettings();
    });
    $("route").addEventListener("change", () => {
        settings.route = $("route").value;
        saveSettings();
        routeMic();
        $("measured").textContent = "";
        showInfo();
    });
    $("measure").addEventListener("click", measure);
    $("open-sheet").addEventListener("click", () => {
        liftAll();
        sheetStatus("");
        showSheet();
        $("sheet").hidden = false;
    });
    $("close-sheet").addEventListener("click", () => {
        $("sheet").hidden = true;
    });
    $("source").addEventListener("change", async () => {
        settings.source = $("source").value;
        saveSettings();
        if (settings.source === "mic" && ctx) await openMic();
        if (!micOn()) control("breath", 0);
        sendMicMap();
        showSheet();
    });
    $("depth").addEventListener("input", () => {
        settings.depth = Number($("depth").value);
        $("depth-v").textContent = `${settings.depth} c/g`;
        vib.sent = null; // the compressor sends the new depth, scaled, at the next reading
    });
    $("depth").addEventListener("change", saveSettings);
    $("order").addEventListener("change", () => {
        settings.order = $("order").value;
        saveSettings();
        buildKeys();
    });
    $("reverb").addEventListener("change", () => {
        settings.reverb = $("reverb").value;
        saveSettings();
        post({ t: "reverb", on: settings.reverb === "on" ? 1 : 0 });
    });
    $("volume").addEventListener("input", () => {
        settings.volumeDb = Number($("volume").value);
        $("volume-v").textContent = `${settings.volumeDb} dB`;
        post({ t: "volume", db: settings.volumeDb });
    });
    $("volume").addEventListener("change", saveSettings);
    $("reset").addEventListener("click", () => {
        settings = structuredClone(defaults);
        saveSettings();
        post({ t: "calset", values: synthCalibration() });
        vib.sent = null;
        post({ t: "reverb", on: 1 });
        post({ t: "volume", db: settings.volumeDb });
        sendMicMap();
        buildKeys();
        showSheet();
        sheetStatus("The settings are the defaults again.");
    });
}

function showGain() {
    $("gain").value = settings.gain;
    $("gain-v").textContent = `${settings.gain > 0 ? "+" : ""}${settings.gain} dB`;
}

function showSmoothing() {
    $("rise").value = settings.breathRiseMs;
    $("rise-v").textContent = `${settings.breathRiseMs} ms`;
    $("fall").value = settings.breathFallMs;
    $("fall-v").textContent = `${settings.breathFallMs} ms`;
}

function showSheet() {
    for (const input of document.querySelectorAll(".marks input")) {
        if (document.activeElement !== input) {
            input.value = settings[input.dataset.group][input.dataset.key];
        }
    }
    $("source").value = settings.source;
    $("depth").value = settings.depth;
    $("depth-v").textContent = `${settings.depth} c/g`;
    $("ceiling").value = settings.ceiling;
    $("ceiling-v").textContent = `±${settings.ceiling} c`;
    $("bend-hz").value = settings.bendHz;
    $("bend-hz-v").textContent = `${settings.bendHz} Hz`;
    $("rate").value = settings.rate;
    $("route").value = settings.route;
    showGain();
    showSmoothing();
    $("order").value = settings.order;
    $("reverb").value = settings.reverb;
    $("volume").value = settings.volumeDb;
    $("volume-v").textContent = `${settings.volumeDb} dB`;
    showInfo();
}

// What the phone says of itself: the latency, the rates, what the microphone does.
function showInfo() {
    const lines = [];
    if (ctx) {
        const ms = (x) => `${((x || 0) * 1000).toFixed(0)} ms`;
        const resampled = ctx.sampleRate !== 44100 ? ", the synth resampled from 44.1 kHz" : "";
        lines.push(`Sound: ${ctx.sampleRate} Hz${resampled}; the browser's buffer `
            + `${ms(ctx.baseLatency)}, then ${ms(ctx.outputLatency)} to the speaker, as the `
            + "phone reports them.");
    } else {
        lines.push("Sound: not started.");
    }
    if (micStream) {
        const s = micStream.getAudioTracks()[0].getSettings();
        const ms = Number.isFinite(s.latency)
            ? `, about ${(s.latency * 1000).toFixed(0)} ms in` : "";
        const flags = ["echoCancellation", "noiseSuppression", "autoGainControl"]
            .filter((k) => s[k]).map((k) => k.replace(/[A-Z]/, (c) => ` ${c.toLowerCase()}`));
        lines.push(`Microphone: ${s.sampleRate || "?"} Hz${ms}; `
            + (flags.length ? `still on: ${flags.join(", ")}.` : "voice processing off.")
            + ` The breath by ${routeName()}${feedError ? ` (${feedError})` : ""}.`);
    } else if (micError) {
        lines.push(`Microphone: ${micError}`);
    }
    lines.push(sensor.theta === null ? "Motion: no reading (no sensor, or not allowed)."
        : `Motion: ${sensor.rate.toFixed(0)} readings a second.`);
    if (touchDelays.length) {
        lines.push(`Keys: a touch reaches the page ${describeMs(touchDelays)} after the screen `
            + "stamps it.");
    }
    $("info").textContent = lines.join(" ");
}

function setStatus(el, kind, text) {
    el.className = `status ${kind}`;
    el.textContent = text;
}

function showStatus() {
    const el = $("side-status");
    if (settings.source === "mic" && !micSource) {
        setStatus(el, "warn", `No microphone${micError ? ` (${micError})` : ""}: `
            + "a held key blows mf.");
    } else if (sensor.theta === null) {
        setStatus(el, "warn", "No motion reading yet: the bend and the vibrato are at rest.");
    } else {
        setStatus(el, "muted", "");
    }
}

// --- the gauges ----------------------------------------------------------------------------

let colors = {};
function readColors() {
    const css = getComputedStyle(document.documentElement);
    const get = (v) => css.getPropertyValue(v).trim();
    colors = {
        text: get("--text"), line: get("--line"), muted: get("--muted"), good: get("--good"),
        warn: get("--warn"), bad: get("--bad"), breath: get("--trace-breath"),
        bend: get("--trace-bend"), vibrato: get("--trace-vibrato"),
    };
}

function sizeCanvas(canvas) {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.round(canvas.clientWidth * dpr);
    const h = Math.round(canvas.clientHeight * dpr);
    if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
    }
    return { g: canvas.getContext("2d"), w, h, dpr };
}

// A bar from lo to hi: the value filled in its colour, the markers as ticks with their names.
function drawGauge(id, lo, hi, value, marks, color, from = lo) {
    const { g, w, h, dpr } = sizeCanvas($(id));
    g.clearRect(0, 0, w, h);
    const x = (v) => (Math.max(lo, Math.min(hi, v)) - lo) / (hi - lo) * w;
    if (Number.isFinite(value)) {
        g.fillStyle = color;
        g.globalAlpha = 0.45;
        const a = x(from);
        const b = x(value);
        g.fillRect(Math.min(a, b), h * 0.45, Math.abs(b - a), h * 0.55);
        g.globalAlpha = 1;
        g.fillRect(b - dpr, h * 0.45, 2 * dpr, h * 0.55);
    }
    g.font = `${10 * dpr}px system-ui, sans-serif`;
    g.textAlign = "center";
    g.textBaseline = "top";
    for (const m of marks) {
        g.strokeStyle = colors.muted;
        g.lineWidth = dpr;
        g.beginPath();
        g.moveTo(x(m.v), h * 0.4);
        g.lineTo(x(m.v), h);
        g.stroke();
        g.fillStyle = colors.muted;
        const tx = Math.max(6 * dpr, Math.min(w - 6 * dpr, x(m.v)));
        g.fillText(m.label, tx, 1 * dpr);
    }
}

function frame() {
    const s = synthState;
    const m = settings.mic;
    const level = mic.db + settings.gain; // the levels are after the gain
    drawGauge("g-breath", MIC_RANGE[0], MIC_RANGE[1], micSource ? level : NaN,
        [{ v: m.floor, label: "·" }, { v: m.pp, label: "pp" }, { v: m.mf, label: "mf" },
            { v: m.ff, label: "ff" }], colors.breath);
    const db = micSource && Number.isFinite(level) ? ` · ${level.toFixed(0)} dB` : "";
    $("t-breath").textContent = `${values.breath}${db}`;

    // the bend: from the rest down to past the whole tone, a little above the rest for the eye
    const t = settings.tilt;
    const semi = bendAngle((cal[CAL.semiLow] + cal[CAL.semiHigh]) / 2);
    drawGauge("g-bend", t.rest - 8, t.rest + t.range + 12, sensor.theta ?? NaN,
        [{ v: t.rest, label: "rest" }, { v: semi, label: "½" },
            { v: t.rest + t.range, label: "1" }], colors.bend, t.rest);
    const cents = s ? ` · ${s[STATE.bendCents].toFixed(0)} c` : "";
    $("t-bend").textContent = sensor.theta === null ? "–"
        : `${sensor.theta.toFixed(0)}° → ${values.bend}${cents}`;

    // the vibrato in cents, the ceiling marked; the compressor's scale beside
    const span = settings.ceiling * 1.6;
    drawGauge("g-vibrato", -span, span, s ? s[STATE.vibCents] : NaN,
        [{ v: -settings.ceiling, label: "−" }, { v: 0, label: "0" },
            { v: settings.ceiling, label: "+" }], colors.vibrato, 0);
    const squeeze = vib.scale < 0.98 ? ` · ×${vib.scale.toFixed(2)}` : "";
    $("t-vibrato").textContent = s ? `${s[STATE.vibCents].toFixed(1)} c${squeeze}` : "–";

    const now = performance.now();
    const color = meter.db > -3 ? colors.bad : meter.db > -12 ? colors.warn : colors.good;
    drawGauge("g-out", -60, 0, meter.db, [{ v: -12, label: "−12" }, { v: -3, label: "−3" }],
        color);
    $("clip").classList.toggle("on", now < meter.clipUntil);

    if (!$("sheet").hidden) {
        const nm = $("now-mic");
        nm.textContent = micSource ? `now ${level.toFixed(1)} dB → breath ${values.breath}`
            : "no microphone";
        $("now-tilt").textContent = sensor.theta === null ? "no motion reading"
            : `now ${sensor.theta.toFixed(1)}°, ${(sensor.theta - t.rest).toFixed(1)}° below `
                + `rest → bend ${values.bend}`;
        if (now - (frame.infoAt || 0) > 1000) {
            showInfo();
            frame.infoAt = now;
        }
    }
    if (now - (frame.statusAt || 0) > 1000 && ctx) {
        showStatus();
        frame.statusAt = now;
    }
    requestAnimationFrame(frame);
}

// --- start ---------------------------------------------------------------------------------

async function boot() {
    readColors();
    matchMedia("(prefers-color-scheme: dark)").addEventListener("change", readColors);
    if (!window.isSecureContext) {
        setStatus($("cover-status"), "bad", "The microphone and the motion sensors need a secure "
            + "page: serve it with just serve-https and open the https address it prints.");
    }
    try {
        await loadSynth();
    } catch (err) {
        setStatus($("cover-status"), "bad", `The synth could not load (${err.message || err}).`);
        return;
    }
    settings = loadSettings();
    buildKeys();
    $("note").textContent = noteName(sounding);
    wireKeys();
    wireSheet();
    showSheet();
    window.addEventListener("devicemotion", onMotion);
    document.addEventListener("visibilitychange", () => {
        if (document.hidden) liftAll();
        else if (ctx) keepAwake();
    });
    if (window.isSecureContext) setStatus($("cover-status"), "muted", "");
    $("start").disabled = false;
    $("start").addEventListener("click", start);
    requestAnimationFrame(frame);
}

boot();
