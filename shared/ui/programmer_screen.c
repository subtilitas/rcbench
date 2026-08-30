/*
 * The programmer.
 *
 * Not one programmer: a family of them.  BLHeli_S and AM32 speak a one-wire
 * bootloader at 19,200; ESCape32 answers a text CLI; VESC wants framed
 * packets; a Hitec D-series servo has its own thing entirely.  They share a
 * connector and nothing else.
 *
 * So it asks, in order: what are you programming, which of its protocols, and
 * only then what answered.  Picking the class is picking which lead is in
 * your hand; picking the protocol is picking what to say down it.
 *
 * ---------------------------------------------------------------------------
 *
 * The parameters draw themselves.
 *
 * A definition says what kind of thing it is -- a switch, a choice, a number
 * on a range -- and the renderer owns one widget per kind.  Nothing here
 * knows what BLHeli_S is; it knows what a bounded number looks like.  Adding
 * a firmware is a table, and the only way to need new drawing code is to
 * invent a kind of setting that none of these have, which in twenty years of
 * ESCs nobody has.
 *
 * That is also how the real configurators do it, and it is worth saying why
 * they are right rather than only that they agree: a screen with one widget
 * per setting drifts, because the fortieth setting is written by somebody in
 * a hurry.  A screen with three widgets cannot.
 *
 * BLHeli_32 is missing from the ESC list on purpose, and not for want of
 * protocol.  Its settings are a 256-byte XTEA block whose key exists only
 * inside a closed binary, so the bench can identify and drive one of these
 * ESCs but cannot honestly name a single setting in it.  docs/BLHeli32.md
 * carries the whole reasoning.
 */

#include "programmer_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD       6
#define CRUMB_Y   12
#define DEV_Y     50
#define DEV_H     40
#define PARM_Y    98
#define PARM_H    252
#define HELP_Y    (PARM_Y + PARM_H + 8)
#define BTN_Y     (H - PAD - 36)

#define ROW_Y0    (PARM_Y + 34)
#define ROW_H     30
#define ROWS_MAX  7

/* The control column, sized to stop clear of the steppers: a value as long
 * as MEDIUM HIGH has to fit beside them, not under them. */
#define CTRL_X    400
#define CTRL_W    216
#define STEP_DN_X 636
#define STEP_UP_X 748
#define STEP_W    34

#define MAX_PARAMS 10

/* ------------------------------------------------------------- the model */

typedef enum {
    PARAM_ENUM = 0,   /* one of a named few */
    PARAM_BOOL,       /* on or off */
    PARAM_NUMBER,     /* a bounded number, with a unit */
} param_kind_t;

typedef struct {
    const char  *group;        /* NULL continues the one above */
    const char  *name;
    param_kind_t kind;
    const char  *help;         /* what it does, in one line */

    const char *const *choices;/* ENUM */
    int          count;

    int          lo, hi, step; /* NUMBER, in tenths if decimals is 1 */
    int          decimals;
    const char  *unit;

    int          initial;
} param_def_t;

typedef struct {
    const char *name;
    const char *transport;
    const char *device;
    const param_def_t *params;
    int         count;
    int         klass;
} proto_t;

typedef struct {
    const char *name;
    const char *blurb;
} class_t;

enum { CLASS_ESC = 0, CLASS_SERVO, CLASS_COUNT };

static const class_t k_classes[CLASS_COUNT] = {
    { "ESC",   "bootloader, CLI and framed packets" },
    { "SERVO", "servos with a published protocol" },
};

typedef enum { STAGE_CLASS = 0, STAGE_PROTOCOL, STAGE_DEVICE } stage_t;

/* ------------------------------------------------------- the parameters */

static const char *const k_dir[]    = { "NORMAL", "REVERSED", "BIDIRECTIONAL" };
/*
 * BLHeli_S alone puts timing in named steps, because that is what its
 * configurator does; every firmware after it settled on degrees of advance,
 * and 0 to 31 is the range they all use.  Two representations of the same
 * physical quantity, in one list, drawn by the same renderer -- which is the
 * argument for the definitions carrying their own kind rather than the
 * screen assuming one.
 */
