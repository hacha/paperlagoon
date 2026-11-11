/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* PaperLagoon on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c and
 * announce.c are in scope), the glue to the engine (dsp/lagoon.c, its own -O2 unit) and the screen.
 *
 * Pages, as HYP-1's: SELECT turns through all of them, a button jumps to its own and is lit while one of its pages
 * shows; the header says where (n/4).
 *   1 the lagoon (HOME)      the music: K1..K4 Density, Bright, Space, Echo
 *   2..4 a voice (EDIT)      the kit's lead, answer and low voice: K1..K4 Color, Bright, Decay, Soft. EDIT again:
 *                            the next voice; EDIT held: the voice as the kit has it
 * PRESETS: the kit (a set of three voices; the edits are kept per kit). PLAY: play or rest. SAVE: keep it all.
 * The keys play no notes: a press offers something to the hidden state (lagoon.h), which the music shows later and
 * loosely; the screen shows at once that something was given (a drop falling to the water, its rings), and the
 * state itself as the water's line, slowly. */
#include "fastmath.h"

#define SET_MAGIC 0x324C504Bu            /* "PLL2": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int16_t par[P_NPARAMS];
    uint8_t kit, pad;
    int8_t tone[EN_NKIT][EN_NROLE][EN_NTONE];
} settings_t;
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
    for (i = 0; i < EN_NKIT * EN_NROLE; i++)
        tone_defaults(i / EN_NROLE, i % EN_NROLE);
}

/* ------------------------------------------------------------ contract --- */
void app_init(void)
{
    int i, f;
    en_init();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC)
        settings_defaults();
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (set.par[i] < p->lo || set.par[i] > p->hi)
            set.par[i] = p->def;
        en_set(i, set.par[i]);
    }
    if (set.kit >= EN_NKIT)
        set.kit = 0;
    en_set_kit(set.kit);
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
enum { PG_LAGOON, PG_LEAD, PG_ANSWER, PG_LOW, NPAGE };
#define PG_ROLE(p) ((p) - PG_LEAD)               /* a voice page's R_* */
#define HOLD_MS 800u                             /* EDIT held this long: the voice as the kit has it */

static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t turned_at[4];                       /* each knob's last turn (the strip lights it) */
    uint32_t last_draw;
    uint32_t edit_at;                            /* EDIT went down (0: up) */
    uint32_t audition_at;                        /* a voice was edited: let it sound then (0: none) */
    uint32_t key_at[NKEYS];                      /* each held key's next offering (0: up) */
    struct {
        uint32_t born;                           /* 0: none */
        int16_t x;
        uint8_t big;
    } drop[8];
    uint8_t drop_i;
    uint8_t edit_held;                           /* the hold has acted: the release does nothing */
    uint8_t page;
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

