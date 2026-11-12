/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha
 * Started from fm1-base's template app (charlesvestal/fm1-base, template/app.c, MPL-2.0, by Charles Vestal) and
 * rewritten; a few of its idioms remain (the settings' defaults, app_say(), the keys' edges). */
/* PaperLagoon on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c and
 * announce.c are in scope), the glue to the engine (dsp/lagoon.c, its own -O2 unit) and the screen.
 *
 * Pages, as HYP-1's: SELECT turns through all of them, a button jumps to its own and is lit while one of its pages
 * shows (the buttons that have a page or a use glow dim); the rule over the knobs says where.
 *   1 the lagoon (HOME)      the music: K1..K4 Density, Bright, Space, Echo
 *   2..4 a voice (EDIT)      the kit's lead, answer and low voice: K1..K4 Color, Bright, Decay, Soft. EDIT again:
 *                            the next voice; EDIT held: the voice as the kit has it
 *   5 the effects (FX)       the tape echo and the plate: K1..K4 Time, Feedback, Size, Wear
 *   6 the age (FX again)     over the whole sound: K1..K4 Haze, Hiss, Dropout, Air (the voices' breath)
 *   7 the environment (ENV)  K1..K4 Drift (how far the cycles and offerings sway it), Offer, Tempo, Transpose
 *   8 the cycles (LFO)       the breath, the tide, the long turn: K1..K3 their depths, K4 Pace
 *   9 the chords (ARP)       broken chords under the music: K1..K4 Level (0: none), Stir, Spread, Wander
 *  10 the pulse (ARP again)  a muffled beat: K1..K4 Level (0: none), Stir, Wander, Weight
 *  11 the memory (SEQ)       what the music holds (a motif and the ones remembered, a chord, the loops):
 *                            K1..K4 Repeat, Change, Length, Memory. SEQ held: hold it (no new motif, chord or
 *                            colour; the changes go on), held again: let go
 *  12 global (GLO)           K1..K4 Tune, Level, Lights (the buttons' glow), Sleep (minutes: then it rests)
 * OCT- / OCT+: the music's register. REC held: the keys play, and the
 * phrase is taken in when it is let go (OCT- / OCT+ then move the keys' octave). SEL held: the knobs are the
 * lagoon's four on any page (on the lagoon, in finer steps); the strip shows it.
 * PRESETS: the kit (a set of three voices; the edits are kept per kit). ALGORITHM: the world (a place that exists
 * nowhere: it leans how the music goes, never the knobs; reached by a crossing: the header says CROSSING). PLAY: play or rest. SAVE: keep it all.
 * The keys play no notes: a press offers something to the hidden state (lagoon.h), which the music shows later and
 * loosely; the screen shows at once that something was given (a drop falling to the water, its rings), and the
 * state itself as the water's line, slowly. */
#include "fastmath.h"

#define SET_MAGIC 0x394C504Bu            /* "PLL9": the settings record; PLL8 .. PLL2 (fewer parameters; before PLL6 six
                                         * kits; ARP a switch, later levels; before PLL9 the chords' Speed, Range,
                                         * Gate) carry over */
#define SET_MAGIC_8 0x384C504Bu
#define SET_MAGIC_7 0x374C504Bu
#define SET_MAGIC_6 0x364C504Bu
#define SET_MAGIC_5 0x354C504Bu
#define SET_MAGIC_4 0x344C504Bu
#define SET_MAGIC_3 0x334C504Bu
#define SET_MAGIC_2 0x324C504Bu
typedef struct {
    uint32_t magic;
    int16_t par[P_NPARAMS];
    uint8_t kit, world;
    int8_t oct;                                  /* the music's register, -2..2 */
    uint8_t spare;
    int8_t tone[EN_NKIT][EN_NROLE][EN_NTONE];
} settings_t;
typedef struct {                                 /* PLL7: thirty-two parameters (ARP's four levels: the chords, the thump,
                                                  * the click, the brush), ARP on or off */
    uint32_t magic;
    int16_t par[32];
    uint8_t kit, world;
    int8_t oct;
    uint8_t arp;
    int8_t tone[EN_NKIT][EN_NROLE][EN_NTONE];
} settings7_t;
typedef struct {                                 /* PLL6: twenty-eight parameters, ARP a turn of four (0 off, 1 the
                                                  * chords, 2 the chords and the pulse, 3 the pulse) */
    uint32_t magic;
    int16_t par[28];
    uint8_t kit, world;
    int8_t oct;
    uint8_t arp;
    int8_t tone[EN_NKIT][EN_NROLE][EN_NTONE];
} settings6_t;
#define OLD_NKIT 6                               /* the kits before PLL6 */
typedef struct {                                 /* PLL5: twenty-eight parameters, six kits */
    uint32_t magic;
    int16_t par[28];
    uint8_t kit, world;
    int8_t oct;
    uint8_t arp;
    int8_t tone[OLD_NKIT][EN_NROLE][EN_NTONE];
} settings5_t;
typedef struct {                                 /* PLL4: twenty-four parameters (before the age) */
    uint32_t magic;
    int16_t par[24];
    uint8_t kit, world;
    int8_t oct;
    uint8_t arp;
    int8_t tone[OLD_NKIT][EN_NROLE][EN_NTONE];
} settings4_t;
typedef struct {                                 /* PLL3: twelve parameters */
    uint32_t magic;
    int16_t par[12];
    uint8_t kit, world;
    int8_t tone[OLD_NKIT][EN_NROLE][EN_NTONE];
} settings3_t;
typedef struct {                                 /* PLL2: the lagoon's four parameters only */
    uint32_t magic;
    int16_t par[4];
    uint8_t kit, world;                          /* (world was a spare byte: older records load world 0) */
    int8_t tone[OLD_NKIT][EN_NROLE][EN_NTONE];
} settings2_t;
static settings_t set;
static uint8_t set_dirty;

static void tone_defaults(int k, int r)
{
    int f;
    for (f = 0; f < EN_NTONE; f++)
        set.tone[k][r][f] = (int8_t)en_tone_default(k, r, f);
}

static void settings_defaults(void)
{
    int i;
    set.magic = SET_MAGIC;
    for (i = 0; i < P_NPARAMS; i++)
        set.par[i] = en_param_info(i)->def;
    set.kit = 0;
    set.world = 0;
    set.oct = 0;
    set.spare = 0;
    for (i = 0; i < EN_NKIT * EN_NROLE; i++)
        tone_defaults(i / EN_NROLE, i % EN_NROLE);
}

/* an older record's ARP, as the two pages' levels: the chords' level, the pulse's (its weight: the thump against the
 * click and the brush) */
static void arp_carry(int chords, int thump, int light)
{
    set.par[P_CH_LEVEL] = (int16_t)clamp(chords, 0, 100);
    set.par[P_PU_LEVEL] = (int16_t)clamp(thump > light ? thump : light, 0, 100);
    set.par[P_PU_WEIGHT] = (int16_t)(thump + light ? thump * 100 / (thump + light) : 50);
}

