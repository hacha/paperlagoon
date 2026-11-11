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

enum { C_SET, C_PLAY, C_SEED, C_PANIC, C_AUDITION, C_OFFER };
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_audition(int role) { post(C_AUDITION, role, 0); }
void en_offer(int key, int strength) { post(C_OFFER, key, strength); }

/* state for the screen (UI) */
volatile uint8_t en_playing;
volatile float en_glow[EN_NVOICE];
volatile uint8_t en_voice_note[EN_NVOICE];
volatile uint32_t en_notes;
volatile float en_cycle;

static int16_t par[P_NPARAMS];
static float density, bright;                    /* 0..1 */
static float echo_send, rev_send, pl_target = 0.8f;

/* ------------------------------------------------------------ tones --- */
/* the modulator's ratios, T_COLOR's steps: whole numbers (clear, as a reed or a tine), then between (bells, metal) */
static const float RATIO[EN_NRATIO] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f, 1.41f, 2.76f, 3.5f, 4.24f, 5.19f};

/* one voice of a kit: the ratio (RATIO's step), the index at the strike (radians) and its fall to -60 dB (s), the
 * level's fall (s), the attack (T_SOFT's scale: 0 = 1 ms .. 100 = 3 s), to the mix, the echo, the plate */
typedef struct {
    uint8_t color;
    float idx, idx_t, t60;
    uint8_t soft;
    float dry, esend, rsend;
} voice_t;
typedef struct {
    const char *name;
    float gain;                                  /* the kit's level: short voices are quieter */
    voice_t v[EN_NROLE];                         /* R_LEAD, R_ANSWER, R_LOW */
} kit_t;
static const kit_t KITS[EN_NKIT] = {
    {"PAPER LAGOON", 1.0f, {{1, 1.6f, 0.30f, 3.6f, 14, 0.9f, 0.8f, 0.7f},      /* a tine, as an electric piano's */
                      {9, 0.9f, 0.90f, 5.0f, 9, 0.6f, 1.0f, 0.9f},       /* glass, an inharmonic bell */
                      {1, 0.4f, 1.60f, 7.0f, 51, 0.8f, 0.3f, 0.6f}}},    /* a soft low tone */
    {"WOODEN RAIN", 1.7f, {{4, 2.2f, 0.05f, 1.6f, 0, 0.9f, 0.6f, 0.5f},        /* a bar of wood, struck */
                     {1, 0.5f, 1.00f, 4.5f, 9, 0.6f, 1.0f, 0.8f},        /* a vibraphone's bar */
                     {2, 0.6f, 0.30f, 3.0f, 17, 0.8f, 0.3f, 0.5f}}},     /* a low, hollow log */
    {"GLASS ORCHARD", 1.0f, {{3, 1.1f, 0.15f, 2.8f, 0, 0.9f, 0.8f, 0.7f},      /* a celesta */
                       {11, 0.7f, 1.50f, 7.0f, 14, 0.5f, 1.0f, 1.0f},    /* a thin glass bowl */
                       {2, 0.3f, 2.00f, 8.0f, 55, 0.7f, 0.3f, 0.7f}}},   /* a held low glass */
    {"SLOW BREATH", 1.0f, {{1, 0.7f, 3.00f, 5.0f, 73, 0.8f, 0.7f, 0.8f},       /* a breath, almost a flute */
                     {2, 0.5f, 4.00f, 6.0f, 82, 0.5f, 0.9f, 1.0f},       /* an octave's breath, slower */
                     {0, 0.3f, 5.00f, 10.0f, 89, 0.8f, 0.3f, 0.7f}}},    /* a low swell */
    {"IRON SNOW", 0.9f, {{7, 1.2f, 0.60f, 6.0f, 9, 0.7f, 0.9f, 0.9f},          /* a struck plate of iron */
                   {8, 0.8f, 2.00f, 9.0f, 17, 0.5f, 1.0f, 1.0f},         /* a far, wide bell */
                   {1, 0.5f, 3.00f, 9.0f, 46, 0.8f, 0.3f, 0.6f}}},       /* a low hum */
    {"HAZE ARCHIVE", 1.0f, {{1, 0.9f, 6.00f, 9.0f, 85, 0.7f, 0.8f, 1.0f},      /* a slow, soft chord tone */
                      {3, 0.4f, 6.00f, 10.0f, 91, 0.4f, 1.0f, 1.0f},     /* a haze above it */
                      {0, 0.5f, 8.00f, 12.0f, 95, 0.7f, 0.3f, 0.8f}}},   /* a low haze */
};
static const char *const ROLE_NAME[EN_NROLE] = {"LEAD", "ANSWER", "LOW"};
static const char *const TONE_NAME[EN_NTONE] = {"Color", "Bright", "Decay", "Soft"};

