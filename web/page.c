/* page.c: what the web page's module adds to the synth: the reverb after it, and a stereo block
 * for the AudioWorklet. The page calls the pf_* functions directly for the gestures. */

#include "../src/panflute.h"
#include "../src/reverb.h"

static float stereo[2 * PF_BLOCK]; /* the left block, then the right */
static float *dry;

/* Resets the synth and the reverb. */
void page_init(void)
{
    pf_init();
    pf_reverb_init();
    dry = 0;
}

/* Turns the reverb on (1) or off (0). */
void page_reverb(int on)
{
    pf_reverb_set(on);
}

/* Renders `frames` (at most PF_BLOCK) and returns the stereo block: left at [0, PF_BLOCK),
 * right at [PF_BLOCK, 2 PF_BLOCK). */
float *page_render(int frames)
{
    if (frames > PF_BLOCK) {
        frames = PF_BLOCK;
    }
    dry = pf_render(frames);
    pf_reverb_process(dry, stereo, stereo + PF_BLOCK, frames);
    return stereo;
}

/* The last block's dry signal, the synth's own, for the level meter. */
float *page_dry(void)
{
    return dry;
}
