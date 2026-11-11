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
    {"Time", 0, 100, 42}, {"Feedback", 0, 100, 68}, {"Size", 0, 100, 69}, {"Wear", 0, 100, 20},
    {"Repeat", 0, 100, 50}, {"Change", 0, 100, 50}, {"Length", 0, 100, 50}, {"Memory", 0, 100, 50},
    {"Drift", 0, 100, 50}, {"Offer", 0, 100, 50}, {"Tempo", 0, 100, 50}, {"Transpose", -12, 12, 0},
    {"Breath", 0, 100, 50}, {"Tide", 0, 100, 50}, {"Turn", 0, 100, 50}, {"Pace", 0, 100, 50},
    {"Tune", -50, 50, 0}, {"Level", 0, 100, 70}, {"Lights", 0, 2, 1}, {"Sleep", 0, 120, 0},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

enum { C_SET, C_PLAY, C_SEED, C_PANIC, C_AUDITION, C_OFFER, C_WORLD, C_HOLD, C_OCT, C_ARP, C_REC, C_REC_NOTE };
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_audition(int role) { post(C_AUDITION, role, 0); }
void en_offer(int key, int strength) { post(C_OFFER, key, strength); }
void en_set_world(int w) { post(C_WORLD, w, 0); }
void en_hold(int on) { post(C_HOLD, on, 0); }
void en_octave(int o) { post(C_OCT, o + 2, 0); }
void en_arp(int on) { post(C_ARP, on, 0); }
void en_rec(int on) { post(C_REC, on, 0); }
void en_rec_note(int note) { post(C_REC_NOTE, note, 0); }

/* state for the screen (UI) */
volatile uint8_t en_playing;
volatile float en_glow[EN_NVOICE];
volatile uint8_t en_voice_note[EN_NVOICE];
volatile uint32_t en_notes;
volatile float en_cycle;

static int16_t par[P_NPARAMS];
static float density, bright;                    /* 0..1 */
static float echo_send, rev_send, pl_target = 0.8f;
static float repeat = 0.5f, change = 0.5f, length = 0.5f, memory = 0.5f;   /* SEQ: 0..1 */
static float echo_secs = 2.4f;
static float drift = 1.0f, offer_k = 1.0f, tempo = 1.0f, depth[3] = {1.0f, 1.0f, 1.0f}, pace = 1.0f, level = 1.0f;
static int transpose, music_oct, raw_note;
volatile uint8_t en_held;
volatile float en_cyc[3], en_beat_s;
volatile uint8_t en_root;
volatile int8_t en_fig_deg[EN_FIG], en_fig_dur[EN_FIG];
volatile uint8_t en_fig_len, en_fig_pos, en_fig_scale;
volatile int8_t en_mem_deg[4][EN_FIG], en_mem_dur[4][EN_FIG];
volatile uint8_t en_mem_len[4];

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

/* a note of the kit's voice; t60x and att_min reshape it (a drone stretches any kit's voice into a swell) */
static void play_shaped(int role, int note, float vel, float pan, int32_t wait, float t60x, float att_min)
{
    fm_patch_t pt;
    int v;
    voice_patch(kit, role, &pt);
    if (!raw_note) {                             /* the music's register (OCT) and Transpose; a played note as is */
        note += transpose + 12 * music_oct;
        while (note > 98)
            note -= 12;
        while (note < 28)
            note += 12;
    }
    pt.t60 *= t60x;
    pt.att_ms = fm_maxf(pt.att_ms, att_min);
    pt.t60 *= fm_clampf(1.0f - (float)(note - 66) * 0.012f, 0.6f, 1.5f);   /* low notes ring longer */
    pt.idx *= (0.3f + 1.4f * bright) * (0.6f + 0.4f * vel);               /* Bright, and a harder strike */
    v = fm_start(&pt, note, vel * KITS[kit].gain, pan, wait);
    en_voice_note[v] = (uint8_t)note;
    en_voice_role[v] = (uint8_t)role;
    if (note > en_note_max)
        en_note_max = (uint8_t)note;
    en_notes++;
}

static void play(int role, int note, float vel, float pan, int32_t wait)
{
    play_shaped(role, note, vel, pan, wait, 1.0f, 0.0f);
}

/* ------------------------------------------------------------ worlds --- */
/* Places that exist nowhere (ALGORITHM). Each makes its music its own way (the algorithm: motifs, a drone, loops),
 * leans the hidden state from its own resting point, and sets the beat, the roots the music wanders among, how fast
 * the cycles turn and where the answer sounds. It never touches the knobs nor the kit. A change is heard at once:
 * the new world starts within a moment (what sounds rings out); only the state's lean glides. World 0 is the music
 * as it was before there were worlds. */
