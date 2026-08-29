/*
 * The programmer.
 *
 * Not one programmer: a family of them.  BLHeli_S and AM32 speak a one-wire
 * bootloader at 19,200; ESCape32 answers a text CLI; VESC wants framed
 * packets; a Hitec D-series servo has its own thing entirely.  They share a
 * connector and nothing else, so the protocol is chosen first and said out
 * loud, rather than being guessed at from whatever answers.
 *
 * That costs a tap before every session and buys the thing a bench needs
 * most: at no point is there a screen full of parameters whose provenance is
 * ambiguous.  You picked the protocol; these are its parameters; that is the
 * device that answered it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "programmer_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD       6
#define PROTO_Y   6
#define PROTO_H   84
#define DEV_Y     98
#define DEV_H     66
#define PARM_Y    172
#define PARM_H    (H - PARM_Y - PAD)

#define CHIP_Y    (PROTO_Y + 16)
#define CHIP_H    30
#define CHIP_GAP  8

#define ROW_Y0    (PARM_Y + 34)
#define ROW_H     28
#define ROWS_MAX  6

#define MAX_PARAMS 8

typedef struct {
    const char *name;
    const char *const *choices;
    int         count;
    int         initial;
} param_def_t;

typedef struct {
    const char *name;
    const char *transport;   /* said out loud, because they differ */
    const char *device;      /* what answers, once it has */
    const param_def_t *params;
    int         count;
} proto_t;

static const char *const k_dir[]   = { "NORMAL", "REVERSED", "BIDIRECTIONAL" };
static const char *const k_timing[]= { "LOW", "MEDIUM LOW", "MEDIUM",
                                       "MEDIUM HIGH", "HIGH" };
static const char *const k_pwm[]   = { "24 kHz", "48 kHz", "96 kHz" };
static const char *const k_power[] = { "25 %", "50 %", "75 %", "100 %" };
static const char *const k_onoff[] = { "OFF", "ON" };
static const char *const k_cut[]   = { "OFF", "3.0 V", "3.3 V", "3.5 V",
                                       "3.7 V" };
static const char *const k_beep[]  = { "OFF", "LOW", "MEDIUM", "HIGH" };

static const param_def_t k_blheli[] = {
    { "Motor direction",   k_dir,    3, 0 },
    { "Timing",            k_timing, 5, 2 },
    { "PWM frequency",     k_pwm,    3, 0 },
    { "Startup power",     k_power,  4, 3 },
    { "Brake on stop",     k_onoff,  2, 0 },
    { "Low voltage cut",   k_cut,    5, 3 },
    { "Beep volume",       k_beep,   4, 2 },
    { "Demag compensation",k_timing, 5, 2 },
};

static const char *const k_ramp[]  = { "OFF", "GENTLE", "NORMAL", "BRISK" };
static const param_def_t k_am32[] = {
    { "Motor direction",   k_dir,    3, 0 },
    { "Timing advance",    k_timing, 5, 2 },
    { "PWM frequency",     k_pwm,    3, 1 },
    { "Sinusoidal startup",k_onoff,  2, 1 },
    { "Complementary PWM", k_onoff,  2, 1 },
    { "Startup ramp",      k_ramp,   4, 2 },
    { "Low voltage cut",   k_cut,    5, 2 },
};

static const param_def_t k_escape32[] = {
    { "Motor direction",   k_dir,    3, 0 },
    { "Timing",            k_timing, 5, 2 },
    { "PWM frequency",     k_pwm,    3, 2 },
    { "Sine startup",      k_onoff,  2, 1 },
    { "Brake on stop",     k_onoff,  2, 1 },
    { "Telemetry",         k_onoff,  2, 1 },
};

static const char *const k_mtype[] = { "BLDC", "FOC" };
static const char *const k_ilim[]  = { "20 A", "40 A", "60 A", "80 A" };
static const param_def_t k_vesc[] = {
    { "Motor type",        k_mtype,  2, 1 },
    { "Current limit",     k_ilim,   4, 1 },
    { "Regen braking",     k_onoff,  2, 1 },
    { "Low voltage cut",   k_cut,    5, 3 },
    { "Telemetry",         k_onoff,  2, 1 },
};

static const char *const k_speed[] = { "SLOW", "NORMAL", "FAST" };
static const char *const k_dead[]  = { "1 us", "2 us", "4 us", "8 us" };
static const param_def_t k_hitec[] = {
    { "Centre",            k_dead,   4, 1 },
    { "Endpoint travel",   k_power,  4, 3 },
    { "Speed",             k_speed,  3, 1 },
    { "Dead band",         k_dead,   4, 1 },
    { "Overload protect",  k_onoff,  2, 1 },
    { "Fail-safe",         k_onoff,  2, 1 },
};

