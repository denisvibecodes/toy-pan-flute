/* coefficients.h: the numbers of the pan flute voice "panflute": the air, the player's
 * embouchure and the jet, the 25 pipes of the raft as cut, and the tone curve applied to the
 * microphone's signal. Everything runs at 44.1 kHz.
 *
 * Generated; do not edit by hand. Plain C99 data, no includes. Lengths in metres, pressures in
 * pascals, times in milliseconds unless a name says otherwise. F&R is N. H. Fletcher and
 * T. D. Rossing, The Physics of Musical Instruments, 2nd ed. */

#ifndef PHYS_MOD_WIND_COEFFICIENTS_H
#define PHYS_MOD_WIND_COEFFICIENTS_H

/* Rate and air --------------------------------------------------------------------------------- */

/* The sample rate, Hz: the model, the pipe table and the equalizer are all at it. */
static const float PF_SAMPLE_RATE = 44100.0f;
/* Air density at 20 deg C, kg/m^3. */
static const float PF_RHO_AIR = 1.2f;
/* Speed of sound at 20 deg C, m/s. */
static const float PF_C_AIR = 343.0f;
/* The pitch standard the pipes are cut to, Hz at A4 (equal temperament:
 * f = A4 2^((key - 69) / 12)). */
static const float PF_A4_HZ = 440.0f;

/* The jet -------------------------------------------------------------------------------------- */

/* The labium's offset over the jet's centreline, y0 / b, b = 2h/5 the Bickley half-width of a
 * jet from a flue of height h. The flow into the pipe is Q_j = b H U [1 + tanh((eta - y0) / b)]
 * (Verge 1997 II eq. 8-9; Fletcher 2005 eq. 4-6). */
static const float PF_JET_OFFSET = 0.75f;
/* The Q of the jet's second-order band-pass, the width of the frequencies its instability
 * amplifies. */
static const float PF_JET_BAND_Q = 1.0f;
/* The band's centre as a Strouhal number on the flue height, St_h = f_c h / U: f_c follows the
 * jet velocity U = sqrt(2 p / rho) (de la Cuadra 2008). */
static const float PF_JET_BAND_STROUHAL = 0.01f;
/* The jet's spatial growth rate beta_j: the band's peak gain exp(beta_j W / h) over the jet's
 * length W (Auvray 2016 eq. 10-11, measured 0.24-0.37 on a panpipe replica). */
static const float PF_JET_GROWTH = 0.33f;
/* The turbulent disturbance per unit of flue height, K_t: the jet's receptivity is
 * eta0 = h v / U + K_t h n, v the acoustic velocity at the open end, n white noise in (-1, 1)
 * shared by the raft (de la Cuadra 2005; Auvray 2012 eq. 9). */
static const float PF_JET_TURBULENCE = 0.03f;
/* The jet wave's convection velocity over the jet velocity, gamma, at the pipe's mf pressure:
 * the transit from flue to labium is tau = W / (gamma U) (Auvray 2016 eq. 10; Terrien 2014
 * eq. 5). */
static const float PF_JET_CONVECTION = 0.4f;
/* The wave's slowing with the breath, s: its speed is gamma U (U / U_mf)^(-s), U_mf the jet
 * velocity at the pipe's mf pressure, so the transit shrinks as U^(s - 1). 0.5 is the dispersion
 * of a wave on a Bickley jet (Fletcher 1979 eq. 13; F&R p. 507). */
static const float PF_JET_SLOWING = 0.5f;
/* The jet's width on the mouth over the bore's radius: H = 2a, the jet as wide as the pipe. */
static const float PF_JET_WIDTH = 2.0f;
/* The transit delay's largest move, samples per sample, so a disturbance in flight keeps about
 * the transit it left with and the delay line is never read backwards. */
static const float PF_JET_SLEW = 0.5f;

/* The bore and its open end -------------------------------------------------------------------- */

/* The wall's viscothermal loss beta: alpha = beta sqrt(f) / a, Np/m (F&R eq. 8.15). With the
 * radiation, the round trip's magnitude |R(f)| = exp(-2 alpha L) (1 - 2 alpha_r (ka)^2), fitted
 * per pipe as a scalar and a unity-DC one-pole exact at the first two modes, f1 and 3 f1. */
