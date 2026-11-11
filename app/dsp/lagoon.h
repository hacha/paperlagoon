/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* PaperLagoon's engine: FM voices through a tape echo and a plate (both from AMB-1), played by a generator.
 *
 * STATUS: the first sketch. The generator is a PLACEHOLDER (a short motif on a pentatonic scale, repeated with
 * small changes and long rests, now and then a low tone under it): it exists so that the sound can be heard and
 * judged, not as the design. The design (notes/concept.md in the workspace): hidden state, slow cycles of unequal
 * length, keys that offer something to the state rather than play notes, knobs that adjust.
 *
 * Threads, as AMB-1's: en_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state marked (UI) is written by the render and only read elsewhere.
 * Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NVOICE 8                    /* FM voices */

/* parameters (the four knobs, for now; placeholders too) */
enum { P_DENSITY, P_BRIGHT, P_SPACE, P_ECHO, P_NPARAMS };
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int on);
void en_seed(uint32_t s);              /* stir some entropy into the generator */
void en_panic(void);                   /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen and the tests (UI) */
extern volatile uint8_t en_playing;
extern volatile float en_glow[EN_NVOICE];      /* 0..1: each voice's level, for the screen */
extern volatile uint8_t en_voice_note[EN_NVOICE];
extern volatile uint32_t en_notes;             /* notes started since boot */
extern volatile float en_cycle;                /* 0..1: where the slow cycle is (the screen's breath) */