static const proto_t k_protos[] = {
    { "BLHeli_S", "one-wire bootloader, 19200 baud",
      "BLHeli_S 16.7  on  EFM8BB21", k_blheli,   8 },
    { "AM32",     "one-wire bootloader, 19200 baud",
      "AM32 2.15  on  STM32G071",    k_am32,     7 },
    { "ESCape32", "text CLI over the signal line",
      "ESCape32 v9  on  AT32F421",   k_escape32, 6 },
    { "VESC",     "framed packets, 115200 baud",
      "VESC 6.05  on  STM32F405",    k_vesc,     5 },
    { "Hitec",    "D-series servo protocol",
      "Hitec D956TW",                k_hitec,    6 },
};
#define PROTO_COUNT ((int)(sizeof(k_protos) / sizeof(k_protos[0])))

static struct {
    int  proto;
    bool connected;
    int  choice[MAX_PARAMS];
    int  scroll;

    gfx_rect_t chip[PROTO_COUNT];
    gfx_rect_t connect_btn;
    gfx_rect_t read_btn, write_btn;
    gfx_rect_t down[ROWS_MAX], up[ROWS_MAX];
    gfx_rect_t page_up, page_dn;

    uint32_t rev;
    uint32_t drawn[2];
    unsigned drawn_mask;
} s;

static const proto_t *proto(void) { return &k_protos[s.proto]; }

/* Every protocol has its own parameters, so selecting one starts its own
 * defaults rather than carrying the last one's indices across. */
static void adopt_defaults(void)
{
    const proto_t *p = proto();
    for (int i = 0; i < MAX_PARAMS; ++i) {
        s.choice[i] = (i < p->count) ? p->params[i].initial : 0;
    }
    s.scroll = 0;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;

    const int w = (W - 2 * PAD - 24 - (PROTO_COUNT - 1) * CHIP_GAP)
                  / PROTO_COUNT;
    for (int i = 0; i < PROTO_COUNT; ++i) {
        s.chip[i] = (gfx_rect_t){ (int16_t)(PAD + 12 + i * (w + CHIP_GAP)),
                                  CHIP_Y, (int16_t)w, CHIP_H };
    }
    s.connect_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                  (int16_t)(DEV_Y + 13), 168, 40 };
    s.read_btn  = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 350),
                                (int16_t)(H - PAD - 42), 168, 34 };
    s.write_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                (int16_t)(H - PAD - 42), 168, 34 };
    for (int i = 0; i < ROWS_MAX; ++i) {
        const int y = ROW_Y0 + i * ROW_H;
        s.down[i] = (gfx_rect_t){ 636, (int16_t)(y - 2), 34, 24 };
        s.up[i]   = (gfx_rect_t){ 748, (int16_t)(y - 2), 34, 24 };
    }
    /* Paging, because a protocol with more parameters than rows is the normal
     * case rather than the exception -- BLHeli_S alone has eight. */
    s.page_up = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76),
                              (int16_t)(PARM_Y + 6), 34, 24 };
    s.page_dn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38),
                              (int16_t)(PARM_Y + 6), 34, 24 };
    adopt_defaults();
}

int  programmer_screen_protocol(void)  { return s.proto; }
bool programmer_screen_connected(void) { return s.connected; }

int programmer_screen_choice(int param)
{
    if (param < 0 || param >= proto()->count) {
        return -1;
    }
    return s.choice[param];
}

/* ------------------------------------------------------------------ events */

static int rows_shown(void)
{
    const int n = proto()->count - s.scroll;
    return (n > ROWS_MAX) ? ROWS_MAX : n;
}

static void step(int row, int by)
{
    const int idx = s.scroll + row;
    if (idx < 0 || idx >= proto()->count) {
        return;
    }
    const param_def_t *d = &proto()->params[idx];
    int v = s.choice[idx] + by;
    /* Clamped, not wrapped.  A parameter that rolls from its last value back
     * to its first will eventually be set to the wrong end by somebody
     * pressing one more time than they meant to. */
    if (v < 0)          { v = 0; }
    if (v >= d->count)  { v = d->count - 1; }
    if (v != s.choice[idx]) {
        s.choice[idx] = v;
        ++s.rev;
    }
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL || evt->type != TOUCH_EVENT_DOWN) {
        return;
    }
    const int px = evt->point.x, py = evt->point.y;

    for (int i = 0; i < PROTO_COUNT; ++i) {
        if (gfx_rect_contains(s.chip[i], px, py)) {
            if (i != s.proto) {
                s.proto = i;
                /* Changing protocol drops the connection.  The device that
                 * answered a one-wire bootloader is not the device that will
                 * answer a CLI, and leaving the old identity on screen beside
                 * new parameters is the one lie this screen must not tell. */
                s.connected = false;
                adopt_defaults();
                ++s.rev;
            }
            return;
        }
    }

    if (gfx_rect_contains(s.connect_btn, px, py)) {
        s.connected = !s.connected;
        ++s.rev;
        return;
    }

    if (!s.connected) {
        return;
    }
    const int max_scroll = (proto()->count > ROWS_MAX)
                               ? proto()->count - ROWS_MAX : 0;
    if (gfx_rect_contains(s.page_up, px, py)) {
        if (s.scroll > 0) { --s.scroll; ++s.rev; }
        return;
    }
    if (gfx_rect_contains(s.page_dn, px, py)) {
        if (s.scroll < max_scroll) { ++s.scroll; ++s.rev; }
        return;
    }
    for (int i = 0; i < rows_shown(); ++i) {
        if (gfx_rect_contains(s.down[i], px, py)) { step(i, -1); return; }
        if (gfx_rect_contains(s.up[i],   px, py)) { step(i, +1); return; }
    }
}