static const float PF_WALL_LOSS = 3e-5f;
/* The open end's radiation loss alpha_r, |R| = 1 - 2 alpha_r (ka)^2, the unflanged end's
 * (Magron 2013); the lip covering the end raises it linearly in the covering to twice this, the
 * flanged end's, with the lip fully over the end. */
static const float PF_RADIATION_LOSS = 0.25f;
/* The vena contracta of the vortex loss at the open end, alpha_vc (Auvray 2014 eq. 2), with the
 * lip at rest; the loss is quadratic in the velocity through the end (Verge 1997 I eq. 8). */
static const float PF_VENA_CONTRACTA = 0.6f;

/* The lip over the end ------------------------------------------------------------------------- */

/* The share r of the pipe's diameter the lower lip overlaps at rest. The covering gamma (0 at
 * rest) moves it to l = r + gamma (1 - r). The jet runs from the lip to the far rim:
 * W = (1 - r) 2a, whatever the covering. */
static const float PF_LIP_OVERLAP = 0.35f;
/* The lip's thickness over the end, mm: the channel under it adds t gamma / s to the end
 * correction, s the open share of the end's area. */
static const float PF_LIP_THICKNESS_MM = 1.5f;
/* The end correction over the radius of an unflanged open end, 0.61 (F&R eq. 8.33):
 * c(l) at l = 0. The end correction is Delta(l) = c(l) a / sqrt(s(l)) + t gamma / s(l),
 * c(l) = 0.61 + 0.24 l, s(l) = (acos u - u sqrt(1 - u^2)) / pi with u = 2l - 1 (the circle's
 * segment beyond the lip's chord). At rest Delta = 0.84 a. */
static const float PF_END_CORRECTION = 0.61f;
/* The end correction over the radius of a flanged open end, 8 / (3 pi) (F&R eq. 8.32):
 * c(l) at l = 1, the lip flanging the end. */
static const float PF_FLANGED_CORRECTION = 0.85f;
/* The smallest open share s of the end's area the lip may leave; the covering is clamped there. */
static const float PF_OPEN_MIN = 0.05f;
/* The covering at which the open share falls to PF_OPEN_MIN with the lip's overlap
 * PF_LIP_OVERLAP: the covering's upper clamp. */
static const float PF_COVERING_MAX = 0.8502951f;
/* The jet's pull on a covering's bend, kappa. A covering gamma bends a pipe by
 * c = 1200 log2((L + Delta(0)) / (L + Delta(gamma))) cents, L the pipe's loop_m, then softened
 * to c / sqrt(1 + kappa (c / 200)^2): the bend the pipe sounds. The table's coverings invert
 * this. */
static const float PF_BEND_PULL = 0.2f;

/* Microphone and sleep ------------------------------------------------------------------------- */

/* The microphone's distance from the raft, m. */
static const float PF_MIC_M = 1.0f;
/* The level, dB re 1 Pa of a pipe's radiated pressure at the microphone, under which an unblown
 * pipe is put to sleep (its state zeroed): far under hearing. */
static const float PF_SLEEP_FLOOR_DB = -120.0f;

/* The player ----------------------------------------------------------------------------------- */

/* The mouth pressure at the three breath levels pp, mf, ff, Pa, at G4. On another pipe each is
 * scaled by the pipe's mf over G4's (mf_pa / PF_PIPE_TABLE[0].mf_pa); 0 Pa at no breath. */
static const float PF_PRESSURES_PA[3] = { 384.0f, 480.0f, 960.0f };
/* The lip's rest, cents re the pipe uncovered: with the bite at rest the lip covers the pipe it
 * blows by the covering that bends it this much, and every pipe is cut in tune there, so
 * uncovered it sounds sharp by this and the vibrato has room upward. */
static const float PF_REST_CENTS = -9.0f;
/* The full bite's bend, cents re the rest, the same on every pipe; half of it at the semitone. */
static const float PF_REACH_CENTS = -200.0f;
/* The vibrato's depth, cents per g of the head's tilt (the tilt high-passed at 0.5 Hz), added to
 * the bite's cents. */