/* a page's position and name, said */
static void page_go(int p)
{
    char t[24];
    uint32_t k;
    p = p < 0 ? 0 : p >= NPAGE ? NPAGE - 1 : p;
    if (p == ui.page)
        return;
    ui.page = (uint8_t)p;
    k = str_put(t, sizeof t, p == PG_LAGOON ? "LAGOON" : en_role_name(PG_ROLE(p)));
    t[k++] = ' ';
    int_text(p + 1, t + k);
    k += p + 1 >= 10 ? 2 : 1;
    t[k++] = '/';
    int_text(NPAGE, t + k);
    app_say(t, "");
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
    if (down & 1u << B_EDIT) {                   /* EDIT acts on release, or when held */
        ui.edit_at = now ? now : 1u;
        ui.edit_held = 0;
    }
    if (ui.edit_at && !ui.edit_held && (btn & 1u << B_EDIT) && now - ui.edit_at >= HOLD_MS &&
        ui.page >= PG_LEAD) {
        tone_reset(PG_ROLE(ui.page));
        ui.edit_held = 1;
    }
    if (up & 1u << B_EDIT) {
        if (!ui.edit_held)
            page_go(ui.page == PG_LAGOON || ui.page == PG_LOW ? PG_LEAD : ui.page + 1);
        ui.edit_at = 0;
    }
    for (k = 0; k < NKEYS; k++) {                /* the keys: offerings */
        uint32_t bit = 1u << k;
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
    for (k = 0; k < 4; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0) {
            if (ui.page == PG_LAGOON)
                param_step((int)k, e);
            else
                tone_step(PG_ROLE(ui.page), (int)k, e);
        }
    if (ui.audition_at && now >= ui.audition_at) {
        ui.audition_at = 0;
        if (ui.page >= PG_LEAD)
            en_audition(PG_ROLE(ui.page));
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (en_playing ? 1u << B_PLAY : 0u) |
                  (ui.page == PG_LAGOON ? 1u << B_HOME : 1u << B_EDIT),
              keys);
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

static void draw_header(uint32_t now)
{
    char t[8];
    int32_t x;
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        const char *s = en_playing ? "PLAYING" : "RESTING";
        uint32_t k;
        if (ui.page == PG_LAGOON) {
            cv_text(10, 6, &FONT_B, en_kit_name(set.kit), C_WHITE);
        } else {
            x = cv_text(10, 6, &FONT_B, en_role_name(PG_ROLE(ui.page)), C_WHITE);
            cv_text(x + 8, 10, &FONT_XS, en_kit_name(set.kit), C_GRAY);
        }
        int_text(ui.page + 1, t);                /* where: n/m */
        k = 1;
        t[k++] = '/';
        int_text(NPAGE, t + k);
        x = 230 - text_w(&FONT_XS, t);
        cv_text(x, 10, &FONT_XS, t, C_HI);
        cv_text(x - 10 - text_w(&FONT_XS, s), 10, &FONT_XS, s, en_playing ? C_HI : C_DIM);
    }
    cv_blit(0, 0);
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
    for (x = 0; x < 240; x += gap < 2 ? 2 : gap) {
        float y = (float)hy + amp * fm_sinf((float)x * 0.045f + t * speed) +
                  0.5f * amp * fm_sinf((float)x * 0.11f - t * speed * 1.7f);
        cv_pset(x, (int32_t)y, C_LINE);
    }
    draw_ring(120, hy, 3 + (int32_t)(4.0f * b), C_LINE);
    for (i = 0; i < EN_NVOICE; i++) {
        float g = en_glow[i];
        int32_t r;
        if (g < 0.02f)
            continue;
        x = 20 + ((int32_t)en_voice_note[i] - 41) * 4;
        x = x < 10 ? 10 : x > 230 ? 230 : x;
        r = 4 + (int32_t)((1.0f - g) * 56.0f);
        draw_ring(x, hy, r, g > 0.5f ? C_HI : g > 0.2f ? C_AMB : C_DIM);
        if (g > 0.6f)
            draw_ring(x, hy, r / 2, C_AMB);
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
    cv_text(10, 4, &FONT_XS, "carrier : modulator", C_GRAY);
    cv_text(10, 18, &FONT_B, t, C_WHITE);
    for (i = 0; i < EN_NVOICE; i++) {            /* the voice, sounding */
        float g = en_glow[i];
        if (g < 0.02f || en_voice_role[i] != role)
            continue;
        draw_ring(200, 28, 3 + (int32_t)((1.0f - g) * 22.0f), g > 0.5f ? C_HI : C_AMB);
    }
    cv_blit(0, 36);
}

/* the knobs: K1..K4, each its name, its value and a bar */
static void draw_knobs(uint32_t now)
{
    char t[16];
    int32_t i;
    cv_begin(240, 52, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < 4; i++) {
        int32_t x = 6 + i * 59, w = 52, v, hi;
        int lit = ui.turned_at[i] && now - ui.turned_at[i] < 600u;
        const char *name;
        if (ui.page == PG_LAGOON) {
            const en_param_t *p = en_param_info((int)i);
            name = p->name;
            v = set.par[i] - p->lo;
            hi = p->hi - p->lo;
            int_text(set.par[i], t);
        } else {
            v = set.tone[set.kit][PG_ROLE(ui.page)][i];
            hi = en_tone_hi((int)i);
            name = en_tone_name((int)i);
            if (i == T_COLOR)
                ratio_text(en_ratio((int)v), t);
            else
                int_text(v, t);
        }
        cv_text(x, 6, &FONT_XS, name, lit ? C_WHITE : C_GRAY);
        cv_text(x, 22, &FONT_B, t, lit ? C_WHITE : C_HI);
        cv_rect(x, 44, w, 2, C_LINE);
        cv_rect(x, 44, w * v / hi, 2, lit ? C_WHITE : C_HI);
    }
    cv_blit(0, 188);
}

static void draw(void)
{
    uint32_t now = plat_ms();
    draw_header(now);
    if (ui.page == PG_LAGOON)
        draw_lagoon(now);
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
    else if (!strcmp(what, "kit"))
        *got = set.kit;
    else if (!strcmp(what, "offers"))            /* offerings since boot (presses) */
        *got = (int)en_offers;
    else if (!strcmp(what, "notemax"))           /* the highest note played since boot */
        *got = en_note_max;
    else if (!strcmp(what, "recalls"))           /* motifs folded back from the memory */
        *got = (int)en_recalls;
    else if (!strcmp(what, "scale"))
        *got = en_scale;
    else if (!strncmp(what, "state", 5))         /* stateN: the hidden state N (lagoon.h S_*), times 100 */
        *got = (int)(en_state[atoi(what + 5) % EN_NSTATE] * 100.0f);
    else if (!strncmp(what, "offset", 6))        /* offsetN: what the offerings moved it by, times 100 */
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
