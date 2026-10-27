// The page: loads the synth's module, routes MIDI and the computer keyboard to it, calibrates
// the breath and the bend, shows what the controllers send, what the synth makes of it and how
// loud the output is. The synth runs in worklet.js.

const CONTROLS = ["breath", "bend", "vibrato"]; // in the order of panflute.h's pf_control
const CFG = {
    notesChannel: 0, breathCc: 1, breathChannel: 2, bendCc: 3, bendChannel: 4,
    vibratoCc: 5, vibratoChannel: 6, keyboardBase: 8,
};
// panflute.h's pf_calibration items and pf_state items
const CAL = { pp: 0, mf: 1, ff: 2, rest: 3, semiLow: 4, semiHigh: 5, full: 6, depth: 7 };
const CAL_ITEMS = 8;
const STATE = { key: 0, pa: 1, paG4: 2, bendCents: 3, vibG: 4, vibCents: 5, covering: 6 };
const STATE_ITEMS = 7;
const BREATH_PA = [384, 480, 960]; // the pressures of pp, mf, ff on G4 (config.h)
const MARKERS = {
    breath: [{ item: CAL.pp, label: "pp" }, { item: CAL.mf, label: "mf" },
        { item: CAL.ff, label: "ff" }],
    bend: [{ item: CAL.rest, label: "rest" }, { item: CAL.semiLow, label: "semitone from" },
        { item: CAL.semiHigh, label: "semitone to" }, { item: CAL.full, label: "full" }],
};
const REVERBS = ["off", "on"]; // page_reverb's 0 and 1
const ANY = "*"; // any MIDI input
const STORE = "panflute.settings.v1";
const TRACE_SECONDS = 5;
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

// The computer keyboard, by physical key: semitones above the lower row's Z.
const LOWER = ["KeyZ", "KeyS", "KeyX", "KeyD", "KeyC", "KeyV", "KeyG", "KeyB", "KeyH", "KeyN",
    "KeyJ", "KeyM", "Comma", "KeyL", "Period", "Semicolon", "Slash"];
const UPPER = ["KeyQ", "Digit2", "KeyW", "Digit3", "KeyE", "KeyR", "Digit5", "KeyT", "Digit6",
    "KeyY", "Digit7", "KeyU", "KeyI", "Digit9", "KeyO", "Digit0", "KeyP"];
const KEYMAP = new Map([...LOWER.map((c, i) => [c, i]), ...UPPER.map((c, i) => [c, 12 + i])]);

const $ = (id) => document.getElementById(id);

let wasmBytes = null;
let probe = null; // the page's own instance of the synth: the maps, the calibration's checks
let hasCal = false;
let defaults = null;
let settings = null;
let midi = null;
let ctx = null;
let node = null;
let ready = false;
let octave = 0;
const held = new Map(); // physical key -> MIDI key

// What the synth was last told, for the monitor and to bring a late-started synth up to date.
const values = { breath: 0, bend: 0, vibrato: 64 };
const traces = { breath: [], bend: [], vibrato: [] }; // [time in ms, value]
let lastKey = null;
let synthState = null; // the worklet's last pf_state report
const meter = { db: -Infinity, holdDb: -Infinity, holdAt: 0, clipUntil: 0 };

// --- the synth -----------------------------------------------------------------------------

function post(msg) {
    if (ready) node.port.postMessage(msg);
}

function noteOn(key) {
    lastKey = key;
    showNote();
    post({ t: "on", key });
}

function noteOff(key) {
    post({ t: "off", key });
}

function control(name, value) {
    values[name] = value;
    const trace = traces[name];
    const now = performance.now();
    trace.push([now, value]);
    while (trace.length > 2 && trace[1][0] < now - TRACE_SECONDS * 1000) trace.shift();
    post({ t: "ctl", id: CONTROLS.indexOf(name), value });
}

