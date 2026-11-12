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

/* parameters, four to a page: the lagoon's, the effects' (FX), the memory's (SEQ), the environment's (ENV), the
 * cycles' (LFO), global (GLO) */
enum { P_DENSITY, P_BRIGHT, P_SPACE, P_ECHO,
       P_TIME, P_FEEDBACK, P_SIZE, P_WEAR,
       P_REPEAT, P_CHANGE, P_LENGTH, P_MEMORY,
       P_DRIFT, P_OFFER, P_TEMPO, P_TRANSPOSE,     /* the environment (ENV) */
       P_BREATH, P_TIDE, P_TURN, P_PACE,           /* the cycles (LFO) */
       P_TUNE, P_LEVEL, P_LIGHTS, P_SLEEP,         /* global (GLO): P_LIGHTS and P_SLEEP are the UI's */
       P_NPARAMS };
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
void en_audition(int role);
void en_octave(int oct);               /* the music's register: -2..2 octaves (OCT- / OCT+) */
void en_arp(int on);                   /* the pulse of broken chords (ARP) */
void en_rec(int on);                   /* REC: 1 a phrase starts (the generator waits), 0 it is taken in */
void en_rec_note(int note);            /* a key played while REC: sounds now, kept for the phrase (MIDI note) */
void en_hold(int on);                  /* hold what the music holds now (a motif, a chord, the loops' colour) */            /* one note of a voice, now (playing or resting) */

/* the hidden state (lagoon.c: what each leans) and the keys' offerings into it */
enum { S_LIGHT, S_BLEED, S_FOLD, S_MARGIN, EN_NSTATE };
void en_offer(int key, int strength);

/* worlds (ALGORITHM): places that exist nowhere; each leans the state, the beat, the roots, the cycles (lagoon.c) */
#define EN_NWORLD 5
const char *en_world_name(int world);
void en_set_world(int world);           /* there at once; only the state's lean glides */  /* key 0..26 (F3..G5); strength 100 a press, less while it is held */
extern volatile uint8_t en_arp_on, en_arp_beat;   /* the pulse on; a count of its notes (the screen's tick) */
extern volatile uint8_t en_recording;          /* a phrase is being played in */
extern volatile uint32_t en_taken;             /* phrases taken in, since boot */
extern volatile float en_cyc[3];               /* the three cycles, where each is (turns) */
extern volatile float en_beat_s;               /* the beat now (s) */
extern volatile uint8_t en_root;               /* the root now (MIDI note, before Transpose) */
extern volatile uint8_t en_held;               /* en_hold: held */
/* what the music holds now, for the screen (SEQ): a figure of up to 8 notes (scale degrees, lengths in beats; for
 * the loops each loop's note and its length in seconds, for the drone the chord's tones), the note sounding, and the
 * motifs remembered (memory) */
#define EN_FIG 8
extern volatile int8_t en_fig_deg[EN_FIG], en_fig_dur[EN_FIG];
extern volatile uint8_t en_fig_len, en_fig_pos, en_fig_scale;
extern volatile int8_t en_mem_deg[4][EN_FIG], en_mem_dur[4][EN_FIG];
extern volatile uint8_t en_mem_len[4];
enum { EN_ALG_MOTIF, EN_ALG_DRONE, EN_ALG_LOOPS, EN_ALG_LETTER, EN_ALG_FOUNTAIN };
extern volatile uint8_t en_alg;                /* the world's way of making music: EN_ALG_* (motifs, a drone, loops, a
                                                * letter's single points, a fountain's figure) */
extern volatile float en_loop_ph[4];           /* the loops (en_alg 2): each one's way round, 0..1 */

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
extern volatile uint8_t en_note_max;           /* the highest note played since boot */
extern volatile float en_period[3];            /* the three cycles' lengths now (s): they wander */
extern volatile uint32_t en_offers;
extern volatile uint32_t en_recalls;           /* motifs folded back from the memory, since boot */            /* offerings since boot (presses) */
extern volatile uint8_t en_scale;              /* the scale of the motif now (0 the coolest .. 4) */   /* R_*: which voice of the kit each FM voice plays */