enum { ALG_MOTIF, ALG_DRONE, ALG_LOOPS };
typedef struct {
    const char *name;
    float bias[EN_NSTATE];                       /* the state's resting lean: light, bleed, fold, margin */
    float beat;                                  /* seconds */
    int8_t roots[4];                             /* the first is where it starts */
    float cycles;                                /* the cycles' lengths, times this */
    int8_t answer;                               /* the answer, this far over the lead (semitones) */
    uint8_t alg;                                 /* ALG_* */
} world_t;
static const world_t WORLDS[EN_NWORLD] = {
    {"SHALLOWS OF MEMORY", {0.0f, 0.0f, 0.0f, 0.0f}, 0.80f, {65, 60, 70, 67}, 1.0f, 12, ALG_MOTIF},        /* 記憶の浅瀬 */
    {"BEHIND THE HORIZON", {-0.35f, 0.2f, 0.0f, 0.3f}, 0.95f, {58, 60, 63, 55}, 1.4f, 7, ALG_DRONE},        /* 水平線の裏側 */
    {"THE UNREAD LETTER", {0.0f, -0.2f, 0.55f, 0.6f}, 0.85f, {62, 67, 69, 64}, 1.0f, 12, ALG_MOTIF},       /* 誰も読まない手紙 */
    {"SILENT FOUNTAIN", {0.35f, 0.35f, 0.0f, -0.3f}, 0.68f, {67, 72, 65, 70}, 0.65f, 19, ALG_MOTIF},       /* 音のしない噴水 */
    {"WHERE LIGHT BECOMES MOSS", {0.2f, 0.4f, -0.3f, 0.1f}, 0.90f, {64, 69, 62, 67}, 2.0f, 12, ALG_LOOPS}, /* 光が苔になるところ */
};
#define WORLD_GLIDE_S 20.0f
static volatile uint8_t world;
volatile uint8_t en_alg;
static float wbias[EN_NSTATE], wcycles = 1.0f;   /* the lean and the cycles' scale, gliding to the world's */
const char *en_world_name(int w) { return WORLDS[(unsigned)w % EN_NWORLD].name; }

/* -------------------------------------------------- the hidden state --- */
/* Four quantities nobody sees (named for paper: they are never shown), each -1..1, 0 neutral. They lean the
 * generator, never the knobs (the knobs adjust, plainly; these only bend what the generator chooses, within bounds):
 *   S_LIGHT   light:  a brighter mode, a higher register, the low voice less often
 *   S_BLEED   bleed:  longer notes, the answer more often
 *   S_FOLD    fold:   wider steps, more changes, the root may wander, and an older motif folds back
 *   S_MARGIN  margin: longer rests, a slower beat, fewer notes to a motif
 * Three slow cycles of unequal length move their resting point (nothing done, the place still turns), and the
 * keys' offerings move them further: an offering falls into a reservoir, which seeps into the state over seconds
 * (one quantity bleeding a little into the next), and what it moved relaxes back over minutes. A key gives a mix
 * across the four (its register, white or black, its pitch, and chance; the register's lean turning with the long
 * cycle), so no key does one thing, nor the same thing for long. */
/* the cycles' lengths wander: each turn ends with a new length to drift toward, within its own range (the ranges
 * never overlap, so the three stay unequal; the shortest, the breath, never under 50 s) */
static const float PER_LO[3] = {50.0f, 120.0f, 300.0f}, PER_HI[3] = {100.0f, 230.0f, 600.0f};
static float per[3], per_to[3];                  /* the lengths now, and where they drift (s) */
#define PER_DRIFT_S 60.0f
volatile float en_period[3];
#define SEEP_S 9.0f                              /* the reservoir's seepage into the state */
#define FADE_S 150.0f                            /* the state's return, once moved */
volatile float en_state[EN_NSTATE];
volatile float en_offset[EN_NSTATE];
volatile uint32_t en_offers;
static float pend[EN_NSTATE], off[EN_NSTATE], st[EN_NSTATE];
static float cyc[3];                             /* the cycles' phases, in turns */
static int8_t gift_pc = -1;                      /* the last offered key's pitch class (-1 none), for a motif */

/* each pitch class's lean across LIGHT, BLEED, FOLD, MARGIN: a fixed, uneven mix */
static const int8_t PC_MIX[12][EN_NSTATE] = {
    {3, 2, -1, 2}, {-1, 3, 3, -2}, {2, -1, 1, 3}, {1, 3, 2, -1}, {3, -1, -2, 2}, {-2, 2, 1, 3},
    {1, -2, 3, 1}, {3, 1, -1, -1}, {-1, 3, -2, 2}, {2, 2, 1, -2}, {1, -1, 3, 2}, {2, 1, -2, 3},
};