async function loadSynth() {
    const res = await fetch("panflute.wasm");
    if (!res.ok) throw new Error(`panflute.wasm: HTTP ${res.status}`);
    wasmBytes = await res.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(wasmBytes, {});
    probe = instance.exports;
    if (probe._initialize) probe._initialize();
    probe.page_init();
    hasCal = typeof probe.pf_calibrate === "function";
    const c = (item) => probe.pf_config(item);
    defaults = {
        notes: { input: ANY, channel: c(CFG.notesChannel) },
        breath: { input: ANY, cc: c(CFG.breathCc), channel: c(CFG.breathChannel) },
        bend: { input: ANY, cc: c(CFG.bendCc), channel: c(CFG.bendChannel) },
        vibrato: { input: ANY, cc: c(CFG.vibratoCc), channel: c(CFG.vibratoChannel) },
        keyboardBase: c(CFG.keyboardBase),
        reverb: "on", // Denis's choice
        volumeDb: -6, // headroom: the high pipes at ff peak near +6 dBFS dry
        // config.h's, when the module cannot say
        calibration: hasCal
            ? Array.from({ length: CAL_ITEMS }, (_, i) => probe.pf_calibration(i))
            : [4, 12, 33, 0, 30, 35, 90, 75],
    };
}

// Applies a whole calibration to an instance, whatever the order the markers move in: an item
// refused for the order may be taken once its neighbour has moved, so a few passes.
function applyCalibration(ex, cal) {
    for (let pass = 0; pass < CAL_ITEMS; pass++) {
        let pending = 0;
        cal.forEach((v, i) => {
            if (ex.pf_calibration(i) !== v && !ex.pf_calibrate(i, v)) pending++;
        });
        if (pending === 0) break;
    }
    return Array.from({ length: CAL_ITEMS }, (_, i) => ex.pf_calibration(i));
}

// One marker moved: checked on the page's instance, then sent to the worklet.
function calibrate(item, value) {
    if (!hasCal || !probe.pf_calibrate(item, value)) return false;
    settings.calibration[item] = probe.pf_calibration(item);
    saveSettings();
    post({ t: "cal", item, value: settings.calibration[item] });
    return true;
}

function setReverb(name) {
    settings.reverb = name;
    saveSettings();
    post({ t: "reverb", on: REVERBS.indexOf(name) });
}

async function startAudio() {
    if (ctx) {
        if (ctx.state === "running") await ctx.suspend();
        else await ctx.resume();
        showAudio();
        return;
    }
    try {
        ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
        ctx.onstatechange = showAudio;
        await ctx.audioWorklet.addModule("worklet.js");
        node = new AudioWorkletNode(ctx, "panflute", {
            numberOfInputs: 0,
            numberOfOutputs: 1,
            outputChannelCount: [2],
            processorOptions: {
                wasm: wasmBytes.slice(0),
                reverb: REVERBS.indexOf(settings.reverb),
                calibration: settings.calibration,
                volumeDb: settings.volumeDb,
            },
        });
        node.port.onmessage = onWorklet;
        node.connect(ctx.destination);
        if (ctx.state !== "running") await ctx.resume();
    } catch (err) {
        setStatus($("audio-status"), "bad", `The sound could not start: ${err.message || err}`);
        ctx = null;
    }
    showAudio();
}

function onWorklet(e) {
    const m = e.data;
    if (m.t === "ready") {
        ready = true;
        // the gestures made before the sound started
        if (lastKey !== null) post({ t: "on", key: lastKey });
        CONTROLS.forEach((name, id) => post({ t: "ctl", id, value: values[name] }));
        showAudio();
    } else if (m.t === "level") {
        meter.db = m.peak > 0 ? 20 * Math.log10(m.peak) : -Infinity;
        const now = performance.now();
        if (meter.db >= meter.holdDb) {
            meter.holdDb = meter.db;
            meter.holdAt = now;
        }
        if (m.clipped) meter.clipUntil = now + 2000;
        if (m.state) synthState = m.state;
        if (m.bad > 0) {
            setStatus($("audio-status"), "bad", `The synth gave ${m.bad} non-finite samples.`);
        }
    } else if (m.t === "error") {
        setStatus($("audio-status"), "bad", `The synth failed to load: ${m.message}`);
    }
}