/* ------------------------------------------------------------ contract --- */
void app_init(void)
{
    int i, f;
    static union {
        settings_t v8;
        settings7_t v7;
        settings6_t v6;
        settings5_t v5;
        settings4_t v4;
        settings3_t v3;
        settings2_t v2;
    } ld;
    int n;
    en_init();
    n = plat_store_load(OBJ_SETTINGS, &ld, sizeof ld);
    if (n == (int)sizeof(settings_t) && ld.v8.magic == SET_MAGIC) {
        set = ld.v8;
    } else if (n == (int)sizeof(settings_t) && ld.v8.magic == SET_MAGIC_8) {   /* PLL8: the same, but the chords'
                                                                                  * Speed (0..3) and Range (1..3) as
                                                                                  * Stir and Spread; Gate gone */
        int sp = ld.v8.par[P_CH_BUSY], rg = ld.v8.par[P_CH_SPREAD];
        set = ld.v8;
        set.magic = SET_MAGIC;
        set.par[P_CH_BUSY] = (int16_t)(clamp(sp, 0, 3) * 100 / 3);
        set.par[P_CH_SPREAD] = (int16_t)((clamp(rg, 1, 3) - 1) * 50);
        set.par[P_CH_WANDER] = en_param_info(P_CH_WANDER)->def;
    } else if (n == (int)sizeof(settings7_t) && ld.v7.magic == SET_MAGIC_7) {   /* older records: carried over */
        settings_defaults();
        for (i = 0; i < 28; i++)
            set.par[i] = ld.v7.par[i];
        set.kit = ld.v7.kit;
        set.world = ld.v7.world;
        set.oct = ld.v7.oct;
        if (ld.v7.arp)
            arp_carry(ld.v7.par[28], ld.v7.par[29], (ld.v7.par[30] + ld.v7.par[31]) / 2);
        for (i = 0; i < (int)sizeof ld.v7.tone; i++)
            ((int8_t *)set.tone)[i] = ((const int8_t *)ld.v7.tone)[i];
    } else if (n == (int)sizeof(settings6_t) && ld.v6.magic == SET_MAGIC_6) {   /* older records: carried over */
        settings_defaults();
        for (i = 0; i < 28; i++)
            set.par[i] = ld.v6.par[i];
        set.kit = ld.v6.kit;
        set.world = ld.v6.world;
        set.oct = ld.v6.oct;
        if (ld.v6.arp)                           /* its turn: the chords alone, both, the pulse alone */
            arp_carry(ld.v6.arp <= 2 ? 50 : 0, ld.v6.arp >= 2 ? 50 : 0, ld.v6.arp >= 2 ? 50 : 0);
        for (i = 0; i < (int)sizeof ld.v6.tone; i++)
            ((int8_t *)set.tone)[i] = ((const int8_t *)ld.v6.tone)[i];
    } else if (n == (int)sizeof(settings5_t) && ld.v5.magic == SET_MAGIC_5) {   /* older records: carried over (the
                                                                                    * new kits as they come) */
        settings_defaults();
        for (i = 0; i < 28; i++)
            set.par[i] = ld.v5.par[i];
        set.kit = ld.v5.kit;
        set.world = ld.v5.world;
        set.oct = ld.v5.oct;
        if (ld.v5.arp)                           /* (then: the chords alone) */
            arp_carry(50, 0, 0);
        set.par[P_AIR] = en_param_info(P_AIR)->def;   /* (its place was Blur's) */
        for (i = 0; i < (int)sizeof ld.v5.tone; i++)
            ((int8_t *)set.tone)[i] = ((const int8_t *)ld.v5.tone)[i];
    } else if (n == (int)sizeof(settings4_t) && ld.v4.magic == SET_MAGIC_4) {   /* older records: carried over */
        settings_defaults();
        for (i = 0; i < 24; i++)
            set.par[i] = ld.v4.par[i];
        set.kit = ld.v4.kit;
        set.world = ld.v4.world;
        set.oct = ld.v4.oct;
        if (ld.v4.arp)                           /* (then: the chords alone) */
            arp_carry(50, 0, 0);
        for (i = 0; i < (int)sizeof ld.v4.tone; i++)
            ((int8_t *)set.tone)[i] = ((const int8_t *)ld.v4.tone)[i];
    } else if (n == (int)sizeof(settings3_t) && ld.v3.magic == SET_MAGIC_3) {   /* older records: carried over */
        settings_defaults();
        for (i = 0; i < 12; i++)
            set.par[i] = ld.v3.par[i];
        set.kit = ld.v3.kit;
        set.world = ld.v3.world;
        for (i = 0; i < (int)sizeof ld.v3.tone; i++)
            ((int8_t *)set.tone)[i] = ((const int8_t *)ld.v3.tone)[i];
    } else if (n == (int)sizeof(settings2_t) && ld.v2.magic == SET_MAGIC_2) {
        settings_defaults();
        for (i = 0; i < 4; i++)
            set.par[i] = ld.v2.par[i];
        set.kit = ld.v2.kit;
        set.world = ld.v2.world;
        for (i = 0; i < (int)sizeof ld.v2.tone; i++)
            ((int8_t *)set.tone)[i] = ((const int8_t *)ld.v2.tone)[i];
    } else {
        settings_defaults();
    }
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (set.par[i] < p->lo || set.par[i] > p->hi)
            set.par[i] = p->def;
        en_set(i, set.par[i]);
    }
    if (set.kit >= EN_NKIT)
        set.kit = 0;
    en_set_kit(set.kit);
    if (set.world >= EN_NWORLD)
        set.world = 0;
    en_set_world_now(set.world);
    set.oct = (int8_t)clamp(set.oct, -2, 2);
    en_octave(set.oct);
    for (i = 0; i < EN_NKIT * EN_NROLE; i++)
        for (f = 0; f < EN_NTONE; f++) {
            int8_t *v = &set.tone[i / EN_NROLE][i % EN_NROLE][f];
            if (*v < 0 || *v > en_tone_hi(f))
                *v = (int8_t)en_tone_default(i / EN_NROLE, i % EN_NROLE, f);
            en_set_tone(i / EN_NROLE, i % EN_NROLE, f, *v);
        }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) { en_render(lr, n, master_q12); }

void app_panic(void)
{
    en_play(0);
    en_panic();
}

/* ------------------------------------------------------------------ UI --- */
enum { PG_LAGOON, PG_LEAD, PG_ANSWER, PG_LOW, PG_FX, PG_AGE, PG_ENV, PG_LFO, PG_CHORDS, PG_PULSE, PG_SEQ, PG_GLO, NPAGE };
#define PG_GROUP(p) (PG_VOICE(p) ? 1 : (p) == PG_FX || (p) == PG_AGE ? 2 : (p) == PG_CHORDS || (p) == PG_PULSE ? 3 : 0)
                                                 /* pages one button walks */
#define ARP_ON (set.par[P_CH_LEVEL] > 0 || set.par[P_PU_LEVEL] > 0)   /* the chords or the pulse sound */
#define PG_ROLE(p) ((p) - PG_LEAD)               /* a voice page's R_* */
#define PG_VOICE(p) ((p) >= PG_LEAD && (p) <= PG_LOW)
#define HOLD_MS 800u                             /* EDIT or SEQ held this long: its second use */

static int page_par(int p)                       /* a page's first parameter (P_*), or -1 (a voice page) */
{
    return p == PG_LAGOON ? P_DENSITY : p == PG_FX ? P_TIME : p == PG_AGE ? P_HAZE : p == PG_CHORDS ? P_CH_LEVEL : p == PG_PULSE ? P_PU_LEVEL : p == PG_SEQ ? P_REPEAT : p == PG_ENV ? P_DRIFT :
           p == PG_LFO ? P_BREATH : p == PG_GLO ? P_TUNE : -1;
}

static const char *page_name(int p)
{
    return p == PG_LAGOON ? "LAGOON" : p == PG_FX ? "EFFECTS" : p == PG_AGE ? "AGE" : p == PG_CHORDS ? "CHORDS" : p == PG_PULSE ? "PULSE" : p == PG_SEQ ? "MEMORY" : p == PG_ENV ? "ENVIRONMENT" :
           p == PG_LFO ? "CYCLES" : p == PG_GLO ? "GLOBAL" : en_role_name(PG_ROLE(p));
}

static struct {
    char msg[2][32];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t turned_at[4];                       /* each knob's last turn (the strip lights it) */
    uint32_t last_draw;
    uint32_t hb_at[2];                           /* EDIT, SEQ went down (0: up) */
    uint32_t audition_at;                        /* a voice was edited: let it sound then (0: none) */
    uint32_t key_at[NKEYS];                      /* each held key's next offering (0: up) */
    struct {
        uint32_t born;                           /* 0: none */
        int16_t x;
        uint8_t big;
    } drop[8];
    uint8_t drop_i;
    uint8_t hb_done[2];                          /* its hold has acted: the release does nothing */
    uint8_t page;
    uint8_t redraw;
    uint8_t shift;                               /* SEL held: the knobs are the lagoon's */
    int8_t rec_oct;                              /* the keys' octave while REC */
    uint32_t play_at;                            /* playing since (the sleep timer) */
} ui;

/* s copied into d (cap bytes with the terminator); returns the length written */
static uint32_t str_put(char *d, uint32_t cap, const char *s)
{
    uint32_t k = 0;
    if (!cap)
        return 0;
    for (; s && s[k] && k + 1 < cap; k++)
        d[k] = s[k];
    d[k] = 0;
    return k;
}

/* a whole number in decimal, a minus sign when below zero (b: 12 bytes). 32-bit only: no 64-bit multiply
 * (felucca-bsp docs/CPU.md, which build.py checks for) */
static void int_text(int32_t v, char *b)
{
    char t[10];
    uint32_t x = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    int n = 0;
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + x % 10u);
        x /= 10u;
    } while (x);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* a ratio as "3.50" (b: 16 bytes) */
static void ratio_text(float r, char *b)
{
    int32_t c = (int32_t)(r * 100.0f + 0.5f);
    uint32_t k;
    int_text(c / 100, b);
    for (k = 0; b[k]; k++)
        ;
    b[k++] = '.';
    b[k++] = (char)('0' + c / 10 % 10);
    b[k++] = (char)('0' + c % 10);
    b[k] = 0;
}

