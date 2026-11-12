/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* The pulse (ARP's second page, PULSE): a muffled beat under the music, at most as Susumu Yokota's, never a dance.
 * Three sounds, none a drum kit's: a soft low thump (a sine falling in pitch, no attack's click), a small click
 * (noise in a narrow band, a few ms), and a brush (noise, high, breathed in and out). On sixteenths of the beat, each
 * on its own Euclidean pattern (k hits spread over n steps), the lengths unequal in some worlds so they drift against
 * each other; each world its own (some without the thump), and a second length for each. The page's four, plain:
 *   Level   all of it (0: none; 50 as made)
 *   Busy    hits taken from or added to the world's patterns (50: as they are)
 *   Wander  how far the patterns move with the music: now and then (more in a fold, less held, more with SEQ's
 *           Change) a hit comes or goes (fewer in the margin), a pattern turns, or takes its other length; at the end
 *           of a phrase the pulse may rest a bar or half. 0: the world's patterns, still
 *   Weight  the thump against the click and the brush (0: no thump; 100: the thump alone; 50 both as made)
 * Not every hit sounds: fewer when sparse (Density) and in the margin; bleed lengthens the brush, light brightens the
 * click. Its own random numbers (the music's untouched). #included by lagoon.c after amb_parts.c (mixl, .., sine()),
 * the worlds and the generators (phrase_end). */
typedef struct {
    uint8_t n, k;                                /* k hits over n steps (n 0: none) */
} euclid_t;
static const euclid_t PULSE_PAT[EN_NWORLD][3] = {   /* the thump, the click, the brush */
    {{16, 2}, {16, 5}, {12, 5}},                 /* SHALLOWS OF MEMORY: halves under, clicks across */
    {{32, 1}, {0, 0}, {24, 3}},                  /* BEHIND THE HORIZON: a far, slow thump, long breaths */
    {{0, 0}, {13, 3}, {20, 2}},                  /* THE UNREAD LETTER: no thump, a clock that is not quite one */
    {{16, 3}, {8, 3}, {16, 8}},                  /* SILENT FOUNTAIN: three in sixteen, the brush on the eighths */
    {{20, 3}, {14, 4}, {9, 4}},                  /* WHERE LIGHT BECOMES MOSS: three lengths, drifting */
};
static const uint8_t PULSE_ALT[EN_NWORLD][3] = {   /* each one's other length (Wander), its hits kept in proportion */
    {12, 10, 16}, {24, 0, 32}, {0, 11, 15}, {12, 6, 12}, {18, 15, 11},
};
static float pu_level, pu_busy, pu_wander = 0.3f, pu_weight = 0.5f;   /* the page's four: 0..2, -1..1, 0..1, 0..1 */
static int8_t pu_dk[3];                          /* Wander: hits come or gone, -2..2 */
static uint8_t pu_alt[3];                        /* Wander: the other length */
static uint8_t pu_rest;                          /* steps left of a rest (a phrase's end) */
static uint32_t pu_seed = 0x3C6EF372u;
static float pu_left;                            /* samples to the next step */
static uint32_t pu_step;
static uint8_t pu_rot[3];                        /* each pattern's turn */
volatile uint32_t en_pulse_hits;                 /* (UI) hits sounded since boot */
volatile uint8_t en_pulse_last;                  /* (UI) the last: 0 the thump, 1 a click, 2 the brush */
volatile uint8_t en_pulse_n[3], en_pulse_k[3], en_pulse_rot[3];   /* (UI) the patterns now */
volatile uint32_t en_pulse_step;                 /* (UI) the step now */

static inline float pu_rnd(void)                 /* 0..1 */
{
    pu_seed = pu_seed * 1664525u + 1013904223u;
    return (float)(pu_seed >> 8) * (1.0f / 16777216.0f);
}
static inline float pu_noise(void) { return 2.0f * pu_rnd() - 1.0f; }

/* the three sounds (ai_*: the brush) */
static float th_ph, th_pf, th_amp, th_att;       /* the thump: phase, pitch's fall, level, its soft start */
static float ck_amp, ck_lo, ck_bd, ck_f, ck_pan; /* the click: level, its band (state, tuning), side */
static float ai_amp, ai_att, ai_dec, ai_hp, ai_lp, ai_pan;   /* the brush: level, rise, fall, its filter, side */
#define TH_DEC 0.99978f                          /* ~0.7 s to -60 dB */
#define TH_PDEC 0.9985f                          /* the pitch's fall: ~15 ms */
#define CK_DEC 0.985f                            /* ~10 ms */

static void pu_pattern(int i, euclid_t *e)      /* a pattern now: the world's, its length, Busy and Wander's hits */
{
    const euclid_t *b = &PULSE_PAT[world][i];
    int n = b->n, k = b->k, alt = PULSE_ALT[world][i];
    e->n = e->k = 0;
    if (!n || !k)
        return;
    if (pu_alt[i] && alt) {
        k = (k * alt + n / 2) / n;
        n = alt;
    }
    k += (int)(pu_busy * 2.0f + (pu_busy >= 0.0f ? 0.5f : -0.5f)) + pu_dk[i];
    k = k < 1 ? 1 : k > n * 3 / 4 ? n * 3 / 4 : k;
    e->n = (uint8_t)n;
    e->k = (uint8_t)k;
}

static void pulse_ui(void)                       /* (control) the patterns, for the screen */
{
    euclid_t e;
    int i;
    for (i = 0; i < 3; i++) {
        pu_pattern(i, &e);
        en_pulse_n[i] = e.n;
        en_pulse_k[i] = e.k;
        en_pulse_rot[i] = pu_rot[i];
    }
}

static void pulse_begin(void)                    /* a world begins: the patterns turned anew, as the world has them */
{
    int i;
    for (i = 0; i < 3; i++) {
        pu_rot[i] = (uint8_t)(pu_rnd() * 16.0f);
        pu_dk[i] = 0;
        pu_alt[i] = 0;
    }
    pu_rest = 0;
    pu_step = 0;
    pu_left = 0.0f;
    pulse_ui();
}

static void pulse_wander(void)                   /* once a bar: the patterns move with the music (Wander) */
{
    float fold = fm_maxf(st[S_FOLD], 0.0f);
    int i, t;
    if (pu_wander < 0.01f) {                     /* still: the world's own */
        pu_dk[0] = pu_dk[1] = pu_dk[2] = 0;
        pu_alt[0] = pu_alt[1] = pu_alt[2] = 0;
        return;
    }
    if (en_held || pu_rnd() >= pu_wander * (0.25f + 0.4f * fold) * (0.5f + change))
        return;
    for (t = 0, i = (int)(pu_rnd() * 2.999f); t < 3 && !PULSE_PAT[world][i].n; t++)
        i = (i + 1) % 3;                         /* one that has a pattern */
    if (!PULSE_PAT[world][i].n)
        return;
    t = (int)(pu_rnd() * 10.0f);
    if (t < 5) {                                 /* a hit comes or goes: fewer in the margin, more in a fold */
        int d = st[S_MARGIN] > 0.2f ? -1 : fold > 0.2f ? 1 : pu_rnd() < 0.5f ? -1 : 1;
        pu_dk[i] = (int8_t)(pu_dk[i] + d < -2 ? -2 : pu_dk[i] + d > 2 ? 2 : pu_dk[i] + d);
    } else if (t < 8) {
        pu_rot[i] = (uint8_t)(pu_rot[i] + (pu_rnd() < 0.5f ? 1 : 255));
    } else {
        pu_alt[i] ^= 1u;
    }
}

static int euclid_hit(const euclid_t *e, uint32_t step, int rot)
{
    uint32_t s;
    if (!e->n || !e->k)
        return 0;
    s = (step + (uint32_t)rot) % e->n;
    return (s * e->k) % e->n < e->k;
}

static void pulse_event(void)                    /* a step: what sounds on it; the next step's time */
{
    float keep = fm_clampf(0.6f + 0.5f * density - 0.35f * fm_maxf(st[S_MARGIN], 0.0f), 0.15f, 1.0f);
    float gk[3];
    euclid_t pat;
    int i;
    gk[0] = pu_level * fm_minf(2.0f * pu_weight, 1.5f);              /* Weight: the thump .. */
    gk[1] = gk[2] = pu_level * fm_minf(2.0f * (1.0f - pu_weight), 1.5f);   /* .. against the click and the brush */
    if (pu_step % 16u == 0u) {                   /* a bar: the patterns may move; a phrase's end: perhaps a rest */
        pulse_wander();
        if ((phrase_end & 1u) && pu_rnd() < 0.6f * pu_wander)
            pu_rest = pu_rnd() < 0.5f ? 8 : 16;
        phrase_end &= (uint8_t)~1u;
    }
    for (i = 0; i < 3 && !pu_rest; i++) {
        pu_pattern(i, &pat);
        if (!euclid_hit(&pat, pu_step, pu_rot[i]) || gk[i] <= 0.0f || pu_rnd() >= keep)
            continue;
        if (i == 0) {
            th_ph = 0.0f;
            th_pf = 1.0f;
            th_att = 0.0f;
            th_amp = (0.36f + 0.1f * pu_rnd()) * gk[0];
        } else if (i == 1) {
            float fc = 2400.0f + 1600.0f * (st[S_LIGHT] + 1.0f) * 0.5f + 600.0f * pu_rnd();
            ck_f = 2.0f * fm_sinf(FM_PI * fc * (1.0f / EN_SR));
            ck_amp = (0.12f + 0.08f * pu_rnd()) * gk[1];
            ck_pan = -ck_pan + (ck_pan == 0.0f ? 0.4f : 0.0f);
        } else {
            ai_amp = (0.05f + 0.03f * pu_rnd()) * gk[2];
            ai_att = 0.0f;
            ai_dec = fm_expf(-6.9f / ((0.12f + 0.12f * fm_maxf(st[S_BLEED], 0.0f)) * EN_SR));
            ai_pan = 0.6f * pu_noise();
        }
        en_pulse_last = (uint8_t)i;
        en_pulse_hits++;
    }
    if (pu_rest)
        pu_rest--;
    en_pulse_step = pu_step;
    pu_step++;
    pu_left += beat_len * 0.25f;
    if (pu_left < 1.0f)
        pu_left = beat_len * 0.25f;
}

static void pulse_render(uint32_t n)             /* into the mix: the thump dry and centred, the click and the brush
                                                  * to the sides and a little to the echo and the plate */
{
    uint32_t j;
    if (th_amp < 1e-4f && ck_amp < 1e-4f && ai_amp < 1e-4f)
        return;
    for (j = 0; j < n; j++) {
        float th = 0.0f, ck = 0.0f, ai = 0.0f;
        if (th_amp >= 1e-4f) {
            th_ph += (44.0f + 66.0f * th_pf) * (1.0f / EN_SR);
            if (th_ph >= 1.0f)
                th_ph -= 1.0f;
            th_pf *= TH_PDEC;
            th_att += (1.0f - th_att) * 0.015f;  /* ~2 ms: no click of its own */
            th = sine(th_ph) * th_amp * th_att;
            th_amp *= TH_DEC;
        }
        if (ck_amp >= 1e-4f) {
            float hp = pu_noise() - ck_lo - 0.35f * ck_bd;
            ck_bd += ck_f * hp;
            ck_lo += ck_f * ck_bd;
            ck = ck_bd * ck_amp;
            ck_amp *= CK_DEC;
        }
        if (ai_amp >= 1e-4f) {
            float x = pu_noise();
            ai_lp += 0.6f * (x - ai_lp);
            ai_hp += 0.25f * (ai_lp - ai_hp);
            ai_att += (1.0f - ai_att) * 0.0015f;   /* ~15 ms in */
            ai = (ai_lp - ai_hp) * ai_amp * ai_att;
            ai_amp *= ai_dec;
        }
        mixl[j] += th + ck * (1.0f - ck_pan) + ai * (1.0f - ai_pan);
        mixr[j] += th + ck * (1.0f + ck_pan) + ai * (1.0f + ai_pan);
        esend[j] += 0.3f * ck;
        rsend[j] += 0.15f * th + 0.4f * ai;
    }
}
