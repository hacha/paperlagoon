/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* The age: what time does to a recording, over the whole of it (the voices, the echoes, the plate), before the last
 * soft clip. Three on FX's second page (AGE; its fourth, Air, is the voices' breath: lagoon.c), one with the tape's Wear;
 * each plain, each 0 for none:
 *   Haze     the band narrows (the highs go first, then the lows) and the loud parts round off
 *   Hiss     a floor of noise under it, breathing slowly; only while the music plays (it fades in and out with it)
 *   Dropout  now and then the sound thins for a moment, darker, one side more than the other, as worn tape loses
 *            its coat; the more, the more often and the deeper
 *   blur     (with Wear, FX) the sound smears: a copy of it a little late, its lateness wandering, fed back a
 *            little, as a head that does not sit still
 * Order: blur, the hiss joins, the haze (so the hiss is narrowed with the rest), the dropout (over the hiss too).
 * #included by amb_parts.c after echo.c (one -O2 unit): uses sine(), s16() and fastmath.h. Its chance is its own
 * (age_rnd), so how much hiss or dropout there is never changes what the music chooses. Cheap per sample:
 * a few one-pole filters, two noises, two delay taps, one sine. */
static float age_haze, age_hiss, age_drop, age_blur;  /* 0..1: P_HAZE, P_HISS, P_DROPOUT; the blur from P_WEAR */
static float age_spl_blur, age_spl_haze;         /* a splash's, for a moment (lagoon.c): more blur, the haze moved */
static float age_bl;                             /* the blur now (age_blur and a splash's), once a chunk */
volatile float en_age_drop;                      /* (UI) a dropout now, 0..1 */
static uint32_t age_seed = 0x2545F491u;
static inline float age_rnd(void)                /* 0..1, apart from the music's frand() */
{
    age_seed = age_seed * 1664525u + 1013904223u;
    return (float)(age_seed >> 8) * (1.0f / 16777216.0f);
}

/* haze: two one-pole lowpasses and a highpass; their coefficients once a chunk */
static float hz_a = 1.0f, hz_h, hz_round;
static float hz_l1[2], hz_l2[2], hz_hp[2];
/* hiss: its colour, its level (gliding to the music's playing), its breath */
static float hs_lp[2], hs_level, hs_ph;
/* dropout: where one is, how deep, how long; the dark it brings */
static float dr_ph = 1.0f, dr_inc, dr_depth[2], dr_dark[2];
static uint32_t dr_wait = 44100u * 8u;
/* blur: a short line a side (int16, 34 ms), its lateness wandering */
#define BL_N 1500
static int16_t bl_buf[2][BL_N];
static int bl_w;
static float bl_ph[2], bl_d[2];

static void age_init(void)
{
    int i;
    for (i = 0; i < BL_N; i++)
        bl_buf[0][i] = bl_buf[1][i] = 0;
    bl_w = 0;
    bl_ph[1] = 0.37f;
}

static float age_onepole(float fc)               /* a one-pole's coefficient for a cutoff (Hz) */
{
    return fm_clampf(1.0f - fm_expf(-FM_TWO_PI * fc * (1.0f / EN_SR)), 0.0f, 1.0f);
}