function showAudio() {
    const button = $("start");
    const status = $("audio-status");
    if (!ctx) {
        button.textContent = "Start sound";
        if (!status.classList.contains("bad")) {
            setStatus(status, "muted", "Sound off: click anywhere on the page to start it.");
        }
        return;
    }
    const running = ctx.state === "running";
    button.textContent = running ? "Pause sound" : "Start sound";
    if (!ready) {
        setStatus(status, "muted", "Starting…");
    } else if (!running) {
        setStatus(status, "muted", "Paused.");
    } else {
        const ms = ((ctx.outputLatency || 0) + (ctx.baseLatency || 0)) * 1000;
        const rate = ctx.sampleRate === 44100 ? "44.1 kHz" : `${ctx.sampleRate} Hz (not 44.1 kHz!)`;
        setStatus(status, ctx.sampleRate === 44100 ? "ok" : "warn",
            `Sound on, ${rate}, about ${ms.toFixed(0)} ms latency.`);
    }
}

// --- MIDI ----------------------------------------------------------------------------------

function routeMatches(route, input, channel) {
    return (route.input === ANY || route.input === input.name)
        && (route.channel === 0 || route.channel === channel);
}

function onMidi(input, data) {
    const status = data[0];
    if (status >= 0xf0) return; // clock, active sensing, sysex
    const type = status & 0xf0;
    const channel = (status & 0x0f) + 1;
    showLast(input, type, channel, data);
    if (!ctx && !$("audio-status").classList.contains("bad")) {
        // a browser starts sound only after a click or a key; MIDI is neither
        setStatus($("audio-status"), "warn",
            "MIDI is arriving but the sound is off: click anywhere on the page to start it.");
    }
    const isOn = type === 0x90 && data[2] > 0;
    const isOff = type === 0x80 || (type === 0x90 && data[2] === 0);
    if ((isOn || isOff) && routeMatches(settings.notes, input, channel)) {
        if (isOn) noteOn(data[1]);
        else noteOff(data[1]);
    } else if (type === 0xb0) {
        for (const name of CONTROLS) {
            const route = settings[name];
            if (route.cc === data[1] && routeMatches(route, input, channel)) control(name, data[2]);
        }
    }
}

function showLast(input, type, channel, data) {
    let what;
    if (type === 0x90 && data[2] > 0) what = `note on ${data[1]}, velocity ${data[2]}`;
    else if (type === 0x80 || type === 0x90) what = `note off ${data[1]}`;
    else if (type === 0xb0) what = `CC ${data[1]} = ${data[2]}`;
    else if (type === 0xe0) what = `pitch bend ${(data[2] << 7) | data[1]}`;
    else if (type === 0xd0) what = `channel pressure ${data[1]}`;
    else what = [...data].map((b) => b.toString(16).padStart(2, "0")).join(" ");
    $("last").textContent = `Last MIDI message: ${input.name}, channel ${channel}, ${what}`;
}

function attachInputs() {
    for (const input of midi.inputs.values()) {
        input.onmidimessage = (e) => onMidi(input, e.data);
    }
    const names = [...midi.inputs.values()].map((i) => i.name);
    const plural = names.length > 1 ? "s" : "";
    const text = names.length
        ? `Web MIDI ready: ${names.length} input${plural} (${names.join(", ")}).`
        : "Web MIDI ready, but no MIDI input is connected. Plug one in; the list updates.";
    setStatus($("midi-status"), names.length ? "ok" : "warn", text);
    fillInputSelects(names);
}

async function startMidi() {
    const status = $("midi-status");
    if (!navigator.requestMIDIAccess) {
        setStatus(status, "bad", "This browser has no Web MIDI. Chrome and Edge have it; Firefox "
            + "asks to install a permission add-on; Safari has none. The computer keyboard still "
            + "plays.");
        return;
    }
    try {
        midi = await navigator.requestMIDIAccess({ sysex: false });
    } catch (err) {
        setStatus(status, "bad", `MIDI access was refused (${err.message || err}). In Firefox, `
            + "allow the site permission add-on; elsewhere, allow MIDI for this page in the site "
            + "settings. The computer keyboard still plays.");
        return;
    }
    midi.onstatechange = attachInputs;
    attachInputs();
}

// --- the computer keyboard -------------------------------------------------------------------

function isFormField(el) {
    return el && (el.tagName === "INPUT" || el.tagName === "SELECT" || el.tagName === "TEXTAREA");
}