static const char *const k_timing[] = { "LOW", "MEDIUM LOW", "MEDIUM",
                                        "MEDIUM HIGH", "HIGH" };
static const char *const k_pwm[]    = { "24 kHz", "48 kHz", "96 kHz" };
static const char *const k_demag[]  = { "OFF", "LOW", "HIGH" };
static const char *const k_mtype[]  = { "BLDC", "FOC" };
static const char *const k_res[]    = { "STANDARD", "HIGH" };

static const param_def_t k_blheli[] = {
  { "MOTOR", "Motor direction", PARAM_ENUM,
    "Which way it turns, and whether reverse is allowed at all",
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { NULL, "Timing", PARAM_ENUM,
    "How far ahead of the rotor the drive commutates: more suits high kV",
    k_timing, 5, 0,0,0,0, NULL, 2 },
  { NULL, "PWM frequency", PARAM_ENUM,
    "Higher is quieter and warmer; lower is efficient and audible",
    k_pwm, 3, 0,0,0,0, NULL, 0 },
  { "STARTUP", "Startup power", PARAM_NUMBER,
    "How hard it pushes to get moving before it can sense the rotor",
    NULL, 0, 25, 150, 25, 0, "%", 100 },
  { NULL, "Demag compensation", PARAM_ENUM,
    "Backs off when the field collapses late: cures stalls under load",
    k_demag, 3, 0,0,0,0, NULL, 1 },
  { "PROTECTION", "Brake on stop", PARAM_BOOL,
    "Holds the motor still at zero throttle instead of letting it freewheel",
    NULL, 0, 0,0,0,0, NULL, 0 },
  { NULL, "Low voltage cut", PARAM_NUMBER,
    "Per cell, where it starts pulling power back to save the pack",
    NULL, 0, 28, 38, 1, 1, "V", 35 },
  { "SOUND", "Beep volume", PARAM_NUMBER,
    "How loud the startup tones and the lost-signal beep are",
    NULL, 0, 0, 100, 25, 0, "%", 50 },
};

static const param_def_t k_am32[] = {
  { "MOTOR", "Motor direction", PARAM_ENUM,
    "Which way it turns, and whether reverse is allowed at all",
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { NULL, "Timing advance", PARAM_NUMBER,
    "Degrees ahead of the rotor: more rpm and more heat, less is cooler",
    NULL, 0, 0, 30, 1, 0, "deg", 22 },
  { NULL, "PWM frequency", PARAM_ENUM,
    "Higher is quieter and warmer; lower is efficient and audible",
    k_pwm, 3, 0,0,0,0, NULL, 1 },
  { "STARTUP", "Sinusoidal startup", PARAM_BOOL,
    "Drives a smooth wave until it has enough speed to sense the rotor",
    NULL, 0, 0,0,0,0, NULL, 1 },
  { NULL, "Startup power", PARAM_NUMBER,
    "How hard it pushes to get moving before it can sense the rotor",
    NULL, 0, 25, 150, 25, 0, "%", 75 },
  { "PROTECTION", "Complementary PWM", PARAM_BOOL,
    "Drives both halves of the bridge: cooler, but needs healthy timing",
    NULL, 0, 0,0,0,0, NULL, 1 },
  { NULL, "Low voltage cut", PARAM_NUMBER,
    "Per cell, where it starts pulling power back to save the pack",
    NULL, 0, 28, 38, 1, 1, "V", 33 },
};

static const param_def_t k_escape32[] = {
  { "MOTOR", "Motor direction", PARAM_ENUM,
    "Which way it turns, and whether reverse is allowed at all",
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { NULL, "Timing", PARAM_NUMBER,
    "Degrees ahead of the rotor: more rpm and more heat, less is cooler",
    NULL, 0, 0, 30, 1, 0, "deg", 18 },
  { NULL, "PWM frequency", PARAM_ENUM,
    "Higher is quieter and warmer; lower is efficient and audible",
    k_pwm, 3, 0,0,0,0, NULL, 2 },
  { "STARTUP", "Sine startup", PARAM_BOOL,
    "Drives a smooth wave until it has enough speed to sense the rotor",
    NULL, 0, 0,0,0,0, NULL, 1 },
  { "PROTECTION", "Brake on stop", PARAM_BOOL,
    "Holds the motor still at zero throttle instead of letting it freewheel",
    NULL, 0, 0,0,0,0, NULL, 1 },
  { NULL, "Telemetry", PARAM_BOOL,
    "Sends volts, amps and rpm back down the signal wire",
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const param_def_t k_vesc[] = {
  { "MOTOR", "Motor type", PARAM_ENUM,
    "Six-step commutation, or field-oriented control",
    k_mtype, 2, 0,0,0,0, NULL, 1 },
  { NULL, "Current limit", PARAM_NUMBER,
    "The most it will draw through the motor, whatever is asked of it",
    NULL, 0, 10, 100, 10, 0, "A", 40 },
  { "PROTECTION", "Regen braking", PARAM_BOOL,
    "Puts braking energy back into the pack rather than into the motor",
    NULL, 0, 0,0,0,0, NULL, 1 },
  { NULL, "Low voltage cut", PARAM_NUMBER,
    "Per cell, where it starts pulling power back to save the pack",
    NULL, 0, 28, 38, 1, 1, "V", 34 },
  { "REPORTING", "Telemetry", PARAM_BOOL,
    "Sends volts, amps and rpm back down the signal wire",
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const param_def_t k_hitec[] = {
  { "TRAVEL", "Centre", PARAM_NUMBER,
    "Where neutral sits, in microseconds either side of 1500",
    NULL, 0, -50, 50, 5, 0, "us", 0 },
  { NULL, "Endpoint travel", PARAM_NUMBER,
    "How far it is allowed to go each way from centre",
    NULL, 0, 50, 150, 10, 0, "%", 100 },
  { NULL, "Direction", PARAM_ENUM,
    "Which way the horn moves for a rising pulse",
    k_dir, 2, 0,0,0,0, NULL, 0 },
  { "RESPONSE", "Speed", PARAM_NUMBER,
    "Slews the horn deliberately, for scale models and gentle linkages",
    NULL, 0, 20, 100, 10, 0, "%", 100 },
  { NULL, "Dead band", PARAM_NUMBER,
    "How far off target it tolerates before correcting: wider runs cooler",
    NULL, 0, 1, 10, 1, 0, "us", 2 },
  { NULL, "Resolution", PARAM_ENUM,
    "How finely it resolves the commanded position",
    k_res, 2, 0,0,0,0, NULL, 0 },
  { "PROTECTION", "Overload protect", PARAM_BOOL,
    "Backs off when it has been stalled long enough to cook itself",
    NULL, 0, 0,0,0,0, NULL, 1 },
  { NULL, "Fail-safe", PARAM_BOOL,
    "Goes to a set position when the pulse stops, rather than going limp",
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const proto_t k_protos[] = {
    { "BLHeli_S", "one-wire bootloader, 19200 baud",
      "BLHeli_S 16.7  on  EFM8BB21", k_blheli,   8, CLASS_ESC },
    { "AM32",     "one-wire bootloader, 19200 baud",
      "AM32 2.15  on  STM32G071",    k_am32,     7, CLASS_ESC },
    { "ESCape32", "text CLI over the signal line",
      "ESCape32 v9  on  AT32F421",   k_escape32, 6, CLASS_ESC },
    { "VESC",     "framed packets, 115200 baud",
      "VESC 6.05  on  STM32F405",    k_vesc,     5, CLASS_ESC },
    { "Hitec",    "D-series servo protocol",
      "Hitec D956TW",                k_hitec,    8, CLASS_SERVO },
};
#define PROTO_COUNT ((int)(sizeof(k_protos) / sizeof(k_protos[0])))

/* --------------------------------------------------------------- the state */

static struct {
    stage_t stage;
    int  klass;
    int  proto;
    bool connected;

    /*
     * What the device said, and what it has been told since.  Two arrays
     * rather than one, because "changed but not written" is a state the
     * screen must be able to show -- a value edited into a box that looks
     * exactly like a value read off the hardware is the same mistake as a
     * simulated reading that looks measured.
     */
    int  device[MAX_PARAMS];
    int  value[MAX_PARAMS];

    int  scroll;
    int  picked;               /* whose help is showing */

    gfx_rect_t tile[CLASS_COUNT];
    gfx_rect_t back, connect_btn, read_btn, write_btn;
    gfx_rect_t page_up, page_dn;
    gfx_rect_t row[ROWS_MAX], down[ROWS_MAX], up[ROWS_MAX];

    uint32_t rev;
    uint32_t drawn[2];
    unsigned drawn_mask;
} s;

static const proto_t *proto(void) { return &k_protos[s.proto]; }

static void adopt_device(void)
{
    const proto_t *p = proto();
    for (int i = 0; i < MAX_PARAMS; ++i) {
        const int v = (i < p->count) ? p->params[i].initial : 0;
        s.device[i] = v;
        s.value[i]  = v;
    }
    s.scroll = 0;
    s.picked = 0;
}

static gfx_rect_t proto_row(int i)
{
    int slot = 0;
    for (int p = 0; p < i; ++p) {
        if (k_protos[p].klass == k_protos[i].klass) {
            ++slot;
        }
    }
    return (gfx_rect_t){ (int16_t)(PAD + 12), (int16_t)(64 + slot * 68),
                         (int16_t)(W - 2 * PAD - 24), 58 };
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    s.stage    = STAGE_CLASS;

    const int tw = (W - 2 * PAD - 24 - 16) / CLASS_COUNT;
    for (int i = 0; i < CLASS_COUNT; ++i) {
        s.tile[i] = (gfx_rect_t){ (int16_t)(PAD + 12 + i * (tw + 16)),
                                  90, (int16_t)tw, 180 };
    }
    s.back        = (gfx_rect_t){ (int16_t)(PAD + 12), CRUMB_Y, 96, 30 };
    s.connect_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                  (int16_t)(DEV_Y + 2), 168, 36 };
    s.read_btn    = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 350),
                                  (int16_t)BTN_Y, 168, 34 };
    s.write_btn   = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                  (int16_t)BTN_Y, 168, 34 };
    s.page_up     = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76),
                                  (int16_t)(PARM_Y + 6), STEP_W, 24 };
    s.page_dn     = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38),
                                  (int16_t)(PARM_Y + 6), STEP_W, 24 };
    for (int i = 0; i < ROWS_MAX; ++i) {
        const int y = ROW_Y0 + i * ROW_H;
        s.row[i]  = (gfx_rect_t){ (int16_t)(PAD + 8), (int16_t)(y - 4),
                                  (int16_t)(CTRL_X - PAD - 16), ROW_H - 2 };
        s.down[i] = (gfx_rect_t){ STEP_DN_X, (int16_t)(y - 2), STEP_W, 24 };
        s.up[i]   = (gfx_rect_t){ STEP_UP_X, (int16_t)(y - 2), STEP_W, 24 };
    }
    adopt_device();
}

int  programmer_screen_protocol(void)  { return s.proto; }
bool programmer_screen_connected(void) { return s.connected; }

int programmer_screen_value(int param)
{
    if (param < 0 || param >= proto()->count) {
        return -1;
    }
    return s.value[param];
}

int programmer_screen_dirty(void)
{
    int n = 0;
    for (int i = 0; i < proto()->count; ++i) {
        if (s.value[i] != s.device[i]) {
            ++n;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ events */

static int rows_shown(void)
{
    const int n = proto()->count - s.scroll;
    return (n > ROWS_MAX) ? ROWS_MAX : n;
}

/*
 * One stepper for every kind, because every kind is a bounded ordered set:
 * a switch has two values, a choice has its list, a number has its range.
 * The widget differs; the gesture does not.
 */
static void step(int row, int by)
{
    const int i = s.scroll + row;
    if (i < 0 || i >= proto()->count) {
        return;
    }
    const param_def_t *d = &proto()->params[i];
    int v = s.value[i];

    switch (d->kind) {
    case PARAM_BOOL:   v = (by > 0) ? 1 : 0;            break;
    case PARAM_ENUM:   v += by;                          break;
    case PARAM_NUMBER: v += by * d->step;                break;
    }

    /* Clamped, not wrapped.  A parameter that rolls from its last value round
     * to its first will one day be set to the wrong end by somebody pressing
     * once more than they meant to, and on an ESC the wrong end is a
     * direction. */
    const int lo = (d->kind == PARAM_NUMBER) ? d->lo : 0;
    const int hi = (d->kind == PARAM_NUMBER) ? d->hi
                 : (d->kind == PARAM_BOOL)   ? 1 : d->count - 1;
    if (v < lo) { v = lo; }
    if (v > hi) { v = hi; }

    if (v != s.value[i]) {
        s.value[i] = v;
        s.picked   = i;
        ++s.rev;
    }
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL || evt->type != TOUCH_EVENT_DOWN) {
        return;
    }
    const int px = evt->point.x, py = evt->point.y;

    /* Back climbs one level, rather than leaving the screen -- the band's own
     * tag does that, and conflating the two is how people end up on the menu
     * when they wanted the protocol list. */
    if (s.stage != STAGE_CLASS && gfx_rect_contains(s.back, px, py)) {
        s.stage = (s.stage == STAGE_DEVICE) ? STAGE_PROTOCOL : STAGE_CLASS;
        if (s.stage == STAGE_PROTOCOL) {
            s.connected = false;
        }
        ++s.rev;
        return;
    }

    if (s.stage == STAGE_CLASS) {
        for (int i = 0; i < CLASS_COUNT; ++i) {
            if (gfx_rect_contains(s.tile[i], px, py)) {
                s.klass = i;
                s.stage = STAGE_PROTOCOL;
                ++s.rev;
                return;
            }
        }
        return;
    }

    if (s.stage == STAGE_PROTOCOL) {
        for (int i = 0; i < PROTO_COUNT; ++i) {
            if (k_protos[i].klass == s.klass
                && gfx_rect_contains(proto_row(i), px, py)) {
                s.proto = i;
                s.connected = false;
                adopt_device();
                s.stage = STAGE_DEVICE;
                ++s.rev;
                return;
            }
        }
        return;
    }

    if (gfx_rect_contains(s.connect_btn, px, py)) {
        s.connected = !s.connected;
        if (s.connected) {
            adopt_device();
        }
        ++s.rev;
        return;
    }
    if (!s.connected) {
        return;
    }

    /* Reading takes what the device says, which throws away staged edits --
     * that is what reading means, and the button says READ. */
    if (gfx_rect_contains(s.read_btn, px, py)) {
        for (int i = 0; i < proto()->count; ++i) {
            s.value[i] = s.device[i];
        }
        ++s.rev;
        return;
    }
    /* And writing makes the device agree with the screen. */
    if (gfx_rect_contains(s.write_btn, px, py)) {
        for (int i = 0; i < proto()->count; ++i) {
            s.device[i] = s.value[i];
        }
        ++s.rev;
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
        /* Touching the name asks what it does. */
        if (gfx_rect_contains(s.row[i], px, py)) {
            s.picked = s.scroll + i;
            ++s.rev;
            return;
        }
    }
}

/* ----------------------------------------------------------------- drawing */

static void fmt_value(const param_def_t *d, int v, char *out, size_t n)
{
    switch (d->kind) {
    case PARAM_BOOL:
        snprintf(out, n, "%s", v ? "ON" : "OFF");
        return;
    case PARAM_ENUM:
        snprintf(out, n, "%s", d->choices[v]);
        return;
    case PARAM_NUMBER:
        if (d->decimals == 1) {
            snprintf(out, n, "%d.%d %s", v / 10, (v < 0 ? -v : v) % 10,
                     d->unit);
        } else {
            snprintf(out, n, "%d %s", v, d->unit);
        }
        return;
    }
}

static void draw_crumb(gfx_canvas_t *c, const char *trail)
{
    ui_button(c, s.back, "BACK", ui_theme_color(UI_C_PANEL_HI), false, true);
    gfx_text(c, s.back.x + s.back.w + 16, CRUMB_Y + 8, trail, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

static void draw_classes(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, 24, "WHAT ARE YOU PROGRAMMING?", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    for (int i = 0; i < CLASS_COUNT; ++i) {
        const gfx_rect_t r = s.tile[i];
        ui_card(c, r, ui_theme_color(UI_C_PANEL_HI));
        gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + 62), r.w, 28 },
                    k_classes[i].name, UI_FONT_HEAD,
                    ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_CENTER);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 12),
                                     (int16_t)(r.y + 104),
                                     (int16_t)(r.w - 24), 16 },
                    k_classes[i].blurb, UI_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_CENTER);
        int n = 0;
        for (int p = 0; p < PROTO_COUNT; ++p) {
            if (k_protos[p].klass == i) { ++n; }
        }
        char have[32];
        snprintf(have, sizeof(have), "%d protocol%s", n, (n == 1) ? "" : "s");
        gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + 138), r.w, 16 },
                    have, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_CENTER);
    }
}

