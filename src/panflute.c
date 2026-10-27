/* panflute.c: a pan flute as a physical model, and the player's gestures on it.
 *
 * The raft is 25 stopped cylindrical pipes, G4 to G6 (MIDI keys 67 to 91), cut once to the
 * numbers of coefficients.h. Each pipe is a tube closed by a stopper, its open end under the
 * player's lower lip, and an air jet blown from the lips across the end to its far rim. All in
 * pascals, metres and seconds, at 44.1 kHz. Per pipe and sample, p- the wave returning to the
 * open end through the bore's losses, p the blowing pressure:
 *
 *   U      = sqrt(2 p / rho)                        jet velocity (Bernoulli)
 *   v      = -2 p- / (rho c)                        acoustic velocity at the open end, inward
 *   eta0   = h v / U  +  K_t h n(t)                 the jet's receptivity, plus its turbulence
 *   eta    = delay_tau( bandpass_U( eta0 ) )        growth and convection, tau = W / (gamma U)
 *   Q_j    = g b H U [1 + tanh((eta - y0) / b)]     the jet's flow into the pipe, g its share
 *   dp_jd  = +(rho delta_d / S_m) dQ_j/dt           the jet-drive pressure
 *   dp_los = -(rho / 2) (w / alpha_vc)^2 sign(w)    vortex shedding at the rim
 *   p+     = R(p-) + dp_jd + dp_los                 into the bore
 *   out    = (rho / (4 pi r)) dQ_m/dt, Q_m = S_m w  the radiated pressure at r = 1 m
 *
 * - The bore: one delay line for the round trip, its fraction a first-order Thiran all-pass
 *   exact at the first mode, the stopper's +1, and one loss filter, a scalar and a unity-DC
 *   one-pole exact at f1 and 3 f1, fitted to the wall's viscothermal loss and the open end's
 *   radiation, |R(f)| = exp(-2 beta sqrt(f) L / a) (1 - 2 alpha_r (ka)^2) (Fletcher & Rossing
 *   eq. 8.15; Magron 2013).
 * - The open end: -1 through an all-pass whose delay is the end correction's round trip. The
 *   lip covers a share l = r + gamma (1 - r) of the diameter, gamma the covering (the bend):
 *   the end correction is that of an orifice flanged as the lip covers it, c(l) a / sqrt(s),
 *   c(l) = 0.61 + 0.24 l, s the open share of the end, plus the channel under the lip,
 *   t gamma / s (Fletcher & Rossing eq. 8.32-8.33). The covering raises the vortex loss as
 *   (s(r) / s(l))^2 and the radiation toward the flanged end's, twice the unflanged.
 * - The jet: the receptivity h v / U (de la Cuadra 2005; Auvray 2012 eq. 9), a second-order
 *   band-pass of peak exp(beta_j W / h) at St_h U / h (Auvray 2016 eq. 10-11; de la Cuadra
 *   2008), the convection delay W / (gamma U) with the wave slowing as (U / U_mf)^(-s), moving
 *   by at most half a sample per sample, and the tanh flow of a Bickley jet of half-width
 *   b = 2h/5 on a labium offset y0 (Verge 1997 II eq. 8-9; Auvray 2012 eq. 12-14). The
 *   turbulence n is white noise shared by the raft.
 * - The jet-drive's spacing delta_d = (4 / pi) sqrt(2 h W) (Auvray 2012 eq. 13). The vortex
 *   loss (Verge 1997 I eq. 8; Auvray 2014 eq. 2) is solved implicitly on the velocity w the end
 *   lets through, so the end's reflection never exceeds 1 in magnitude, and the microphone
 *   hears a monopole fed by that same velocity: a covered end radiates as little as it passes.
 *
 * Nothing is added for the attack or the release: the tone starts when the jet's flow rises
 * and dies when it stops. A pipe whose jet share times the pressure is 0 rings at its own
 * losses, and sleeps (its state zeroed, its cost gone) once it is under -120 dB re 1 Pa; so a
 * raft of 25 costs the two or three pipes that sound.
 *
 * The gestures set ramps that the pipes read per sample, at the first sample of the next block:
 * the breath through config.h's anchors to pascals, times the lip's pipe's mf ratio; the bend
 * through its markers to cents, the vibrato's high-passed cents added, and the cents to the lip's
 * pipe's own covering through the bend's estimate on the loop's length; the keys hand the jet
 * from pipe to pipe, at once with no breath, else as a hop: the lip lifted, then landed.
 *
 * The shaping: a DC block at 20 Hz (a microphone's coupling), a brightness curve fitted to
 * recordings of pan flutes as a cascade of biquads, and the output gain.
 *
 * Plain C99: static state, no allocation, no I/O. */

#include <math.h>
#include <stdint.h>

#include "panflute.h"
#include "config.h"
#include "coefficients.h"

#define PI_F 3.14159265f
#define TAU_F 6.28318531f
#define SR 44100.0f

/* The bore's line, samples: the round trip of the longest pipe, G4, is 52. A power of 2. */
#define BORE_SLOTS 64
/* The jet's convection line, samples: the longest transit, on G4 with the breath at its floor,
 * is 154 (the harness checks it). A power of 2. */
#define JET_SLOTS 256

/* Under this pressure the divisions by U hold U fixed, Pa: a numerical floor, far under any
 * pressure a pipe speaks at. At p = 0 the flow is still 0, through U itself. */
#define P_FLOOR 1.0f
/* The loss's upper fit point, a share of the rate: 3 f1 is held under it on the short pipes. */
#define FIT_CAP 0.45f
/* The vibrato's high-pass, second-order Butterworth: its corner, Hz, passes Denis's wobble
 * (0.7 to 2.8 Hz) and drops the head's wander and a held lean; its cents are rounded to a step
 * under the sensor's own, so a still head moves nothing. */
#define VIBRATO_CORNER_HZ 0.5f
#define VIBRATO_STEP_CENTS 0.1f
/* The turbulence's noise source: a constant, each draw a hash of it and the sample. */
#define TURBULENCE 0xC0FFEEu
#define GOLDEN UINT64_C(0x9E3779B97F4A7C15)