static volatile uint8_t kit;
static volatile int8_t tone[EN_NKIT][EN_NROLE][EN_NTONE];   /* the edits (en_tone_default: as KITS has them) */
volatile uint8_t en_voice_role[EN_NVOICE];
volatile uint8_t en_note_max;

const char *en_kit_name(int k) { return KITS[(unsigned)k % EN_NKIT].name; }
const char *en_role_name(int r) { return ROLE_NAME[(unsigned)r % EN_NROLE]; }
const char *en_tone_name(int f) { return TONE_NAME[(unsigned)f % EN_NTONE]; }
int en_tone_hi(int f) { return f == T_COLOR ? EN_NRATIO - 1 : 100; }
float en_ratio(int c) { return RATIO[c < 0 ? 0 : c >= EN_NRATIO ? EN_NRATIO - 1 : c]; }
int en_tone_default(int k, int r, int f)
{
    const voice_t *v = &KITS[(unsigned)k % EN_NKIT].v[(unsigned)r % EN_NROLE];
    return f == T_COLOR ? v->color : f == T_SOFT ? v->soft : 50;   /* Bright and Decay: 50, as the kit has them */
}
void en_set_kit(int k) { kit = (uint8_t)((unsigned)k % EN_NKIT); }
void en_set_tone(int k, int r, int f, int v)
{
    if (k < 0 || k >= EN_NKIT || r < 0 || r >= EN_NROLE || f < 0 || f >= EN_NTONE)
        return;
    tone[k][r][f] = (int8_t)(v < 0 ? 0 : v > en_tone_hi(f) ? en_tone_hi(f) : v);
}

/* a kit's voice, as edited: Bright and Decay scale the kit's by 1/4..4 (50: as it is); Soft is the attack */
static void voice_patch(int k, int role, fm_patch_t *pt)
{
    const voice_t *b = &KITS[k].v[role];
    const volatile int8_t *e = tone[k][role];
    pt->ratio = en_ratio(e[T_COLOR]);
    pt->idx = b->idx * fm_exp2f((float)(e[T_BRIGHT] - 50) * 0.04f);
    pt->idx_t = b->idx_t;
    pt->t60 = b->t60 * fm_exp2f((float)(e[T_DECAY] - 50) * 0.04f);
    pt->att_ms = fm_exp2f((float)e[T_SOFT] * 0.1155f);   /* 2^(100 * 0.1155) = 3000 ms */
    pt->dry = b->dry;
    pt->esend = b->esend;
    pt->rsend = b->rsend;
}

void en_voice_shape(int k, int role, float *ratio, float *att_ms, float *t60)
{
    fm_patch_t pt;
    voice_patch((int)((unsigned)k % EN_NKIT), (int)((unsigned)role % EN_NROLE), &pt);
    *ratio = pt.ratio;
    *att_ms = pt.att_ms;
    *t60 = pt.t60;
}

static void play(int role, int note, float vel, float pan, int32_t wait)
{
    fm_patch_t pt;
    int v;
    voice_patch(kit, role, &pt);
    pt.t60 *= fm_clampf(1.0f - (float)(note - 66) * 0.012f, 0.6f, 1.5f);   /* low notes ring longer */
    pt.idx *= (0.3f + 1.4f * bright) * (0.6f + 0.4f * vel);               /* Bright, and a harder strike */
    v = fm_start(&pt, note, vel * KITS[kit].gain, pan, wait);
    en_voice_note[v] = (uint8_t)note;
    en_voice_role[v] = (uint8_t)role;
    if (note > en_note_max)
        en_note_max = (uint8_t)note;
    en_notes++;
}

