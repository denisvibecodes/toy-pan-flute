/* reverb.h: a listening reverb after the pan flute, Freeverb (Jezar, public domain), on or off.
 * Mono in, stereo out, 44.1 kHz, static state. The pan flute itself (panflute.h) needs none. */

#ifndef PF_REVERB_H
#define PF_REVERB_H

/* Clears every line; the reverb starts off. */
void pf_reverb_init(void);
/* Turns the reverb on (1) or off (0). The reverb runs either way; its wet moves to the new level
 * over 10 ms, so the switch does not click. */
void pf_reverb_set(int on);
/* The dry signal `in` plus the reverb's wet, on both sides. `left` and `right` may not alias
 * `in`. */
void pf_reverb_process(const float *in, float *left, float *right, int frames);

#endif