/* The SplitMix64 finalizer (Steele, Lea & Flood 2014): a bijective mix of 64 bits. */
static uint64_t splitmix(uint64_t x)
{
    uint64_t z = x + GOLDEN;
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* Ramps ========================================================================================
 *
 * A control the model reads per sample: from v0 to target over n samples (the target from the
 * n-th on, exactly), then, when then_n > 0, on to `then` over then_n more. A flat is n = 0. */

typedef struct {
    float v0, step, target, then;
    int k, n, then_n;
} Ramp;

static void ramp_flat(Ramp *r, float v)
{
    r->v0 = r->target = v;
    r->step = 0.0f;
    r->k = r->n = r->then_n = 0;
}

/* The value at the next sample to render. */
static float ramp_value(const Ramp *r)
{
    return r->k < r->n ? r->v0 + r->step * (float)r->k : r->target;
}

/* From where it is to `target` over `n` samples (at least 1). */
static void ramp_to(Ramp *r, float target, int n)
{
    float v = ramp_value(r);
    if (n < 1) {
        n = 1;
    }
    r->v0 = v;
    r->target = target;
    r->step = (target - v) / (float)n;
    r->k = 0;
    r->n = n;
    r->then_n = 0;
}

/* Held where it is for `n` samples, then `target` in one step. */
static void ramp_hold(Ramp *r, int n, float target)
{
    r->v0 = ramp_value(r);
    r->step = 0.0f;
    r->target = target;
    r->k = 0;
    r->n = n;
    r->then_n = 0;
}

/* Then on to `then` over `n` samples, once the segment running has landed. */
static void ramp_then(Ramp *r, float then, int n)
{
    r->then = then;
    r->then_n = n < 1 ? 1 : n;
}

static int ramp_running(const Ramp *r)
{
    return r->k < r->n || r->then_n > 0;
}

/* Into the second segment, at its first sample. */
static void ramp_next(Ramp *r)
{
    r->k -= r->n;
    r->v0 = r->target;
    r->target = r->then;
    r->n = r->then_n;
    r->step = (r->target - r->v0) / (float)r->n;
    r->then_n = 0;
}

/* The value `m` samples after the next. */
static float ramp_ahead(const Ramp *r, int m)
{
    int k = r->k + m;
    if (k < r->n) {
        return r->v0 + r->step * (float)k;
    }
    if (r->then_n > 0) {
        k -= r->n;
        if (k < r->then_n) {
            return r->target + (r->then - r->target) / (float)r->then_n * (float)k;
        }
        return r->then;
    }
    return r->target;
}

static void ramp_skip(Ramp *r, int m)
{
    if (r->k >= r->n && r->then_n == 0) {
        return;
    }
    r->k += m;
    if (r->then_n > 0 && r->k >= r->n) {
        ramp_next(r);
    }
    if (r->k > r->n) {
        r->k = r->n;
    }
}

/* The next sample's value, the ramp advanced. */
static float ramp_tick(Ramp *r)
{
    float v;
    if (r->k >= r->n) {
        if (r->then_n == 0) {
            return r->target;
        }
        ramp_next(r);
    }
    v = r->v0 + r->step * (float)r->k;
    r->k++;
    return v;
}

/* The pipes ==================================================================================== */

typedef struct {
    /* the bore: the line's integer part (n samples), the Thiran's coefficient and states, the
     * loss's one-pole and scalar and state; the scalar as fitted and the radiation's share of
     * the loss at f1, 2 alpha_r (k1 a)^2 */
    float line[BORE_SLOTS];
    int write, n;
    float th, th_x1, th_y1;
    float pole, gain, lp_y, gain_fit, radiated;
    /* the open end: the bore's radius, the end correction uncovered (m), the covering the
     * all-pass holds, its coefficient and state */
    float radius, end_open, gamma, ap, ap_w1;
    /* the jet: its length W, the flue height h, the half-width b, the width H, the offset y0,
     * the band's damping 1 / Q and peak gain over it, the mf velocity the wave's slowing is
     * referenced at, its speed and the band's coefficients at u_at, the band's states */
    float jet_m, flue, half, width, offset, damping, growth, u_mf;
    float u_at, speed, a1, a2, a3, ic1, ic2;
    /* the convection line, the transit it reads at (0: unset, the target taken at once) */
    float jet[JET_SLOTS];
    int jet_write;
    float tau;
    /* the loop: the jet-drive per unit of flow difference, rho delta_d / S_m sr; the vortex
     * loss per v |v| at rest and now, the latter as 4 K / (rho c); the radiated pressure per
     * unit of the radiating wave's difference, -S_m sr / (2 pi r c); the held wave's scale for
     * the sleep, S_m f1 / (r c); the last flow and the last radiating wave */
    float drive, vortex_rest, vortex4, radiate, held, q, radiating;
    /* the bend's estimate: the loop's length less the end correction (m) */
    float loop_m;
    /* the largest output magnitude over the block; awake or asleep */
    float peak;
    int awake;
    /* what the gestures give it: the jet's share, the covering; and its mf over G4's */
    Ramp share, cover;
    float ratio;
} Pipe;

static Pipe pipes[PF_PIPES];
/* the open share of the end with the lip at rest, s(r) */
static float open_rest;

/* The open share of the end when the lip covers the share `l` of its diameter from the rim:
 * the circle's segment beyond the chord, s = (acos u - u sqrt(1 - u^2)) / pi, u = 2l - 1. */
static float open_share(float l)
{
    float u = 2.0f * l - 1.0f;
    u = u < -1.0f ? -1.0f : u > 1.0f ? 1.0f : u;
    return (acosf(u) - u * sqrtf(fmaxf(1.0f - u * u, 0.0f))) / PI_F;
}

static float clamp_covering(float gamma)
{
    return gamma < 0.0f ? 0.0f : gamma > PF_COVERING_MAX ? PF_COVERING_MAX : gamma;
}

/* The end correction of a pipe of radius `a` at the covering `g` (clamped), the lip over the
 * share l of the diameter leaving the open share s, m. */
static float end_correction_at(float a, float g, float l, float s)
{
    float c = PF_END_CORRECTION + (PF_FLANGED_CORRECTION - PF_END_CORRECTION) * l;
    return c * a / sqrtf(s) + 1e-3f * PF_LIP_THICKNESS_MM * g / s;
}

/* The same at the covering `gamma` (clamped), m. */
static float end_correction(float a, float gamma)
{
    float g = clamp_covering(gamma);
    float l = PF_LIP_OVERLAP + g * (1.0f - PF_LIP_OVERLAP);
    return end_correction_at(a, g, l, open_share(l));
}

/* The first-order all-pass (c + z^-1) / (1 + c z^-1) of DC delay d samples. */
static float all_pass_coefficient(float d)
{
    return (1.0f - d) / (1.0f + d);
}

/* Its phase delay at w (rad/sample), samples: 1 - 2 atan(c sin w / (1 + c cos w)) / w, the
 * arctangent of an angle within a quarter turn taken as pi/2 - acos of its sine. */
static float all_pass_delay(float c, float w)
{
    float re = 1.0f + c * cosf(w), im = c * sinf(w);
    float angle = 0.5f * PI_F - acosf(im / sqrtf(re * re + im * im));
    return 1.0f - 2.0f * angle / w;
}

/* The bore's loss magnitude at f (Hz): the wall's over the length, the radiation's. */
static float loss_magnitude(float f, float length, float a)
{
    float alpha = PF_WALL_LOSS * sqrtf(f) / a;
    float ka = TAU_F * f / PF_C_AIR * a;
    return expf(-2.0f * alpha * length) * (1.0f - 2.0f * PF_RADIATION_LOSS * ka * ka);
}

/* |(1 - p) / (1 - p e^-jw)|, the unity-DC one-pole's magnitude. */
static float one_pole_magnitude(float p, float w)
{
    float re = 1.0f - p * cosf(w), im = p * sinf(w);
    return (1.0f - p) / sqrtf(re * re + im * im);
}

/* The pipe of the table's row `t`, silent and asleep. */
static void pipe_build(Pipe *p, const PfPipe *t)
{
    float a = t->radius_m, L = t->length_m;
    float end = end_correction(a, 0.0f);
    float f1 = PF_C_AIR / (4.0f * (L + end));
    float f2 = fminf(3.0f * f1, FIT_CAP * SR);
    float delay = fmaxf(2.0f * L * SR / PF_C_AIR, 1.5f);
    float w1 = TAU_F * f1 / SR, w2 = TAU_F * f2 / SR;
    float lo = 0.3f, hi = 1.7f, m1, m2, r2, ka, area;
    int i;

    /* the line's integer part, and the Thiran whose phase delay at f1 is the fraction:
     * bisection on its DC delay, the phase delay rising with it */
    p->n = (int)floorf(delay - 0.5f);
    for (i = 0; i < 40; i++) {
        float mid = 0.5f * (lo + hi);
        if (all_pass_delay(all_pass_coefficient(mid), w1) < delay - (float)p->n) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    p->th = all_pass_coefficient(0.5f * (lo + hi));

    /* the loss: the one-pole whose magnitudes at f1 and f2 stand as the loss's, in closed form
     * (1 - 2p cos w1 + p^2) = r^2 (1 - 2p cos w2 + p^2), the root inside the circle; the
     * scalar exact at f1 */
    m1 = loss_magnitude(f1, L, a);
    m2 = loss_magnitude(f2, L, a);
    r2 = fminf(m2 / m1, 1.0f);
    r2 *= r2;
    if (r2 >= 1.0f - 1e-7f) {
        p->pole = 0.0f;
    } else {
        float b = (cosf(w1) - r2 * cosf(w2)) / (1.0f - r2);
        float pole = b - sqrtf(fmaxf(b * b - 1.0f, 0.0f));
        p->pole = pole < 0.0f ? 0.0f : pole > 0.9995f ? 0.9995f : pole;
    }
    p->gain_fit = p->gain = m1 / one_pole_magnitude(p->pole, w1);
    ka = TAU_F * f1 / PF_C_AIR * a;
    p->radiated = 2.0f * PF_RADIATION_LOSS * ka * ka;

    /* the open end, uncovered */
    p->radius = a;
    p->end_open = end;
    p->gamma = 0.0f;
    p->ap = all_pass_coefficient(2.0f * SR / PF_C_AIR * end);

    /* the jet */
    p->jet_m = t->jet_m;
    p->flue = t->flue_m;
    p->half = 0.4f * t->flue_m;
    p->width = PF_JET_WIDTH * a;
    p->offset = PF_JET_OFFSET * p->half;
    p->damping = 1.0f / PF_JET_BAND_Q;
    p->growth = expf(PF_JET_GROWTH * t->jet_m / t->flue_m) * p->damping;
    p->u_mf = sqrtf(2.0f * t->mf_pa / PF_RHO_AIR);

    /* the loop */
    area = PI_F * a * a;
    p->drive = PF_RHO_AIR * (4.0f / PI_F * sqrtf(2.0f * t->flue_m * t->jet_m)) / area * SR;
    p->vortex_rest = PF_RHO_AIR / (2.0f * PF_VENA_CONTRACTA * PF_VENA_CONTRACTA);
    p->vortex4 = 4.0f * p->vortex_rest / (PF_RHO_AIR * PF_C_AIR);
    p->radiate = -area * SR / (2.0f * PI_F * PF_MIC_M * PF_C_AIR);
    p->held = area * f1 / (PF_MIC_M * PF_C_AIR);
    p->loop_m = t->loop_m;
    p->ratio = t->mf_pa / PF_PIPE_TABLE[0].mf_pa;
}

/* Silence: every state to 0, the constants kept. */
static void pipe_clear(Pipe *p)
{
    int i;
    for (i = 0; i < BORE_SLOTS; i++) {
        p->line[i] = 0.0f;
    }
    for (i = 0; i < JET_SLOTS; i++) {
        p->jet[i] = 0.0f;
    }
    p->write = p->jet_write = 0;
    p->th_x1 = p->th_y1 = p->lp_y = p->ap_w1 = 0.0f;
    p->ic1 = p->ic2 = p->tau = 0.0f;
    p->q = p->radiating = p->peak = 0.0f;
}

static int pipe_finite(const Pipe *p)
{
    int i;
    for (i = 0; i < BORE_SLOTS; i++) {
        if (!isfinite(p->line[i])) {
            return 0;
        }
    }
    for (i = 0; i < JET_SLOTS; i++) {
        if (!isfinite(p->jet[i])) {
            return 0;
        }
    }
    return isfinite(p->th_x1) && isfinite(p->th_y1) && isfinite(p->lp_y) && isfinite(p->ap_w1)
        && isfinite(p->ic1) && isfinite(p->ic2) && isfinite(p->q) && isfinite(p->radiating);
}

/* The largest magnitude of the wave the bore holds, Pa. */
static float bore_peak(const Pipe *p)
{
    float m = fmaxf(fabsf(p->th_x1), fmaxf(fabsf(p->th_y1), fabsf(p->lp_y)));
    int i;
    for (i = 1; i <= p->n + 1; i++) {
        m = fmaxf(m, fabsf(p->line[(p->write - i) & (BORE_SLOTS - 1)]));
    }
    return m;
}

/* The lip moved to the covering `g` (clamped): the end's all-pass, the vortex loss on the
 * opening left and the radiation toward the flanged end's, each over its value at rest. */
static void pipe_cover(Pipe *p, float g)
{
    float l = PF_LIP_OVERLAP + g * (1.0f - PF_LIP_OVERLAP);
    float s = open_share(l);
    float v = open_rest / s;
    p->gamma = g;
    p->ap = all_pass_coefficient(2.0f * SR / PF_C_AIR * end_correction_at(p->radius, g, l, s));
    p->vortex4 = 4.0f * p->vortex_rest * v * v / (PF_RHO_AIR * PF_C_AIR);
    p->gain = p->gain_fit * (1.0f - (1.0f + g) * p->radiated) / (1.0f - p->radiated);
}

/* The jet's band and the wave's speed at the velocity `u`: g = tan(pi f_c / sr), f_c = St_h U / h
 * held under 0.45 of the rate; the speed gamma U (U / U_mf)^(-s). */
static void jet_tune(Pipe *p, float u)
{
    float fc = fminf(PF_JET_BAND_STROUHAL * u / p->flue, 0.45f * SR);
    float g = tanf(PI_F * fc / SR);
    p->a1 = 1.0f / (1.0f + g * (g + p->damping));
    p->a2 = g * p->a1;
    p->a3 = g * p->a2;
    p->speed = PF_JET_CONVECTION * u;
    if (PF_JET_SLOWING != 0.0f) {
        p->speed *= powf(u / p->u_mf, -PF_JET_SLOWING);
    }
    p->u_at = u;
}

/* The jet's flow into the mouth at the deflection `eta`, the jet whole on it, m^3/s. */
static float jet_flow(const Pipe *p, float u, float eta)
{
    return p->half * p->width * u * (1.0f + tanhf((eta - p->offset) / p->half));
}

/* One sample of the loop: u, uf the jet velocity and its floored value, draw the turbulence,
 * g the jet's share, c the covering. Returns the radiated pressure at the microphone, Pa. */
static float pipe_tick(Pipe *p, float u, float uf, float draw, float g, float c)
{
    float p_minus, v, eta0, v1, v2, v3, target, read, frac, eta, q, jet_drive;
    float w0, reflected, vel, r, vortex, x, y;
    int ir, i0;

    c = clamp_covering(c);
    if (c != p->gamma) {
        pipe_cover(p, c);
    }

    /* the bore: p- through the loss, the Thiran's output of the sample before */
    p->lp_y = (1.0f - p->pole) * p->th_y1 + p->pole * p->lp_y;
    p_minus = p->gain * p->lp_y;
    v = -2.0f / (PF_RHO_AIR * PF_C_AIR) * p_minus;

    /* the jet: receptivity and turbulence, the band-pass (state-variable, its band output 1 at
     * the centre), the growth, the convection along the line, linearly interpolated */
    eta0 = p->flue * (v / uf + PF_JET_TURBULENCE * draw);
    if (uf != p->u_at) {
        jet_tune(p, uf);
    }
    v3 = eta0 - p->ic2;
    v1 = p->a1 * p->ic1 + p->a2 * v3;
    v2 = p->ic2 + p->a2 * p->ic1 + p->a3 * v3;
    p->ic1 = 2.0f * v1 - p->ic1;
    p->ic2 = 2.0f * v2 - p->ic2;
    target = fminf(p->jet_m / p->speed * SR, SR / 40.0f);
    if (p->tau == 0.0f) {
        p->tau = target;
    } else {
        float d = target - p->tau;
        p->tau += d < -PF_JET_SLEW ? -PF_JET_SLEW : d > PF_JET_SLEW ? PF_JET_SLEW : d;
    }
    p->jet[p->jet_write] = p->growth * v1;
    read = (float)p->jet_write - fminf(fmaxf(p->tau, 1.0f), (float)(JET_SLOTS - 2));
    if (read < 0.0f) {
        read += (float)JET_SLOTS;
    }
    ir = (int)read;
    frac = read - (float)ir;
    i0 = ir & (JET_SLOTS - 1);
    eta = p->jet[i0] + frac * (p->jet[(i0 + 1) & (JET_SLOTS - 1)] - p->jet[i0]);
    p->jet_write = (p->jet_write + 1) & (JET_SLOTS - 1);

    /* the flow, its difference the jet-drive; a pipe the jet is off gets none */
    q = g > 0.0f ? g * jet_flow(p, u, eta) : 0.0f;
    jet_drive = p->drive * (q - p->q);
    p->q = q;

    /* the open end's all-pass on p-; the vortex loss on the velocity w the end lets through:
     * without it u = -(p- + AP(p-)) / rho c, with it w = 2u / (1 + r),
     * r = sqrt(1 + 4 K |u| / rho c), the loss -rho c u (r - 1) / (r + 1), the reflection a
     * blend of the open end's and a closed end's */
    w0 = p_minus - p->ap * p->ap_w1;
    reflected = p->ap * w0 + p->ap_w1;
    p->ap_w1 = w0;
    vel = -1.0f / (PF_RHO_AIR * PF_C_AIR) * (p_minus + reflected);
    r = sqrtf(1.0f + p->vortex4 * fabsf(vel));
    vortex = -(PF_RHO_AIR * PF_C_AIR) * vel * (r - 1.0f) / (r + 1.0f);

    /* into the bore: the line, then the Thiran on the sample written n ticks ago */
    p->line[p->write] = jet_drive + vortex - reflected;
    p->write = (p->write + 1) & (BORE_SLOTS - 1);
    x = p->line[(p->write - 1 - p->n) & (BORE_SLOTS - 1)];
    y = p->th * x + p->th_x1 - p->th * p->th_y1;
    p->th_x1 = x;
    p->th_y1 = y;

    /* the radiated pressure: the end's own velocity on the open end's scale, differenced */
    x = (p_minus + reflected) / (1.0f + r);
    y = p->radiate * (x - p->radiating);
    p->radiating = x;
    p->peak = fmaxf(p->peak, fabsf(y));
    return y;
}

/* The bend's estimate: the cents the covering `gamma` bends the pipe by, re uncovered, a quarter
 * wave on the loop's length with the lip's end correction, 1200 log2((L + D(0)) / (L + D(gamma))),
 * softened by the jet's pull as c / sqrt(1 + kappa (c / 200)^2). */
static float bend_cents(const Pipe *p, float gamma)
{
    float c = 1200.0f * log2f((p->loop_m + p->end_open)
        / (p->loop_m + end_correction(p->radius, gamma)));
    return c / sqrtf(1.0f + PF_BEND_PULL * (c / 200.0f) * (c / 200.0f));
}

/* The covering that bends the pipe by `cents` (0 or below): the estimate inverted by bisection,
 * the bend falling with the covering; 0 at and above 0 c, the cap past the cap's bend. */
static float covering_for(const Pipe *p, float cents)
{
    float lo = 0.0f, hi = PF_COVERING_MAX;
    int i;
    if (cents >= 0.0f) {
        return 0.0f;
    }
    if (bend_cents(p, hi) >= cents) {
        return hi;
    }
    for (i = 0; i < 32; i++) {
        float mid = 0.5f * (lo + hi);
        if (bend_cents(p, mid) > cents) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5f * (lo + hi);
}

/* The state ==================================================================================== */

static float out[PF_BLOCK];

/* the pressure on G4's map (Pa) and the mf ratio of the pipe under the lip: their product is
 * the blowing pressure */
static Ramp breath, ratio;
/* the lip's pipe, the last key struck (-1 before any) */
static int lip;
/* the controls' last values, and the breath's and the bend's at the last block's start (a step
 * is measured from there, and a key is under a held breath when both are blowing) */
static int breath_in, bend_in, breath_was, bend_was;
/* the vibrato: the tilt (g) and whether the head spoke, the high-pass's coefficients and
 * state, its cents; the samples to the grid's next point, and the grid's period */
static float tilt, hp_b0, hp_b1, hp_b2, hp_a1, hp_a2, hp_s1, hp_s2, wobble;
static int tilted, grid_left, grid;
/* the lines: a small step's (the sensor's period), a large step's, the hop's lift and attack */
static int line_large, lift, attack;
/* the raft awake, and its samples since it woke (the turbulence's counter); the sleep floor */
static int raft_awake;
static uint64_t since_wake;
static float floor_pa;
/* the turbulence's key, SplitMix64 of its source */
static uint64_t noise_key;
/* the calibration (pf_calibrate), config.h's until changed */
static float cal[PF_CAL_ITEMS];
/* what pf_state shows, at the last block's end; the covering its bend's cents were read at */
static float shown[PF_STATE_ITEMS], shown_covering = -1.0f;
/* the shaping: the DC block's states, the cascade's */
static float dc_x1, dc_y1, eq_s1[PF_EQ_SECTIONS], eq_s2[PF_EQ_SECTIONS];

/* The gestures' maps =========================================================================== */

/* The breath value to the pressure on G4, Pa: through (0, 0) and the three levels, beyond ff
 * with the last segment's slope. */
static float breath_pa(float value)
{
    float x0 = cal[PF_CAL_BREATH_MF], y0 = PF_BREATH_MF_PA;
    float x1 = cal[PF_CAL_BREATH_FF], y1 = PF_BREATH_FF_PA;
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value <= cal[PF_CAL_BREATH_PP]) {
        x0 = 0.0f;
        y0 = 0.0f;
        x1 = cal[PF_CAL_BREATH_PP];
        y1 = PF_BREATH_PP_PA;
    } else if (value <= cal[PF_CAL_BREATH_MF]) {
        x0 = cal[PF_CAL_BREATH_PP];
        y0 = PF_BREATH_PP_PA;
        x1 = cal[PF_CAL_BREATH_MF];
        y1 = PF_BREATH_MF_PA;
    }
    return y0 + (y1 - y0) * (value - x0) / (x1 - x0);
}

/* The share of the way from `from` to `to` that x has come, 0 to 1; a step at `to` when the two
 * coincide. */
static float along(float x, float from, float to)
{
    if (to <= from) {
        return x >= to ? 1.0f : 0.0f;
    }
    x = (x - from) / (to - from);
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

/* The bend value to cents re the pipe uncovered (config.h's markers). */
static float bend_value_cents(float value)
{
    float half = 0.5f * PF_BEND_REACH_CENTS;
    return PF_BEND_REST_CENTS
        + half * along(value, cal[PF_CAL_BEND_REST], cal[PF_CAL_BEND_SEMITONE_LOW])
        + half * along(value, cal[PF_CAL_BEND_SEMITONE_HIGH], cal[PF_CAL_BEND_FULL]);
}

/* The lip's pipe's covering for the bend and the wobble now; the last answer kept, the
 * bisection being the costly part. */
static float lip_covering(void)
{
    static int key = -1;
    static float cents, covering;
    float c = bend_value_cents((float)bend_in) + wobble;
    if (lip != key || c != cents) {
        key = lip;
        cents = c;
        covering = covering_for(&pipes[lip], c);
    }
    return covering;
}

static int samples(float ms)
{
    int n = (int)floorf(ms * SR / 1000.0f + 0.5f);
    return n < 1 ? 1 : n;
}

/* A line's length for a step of `delta` values (config.h's lines). */
static int line_length(int delta)
{
    float d = (float)(delta < 0 ? -delta : delta), ms;
    if (d <= PF_LINE_SMALL) {
        ms = PF_LINE_PERIOD_MS;
    } else if (d >= PF_LINE_LARGE) {
        ms = PF_LINE_FLOOR_MS;
    } else {
        ms = PF_LINE_PERIOD_MS
            + (PF_LINE_FLOOR_MS - PF_LINE_PERIOD_MS) * (d - PF_LINE_SMALL)
            / (PF_LINE_LARGE - PF_LINE_SMALL);
    }
    return samples(ms);
}

/* `r` to `target` along a line of `n` samples; held flat when it is there already. */
static void line_to(Ramp *r, float target, int n)
{
    if (target != ramp_value(r)) {
        ramp_to(r, target, n);
    } else if (ramp_running(r)) {
        ramp_flat(r, target);
    }
}

/* The interface ================================================================================ */

void pf_init(void)
{
    float k, norm;
    int i;
    open_rest = open_share(PF_LIP_OVERLAP);
    for (i = 0; i < PF_PIPES; i++) {
        pipe_build(&pipes[i], &PF_PIPE_TABLE[i]);
        pipe_clear(&pipes[i]);
        pipes[i].awake = 0;
        ramp_flat(&pipes[i].share, 0.0f);
        ramp_flat(&pipes[i].cover, 0.0f);
    }
    ramp_flat(&breath, 0.0f);
    ramp_flat(&ratio, 1.0f);
    lip = -1;
    breath_in = bend_in = breath_was = bend_was = 0;
    grid = line_length(0);
    line_large = samples(PF_LINE_FLOOR_MS);
    lift = samples(PF_HOP_LIFT_MS);
    attack = samples(PF_HOP_ATTACK_MS);
    grid_left = 0;
    /* the vibrato's high-pass at the grid's rate, its corner prewarped */
    k = tanf(PI_F * VIBRATO_CORNER_HZ * (float)grid / SR);
    norm = 1.0f / (1.0f + k * 1.41421356f + k * k);
    hp_b0 = norm;
    hp_b1 = -2.0f * norm;
    hp_b2 = norm;
    hp_a1 = 2.0f * (k * k - 1.0f) * norm;
    hp_a2 = (1.0f - k * 1.41421356f + k * k) * norm;
    hp_s1 = hp_s2 = wobble = tilt = 0.0f;
    tilted = 0;
    raft_awake = 0;
    since_wake = 0;
    floor_pa = powf(10.0f, PF_SLEEP_FLOOR_DB / 20.0f);
    noise_key = splitmix(TURBULENCE);
    cal[PF_CAL_BREATH_PP] = PF_BREATH_PP;
    cal[PF_CAL_BREATH_MF] = PF_BREATH_MF;
    cal[PF_CAL_BREATH_FF] = PF_BREATH_FF;
    cal[PF_CAL_BEND_REST] = PF_BEND_REST;
    cal[PF_CAL_BEND_SEMITONE_LOW] = PF_BEND_SEMITONE_LOW;
    cal[PF_CAL_BEND_SEMITONE_HIGH] = PF_BEND_SEMITONE_HIGH;
    cal[PF_CAL_BEND_FULL] = PF_BEND_FULL;
    cal[PF_CAL_VIBRATO_DEPTH] = PF_VIBRATO_DEPTH_CENTS_PER_G;
    for (i = 0; i < PF_STATE_ITEMS; i++) {
        shown[i] = 0.0f;
    }
    shown[PF_STATE_KEY] = -1.0f;
    shown[PF_STATE_BEND_CENTS] = 0.0f;
    shown_covering = -1.0f;
    dc_x1 = dc_y1 = 0.0f;
    for (i = 0; i < PF_EQ_SECTIONS; i++) {
        eq_s1[i] = eq_s2[i] = 0.0f;
    }
}

void pf_note_on(int key)
{
    int i, to = key - PF_FIRST_KEY;
    Pipe *p;
    if (to < 0 || to >= PF_PIPES || to == lip) {
        return;
    }
    p = &pipes[to];
    if (lip < 0 || breath_was <= 0 || breath_in <= 0) {
        /* no breath held: the jet handed over at once */
        for (i = 0; i < PF_PIPES; i++) {
            ramp_flat(&pipes[i].share, 0.0f);
            ramp_flat(&pipes[i].cover, 0.0f);
        }
        ramp_flat(&p->share, 1.0f);
        if (ramp_value(&breath) > 0.0f) {
            ramp_to(&ratio, p->ratio, line_large);
        } else {
            ramp_flat(&ratio, p->ratio);
        }
        lip = to;
        ramp_flat(&p->cover, lip_covering());
        return;
    }
    /* a hop: every pipe with a share of the jet lifted over the lift, its covering held until
     * its share is 0, then 0; the new pipe's share from where it is to 0 with them, then up to
     * the whole jet over the attack; the pressure to the new pipe's mf along the hop */
    for (i = 0; i < PF_PIPES; i++) {
        Pipe *o = &pipes[i];
        if (i == to) {
            continue;
        }
        if (ramp_value(&o->share) > 0.0f) {
            ramp_to(&o->share, 0.0f, lift);
            ramp_hold(&o->cover, lift, 0.0f);
        } else {
            ramp_flat(&o->share, 0.0f);
            ramp_flat(&o->cover, 0.0f);
        }
    }
    lip = to;
    if (ramp_value(&p->share) > 0.0f) {
        ramp_to(&p->share, 0.0f, lift);
        ramp_to(&p->cover, lip_covering(), line_large);
    } else {
        ramp_hold(&p->share, lift, 0.0f);
        ramp_flat(&p->cover, lip_covering());
    }
    ramp_then(&p->share, 1.0f, attack);
    ramp_to(&ratio, p->ratio, lift + attack);
}

void pf_note_off(int key)
{
    (void)key;
}

void pf_control(int control, int value)
{
    value = value < 0 ? 0 : value > 127 ? 127 : value;
    switch (control) {
    case PF_BREATH:
        breath_in = value;
        line_to(&breath, breath_pa((float)value), line_length(value - breath_was));
        break;
    case PF_BEND:
        bend_in = value;
        if (lip >= 0) {
            line_to(&pipes[lip].cover, lip_covering(), line_length(value - bend_was));
        }
        break;
    case PF_VIBRATO:
        tilt = ((float)value - PF_VIBRATO_CENTRE) * PF_VIBRATO_G_PER_UNIT;
        if (!tilted) {
            /* the filter at rest on the head's first word, so its offset is no step */
            hp_s1 = (hp_b1 + hp_b2) * tilt;
            hp_s2 = hp_b2 * tilt;
            tilted = 1;
        }
        break;
    default:
        break;
    }
}

/* A point of the vibrato's grid: the filter stepped on the tilt held there; on the lip's pipe, a
 * line over one period to the bend's and the wobble's covering, unless a line of the bend still
 * runs there (left to land, the next point catching up). */
static void grid_point(void)
{
    Pipe *p;
    float target;
    if (tilted) {
        float y = hp_b0 * tilt + hp_s1;
        hp_s1 = hp_b1 * tilt - hp_a1 * y + hp_s2;
        hp_s2 = hp_b2 * tilt - hp_a2 * y;
        shown[PF_STATE_VIBRATO_G] = y;
        wobble = VIBRATO_STEP_CENTS
            * roundf(y * cal[PF_CAL_VIBRATO_DEPTH] / VIBRATO_STEP_CENTS);
    }
    if (lip < 0) {
        return;
    }
    p = &pipes[lip];
    if (ramp_running(&p->cover)) {
        return;
    }
    target = lip_covering();
    if (target != p->cover.target) {
        ramp_to(&p->cover, target, grid);
    }
}

/* `n` samples of the awake pipes `awake[0..count)` into `mic`. */
static void render_raft(float *mic, int n, const int *awake, int count)
{
    float shares[PF_PIPES];
    int i, j;
    for (i = 0; i < n; i++) {
        float pa = fmaxf(ramp_tick(&breath) * ramp_tick(&ratio), 0.0f);
        float u = sqrtf(2.0f * pa / PF_RHO_AIR);
        float uf = sqrtf(2.0f * fmaxf(pa, P_FLOOR) / PF_RHO_AIR);
        float draw, y = 0.0f;
        /* the turbulence's draw: SplitMix64 of the source and the samples since the wake, 23
         * bits to (-1, 1) */
        uint64_t z = splitmix(noise_key + since_wake * GOLDEN);
        draw = (float)(2u * (uint32_t)(z >> 41) + 1u) * (1.0f / 8388608.0f) - 1.0f;
        since_wake++;
        for (j = 0; j < count; j++) {
            Pipe *p = &pipes[awake[j]];
            float g = ramp_tick(&p->share), c = ramp_tick(&p->cover);
            g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
            shares[j] = g;
            y += pipe_tick(p, u, uf, draw, g, c);
        }
        if (!isfinite(y)) {
            /* a non-finite state never leaves a delay line: each pipe gone non-finite is
             * silenced, its last flow primed at its share and the breath, so the jet-drive
             * does not fire from 0; the sample is 0 */
            for (j = 0; j < count; j++) {
                Pipe *p = &pipes[awake[j]];
                if (!pipe_finite(p)) {
                    pipe_clear(p);
                    p->q = shares[j] * jet_flow(p, u, 0.0f);
                }
            }
            y = 0.0f;
        }
        mic[i] = y;
    }
}

static void shaping_clear(void)
{
    int i;
    dc_x1 = dc_y1 = 0.0f;
    for (i = 0; i < PF_EQ_SECTIONS; i++) {
        eq_s1[i] = eq_s2[i] = 0.0f;
    }
}

/* What pf_state shows, at the block's end. The bend's cents are the estimate's at the lip's
 * covering, read again only when the covering moved. */
static void show(void)
{
    float pa = ramp_value(&breath);
    shown[PF_STATE_KEY] = lip < 0 ? -1.0f : (float)(lip + PF_FIRST_KEY);
    shown[PF_STATE_PRESSURE_G4_PA] = pa;
    shown[PF_STATE_PRESSURE_PA] = pa * ramp_value(&ratio);
    shown[PF_STATE_VIBRATO_CENTS] = wobble;
    if (lip < 0) {
        shown[PF_STATE_COVERING] = 0.0f;
        shown[PF_STATE_BEND_CENTS] = bend_value_cents((float)bend_in) - PF_BEND_REST_CENTS;
        shown_covering = -1.0f;
    } else {
        float c = clamp_covering(ramp_value(&pipes[lip].cover));
        shown[PF_STATE_COVERING] = c;
        if (c != shown_covering) {
            shown_covering = c;
            shown[PF_STATE_BEND_CENTS] = bend_cents(&pipes[lip], c) - wobble - PF_BEND_REST_CENTS;
        }
    }
}

float *pf_render(int frames)
{
    float p0, p1, last[PF_PIPES];
    int awake[PF_PIPES], count = 0, woke = 0, i, done;
    if (frames > PF_BLOCK) {
        frames = PF_BLOCK;
    }
    if (frames <= 0) {
        return out;
    }
    breath_was = breath_in;
    bend_was = bend_in;

    /* the wake: a pipe whose drive, its share times the pressure, is positive at the block's
     * first or last sample wakes at the first, from rest. Nothing moves a share or the pressure
     * inside a block but its ramps, so the last sample's are known here. */
    p0 = ramp_value(&breath) * ramp_value(&ratio);
    p1 = ramp_ahead(&breath, frames - 1) * ramp_ahead(&ratio, frames - 1);
    for (i = 0; i < PF_PIPES; i++) {
        Pipe *p = &pipes[i];
        float g1 = ramp_ahead(&p->share, frames - 1);
        if (!p->awake && (ramp_value(&p->share) * p0 > 0.0f || g1 * p1 > 0.0f)) {
            p->awake = 1;
            woke = 1;
            if (!raft_awake) {
                raft_awake = 1;
                since_wake = 0;
            }
        }
        if (p->awake) {
            last[count] = g1;
            awake[count++] = i;
        }
    }

    /* the block in pieces cut at the vibrato's grid points */
    for (done = 0; done < frames;) {
        int n;
        if (grid_left == 0) {
            grid_point();
            grid_left = grid;
        }
        n = frames - done < grid_left ? frames - done : grid_left;
        if (raft_awake) {
            render_raft(out + done, n, awake, count);
        } else {
            for (i = 0; i < n; i++) {
                out[done + i] = 0.0f;
            }
            ramp_skip(&breath, n);
            ramp_skip(&ratio, n);
        }
        for (i = 0; i < PF_PIPES; i++) {
            if (!pipes[i].awake) {
                ramp_skip(&pipes[i].share, n);
                ramp_skip(&pipes[i].cover, n);
            }
        }
        done += n;
        grid_left -= n;
    }

    if (raft_awake) {
        int sleeping = 1;
        /* the sleep: an awake pipe with no drive at the last sample, its output under the
         * floor over the block and the wave it holds under it at its first mode, is wiped */
        for (i = 0; i < count; i++) {
            Pipe *p = &pipes[awake[i]];
            if (last[i] * p1 <= 0.0f && p->peak < floor_pa
                    && bore_peak(p) * p->held < floor_pa) {
                pipe_clear(p);
                p->awake = 0;
            }
            p->peak = 0.0f;
            sleeping &= !p->awake;
        }

        /* the shaping: the DC block, the brightness curve, the gain */
        for (i = 0; i < frames; i++) {
            float x = out[i];
            int s;
            dc_y1 = x - dc_x1 + PF_DC_BLOCK_POLE * dc_y1;
            dc_x1 = x;
            x = PF_EQ_SCALE * dc_y1;
            for (s = 0; s < PF_EQ_SECTIONS; s++) {
                const float *b = PF_EQ[s];
                float y = b[0] * x + eq_s1[s];
                eq_s1[s] = b[1] * x - b[3] * y + eq_s2[s];
                eq_s2[s] = b[2] * x - b[4] * y;
                x = y;
            }
            out[i] = PF_OUTPUT_GAIN * x;
        }
        if (!isfinite(dc_y1) || !isfinite(eq_s1[PF_EQ_SECTIONS - 1])) {
            shaping_clear();
        }

        /* the raft asleep with its last pipe, but never in the block it woke: the shaping
         * cleared, so silence is exact zeros from here */
        if (sleeping && !woke) {
            raft_awake = 0;
            shaping_clear();
        }
    }
    show();
    return out;
}

int pf_config(int item)
{
    switch (item) {
    case PF_CFG_NOTES_CHANNEL: return PF_NOTES_CHANNEL;
    case PF_CFG_BREATH_CC: return PF_BREATH_CC;
    case PF_CFG_BREATH_CHANNEL: return PF_BREATH_CHANNEL;
    case PF_CFG_BEND_CC: return PF_BEND_CC;
    case PF_CFG_BEND_CHANNEL: return PF_BEND_CHANNEL;
    case PF_CFG_VIBRATO_CC: return PF_VIBRATO_CC;
    case PF_CFG_VIBRATO_CHANNEL: return PF_VIBRATO_CHANNEL;
    case PF_CFG_KEYBOARD_BASE: return PF_KEYBOARD_BASE;
    default: return -1;
    }
}

float pf_calibration(int item)
{
    return item >= 0 && item < PF_CAL_ITEMS ? cal[item] : -1.0f;
}

int pf_calibrate(int item, float value)
{
    float c[PF_CAL_ITEMS];
    int i;
    if (item < 0 || item >= PF_CAL_ITEMS) {
        return 0;
    }
    for (i = 0; i < PF_CAL_ITEMS; i++) {
        c[i] = cal[i];
    }
    c[item] = value;
    /* written so that a NaN fails every test */
    if (!(0.0f < c[PF_CAL_BREATH_PP] && c[PF_CAL_BREATH_PP] < c[PF_CAL_BREATH_MF]
            && c[PF_CAL_BREATH_MF] < c[PF_CAL_BREATH_FF] && c[PF_CAL_BREATH_FF] <= 127.0f)) {
        return 0;
    }
    if (!(0.0f <= c[PF_CAL_BEND_REST] && c[PF_CAL_BEND_REST] < c[PF_CAL_BEND_SEMITONE_LOW]
            && c[PF_CAL_BEND_SEMITONE_LOW] <= c[PF_CAL_BEND_SEMITONE_HIGH]
            && c[PF_CAL_BEND_SEMITONE_HIGH] < c[PF_CAL_BEND_FULL]
            && c[PF_CAL_BEND_FULL] <= 127.0f)) {
        return 0;
    }
    if (!(0.0f <= c[PF_CAL_VIBRATO_DEPTH] && c[PF_CAL_VIBRATO_DEPTH] <= 300.0f)) {
        return 0;
    }
    cal[item] = value;
    /* the control's ramp retargeted along a small step's line; the vibrato's depth is taken at
     * the grid's next point, whose line runs over a period */
    if (item <= PF_CAL_BREATH_FF) {
        line_to(&breath, breath_pa((float)breath_in), grid);
    } else if (item <= PF_CAL_BEND_FULL && lip >= 0) {
        line_to(&pipes[lip].cover, lip_covering(), grid);
    }
    return 1;
}

float pf_map(int control, float value)
{
    switch (control) {
    case PF_BREATH: return breath_pa(value);
    case PF_BEND: return bend_value_cents(value) - PF_BEND_REST_CENTS;
    default: return 0.0f;
    }
}

float pf_state(int item)
{
    return item >= 0 && item < PF_STATE_ITEMS ? shown[item] : 0.0f;
}