static void offer(int key, int strength)         /* key 0..26 (F3..G5), strength 0..100 */
{
    int i, pc = (key + 5) % 12;
    int black = (0x54A >> pc) & 1;               /* C# D# F# G# A# */
    float m = 0.17f * offer_k * (float)strength * 0.01f, q[EN_NSTATE];
    float reg = (float)(key - 13) * (1.0f / 13.0f) * 1.4f, turn = cyc[2] * FM_TWO_PI;
    /* the register's meaning turns with the long cycle: high keys lighten for a while, then widen the margin,
     * then darken, then narrow it (and low keys the other way); nobody can count on it for long */
    q[S_LIGHT] = reg * fm_cosf(turn);
    q[S_BLEED] = 0.0f;
    q[S_FOLD] = black ? 0.8f : -0.2f;          /* black keys fold */
    q[S_MARGIN] = (black ? -0.3f : 0.4f) + reg * fm_sinf(turn);
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
    for (i = 0; i < 3; i++) {                    /* the phase runs on, whatever the length does: no jumps */
        per[i] += (per_to[i] - per[i]) * (float)n * (1.0f / (PER_DRIFT_S * EN_SR));
        cyc[i] += (float)n / (per[i] * wcycles * pace * EN_SR);
        if (cyc[i] >= 1.0f) {
            cyc[i] -= 1.0f;
            per_to[i] = PER_LO[i] + (PER_HI[i] - PER_LO[i]) * frand();
        }
        en_period[i] = per[i] * wcycles * pace;
    }
    base[S_MARGIN] = depth[0] * fm_sinf(cyc[0] * FM_TWO_PI);          /* the breath: rests long and short */
    base[S_LIGHT] = depth[1] * fm_sinf(cyc[1] * FM_TWO_PI);           /* the tide */
    base[S_BLEED] = 0.5f * (depth[1] + depth[2]) * fm_sinf((cyc[1] + cyc[2]) * FM_TWO_PI);
    base[S_FOLD] = depth[2] * fm_sinf(cyc[2] * FM_TWO_PI);            /* the long turn */
    {
        const world_t *w = &WORLDS[world];
        float g = (float)n * (1.0f / (WORLD_GLIDE_S * EN_SR));
        wcycles += (w->cycles - wcycles) * g;
        for (i = 0; i < EN_NSTATE; i++)
            wbias[i] += (w->bias[i] - wbias[i]) * g;
    }
    for (i = 0; i < EN_NSTATE; i++) {
        float f = pend[i] * seep;
        pend[i] -= f;
        off[i] += 0.85f * f;
        off[(i + 1) % EN_NSTATE] += 0.15f * f;   /* a little bleeds into the next */
        off[i] -= off[i] * fade;
        off[i] = fm_clampf(off[i], -1.0f, 1.0f);
        st[i] = fm_tanhf(wbias[i] + drift * (AMP[i] * base[i] + off[i]));   /* soft at the ends: never pinned */
        en_state[i] = st[i];
        en_offset[i] = off[i];
    }
}

/* -------------------------------------------------- the generator --- */
/* A motif of 3..5 notes on a scale over a root, each a beat or a few long, played a few times with small changes,
 * a rest after each pass (Density shortens it, S_MARGIN lengthens it), a low tone under a pass now and then, the
 * answer an octave up now and then. A new motif takes its scale from the state (S_LIGHT: darker .. brighter; S_FOLD
 * lets in the fourth and seventh), and sometimes the pitch of the last offering, changed into the scale; or, the
 * deeper the fold, one of the last few motifs comes back (its degrees read in the scale of now). */
#define NSCALE 5
static const int8_t SCALE[NSCALE][8] = {         /* the length first, then the steps over the root */
    {5, 0, 3, 5, 7, 10},                         /* minor pentatonic: the coolest */
    {5, 0, 2, 5, 7, 9},                          /* the yo scale */
    {5, 0, 2, 4, 7, 9},                          /* major pentatonic */
    {6, 0, 2, 4, 7, 9, 11},                      /* with the seventh */
    {7, 0, 2, 4, 6, 7, 9, 11},                   /* lydian: the brightest, the most folded */
};
#define MOT_MAX 7
static int8_t mot_deg[MOT_MAX], mot_dur[MOT_MAX];
static int mot_len, mot_i, mot_reps, scale_i = 2, root = 65, root_wait;
static float beat_len = 0.8f * EN_SR;            /* the world's beat (S_MARGIN: up to 15 % either way) */
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

#define MEM 4                                    /* motifs remembered, to fold back */
static struct {
    int8_t deg[MOT_MAX], dur[MOT_MAX];
    uint8_t len;
} mem[MEM];
static int mem_n, mem_w;
volatile uint32_t en_recalls;

static void motif_keep(void)                     /* the motif ending, into the memory */
{
    int i;
    for (i = 0; i < mot_len; i++) {
        mem[mem_w].deg[i] = mot_deg[i];
        mem[mem_w].dur[i] = mot_dur[i];
    }
    mem[mem_w].len = (uint8_t)mot_len;
    for (i = 0; i < mot_len && i < EN_FIG; i++) {   /* for the screen */
        en_mem_deg[mem_w][i] = mot_deg[i];
        en_mem_dur[mem_w][i] = mot_dur[i];
    }
    en_mem_len[mem_w] = (uint8_t)mot_len;
    mem_w = (mem_w + 1) % MEM;
    if (mem_n < MEM)
        mem_n++;
}