/* -------------------------------------------------- the hidden state --- */
/* Four quantities nobody sees, each -1..1, 0 neutral. They lean the generator, never the knobs (the knobs adjust,
 * plainly; these only bend what the generator chooses, within bounds):
 *   S_WARM   up: a brighter mode, a higher register, the low voice less often
 *   S_WET    up: longer notes, the answer more often
 *   S_LOOSE  up: wider steps, more changes, new motifs sooner, the root may wander
 *   S_STILL  up: longer rests, a slower beat
 * Three slow cycles of unequal length move their resting point (nothing done, the place still turns), and the
 * keys' offerings move them further: an offering falls into a reservoir, which seeps into the state over seconds
 * (one quantity bleeding a little into the next), and what it moved relaxes back over minutes. A key gives a mix
 * across the four (its register, white or black, its pitch, and chance), so no key does one thing. */
#define CYC_A 71.0f                              /* the cycles, in seconds: no two share a beat for a long time */
#define CYC_B 163.0f
#define CYC_C 409.0f
#define SEEP_S 9.0f                              /* the reservoir's seepage into the state */
#define FADE_S 150.0f                            /* the state's return, once moved */
volatile float en_state[EN_NSTATE];
volatile float en_offset[EN_NSTATE];
volatile uint32_t en_offers;
static float pend[EN_NSTATE], off[EN_NSTATE], st[EN_NSTATE];
static float cyc[3];                             /* the cycles' phases, in turns */
static int8_t gift_pc = -1;                      /* the last offered key's pitch class (-1 none), for a motif */

/* each pitch class's lean across WARM, WET, LOOSE, STILL: a fixed, uneven mix */
static const int8_t PC_MIX[12][EN_NSTATE] = {
    {3, 2, -1, 2}, {-1, 3, 3, -2}, {2, -1, 1, 3}, {1, 3, 2, -1}, {3, -1, -2, 2}, {-2, 2, 1, 3},
    {1, -2, 3, 1}, {3, 1, -1, -1}, {-1, 3, -2, 2}, {2, 2, 1, -2}, {1, -1, 3, 2}, {2, 1, -2, 3},
};

static void offer(int key, int strength)         /* key 0..26 (F3..G5), strength 0..100 */
{
    int i, pc = (key + 5) % 12;
    int black = (0x54A >> pc) & 1;               /* C# D# F# G# A# */
    float m = 0.17f * (float)strength * 0.01f, q[EN_NSTATE];
    q[S_WARM] = (float)(key - 13) * (1.0f / 13.0f) * 1.4f;     /* high keys warm, low keys cool */
    q[S_WET] = 0.0f;
    q[S_LOOSE] = black ? 0.8f : -0.2f;          /* black keys loosen */
    q[S_STILL] = black ? -0.3f : 0.4f;
    for (i = 0; i < EN_NSTATE; i++)
        pend[i] += m * (q[i] + (float)PC_MIX[pc][i] * 0.25f + 0.5f * (frand() * 2.0f - 1.0f));
    if (strength >= 100) {
        gift_pc = (int8_t)pc;
        en_offers++;
    }
}

static void state_step(uint32_t n)
{
    static const float AMP[EN_NSTATE] = {0.45f, 0.4f, 0.35f, 0.5f};
    float seep = (float)n * (1.0f / (SEEP_S * EN_SR)), fade = (float)n * (1.0f / (FADE_S * EN_SR));
    float base[EN_NSTATE];
    int i;
    cyc[0] += (float)n * (1.0f / (CYC_A * EN_SR));
    cyc[1] += (float)n * (1.0f / (CYC_B * EN_SR));
    cyc[2] += (float)n * (1.0f / (CYC_C * EN_SR));
    for (i = 0; i < 3; i++)
        if (cyc[i] >= 1.0f)
            cyc[i] -= 1.0f;
    base[S_STILL] = fm_sinf(cyc[0] * FM_TWO_PI);                     /* the breath: rests long and short */
    base[S_WARM] = fm_sinf(cyc[1] * FM_TWO_PI);                      /* the tide */
    base[S_WET] = fm_sinf((cyc[1] + cyc[2]) * FM_TWO_PI);
    base[S_LOOSE] = fm_sinf(cyc[2] * FM_TWO_PI);                     /* the long turn */
    for (i = 0; i < EN_NSTATE; i++) {
        float f = pend[i] * seep;
        pend[i] -= f;
        off[i] += 0.85f * f;
        off[(i + 1) % EN_NSTATE] += 0.15f * f;   /* a little bleeds into the next */
        off[i] -= off[i] * fade;
        off[i] = fm_clampf(off[i], -1.0f, 1.0f);
        st[i] = fm_tanhf(AMP[i] * base[i] + off[i]);   /* soft at the ends: never pinned */
        en_state[i] = st[i];
        en_offset[i] = off[i];
    }
}

