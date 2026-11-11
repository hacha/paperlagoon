/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* PaperLagoon on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c and
 * announce.c are in scope), the glue to the engine (dsp/lagoon.c, its own -O2 unit) and a first, plain screen.
 *
 * STATUS: a sketch. PLAY starts and rests the music, K1..K4 adjust the engine's four placeholder parameters,
 * SAVE keeps them. The keys only light. The screen shows each sounding voice as a ripple. */
#include "fastmath.h"

#define SET_MAGIC 0x314C504Bu            /* "PLL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int16_t par[P_NPARAMS];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static void settings_defaults(void)
{
    int i;
    set.magic = SET_MAGIC;
    for (i = 0; i < P_NPARAMS; i++)
        set.par[i] = en_param_info(i)->def;
}

/* ------------------------------------------------------------ contract --- */
void app_init(void)
{
    int i;
    en_init();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC)
        settings_defaults();
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (set.par[i] < p->lo || set.par[i] > p->hi)
            set.par[i] = p->def;
        en_set(i, set.par[i]);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) { en_render(lr, n, master_q12); }

void app_panic(void)
{
    en_play(0);
    en_panic();
}

/* ------------------------------------------------------------------ UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t turned_at[P_NPARAMS];
    uint32_t last_draw;
    uint8_t redraw;
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

/* a whole number in decimal, a minus sign when below zero (b: 12 bytes) */
static void int_text(int32_t v, char *b)
{
    static const int32_t P10[10] = {1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
    int64_t x = v;
    int i, any = 0;
    if (x < 0) {
        *b++ = '-';
        x = -x;
    }
    for (i = 0; i < 10; i++) {
        int dgt = (int)(x / P10[i]);
        x -= (int64_t)dgt * P10[i];
        if (dgt || any || i == 9) {
            *b++ = (char)('0' + dgt);
            any = 1;
        }
    }
    *b = 0;
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

static void param_step(int p, int32_t d)
{
    const en_param_t *pi = en_param_info(p);
    int32_t v = clamp(set.par[p] + 2 * d, pi->lo, pi->hi);
    char t[12];
    ui.turned_at[p] = plat_ms();
    if (v == set.par[p])
        return;
    set.par[p] = (int16_t)v;
    set_dirty = 1;
    en_set(p, (int)v);
    int_text(v, t);
    app_say(pi->name, t);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    int32_t e;
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_PLAY) {
            en_play(!en_playing);
            app_say(en_playing ? "REST" : "PLAY", "");
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        }
    }
    for (k = 0; k < P_NPARAMS; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            param_step((int)k, e);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (en_playing ? 1u << B_PLAY : 0u), keys);
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

static void draw(void)
{
    char t[24];
    uint32_t i, now = plat_ms();
    /* the header: the name, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
        cv_text(230 - text_w(&FONT_XS, en_playing ? "PLAYING" : "RESTING"), 10, &FONT_XS,
                en_playing ? "PLAYING" : "RESTING", en_playing ? C_HI : C_DIM);
    }
    cv_blit(0, 0);
    /* the water: each sounding voice a ripple, wider as it fades; a slow breath at the centre (the cycle) */
    cv_begin(240, 144, C_BLACK);
    {
        float b = 0.5f + 0.5f * fm_sinf(en_cycle * FM_TWO_PI);
        draw_ring(120, 72, 3 + (int32_t)(4.0f * b), C_LINE);
    }
    for (i = 0; i < EN_NVOICE; i++) {
        float g = en_glow[i];
        int32_t x, r;
        if (g < 0.02f)
            continue;
        x = 20 + ((int32_t)en_voice_note[i] - 41) * 4;
        x = x < 10 ? 10 : x > 230 ? 230 : x;
        r = 4 + (int32_t)((1.0f - g) * 56.0f);
        draw_ring(x, 72, r, g > 0.5f ? C_HI : g > 0.2f ? C_AMB : C_DIM);
        if (g > 0.6f)
            draw_ring(x, 72, r / 2, C_AMB);
    }
    cv_blit(0, 36);
    /* the knobs: K1..K4, each its name, its value and a bar */
    cv_begin(240, 52, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info((int)i);
        int32_t x = 6 + (int32_t)i * 59, w = 52;
        int lit = ui.turned_at[i] && now - ui.turned_at[i] < 600u;
        cv_text(x, 6, &FONT_XS, p->name, lit ? C_WHITE : C_GRAY);
        int_text(set.par[i], t);
        cv_text(x, 22, &FONT_B, t, lit ? C_WHITE : C_HI);
        cv_rect(x, 44, w, 2, C_LINE);
        cv_rect(x, 44, w * (set.par[i] - p->lo) / (p->hi - p->lo), 2, lit ? C_WHITE : C_HI);
    }
    cv_blit(0, 188);
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
    else if (!strncmp(what, "param", 5))         /* paramN: parameter N's value (lagoon.h P_*) */
        *got = set.par[atoi(what + 5) % P_NPARAMS];
    else
        return 0;
    return 1;
}
#endif