static uint8_t fig_vary_pending;
static int reps_new(void)                        /* passes of a motif: Repeat 50: 2..4 */
{
    int r = (int)(repeat * 4.0f) + irand(3);
    return r < 1 ? 1 : r;
}

static void fig_motif(void)                      /* the motif, for the screen */
{
    int i;
    for (i = 0; i < mot_len && i < EN_FIG; i++) {
        en_fig_deg[i] = mot_deg[i];
        en_fig_dur[i] = mot_dur[i];
    }
    en_fig_len = (uint8_t)mot_len;
    en_fig_scale = (uint8_t)SCALE[scale_i][0];
}

static int margin_cut(void)                      /* notes a motif loses to the margin */
{
    return st[S_MARGIN] > 0.2f && frand() < st[S_MARGIN] ? 1 + irand(2) : 0;
}

static void scale_pick(void)                     /* the scale from the state: S_LIGHT darker .. brighter */
{
    float x = 2.0f + 2.4f * st[S_LIGHT] + (st[S_FOLD] > 0.4f ? 0.7f : 0.0f) + 0.6f * (frand() - 0.5f);
    scale_i = x < 0.0f ? 0 : x > (float)(NSCALE - 1) ? NSCALE - 1 : (int)(x + 0.5f);
    en_scale = (uint8_t)scale_i;
}

static void root_wander(void)
{
    if (--root_wait <= 0 && frand() < 0.12f + 0.25f * fm_maxf(st[S_FOLD], 0.0f)) {
        root = WORLDS[world].roots[irand(4)];
        root_wait = 3;
    }
}

static void motif_new(void)
{
    static const int8_t DUR[6] = {1, 1, 2, 2, 3, 4};
    int i, n, d, lo, hi, step = st[S_FOLD] > 0.3f ? 3 : 2;
    scale_pick();
    root_wander();
    n = SCALE[scale_i][0];
    lo = n / 5;                                  /* the register: a degree range of two octaves, raised by light */
    hi = 2 * n;
    d = (int)((float)n * (0.6f + 0.35f * st[S_LIGHT])) + irand(n * 4 / 5 + 1);
    if (mot_len)
        motif_keep();
    if (gift_pc >= 0 && frand() < 0.6f) {        /* the offering, come back changed */
        d = pc_degree(gift_pc) + n * (1 + irand(2));
        gift_pc = -1;
    } else if (mem_n >= 2 && frand() < 0.24f * memory + 0.6f * fm_maxf(st[S_FOLD], 0.0f)) {   /* an older one folds back */
        int m = (mem_w - 2 - irand(mem_n - 1) + 2 * MEM) % MEM;   /* not the one just ended */
        mot_len = mem[m].len - margin_cut();
        mot_len = mot_len < 2 ? 2 : mot_len;
        for (i = 0; i < mot_len; i++) {
            mot_deg[i] = (int8_t)(mem[m].deg[i] < lo ? lo : mem[m].deg[i] > hi ? hi : mem[m].deg[i]);
            mot_dur[i] = mem[m].dur[i];
        }
        mot_reps = reps_new();
        mot_i = 0;
        en_recalls++;
        fig_motif();
        return;
    }
    mot_len = 1 + (int)(length * 4.0f) + irand(3) - margin_cut();   /* Length 50: 3..5 */
    mot_len = mot_len < 2 ? 2 : mot_len > MOT_MAX ? MOT_MAX : mot_len;
    for (i = 0; i < mot_len; i++) {
        int k = irand(6);
        d = d < lo ? lo : d > hi ? hi : d;
        mot_deg[i] = (int8_t)d;
        if (st[S_BLEED] > 0.2f && frand() < st[S_BLEED])
            k = k < 5 ? k + 1 : 5;               /* bleed: longer notes */
        mot_dur[i] = DUR[k];
        d += irand(2) ? 1 + irand(step) : -(1 + irand(step));
    }
    mot_reps = reps_new() - (st[S_FOLD] > 0.5f ? 1 : 0);
    mot_reps = mot_reps < 1 ? 1 : mot_reps;
    mot_i = 0;
    fig_motif();
}

static void motif_vary(void)
{
    fig_vary_pending = 1;
    int n = SCALE[scale_i][0], i = irand(mot_len), d = mot_deg[i] + (irand(2) ? 1 : -1);
    mot_deg[i] = (int8_t)(d < n / 5 ? n / 5 : d > 2 * n ? 2 * n : d);
    if (frand() < 0.3f) {                        /* or two notes trade places in time */
        int j = irand(mot_len), t = mot_dur[i];
        mot_dur[i] = mot_dur[j];
        mot_dur[j] = (int8_t)t;
    }
}