/* once a chunk: the coefficients, the hiss's level, the next dropout */
static void age_control(uint32_t n, int playing)
{
    float t = (float)n * (1.0f / EN_SR), hz = fm_clampf(age_haze + age_spl_haze, 0.0f, 1.0f);
    /* haze 0: open (no filter); 1: 2.2 kHz down, 320 Hz up */
    hz_a = hz > 0.005f ? age_onepole(18000.0f * fm_powf(2200.0f / 18000.0f, hz)) : 1.0f;
    hz_h = hz > 0.005f ? age_onepole(20.0f + 300.0f * hz * hz) : 0.0f;
    hz_round = 0.9f * hz;
    age_bl = fm_minf(age_blur + age_spl_blur, 1.0f);
    hs_level += ((playing ? 0.019f * age_hiss * age_hiss : 0.0f) - hs_level) * fm_minf(t * 0.7f, 1.0f);
    hs_ph += t * 0.071f;                         /* the breath: ~14 s */
    if (hs_ph >= 1.0f)
        hs_ph -= 1.0f;
    if (dr_wait > n) {
        dr_wait -= n;
    } else {
        /* the next, after 45 s .. 3 s on average (Dropout 0: never) */
        float mean = 45.0f * fm_powf(3.0f / 45.0f, age_drop);
        dr_wait = (uint32_t)((0.3f + 1.4f * age_rnd()) * mean * EN_SR);
        if (age_drop > 0.005f && playing && dr_ph >= 1.0f) {
            float len = 0.04f + (0.08f + 0.25f * age_drop) * age_rnd();   /* 40 .. 370 ms */
            float d = 0.25f + 0.7f * age_drop * (0.4f + 0.6f * age_rnd());
            int s = (age_rnd() < 0.5f);
            dr_ph = 0.0f;
            dr_inc = 1.0f / (len * EN_SR);
            dr_depth[s] = fm_minf(d, 0.95f);
            dr_depth[1 - s] = fm_minf(d * (0.5f + 0.4f * age_rnd()), 0.95f);
        }
    }
    en_age_drop = dr_ph < 1.0f ? sine(dr_ph * 0.5f) : 0.0f;
}

/* one sample, both sides */
static inline void age_px(float *l, float *r)
{
    float x[2], e = 0.0f;
    int c;
    x[0] = *l;
    x[1] = *r;
    if (dr_ph < 1.0f) {                          /* the dropout's shape: half a sine, in and out */
        e = sine(dr_ph * 0.5f);
        dr_ph += dr_inc;
    }
    if (age_bl > 0.005f) {
        int i0, i1, k;
        float f, y;
        for (c = 0; c < 2; c++) {
            bl_ph[c] += (c ? 0.43f : 0.29f) * (1.0f / EN_SR);
            if (bl_ph[c] >= 1.0f)
                bl_ph[c] -= 1.0f;
            /* lateness 12..31 ms, wandering more with more blur */
            bl_d[c] = (0.012f + 0.006f * age_bl * (1.0f + sine(bl_ph[c]))) * EN_SR + 300.0f * age_bl;
            k = (int)bl_d[c];
            f = bl_d[c] - (float)k;
            i0 = bl_w - k;
            i0 += i0 < 0 ? BL_N : 0;
            i1 = i0 - 1;
            i1 += i1 < 0 ? BL_N : 0;
            y = ((float)bl_buf[c][i0] + ((float)bl_buf[c][i1] - (float)bl_buf[c][i0]) * f) * (1.0f / 12000.0f);
            bl_buf[c][bl_w] = s16(x[c] + 0.35f * age_bl * y);
            x[c] = (1.0f - 0.35f * age_bl) * x[c] + 0.65f * age_bl * y;
        }
        if (++bl_w >= BL_N)
            bl_w = 0;
    }
    for (c = 0; c < 2; c++) {
        if (hs_level > 1e-6f) {                  /* the hiss, its highs softened */
            hs_lp[c] += 0.4f * ((age_rnd() * 2.0f - 1.0f) - hs_lp[c]);
            x[c] += hs_level * (0.75f + 0.25f * sine(hs_ph + 0.5f * (float)c)) * hs_lp[c];
        }
        if (hz_a < 1.0f) {
            hz_l1[c] += hz_a * (x[c] - hz_l1[c]);
            hz_l2[c] += hz_a * (hz_l1[c] - hz_l2[c]);
            hz_hp[c] += hz_h * (hz_l2[c] - hz_hp[c]);
            x[c] = hz_l2[c] - hz_hp[c];
            x[c] *= 1.0f / (1.0f + hz_round * fm_fabsf(x[c]));   /* the loud parts round off */
        }
        if (e > 0.0f) {                          /* the coat lost: darker, thinner */
            dr_dark[c] += 0.12f * (x[c] - dr_dark[c]);
            x[c] = (1.0f - dr_depth[c] * e) * (x[c] + (dr_dark[c] - x[c]) * 0.8f * e);
        } else {
            dr_dark[c] = x[c];
        }
    }
    *l = x[0];
    *r = x[1];
}
