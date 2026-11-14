/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changes Copyright (C) 2026 hacha */
/* Parts of AMB-1's engine (charlesvestal/fm1-amb, app/dsp/amb.c, MPL-2.0), taken for PaperLagoon: the command
 * queue's post(), the random numbers, the sine table, the slow random modulators, the two-operator FM voice (AMB-1's
 * bell, its settings now given by the caller rather than read from AMB-1's parameters and loops) and the output
 * stage (the voices' sends through the echo and the plate, AMB-1's render_chunk() mix). PaperLagoon gave the voice a
 * breath (fm_patch_t.breath: air through it, a rough modulation, a wandering pitch), and made no two notes the same
 * (fm_start: the modulator a little off its ratio and anywhere in its turn, the pitch and the index a little off).
 * #included by lagoon.c (one -O2 unit), after fastmath.h, EN_SR, EN_POOL and BARRIER(); it #includes plate.c and
 * echo.c itself (and, PaperLagoon, age.c: the age over the whole mix, before the soft clip). */

/* ------------------------------------------------------------ commands --- */
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}

/* ------------------------------------------------------- random, pitch --- */
static uint32_t rng = 0x9E3779B9u;
static uint32_t rnd(void)                       /* Marsaglia's xorshift (Xorshift RNGs, 2003): shifts 13, 17, 5 */
{
    uint32_t x = rng;
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}
static void sine_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
}

#include "plate.c"
#include "echo.c"
#include "age.c"                                  /* (PaperLagoon) */

/* ------------------------------------------------- slow random motion --- */
#define NMOD 4                                  /* (AMB-1 has nine) here the echo's four */
static struct {
    float v, tgt, rate;
    uint32_t left;                              /* samples until a new target */
} mod[NMOD];
static float modv[NMOD];

static void mods_init(void)
{
    int i;
    for (i = 0; i < NMOD; i++) {
        mod[i].v = mod[i].tgt = 0.0f;
        mod[i].left = 0;
    }
}
static void mods_step(uint32_t n)
{
    int i;
    for (i = 0; i < NMOD; i++) {
        if (mod[i].left <= n) {
            float secs = 7.0f + 33.0f * frand();
            mod[i].tgt = frand() * 2.0f - 1.0f;
            mod[i].left = (uint32_t)(secs * EN_SR);
            mod[i].rate = (float)n / (secs * EN_SR * 0.5f);
        } else {
            mod[i].left -= n;
        }
        mod[i].v += (mod[i].tgt - mod[i].v) * fm_minf(mod[i].rate, 1.0f);
        modv[i] = mod[i].v;
    }
}

/* -------------------------------------------------- two-operator FM --- */
/* AMB-1's bell: a sine modulating a sine; the level and the index each fall on their own time (the index faster:
 * a note starts bright and settles round), a short attack. The caller gives what AMB-1 took from its loops */
#define NFM 8
typedef struct {
    float cph, mph, cinc, minc, amp, adec, idx, idec, att, arate, gl, gr, dry, es, rs, peak;
    float bn, bl, sf, slo, sbd, nz, dph, drift;  /* (PaperLagoon) the breath: its roughness, its air (level, the air's
                                                  * filter: tuning, state), the noise, the pitch's wander */
    int32_t wait;
    uint8_t on, note;
} fmv_t;
static fmv_t fmv[NFM];

typedef struct {
    float ratio;                                 /* the modulator's frequency, times the carrier's */
    float idx;                                   /* the index at the strike, in radians */
    float idx_t, t60, att_ms;                    /* the index's and the level's fall to -60 dB (s); the attack */
    float dry, esend, rsend;                     /* to the mix, the echo, the plate */
    float breath;                                /* (PaperLagoon) 0..1: air and roughness in the tone */
} fm_patch_t;

static float fm_tune;                            /* (PaperLagoon) the tuning, in semitones */
static uint32_t fm_seed = 0x68E31DA4u;           /* (PaperLagoon) the breath's noise: apart from the music's frand() */
static inline float fm_noise(void)               /* -1..1 */
{
    fm_seed = fm_seed * 1664525u + 1013904223u;
    return (float)(int32_t)fm_seed * (1.0f / 2147483648.0f);
}
static float fm_noise_ph(void) { return 0.5f + 0.5f * fm_noise(); }