/* two words over the header for a moment, and to a screen reader (announce.c) as one line */
void app_say(const char *a, const char *b)
{
    char line[50];
    uint32_t k;
    str_put(ui.msg[0], sizeof ui.msg[0], a);
    str_put(ui.msg[1], sizeof ui.msg[1], b);
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (!ann_on())
        return;
    k = str_put(line, sizeof line, ui.msg[0]);
    if (ui.msg[1][0] && k + 1 < sizeof line) {
        line[k++] = ' ';
        str_put(line + k, sizeof line - k, ui.msg[1]);
    }
    ann_say(line, 0, ANN_NOW);
}

/* a page: the header shows it (its name, and where it is in the strip of pages); a screen reader hears "name n of m" */
static void page_go(int p)
{
    char t[32];
    uint32_t k;
    p = p < 0 ? 0 : p >= NPAGE ? NPAGE - 1 : p;
    if (p == ui.page)
        return;
    ui.page = (uint8_t)p;
    ui.msg_until = 0;                            /* a message over the header gives way to the page */
    ui.redraw = 1;
    if (!ann_on())
        return;
    k = str_put(t, sizeof t, page_name(p));
    k += str_put(t + k, sizeof t - k, " ");
    int_text(p + 1, t + k);
    k += p + 1 >= 10 ? 2 : 1;
    k += str_put(t + k, sizeof t - k, " of ");
    int_text(NPAGE, t + k);
    ann_say(t, 0, ANN_NOW);
}

static void par_text(int p, int v, char *t)       /* a parameter's value as the screen says it */
{
    if (p == P_LIGHTS)                           /* the dim lights: off, low, high */
        str_put(t, 12, v == 0 ? "OFF" : v == 1 ? "LOW" : "HIGH");
    else if (p == P_SLEEP && !v)
        str_put(t, 12, "OFF");
    else
        int_text(v, t);
}

static void param_step(int p, int knob, int32_t d)
{
    const en_param_t *pi = en_param_info(p);
    int step = pi->hi - pi->lo <= 30 || (ui.shift && ui.page == PG_LAGOON) ? 1 : 2;   /* small ranges, SEL: fine */
    int32_t v = clamp(set.par[p] + step * d, pi->lo, pi->hi);
    char t[12];
    ui.turned_at[knob] = plat_ms();
    if (v == set.par[p])
        return;
    set.par[p] = (int16_t)v;
    set_dirty = 1;
    en_set(p, (int)v);
    if (p == P_SLEEP)                            /* the sleep timer counts from when it is set */
        ui.play_at = plat_ms();
    par_text(p, (int)v, t);
    app_say(pi->name, t);
}

static void tone_step(int role, int f, int32_t d)
{
    int8_t *v = &set.tone[set.kit][role][f];
    int32_t n = clamp(*v + (f == T_COLOR ? d : 2 * d), 0, en_tone_hi(f));
    char t[16];
    ui.turned_at[f] = plat_ms();
    if (n == *v)
        return;
    *v = (int8_t)n;
    set_dirty = 1;
    en_set_tone(set.kit, role, f, (int)n);
    ui.audition_at = plat_ms() + 250u;           /* when the turning pauses, the voice sounds */
    if (f == T_COLOR)
        ratio_text(en_ratio((int)n), t);
    else
        int_text(n, t);
    app_say(en_tone_name(f), t);
}

static void tone_reset(int role)
{
    int f;
    tone_defaults(set.kit, role);
    for (f = 0; f < EN_NTONE; f++)
        en_set_tone(set.kit, role, f, set.tone[set.kit][role][f]);
    set_dirty = 1;
    ui.audition_at = plat_ms() + 1u;
    app_say(en_role_name(role), "AS IT WAS");
}

static void world_step(int32_t d)
{
    char t[12];
    uint32_t k;
    int n = ((int)set.world + (int)d) % EN_NWORLD;
    if (n < 0)
        n += EN_NWORLD;
    set.world = (uint8_t)n;
    set_dirty = 1;
    en_set_world(n);
    int_text(n + 1, t);
    k = (uint32_t)(n + 1 >= 10 ? 2 : 1);
    t[k++] = '/';
    int_text(EN_NWORLD, t + k);
    app_say(en_world_name(n), t);
}

static void kit_step(int32_t d)
{
    char t[12];
    uint32_t k;
    int n = ((int)set.kit + (int)d) % EN_NKIT;
    if (n < 0)
        n += EN_NKIT;
    set.kit = (uint8_t)n;
    set_dirty = 1;
    en_set_kit(n);
    int_text(n + 1, t);
    k = (uint32_t)(n + 1 >= 10 ? 2 : 1);
    t[k++] = '/';
    int_text(EN_NKIT, t + k);
    app_say(en_kit_name(n), t);
}

#define DROP_FALL_MS 380u                        /* a drop's fall to the water */
#define DROP_RING_MS 2600u                       /* its rings */
#define HOLD_OFFER_MS 700u                       /* a held key offers again, less, this often */

static void key_offer(uint32_t k, int strength, uint32_t now)
{
    en_offer((int)k, strength);
    ui.drop[ui.drop_i].born = now ? now : 1u;
    ui.drop[ui.drop_i].x = (int16_t)(12 + (int32_t)k * 216 / (NKEYS - 1));
    ui.drop[ui.drop_i].big = (uint8_t)(strength >= 100);
    ui.drop_i = (uint8_t)((ui.drop_i + 1u) % 8u);
}