static void motif_event(void)
{
    if (mot_i < mot_len) {
        int note = deg_note(mot_deg[mot_i]), ans;
        float vel = 0.45f + 0.35f * frand();
        while (note > 89)                        /* the ceiling: the lead to F6, the answer to A6 */
            note -= 12;
        for (ans = note + WORLDS[world].answer; ans > 93; ans -= 12)
            ;
        if (mot_i == 0 && frand() < 0.5f - 0.25f * st[S_LIGHT])   /* a low tone under the pass: root or fifth */
            play(R_LOW, root - 24 + (irand(3) ? 0 : 7), 0.5f, 0.0f, 0);
        play(R_LEAD, note, vel, -0.3f + 0.6f * frand(), irand(1500));   /* a hand: up to 34 ms late */
        if (frand() < 0.08f + 0.3f * density + 0.12f * st[S_BLEED])   /* the answer, an octave up, a little later */
            play(R_ANSWER, ans, vel * 0.5f, 0.6f * (frand() * 2.0f - 1.0f), (int32_t)(beat_len * (0.5f + frand())));
        gen_left += beat_len * (float)mot_dur[mot_i];
        en_fig_pos = (uint8_t)mot_i;
        mot_i++;
        return;
    }
    {   /* the pass is over: a rest, then the motif again (perhaps changed) or a new one */
        float lean = fm_maxf(0.5f + 0.9f * st[S_MARGIN], 0.0f);   /* 0 short rests .. ~1.4 long */
        gen_left += beat_len * (2.0f + (1.0f - density) * 8.0f * (0.5f + lean) + 3.0f * frand());
        beat_len = WORLDS[world].beat * tempo * EN_SR * (1.0f + 0.15f * st[S_MARGIN]);
    }
    mot_i = 0;
    if (--mot_reps <= 0 && !en_held)
        motif_new();
    else if (frand() < 0.7f * change + 0.3f * fm_maxf(st[S_FOLD], 0.0f))
        motif_vary();
    if (fig_vary_pending) {
        fig_vary_pending = 0;
        fig_motif();
    }
}

/* -------------------------------------------------------- the drone --- */
/* ALG_DRONE: chords that swell and fall back, a low tone under every second or third, now and then one note of a
 * melody above. Any kit's voices, stretched (a slow attack, a long fall). The state leans it: light the register,
 * bleed the overlap (swells come sooner), margin the space between, fold how often the chord moves; an offering's
 * pitch joins the next chord. */
static int drone_deg, drone_k, drone_chord;     /* drone_chord: the chord's lowest degree (for ARP) */

static void drone_event(void)
{
    static const int8_t STEP[4] = {2, -2, 3, -1};
    int n = SCALE[scale_i][0], base = n + (int)((float)n * 0.3f * st[S_LIGHT]), i, tones;
    drone_chord = base + drone_deg;
    float gap;
    if (drone_k % (2 + irand(2)) == 0)           /* the low tone: the root, or the chord's own */
        play_shaped(R_LOW, deg_note(drone_deg % n) - 24, 0.55f, 0.0f, 0, 2.5f, 1500.0f);
    tones = 2 + (frand() < 0.3f + 0.3f * st[S_BLEED]);
    for (i = 0; i < tones; i++) {                /* the chord: from its degree, every other step of the scale */
        int d = base + drone_deg + 2 * irand(3), note;
        if (i == 0 && gift_pc >= 0) {            /* the offering, in the chord */
            d = pc_degree(gift_pc) + n;
            gift_pc = -1;
        }
        en_fig_deg[i] = (int8_t)d;               /* for the screen */
        en_fig_dur[i] = 4;
        note = deg_note(d);
        while (note > 84)
            note -= 12;
        play_shaped(i ? R_ANSWER : R_LEAD, note, 0.45f + 0.15f * frand(), 0.7f * (frand() * 2.0f - 1.0f),
                    irand((int)(1.5f * EN_SR)), 3.0f, 900.0f);
    }
    en_fig_len = (uint8_t)tones;
    en_fig_pos = 0;
    en_fig_scale = (uint8_t)n;
    if (frand() < 0.18f + 0.2f * fm_maxf(st[S_FOLD], 0.0f)) {   /* one note of a melody, plain */
        int note = deg_note(base + n + irand(n));
        while (note > 89)
            note -= 12;
        play(R_LEAD, note, 0.4f, 0.4f * (frand() * 2.0f - 1.0f), (int32_t)(beat_len * (2.0f + 3.0f * frand())));
    }
    if (!en_held && frand() < (0.25f + 0.3f * fm_maxf(st[S_FOLD], 0.0f)) * 2.0f * change) {   /* the chord moves */
        drone_deg += STEP[irand(4)];
        drone_deg = drone_deg < 0 ? 0 : drone_deg > n ? n : drone_deg;
    }
    if (drone_k % 4 == 3 && !en_held) {          /* now and then, a new colour and root */
        scale_pick();
        root_wander();
    }
    drone_k++;
    gap = (6.0f + 8.0f * frand()) * (1.0f + 0.6f * st[S_MARGIN]) * (1.0f - 0.3f * st[S_BLEED]) * (1.6f - density) *
          (0.5f + length);                       /* Length: the swells' length */
    gen_left += beat_len * gap;
}

