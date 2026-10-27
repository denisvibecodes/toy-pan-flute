/* config.h: the player's settings. The one file to edit.
 *
 * Two parts. The MIDI mapping holds the defaults a front end starts from; the synth never reads
 * it, the web page lets it be changed and remembers the change. Then the player's maps and
 * times, which the synth reads: how a breath value becomes a pressure, how the bend and the
 * vibrato become the lip's covering of the pipe's end, how fast each control follows. Every
 * value 0-127 is a control's value as a 7-bit controller sends it. */

#ifndef PANFLUTE_CONFIG_H
#define PANFLUTE_CONFIG_H

/* The front end's defaults ======================================================================
 *
 * Channels are 1-16, 0 for any. The controller numbers are the TEControl breath controller's:
 * breath CC 2, bite CC 1 (the bend), head tilt CC 13 (the vibrato). */

/* The keys ------------------------------------------------------------------------------------- */

#define PF_NOTES_CHANNEL 0

/* The controllers ------------------------------------------------------------------------------ */

#define PF_BREATH_CC 2
#define PF_BREATH_CHANNEL 0
#define PF_BEND_CC 1
#define PF_BEND_CHANNEL 0
#define PF_VIBRATO_CC 13
#define PF_VIBRATO_CHANNEL 0

/* The computer keyboard ------------------------------------------------------------------------ */

/* The MIDI key of Z, the lower row's C: 60 is C4. A held key blows the breath's mf marker. */
#define PF_KEYBOARD_BASE 60

/* The player's maps and times ===================================================================
 *
 * Read by the synth. Every map is piecewise linear in the control's value. */

/* The breath ----------------------------------------------------------------------------------- */

/* The breath value to the blowing pressure on the lowest pipe (G4): three levels, each a breath
 * value and its pressure, the map piecewise linear through (0, 0 Pa) and the three, beyond ff
 * with the last segment's slope. Each other pipe is blown at these times its own mf over G4's
 * (twice an octave up), so a level lands every pipe at the same share of its own mf. The levels
 * are Denis's, measured on his playing: a gentle blow (pp) at 4, his median breath with sound
 * (mf) at 12, ff at 33; beyond 33 the pressure rises by about 23 Pa a value. The page can move
 * the three values (pf_calibrate); the pressures are the instrument's. */
#define PF_BREATH_PP 4.0f
#define PF_BREATH_MF 12.0f
#define PF_BREATH_FF 33.0f
#define PF_BREATH_PP_PA 384.0f
#define PF_BREATH_MF_PA 480.0f
#define PF_BREATH_FF_PA 960.0f

/* The bend ------------------------------------------------------------------------------------- */

/* The bend value to cents below the pipe's pitch: the lower lip rolling over the open end. At
 * rest the lip already covers the end a little, by the covering that bends the pipe by
 * PF_BEND_REST_CENTS; the bend adds half PF_BEND_REACH_CENTS from PF_BEND_REST up to
 * PF_BEND_SEMITONE_LOW, holds it there to PF_BEND_SEMITONE_HIGH (a plateau, so a semitone is
 * easily found and held), adds the other half by PF_BEND_FULL and holds the whole reach above.
 * With PF_BEND_REST at 0 the covering engages from the first value. The cents are turned into
 * each pipe's own covering, so a semitone is a semitone on every pipe. */
#define PF_BEND_REST 0.0f
#define PF_BEND_SEMITONE_LOW 30.0f
#define PF_BEND_SEMITONE_HIGH 35.0f
#define PF_BEND_FULL 90.0f
/* The lip's rest, cents re the pipe uncovered. The pipes are cut in tune with the lip here
 * (coefficients.h's PF_REST_CENTS): another value tunes every pipe by the difference. Its room
 * upward is the vibrato's. */
#define PF_BEND_REST_CENTS -9.0f
/* The full bend's reach, cents re the rest: -200 is a whole tone. */
#define PF_BEND_REACH_CENTS -200.0f

/* The vibrato ---------------------------------------------------------------------------------- */

/* The vibrato value to the head's tilt in g, the accelerometer's specific force:
 * g = (value - PF_VIBRATO_CENTRE) * PF_VIBRATO_G_PER_UNIT, the TEControl's tilt as measured on
 * Denis's unit. The tilt is high-passed at 0.5 Hz, so a still head or a held lean bends nothing
 * and only the wobble is heard; the centre only keeps the numbers small. */
#define PF_VIBRATO_CENTRE 61.5f
#define PF_VIBRATO_G_PER_UNIT (1.0f / 61.0f)
/* The wobble's depth, cents per g of the high-passed tilt, added to the bend's cents: Denis's
 * wobble of about 0.2 g peak to peak is about 15 cents. 0 turns the vibrato off. */
#define PF_VIBRATO_DEPTH_CENTS_PER_G 75.0f

/* How fast the breath and the bend follow ------------------------------------------------------ */

/* A change of the breath or the bend is ramped along a line, its length by the size of the
 * step: PF_LINE_PERIOD_MS for a step of up to PF_LINE_SMALL values (the TEControl sends every
 * 10.4 ms while a sensor moves, so a swell's 7-bit stairs are joined into one line),
 * PF_LINE_FLOOR_MS from PF_LINE_LARGE values up (a tongued attack or stop keeps its edge),
 * linear in the step between. The vibrato's filter runs on a grid of PF_LINE_PERIOD_MS too. */
#define PF_LINE_PERIOD_MS 10.4f
#define PF_LINE_FLOOR_MS 4.0f
#define PF_LINE_SMALL 2.0f
#define PF_LINE_LARGE 8.0f

/* A key under a held breath is a hop: the lip lifts off the pipe it blows, the jet's share there
 * falling to 0 over PF_HOP_LIFT_MS, then lands on the new pipe, its share rising from 0 to the
 * whole jet over PF_HOP_ATTACK_MS. A key with no breath moves the lip at once. */
#define PF_HOP_LIFT_MS 20.0f
#define PF_HOP_ATTACK_MS 20.0f

#endif
