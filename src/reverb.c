/* reverb.c: the listening reverb (reverb.h), Freeverb's topology (Jezar at Dreampoint, 2000,
 * public domain): per side, eight damped feedback combs in parallel and four allpasses in
 * series, the right side's delays 23 samples longer than the left's (Freeverb's stereo spread),
 * its lengths those it tunes for 44.1 kHz. Described in J. O. Smith, *Physical Audio Signal
 * Processing*, "Freeverb". The wet is added to the dry signal on both sides.
 *
 * Freeverb's input gain (0.015), damping (0.2) and allpass feedback (0.5) are its own; the comb
 * feedback 0.9 (Freeverb's default room size gives 0.84), for a longer decay, and the wet gain
 * 0.35 are Denis's choices by ear. */

#include "reverb.h"

#include <string.h>

#define COMBS 8
#define ALLPASSES 4
#define SPREAD 23
#define INPUT_GAIN 0.015f
#define FEEDBACK 0.9f
#define DAMP 0.2f
#define ALLPASS_FEEDBACK 0.5f
#define WET 0.35f

/* The wet's move on a switch, frames: 10 ms, short enough to be heard as a cut, long enough not
 * to click. */
#define SWITCH 441

static const int comb_len[COMBS] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int allpass_len[ALLPASSES] = { 556, 441, 341, 225 };

/* Every line's samples: the combs' 11024 and the allpasses' 1563, twice, and the right side's
 * spread, 12 x 23. */
#define POOL 25450

static float pool[POOL];

struct comb {
    float *buf;
    int n;
    int i;
    float store;
};

struct allpass {
    float *buf;
    int n;
    int i;
};

static struct {
    struct comb comb[2][COMBS];
    struct allpass allpass[2][ALLPASSES];
    float wet; /* the wet's gain now */
    float target; /* the wet's gain once the switch is done: WET or 0 */
    float step; /* the wet's change a frame while it moves */
    int moving; /* the switch's frames left; 0, none running */
} f;

void pf_reverb_init(void)
{
    int s, i, used = 0;

    memset(pool, 0, sizeof pool);
    for (s = 0; s < 2; s++) {
        for (i = 0; i < COMBS; i++) {
            f.comb[s][i].n = comb_len[i] + s * SPREAD;
            f.comb[s][i].buf = pool + used;
            f.comb[s][i].i = 0;
            f.comb[s][i].store = 0.0f;
            used += f.comb[s][i].n;
        }
        for (i = 0; i < ALLPASSES; i++) {
            f.allpass[s][i].n = allpass_len[i] + s * SPREAD;
            f.allpass[s][i].buf = pool + used;
            f.allpass[s][i].i = 0;
            used += f.allpass[s][i].n;
        }
    }
    f.wet = 0.0f;
    f.target = 0.0f;
    f.step = 0.0f;
    f.moving = 0;
}

void pf_reverb_set(int on)
{
    float target = on ? WET : 0.0f;

    if (target == f.target) return;
    f.target = target;
    f.step = (target - f.wet) / SWITCH;
    f.moving = SWITCH;
}

static float comb_tick(struct comb *c, float x)
{
    float y = c->buf[c->i];
    c->store = y * (1.0f - DAMP) + c->store * DAMP;
    c->buf[c->i] = x + c->store * FEEDBACK;
    if (++c->i == c->n) c->i = 0;
    return y;
}

static float allpass_tick(struct allpass *a, float x)
{
    float b = a->buf[a->i];
    float y = -x + b;
    a->buf[a->i] = x + b * ALLPASS_FEEDBACK;
    if (++a->i == a->n) a->i = 0;
    return y;
}

void pf_reverb_process(const float *in, float *left, float *right, int frames)
{
    int j, s, i;

    if (f.comb[0][0].buf == 0) pf_reverb_init();
    for (j = 0; j < frames; j++) {
        float x = in[j] * INPUT_GAIN, y[2];
        for (s = 0; s < 2; s++) {
            y[s] = 0.0f;
            for (i = 0; i < COMBS; i++) y[s] += comb_tick(&f.comb[s][i], x);
            for (i = 0; i < ALLPASSES; i++) y[s] = allpass_tick(&f.allpass[s][i], y[s]);
        }
        if (f.moving > 0) {
            f.wet = --f.moving ? f.wet + f.step : f.target;
        }
        left[j] = in[j] + y[0] * f.wet;
        right[j] = in[j] + y[1] * f.wet;
    }
}