/* -------------------------------------------------------------- loops --- */
/* ALG_LOOPS: four loops of unequal length, each a note or two, sounding as their lengths come round; they drift in
 * and out of step (as tape loops do). The state leans them: the margin stretches the lengths, bleed lengthens the
 * notes, light lifts the register, fold now and then moves a note or wakes a loop's second one; an offering's pitch
 * becomes the next loop's second note. Their positions (UI) show on the screen. */
#define NLOOP 4
static const float LOOP_S[NLOOP] = {6.1f, 7.7f, 9.4f, 12.9f};
static const uint8_t LOOP_ROLE[NLOOP] = {R_LEAD, R_ANSWER, R_LEAD, R_LOW};
static struct {
    float left, len;                             /* samples to its next sound, its length now */
    int8_t deg[2];
    uint8_t two, i;                              /* two notes (alternating), the next */
} loop[NLOOP];
volatile float en_loop_ph[NLOOP];

static float loop_len(int i)
{
    return LOOP_S[i] * (1.0f + 0.35f * st[S_MARGIN]) * (1.3f - 0.6f * density) * (0.5f + length) * EN_SR;
}

static void loops_begin(void)
{
    static const int8_t DEG[NLOOP] = {0, 2, 4, 0};
    int i, n = SCALE[scale_i][0];
    for (i = 0; i < NLOOP; i++) {
        loop[i].len = loop_len(i);
        loop[i].left = (float)EN_SR * (0.3f + 1.2f * (float)i);   /* they come in one by one */
        loop[i].deg[0] = (int8_t)(DEG[i] + (i < 3 ? n : 0));
        loop[i].deg[1] = (int8_t)(loop[i].deg[0] + 1 + irand(2));
        loop[i].two = 0;
        loop[i].i = 0;
    }
}

static void loops_tick(uint32_t n)               /* (control) the loops run on; their places for the screen */
{
    int i;
    for (i = 0; i < NLOOP; i++) {
        loop[i].left -= (float)n;
        en_loop_ph[i] = fm_clampf(1.0f - loop[i].left / loop[i].len, 0.0f, 1.0f);
    }
}

static void loops_event(void)
{
    int i, n = SCALE[scale_i][0];
    float next = 1e9f, lift = (float)(int)((float)n * 0.3f * st[S_LIGHT]);
    for (i = 0; i < NLOOP; i++) {
        int note;
        if (loop[i].left >= 1.0f)
            continue;
        note = deg_note(loop[i].deg[loop[i].two ? loop[i].i : 0] + (i < 3 ? (int)lift : 0));
        if (i == 3)
            note -= 12;
        while (note > 89)
            note -= 12;
        play_shaped(LOOP_ROLE[i], note, i == 3 ? 0.5f : 0.4f + 0.15f * frand(), -0.6f + 0.4f * (float)i, 0,
                    1.0f + 0.8f * fm_maxf(st[S_BLEED], 0.0f), 0.0f);
        loop[i].i ^= 1;
        if (gift_pc >= 0 && i < 3) {             /* the offering: this loop's second note */
            loop[i].deg[1] = (int8_t)(pc_degree(gift_pc) + n);
            loop[i].two = 1;
            gift_pc = -1;
        } else if (frand() < (0.04f + 0.12f * fm_maxf(st[S_FOLD], 0.0f)) * 2.0f * change) {   /* a note moves, or a second wakes */
            if (irand(2))
                loop[i].deg[irand(2)] += (int8_t)(irand(2) ? 1 : -1);
            else
                loop[i].two ^= 1;
        }
        loop[i].len = loop_len(i);
        loop[i].left += loop[i].len;
        if (loop[i].left < 1.0f)                 /* (long behind: start its round afresh) */
            loop[i].left = loop[i].len * (0.2f + 0.8f * frand());
    }
    for (i = 0; i < NLOOP; i++) {
        next = fm_minf(next, loop[i].left);
        en_fig_deg[i] = loop[i].deg[0];          /* for the screen: each loop's note, its length (s) */
        en_fig_dur[i] = (int8_t)(loop[i].len * (1.0f / EN_SR) + 0.5f);
    }
    en_fig_len = NLOOP;
    en_fig_scale = (uint8_t)n;
    gen_left = fm_maxf(next, 1.0f);
    if (!en_held && frand() < 0.03f) {           /* rarely, the colour turns under them */
        scale_pick();
        root_wander();
    }
}

/* ------------------------------------------------------- the dispatcher --- */
static void gen_event(void)
{
    switch (WORLDS[world].alg) {
    case ALG_DRONE: drone_event(); break;
    case ALG_LOOPS: loops_event(); break;
    default: motif_event(); break;
    }
}

static void world_begin(void)                    /* a new world, at once: its root, its beat, its algorithm */
{
    const world_t *w = &WORLDS[world];
    root = w->roots[0];
    root_wait = 2;
    beat_len = w->beat * tempo * EN_SR;
    en_alg = w->alg;
    scale_pick();
    switch (w->alg) {
    case ALG_DRONE:
        drone_deg = 0;
        drone_k = 0;
        break;
    case ALG_LOOPS: loops_begin(); break;
    default:
        mot_len = 0;
        motif_new();
        break;
    }
    gen_left = 0.3f * EN_SR;
}