/* a button with two uses: 1 when let go soon, 2 once when held HOLD_MS (its release then does nothing) */
static int press(int h, uint32_t b, uint32_t btn, uint32_t down, uint32_t up, uint32_t now)
{
    if (down & 1u << b) {
        ui.hb_at[h] = now ? now : 1u;
        ui.hb_done[h] = 0;
    }
    if (ui.hb_at[h] && !ui.hb_done[h] && (btn & 1u << b) && now - ui.hb_at[h] >= HOLD_MS) {
        ui.hb_done[h] = 1;
        return 2;
    }
    if (up & 1u << b) {
        int r = ui.hb_at[h] && !ui.hb_done[h];
        ui.hb_at[h] = 0;
        return r;
    }
    return 0;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), k;
    uint32_t down = btn & ~ui.btn, up = ui.btn & ~btn;
    int32_t e;
    if (down & 1u << B_PLAY) {
        en_play(!en_playing);
        app_say(en_playing ? "REST" : "PLAY", "");
    }
    if (down & 1u << B_SAVE) {
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
            set_dirty = 0;
            app_say("SAVED", "");
        } else {
            app_say("SAVE", "FAILED");
        }
    }
    if (down & 1u << B_HOME)
        page_go(PG_LAGOON);
    if (down & 1u << B_FX)                       /* the effects; again: the age, and back */
        page_go(ui.page == PG_FX ? PG_AGE : PG_FX);
    if (down & 1u << B_ENV)
        page_go(PG_ENV);
    if (down & 1u << B_LFO)
        page_go(PG_LFO);
    if (down & 1u << B_GLO)
        page_go(PG_GLO);
    ui.shift = (uint8_t)((btn >> B_SEL) & 1u);
    if ((down | up) & 1u << B_SEL)
        ui.redraw = 1;
    if (down & 1u << B_ARP)                      /* the chords; again: the pulse, and back */
        page_go(ui.page == PG_CHORDS ? PG_PULSE : PG_CHORDS);
    if (down & 1u << B_REC) {                    /* REC held: the keys play; let go: the phrase is taken in */
        ui.rec_oct = 0;
        en_rec(1);
        app_say("REC", "PLAY A PHRASE");
    }
    if (up & 1u << B_REC) {
        en_rec(0);
        app_say("TAKEN IN", "");
    }
    if (down & (1u << B_OCTDN | 1u << B_OCTUP)) {
        int d = down & 1u << B_OCTUP ? 1 : -1;
        char t[12];
        if (btn & 1u << B_REC) {                 /* while REC: the keys' octave */
            ui.rec_oct = (int8_t)clamp(ui.rec_oct + d, -2, 2);
            int_text(ui.rec_oct, t);
            app_say("KEYS OCT", t);
        } else {
            set.oct = (int8_t)clamp(set.oct + d, -2, 2);
            set_dirty = 1;
            en_octave(set.oct);
            int_text(set.oct, t);
            app_say("OCTAVE", t);
        }
    }
    if (down & 1u << B_PLAY && !en_playing)
        ui.play_at = now;
    switch (press(0, B_EDIT, btn, down, up, now)) {   /* EDIT: the voices; held: the voice as the kit has it */
    case 1: page_go(PG_VOICE(ui.page) && ui.page != PG_LOW ? ui.page + 1 : PG_LEAD); break;
    case 2:
        if (PG_VOICE(ui.page))
            tone_reset(PG_ROLE(ui.page));
        break;
    }
    switch (press(1, B_SEQ, btn, down, up, now)) {    /* SEQ: the memory; held: hold, or let go */
    case 1: page_go(PG_SEQ); break;
    case 2:
        en_hold(!en_held);
        app_say(en_held ? "LET GO" : "HELD", "");
        break;
    }
    for (k = 0; k < NKEYS; k++) {                /* the keys: offerings (while REC: notes) */
        uint32_t bit = 1u << k;
        if ((btn & 1u << B_REC) && (keys & bit) && !(ui.keys & bit)) {
            en_rec_note(53 + (int)k + 12 * ui.rec_oct);   /* key 0 = F3 */
            ui.key_at[k] = 0;
            continue;
        }
        if (btn & 1u << B_REC)
            continue;
        if ((keys & bit) && !(ui.keys & bit)) {
            key_offer(k, 100, now);
            ui.key_at[k] = now + HOLD_OFFER_MS;
        } else if ((keys & bit) && ui.key_at[k] && now >= ui.key_at[k]) {
            key_offer(k, 35, now);
            ui.key_at[k] = now + HOLD_OFFER_MS;
        } else if (!(keys & bit)) {
            ui.key_at[k] = 0;
        }
    }
    if ((e = plat_enc(EN_SELECT)) != 0)
        page_go((int)ui.page + (int)e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        kit_step(e);
    if ((e = plat_enc(EN_ALGO)) != 0)
        world_step(e);
    for (k = 0; k < 4; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0) {
            if (ui.shift)                        /* SEL: the lagoon's four, from anywhere */
                param_step(P_DENSITY + (int)k, (int)k, e);
            else if (PG_VOICE(ui.page))
                tone_step(PG_ROLE(ui.page), (int)k, e);
            else
                param_step(page_par(ui.page) + (int)k, (int)k, e);
        }
    if (ui.audition_at && now >= ui.audition_at) {
        ui.audition_at = 0;
        if (PG_VOICE(ui.page))
            en_audition(PG_ROLE(ui.page));
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {   /* lit: the page's button, playing, unsaved, held; dim: every button that has a page or a use */
        uint32_t page_btn = ui.page == PG_LAGOON ? 1u << B_HOME : ui.page == PG_FX || ui.page == PG_AGE ? 1u << B_FX :
                            ui.page == PG_SEQ ? 1u << B_SEQ : ui.page == PG_ENV ? 1u << B_ENV : PG_GROUP(ui.page) == 3 ? 1u << B_ARP :
                            ui.page == PG_LFO ? 1u << B_LFO : ui.page == PG_GLO ? 1u << B_GLO : 1u << B_EDIT;
        uint32_t lit = btn | page_btn | (set_dirty ? 1u << B_SAVE : 0u) | (en_playing ? 1u << B_PLAY : 0u) |
                       (en_held ? 1u << B_SEQ : 0u) | (ARP_ON ? 1u << B_ARP : 0u) |
                       (set.oct > 0 ? 1u << B_OCTUP : 0u) | (set.oct < 0 ? 1u << B_OCTDN : 0u);
        uint32_t dim = (1u << NB) - 1u;          /* every button has a use */
        plat_lights(lit, keys, dim, 0, set.par[P_LIGHTS]);
    }
}

void app_input_only(void) { input(); }

/* a ring: the midpoint circle, clipped by the canvas */
static void draw_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x = r, y = 0, err = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c);
        cv_pset(cx + y, cy + x, c);
        cv_pset(cx - y, cy + x, c);
        cv_pset(cx - x, cy + y, c);
        cv_pset(cx - x, cy - y, c);
        cv_pset(cx - y, cy - x, c);
        cv_pset(cx + y, cy - x, c);
        cv_pset(cx + x, cy - y, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* the header: the world (as HYP-1's the piece), whatever the page; playing or resting at its right. A message over it
 * for a moment; REC said over the rest */
static void draw_header(uint32_t now)
{
    int32_t x, end;
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {                    /* a long message in the smaller letters */
        int big = 18 + text_w(&FONT_B, ui.msg[0]) + text_w(&FONT_B, ui.msg[1]) <= 230;
        int32_t y = big ? 6 : 9;
        x = cv_text(10, y, big ? &FONT_B : &FONT_XS, ui.msg[0], C_WHITE);
        cv_text(x + 8, y, big ? &FONT_B : &FONT_XS, ui.msg[1], C_HI);
    } else if (en_recording) {
        end = cv_text(10, 6, &FONT_B, "REC", C_WHITE);
        cv_text(end + 8, 10, &FONT_XS, "THE KEYS PLAY; LET GO TO GIVE IT", C_AMB);
    } else {
        const char *s = en_crossing ? "CROSSING" : 0;   /* (playing or resting: the name says it, white or grey) */
        uint16_t c = en_playing ? C_WHITE : C_GRAY;   /* resting: the name greyed (a long one leaves no room to say) */
        if (en_crossing) {                       /* a crossing: the name not yet reached breathes, a wake runs under */
            int32_t ph = (int32_t)(now % 1600u), b = ph < 800 ? ph : 1600 - ph, i;
            c = b < 270 ? C_GRAY : b < 540 ? C_HI : C_WHITE;
            for (i = 0; i < 5; i++)
                cv_rect(10 + (int32_t)((now / 12u + (uint32_t)i * 47u) % 220u), 29, 6 - i, 1, i ? C_GRAY : C_HI);
        }
        end = cv_text(10, 6, &FONT_B, en_world_name(set.world), c);
        if (s && (x = 230 - text_w(&FONT_XS, s)) > end + 8)   /* where a long name leaves room */
            cv_text(x, 10, &FONT_XS, s, C_AMB);
    }
    cv_blit(0, 0);
}

/* the rule over the knobs (after HYP-1's): at its left what the knobs act on, if it needs saying (a voice: the kit;
 * SEL held: the lagoon's four; the memory held); at its right the page's name, then a short bar a page, the one shown
 * lit, the voices' three a little brighter while one of them shows */
static void draw_rule(void)
{
    const char *ctx = 0, *name = PG_VOICE(ui.page) ? en_role_name(PG_ROLE(ui.page)) : page_name(ui.page);
    uint16_t cc = C_AMB;
    int32_t xd = 231 - NPAGE * 5, xt = xd - 6 - text_w(&FONT_XS, name), xs = 0, i;
    if (ui.shift)
        ctx = ui.page == PG_LAGOON ? "SEL: FINE" : "SEL: LAGOON";
    else if (en_crossing)                        /* (any page: a crossing under way) */
        ctx = "CROSSING";
    else if (ui.page == PG_SEQ && en_held)
        ctx = "HELD";
    else if ((ui.page == PG_CHORDS && !set.par[P_CH_LEVEL]) || (ui.page == PG_PULSE && !set.par[P_PU_LEVEL]))
        ctx = "LEVEL 0: SILENT", cc = C_GRAY;
    else if (PG_VOICE(ui.page))
        ctx = en_kit_name(set.kit), cc = C_GRAY;
    if (ctx)
        xs = cv_text(6, 0, &FONT_XS, ctx, cc) + 6;
    if (xt - 6 > xs)
        cv_rect(xs, 5, xt - 6 - xs, 1, C_LINE);
    cv_text(xt, 0, &FONT_XS, name, C_HI);
    for (i = 0; i < NPAGE; i++) {
        int cur = i == (int32_t)ui.page, group = PG_GROUP(i) && PG_GROUP(i) == PG_GROUP(ui.page);
        cv_rect(xd + i * 5, cur ? 2 : 3, 3, cur ? 7 : 5, cur ? C_HI : group ? C_GRAY : C_LINE);
    }
}

/* a flat ring on the water: an ellipse, rx wide, a third as tall */
static void draw_ellipse(int32_t cx, int32_t cy, int32_t rx, uint16_t c)
{
    int32_t i, n = rx < 8 ? 16 : 40;
    for (i = 0; i < n; i++) {
        float a = (float)i * (FM_TWO_PI / (float)n);
        cv_pset(cx + (int32_t)(fm_cosf(a) * (float)rx), cy + (int32_t)(fm_sinf(a) * (float)rx * 0.3f), c);
    }
}

/* the lagoon. The water's line is the hidden state, slowly: higher in light, rougher when folded, its dots closer
 * when it bleeds, its waves slower in the margin. Each sounding voice a ripple, wider as it fades; a slow breath at the
 * centre (the shortest cycle). An offering: a drop falls to the water at once, and rings spread */
static void draw_lagoon(uint32_t now)
{
    uint32_t i;
    float light = en_state[S_LIGHT], bleed = en_state[S_BLEED], fold = en_state[S_FOLD], margin = en_state[S_MARGIN];
    float b = 0.5f + 0.5f * fm_sinf(en_cycle * FM_TWO_PI), t = (float)now * 0.001f;
    float amp = 1.5f + 3.5f * (fold + 1.0f), speed = 0.9f - 0.6f * margin;
    int32_t hy = 80 - (int32_t)(light * 26.0f), x, gap = 6 - (int32_t)(bleed * 3.0f);
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 2, &FONT_XS, en_kit_name(set.kit), C_GRAY);
    static uint32_t hits, hit_at;
    static uint8_t hit_what;
    uint16_t wc = C_LINE;
    if (en_pulse_hits != hits) {                 /* the pulse: a thump shivers the water, a click sparks on it */
        hits = en_pulse_hits;
        hit_at = now;
        hit_what = en_pulse_last;
    }
    if (hit_what == 0 && now - hit_at < 160u)
        wc = C_GRAY, amp += 1.5f;
    if (en_alg == EN_ALG_DRONE)                             /* the drone: a heavy, slow water */
        speed *= 0.5f;
    for (x = 0; x < 240; x += gap < 2 ? 2 : gap) {
        float y = (float)hy + amp * fm_sinf((float)x * 0.045f + t * speed) +
                  0.5f * amp * fm_sinf((float)x * 0.11f - t * speed * 1.7f);
        cv_pset(x, (int32_t)y, wc);
        if (hit_what && now - hit_at < 90u && ((uint32_t)x * 2654435761u + hits * 40503u) >> 27 == 0)
            cv_rect(x, (int32_t)y - 2, 2, 2, hit_what == 1 ? C_WHITE : C_GRAY);
        if (en_alg == EN_ALG_DRONE) {
            cv_pset(x + gap / 2, (int32_t)y + 3, C_LINE);
            cv_pset(x, (int32_t)y + 6, C_LINE);
        }
    }
    draw_ring(120, hy, 3 + (int32_t)(4.0f * b), C_LINE);
    for (i = 0; i < EN_NVOICE; i++) {
        float g = en_glow[i];
        int32_t r;
        if (g < 0.02f)
            continue;
        x = 20 + ((int32_t)en_voice_note[i] - 41) * 4;
        x = x < 10 ? 10 : x > 230 ? 230 : x;
        if (en_alg == EN_ALG_DRONE) {                       /* the drone: bands that swell with the sound and sink with it */
            int32_t rx = 6 + (int32_t)(g * 54.0f);
            draw_ellipse(x, hy + 3, rx, g > 0.5f ? C_HI : g > 0.2f ? C_AMB : C_DIM);
            draw_ellipse(x, hy + 3, rx * 2 / 3, g > 0.5f ? C_AMB : C_DIM);
            continue;
        }
        if (en_alg == EN_ALG_LETTER) {           /* the letter: each word a point lifting off the water, a small ring */
            cv_rect(x, hy - 8 - (int32_t)((1.0f - g) * 44.0f), 2, 2, g > 0.5f ? C_WHITE : g > 0.2f ? C_HI : C_DIM);
            draw_ring(x, hy, 3 + (int32_t)((1.0f - g) * 16.0f), g > 0.3f ? C_AMB : C_DIM);
            continue;
        }
        if (en_alg == EN_ALG_FOUNTAIN) {         /* the fountain: small rings where its drops fall */
            draw_ellipse(x, hy, 3 + (int32_t)((1.0f - g) * 18.0f), g > 0.3f ? C_GRAY : C_DIM);
            continue;
        }
        r = 4 + (int32_t)((1.0f - g) * 56.0f);
        draw_ring(x, hy, r, g > 0.5f ? C_HI : g > 0.2f ? C_AMB : C_DIM);
        if (g > 0.6f)
            draw_ring(x, hy, r / 2, C_AMB);
    }
    if (en_alg == EN_ALG_FOUNTAIN && en_fig_len) {   /* the fountain: its figure as drops over the water, one lit */
        int32_t lo = 99, hi = -99, n = en_fig_len > EN_FIG ? EN_FIG : en_fig_len, x0 = 120 - (n - 1) * 8;
        for (i = 0; i < (uint32_t)n; i++) {
            lo = en_fig_deg[i] < lo ? en_fig_deg[i] : lo;
            hi = en_fig_deg[i] > hi ? en_fig_deg[i] : hi;
        }
        for (i = 0; i < (uint32_t)n; i++) {
            int32_t y = hy - 10 - 34 * (en_fig_deg[i] - lo) / (hi > lo ? hi - lo : 1);
            int cur = i == en_fig_pos;
            cv_rect(x0 + (int32_t)i * 16 - (cur ? 1 : 0), y - (cur ? 1 : 0), cur ? 4 : 2, cur ? 4 : 2,
                    cur ? C_WHITE : C_HI);
        }
    }
    if (en_alg == EN_ALG_LOOPS)                             /* the loops: four buoys, a light going round each */
        for (i = 0; i < 4; i++) {
            float ph = en_loop_ph[i], a = ph * FM_TWO_PI - FM_PI / 2.0f;
            int32_t bx = 36 + (int32_t)i * 56, by = hy - 26;
            draw_ring(bx, by, 9, ph < 0.06f ? C_WHITE : C_LINE);
            cv_rect(bx + (int32_t)(fm_cosf(a) * 9.0f) - 1, by + (int32_t)(fm_sinf(a) * 9.0f) - 1, 3, 3, C_HI);
            if (ph < 0.15f)
                draw_ring(bx, by, 9 + (int32_t)(ph * 80.0f), C_GRAY);
        }
    for (i = 0; i < 8; i++) {                    /* the offerings */
        uint32_t age;
        if (!ui.drop[i].born)
            continue;
        age = now - ui.drop[i].born;
        if (age < DROP_FALL_MS) {                /* falling */
            int32_t y = (int32_t)((float)hy * (float)(age * age) / (float)(DROP_FALL_MS * DROP_FALL_MS));
            cv_rect(ui.drop[i].x, y, 2, ui.drop[i].big ? 4 : 2, C_WHITE);
        } else if (age < DROP_FALL_MS + DROP_RING_MS) {   /* rings, spreading */
            float f = (float)(age - DROP_FALL_MS) / (float)DROP_RING_MS;
            int32_t rx = 3 + (int32_t)(f * (ui.drop[i].big ? 70.0f : 34.0f));
            uint16_t c = f < 0.3f ? C_WHITE : f < 0.65f ? C_GRAY : C_DIM;
            draw_ellipse(ui.drop[i].x, hy, rx, c);
            if (ui.drop[i].big && f < 0.7f)
                draw_ellipse(ui.drop[i].x, hy, rx * 3 / 5, c);
        } else {
            ui.drop[i].born = 0;
        }
    }
    cv_blit(0, 36);
}

/* a voice: its shape over ten seconds (the attack's rise, the fall), its ratio, and its ripple when it sounds */
static void draw_voice(int role)
{
    float ratio, att, t60, ia, id;
    int32_t x, y0 = 0, i;
    char t[24];
    uint32_t k;
    en_voice_shape(set.kit, role, &ratio, &att, &t60);
    ia = 1.0f / fm_maxf(att * 0.001f, 0.0005f);
    id = -6.9f / t60;
    cv_begin(240, 144, C_BLACK);
    cv_rect(10, 130, 220, 1, C_LINE);
    for (i = 1; i < 10; i++)                     /* the seconds */
        cv_rect(10 + i * 22, 128, 1, 3, C_LINE);
    for (x = 0; x < 220; x++) {
        float s = (float)x * (10.0f / 220.0f);
        float a = (1.0f - fm_expf(-s * ia)) * fm_expf(s * id);
        int32_t y = 130 - (int32_t)(a * 90.0f);
        if (x)
            cv_line(9 + x, y0, 10 + x, y, C_HI);
        y0 = y;
    }
    k = str_put(t, sizeof t, "1 : ");
    ratio_text(ratio, t + k);
    cv_text(10, 4, &FONT_XS, "CARRIER : MODULATOR", C_GRAY);
    cv_text(10, 18, &FONT_B, t, C_WHITE);
    if (en_voice_breath(set.kit, role)) {        /* the voice's air (the kit's, times Air): grains over the shape */
        static uint32_t seed = 0x51ED270Bu;
        int32_t b = en_voice_breath(set.kit, role), y1 = 0;
        cv_text(150, 64, &FONT_XS, "AIR", C_GRAY);
        int_text(b, t);
        cv_text(206, 64, &FONT_XS, t, C_HI);
        for (i = 0; i < b * 2; i++) {
            seed = seed * 1664525u + 1013904223u;
            x = (int32_t)((seed >> 8) % 220u);
            {
                float s = (float)x * (10.0f / 220.0f);
                y1 = 130 - (int32_t)((1.0f - fm_expf(-s * ia)) * fm_expf(s * id) * 90.0f);
            }
            cv_pset(10 + x, y1 - 2 - (int32_t)((seed >> 4) % 9u), C_DIM);
        }
    }
    for (i = 0; i < EN_NVOICE; i++) {            /* the voice, sounding */
        float g = en_glow[i];
        if (g < 0.02f || en_voice_role[i] != role)
            continue;
        draw_ring(200, 28, 3 + (int32_t)((1.0f - g) * 22.0f), g > 0.5f ? C_HI : C_AMB);
    }
    cv_blit(0, 36);
}

/* the effects: the tape's repeats (spaced by Time, each fainter by Feedback, wavering with Wear) above, the plate's
 * tail (Size) below */
static void draw_fx(uint32_t now)
{
    float secs = 0.3f + 0.05f * (float)set.par[P_TIME], fb = 0.2f + 0.0072f * (float)set.par[P_FEEDBACK];
    float wear = (float)set.par[P_WEAR] * 0.01f, a = 1.0f, t = (float)now * 0.001f;
    int32_t i, x, tail = 20 + 2 * set.par[P_SIZE];
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 2, &FONT_XS, "TAPE", C_GRAY);
    cv_rect(10, 70, 220, 1, C_LINE);
    for (i = 0; i < 16; i++) {                   /* the sound, then its repeats */
        float xf = 14.0f + (float)i * secs * 30.0f;
        int32_t h = (int32_t)(a * 52.0f);
        if (xf > 228.0f || h < 1)
            break;
        xf += wear * 3.0f * (float)i * fm_sinf(t * 1.3f + (float)i * 1.7f);
        cv_rect((int32_t)xf, 70 - h, i ? 2 : 3, h, i ? (a > 0.4f ? C_HI : C_AMB) : C_WHITE);
        a *= fb;
    }
    cv_text(10, 80, &FONT_XS, "PLATE", C_GRAY);
    for (x = 0; x < tail && x < 220; x++) {      /* the plate's tail, fading */
        float f = 1.0f - (float)x / (float)tail;
        int32_t h = (int32_t)(f * f * 34.0f);
        if (!(x & 1))
            cv_rect(12 + x, 134 - h, 1, h, f > 0.5f ? C_HI : C_DIM);
    }
    cv_rect(10, 134, 220, 1, C_LINE);
    cv_blit(0, 36);
}