static void draw_protocol_list(gfx_canvas_t *c)
{
    draw_crumb(c, k_classes[s.klass].name);
    for (int i = 0; i < PROTO_COUNT; ++i) {
        if (k_protos[i].klass != s.klass) {
            continue;
        }
        const gfx_rect_t r = proto_row(i);
        ui_card(c, r, ui_theme_color(UI_C_PANEL));
        gfx_text(c, r.x + 20, r.y + 12, k_protos[i].name, UI_FONT_HEAD,
                 ui_theme_color(UI_C_TEXT), 1);
        /* The transport, on every row.  This is the whole reason the list
         * exists rather than an autodetect: they are not variations of one
         * thing, they are different conversations. */
        gfx_text(c, r.x + 20, r.y + 38, k_protos[i].transport,
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 60),
                                     (int16_t)(r.y + 20), 40, 20 },
                    ">", UI_FONT_HEAD, ui_theme_color(UI_C_ACCENT), 1,
                    GFX_ALIGN_CENTER);
    }
}

static void draw_device(gfx_canvas_t *c)
{
    const int x = PAD + 12;
    const gfx_color_t tone = s.connected ? ui_theme_color(UI_C_OK)
                                         : ui_theme_color(UI_C_TEXT_FAINT);
    gfx_fill_circle_aa(c, x + 7, DEV_Y + 20, 6, tone);
    gfx_text(c, x + 22, DEV_Y + 4, s.connected ? proto()->device
                                               : "nothing has answered",
             UI_FONT_LABEL,
             s.connected ? ui_theme_color(UI_C_TEXT)
                         : ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, x + 22, DEV_Y + 24, proto()->transport, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, s.connect_btn, s.connected ? "DISCONNECT" : "CONNECT",
              s.connected ? ui_theme_color(UI_C_PANEL_HI)
                          : ui_theme_color(UI_C_ACCENT),
              false, true);
}