function onKeyDown(e) {
    if (isFormField(e.target) || e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.code === "ArrowUp" || e.code === "ArrowDown") {
        e.preventDefault();
        octave = Math.max(-2, Math.min(2, octave + (e.code === "ArrowUp" ? 1 : -1)));
        showOctave();
        return;
    }
    const offset = KEYMAP.get(e.code);
    if (offset === undefined) return;
    e.preventDefault();
    if (e.repeat || held.has(e.code)) return;
    if (!ctx) startAudio(); // a key press may start the sound
    const key = settings.keyboardBase + 12 * octave + offset;
    const first = held.size === 0;
    held.set(e.code, key);
    noteOn(key);
    if (first) control("breath", Math.round(settings.calibration[CAL.mf]));
}

function onKeyUp(e) {
    const key = held.get(e.code);
    if (key === undefined) return;
    held.delete(e.code);
    noteOff(key);
    if (held.size === 0) control("breath", 0);
}

function releaseAll() {
    if (held.size === 0) return;
    for (const key of held.values()) noteOff(key);
    held.clear();
    control("breath", 0);
}

function showOctave() {
    const z = settings.keyboardBase + 12 * octave;
    $("kb-octave").textContent = `Z is ${noteName(z)}, Q is ${noteName(z + 12)}; the lower row `
        + `reaches ${noteName(z)}–${noteName(z + 16)}, the upper ${noteName(z + 12)}–`
        + `${noteName(z + 28)}. The pipes are G4–G6.`;
}

// --- settings ------------------------------------------------------------------------------

function loadSettings() {
    let saved = null;
    try {
        saved = JSON.parse(localStorage.getItem(STORE));
    } catch {
        saved = null;
    }
    const s = structuredClone(defaults);
    if (saved && typeof saved === "object") {
        for (const name of ["notes", ...CONTROLS]) Object.assign(s[name], saved[name] || {});
        if (REVERBS.includes(saved.reverb)) s.reverb = saved.reverb;
        if (Number.isFinite(saved.volumeDb)) {
            s.volumeDb = Math.max(-30, Math.min(6, saved.volumeDb));
        }
        const cal = saved.calibration;
        if (Array.isArray(cal) && cal.length === CAL_ITEMS && cal.every(Number.isFinite)) {
            s.calibration = hasCal ? applyCalibration(probe, cal) : cal;
        }
    }
    return s;
}

function saveSettings() {
    try {
        localStorage.setItem(STORE, JSON.stringify(settings));
    } catch {
        // private window or blocked storage: the settings last for this visit only
    }
}

function rows() {
    return [...document.querySelectorAll("tr[data-route]")];
}

function fillInputSelects(names) {
    for (const row of rows()) {
        const route = settings[row.dataset.route];
        const select = row.querySelector("select.input");
        const options = [[ANY, "Any input"], ...names.map((n) => [n, n])];
        if (route.input !== ANY && !names.includes(route.input)) {
            options.push([route.input, `${route.input} (not connected)`]);
        }
        select.replaceChildren(...options.map(([value, label]) => new Option(label, value)));
        select.value = route.input;
    }
}

function showSettings() {
    for (const row of rows()) {
        const route = settings[row.dataset.route];
        row.querySelector("select.channel").value = String(route.channel);
        const cc = row.querySelector("input.cc");
        if (cc) cc.value = route.cc;
    }
    for (const radio of document.querySelectorAll("input[name=reverb]")) {
        radio.checked = radio.value === settings.reverb;
    }
    $("volume").value = settings.volumeDb;
    $("volume-db").textContent = `${settings.volumeDb} dB`;
    fillInputSelects(midi ? [...midi.inputs.values()].map((i) => i.name) : []);
    showOctave();
    showCalibration();
}

function wireSettings() {
    for (const row of rows()) {
        const route = () => settings[row.dataset.route];
        const channel = row.querySelector("select.channel");
        channel.replaceChildren(new Option("Any", "0"),
            ...Array.from({ length: 16 }, (_, i) => new Option(String(i + 1), String(i + 1))));
        channel.addEventListener("change", () => {
            route().channel = Number(channel.value);
            saveSettings();
            channel.blur();
        });
        const input = row.querySelector("select.input");
        input.addEventListener("change", () => {
            route().input = input.value;
            saveSettings();
            input.blur();
        });
        const cc = row.querySelector("input.cc");
        if (cc) {
            cc.addEventListener("input", () => {
                const v = parseInt(cc.value, 10);
                if (v >= 0 && v <= 127) {
                    route().cc = v;
                    saveSettings();
                }
            });
            cc.addEventListener("keydown", (e) => e.key === "Enter" && cc.blur());
        }
    }
    for (const radio of document.querySelectorAll("input[name=reverb]")) {
        radio.addEventListener("change", () => {
            if (radio.checked) setReverb(radio.value);
            radio.blur();
        });
    }
    const volume = $("volume");
    volume.addEventListener("input", () => {
        settings.volumeDb = Number(volume.value);
        $("volume-db").textContent = `${settings.volumeDb} dB`;
        post({ t: "volume", db: settings.volumeDb });
    });
    volume.addEventListener("change", () => {
        saveSettings();
        volume.blur();
    });
    $("reset").addEventListener("click", () => {
        for (const name of ["notes", ...CONTROLS]) settings[name] = structuredClone(defaults[name]);
        saveSettings();
        showSettings();
    });
}