/* the age: each of the four as what it does. Haze: the band left (low to high, a line between its ends); Hiss: a
 * field of grains, as many as it is loud; Dropout: a strip of tape, worn in places, broken while a dropout passes;
 * Air: a tone with air round it, as much as the voices have (the kit's breath, times Air) */
static void draw_age(uint32_t now)
{
    static uint32_t seed = 0x9E3779B9u;
    float haze = (float)set.par[P_HAZE] * 0.01f, hiss = (float)set.par[P_HISS] * 0.01f;
    float drop = (float)set.par[P_DROPOUT] * 0.01f, e = en_age_drop;
    float t = (float)now * 0.001f;
    int32_t i, x, x0 = 76, w = 154, lo, hi;
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 4, &FONT_XS, "HAZE", C_GRAY);    /* the band: 20 Hz .. 20 kHz across, what is left lit */
    lo = x0 + (int32_t)((float)w * 0.33f * haze * haze);
    hi = x0 + w - (int32_t)((float)w * 0.3f * haze);
    cv_rect(x0, 12, w, 1, C_LINE);
    cv_rect(lo, 10, hi - lo, 5, C_HI);
    cv_text(10, 40, &FONT_XS, "HISS", C_GRAY);   /* grains, new each frame */
    for (i = 0; i < (int32_t)(hiss * hiss * 220.0f) + (hiss > 0.0f); i++) {
        seed = seed * 1664525u + 1013904223u;
        cv_pset(x0 + (int32_t)((seed >> 8) % (uint32_t)w), 38 + (int32_t)((seed >> 20) % 16u),
                (seed & 0x30u) ? C_DIM : C_GRAY);
    }
    cv_text(10, 76, &FONT_XS, "DROPOUT", C_GRAY);   /* the tape: worn spots, a break while one passes */
    for (i = 0; i < w; i += 2) {
        uint32_t hsh = (uint32_t)(i * 2654435761u) >> 24;
        int worn = (float)hsh < 256.0f * 0.25f * drop;
        int brk = e > 0.02f && i > w / 2 - (int32_t)(e * 40.0f) && i < w / 2 + (int32_t)(e * 40.0f);
        if (!brk)
            cv_rect(x0 + i, 80, 2, 3, worn ? C_DIM : C_HI);
    }
    if (e > 0.02f)
        cv_rect(x0 + w / 2 - 1, 76, 2, 11, C_WHITE);
    cv_text(10, 112, &FONT_XS, "AIR", C_GRAY);   /* a tone, and air round it (the kit's breath, times this) */
    {
        int32_t air = 0, r;
        for (r = 0; r < EN_NROLE; r++)
            air += en_voice_breath(set.kit, r);
        air = air * 3 / EN_NROLE;
        for (i = 0; i < w; i++)
            cv_pset(x0 + i, 118 + (int32_t)(6.0f * fm_sinf((float)i * 0.09f - t * 1.2f)), C_HI);
        for (i = 0; i < air; i++) {
            seed = seed * 1664525u + 1013904223u;
            x = (int32_t)((seed >> 8) % (uint32_t)w);
            cv_pset(x0 + x, 118 + (int32_t)(6.0f * fm_sinf((float)x * 0.09f - t * 1.2f)) +
                                (int32_t)((seed >> 20) % 15u) - 7, C_DIM);
        }
        if (!air)
            cv_text(x0, 128, &FONT_XS, "NONE IN THIS KIT", C_DIM);
    }
    cv_blit(0, 36);
}