/* --- one widget per kind, and there are only three ---------------------- */

static void widget_bool(gfx_canvas_t *c, int y, bool on, gfx_color_t ink)
{
    const int tw = 56, th = 22;
    const int x = CTRL_X + CTRL_W - tw;
    gfx_fill_round_rect(c, x, y - 3, tw, th, th / 2,
                        on ? ink : ui_theme_color(UI_C_PANEL_SUNK));
    gfx_fill_circle_aa(c, on ? (x + tw - 11) : (x + 11), y + th / 2 - 3, 8,
                       on ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                          : ui_theme_color(UI_C_TEXT_DIM));
}

static void widget_enum(gfx_canvas_t *c, int y, const char *text,
                        gfx_color_t ink)
{
    gfx_text_in(c, (gfx_rect_t){ CTRL_X, (int16_t)y, CTRL_W, 16 },
                text, UI_FONT_LABEL, ink, 1, GFX_ALIGN_RIGHT);
}

static void widget_number(gfx_canvas_t *c, int y, const param_def_t *d,
                          int v, const char *text, gfx_color_t ink)
{
    gfx_text_in(c, (gfx_rect_t){ CTRL_X, (int16_t)y, CTRL_W, 16 },
                text, UI_FONT_LABEL, ink, 1, GFX_ALIGN_RIGHT);
    /* A hairline of range under the number.  An enum is a list and a number
     * is a continuum, and the difference is worth one row of pixels: it says
     * how much of the travel is left without spending a line on it. */
    const int bw = CTRL_W;
    const int span = (d->hi > d->lo) ? d->hi - d->lo : 1;
    const int fill = (v - d->lo) * bw / span;
    gfx_hline(c, CTRL_X, y + 19, bw, ui_theme_color(UI_C_PANEL_SUNK));
    if (fill > 0) {
        gfx_hline(c, CTRL_X, y + 19, fill, ink);
    }
}

