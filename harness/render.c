/* render.c: the pan flute played from a script, written to a WAV file (16-bit mono, 44.1 kHz).
 *
 *   render OUT.wav             a phrase: a swell to mf on D5, a hop to G5, a bend to the
 *                              semitone and back, a vibrato, a hop to G4 and a swell to ff, a
 *                              release
 *   render OUT.wav scale [V]   every pipe G4 to G6 in turn at the breath value V (12, mf, by
 *                              default), 1.5 s each, 0.6 s apart
 *   render OUT.wav levels      G4, D5 and G6, each at the pp, mf and ff markers in turn
 *
 * The breath, the bend and the vibrato are sent as a TEControl sends them: their values every
 * 10.4 ms while they move. Prints the level, the peak, the clipped samples and the cost.
 *
 * The synth is included whole, so the harness builds from this file alone. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_M_X64) || defined(__SSE3__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define FLUSH_DENORMALS() \
    (_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON), _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON))
#else
#define FLUSH_DENORMALS() ((void)0)
#endif

#include "../src/panflute.c"

#define RATE 44100
#define SENSOR 459 /* the TEControl's period, 10.4 ms, samples */
#define MOST (RATE * 120)

/* The script: the keys struck, at their times; the three controls as curves of time. */
typedef struct {
    double at;
    int key;
} Note;

static Note notes[64];
static int note_count;
static double (*curve[PF_CONTROLS])(double t);
static double length_s;
static float level;

static short pcm[MOST];

/* A line from (t0, v0) to (t1, v1), held either side. */
static double seg(double t, double t0, double v0, double t1, double v1)
{
    if (t <= t0) {
        return v0;
    }
    if (t >= t1) {
        return v1;
    }
    return v0 + (v1 - v0) * (t - t0) / (t1 - t0);
}

/* The phrase --------------------------------------------------------------------------------- */

static double phrase_breath(double t)
{
    if (t < 5.2) {
        return seg(t, 0.10, 0.0, 0.35, 12.0);
    }
    if (t < 6.4) {
        return seg(t, 5.3, 12.0, 5.6, 33.0);
    }
    return seg(t, 6.4, 33.0, 6.55, 0.0);
}

static double phrase_bend(double t)
{
    return t < 2.8 ? seg(t, 2.3, 0.0, 2.6, 32.0) : seg(t, 2.8, 32.0, 3.0, 0.0);
}

static double phrase_vibrato(double t)
{
    /* the head still at 62, then a wobble of 0.1 g at 2 Hz for 1.5 s */
    if (t < 3.4 || t > 4.9) {
        return 62.0;
    }
    return 62.0 + 6.1 * sin(2.0 * 3.141592653589793 * 2.0 * (t - 3.4));
}

static void phrase(void)
{
    static const Note n[] = { { 0.05, 74 }, { 1.30, 79 }, { 5.20, 67 } };
    memcpy(notes, n, sizeof n);
    note_count = 3;
    curve[PF_BREATH] = phrase_breath;
    curve[PF_BEND] = phrase_bend;
    curve[PF_VIBRATO] = phrase_vibrato;
    length_s = 7.5;
}

/* The scale and the levels --------------------------------------------------------------------- */

#define HELD 1.5
#define APART 2.1

static double still(double t)
{
    (void)t;
    return 0.0;
}

static double head(double t)
{
    (void)t;
    return 62.0;
}

/* the breath at `level` from each note's time for HELD seconds, a tongued attack and stop */
static double held_breath(double t)
{
    int i;
    for (i = 0; i < note_count; i++) {
        double from = notes[i].at;
        if (t >= from + 0.01 && t < from + 0.01 + HELD) {
            return level;
        }
    }
    return 0.0;
}

static void scale(float value)
{
    int k;
    note_count = 0;
    for (k = PF_FIRST_KEY; k < PF_FIRST_KEY + PF_PIPES; k++) {
        notes[note_count].at = 0.05 + APART * note_count;
        notes[note_count++].key = k;
    }
    level = value;
    curve[PF_BREATH] = held_breath;
    curve[PF_BEND] = still;
    curve[PF_VIBRATO] = head;
    length_s = 0.05 + APART * note_count;
}

/* G4, D5, G6, each at pp, mf, ff by the markers */
static double marker_breath(double t)
{
    int i = (int)((t - 0.05) / APART);
    double from = 0.05 + APART * i;
    if (i < 0 || i >= note_count || t < from + 0.01 || t >= from + 0.01 + HELD) {
        return 0.0;
    }
    return pf_calibration(PF_CAL_BREATH_PP + i % 3);
}

static void levels(void)
{
    static const int keys[] = { 67, 74, 91 };
    int i;
    note_count = 0;
    for (i = 0; i < 9; i++) {
        notes[note_count].at = 0.05 + APART * note_count;
        notes[note_count++].key = keys[i / 3];
    }
    curve[PF_BREATH] = marker_breath;
    curve[PF_BEND] = still;
    curve[PF_VIBRATO] = head;
    length_s = 0.05 + APART * note_count;
}