/* the chords: their walk over the chord's tones (the world's, as Wander turns it), a place a step, the one now lit;
 * the grain now under it */
static void draw_chords(void)
{
    static const char *const WALK[EN_NWORLD] = {"UP THE CHORD AND DOWN", "FALLING", "SCATTERED", "RISING, FALLING BACK",
                                                "A PENDULUM"};
    static const char *const GRAIN[4] = {"QUARTERS", "EIGHTHS", "TRIPLETS", "SIXTEENTHS"};
    int32_t i, len = en_arp_len, m = en_arp_top ? en_arp_top : 1;
    int on = set.par[P_CH_LEVEL] > 0;
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 2, &FONT_XS, WALK[set.world % EN_NWORLD], C_GRAY);
    for (i = 0; i < len && i < 48; i++) {
        int32_t x = 14 + i * 212 / len, y = 96 - (int32_t)en_arp_seq[i] * 72 / m;
        int cur = on && i == en_arp_pos;
        cv_rect(x, y, cur ? 5 : 3, cur ? 5 : 3, cur ? C_WHITE : on ? C_HI : C_DIM);
    }
    cv_text(10, 112, &FONT_XS, GRAIN[en_arp_div & 3], on ? C_HI : C_DIM);
    cv_blit(0, 36);
}

/* the pulse: its three patterns as rings of steps (the thump's of beats; the hits marked, the step now lit, a ring where one sounds), each
 * faint as Weight leaves it little */
