/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* PaperLagoon's engine: FM voices through a tape echo and a plate (both from AMB-1), played by a generator.
 *
 * STATUS: a sketch of the design (notes/concept.md in the workspace): a generator of short motifs, leaned by a
 * hidden state that slow cycles of unequal length turn and the keys' offerings move; knobs that adjust plainly.
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

/* tones: sets of three voices (kits), one chosen at a time; each voice's four settings can be edited, per kit.
 * The UI writes them (en_set_kit, en_set_tone: single bytes); the render reads them as a note starts */
#define EN_NKIT 6
enum { R_LEAD, R_ANSWER, R_LOW, EN_NROLE };
enum { T_COLOR, T_BRIGHT, T_DECAY, T_SOFT, EN_NTONE };
#define EN_NRATIO 12                   /* T_COLOR: 0..EN_NRATIO-1, the modulator's ratio (en_ratio); the rest 0..100 */
const char *en_kit_name(int kit);
const char *en_role_name(int role);
const char *en_tone_name(int field);
int en_tone_default(int kit, int role, int field);
int en_tone_hi(int field);
float en_ratio(int color);
void en_set_kit(int kit);
void en_set_tone(int kit, int role, int field, int value);
void en_voice_shape(int kit, int role, float *ratio, float *att_ms, float *t60);   /* for the screen */
void en_audition(int role);            /* one note of a voice, now (playing or resting) */

/* the hidden state (lagoon.c: what each leans) and the keys' offerings into it */
enum { S_WARM, S_WET, S_LOOSE, S_STILL, EN_NSTATE };
void en_offer(int key, int strength);  /* key 0..26 (F3..G5); strength 100 a press, less while it is held */

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
extern volatile uint8_t en_voice_role[EN_NVOICE];
extern volatile float en_state[EN_NSTATE];     /* -1..1: the hidden state, as it leans the generator now */
extern volatile float en_offset[EN_NSTATE];    /* what the offerings have moved it by (the rest: the cycles) */
extern volatile uint32_t en_offers;            /* offerings since boot (presses) */
extern volatile uint8_t en_scale;              /* the scale of the motif now (0 the coolest .. 4) */   /* R_*: which voice of the kit each FM voice plays */