// --- the calibration -----------------------------------------------------------------------

const ORDER_HELP = "That breaks the markers' order: 0 < pp < mf < ff ≤ 127, and "
    + "rest < semitone from ≤ semitone to < full ≤ 127; the depth is 0 to 300.";

function calStatus(text) {
    $("cal-status").textContent = text;
}

// The control's median over the last second, sampled every 5 ms from its trace.
function recentMedian(name) {
    const trace = traces[name];
    const now = performance.now();
    const samples = [];
    let j = 0;
    let v = trace.length ? trace[0][1] : values[name];
    for (let t = now - 1000; t <= now; t += 5) {
        while (j < trace.length && trace[j][0] <= t) v = trace[j++][1];
        samples.push(v);
    }
    samples.sort((a, b) => a - b);
    return samples[samples.length >> 1];
}

// The graphs' axes: the breath's shows the part Denis plays in, the bend's all of it.
function breathRange() {
    return Math.max(40, Math.ceil((settings.calibration[CAL.ff] * 1.6) / 10) * 10);
}

const graphs = {}; // name -> { canvas, x(value) -> px, v(px) -> value, drag }

function wireCalibration() {
    const section = $("calibration");
    if (!hasCal) {
        section.querySelector(".cal-help").textContent = "This build of the synth has no "
            + "calibration (pf_calibrate is not exported): rebuild it with just build-wasm.";
        for (const el of section.querySelectorAll(".cal, #reset-cal")) el.hidden = true;
        return;
    }
    for (const box of section.querySelectorAll(".cal")) {
        const name = box.dataset.cal;
        const list = box.querySelector(".cal-markers");
        const items = name === "vibrato"
            ? [{ item: CAL.depth, label: "depth, cents per g" }] : MARKERS[name];
        for (const m of items) {
            const label = document.createElement("span");
            label.textContent = m.label;
            const input = document.createElement("input");
            input.type = "number";
            input.min = "0";
            input.max = m.item === CAL.depth ? "300" : "127";
            input.step = "1";
            input.dataset.item = m.item;
            input.addEventListener("change", () => {
                const v = Number(input.value);
                if (!Number.isFinite(v) || !calibrate(m.item, v)) calStatus(ORDER_HELP);
                else calStatus("");
                showCalibration();
            });
            input.addEventListener("keydown", (e) => e.key === "Enter" && input.blur());
            list.append(label, input);
            if (m.item === CAL.depth) {
                list.append(document.createElement("span"));
                continue;
            }
            const set = document.createElement("button");
            set.textContent = "Set";
            set.title = `The ${name}'s median over the last second`;
            set.addEventListener("click", () => {
                const v = recentMedian(name);
                if (calibrate(m.item, v)) calStatus(`${m.label} set to ${v}.`);
                else calStatus(`${m.label} at ${v}: ${ORDER_HELP}`);
                showCalibration();
                set.blur();
            });
            list.append(set);
        }
        const canvas = box.querySelector("canvas");
        graphs[name] = { canvas, x: null, v: null, drag: null };
        if (name !== "vibrato") wireDrag(name, canvas, MARKERS[name]);
    }
    $("reset-cal").addEventListener("click", () => {
        settings.calibration = applyCalibration(probe, defaults.calibration);
        saveSettings();
        post({ t: "calset", values: settings.calibration });
        showCalibration();
        calStatus("The calibration is config.h's again.");
    });
}

