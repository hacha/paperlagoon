/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* PaperLagoon's engine (lagoon.h). One compilation unit, -O2. The parts taken from AMB-1 (the command queue, random
 * numbers, the sine table, the slow modulators, the FM voice, the output stage, the tape echo and the plate) are in
 * amb_parts.c, plate.c and echo.c, under MPL-2.0 with their own notices; this file is the generator and the glue. */
#include "lagoon.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

#include "amb_parts.c"

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Density", 0, 100, 45}, {"Bright", 0, 100, 40}, {"Space", 0, 100, 60}, {"Echo", 0, 100, 40},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

enum { C_SET, C_PLAY, C_SEED, C_PANIC };
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }

/* state for the screen (UI) */
volatile uint8_t en_playing;
volatile float en_glow[EN_NVOICE];
volatile uint8_t en_voice_note[EN_NVOICE];
volatile uint32_t en_notes;
volatile float en_cycle;

static int16_t par[P_NPARAMS];
static float density, bright;                    /* 0..1 */
static float echo_send, rev_send, pl_target = 0.8f;

/* ------------------------------------------------------------ colours --- */
/* PLACEHOLDER colours for the FM voice (amb_parts.c): ratio, index, index fall, level fall, attack, dry/echo/plate */
enum { TB_TINE, TB_GLASS, TB_LOW, TB_N };
static const fm_patch_t TIMBRE[TB_N] = {
    {1.0f, 1.6f, 0.30f, 3.6f, 3.0f, 0.9f, 0.8f, 0.7f},     /* a tine, as an electric piano's */
    {3.5f, 0.9f, 0.90f, 5.0f, 2.0f, 0.6f, 1.0f, 0.9f},     /* glass: an inharmonic, ringing bell */
    {1.0f, 0.4f, 1.60f, 7.0f, 60.0f, 0.8f, 0.3f, 0.6f},    /* a soft low tone */
};

static void play(int tb, int note, float vel, float pan, int32_t wait)
{
    fm_patch_t pt = TIMBRE[tb];
    int v;
    pt.t60 *= fm_clampf(1.0f - (float)(note - 66) * 0.012f, 0.6f, 1.5f);   /* low notes ring longer */
    pt.idx *= (0.3f + 1.4f * bright) * (0.6f + 0.4f * vel);               /* Bright, and a harder strike */
    v = fm_start(&pt, note, vel, pan, wait);
    en_voice_note[v] = (uint8_t)note;
    en_notes++;
}

/* -------------------------------------------------- the generator --- */
/* PLACEHOLDER (lagoon.h): a motif of 3..5 notes on a major pentatonic over F, each a beat or a few long, played
 * a few times with small changes, a long rest after each pass (Density shortens it), a low tone under a pass now
 * and then, and glass answering a note now and then. A slow cycle (~90 s) leans the rests longer and shorter. */
static const int8_t PENT[5] = {0, 2, 4, 7, 9};
#define ROOT 65                                  /* F4 */
#define MOT_MAX 5
static int8_t mot_deg[MOT_MAX], mot_dur[MOT_MAX];
static int mot_len, mot_i, mot_reps;
static float beat_len = 0.8f * EN_SR;            /* 75 a minute */
static float gen_left;                           /* samples to the next event */
static float cyc_ph;                             /* the slow cycle, in turns */

static int deg_note(int d)                       /* scale degree (0 the root) to a MIDI note */
{
    int o = d >= 0 ? d / 5 : -((4 - d) / 5);
    return ROOT + 12 * o + PENT[d - 5 * o];
}

static void motif_new(void)
{
    int i, d = 3 + irand(4);
    static const int8_t DUR[6] = {1, 1, 2, 2, 3, 4};
    mot_len = 3 + irand(3);
    for (i = 0; i < mot_len; i++) {
        mot_deg[i] = (int8_t)d;
        mot_dur[i] = DUR[irand(6)];
        d += irand(2) ? 1 + irand(2) : -(1 + irand(2));
        d = d < 1 ? 1 : d > 10 ? 10 : d;
    }
    mot_reps = 2 + irand(3);
    mot_i = 0;
}

static void motif_vary(void)
{
    int i = irand(mot_len), d = mot_deg[i] + (irand(2) ? 1 : -1);
    mot_deg[i] = (int8_t)(d < 1 ? 1 : d > 10 ? 10 : d);
    if (frand() < 0.3f) {                        /* or two notes trade places in time */
        int j = irand(mot_len), t = mot_dur[i];
        mot_dur[i] = mot_dur[j];
        mot_dur[j] = (int8_t)t;
    }
}

