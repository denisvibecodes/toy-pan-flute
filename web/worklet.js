// The audio thread: runs the synth's WebAssembly module, one render call per 128-frame block.
// Events from the page queue up and are applied at the block's first sample. The meter reads
// the synth's dry signal, before the reverb; the volume scales what goes to the speakers, and
// the clip light is for that. On a device whose rate is not 44.1 kHz (a phone's is 48 kHz, and a
// context forced to 44.1 kHz there plays with a longer buffer), the synth is resampled here.
//
// The phone page also feeds the microphone in: blowing on it makes wind noise, loudest under a
// few hundred hertz. The breath is that noise's level, here on the audio thread so it reaches
// the synth in the block it was heard: a DC blocker, a sixth-order low-pass at MIC_LOWPASS_HZ
// (the pipes start at G4, 392 Hz, 45 dB down there, and a phone's speaker gives little under
// 300 Hz, so the flute heard back is filtered out), the mean square followed with a fast attack
// and a slower release, in dB, mapped piecewise linearly onto breath values. The map comes from
// the page ({t: "mic"}).

const METER_BLOCKS = 8; // a level report every 8 blocks, about 23 ms
const BLOCK = 128; // PF_BLOCK: the right side starts this many floats after the left
const STATE_ITEMS = 7; // panflute.h's PF_STATE_ITEMS
const SYNTH_RATE = 44100; // coefficients.h's: the synth runs at it whatever the device's rate
const MIC_BLOCKS = 2; // the mic's breath looked at every 2 blocks, 5.8 ms at 44.1 kHz
const MIC_LOWPASS_HZ = 150;
const MIC_LOWPASS_Q = [0.5176, 0.7071, 1.9319]; // a sixth-order Butterworth's three sections
const MIC_DC_HZ = 20;
const MIC_ATTACK_S = 0.005;
const MIC_RELEASE_S = 0.040;
const MIC_HYSTERESIS = 0.7; // a held breath does not flicker between two values
// The round trip's beep: 5 ms of 2 kHz, the microphone high-passed at 1 kHz to hear it over the
// rumble, its arrival the first sample over 6 times the quiet spell's peak.
const PING_HZ = 2000;
const PING_S = 0.005;
const PING_LEVEL = 0.5;
const PING_HP_HZ = 1000;
const PING_QUIET_S = 0.15;
const PING_OVER_NOISE = 6;
const PING_MIN = 0.003;
const PING_TIMEOUT_S = 1;