// A marker dragged along its graph's value axis.
function wireDrag(name, canvas, markers) {
    const g = graphs[name];
    const nearest = (e) => {
        if (!g.x) return null;
        const px = e.offsetX;
        let best = null;
        for (const m of markers) {
            const d = Math.abs(g.x(settings.calibration[m.item]) - px);
            if (d <= 8 && (!best || d < best.d)) best = { m, d };
        }
        return best && best.m;
    };
    canvas.addEventListener("pointerdown", (e) => {
        const m = nearest(e);
        if (!m) return;
        g.drag = m;
        canvas.setPointerCapture(e.pointerId);
        e.preventDefault();
    });
    canvas.addEventListener("pointermove", (e) => {
        if (!g.drag) {
            canvas.style.cursor = nearest(e) ? "ew-resize" : "default";
            return;
        }
        // toward the pointer, value by value, as far as the neighbouring markers allow
        const target = Math.round(g.v(e.offsetX));
        let v = settings.calibration[g.drag.item];
        const step = Math.sign(target - v);
        let moved = false;
        while (v !== target && calibrate(g.drag.item, v + step)) {
            v += step;
            moved = true;
        }
        if (moved) showCalibration();
    });
    const end = () => {
        if (g.drag) calStatus(`${g.drag.label} at ${settings.calibration[g.drag.item]}.`);
        g.drag = null;
    };
    canvas.addEventListener("pointerup", end);
    canvas.addEventListener("pointercancel", end);
}