static void draw_pulse(uint32_t now)
{
    static const char *const NAME[3] = {"THUMP", "CLICK", "BRUSH"};
    static uint32_t hits, hit_at;
    int on = set.par[P_PU_LEVEL] > 0, w = set.par[P_PU_WEIGHT];
    int32_t i, s;
    if (en_pulse_hits != hits) {
        hits = en_pulse_hits;
        hit_at = now;
    }
    cv_begin(240, 144, C_BLACK);
    for (i = 0; i < 3; i++) {                    /* three rings, side by side */
        int32_t cx = 42 + i * 78, cy = 64, r = 32, n = en_pulse_n[i], k = en_pulse_k[i];
        int lvl = on && (i == 0 ? w > 0 : w < 100);
        cv_text(cx - 22, 2, &FONT_XS, NAME[i], lvl ? C_GRAY : C_DIM);
        if (!n || !k) {
            cv_text(cx - 12, cy - 6, &FONT_XS, "NONE", C_DIM);
            continue;
        }
        draw_ring(cx, cy, 3, C_LINE);
        for (s = 0; s < n; s++) {
            float a = (float)s * (FM_TWO_PI / (float)n) - FM_PI / 2.0f;
            int32_t x = cx + (int32_t)(fm_cosf(a) * (float)r), y = cy + (int32_t)(fm_sinf(a) * (float)r);
            int hit = (uint32_t)((s + en_pulse_rot[i]) % n) * (uint32_t)k % (uint32_t)n < (uint32_t)k;
            int cur = lvl && (int32_t)((i ? en_pulse_step : en_pulse_step / 4u) % (uint32_t)n) == s;   /* (the thump's: beats) */
            if (hit)
                cv_rect(x - 1, y - 1, 3, 3, !lvl ? C_DIM : cur ? C_WHITE : C_HI);
            else
                cv_rect(x, y, 1, 1, cur ? C_WHITE : C_GRAY);
            if (cur && hit && en_pulse_last == i && now - hit_at < 150u)
                draw_ring(x, y, 5, C_WHITE);
        }
    }
    cv_blit(0, 36);
}

static void k_append_s(char *t)                  /* " S" after a number */
{
    uint32_t k;
    for (k = 0; t[k]; k++)
        ;
    t[k++] = ' ';
    t[k++] = 'S';
    t[k] = 0;
}

/* a figure: notes as bars, across by their lengths, up by their degrees; one lit. points: each a point where it
 * starts (the letter's words, far apart), not a bar */
static void draw_fig(int32_t x0, int32_t y0, int32_t w, int32_t h, const volatile int8_t *deg,
                     const volatile int8_t *dur, int n, int lit, uint16_t c, int points)
{
    int32_t i, total = 0, lo = 99, hi = -99, x = x0;
    for (i = 0; i < n; i++) {
        total += dur[i] > 0 ? dur[i] : 1;
        lo = deg[i] < lo ? deg[i] : lo;
        hi = deg[i] > hi ? deg[i] : hi;
    }
    if (!total)
        return;
    if (hi - lo < 4)
        hi = lo + 4;
    for (i = 0; i < n; i++) {
        int32_t bw = w * (dur[i] > 0 ? dur[i] : 1) / total;
        int32_t y = y0 + h - 3 - (h - 4) * (deg[i] - lo) / (hi - lo);
        if (points)
            cv_rect(x, y - 1, 3, 3, i == lit ? C_WHITE : c);
        else
            cv_rect(x, y, bw > 3 ? bw - 2 : 1, 3, i == lit ? C_WHITE : c);
        x += bw;
    }
}

/* the memory: what the music holds now, large; the motifs remembered, small, under it */
static void draw_seq(void)
{
    int32_t i;
    const char *what = en_alg == EN_ALG_DRONE ? "THE CHORD" : en_alg == EN_ALG_LOOPS ? "THE LOOPS" :
                       en_alg == EN_ALG_LETTER ? "THE LINE" : en_alg == EN_ALG_FOUNTAIN ? "THE FIGURE" : "THE MOTIF";
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 2, &FONT_XS, what, C_GRAY);
    if (en_alg == EN_ALG_LOOPS) {                           /* the loops: each its note, its length, where it is */
        for (i = 0; i < 4 && i < en_fig_len; i++) {
            int32_t y = 26 + i * 16, w = 14 * en_fig_dur[i];
            w = w > 210 ? 210 : w;
            cv_rect(14, y, w, 1, C_LINE);
            cv_rect(14 + (int32_t)(en_loop_ph[i] * (float)w), y - 2, 3, 5, C_HI);
        }
    } else if (en_alg == EN_ALG_DRONE) {                    /* the chord: its tones at once, stacked by degree */
        for (i = 0; i < en_fig_len && i < EN_FIG; i++) {
            int32_t d = en_fig_deg[i] - en_fig_scale, y = 76 - 4 * (d < 0 ? 0 : d > 14 ? 14 : d);
            cv_rect(40, y, 160, 3, i ? C_HI : C_WHITE);
        }
    } else {
        draw_fig(14, 18, 212, 64, en_fig_deg, en_fig_dur, en_fig_len, en_fig_pos, C_HI, en_alg == EN_ALG_LETTER);
    }
    if (en_alg != EN_ALG_DRONE && en_alg != EN_ALG_LOOPS) {   /* the motifs (lines, figures) remembered */
        cv_text(10, 92, &FONT_XS, "REMEMBERED", C_GRAY);
        for (i = 0; i < 4; i++)
            if (en_mem_len[i])
                draw_fig(12 + i * 56, 108, 50, 30, en_mem_deg[i], en_mem_dur[i], en_mem_len[i], -1, C_DIM,
                         en_alg == EN_ALG_LETTER);
    }
    cv_blit(0, 36);
}

/* the environment: where the music is (the world, its root, its beat, its scale) and what moves it */
static void draw_env(void)
{
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const SCALE_NAME[5] = {"MINOR PENTATONIC", "YO", "MAJOR PENTATONIC", "WITH THE SEVENTH",
                                              "LYDIAN"};
    char t[24];
    uint32_t k;
    int32_t bpm = (int32_t)(60.0f / (en_beat_s > 0.05f ? en_beat_s : 0.8f) + 0.5f);
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 2, &FONT_XS, "THE WORLD", C_GRAY);
    cv_text(10, 18, &FONT_B, en_world_name(set.world), C_WHITE);
    cv_text(10, 46, &FONT_XS, "ROOT", C_GRAY);
    cv_text(70, 42, &FONT_B, NOTE[((int)en_root + set.par[P_TRANSPOSE] + 120) % 12], C_HI);
    cv_text(10, 66, &FONT_XS, "BEAT", C_GRAY);
    int_text(bpm, t);
    k = str_put(t + 6, 12, t);                   /* (t: the number, then " a minute") */
    str_put(t + 6 + k, 12, " A MINUTE");
    cv_text(70, 62, &FONT_B, t + 6, C_HI);
    cv_text(10, 86, &FONT_XS, "SCALE", C_GRAY);
    cv_text(70, 84, &FONT_XS, SCALE_NAME[en_scale % 5], C_HI);
    cv_text(10, 106, &FONT_XS, "OCTAVE", C_GRAY);
    int_text(set.oct, t);
    cv_text(70, 102, &FONT_B, t, C_HI);
    cv_blit(0, 36);
}

/* the cycles: the breath, the tide, the long turn, each an orbit (its size its depth) with where it is now, and its
 * length in seconds */
static void draw_lfo(void)
{
    static const char *const NAME[3] = {"BREATH", "TIDE", "TURN"};
    int32_t i;
    char t[12];
    cv_begin(240, 144, C_BLACK);
    for (i = 0; i < 3; i++) {
        int32_t cx = 42 + i * 78, cy = 60, r = 6 + set.par[P_BREATH + i] * 26 / 100;
        float a = en_cyc[i] * FM_TWO_PI - FM_PI / 2.0f;
        draw_ring(cx, cy, 32, C_LINE);
        draw_ring(cx, cy, r, C_GRAY);
        cv_rect(cx + (int32_t)(fm_cosf(a) * (float)r) - 2, cy + (int32_t)(fm_sinf(a) * (float)r) - 2, 5, 5, C_HI);
        cv_text(cx - text_w(&FONT_XS, NAME[i]) / 2, 100, &FONT_XS, NAME[i], C_GRAY);
        int_text((int32_t)(en_period[i] + 0.5f), t);
        k_append_s(t);
        cv_text(cx - text_w(&FONT_XS, t) / 2, 116, &FONT_XS, t, C_HI);
    }
    cv_blit(0, 36);
}

/* global: the tuning as a frequency, the level, the lights, the sleep timer's minutes left */
static void draw_glo(uint32_t now)
{
    char t[24];
    uint32_t k;
    int32_t hz10 = (int32_t)(4400.0f * fm_exp2f((float)set.par[P_TUNE] * (1.0f / 1200.0f)) + 0.5f);
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 2, &FONT_XS, APP_NAME, C_GRAY);
    cv_text(10, 26, &FONT_XS, "A4", C_GRAY);
    int_text(hz10 / 10, t);
    k = str_put(t + 12, 12, t);
    t[12 + k] = '.';
    t[13 + k] = (char)('0' + hz10 % 10);
    str_put(t + 14 + k, 8, " HZ");
    cv_text(80, 22, &FONT_B, t + 12, C_HI);
    cv_text(10, 52, &FONT_XS, "SLEEP", C_GRAY);
    if (!set.par[P_SLEEP]) {
        cv_text(80, 48, &FONT_B, "OFF", C_DIM);
    } else {
        int32_t left = (int32_t)set.par[P_SLEEP] * 60 - (int32_t)((now - ui.play_at) / 1000u);
        if (!en_playing) {
            cv_text(80, 48, &FONT_B, "RESTING", C_DIM);
        } else {
            int_text(left > 0 ? (left + 59) / 60 : 0, t);
            k = str_put(t + 12, 12, t);
            str_put(t + 12 + k, 12, " MIN LEFT");
            cv_text(80, 48, &FONT_B, t + 12, C_HI);
        }
    }
    cv_blit(0, 36);
}