/* ----------------------------------------------------------------- drawing */

static void draw_protocols(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, PROTO_Y + 40 + 6, "", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    for (int i = 0; i < PROTO_COUNT; ++i) {
        const bool on = (i == s.proto);
        ui_button(c, s.chip[i], k_protos[i].name,
                  on ? ui_theme_color(UI_C_ACCENT)
                     : ui_theme_color(UI_C_PANEL_HI),
                  false, true);
    }
    /* The transport, under the name.  These have nothing in common, and a
     * screen that hides that is a screen that will one day be pointed at the
     * wrong device with great confidence. */
    gfx_text(c, PAD + 12, PROTO_Y + 56, proto()->transport, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

static void draw_device(gfx_canvas_t *c)
{
    const int x = PAD + 12;
    if (s.connected) {
        gfx_fill_circle_aa(c, x + 7, DEV_Y + 33, 6,
                           ui_theme_color(UI_C_OK));
        gfx_text(c, x + 22, DEV_Y + 16, proto()->device, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, x + 22, DEV_Y + 38, "answered, parameters read",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    } else {
        gfx_fill_circle_aa(c, x + 7, DEV_Y + 33, 6,
                           ui_theme_color(UI_C_TEXT_FAINT));
        gfx_text(c, x + 22, DEV_Y + 16, "nothing has answered",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, x + 22, DEV_Y + 38, "connect the lead and try",
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    ui_button(c, s.connect_btn, s.connected ? "DISCONNECT" : "CONNECT",
              s.connected ? ui_theme_color(UI_C_PANEL_HI)
                          : ui_theme_color(UI_C_ACCENT),
              false, true);
}

static void draw_params(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, PARM_Y + 14, "PARAMETERS", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    if (!s.connected) {
        gfx_text_in(c, (gfx_rect_t){ PAD, (int16_t)(PARM_Y + PARM_H / 2 - 8),
                                     (int16_t)(W - 2 * PAD), 16 },
                    "no device, so nothing to show",
                    UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_CENTER);
        return;
    }

    const int max_scroll = (proto()->count > ROWS_MAX)
                               ? proto()->count - ROWS_MAX : 0;
    char count[40];
    /* Which of them, not how many: "7 of 8" leaves you wondering which one
     * is missing. */
    snprintf(count, sizeof(count), "%d-%d of %d",
             s.scroll + 1, s.scroll + rows_shown(), proto()->count);
    gfx_text_in(c, (gfx_rect_t){ 420, (int16_t)(PARM_Y + 12), 260, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, s.page_up, "^", ui_theme_color(UI_C_PANEL_HI),
              false, s.scroll > 0);
    ui_button(c, s.page_dn, "v", ui_theme_color(UI_C_PANEL_HI),
              false, s.scroll < max_scroll);

    for (int i = 0; i < rows_shown(); ++i) {
        const int idx = s.scroll + i;
        const int y = ROW_Y0 + i * ROW_H;
        const param_def_t *d = &proto()->params[idx];

        if ((i & 1) == 0) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16, ROW_H - 2,
                                4, ui_theme_color(UI_C_PANEL_SUNK));
        }
        gfx_text(c, PAD + 20, y, d->name, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text_in(c, (gfx_rect_t){ 380, (int16_t)y, 244, 16 },
                    d->choices[s.choice[idx]], UI_FONT_LABEL,
                    ui_theme_color(UI_C_ACCENT), 1, GFX_ALIGN_RIGHT);

        const bool at_lo = (s.choice[idx] == 0);
        const bool at_hi = (s.choice[idx] == d->count - 1);
        ui_button(c, s.down[i], "-", ui_theme_color(UI_C_PANEL_HI),
                  false, !at_lo);
        ui_button(c, s.up[i], "+", ui_theme_color(UI_C_PANEL_HI),
                  false, !at_hi);
    }

    ui_button(c, s.read_btn, "READ", ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, s.write_btn, "WRITE", ui_theme_color(UI_C_WARN),
              false, true);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s.drawn_mask |= bit;
    }
    if (s.drawn[buf] == s.rev) {
        return;
    }
    s.drawn[buf] = s.rev;

    ui_card(c, (gfx_rect_t){ PAD, PROTO_Y, (int16_t)(W - 2 * PAD), PROTO_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD), DEV_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, PARM_Y, (int16_t)(W - 2 * PAD),
                             (int16_t)PARM_H },
            ui_theme_color(UI_C_PANEL));

    draw_protocols(c);
    draw_device(c);
    draw_params(c);
}

static const ui_screen_t k_screen = {
    .title  = "PROGRAMMER",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *programmer_screen(void) { return &k_screen; }