static void draw_params(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, PARM_Y + 12, "PARAMETERS", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    if (!s.connected) {
        gfx_text_in(c, (gfx_rect_t){ PAD, (int16_t)(PARM_Y + PARM_H / 2 - 8),
                                     (int16_t)(W - 2 * PAD), 16 },
                    "no device, so nothing to show", UI_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
        return;
    }

    const int max_scroll = (proto()->count > ROWS_MAX)
                               ? proto()->count - ROWS_MAX : 0;
    char count[40];
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
        const bool dirty = (s.value[idx] != s.device[idx]);

        if (idx == s.picked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        /* The group, on the row that starts one. */
        if (d->group != NULL) {
            gfx_text(c, PAD + 12, y, d->group, UI_FONT_LABEL,
                     ui_theme_color(UI_C_TEXT_FAINT), 1);
        }
        /*
         * A staged change wears a mark and its own colour.  Everything on
         * this screen otherwise looks the same whether it came off the device
         * or was typed at it a second ago, which is the measured-versus-
         * invented mistake wearing different clothes.
         */
        if (dirty) {
            /* Beside the name, not at the card's edge, where the group
             * label already is. */
            gfx_fill_round_rect(c, PAD + 96, y + 2, 3, 12, 1,
                                ui_theme_color(UI_C_WARN));
        }
        gfx_text(c, PAD + 108, y, d->name, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);

        const gfx_color_t ink = dirty ? ui_theme_color(UI_C_WARN)
                                      : ui_theme_color(UI_C_ACCENT);
        char text[40];
        fmt_value(d, s.value[idx], text, sizeof(text));

        switch (d->kind) {
        case PARAM_BOOL:   widget_bool(c, y, s.value[idx] != 0, ink); break;
        case PARAM_ENUM:   widget_enum(c, y, text, ink);              break;
        case PARAM_NUMBER: widget_number(c, y, d, s.value[idx], text, ink);
                           break;
        }

        const int lo = (d->kind == PARAM_NUMBER) ? d->lo : 0;
        const int hi = (d->kind == PARAM_NUMBER) ? d->hi
                     : (d->kind == PARAM_BOOL)   ? 1 : d->count - 1;
        ui_button(c, s.down[i], "-", ui_theme_color(UI_C_PANEL_HI),
                  false, s.value[idx] > lo);
        ui_button(c, s.up[i], "+", ui_theme_color(UI_C_PANEL_HI),
                  false, s.value[idx] < hi);
    }

    /*
     * What the selected parameter does.  Without it a parameter list is a
     * quiz: "Demag compensation" is four syllables and no information, and
     * every configurator worth using answers that question somewhere.
     */
    const param_def_t *p = &proto()->params[s.picked];
    gfx_text(c, PAD + 12, HELP_Y, p->name, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, PAD + 12, HELP_Y + 18, p->help, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);

    const int n = programmer_screen_dirty();
    char wr[32];
    if (n > 0) {
        snprintf(wr, sizeof(wr), "WRITE %d", n);
    } else {
        snprintf(wr, sizeof(wr), "WRITE");
    }
    ui_button(c, s.read_btn, "READ", ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, s.write_btn, wr,
              (n > 0) ? ui_theme_color(UI_C_WARN)
                      : ui_theme_color(UI_C_PANEL_HI),
              false, n > 0);
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
    gfx_clear(c, ui_theme_color(UI_C_BG));

    if (s.stage == STAGE_CLASS) {
        draw_classes(c);
        return;
    }
    if (s.stage == STAGE_PROTOCOL) {
        draw_protocol_list(c);
        return;
    }

    char trail[64];
    snprintf(trail, sizeof(trail), "%s  >  %s",
             k_classes[s.klass].name, proto()->name);
    draw_crumb(c, trail);
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD), DEV_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, PARM_Y, (int16_t)(W - 2 * PAD), PARM_H },
            ui_theme_color(UI_C_PANEL));
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