/* Playing ------------------------------------------------------------------------------------- */

static void write_le(FILE *f, unsigned v, int bytes)
{
    int i;
    for (i = 0; i < bytes; i++) {
        fputc((int)((v >> (8 * i)) & 0xFF), f);
    }
}

static int write_wav(const char *path, const short *s, long n)
{
    FILE *f = fopen(path, "wb");
    long i;
    if (!f) {
        return 0;
    }
    fwrite("RIFF", 1, 4, f);
    write_le(f, (unsigned)(36 + 2 * n), 4);
    fwrite("WAVEfmt ", 1, 8, f);
    write_le(f, 16, 4);
    write_le(f, 1, 2);
    write_le(f, 1, 2);
    write_le(f, RATE, 4);
    write_le(f, 2 * RATE, 4);
    write_le(f, 2, 2);
    write_le(f, 16, 2);
    fwrite("data", 1, 4, f);
    write_le(f, (unsigned)(2 * n), 4);
    for (i = 0; i < n; i++) {
        write_le(f, (unsigned)(unsigned short)s[i], 2);
    }
    return fclose(f) == 0;
}

/* The longest transit the jet's line is asked for, samples: on each pipe at the breath's
 * floor, its wave slowest there. */
static float longest_transit(void)
{
    float most = 0.0f, uf = sqrtf(2.0f * P_FLOOR / PF_RHO_AIR);
    int i;
    for (i = 0; i < PF_PIPES; i++) {
        const Pipe *p = &pipes[i];
        float speed = PF_JET_CONVECTION * uf * powf(uf / p->u_mf, -PF_JET_SLOWING);
        most = fmaxf(most, fminf(p->jet_m / speed * SR, SR / 40.0f));
    }
    return most;
}

int main(int argc, char **argv)
{
    long n, i, clipped = 0;
    int last[PF_CONTROLS], next = 0, c;
    double sum = 0.0, peak = 0.0, secs;
    clock_t start;

    if (argc < 2) {
        fprintf(stderr, "usage: render OUT.wav [scale [BREATH] | levels]\n");
        return 2;
    }
    FLUSH_DENORMALS();
    pf_init();
    if (argc >= 3 && strcmp(argv[2], "scale") == 0) {
        scale(argc >= 4 ? (float)atof(argv[3]) : 12.0f);
    } else if (argc >= 3 && strcmp(argv[2], "levels") == 0) {
        levels();
    } else {
        phrase();
    }
    printf("jet line: longest transit %.1f of %d samples\n", longest_transit(), JET_SLOTS - 2);
    if (longest_transit() > (float)(JET_SLOTS - 2)) {
        fprintf(stderr, "the jet's line is too short\n");
        return 1;
    }
    n = (long)(length_s * RATE);
    if (n > MOST) {
        n = MOST;
    }
    for (c = 0; c < PF_CONTROLS; c++) {
        last[c] = -1;
    }

    start = clock();
    for (i = 0; i < n; i += PF_BLOCK) {
        int frames = n - i < PF_BLOCK ? (int)(n - i) : PF_BLOCK, k;
        float *y;
        /* the sensor's messages that fall in this block, every 10.4 ms while a value moves */
        if (i / SENSOR != (i + frames) / SENSOR || i == 0) {
            double t = (double)((i + frames) / SENSOR * SENSOR) / RATE;
            for (c = 0; c < PF_CONTROLS; c++) {
                int v = (int)floor(curve[c](t) + 0.5);
                if (v != last[c]) {
                    pf_control(c, v);
                    last[c] = v;
                }
            }
        }
        while (next < note_count && notes[next].at * RATE < (double)(i + frames)) {
            pf_note_on(notes[next++].key);
        }
        y = pf_render(frames);
        for (k = 0; k < frames; k++) {
            double s = y[k];
            sum += s * s;
            peak = fabs(s) > peak ? fabs(s) : peak;
            if (s > 1.0 || s < -1.0) {
                clipped++;
                s = s > 0.0 ? 1.0 : -1.0;
            }
            pcm[i + k] = (short)floor(s * 32767.0 + 0.5);
        }
    }
    secs = (double)(clock() - start) / CLOCKS_PER_SEC;

    printf("%.2f s: rms %.1f dBFS, peak %.1f dBFS, %ld samples clipped\n", (double)n / RATE,
        10.0 * log10(sum / (double)n + 1e-30), 20.0 * log10(peak + 1e-30), clipped);
    printf("cost: %.1f ns a sample, %.2f %% of real time\n", 1e9 * secs / (double)n,
        100.0 * secs * RATE / (double)n);
    if (!write_wav(argv[1], pcm, n)) {
        fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
