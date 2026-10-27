/* panflute.h: the pan flute's interface. One instrument, static state, mono, 44.1 kHz.
 *
 * A front end (the web page, the native harness, a firmware) calls pf_init once, then feeds
 * the gestures and pulls blocks of samples. Every gesture is applied at the first sample of the
 * next block. The synth never reads MIDI: the front end maps its controllers onto the three
 * named controls, with the defaults of config.h. */

#ifndef PANFLUTE_H
#define PANFLUTE_H

/* The most frames one pf_render call returns. */
#define PF_BLOCK 128

/* The three controls, each 0-127 as a 7-bit controller sends it. */
enum pf_control {
    PF_BREATH = 0,  /* the blowing pressure */
    PF_BEND = 1,    /* the lip covering the pipe's end, bending the pitch down */
    PF_VIBRATO = 2, /* the head's tilt, wobbling the covering */
    PF_CONTROLS = 3
};

/* The front end's defaults from config.h, read by pf_config. */
enum pf_config_item {
    PF_CFG_NOTES_CHANNEL = 0,   /* the keys' MIDI channel, 1-16, 0 for any */
    PF_CFG_BREATH_CC = 1,       /* the breath's controller number */
    PF_CFG_BREATH_CHANNEL = 2,  /* its channel, 1-16, 0 for any */
    PF_CFG_BEND_CC = 3,
    PF_CFG_BEND_CHANNEL = 4,
    PF_CFG_VIBRATO_CC = 5,
    PF_CFG_VIBRATO_CHANNEL = 6,
    /* 7 is unused (it was the computer keyboard's breath, now the mf marker) */
    PF_CFG_KEYBOARD_BASE = 8,   /* the MIDI key of the computer keyboard's Z */
    PF_CFG_ITEMS = 9
};

/* The player's calibration: config.h's values until changed. The breath map runs through
 * (0, 0 Pa), (pp, 384 Pa), (mf, 480 Pa), (ff, 960 Pa) on G4, past ff with its last slope; the
 * bend's markers are as config.h's PF_BEND_*. */
enum pf_calibration {
    PF_CAL_BREATH_PP = 0,          /* the breath value of pp (384 Pa at G4) */
    PF_CAL_BREATH_MF = 1,          /* of mf (480 Pa) */
    PF_CAL_BREATH_FF = 2,          /* of ff (960 Pa) */
    PF_CAL_BEND_REST = 3,          /* the bend's markers */
    PF_CAL_BEND_SEMITONE_LOW = 4,
    PF_CAL_BEND_SEMITONE_HIGH = 5,
    PF_CAL_BEND_FULL = 6,
    PF_CAL_VIBRATO_DEPTH = 7,      /* cents per g of the high-passed tilt */
    PF_CAL_ITEMS = 8
};

/* What the synth is doing at the end of the last block, for a front end to show. */
enum pf_state_item {
    PF_STATE_KEY = 0,             /* the lip's pipe, MIDI key, -1 for none */
    PF_STATE_PRESSURE_PA = 1,     /* the blowing pressure on the lip's pipe, Pa, as ramped */
    PF_STATE_PRESSURE_G4_PA = 2,  /* the same breath's pressure on G4, pf_map's own scale */
    PF_STATE_BEND_CENTS = 3,      /* the bend's cents re the lip at rest (below) */
    PF_STATE_VIBRATO_G = 4,       /* the high-passed tilt, g */
    PF_STATE_VIBRATO_CENTS = 5,   /* the vibrato's cents added to the bend */
    PF_STATE_COVERING = 6,        /* the lip's covering of the pipe's end, as ramped (below) */
    PF_STATE_ITEMS = 7
};
/* PF_STATE_BEND_CENTS: 0 at rest, where the pipes are in tune, the vibrato excluded; read off
 * the lip's covering as ramped by the bend's estimate, less the vibrato's cents, so it follows
 * the ramps.
 * PF_STATE_COVERING: 0 with the lip resting on the rim, 1 with it over the whole diameter: the
 * share of the way from the resting lip's overlap (35 % of the diameter) to the far rim, held
 * under 0.85, where 5 % of the end is left open. */

/* Resets every pipe and gesture to silence, and the calibration to config.h's. */
void pf_init(void);
/* A key struck: the lip moves to that pipe (MIDI key 67-91; others are ignored). */
void pf_note_on(int key);
/* A key released. The breath ends a note, so this moves nothing; it is here for front ends. */
void pf_note_off(int key);
/* A control's new value, 0-127. */
void pf_control(int control, int value);
/* Renders `frames` samples (at most PF_BLOCK) and returns them; the buffer is the synth's own
 * and holds until the next call. */
float *pf_render(int frames);
/* One of config.h's front-end defaults, or -1 for an unknown item. */
int pf_config(int item);
/* A calibration item's current value, or -1 for an unknown item. */
float pf_calibration(int item);
/* Sets a calibration item from the next block on: 1 if taken; 0, nothing changed, when the
 * value is out of range or breaks the order 0 < pp < mf < ff <= 127,
 * 0 <= rest < semitone low <= semitone high < full <= 127, 0 <= depth <= 300. A moved marker
 * retargets the control's ramp along the sensor's line, so nothing clicks. */
int pf_calibrate(int item, float value);
/* The maps, for a front end to draw: PF_BREATH to Pa on G4; PF_BEND to cents re the lip at
 * rest, where the pipes are in tune (0 at rest, -200 at full); PF_VIBRATO (and anything else)
 * to 0. `value` is 0-127. */
float pf_map(int control, float value);
/* One item of what the synth is doing, or 0 for an unknown item. */
float pf_state(int item);

#endif