function showCalibration() {
    for (const input of document.querySelectorAll(".cal-markers input")) {
        if (document.activeElement !== input) {
            input.value = settings.calibration[Number(input.dataset.item)];
        }
    }
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

// One transfer graph: the curve from the synth's own map, the markers, the controller's value.
function drawTransfer(name) {
    const graph = graphs[name];
    if (!graph || graph.canvas.clientWidth === 0) return;
    const { g, w, h, dpr } = sizeCanvas(graph.canvas);
    g.clearRect(0, 0, w, h);
    g.font = `${11 * dpr}px system-ui, sans-serif`;
    const left = 40 * dpr;
    const right = 8 * dpr;
    const top = 16 * dpr;
    const bottom = 16 * dpr;
    const cal = settings.calibration;

    let x0, x1, y0, y1, map, yTicks, unit;
    if (name === "breath") {
        x0 = 0;
        x1 = breathRange();
        map = (v) => probe.pf_map(0, v);
        y0 = 0;
        y1 = Math.ceil(map(x1) / 250) * 250;
        yTicks = [0, BREATH_PA[1], BREATH_PA[2]]; // mf and ff; pp's sits too close to mf's
        unit = "Pa";
    } else if (name === "bend") {
        x0 = 0;
        x1 = 127;
        map = (v) => probe.pf_map(1, v);
        const low = Math.min(map(127), map(0));
        y0 = Math.floor((low - 10) / 50) * 50;
        y1 = 25; // room above the rest's 0 for its label, under the markers' names
        yTicks = [map(0), map(cal[CAL.semiLow]), map(cal[CAL.full])].map((c) => Math.round(c));
        unit = "c";
    } else {
        const span = 0.5; // g either way: past Denis's wobble (0.2 g peak to peak) and a lean
        x0 = -span;
        x1 = span;
        map = (gv) => cal[CAL.depth] * gv;
        const c = Math.max(10, Math.ceil((cal[CAL.depth] * span) / 10) * 10);
        y0 = -c;
        y1 = c;
        yTicks = [y0, 0, y1];
        unit = "c";
    }
    const px = (v) => left + ((v - x0) / (x1 - x0)) * (w - left - right);
    const py = (y) => h - bottom - ((y - y0) / (y1 - y0)) * (h - top - bottom);
    graph.x = (v) => px(v) / dpr;
    graph.v = (cssX) => x0 + ((cssX * dpr - left) / (w - left - right)) * (x1 - x0);

    // the axes' ticks
    g.strokeStyle = colors.line;
    g.fillStyle = colors.muted;
    g.lineWidth = 1 * dpr;
    g.textAlign = "right";
    g.textBaseline = "middle";
    let lastLabelY = Infinity;
    for (const y of [...yTicks].sort((a, b) => a - b)) {
        g.beginPath();
        g.moveTo(left, py(y));
        g.lineTo(w - right, py(y));
        g.stroke();
        if (lastLabelY - py(y) >= 11 * dpr) { // a label too close to the last is left out
            g.fillText(`${y} ${unit}`, left - 4 * dpr, py(y));
            lastLabelY = py(y);
        }
    }
    g.textBaseline = "bottom";
    const xLabels = name === "vibrato" ? [x0, 0, x1] : [x0, x1];
    xLabels.forEach((v, i) => {
        g.textAlign = i === 0 ? "left" : i === xLabels.length - 1 ? "right" : "center";
        g.fillText(name === "vibrato" ? `${v} g` : String(v), px(v), h - 1 * dpr);
    });

    // the markers
    if (name !== "vibrato") {
        g.setLineDash([3 * dpr, 3 * dpr]);
        for (const m of MARKERS[name]) {
            const v = cal[m.item];
            g.strokeStyle = graph.drag === m ? colors.text : colors.muted;
            g.beginPath();
            g.moveTo(px(v), top);
            g.lineTo(px(v), h - bottom);
            g.stroke();
        }
        g.setLineDash([]);
        g.fillStyle = colors.text;
        g.textBaseline = "top";
        let lastRight = -Infinity;
        for (const m of MARKERS[name]) {
            const v = cal[m.item];
            const label = m.label === "semitone from" ? "½" : m.label === "semitone to" ? "½"
                : m.label;
            const tw = g.measureText(label).width;
            const lx = Math.max(px(v), lastRight + tw / 2 + 2 * dpr);
            g.fillText(label, lx, 2 * dpr);
            lastRight = lx + tw / 2;
        }
    }

    // the curve
    g.strokeStyle = colors[name];
    g.lineWidth = 2 * dpr;
    g.beginPath();
    const steps = 200;
    for (let i = 0; i <= steps; i++) {
        const v = x0 + ((x1 - x0) * i) / steps;
        const y = py(Math.max(y0, Math.min(y1, map(v))));
        if (i === 0) g.moveTo(px(v), y);
        else g.lineTo(px(v), y);
    }
    g.stroke();

    // the value now: the controller's on the curve (the vibrato: the synth's high-passed tilt)
    let v;
    if (name === "vibrato") v = synthState ? synthState[STATE.vibG] : 0;
    else v = values[name];
    const vc = Math.max(x0, Math.min(x1, v));
    g.strokeStyle = colors[name];
    g.globalAlpha = 0.35;
    g.lineWidth = 1 * dpr;
    g.beginPath();
    g.moveTo(px(vc), top);
    g.lineTo(px(vc), h - bottom);
    g.stroke();
    g.globalAlpha = 1;
    g.fillStyle = colors[name];
    g.beginPath();
    g.arc(px(vc), py(Math.max(y0, Math.min(y1, map(vc)))), 4.5 * dpr, 0, 2 * Math.PI);
    g.fill();

    graph.readout = readout(name, v, map);
}

function readout(name, v, map) {
    const s = synthState;
    const sounding = s && ctx && ctx.state === "running";
    if (name === "breath") {
        let text = `breath ${v} → ${map(v).toFixed(0)} Pa on G4`;
        if (sounding && s[STATE.key] >= 0) {
            text += ` · the synth: ${s[STATE.pa].toFixed(0)} Pa on ${noteName(s[STATE.key])}`;
        }
        return text;
    }
    if (name === "bend") {
        // cents re the lip at rest; no "-0.0" for a rounding just under 0
        const cents = (c) => (Math.abs(c) < 0.05 ? 0 : c).toFixed(1);
        let text = `bend ${v} → ${cents(map(v))} c`;
        if (sounding) {
            text += ` · the synth: ${cents(s[STATE.bendCents])} c, covering `
                + `${(s[STATE.covering] * 100).toFixed(1)} %`;
        }
        return text;
    }
    let text = `tilt ${values.vibrato}`;
    if (sounding) {
        text += ` · high-passed ${s[STATE.vibG].toFixed(3)} g → ${s[STATE.vibCents].toFixed(1)} c`;
    } else {
        text += " · the synth's high-passed tilt shows once the sound is on";
    }
    return text;
}

// --- the monitor ---------------------------------------------------------------------------

function noteName(key) {
    return `${NAMES[key % 12]}${Math.floor(key / 12) - 1}`;
}

function showNote() {
    $("note").textContent = noteName(lastKey);
    const outside = lastKey < 67 || lastKey > 91;
    $("note-info").textContent = `MIDI ${lastKey}${outside ? ", outside G4–G6: ignored" : ""}`;
}

function setStatus(el, kind, text) {
    el.className = `status ${kind}`;
    el.textContent = text;
}

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

function drawTrace(name, now) {
    const canvas = $(`g-${name}`);
    const { g, w, h, dpr } = sizeCanvas(canvas);
    g.clearRect(0, 0, w, h);
    const top = name === "breath" ? breathRange() : 127;
    const pad = 3 * dpr;
    const y = (v) => h - pad - (Math.min(v, top) / top) * (h - 2 * pad);
    const span = TRACE_SECONDS * 1000;
    const x = (t) => Math.max(0, ((t - (now - span)) / span) * w);
    // the markers, or the head's rest, for the eye
    const guides = name === "vibrato" ? [64]
        : MARKERS[name].map((m) => settings.calibration[m.item]);
    g.strokeStyle = colors.line;
    g.lineWidth = 1 * dpr;
    g.setLineDash([4 * dpr, 4 * dpr]);
    for (const v of guides) {
        g.beginPath();
        g.moveTo(0, y(v));
        g.lineTo(w, y(v));
        g.stroke();
    }
    g.setLineDash([]);
    const trace = traces[name];
    g.strokeStyle = colors[name];
    g.lineWidth = 1.5 * dpr;
    g.beginPath();
    let v = trace.length ? trace[0][1] : values[name];
    g.moveTo(trace.length ? x(trace[0][0]) : 0, y(v));
    for (let i = 1; i < trace.length; i++) {
        const xi = x(trace[i][0]);
        g.lineTo(xi, y(v));
        v = trace[i][1];
        g.lineTo(xi, y(v));
    }
    g.lineTo(w, y(v));
    g.stroke();
    $(`v-${name}`).textContent = values[name];
}

function drawMeter(now) {
    if (now - meter.holdAt > 1500) {
        meter.holdDb -= 0.4; // about 24 dB a second once the hold is over
        if (meter.holdDb < meter.db) meter.holdDb = meter.db;
    }
    const pos = (db) => Math.max(0, Math.min(1, (db + 60) / 60)) * 100;
    const fill = $("meter-fill");
    fill.style.width = `${pos(meter.db)}%`;
    fill.style.background = meter.db > -3 ? colors.bad : meter.db > -12 ? colors.warn : colors.good;
    $("meter-hold").style.left = `calc(${pos(meter.holdDb)}% - 2px)`;
    $("meter-db").textContent = Number.isFinite(meter.holdDb) && meter.holdDb > -90
        ? `${meter.holdDb.toFixed(1)} dBFS` : "–∞ dBFS";
    $("clip").classList.toggle("on", now < meter.clipUntil);
}

function frame() {
    const now = performance.now();
    for (const name of CONTROLS) drawTrace(name, now);
    if (hasCal) {
        for (const name of CONTROLS) {
            drawTransfer(name);
            const box = document.querySelector(`.cal[data-cal=${name}] .cal-readout`);
            if (graphs[name] && box.textContent !== graphs[name].readout) {
                box.textContent = graphs[name].readout;
            }
        }
    }
    drawMeter(now);
    requestAnimationFrame(frame);
}

// --- start ---------------------------------------------------------------------------------

async function boot() {
    readColors();
    matchMedia("(prefers-color-scheme: dark)").addEventListener("change", readColors);
    try {
        await loadSynth();
    } catch (err) {
        setStatus($("audio-status"), "bad", `The synth could not load (${err.message || err}). `
            + "Serve the page over http (just serve), not from a file.");
        return;
    }
    settings = loadSettings();
    wireSettings();
    wireCalibration();
    showSettings();
    showAudio();
    $("start").disabled = false;
    $("start").addEventListener("click", () => startAudio());
    // the first click anywhere starts the sound (the button handles its own clicks)
    window.addEventListener("pointerdown", (e) => {
        if (!ctx && !e.target.closest("#start")) startAudio();
    });
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", releaseAll);
    document.addEventListener("visibilitychange", () => document.hidden && releaseAll());
    requestAnimationFrame(frame);
    startMidi();
}

boot();