static const float PF_VIBRATO_CENTS_PER_G = 75.0f;
/* A hop between pipes, non-legato under a held breath: the lip lifts off, every pipe's share of
 * the jet falling linearly to 0 over this time, ms. */
static const float PF_LIFT_MS = 20.0f;
/* Then the new pipe's share of the jet rises linearly from 0 to 1 over this time, ms. */
static const float PF_ATTACK_MS = 20.0f;

/* The pipes ------------------------------------------------------------------------------------ */

/* The raft: 25 stopped cylindrical pipes, one per MIDI key 67 (G4) to 91 (G6), in the keys'
 * order. Each is a tube closed by a stopper, open at the mouth, its radius tied to its length by
 * d = 1.32 L^0.43 (mm; Fletcher 2005 p. 370). The length is cut so that the loop (the round trip
 * in the tube with its loss, the open end's reflection with the lip at rest, and one sample of
 * latency at 44.1 kHz) sounds the key's pitch, sharpened by 8.1 c at G4 rising 15.5 c an octave
 * so that the jet's pull at mf takes it back, with the lip resting over the end at the covering
 * that bends the pipe by PF_REST_CENTS.
 *
 * key: the MIDI key.
 * length_m: the cut length, open end to stopper, m.
 * radius_m: the bore's radius a, m.
 * jet_m: the jet's length W, from the lip to the far rim, (1 - PF_LIP_OVERLAP) 2a, m.
 * flue_m: the flue height h, the slit between the lips, m: 0.8 mm at G4, narrowed by 1.25 an
 * octave up the raft.
 * mf_pa: the pipe's mf blowing pressure, Pa: 480 Pa at G4, times 2 an octave up. The jet's wave
 * slowing is referenced at its velocity.
 * rest_covering: the covering gamma of the lip at rest, the one that bends the pipe by
 * PF_REST_CENTS.
 * loop_m: the loop's acoustic length less the end correction, c / 4 f_t - Delta(rest), f_t the
 * pitch it is cut to, m: the L of the bend's estimate (PF_BEND_PULL).
 * covering[4]: the coverings that bend the pipe by 0, -15, -100 and -200 cents re uncovered (0
 * is no covering). */
#define PF_PIPES 25
#define PF_FIRST_KEY 67

typedef struct {
    int key;
    float length_m;
    float radius_m;
    float jet_m;
    float flue_m;
    float mf_pa;
    float rest_covering;
    float loop_m;
    float covering[4];
} PfPipe;