// 4-point Hermite interpolation in b between k and k + 1, t in [0, 1).
function hermite(b, k, t) {
    const xm = b[k - 1];
    const x0 = b[k];
    const x1 = b[k + 1];
    const x2 = b[k + 2];
    const c1 = 0.5 * (x1 - xm);
    const c2 = xm - 2.5 * x0 + 2 * x1 - 0.5 * x2;
    const c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

// A low-pass biquad (RBJ), Direct Form I.
function lowpass(fc, fs, q) {
    const w = (2 * Math.PI * fc) / fs;
    const alpha = Math.sin(w) / (2 * q);
    const a0 = 1 + alpha;
    const c = Math.cos(w);
    return {
        b0: (1 - c) / 2 / a0, b1: (1 - c) / a0, b2: (1 - c) / 2 / a0,
        a1: (-2 * c) / a0, a2: (1 - alpha) / a0, x1: 0, x2: 0, y1: 0, y2: 0,
    };
}

function micFilters(rate) {
    return MIC_LOWPASS_Q.map((q) => lowpass(MIC_LOWPASS_HZ, rate, q));
}

function biquad(f, x) {
    const y = f.b0 * x + f.b1 * f.x1 + f.b2 * f.x2 - f.a1 * f.y1 - f.a2 * f.y2;
    f.x2 = f.x1;
    f.x1 = x;
    f.y2 = f.y1;
    f.y1 = y;
    return y;
}

// The mic's level in dB to a breath value: 0 under the floor, then linear through (floor, 0),
// (pp, its value), (mf, its), (ff, its), past ff with the last segment's slope.
function micBreath(db, m) {
    if (!m || db <= m.floor) return 0;
    const xs = [m.floor, m.pp, m.mf, m.ff];
    const ys = [0, m.vpp, m.vmf, m.vff];
    let i = 0;
    while (i < 2 && db > xs[i + 1]) i++;
    const v = ys[i] + ((db - xs[i]) * (ys[i + 1] - ys[i])) / (xs[i + 1] - xs[i]);
    return Math.max(0, Math.min(127, v));
}

class PanFlute extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.queue = [];
        this.gain = Math.pow(10, (options.processorOptions.volumeDb ?? 0) / 20);
        this.peak = 0;
        this.clipped = false;
        this.bad = 0;
        this.blocks = 0;
        this.mic = null; // the map, from the page; null: the mic is not the breath
        this.micEnv = 0;
        this.micFrames = 0;
        this.micPeak = 0;
        this.micSetup(sampleRate);
        this.breath = 0;
        this.breathTarget = 0; // the mic's breath, before the smoothing
        this.breathSmooth = 0;
        this.frame = 0; // the output's frames so far: the clock of the round trip's measure
        this.ping = null;
        // the device's own rate when it is not the synth's: the synth resampled here
        if (Math.abs(sampleRate - SYNTH_RATE) > 1) {
            this.rs = {
                step: SYNTH_RATE / sampleRate, pos: 1, n: 1,
                l: new Float32Array(8 * BLOCK), r: new Float32Array(8 * BLOCK),
            };
            this.outL = new Float32Array(BLOCK);
            this.outR = new Float32Array(BLOCK);
        } else {
            this.rs = null;
        }
        try {
            const module = new WebAssembly.Module(options.processorOptions.wasm);
            const ex = new WebAssembly.Instance(module, {}).exports;
            if (ex._initialize) ex._initialize();
            ex.page_init();
            ex.page_reverb(options.processorOptions.reverb);
            this.ex = ex;
            this.calibrateAll(options.processorOptions.calibration);
        } catch (err) {
            this.port.postMessage({ t: "error", message: String(err) });
            return;
        }
        this.port.onmessage = (e) => this.queue.push(e.data);
        this.port.postMessage({ t: "ready" });
    }

    apply(m) {
        const ex = this.ex;
        if (m.t === "on") ex.pf_note_on(m.key);
        else if (m.t === "off") ex.pf_note_off(m.key);
        else if (m.t === "ctl") {
            ex.pf_control(m.id, m.value);
            if (m.id === 0) this.breath = m.value;
        }
        else if (m.t === "reverb") ex.page_reverb(m.on);
        else if (m.t === "cal" && ex.pf_calibrate) ex.pf_calibrate(m.item, m.value);
        else if (m.t === "calset") this.calibrateAll(m.values);
        else if (m.t === "volume") this.gain = Math.pow(10, m.db / 20);
        else if (m.t === "mic") {
            this.mic = m.map;
            if (!m.map) {
                this.breathTarget = 0;
                this.breathSmooth = 0;
                this.setBreath(0);
            }
        } else if (m.t === "micdata") {
            if (m.rate !== this.micRate) this.micSetup(m.rate);
            this.listen(m.x, m.x.length, true);
        } else if (m.t === "ping" && !this.ping) {
            this.ping = { phase: "quiet", until: this.frame + PING_QUIET_S * sampleRate, noise: 0 };
        }
    }

    // The round trip's beep: after a quiet spell (the room's level), a short tone, then the
    // microphone watched for it; the frames between are the speaker's and the microphone's
    // latencies together, as the breath's sound has them.
    beep(out, frames) {
        const p = this.ping;
        if (!p) return;
        if (p.phase === "quiet" && this.frame + frames >= p.until) {
            p.phase = "beep";
            p.t0 = this.frame + frames; // the next block's first frame
            p.threshold = Math.max(PING_MIN, PING_OVER_NOISE * p.noise);
            return;
        }
        if (p.phase !== "beep" && p.phase !== "listen") return;
        const n = Math.round(PING_S * sampleRate);
        const fade = Math.round(0.0005 * sampleRate);
        for (let i = 0; i < frames; i++) {
            const k = this.frame + i - p.t0;
            if (k < 0 || k >= n) continue;
            const env = Math.min(1, k / fade, (n - k) / fade);
            const x = PING_LEVEL * env * Math.sin((2 * Math.PI * PING_HZ * k) / sampleRate);
            for (const ch of out) ch[i] += x;
        }
        p.phase = "listen";
        if (this.frame - p.t0 > PING_TIMEOUT_S * sampleRate) this.pong(null);
    }

    pong(frames) {
        const p = this.ping;
        this.port.postMessage({
            t: "pong", ms: frames === null ? null : (frames * 1000) / sampleRate,
            noise: p.noise, threshold: p.threshold,
        });
        this.ping = null;
    }

    // The microphone's high-passed sample, for the beep: the room's level while quiet, the beep's
    // arrival once it has gone out.
    hearPing(x, i) {
        const h = this.pingHp;
        const y = h.a * (h.y1 + x - h.x1);
        h.x1 = x;
        h.y1 = y;
        const p = this.ping;
        if (!p) return;
        const a = Math.abs(y);
        if (p.phase === "quiet") {
            if (a > p.noise) p.noise = a;
        } else if (p.phase === "listen" && a > p.threshold) {
            this.pong(this.frame + i - p.t0);
        }
    }

    setBreath(v) {
        if (v !== this.breath) this.ex.pf_control(0, v);
        this.breath = v;
    }

    // The microphone's filters for its rate: the worklet's input runs at the context's, the
    // page's chunks at the track's.
    micSetup(rate) {
        this.micRate = rate;
        this.micDc = { x1: 0, y1: 0, r: 1 - (2 * Math.PI * MIC_DC_HZ) / rate };
        this.micLp = micFilters(rate);
        this.micAttack = 1 - Math.exp(-1 / (rate * MIC_ATTACK_S));
        this.micRelease = 1 - Math.exp(-1 / (rate * MIC_RELEASE_S));
        this.micUpdate = MIC_BLOCKS * BLOCK * (rate / sampleRate);
        this.pingHp = { x1: 0, y1: 0, a: 1 / (1 + (2 * Math.PI * PING_HP_HZ) / rate) };
    }

    // The mic's samples through the filters into the follower; every MIC_BLOCKS blocks' worth,
    // the breath. They come from the worklet's input, or as chunks the page read from the
    // microphone's track (direct): a chunk's samples all reach this thread at this block, the
    // time the beep's arrival takes.
    listen(input, frames, direct = false) {
        const dc = this.micDc;
        const [lp1, lp2, lp3] = this.micLp;
        let env = this.micEnv;
        let peak = this.micPeak;
        for (let i = 0; i < frames; i++) {
            const x = input[i];
            this.hearPing(x, direct ? 0 : i);
            const a = Math.abs(x);
            if (a > peak) peak = a;
            const d = x - dc.x1 + dc.r * dc.y1;
            dc.x1 = x;
            dc.y1 = d;
            const y = biquad(lp3, biquad(lp2, biquad(lp1, d)));
            const p = y * y;
            env += (p > env ? this.micAttack : this.micRelease) * (p - env);
        }
        if (!Number.isFinite(env)) {
            env = 0;
            this.micLp = micFilters(this.micRate);
        }
        this.micEnv = env;
        this.micPeak = peak;
        this.micFrames += frames;
        if (this.micFrames < this.micUpdate) return;
        this.micFrames = 0;
        if (this.mic) this.breathTarget = micBreath(this.micDb(), this.mic);
    }

    // The breath smoothed toward the mic's, one block: up with the rise's time constant, down
    // with the fall's, so the wind noise's dips and the chunks' uneven arrival do not chop it.
    // Run on the output's blocks, whatever the route.
    smoothBreath(frames) {
        const m = this.mic;
        const up = this.breathTarget > this.breathSmooth;
        const ms = up ? m.riseMs : m.fallMs;
        const a = ms > 0 ? 1 - Math.exp(-frames / (sampleRate * ms * 1e-3)) : 1;
        this.breathSmooth += a * (this.breathTarget - this.breathSmooth);
        const x = this.breathSmooth;
        const v = Math.round(x);
        if (v !== this.breath && Math.abs(x - this.breath) > MIC_HYSTERESIS) this.setBreath(v);
    }

    micDb() {
        return 10 * Math.log10(this.micEnv + 1e-12);
    }

    // A whole calibration, in a few passes: a marker refused for the order may be taken once
    // its neighbour has moved.
    calibrateAll(values) {
        const ex = this.ex;
        if (!ex.pf_calibrate || !Array.isArray(values)) return;
        for (let pass = 0; pass < values.length; pass++) {
            let pending = 0;
            values.forEach((v, i) => {
                if (ex.pf_calibration(i) !== v && !ex.pf_calibrate(i, v)) pending++;
            });
            if (pending === 0) break;
        }
    }

    // What the synth is doing, for the page's graphs (panflute.h's pf_state items).
    state() {
        const ex = this.ex;
        if (!ex.pf_state) return null;
        return Array.from({ length: STATE_ITEMS }, (_, i) => ex.pf_state(i));
    }

    // One block of the synth, `frames` long: the stereo pointers; the dry peak noted.
    renderSynth(frames) {
        const memory = this.ex.memory.buffer;
        const ptr = this.ex.page_render(frames);
        const dry = new Float32Array(memory, this.ex.page_dry(), frames);
        for (let i = 0; i < frames; i++) {
            const a = Math.abs(dry[i]);
            if (a > this.peak) this.peak = a;
        }
        return [new Float32Array(memory, ptr, frames),
            new Float32Array(memory, ptr + 4 * BLOCK, frames)];
    }

    // The synth at 44.1 kHz into the output at the device's rate: whole blocks rendered as the
    // interpolation needs them, a 4-point Hermite between their samples.
    resampled(left, right, frames) {
        const rs = this.rs;
        for (let i = 0; i < frames; i++) {
            let k = Math.floor(rs.pos);
            while (k + 2 >= rs.n) {
                const [l, r] = this.renderSynth(BLOCK);
                rs.l.set(l, rs.n);
                rs.r.set(r, rs.n);
                rs.n += BLOCK;
            }
            const t = rs.pos - k;
            left[i] = hermite(rs.l, k, t);
            right[i] = hermite(rs.r, k, t);
            rs.pos += rs.step;
        }
        const drop = Math.floor(rs.pos) - 1; // keep one sample before the next position
        if (drop > 0) {
            rs.l.copyWithin(0, drop, rs.n);
            rs.r.copyWithin(0, drop, rs.n);
            rs.n -= drop;
            rs.pos -= drop;
        }
    }

    process(inputs, outputs) {
        const out = outputs[0];
        if (!this.ex || out.length === 0) return true;
        for (const m of this.queue) this.apply(m);
        this.queue.length = 0;
        const mic = inputs[0] && inputs[0][0];
        if (mic) this.listen(mic, mic.length);
        if (this.mic) this.smoothBreath(out[0].length);

        const frames = out[0].length;
        let sides;
        if (this.rs) {
            sides = [this.outL, this.outR];
            this.resampled(this.outL, this.outR, frames);
        } else {
            sides = this.renderSynth(frames);
        }
        const gain = this.gain;
        for (let c = 0; c < out.length; c++) {
            const side = sides[Math.min(c, 1)];
            const dest = out[c];
            for (let i = 0; i < frames; i++) {
                const x = side[i] * gain;
                if (Number.isFinite(x)) {
                    dest[i] = x;
                    if (x >= 1 || x <= -1) this.clipped = true;
                } else {
                    dest[i] = 0;
                    this.bad++;
                }
            }
        }
        this.beep(out, frames);
        this.frame += frames;
        const peak = this.peak;

        if (++this.blocks >= METER_BLOCKS) {
            this.port.postMessage({
                t: "level", peak, clipped: this.clipped, bad: this.bad, state: this.state(),
                micDb: this.micDb(), micPeak: this.micPeak, breath: this.breath,
            });
            this.micPeak = 0;
            this.blocks = 0;
            this.peak = 0;
            this.clipped = false;
            this.bad = 0;
        }
        return true;
    }
}

registerProcessor("panflute", PanFlute);