static void gen_event(void)
{
    if (mot_i < mot_len) {
        int note = deg_note(mot_deg[mot_i]);
        float vel = 0.45f + 0.35f * frand();
        if (mot_i == 0 && frand() < 0.5f)        /* a low tone under the pass: the root or the fifth */
            play(TB_LOW, ROOT - 24 + (irand(3) ? 0 : 7), 0.5f, 0.0f, 0);
        play(TB_TINE, note, vel, -0.3f + 0.6f * frand(), irand(1500));   /* a hand: up to 34 ms late */
        if (frand() < 0.08f + 0.3f * density)    /* glass answers, an octave up, a little later */
            play(TB_GLASS, note + 12, vel * 0.5f, 0.6f * (frand() * 2.0f - 1.0f), (int32_t)(beat_len * (0.5f + frand())));
        gen_left += beat_len * (float)mot_dur[mot_i];
        mot_i++;
        return;
    }
    {   /* the pass is over: a rest, then the motif again (perhaps changed) or a new one */
        float lean = 0.5f + 0.5f * fm_sinf(cyc_ph * FM_TWO_PI);   /* the cycle: 0 short rests .. 1 long */
        gen_left += beat_len * (2.0f + (1.0f - density) * 8.0f * (0.5f + lean) + 3.0f * frand());
    }
    mot_i = 0;
    if (--mot_reps <= 0)
        motif_new();
    else if (frand() < 0.35f)
        motif_vary();
}

/* ------------------------------------------------------------ parameters --- */
static void apply(int p, int v)
{
    if (p < 0 || p >= P_NPARAMS)
        return;
    v = v < PARAMS[p].lo ? PARAMS[p].lo : v > PARAMS[p].hi ? PARAMS[p].hi : v;
    par[p] = (int16_t)v;
    switch (p) {
    case P_DENSITY: density = (float)v * 0.01f; break;
    case P_BRIGHT: bright = (float)v * 0.01f; break;
    case P_SPACE:                                /* the plate's send and size, the echo's feedback */
        rev_send = 0.15f + (float)(v * v) * 0.0001f;
        pl_target = 0.6f + 0.375f * (float)v * 0.01f;
        echo_fb = 0.5f + 0.32f * (float)v * 0.01f;
        break;
    case P_ECHO: echo_send = (float)(v * v) * 0.0001f; break;
    }
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN], a = c >> 16 & 0xFFu;
        int b = (int)(int16_t)(c & 0xFFFFu), i;
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET: apply((int)a, b); break;
        case C_PLAY:
            if (a && !en_playing)
                gen_left = 0.5f * EN_SR;
            en_playing = (uint8_t)(a != 0);
            break;
        case C_SEED:
            rng ^= (uint32_t)b * 2654435761u;
            if (!rng)
                rng = 1;
            break;
        case C_PANIC:
            for (i = 0; i < NFM; i++)
                fmv[i].on = 0;
            break;
        }
    }
}

void en_init(void)
{
    int i;
    sine_init();
    plate_init();
    echo_init();
    mods_init();
    q_r = q_w = 0;
    for (i = 0; i < NFM; i++)
        fmv[i].on = 0;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    pl_decay = pl_target;
    echo_time(beat_len * 3.0f / EN_SR);          /* three beats */
    motif_new();
    gen_left = 1.5f * EN_SR;
    cyc_ph = 0.0f;
    en_playing = 1;                              /* it plays from the start */
}

/* ------------------------------------------------------------ the render --- */
static void control(uint32_t n)                  /* once a chunk: the slow things */
{
    int i;
    mods_step(n);
    echo_control(n, 0.5f, modv, en_playing);
    plate_tick(pl_target);
    cyc_ph += (float)n * (1.0f / (90.0f * EN_SR));
    if (cyc_ph >= 1.0f)
        cyc_ph -= 1.0f;
    en_cycle = cyc_ph;
    for (i = 0; i < EN_NVOICE && i < NFM; i++)
        en_glow[i] = fmv[i].on && fmv[i].wait <= 0 ? fm_minf(fmv[i].amp / fm_maxf(fmv[i].peak, 0.01f), 1.0f) : 0.0f;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        if (en_playing) {                        /* the generator's next event lands on its sample */
            if (gen_left <= 0.0f)
                gen_event();
            if ((float)k > gen_left)
                k = (uint32_t)gen_left + 1u;
            gen_left -= (float)k;
        }
        control(k);
        mix_clear(k);
        fm_render(k, 1.6f);
        mix_out(out, k, gain, echo_send, rev_send);
        out += 2 * k;
        n -= k;
    }
}