static const PfPipe PF_PIPE_TABLE[PF_PIPES] = {
    /* G4, 392.00 Hz */
    { 67, 0.20241119f, 0.006474777f, 0.00841721f, 0.00080000004f, 480.0f,
        0.14767236f, 0.21118522f, { 0.0f, 0.22407201f, 0.6580365f, 0.8018644f } },
    /* G#4, 415.30 Hz */
    { 68, 0.19055688f, 0.0063089137f, 0.008201588f, 0.0007852613f, 508.5423f,
        0.14253059f, 0.19901451f, { 0.0f, 0.21693552f, 0.64863205f, 0.7952415f } },
    /* A4, 440.00 Hz */
    { 69, 0.17937863f, 0.006147031f, 0.007991141f, 0.00077079405f, 538.7818f,
        0.13752398f, 0.18754005f, { 0.0f, 0.20994636f, 0.6390984f, 0.7884495f } },
    /* A#4, 466.16 Hz */
    { 70, 0.16883782f, 0.005989023f, 0.0077857296f, 0.0007565933f, 570.8194f,
        0.13265255f, 0.17672217f, { 0.0f, 0.20310727f, 0.6294404f, 0.78148735f } },
    /* B4, 493.88 Hz */
    { 71, 0.15889868f, 0.0058347965f, 0.007585235f, 0.00074265426f, 604.7621f,
        0.12791753f, 0.16652338f, { 0.0f, 0.19641906f, 0.61966455f, 0.7743547f } },
    /* C5, 523.25 Hz */
    { 72, 0.14952601f, 0.0056842375f, 0.0073895087f, 0.00072897197f, 640.72314f,
        0.12331504f, 0.15690847f, { 0.0f, 0.18988287f, 0.6097764f, 0.7670511f } },
    /* C#5, 554.37 Hz */
    { 73, 0.14068864f, 0.0055372664f, 0.007198446f, 0.00071554177f, 678.8225f,
        0.11884333f, 0.1478441f, { 0.0f, 0.18349847f, 0.5997819f, 0.75957656f } },
    /* D5, 587.33 Hz */
    { 74, 0.13235548f, 0.0053937775f, 0.0070119104f, 0.000702359f, 719.1874f,
        0.11450072f, 0.13929896f, { 0.0f, 0.17726699f, 0.5896888f, 0.75193167f } },
    /* D#5, 622.25 Hz */
    { 75, 0.12449787f, 0.00525368f, 0.006829784f, 0.00068941916f, 761.9525f,
        0.11028662f, 0.1312434f, { 0.0f, 0.17118704f, 0.57950294f, 0.7441163f } },
    /* E5, 659.26 Hz */
    { 76, 0.11708869f, 0.0051168823f, 0.006651947f, 0.0006767176f, 807.26056f,
        0.1061963f, 0.12364956f, { 0.0f, 0.16525683f, 0.56923175f, 0.73613167f } },
    /* F5, 698.46 Hz */
    { 77, 0.11010227f, 0.004983293f, 0.0064782803f, 0.00066425017f, 855.26276f,
        0.10223162f, 0.116491064f, { 0.0f, 0.1594789f, 0.5588838f, 0.72797954f } },
    /* F#5, 739.99 Hz */
    { 78, 0.10351567f, 0.004852847f, 0.0063087014f, 0.00065201236f, 906.1193f,
        0.09838776f, 0.10974312f, { 0.0f, 0.15385157f, 0.5484661f, 0.7196605f } },
    /* G5, 783.99 Hz */
    { 79, 0.09730451f, 0.0047254283f, 0.006143057f, 0.00064000004f, 960.0f,
        0.09466393f, 0.10338232f, { 0.0f, 0.14837372f, 0.53798735f, 0.71117747f } },
    /* G#5, 830.61 Hz */
    { 80, 0.091448635f, 0.0046009785f, 0.0059812716f, 0.00062820903f, 1017.0846f,
        0.09105645f, 0.09738658f, { 0.0f, 0.14304453f, 0.5274558f, 0.70253193f } },
    /* A5, 880.00 Hz */
    { 81, 0.085926704f, 0.0044793924f, 0.00582321f, 0.0006166352f, 1077.5636f,
        0.08756402f, 0.09173511f, { 0.0f, 0.13786161f, 0.51688063f, 0.6937275f } },
    /* A#5, 932.33 Hz */
    { 82, 0.08072118f, 0.004360624f, 0.005668811f, 0.0006052747f, 1141.6388f,
        0.08418378f, 0.08640823f, { 0.0f, 0.13282502f, 0.50626945f, 0.6847662f } },
    /* B5, 987.77 Hz */
    { 83, 0.07581072f, 0.004244516f, 0.0055178707f, 0.0005941234f, 1209.5242f,
        0.08091506f, 0.0813875f, { 0.0f, 0.12793347f, 0.49563444f, 0.67565346f } },
    /* C6, 1046.50 Hz */
    { 84, 0.07118382f, 0.0041311216f, 0.0053704577f, 0.00058317755f, 1281.4463f,
        0.077754796f, 0.07665533f, { 0.0f, 0.123183936f, 0.48498034f, 0.66639066f } },
    /* C#6, 1108.73 Hz */
    { 85, 0.06682126f, 0.0040202895f, 0.005226376f, 0.0005724334f, 1357.645f,
        0.074698076f, 0.072195336f, { 0.0f, 0.11857493f, 0.47431892f, 0.65698326f } },
    /* D6, 1174.66 Hz */
    { 86, 0.06270809f, 0.0039119488f, 0.005085533f, 0.0005618872f, 1438.3749f,
        0.071746275f, 0.067992f, { 0.0f, 0.11410482f, 0.46365994f, 0.6474366f } },
    /* D#6, 1244.51 Hz */
    { 87, 0.058830045f, 0.0038060248f, 0.004947832f, 0.00055153534f, 1523.905f,
        0.06889519f, 0.06403066f, { 0.0f, 0.109772086f, 0.45301253f, 0.63775575f } },
    /* E6, 1318.51 Hz */
    { 88, 0.05517362f, 0.0037024445f, 0.0048131775f, 0.00054137415f, 1614.5211f,
        0.066142336f, 0.060297515f, { 0.0f, 0.105573595f, 0.44238675f, 0.62794614f } },
    /* F6, 1396.91 Hz */
    { 89, 0.051725753f, 0.003601123f, 0.00468146f, 0.0005314001f, 1710.5256f,
        0.06348607f, 0.056779563f, { 0.0f, 0.10150893f, 0.43179232f, 0.6180146f } },
    /* F#6, 1479.98 Hz */
    { 90, 0.04847375f, 0.0035019652f, 0.004552555f, 0.0005216099f, 1812.2386f,
        0.06092322f, 0.053464547f, { 0.0f, 0.09757471f, 0.4212401f, 0.6079681f } },
    /* G6, 1567.98 Hz */
    { 91, 0.045417327f, 0.0034052522f, 0.0044268277f, 0.000512f, 1920.0f,
        0.05844894f, 0.050340578f, { 0.0f, 0.09376356f, 0.41072804f, 0.59780467f } },
};