static int fm_start(const fm_patch_t *pt, int note, float vel, float pan, int32_t wait)   /* the voice used */
{
    int i, best = 0;
    float lo = 9.0f, p;
    fmv_t *v;
    for (i = 0; i < NFM; i++) {                 /* a free voice, or the quietest */
        float a = fmv[i].on ? fmv[i].amp : -1.0f;
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    v = &fmv[best];
    v->on = 1;
    v->note = (uint8_t)note;
    v->cph = v->mph = 0.0f;
    v->cinc = mtof((float)note + fm_tune) * (1.0f / EN_SR);
    v->minc = v->cinc * pt->ratio;
    v->amp = v->peak = vel;
    v->adec = fm_expf(-6.9f / (pt->t60 * EN_SR));
    v->idx = pt->idx * (1.0f / FM_TWO_PI);
    v->idec = fm_expf(-6.9f / (pt->idx_t * EN_SR));
    v->att = 0.0f;
    v->arate = fm_minf(1.0f, 1.0f / (pt->att_ms * 0.001f * EN_SR));
    p = (fm_clampf(pan, -1.0f, 1.0f) + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(p);
    v->gr = fm_sinf(p);
    v->dry = pt->dry;
    v->es = pt->esend;
    v->rs = pt->rsend;
    v->wait = wait;
    {   /* (PaperLagoon) no note twice the same: the pitch a cent or two off; the modulator a fraction of a hertz off
         * its ratio, so the partials it shares with the carrier beat slowly against each other (a turn in one to four
         * seconds, each note its own: the shimmer of an FM piano), and starting anywhere in its turn; the strike a
         * little brighter or duller. The breath's chance (fm_noise): the music's own is untouched */
        float d = fm_noise();
        v->cinc *= 1.0f + 0.0012f * fm_noise();
        v->minc = v->cinc * pt->ratio + (d < 0.0f ? -1.0f : 1.0f) * (0.12f + 0.3f * fm_fabsf(d)) * (1.0f / EN_SR);
        v->mph = fm_noise_ph();
        v->idx *= 1.0f + 0.07f * fm_noise();
    }
    {   /* (PaperLagoon) the breath: the air a band of noise round the note (an octave up for a high one's sake: under
         * 6 kHz), its level steadied against the band's width; a rough modulation; a pitch that wanders a little */
        float fc = fm_minf(mtof((float)note) * 1.5f, 6000.0f), sf = 2.0f * fm_sinf(FM_PI * fc * (1.0f / EN_SR));
        float b = fm_clampf(pt->breath, 0.0f, 1.7f);
        v->sf = sf;
        v->bl = 0.9f * b * fm_rsqrtf(sf * 40.0f);
        v->bn = 0.05f * b * b;
        v->slo = v->sbd = v->nz = 0.0f;
        v->drift = 0.0035f * b;
        v->dph = fm_noise_ph();
    }
    return best;
}

#define BLK 32                                   /* samples per chunk */
static float mixl[BLK], mixr[BLK], esend[BLK], rsend[BLK];

static void fm_render(uint32_t n, float g)
{
    int i;
    for (i = 0; i < NFM; i++) {
        fmv_t *v = &fmv[i];
        float cph = v->cph, mph = v->mph, amp = v->amp, idx = v->idx, att = v->att;
        float cinc = v->cinc, minc = v->minc, bl = v->bl, bn = v->bn;
        uint32_t j = 0;
        if (!v->on)
            continue;
        if (v->drift > 0.0f) {                   /* (PaperLagoon) the pitch's wander, once a chunk: ~0.13 Hz */
            float w;
            v->dph += (float)n * (0.13f / EN_SR);
            if (v->dph >= 1.0f)
                v->dph -= 1.0f;
            w = 1.0f + v->drift * sine(v->dph);
            cinc *= w;
            minc *= w;
        }
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j = (uint32_t)v->wait;
            v->wait = 0;
        }
        for (; j < n; j++) {
            float m = sine(mph) * idx, x;
            float p;
            if (bl > 0.0f) {                     /* (PaperLagoon) the breath: rough, and air beside the tone */
                float wn = fm_noise(), hp;
                v->nz += 0.3f * (wn - v->nz);
                m += v->nz * bn;
                hp = wn - v->slo - 0.7f * v->sbd;
                v->sbd += v->sf * hp;
                v->slo += v->sf * v->sbd;
            }
            p = cph + m + 4.0f;                  /* + 4 turns: positive for the table */
            x = (sine(p) + bl * v->sbd) * amp * att * g;
            mixl[j] += x * v->gl * v->dry;
            mixr[j] += x * v->gr * v->dry;
            esend[j] += x * v->es;
            rsend[j] += x * v->rs;
            cph += cinc;
            mph += minc;
            if (cph >= 1.0f)
                cph -= 1.0f;
            if (mph >= 1.0f)
                mph -= 1.0f;
            amp *= v->adec;
            idx *= v->idec;
            att += (1.0f - att) * v->arate;
        }
        v->cph = cph;
        v->mph = mph;
        v->amp = amp;
        v->idx = idx;
        v->att = att;
        if (amp < 0.0006f)
            v->on = 0;
    }
}

/* ---------------------------------------------------------- the output --- */
/* AMB-1's mix: the dry voices, the tape's heads, the Echoplex and the plate (fed the sends and the echoes), soft
 * clipped (tanh) to 24 bits */
static void mix_clear(uint32_t n)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = esend[j] = rsend[j] = 0.0f;
}
static void mix_out(int32_t *out, uint32_t n, float gain, float eg, float rg, float wet)   /* (PaperLagoon) wet: the
                                                  * echoes' and the plate's return, swollen by a splash */
{
    uint32_t j;
    for (j = 0; j < n; j++) {
        float wl, wr, l, r, el, er, xl, xr;
        echoes(esend[j] * eg, &el, &er, &xl, &xr);
        reverb((rsend[j] * rg + (el + er + xl + xr) * 0.35f * rg) * 0.8f, &wl, &wr);
        l = (mixl[j] + wet * (0.75f * el + 0.3f * xl + 1.3f * wl)) * 0.36f * gain;
        r = (mixr[j] + wet * (0.75f * er + 0.3f * xr + 1.3f * wr)) * 0.36f * gain;
        age_px(&l, &r);                          /* (PaperLagoon) the age: haze, hiss, dropout, blur */
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}