/* ---------------------------------------------------------------- ARP --- */
/* A pulse of broken chords under the music (ARP): eighths of the beat, soft and short, the answer's voice, up and
 * down the chord the music stands on (the motif's note, the drone's chord, the first loop's note), an octave under.
 * Not every eighth sounds: fewer when sparse (Density), fewer in the margin. No drums: at most, Yokota's pulse. */
static float arp_left;
static int arp_i;
volatile uint8_t en_arp_on, en_arp_beat;

static void arp_event(void)
{
    static const int8_t UPDOWN[6] = {0, 2, 4, 6, 4, 2};
    int n = SCALE[scale_i][0], cd, note;
    float skip = fm_clampf(0.1f + 0.5f * (1.0f - density) + 0.3f * fm_maxf(st[S_MARGIN], 0.0f), 0.0f, 0.85f);
    arp_left += beat_len * 0.5f;
    if (arp_left < 1.0f)
        arp_left = beat_len * 0.5f;
    if (WORLDS[world].alg == ALG_DRONE)
        cd = drone_chord;
    else if (WORLDS[world].alg == ALG_LOOPS)
        cd = loop[0].deg[0];
    else
        cd = mot_len ? mot_deg[(mot_i + mot_len - 1) % mot_len] : n;
    arp_i = (arp_i + 1) % 6;
    if (frand() < skip)
        return;
    note = deg_note(cd - n + UPDOWN[arp_i]);
    while (note > 84)
        note -= 12;
    play_shaped(R_ANSWER, note, 0.16f + 0.08f * frand(), arp_i & 1 ? 0.35f : -0.35f, 0, 0.3f, 0.0f);
    en_arp_beat++;
}

/* ---------------------------------------------------------------- REC --- */
/* A phrase played in (REC held: the keys sound, the generator waits): let go, it becomes the music's own. Its notes
 * are read into the scale (the nearest degree), its gaps into beats (1..4); the motif plays it, then changes it as it
 * would its own; the drone takes its first note into the chord, the loops its first notes. One note: an offering. */
#define REC_MAX MOT_MAX
static uint8_t rec_note[REC_MAX], rec_n;
static uint32_t rec_t[REC_MAX], clock_s;
volatile uint8_t en_recording;
volatile uint32_t en_taken;

static int note_degree(int note)                 /* a MIDI note to a degree of the scale (0 the root) */
{
    int n = SCALE[scale_i][0], rel = note - root, o = rel >= 0 ? rel / 12 : -((11 - rel) / 12);
    return pc_degree(note % 12) + n * o;
}