/* The shaping stage ---------------------------------------------------------------------------- */

/* The microphone's signal (Pa) to the output: the DC block, the equalizer's scale and cascade,
 * then the output gain, in that order. */
/* The microphone's coupling, Hz: a one-pole DC block y[n] = x[n] - x[n-1] + R y[n-1],
 * R = exp(-2 pi f / fs) clamped to 0.9 .. 0.9999. */
static const float PF_DC_BLOCK_HZ = 20.0f;
/* R at PF_DC_BLOCK_HZ and 44.1 kHz. */
static const float PF_DC_BLOCK_POLE = 0.99715453f;
/* The output gain after the equalizer, full scale per pascal. */
static const float PF_OUTPUT_GAIN = 6.417f;

/* The tone curve, a brightness correction fitted to recordings of pan flutes: its nodes (Hz,
 * dB), piecewise linear in log f between them and held under the first and over the last.
 *       300 Hz    +0.0 dB
 *       600 Hz    +6.5 dB
 *      1200 Hz    +4.6 dB
 *      2400 Hz    -8.1 dB
 *      4800 Hz    -3.7 dB
 *      9600 Hz   -19.0 dB
 *
 * It is realised as a scalar (the first node's gain) and a cascade of minimum-phase biquads:
 * peaking filters (R. Bristow-Johnson, Audio EQ Cookbook) of Q 1.6 at every third of an octave
 * on a grid through 1 kHz, their gains fitted by least squares to the curve (V. Valimaki and
 * J. Liski, Accurate cascade graphic equalizer, IEEE SPL 24(2), 2017), and a high shelf (S = 1)
 * carrying the last node's gain less the first's. Each section runs in transposed direct
 * form II, a0 = 1:
 *   y = b0 x + s1;  s1 = b1 x - a1 y + s2;  s2 = b2 x - a2 y;
 *
 * The cascade's gain with these f32 coefficients, against the curve, dB:
 *       100 Hz    +0.026 dB  (curve  +0.00)
 *       300 Hz    +0.216 dB  (curve  +0.00)
 *       600 Hz    +6.241 dB  (curve  +6.50)
 *      1200 Hz    +4.231 dB  (curve  +4.60)
 *      2400 Hz    -7.531 dB  (curve  -8.10)
 *      4800 Hz    -4.386 dB  (curve  -3.70)
 *      9600 Hz   -18.507 dB  (curve -19.00)
 *     16000 Hz   -18.967 dB  (curve -19.00) */