/* -------------------------------------------------- the generator --- */
/* A motif of 3..5 notes on a scale over a root, each a beat or a few long, played a few times with small changes,
 * a rest after each pass (Density shortens it, S_STILL lengthens it), a low tone under a pass now and then, the
 * answer an octave up now and then. A new motif takes its scale from the state (S_WARM: darker .. brighter; S_LOOSE
 * lets in the fourth and seventh), and sometimes the pitch of the last offering, changed into the scale. */
#define NSCALE 5
static const int8_t SCALE[NSCALE][8] = {         /* the length first, then the steps over the root */
    {5, 0, 3, 5, 7, 10},                         /* minor pentatonic: the coolest */
    {5, 0, 2, 5, 7, 9},                          /* the yo scale */
    {5, 0, 2, 4, 7, 9},                          /* major pentatonic */
    {6, 0, 2, 4, 7, 9, 11},                      /* with the seventh */
    {7, 0, 2, 4, 6, 7, 9, 11},                   /* lydian: the brightest, the loosest */
};
static const int8_t ROOTS[4] = {65, 60, 70, 67};   /* F4, then C, B flat, G: a fourth or a fifth away */
#define MOT_MAX 5
static int8_t mot_deg[MOT_MAX], mot_dur[MOT_MAX];
static int mot_len, mot_i, mot_reps, scale_i = 2, root = 65, root_wait;
static float beat_len = 0.8f * EN_SR;            /* 75 a minute (S_STILL: up to 15 % either way) */
static float gen_left;                           /* samples to the next event */
volatile uint8_t en_scale;

static int deg_note(int d)                       /* scale degree (0 the root) to a MIDI note */
{
    int n = SCALE[scale_i][0], o = d >= 0 ? d / n : -((n - 1 - d) / n);
    return root + 12 * o + SCALE[scale_i][1 + d - n * o];
}

static int pc_degree(int pc)                     /* a pitch class to the nearest degree of the scale, low octave */
{
    int n = SCALE[scale_i][0], i, best = 0, bd = 99, rel = ((pc - root) % 12 + 12) % 12;
    for (i = 0; i < n; i++) {
        int dd = SCALE[scale_i][1 + i] - rel;
        dd = dd < 0 ? -dd : dd;
        dd = dd > 6 ? 12 - dd : dd;
        if (dd < bd) {
            bd = dd;
            best = i;
        }
    }
    return best;
}

static void motif_new(void)
{
    static const int8_t DUR[6] = {1, 1, 2, 2, 3, 4};
    int i, n, d, lo, hi, step = st[S_LOOSE] > 0.3f ? 3 : 2;
    float x = 2.0f + 2.4f * st[S_WARM] + (st[S_LOOSE] > 0.4f ? 0.7f : 0.0f) + 0.6f * (frand() - 0.5f);
    scale_i = x < 0.0f ? 0 : x > (float)(NSCALE - 1) ? NSCALE - 1 : (int)(x + 0.5f);
    en_scale = (uint8_t)scale_i;
    if (--root_wait <= 0 && frand() < 0.12f + 0.25f * fm_maxf(st[S_LOOSE], 0.0f)) {   /* the root wanders */
        root = ROOTS[irand(4)];
        root_wait = 3;
    }
    n = SCALE[scale_i][0];
    lo = n / 5;                                  /* the register: a degree range of two octaves, raised by warmth */
    hi = 2 * n;
    d = (int)((float)n * (0.6f + 0.35f * st[S_WARM])) + irand(n * 4 / 5 + 1);
    if (gift_pc >= 0 && frand() < 0.6f) {        /* the offering, come back changed */
        d = pc_degree(gift_pc) + n * (1 + irand(2));
        gift_pc = -1;
    }
    mot_len = 3 + irand(3);
    for (i = 0; i < mot_len; i++) {
        int k = irand(6);
        d = d < lo ? lo : d > hi ? hi : d;
        mot_deg[i] = (int8_t)d;
        if (st[S_WET] > 0.2f && frand() < st[S_WET])
            k = k < 5 ? k + 1 : 5;               /* wet: longer notes */
        mot_dur[i] = DUR[k];
        d += irand(2) ? 1 + irand(step) : -(1 + irand(step));
    }
    mot_reps = 2 + irand(3) - (st[S_LOOSE] > 0.5f ? 1 : 0);
    mot_i = 0;
}