static void rec_take(void)
{
    int i, n = SCALE[scale_i][0];
    if (rec_n == 0)
        return;
    if (rec_n == 1) {                            /* one note: an offering of its pitch */
        gift_pc = (int8_t)(rec_note[0] % 12);
        return;
    }
    en_taken++;
    switch (WORLDS[world].alg) {
    case ALG_DRONE:
        drone_deg = note_degree(rec_note[0]) - n;
        drone_deg = drone_deg < 0 ? 0 : drone_deg > n ? n : drone_deg;
        break;
    case ALG_LOOPS:
        for (i = 0; i < 3 && i < rec_n; i++)
            loop[i].deg[0] = (int8_t)note_degree(rec_note[i]);
        break;
    default:
        if (mot_len)
            motif_keep();
        mot_len = rec_n;
        {   /* the phrase's shape kept, moved by whole octaves into the motifs' register (degrees 1 .. 2n) */
            int lo = 99, hi = -99, sh = 0;
            for (i = 0; i < rec_n; i++) {
                int d = note_degree(rec_note[i]);
                lo = d < lo ? d : lo;
                hi = d > hi ? d : hi;
            }
            while (lo + sh < 1)
                sh += n;
            while (hi + sh > 2 * n + 2 && lo + sh - n >= 1)
                sh -= n;
            for (i = 0; i < rec_n; i++)
                mot_deg[i] = (int8_t)(note_degree(rec_note[i]) + sh);
        }
        for (i = 0; i < rec_n; i++) {
            int b;
            b = i + 1 < rec_n ? (int)((float)(rec_t[i + 1] - rec_t[i]) / beat_len + 0.5f) : 2;
            mot_dur[i] = (int8_t)(b < 1 ? 1 : b > 4 ? 4 : b);
        }
        mot_reps = reps_new() + 1;
        mot_i = 0;
        fig_motif();
        break;
    }
    gen_left = beat_len;                         /* a breath, then it plays */
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
    case P_SPACE: rev_send = 0.15f + (float)(v * v) * 0.0001f; break;   /* the plate's send */
    case P_ECHO: echo_send = (float)(v * v) * 0.0001f; break;
    case P_TIME:                                 /* the tape's loop: 0.3 .. 5.3 s; the tape glides there */
        echo_secs = 0.3f + 0.05f * (float)v;
        echo_time(echo_secs);
        break;
    case P_FEEDBACK: echo_fb = 0.2f + 0.0072f * (float)v; break;      /* 0.2 .. 0.92 */
    case P_SIZE: pl_target = 0.5f + 0.0047f * (float)v; break;        /* the plate's decay */
    case P_WEAR: echo_wear = (float)v * 0.01f; break;                 /* the tape's age: wow, flutter, dark */
    case P_REPEAT: repeat = (float)v * 0.01f; break;
    case P_CHANGE: change = (float)v * 0.01f; break;
    case P_LENGTH: length = (float)v * 0.01f; break;
    case P_MEMORY: memory = (float)v * 0.01f; break;
    case P_DRIFT: drift = (float)v * 0.02f; break;                  /* the cycles' and offerings' sway: 0 .. 2 */
    case P_OFFER: offer_k = (float)v * 0.02f; break;
    case P_TEMPO: tempo = fm_exp2f((float)(50 - v) * (1.0f / 60.0f)); break;   /* the beat: x1.8 .. x0.56 */
    case P_TRANSPOSE: transpose = v; break;
    case P_BREATH: depth[0] = (float)v * 0.02f; break;
    case P_TIDE: depth[1] = (float)v * 0.02f; break;
    case P_TURN: depth[2] = (float)v * 0.02f; break;
    case P_PACE: pace = fm_exp2f((float)(50 - v) * (1.0f / 40.0f)); break;    /* the cycles' lengths: x2.4 .. x0.42 */
    case P_TUNE: fm_tune = (float)v * 0.01f; break;                 /* cents, as semitones */
    case P_LEVEL: level = fm_exp2f((float)(v - 70) * 0.05f); break; /* 0.3 dB a step, 70 as it was */
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
        case C_WORLD:
            if (a < EN_NWORLD && a != world) {
                world = (uint8_t)a;
                world_begin();
            }
            break;
        case C_HOLD: en_held = (uint8_t)(a != 0); break;
        case C_OCT: music_oct = (int)a - 2; break;
        case C_ARP:
            en_arp_on = (uint8_t)(a != 0);
            arp_left = 0.0f;
            break;
        case C_REC:
            if (a && !en_recording) {
                en_recording = 1;
                rec_n = 0;
            } else if (!a && en_recording) {
                en_recording = 0;
                rec_take();
            }
            break;
        case C_REC_NOTE:
            if (en_recording && a < 128) {
                raw_note = 1;
                play(R_LEAD, (int)a, 0.6f, 0.0f, 0);
                raw_note = 0;
                if (rec_n < REC_MAX) {
                    rec_note[rec_n] = (uint8_t)a;
                    rec_t[rec_n++] = clock_s;
                }
            }
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
    for (i = 0; i < EN_NSTATE; i++)
        pend[i] = off[i] = st[i] = 0.0f;
    cyc[0] = cyc[1] = cyc[2] = 0.0f;
    per[0] = per_to[0] = 71.0f;                  /* the first turns: as before */
    per[1] = per_to[1] = 163.0f;
    per[2] = per_to[2] = 409.0f;
    gift_pc = -1;
    mem_n = mem_w = 0;
    mot_len = 0;
    world = 0;
    en_alg = ALG_MOTIF;
    wcycles = 1.0f;
    for (i = 0; i < EN_NSTATE; i++)
        wbias[i] = 0.0f;
    root = WORLDS[0].roots[0];
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
    echo_control(n, 0.5f, modv, en_playing);   /* (and echo_wear: P_WEAR) */
    plate_tick(pl_target);
    state_step(n);
    en_cycle = cyc[0];
    en_cyc[0] = cyc[0];
    en_cyc[1] = cyc[1];
    en_cyc[2] = cyc[2];
    en_beat_s = beat_len * (1.0f / EN_SR);
    en_root = (uint8_t)root;
    if (WORLDS[world].alg == ALG_LOOPS && en_playing)
        loops_tick(n);
    for (i = 0; i < EN_NVOICE && i < NFM; i++)
        en_glow[i] = fmv[i].on && fmv[i].wait <= 0 ? fm_minf(fmv[i].amp / fm_maxf(fmv[i].peak, 0.01f), 1.0f) : 0.0f;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f * level;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        if (en_playing && !en_recording) {       /* the generator's next event lands on its sample (paused while
                                                  * a phrase is played in: REC) */
            if (gen_left <= 0.0f)
                gen_event();
            if (en_arp_on && arp_left <= 0.0f)
                arp_event();
            if ((float)k > gen_left)
                k = (uint32_t)gen_left + 1u;
            if (en_arp_on && (float)k > arp_left)
                k = (uint32_t)arp_left + 1u;
            gen_left -= (float)k;
            arp_left -= (float)k;
        }
        clock_s += k;
        control(k);
        mix_clear(k);
        fm_render(k, 1.6f);
        mix_out(out, k, gain, echo_send, rev_send);
        out += 2 * k;
        n -= k;
    }
}