/* The equalizer's input scale, linear: the curve's gain under its first node. */
static const float PF_EQ_SCALE = 1.0f;
#define PF_EQ_SECTIONS 21

/* Each section's { b0, b1, b2, a1, a2 }, in order. */
static const float PF_EQ[PF_EQ_SECTIONS][5] = {
    /* peaking, 198.4 Hz, -0.129 dB */
    { 0.9998703f, -1.9815667f, 0.98248845f, -1.9815667f, 0.98235875f },
    /* peaking, 250.0 Hz, +0.026 dB */
    { 1.0000327f, -1.9767655f, 0.9779874f, -1.9767655f, 0.97802013f },
    /* peaking, 315.0 Hz, -1.533 dB */
    { 0.9975602f, -1.9678535f, 0.97227657f, -1.9678535f, 0.9698367f },
    /* peaking, 396.9 Hz, +1.365 dB */
    { 1.0027331f, -1.9647287f, 0.9651404f, -1.9647287f, 0.96787345f },
    /* peaking, 500.0 Hz, +1.079 dB */
    { 1.0027072f, -1.9540793f, 0.95634085f, -1.9540793f, 0.95904803f },
    /* peaking, 630.0 Hz, +4.229 dB */
    { 1.0134785f, -1.9491509f, 0.9435499f, -1.9491509f, 0.9570283f },
    /* peaking, 793.7 Hz, +0.774 dB */
    { 1.0030422f, -1.9223915f, 0.9317067f, -1.9223915f, 0.934749f },
    /* peaking, 1000.0 Hz, +2.131 dB */
    { 1.0104992f, -1.9049621f, 0.9139624f, -1.9049621f, 0.92446166f },
    /* peaking, 1259.9 Hz, +3.385 dB */
    { 1.0209228f, -1.8814725f, 0.89127564f, -1.8814725f, 0.9121984f },
    /* peaking, 1587.4 Hz, -0.769 dB */
    { 0.99421406f, -1.8160421f, 0.86928535f, -1.8160421f, 0.8634994f },
    /* peaking, 2000.0 Hz, -1.342 dB */
    { 0.9875918f, -1.7529879f, 0.8390551f, -1.7529879f, 0.8266469f },
    /* peaking, 2519.8 Hz, -6.352 dB */
    { 0.9291236f, -1.6166275f, 0.7975946f, -1.6166275f, 0.7267182f },
    /* peaking, 3174.8 Hz, -0.741 dB */
    { 0.98980105f, -1.5744487f, 0.76069504f, -1.5744487f, 0.7504961f },
    /* peaking, 4000.0 Hz, -1.705 dB */
    { 0.97204703f, -1.4198215f, 0.7142957f, -1.4198215f, 0.6863427f },
    /* peaking, 5039.7 Hz, +2.662 dB */
    { 1.0537796f, -1.2803684f, 0.64634866f, -1.2803684f, 0.70012826f },
    /* peaking, 6349.6 Hz, -4.008 dB */
    { 0.9126579f, -0.943833f, 0.6147065f, -0.943833f, 0.5273645f },
    /* peaking, 8000.0 Hz, -3.758 dB */
    { 0.9084712f, -0.61778045f, 0.5702862f, -0.61778045f, 0.4787574f },
    /* peaking, 10079.4 Hz, -5.785 dB */
    { 0.85330373f, -0.18759671f, 0.5433111f, -0.18759671f, 0.39661482f },
    /* peaking, 12699.2 Hz, -0.702 dB */
    { 0.98133874f, 0.3590368f, 0.53820944f, 0.3590368f, 0.5195482f },
    /* peaking, 16000.0 Hz, +0.246 dB */
    { 1.0054562f, 1.0550994f, 0.6154384f, 1.0550994f, 0.62089455f },
    /* high shelf, 9600.0 Hz, -19.000 dB at Nyquist */
    { 0.29540375f, 0.1167887f, 0.058885798f, -0.8228252f, 0.2939034f },
};

#endif /* PHYS_MOD_WIND_COEFFICIENTS_H */