static void motif_vary(void)
{
    int n = SCALE[scale_i][0], i = irand(mot_len), d = mot_deg[i] + (irand(2) ? 1 : -1);
    mot_deg[i] = (int8_t)(d < n / 5 ? n / 5 : d > 2 * n ? 2 * n : d);
    if (frand() < 0.3f) {                        /* or two notes trade places in time */
        int j = irand(mot_len), t = mot_dur[i];
        mot_dur[i] = mot_dur[j];
        mot_dur[j] = (int8_t)t;
    }
}

static void gen_event(void)
{
    if (mot_i < mot_len) {
        int note = deg_note(mot_deg[mot_i]), ans;
        float vel = 0.45f + 0.35f * frand();
        while (note > 89)                        /* the ceiling: the lead to F6, the answer to A6 */
            note -= 12;
        for (ans = note + 12; ans > 93; ans -= 12)
            ;
        if (mot_i == 0 && frand() < 0.5f - 0.25f * st[S_WARM])   /* a low tone under the pass: root or fifth */
            play(R_LOW, root - 24 + (irand(3) ? 0 : 7), 0.5f, 0.0f, 0);
        play(R_LEAD, note, vel, -0.3f + 0.6f * frand(), irand(1500));   /* a hand: up to 34 ms late */
        if (frand() < 0.08f + 0.3f * density + 0.12f * st[S_WET])   /* the answer, an octave up, a little later */
            play(R_ANSWER, ans, vel * 0.5f, 0.6f * (frand() * 2.0f - 1.0f), (int32_t)(beat_len * (0.5f + frand())));
        gen_left += beat_len * (float)mot_dur[mot_i];
        mot_i++;
        return;
    }
    {   /* the pass is over: a rest, then the motif again (perhaps changed) or a new one */
        float lean = 0.5f + 0.5f * st[S_STILL];  /* 0 short rests .. 1 long */
        gen_left += beat_len * (2.0f + (1.0f - density) * 8.0f * (0.5f + lean) + 3.0f * frand());
        beat_len = 0.8f * EN_SR * (1.0f + 0.15f * st[S_STILL]);
    }
    mot_i = 0;
    if (--mot_reps <= 0)
        motif_new();
    else if (frand() < 0.35f + 0.3f * fm_maxf(st[S_LOOSE], 0.0f))
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
        case C_OFFER:
            if (a < 27)
                offer((int)a, b);
            break;
        case C_AUDITION:                         /* a note where the voice usually sounds */
            if (a < EN_NROLE)
                play((int)a, a == R_LOW ? root - 24 : a == R_ANSWER ? root + 19 : root + 7, 0.7f, 0.0f, 0);
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
    for (i = 0; i < EN_NKIT * EN_NROLE * EN_NTONE; i++)
        tone[i / (EN_NROLE * EN_NTONE)][i / EN_NTONE % EN_NROLE][i % EN_NTONE] =
            (int8_t)en_tone_default(i / (EN_NROLE * EN_NTONE), i / EN_NTONE % EN_NROLE, i % EN_NTONE);
    kit = 0;
    pl_decay = pl_target;
    echo_time(beat_len * 3.0f / EN_SR);          /* three beats */
    for (i = 0; i < EN_NSTATE; i++)
        pend[i] = off[i] = st[i] = 0.0f;
    cyc[0] = cyc[1] = cyc[2] = 0.0f;
    gift_pc = -1;
    root = ROOTS[0];
    root_wait = 2;
    motif_new();
    gen_left = 1.5f * EN_SR;
    en_playing = 1;                              /* it plays from the start */
}

/* ------------------------------------------------------------ the render --- */
static void control(uint32_t n)                  /* once a chunk: the slow things */
{
    int i;
    mods_step(n);
    echo_control(n, 0.5f, modv, en_playing);
    plate_tick(pl_target);
    state_step(n);
    en_cycle = cyc[0];
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