/* the knobs: K1..K4, each its name, its value and a bar */
static void draw_knobs(uint32_t now)
{
    char t[16];
    int32_t i;
    cv_begin(240, 60, C_BLACK);
    draw_rule();
    for (i = 0; i < 4; i++) {
        int32_t x = 6 + i * 59, w = 52, v, hi;
        int lit = ui.turned_at[i] && now - ui.turned_at[i] < 600u;
        const char *name;
        if (ui.shift || !PG_VOICE(ui.page)) {    /* (SEL: the lagoon's four) */
            int pn = (ui.shift ? P_DENSITY : page_par(ui.page)) + (int)i;
            const en_param_t *p = en_param_info(pn);
            name = p->name;
            v = set.par[pn] - p->lo;
            hi = p->hi - p->lo;
            par_text(pn, set.par[pn], t);
        } else {
            v = set.tone[set.kit][PG_ROLE(ui.page)][i];
            hi = en_tone_hi((int)i);
            name = en_tone_name((int)i);
            if (i == T_COLOR)
                ratio_text(en_ratio((int)v), t);
            else
                int_text(v, t);
        }
        cv_text(x, 14, &FONT_XS, name, lit ? C_WHITE : C_GRAY);
        cv_text(x, 29, &FONT_B, t, lit ? C_WHITE : C_HI);
        cv_rect(x, 51, w, 2, C_LINE);
        cv_rect(x, 51, w * v / hi, 2, lit ? C_WHITE : C_HI);
    }
    cv_blit(0, 180);
}

static void draw(void)
{
    uint32_t now = plat_ms();
    draw_header(now);
    if (ui.page == PG_LAGOON)
        draw_lagoon(now);
    else if (ui.page == PG_FX)
        draw_fx(now);
    else if (ui.page == PG_AGE)
        draw_age(now);
    else if (ui.page == PG_CHORDS)
        draw_chords();
    else if (ui.page == PG_PULSE)
        draw_pulse(now);
    else if (ui.page == PG_SEQ)
        draw_seq();
    else if (ui.page == PG_ENV)
        draw_env();
    else if (ui.page == PG_LFO)
        draw_lfo();
    else if (ui.page == PG_GLO)
        draw_glo(now);
    else
        draw_voice(PG_ROLE(ui.page));
    draw_knobs(now);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    ann_tick(now);
    if (!ui.play_at)
        ui.play_at = now ? now : 1u;
    {                                            /* the sleep timer's last minute: dusk (sparser, quieter, slower) */
        static int dusk_sent = -1;
        int dk = 0;
        uint32_t end = (uint32_t)set.par[P_SLEEP] * 60000u, t = now - ui.play_at;
        if (set.par[P_SLEEP] && en_playing && t + 60000u > end)
            dk = t >= end ? 100 : (int)(100u - (end - t) / 600u);
        if (dk != dusk_sent) {
            en_dusk(dk);
            dusk_sent = dk;
        }
        if (set.par[P_SLEEP] && en_playing && t >= end) {   /* asleep */
            en_play(0);
            app_say("SLEEP", "RESTING");
        }
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw || now - ui.last_draw >= 33u) {   /* the ripples move: about 30 frames a second */
        ui.redraw = 0;
        ui.last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    int i;
    if (!strcmp(what, "playing"))
        *got = en_playing;
    else if (!strcmp(what, "notes"))             /* notes started since boot */
        *got = (int)en_notes;
    else if (!strcmp(what, "voices")) {          /* voices sounding now */
        for (i = 0, *got = 0; i < EN_NVOICE; i++)
            *got += en_glow[i] > 0.0f;
    } else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "page"))              /* 0 the lagoon, 1..3 the voices */
        *got = ui.page;
    else if (!strcmp(what, "alg"))               /* the world's algorithm: 0 motifs, 1 drone, 2 loops */
        *got = en_alg;
    else if (!strcmp(what, "oct"))
        *got = set.oct;
    else if (!strcmp(what, "arp"))
        *got = (set.par[P_CH_LEVEL] > 0) | (set.par[P_PU_LEVEL] > 0) << 1;   /* 1 the chords, 2 the pulse */
    else if (!strcmp(what, "pulsepat"))          /* the pulse's patterns now, as one number (n, k, turn of each) */
        *got = (int)(((uint32_t)en_pulse_n[0] * 31u + en_pulse_k[0] * 7u + en_pulse_rot[0]) * 1000000u +
                     ((uint32_t)en_pulse_n[1] * 31u + en_pulse_k[1] * 7u + en_pulse_rot[1]) * 1000u +
                     ((uint32_t)en_pulse_n[2] * 31u + en_pulse_k[2] * 7u + en_pulse_rot[2]) % 1000u) & 0x7FFFFFFF;
    else if (!strcmp(what, "chordwalk")) {       /* the chords' walk now, as one number */
        uint32_t h = en_arp_len, k;
        for (k = 0; k < en_arp_len; k++)
            h = h * 31u + en_arp_seq[k];
        *got = (int)(h & 0x7FFFFFFF);
    } else if (!strcmp(what, "chordgrain"))      /* the chords' grain now: 0 quarters .. 3 sixteenths */
        *got = en_arp_div;
    else if (!strcmp(what, "crossing"))          /* a crossing under way */
        *got = en_crossing;
    else if (!strcmp(what, "splash"))            /* the splash now, 0..100 */
        *got = (int)(en_splash * 100.0f);
    else if (!strcmp(what, "pulsehits"))         /* the pulse's hits since boot */
        *got = (int)en_pulse_hits;
    else if (!strcmp(what, "arpbeats"))          /* the pulse's notes (a count, mod 256) */
        *got = en_arp_beat;
    else if (!strcmp(what, "rec"))
        *got = en_recording;
    else if (!strcmp(what, "taken"))             /* phrases taken in */
        *got = (int)en_taken;
    else if (!strncmp(what, "fig", 3))           /* figN: the figure's note N, a scale degree */
        *got = en_fig_deg[atoi(what + 3) % EN_FIG];
    else if (!strcmp(what, "held"))
        *got = en_held;
    else if (!strcmp(what, "world"))
        *got = set.world;
    else if (!strcmp(what, "kit"))
        *got = set.kit;
    else if (!strcmp(what, "offers"))            /* offerings since boot (presses) */
        *got = (int)en_offers;
    else if (!strcmp(what, "notemax"))           /* the highest note played since boot */
        *got = en_note_max;
    else if (!strncmp(what, "period", 6))        /* periodN: cycle N's length now, in seconds */
        *got = (int)en_period[atoi(what + 6) % 3];
    else if (!strcmp(what, "recalls"))           /* motifs folded back from the memory */
        *got = (int)en_recalls;
    else if (!strcmp(what, "scale"))
        *got = en_scale;
    else if (!strncmp(what, "state", 5))         /* stateN: the hidden state N (lagoon.h S_*), times 100 */
        *got = (int)(en_state[atoi(what + 5) % EN_NSTATE] * 100.0f);
    else if (!strcmp(what, "offsetsum")) {       /* how far the offerings moved the state, all four, times 100 */
        float a = 0.0f;
        for (i = 0; i < EN_NSTATE; i++)
            a += en_offset[i] < 0.0f ? -en_offset[i] : en_offset[i];
        *got = (int)(a * 100.0f);
    } else if (!strncmp(what, "offset", 6))        /* offsetN: what the offerings moved it by, times 100 */
        *got = (int)(en_offset[atoi(what + 6) % EN_NSTATE] * 100.0f);
    else if (!strncmp(what, "param", 5))         /* paramN: parameter N's value (lagoon.h P_*) */
        *got = set.par[atoi(what + 5) % P_NPARAMS];
    else if (!strncmp(what, "tone", 4) && what[4] && what[5])   /* toneRF: this kit's voice R, setting F */
        *got = set.tone[set.kit][(what[4] - '0') % EN_NROLE][(what[5] - '0') % EN_NTONE];
    else
        return 0;
    return 1;
}
#endif
