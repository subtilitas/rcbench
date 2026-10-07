/*
 * The programmer screen: one renderer for several protocols.
 *
 * BLHeli_S and AM32 speak a one-wire bootloader at 19,200 baud; ESCape32
 * answers a text CLI (command-line interface); VESC uses framed packets at
 * 115,200 baud; a Hitec D-series servo has its own protocol.  They share a
 * connector and nothing else, so the screen asks in order: the device class,
 * the protocol, and only then what answered.
 *
 * The parameters draw themselves.  A definition says what kind of setting
 * it is (a switch, a choice, a bounded number) and the renderer owns one
 * widget per kind.  Nothing here knows what BLHeli_S is.  Adding a firmware
 * is a table; new drawing code is needed only for a kind of setting none of
 * these firmwares has.
 *
 * BLHeli_32 is absent from the ESC (electronic speed controller) list.  The
 * bench identifies and drives these ESCs, and direction, 3D mode, beacon and
 * save-settings work as DShot special commands, but the parameters are
 * stored in a form the bench cannot read.  docs/BLHeli32.md has the detail.
 *
 * ESC STICK is the third class: an ESC with no wire to talk on, programmed
 * through its throttle-stick menu.  Its list is the ESC profiles
 * (esc_profile.h), its parameters are the profile's menu items, and its
 * write is a run of the stick programmer (esc_stick.h): the bench arms,
 * holds the throttle at the entry position, switches the supply on and
 * counts the beeps in the supply current.  A run starts only from a red
 * warning held for two seconds, as ARM and the servo's HV warning are, and
 * every move it makes goes out as the MOTOR screen's commands do, through
 * the arming policy and the output bank.
 *
 * SPDX-License-Identifier: MIT
 */

#include "programmer_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esc_profile.h"
#include "settings.h"
#include "supply_screen.h"
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

enum { CLASS_ESC = 0, CLASS_SERVO, CLASS_STICK, CLASS_COUNT };

static const class_t k_classes[CLASS_COUNT] = {
    { "ESC",   "bootloader, CLI and packets" },
    { "SERVO", "servos, published protocols" },
    { "ESC STICK", "throttle menus, by beeps" },
};

typedef enum { STAGE_CLASS = 0, STAGE_PROTOCOL, STAGE_DEVICE } stage_t;

/* ------------------------------------------------------- the parameters */

static const char *const k_dir[]    = { "NORMAL", "REVERSED", "BIDIRECTIONAL" };
/*
 * BLHeli_S puts timing in named steps, as its configurator does; the other
 * firmwares use degrees of advance, 0 to 31.  Two representations of one
 * quantity in one list is why each definition carries its own kind.
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

/* ------------------------------------------------------- the stick class */

/* The profile list: rows of SP_ROW_H from SP_ROW_Y0, SP_ROWS to a page. */
#define SP_MAX     (128)
#define SP_ROWS    9
#define SP_ROW_Y0  52
#define SP_ROW_H   36
#define SP_QUEUE   8
#define SP_NOTE    72

/* The settings the TIMING page shows, in its order. */
static const setting_id_t k_sp_settings[] = {
    SET_STICK_V, SET_STICK_I, SET_STICK_BEEP_MIN, SET_STICK_GAP_MIN,
    SET_STICK_LONG, SET_STICK_LONG_MAX, SET_STICK_GROUP_GAP, SET_STICK_ENTRY,
    SET_STICK_STORE, SET_STICK_OFF, SET_STICK_SILENCE, SET_STICK_TIMEOUT,
    SET_STICK_THRESHOLD, SET_STICK_HYST,
};
#define SP_SETTINGS ((int)(sizeof(k_sp_settings) / sizeof(k_sp_settings[0])))

typedef struct {
    /* The list: the profiles that can run, then those that cannot. */
    int  order[SP_MAX];
    int  count, runnable, scroll;

    /* The profile picked, and what is to change: per item, -1 to keep it,
     * else the index of the value to store. */
    const esc_profile_t *p;
    int  pick[256];
    int  iscroll, picked;
    char note[SP_NOTE];     /* why RUN cannot start; "" when it can */
    bool could;             /* whether RUN could, as last drawn */

    /* The warning, and its hold. */
    bool      warn, warn_down;
    uint8_t   warn_id;
    ui_hold_t hold;

    bool timing;
    int  tscroll, tpicked;

    /* The run, and whether its result is still on screen. */
    esc_stick_t run;
    bool        shown;
    uint32_t    runs;
    uint32_t    sig;        /* what the progress showed last */

    /* The bench, as the application last said. */
    uint32_t now_ms, stops;
    bool     armed, link_up;

    /* What the run has asked for, and what is still to be taken. */
    motor_cmd_t q[SP_QUEUE];
    int         qn;
    bool        sent_arm, sent_supply;
    float       sent_pct;

    gfx_rect_t rows[SP_ROWS], list_up, list_dn;
    gfx_rect_t hold_btn, cancel_btn;
} stick_t;

/* --------------------------------------------------------------- the state */

static struct {
    stage_t stage;
    int  klass;
    int  proto;
    bool connected;

    /*
     * What the device reported, and what has been staged since.  Two arrays,
     * because "changed but not written" is a state the screen shows: a
     * staged value must not look like a value read off the hardware.
     */
    int  device[MAX_PARAMS];
    int  value[MAX_PARAMS];

    int  scroll;
    int  picked;               /* whose help is showing */

    gfx_rect_t tile[CLASS_COUNT];
    gfx_rect_t back, connect_btn, read_btn, write_btn;
    gfx_rect_t page_up, page_dn;
    gfx_rect_t row[ROWS_MAX], down[ROWS_MAX], up[ROWS_MAX];

    stick_t  st;

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

void programmer_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    s.stage    = STAGE_CLASS;

    const int tw = (W - 2 * PAD - 24 - 16 * (CLASS_COUNT - 1)) / CLASS_COUNT;
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
    for (int i = 0; i < SP_ROWS; ++i) {
        s.st.rows[i] = (gfx_rect_t){ (int16_t)(PAD + 12),
                                     (int16_t)(SP_ROW_Y0 + i * SP_ROW_H),
                                     (int16_t)(W - 2 * PAD - 24),
                                     SP_ROW_H - 4 };
    }
    s.st.list_up = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76), CRUMB_Y,
                                 STEP_W, 30 };
    s.st.list_dn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38), CRUMB_Y,
                                 STEP_W, 30 };
    s.st.hold_btn   = (gfx_rect_t){ PAD + 20, (int16_t)(H - PAD - 76),
                                    260, 56 };
    s.st.cancel_btn = (gfx_rect_t){ (int16_t)(W - PAD - 20 - 180),
                                    (int16_t)(H - PAD - 76), 180, 56 };
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

    /* Clamped, not wrapped: one press too many on a wrapped list lands on the
     * other end, and on an ESC the other end of a list can be a direction. */
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

/* The stick class's half of the events, further down with its drawing. */
static bool sp_down(const touch_event_t *evt);
static void sp_track(const touch_event_t *evt);
static void sp_build_list(void);
static int  sp_runnable_count(void);
static void sp_render(gfx_canvas_t *c);

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (evt->type != TOUCH_EVENT_DOWN) {
        sp_track(evt);         /* only the warning's hold follows a finger */
        return;
    }
    const int px = evt->point.x, py = evt->point.y;

    if (s.klass == CLASS_STICK && s.stage != STAGE_CLASS
        && sp_down(evt)) {
        return;
    }

    /* Back climbs one level; the band's home tag leaves the screen. */
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
                if (i == CLASS_STICK) {
                    sp_build_list();
                }
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

    /* READ takes what the device says and discards staged edits. */
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
        char have[40];
        if (i == CLASS_STICK) {
            snprintf(have, sizeof(have), "%d of %d profiles run",
                     sp_runnable_count(), (int)esc_profiles_count());
        } else {
            snprintf(have, sizeof(have), "%d protocol%s", n,
                     (n == 1) ? "" : "s");
        }
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
        /* The transport, on every row: the protocols are different
         * conversations, not variants of one, so there is no autodetect. */
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
    /* A hairline under the number showing where in its range the value
     * sits. */
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
         * A staged change carries a mark and its own colour, so a value typed
         * at the screen is distinguishable from one read off the device.
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
     * The selected parameter's help line.
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

/* ============================================== the stick class ======== */

/*
 * A text cut to @p max_chars, with ".." in place of what did not fit: the
 * profiles' names are the manuals' and run long.
 */
static void sp_text(gfx_canvas_t *c, int x, int y, const char *text,
                    int max_chars, gfx_color_t ink)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "%s", (text != NULL) ? text : "");
    if (max_chars >= 3 && max_chars < (int)sizeof(buf)
        && (int)strlen(buf) > max_chars) {
        buf[max_chars - 2] = '.';
        buf[max_chars - 1] = '.';
        buf[max_chars] = '\0';
    }
    gfx_text(c, x, y, buf, UI_FONT_LABEL, ink, 1);
}

/*
 * Why a profile cannot run, into @p buf, or NULL when it can: the engine's
 * reason, or a voltage the supply cannot give -- VOLTAGE where it is set,
 * else the profile's cell count.
 */
static const char *sp_why(const esc_profile_t *p, char *buf, size_t n)
{
    const char *why = NULL;
    if (esc_stick_kind(p, &why) == ESC_STICK_KIND_NONE) {
        return why;
    }
    const float v = settings_get(SET_STICK_V);
    const uint32_t mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                                   : esc_stick_profile_mv(p);
    const unsigned cap = (unsigned)lroundf(supply_screen_caps().v_max
                                           * 1000.0f);
    if (mv > cap) {
        snprintf(buf, n, "needs %u.%u V, cap %u.%u V",
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u),
                 cap / 1000u, cap % 1000u / 100u);
        return buf;
    }
    return NULL;
}

static bool sp_runs(const esc_profile_t *p)
{
    char buf[40];
    return sp_why(p, buf, sizeof(buf)) == NULL;
}

static int sp_runnable_count(void)
{
    int n = 0;
    const size_t total = esc_profiles_count();
    for (size_t i = 0; i < total; ++i) {
        n += sp_runs(esc_profiles_at(i)) ? 1 : 0;
    }
    return n;
}

/* What can run first, then what cannot, each in the registry's order. */
static void sp_build_list(void)
{
    stick_t *t = &s.st;
    t->count = 0;
    t->runnable = 0;
    t->scroll = 0;
    const size_t total = esc_profiles_count();
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < total && t->count < SP_MAX; ++i) {
            const bool runs = sp_runs(esc_profiles_at(i));
            if (runs == (pass == 0)) {
                t->order[t->count++] = (int)i;
                t->runnable += runs ? 1 : 0;
            }
        }
    }
}

static void sp_pick_profile(const esc_profile_t *p)
{
    stick_t *t = &s.st;
    t->p = p;
    for (int i = 0; i < 256; ++i) {
        t->pick[i] = -1;
    }
    t->iscroll = 0;
    t->picked = 0;
    t->shown = false;
    t->timing = false;
    t->warn = false;
    t->warn_down = false;
    ui_hold_reset(&t->hold);
}

/* The picks as changes, in item order; how many were picked in all. */
static size_t sp_changes(esc_stick_change_t *ch, size_t *picked)
{
    const stick_t *t = &s.st;
    size_t n = 0, all = 0;
    for (unsigned i = 0; t->p != NULL && i < t->p->item_count; ++i) {
        if (t->pick[i] < 0) {
            continue;
        }
        ++all;
        if (n < ESC_STICK_MAX_CHANGES) {
            ch[n].item = (uint8_t)i;
            ch[n].value = (uint8_t)t->pick[i];
            ++n;
        }
    }
    *picked = all;
    return n;
}

/* The timing as the settings hold it.  The entry is the profile's own where
 * it states one: the manual's number, not the bench's guess. */
static void sp_timing(esc_stick_timing_t *t)
{
    const esc_profile_t *p = s.st.p;
    t->beep_min_ms   = (uint32_t)settings_get_int(SET_STICK_BEEP_MIN);
    t->gap_min_ms    = (uint32_t)settings_get_int(SET_STICK_GAP_MIN);
    t->long_ms       = (uint32_t)settings_get_int(SET_STICK_LONG);
    t->long_max_ms   = (uint32_t)settings_get_int(SET_STICK_LONG_MAX);
    t->group_gap_ms  = (uint32_t)settings_get_int(SET_STICK_GROUP_GAP);
    t->entry_ms      = (p != NULL && p->entry_hold_ms != 0u)
                           ? p->entry_hold_ms
                           : (uint32_t)settings_get_int(SET_STICK_ENTRY);
    t->store_ms      = (uint32_t)settings_get_int(SET_STICK_STORE);
    t->off_ms        = (uint32_t)settings_get_int(SET_STICK_OFF);
    t->silence_ms    = (uint32_t)settings_get_int(SET_STICK_SILENCE);
    t->timeout_ms    = (uint32_t)settings_get_int(SET_STICK_TIMEOUT);
    t->threshold_ma  = (uint32_t)settings_get_int(SET_STICK_THRESHOLD);
    t->hysteresis_ma = (uint32_t)settings_get_int(SET_STICK_HYST);
}

/* The supply's set points for a run, mV and mA: VOLTAGE, or the profile's
 * cell count when it is 0. */
static void sp_supply(uint32_t *mv, uint32_t *ma)
{
    const float v = settings_get(SET_STICK_V);
    *mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                     : esc_stick_profile_mv(s.st.p);
    *ma = (uint32_t)lroundf(settings_get(SET_STICK_I) * 1000.0f);
}

/*
 * Everything a run needs, and whether it can start.  When it cannot, the
 * note says why; with nothing picked the note stays empty and RUN is simply
 * not offered.  Refused, never adjusted: a voltage over the SUPPLY cap is
 * the operator's to change.
 */
static bool sp_plan(esc_stick_change_t *ch, size_t *n,
                    esc_stick_timing_t *t, uint32_t *mv, uint32_t *ma)
{
    stick_t *st = &s.st;
    st->note[0] = '\0';
    size_t picked = 0;
    *n = sp_changes(ch, &picked);
    sp_timing(t);
    sp_supply(mv, ma);
    if (st->p == NULL || picked == 0u) {
        return false;
    }
    if (picked > ESC_STICK_MAX_CHANGES) {
        snprintf(st->note, sizeof(st->note), "at most %u changes in one run",
                 (unsigned)ESC_STICK_MAX_CHANGES);
        return false;
    }
    if (*mv == 0u) {
        snprintf(st->note, sizeof(st->note),
                 "no cell count in the profile: set VOLTAGE on TIMING");
        return false;
    }
    const supply_caps_t caps = supply_screen_caps();
    const unsigned vmax = (unsigned)lroundf(caps.v_max * 1000.0f);
    const unsigned vmin = (unsigned)lroundf(caps.v_min * 1000.0f);
    const unsigned imax = (unsigned)lroundf(caps.i_max * 1000.0f);
    if (*mv > vmax) {
        snprintf(st->note, sizeof(st->note),
                 "%u.%02u V is above the SUPPLY cap of %u.%02u V",
                 (unsigned)(*mv / 1000u), (unsigned)(*mv % 1000u / 10u),
                 vmax / 1000u, vmax % 1000u / 10u);
        return false;
    }
    if (*mv < vmin) {
        snprintf(st->note, sizeof(st->note),
                 "VOLTAGE is below the supply's %u.%02u V",
                 vmin / 1000u, vmin % 1000u / 10u);
        return false;
    }
    if (*ma > imax) {
        snprintf(st->note, sizeof(st->note),
                 "CURRENT LIMIT is above the SUPPLY cap of %u.%02u A",
                 imax / 1000u, imax % 1000u / 10u);
        return false;
    }
    if (supply_screen_output_live()) {
        snprintf(st->note, sizeof(st->note),
                 "switch the supply's output off first");
        return false;
    }
    const char *why = NULL;
    if (!esc_stick_check(st->p, ch, *n, t, &why)) {
        snprintf(st->note, sizeof(st->note), "%s",
                 (why != NULL) ? why : "refused");
        return false;
    }
    return true;
}

static bool sp_can_run(void)
{
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n;
    esc_stick_timing_t t;
    uint32_t mv, ma;
    return sp_plan(ch, &n, &t, &mv, &ma);
}

/* A command for the application.  The queue keeps the newest: a DISARM is
 * never the one dropped, because nothing is queued behind one. */
static void sp_push(motor_cmd_kind_t kind, float value)
{
    stick_t *t = &s.st;
    if (t->qn == SP_QUEUE) {
        memmove(&t->q[0], &t->q[1], (SP_QUEUE - 1) * sizeof(t->q[0]));
        t->qn--;
    }
    t->q[t->qn].kind = kind;
    t->q[t->qn].value = value;
    t->qn++;
}

/* What the run asks for, onto the paths an operator's own presses take:
 * ARM, DISARM and THROTTLE as the MOTOR screen sends them, the supply as
 * SUPPLY's own switch. */
static void sp_follow(void)
{
    stick_t *t = &s.st;
    const esc_stick_out_t *o = esc_stick_out(&t->run);
    if (o->arm != t->sent_arm) {
        t->sent_arm = o->arm;
        sp_push(o->arm ? MOTOR_CMD_ARM : MOTOR_CMD_DISARM, 0.0f);
    }
    if (o->throttle_pct != t->sent_pct) {
        t->sent_pct = o->throttle_pct;
        sp_push(MOTOR_CMD_THROTTLE, o->throttle_pct);
    }
    if (o->supply_on != t->sent_supply) {
        t->sent_supply = o->supply_on;
        if (o->supply_on) {
            supply_screen_put((float)o->supply_mv / 1000.0f,
                              (float)o->supply_ma / 1000.0f);
            supply_screen_ask_on();
        } else {
            supply_screen_ask_off();
        }
    }
}

static void sp_start(void)
{
    stick_t *t = &s.st;
    t->warn = false;
    t->warn_down = false;
    ui_hold_reset(&t->hold);
    ++s.rev;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n;
    esc_stick_timing_t tm;
    uint32_t mv, ma;
    if (!sp_plan(ch, &n, &tm, &mv, &ma)) {
        return;     /* the note says why */
    }
    const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                  t->link_up };
    const char *why = NULL;
    if (!esc_stick_start(&t->run, t->p, ch, n, &tm, mv, ma, &b, &why)) {
        snprintf(t->note, sizeof(t->note), "%s",
                 (why != NULL) ? why : "refused");
        return;
    }
    t->runs++;
    t->shown = true;
    t->sent_arm = false;
    t->sent_supply = false;
    t->sent_pct = ESC_STICK_PCT_MIN;
    t->sig = 0u;
    /* The set points before anything else, so an ON never meets old ones. */
    supply_screen_put((float)mv / 1000.0f, (float)ma / 1000.0f);
    sp_follow();
}

static void sp_end_hold(void)
{
    if (s.st.warn_down || s.st.hold.held_s > 0.0f) {
        s.st.warn_down = false;
        ui_hold_reset(&s.st.hold);
        ++s.rev;
    }
}

/* What the display shows of a run, to repaint only when it changes. */
static uint32_t sp_signature(void)
{
    const esc_stick_t *e = &s.st.run;
    const uint32_t tenths = (e->phase == ESC_STICK_ENTRY
                             || e->phase == ESC_STICK_SIGNAL)
                                ? e->now_ms / 100u : 0u;
    const uint32_t parts[] = {
        (uint32_t)e->phase, esc_stick_beeps(e), e->groups, e->last_count,
        e->last_valid ? 1u : 0u, e->last_trusted ? 1u : 0u,
        e->last_in_order ? 1u : 0u,
        (uint32_t)e->ma, (uint32_t)esc_det_floor_ma(&e->det), e->iv_ms,
        esc_stick_done_count(e), e->entries, e->active, tenths,
    };
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        h = (h ^ parts[i]) * 16777619u;
    }
    return h;
}

static void sp_tick(float dt_s)
{
    stick_t *t = &s.st;
    if (t->warn && t->warn_down) {
        ++s.rev;            /* the hold's fill */
        if (ui_hold_tick(&t->hold, dt_s)) {
            sp_start();
        }
    }
    if (esc_stick_running(&t->run)) {
        const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                      t->link_up };
        esc_stick_step(&t->run, &b);
        sp_follow();
        const uint32_t sig = sp_signature();
        if (sig != t->sig) {
            t->sig = sig;
            ++s.rev;
        }
        return;
    }
    if (s.klass == CLASS_STICK && s.stage == STAGE_DEVICE && t->p != NULL
        && !t->shown) {
        /* RUN follows the supply and the caps, which change elsewhere. */
        char was[SP_NOTE];
        memcpy(was, t->note, sizeof(was));
        const bool could = sp_can_run();
        if (strcmp(was, t->note) != 0 || could != t->could) {
            t->could = could;
            ++s.rev;
        }
    }
}

static void sp_leave(void)
{
    stick_t *t = &s.st;
    if (esc_stick_running(&t->run)) {
        esc_stick_abort(&t->run, ESC_STICK_R_LEFT);
        sp_follow();
    }
    /* A warning not held is a run not started. */
    t->warn = false;
    sp_end_hold();
    if (t->timing) {
        t->timing = false;
        if (settings_dirty()) {
            settings_request_save();
        }
    }
}

/*
 * Drop the ARM and the THROTTLE a run has queued and the application has
 * not taken: the hold completes in the frame's tick, after the frame has
 * handed its commands over, so they wait a frame here -- and the next frame
 * would stamp them with what it knows then, past a stop or a loss that
 * came in between.  Returns whether an ARM was among them.
 */
static bool sp_drop_drive(void)
{
    stick_t *t = &s.st;
    bool arm = false;
    int k = 0;
    for (int i = 0; i < t->qn; ++i) {
        const motor_cmd_kind_t kind = t->q[i].kind;
        if (kind == MOTOR_CMD_ARM || kind == MOTOR_CMD_THROTTLE) {
            arm = arm || kind == MOTOR_CMD_ARM;
            continue;
        }
        t->q[k++] = t->q[i];
    }
    t->qn = k;
    return arm;
}

/* The run ends now, and nothing it queued to drive survives the end. */
static void sp_end_run(esc_stick_reason_t why)
{
    stick_t *t = &s.st;
    if (!esc_stick_running(&t->run)) {
        return;
    }
    (void)sp_drop_drive();
    esc_stick_abort(&t->run, why);
    sp_follow();
    ++s.rev;
}

/*
 * Touch went missing: a hold under way may have no finger on it, and an
 * arm the warning's hold asked for may have come from a contact the panel
 * lost.  Before the arm is in force the run ends; once the bench is armed
 * the control task's watch over the arm decides, as it does for ARM.
 */
static void sp_cancel(void)
{
    sp_end_hold();
    stick_t *t = &s.st;
    bool queued = false;
    for (int i = 0; i < t->qn; ++i) {
        queued = queued || t->q[i].kind == MOTOR_CMD_ARM;
    }
    if (esc_stick_running(&t->run)
        && (queued || t->run.phase == ESC_STICK_ARMING)) {
        sp_end_run(ESC_STICK_R_TOUCH);
    }
}

static void sp_track(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    if (!t->warn_down || evt->point.id != t->warn_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* A finger that leaves the button abandons the hold, as on ARM. */
        if (!gfx_rect_contains(t->hold_btn, evt->point.x, evt->point.y)
            && ui_hold_leave(&t->hold)) {
            t->warn_down = false;
            ++s.rev;
        }
        return;
    }
    (void)ui_hold_end(&t->hold);
    t->warn_down = false;
    ++s.rev;
}

static int sp_rows_shown(int total, int scroll)
{
    const int n = total - scroll;
    return (n > ROWS_MAX) ? ROWS_MAX : (n < 0) ? 0 : n;
}

static bool sp_down(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    const int px = evt->point.x, py = evt->point.y;

    /* The warning covers the screen, BACK included. */
    if (t->warn) {
        if (gfx_rect_contains(t->hold_btn, px, py)) {
            t->warn_down = true;
            t->warn_id = evt->point.id;
            ui_hold_begin(&t->hold);
            ++s.rev;
        } else if (gfx_rect_contains(t->cancel_btn, px, py)) {
            t->warn = false;
            sp_end_hold();
            ++s.rev;
        }
        return true;
    }

    if (s.stage == STAGE_PROTOCOL) {
        if (gfx_rect_contains(s.back, px, py)) {
            s.stage = STAGE_CLASS;
            ++s.rev;
            return true;
        }
        if (gfx_rect_contains(t->list_up, px, py) && t->scroll > 0) {
            t->scroll = (t->scroll > SP_ROWS) ? t->scroll - SP_ROWS : 0;
            ++s.rev;
            return true;
        }
        if (gfx_rect_contains(t->list_dn, px, py)
            && t->scroll + SP_ROWS < t->count) {
            t->scroll += SP_ROWS;
            ++s.rev;
            return true;
        }
        for (int i = 0; i < SP_ROWS && t->scroll + i < t->count; ++i) {
            if (!gfx_rect_contains(t->rows[i], px, py)) {
                continue;
            }
            const esc_profile_t *p = esc_profiles_at(
                (size_t)t->order[t->scroll + i]);
            /* A profile the engine cannot run says why on its row and goes
             * no further. */
            if (sp_runs(p)) {
                sp_pick_profile(p);
                s.stage = STAGE_DEVICE;
                ++s.rev;
            }
            return true;
        }
        return true;
    }

    /* The device page.  A run under way takes ABORT and nothing else; STOP
     * is in the band, and leaving the screen aborts. */
    if (esc_stick_running(&t->run)) {
        if (gfx_rect_contains(s.write_btn, px, py)) {
            esc_stick_abort(&t->run, ESC_STICK_R_USER);
            sp_follow();
            ++s.rev;
        }
        return true;
    }
    if (t->shown) {
        const bool back = gfx_rect_contains(s.back, px, py);
        if (back || gfx_rect_contains(s.write_btn, px, py)) {
            t->shown = false;               /* OK: back to the menu */
            if (back) {
                s.stage = STAGE_PROTOCOL;
            }
            ++s.rev;
        }
        return true;
    }
    if (t->timing) {
        if (gfx_rect_contains(s.back, px, py)
            || gfx_rect_contains(s.write_btn, px, py)) {
            t->timing = false;              /* CLOSE */
            if (settings_dirty()) {
                settings_request_save();
            }
            ++s.rev;
            return true;
        }
        if (gfx_rect_contains(s.read_btn, px, py)) {
            settings_reset(SET_CAT_STICK);  /* DEFAULTS */
            ++s.rev;
            return true;
        }
        const int max_scroll = SP_SETTINGS - ROWS_MAX;
        if (gfx_rect_contains(s.page_up, px, py) && t->tscroll > 0) {
            --t->tscroll;
            ++s.rev;
            return true;
        }
        if (gfx_rect_contains(s.page_dn, px, py) && t->tscroll < max_scroll) {
            ++t->tscroll;
            ++s.rev;
            return true;
        }
        for (int i = 0; i < sp_rows_shown(SP_SETTINGS, t->tscroll); ++i) {
            const int idx = t->tscroll + i;
            const int by = gfx_rect_contains(s.down[i], px, py)  ? -1
                         : gfx_rect_contains(s.up[i], px, py)    ?  1 : 0;
            if (by != 0) {
                settings_adjust(k_sp_settings[idx], by);
            }
            if (by != 0 || gfx_rect_contains(s.row[i], px, py)) {
                t->tpicked = idx;
                ++s.rev;
                return true;
            }
        }
        return true;
    }

    if (gfx_rect_contains(s.back, px, py)) {
        s.stage = STAGE_PROTOCOL;
        ++s.rev;
        return true;
    }
    if (gfx_rect_contains(s.connect_btn, px, py)) {
        t->timing = true;                   /* TIMING */
        t->tscroll = 0;
        t->tpicked = 0;
        ++s.rev;
        return true;
    }
    if (gfx_rect_contains(s.write_btn, px, py)) {
        if (sp_can_run()) {
            t->warn = true;                 /* RUN asks first */
            t->warn_down = false;
            ui_hold_reset(&t->hold);
        }
        ++s.rev;
        return true;
    }
    const int items = (int)t->p->item_count;
    const int max_scroll = (items > ROWS_MAX) ? items - ROWS_MAX : 0;
    if (gfx_rect_contains(s.page_up, px, py) && t->iscroll > 0) {
        --t->iscroll;
        ++s.rev;
        return true;
    }
    if (gfx_rect_contains(s.page_dn, px, py) && t->iscroll < max_scroll) {
        ++t->iscroll;
        ++s.rev;
        return true;
    }
    for (int i = 0; i < sp_rows_shown(items, t->iscroll); ++i) {
        const int idx = t->iscroll + i;
        const int by = gfx_rect_contains(s.down[i], px, py)  ? -1
                     : gfx_rect_contains(s.up[i], px, py)    ?  1 : 0;
        if (by != 0 && !esc_stick_is_action(&t->p->items[idx])) {
            /* KEEP, then each value in the profile's order; clamped, not
             * wrapped, as every stepper here is.  Reset and exit are
             * actions, not settings, and are not offered. */
            int v = t->pick[idx] + by;
            const int hi = (int)t->p->items[idx].value_count - 1;
            v = (v < -1) ? -1 : (v > hi) ? hi : v;
            t->pick[idx] = v;
        }
        if (by != 0 || gfx_rect_contains(s.row[i], px, py)) {
            t->picked = idx;
            ++s.rev;
            return true;
        }
    }
    return true;
}

/* ------------------------------------------- the stick class, drawn ----- */

static void sp_draw_list(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    draw_crumb(c, "ESC STICK");
    char count[48];
    const int last = (t->scroll + SP_ROWS < t->count) ? t->scroll + SP_ROWS
                                                      : t->count;
    snprintf(count, sizeof(count), "%d-%d of %d, %d run",
             (t->count > 0) ? t->scroll + 1 : 0, last, t->count,
             t->runnable);
    gfx_text_in(c, (gfx_rect_t){ 360, (int16_t)(CRUMB_Y + 7), 320, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, t->list_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll > 0);
    ui_button(c, t->list_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll + SP_ROWS < t->count);

    for (int i = 0; i < SP_ROWS && t->scroll + i < t->count; ++i) {
        const gfx_rect_t r = t->rows[i];
        const esc_profile_t *p = esc_profiles_at(
            (size_t)t->order[t->scroll + i]);
        char cap[40];
        const char *why = sp_why(p, cap, sizeof(cap));
        const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
        const bool runs = why == NULL;
        ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                           : ui_theme_color(UI_C_PANEL_SUNK));
        const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                     : ui_theme_color(UI_C_TEXT_FAINT);
        sp_text(c, r.x + 12, r.y + 8, p->brand, 13, ink);
        sp_text(c, r.x + 124, r.y + 8, p->family, (r.w - 124 - 236) / 8,
                runs ? ui_theme_color(UI_C_TEXT_DIM)
                     : ui_theme_color(UI_C_TEXT_FAINT));
        char right[48];
        if (runs) {
            snprintf(right, sizeof(right), "%s%u ITEM%s  %s",
                     esc_profiles_is_override(p) ? "CARD  " : "",
                     (unsigned)p->item_count,
                     (p->item_count == 1u) ? "" : "S",
                     (kind == ESC_STICK_KIND_TWO_STAGE) ? "TWO-STAGE"
                                                        : "ONE-STAGE");
        } else {
            snprintf(right, sizeof(right), "%s", why);
        }
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 232),
                                     (int16_t)(r.y + 8), 220, 16 },
                    right, UI_FONT_LABEL,
                    runs ? ui_theme_color(UI_C_ACCENT)
                         : ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_RIGHT);
    }
    gfx_text(c, PAD + 12, SP_ROW_Y0 + SP_ROWS * SP_ROW_H + 6,
             "No profile is verified: no ESC has been recorded.",
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);
}

/* "MIN", "MID" or "MAX" for a percentage the run commands. */
static const char *sp_pos(float pct)
{
    return (pct >= ESC_STICK_PCT_MAX) ? "MAX"
         : (pct >= ESC_STICK_PCT_MID) ? "MID" : "MIN";
}

static const char *sp_reason_help(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_STOP:
        return "STOP was pressed, or the bench stopped itself.";
    case ESC_STICK_R_DISARMED:
        return "The bench disarmed during the run.";
    case ESC_STICK_R_LINK:
        return "The coprocessor stopped answering.";
    case ESC_STICK_R_SUPPLY_OFF:
        return "The supply's output went off: a trip, or a lost ON.";
    case ESC_STICK_R_SUPPLY_LOST:
        return "The supply stopped answering.";
    case ESC_STICK_R_STALE:
        return "No new supply reading for 1000 ms.";
    case ESC_STICK_R_RATE:
        return "3 readings in a row came later than BEEP MIN or GAP MIN.";
    case ESC_STICK_R_NOT_ARMED:
        return "The bench did not arm within 3000 ms.";
    case ESC_STICK_R_NO_POWER:
        return "The supply did not report its output on within 3000 ms.";
    case ESC_STICK_R_NO_BEEPS:
        return "No beep for SILENCE: check the load and THRESHOLD.";
    case ESC_STICK_R_HIGH:
        return "The current stayed over THRESHOLD: a motor, or a low THRESHOLD.";
    case ESC_STICK_R_TIMEOUT:
        return "The wanted beeps were not heard in order within TIMEOUT.";
    case ESC_STICK_R_SUPPLY_ON:
        return "The supply did not report its output off within 3000 ms.";
    case ESC_STICK_R_TOUCH:
        return "Touch events were lost before the arm was taken.";
    case ESC_STICK_R_USER:
        return "ABORT was pressed.";
    case ESC_STICK_R_LEFT:
        return "The screen was left during the run.";
    default:
        return "";
    }
}

/* "3 Cutoff mode -> 2 hard cutoff" for a change. */
static void sp_change_text(const esc_stick_change_t *ch, char *out, size_t n)
{
    const esc_item_t *it = &s.st.p->items[ch->item];
    const esc_value_t *v = &it->values[ch->value];
    snprintf(out, n, "%u %s -> %u %s", (unsigned)it->number, it->name,
             (unsigned)v->number, v->name);
}

static void sp_draw_progress(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    const gfx_color_t txt = ui_theme_color(UI_C_TEXT);
    char line[128];

    gfx_text(c, PAD + 12, PARM_Y + 12, "RUN", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, PAD + 12, PARM_Y + 34, esc_stick_phase_text(e->phase),
             UI_FONT_HEAD, txt, 1);
    snprintf(line, sizeof(line), "%u", esc_stick_beeps(e));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - PAD - 232),
                                 (int16_t)(PARM_Y + 14), 210, 34 },
                line, UI_FONT_NUM, ui_theme_color(UI_C_CURR), 1,
                GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - PAD - 232),
                                 (int16_t)(PARM_Y + 50), 210, 16 },
                "BEEPS IN THIS GROUP", UI_FONT_LABEL,
                ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);

    const int y0 = PARM_Y + 80;
    const int pitch = 22;
    /* The change being made; while items are counted, the first one still
     * to make, since the menu decides which comes up first. */
    uint8_t shown = e->active;
    if (e->phase != ESC_STICK_VALUES && e->phase != ESC_STICK_STORE) {
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                shown = i;
                break;
            }
        }
    }
    char what[96];
    sp_change_text(&e->ch[shown], what, sizeof(what));
    const unsigned done = esc_stick_done_count(e);
    snprintf(line, sizeof(line), "SELECTION %u OF %u   %s",
             (done < e->n) ? done + 1u : (unsigned)e->n, (unsigned)e->n,
             what);
    sp_text(c, PAD + 12, y0, line, 94, txt);

    switch (e->phase) {
    case ESC_STICK_ARMING:
        snprintf(line, sizeof(line), "Arming the bench, throttle at MIN, "
                 "supply off.");
        break;
    case ESC_STICK_SIGNAL:
        snprintf(line, sizeof(line), "Throttle at %s for the entry; the "
                 "supply comes on in %u ms.",
                 sp_pos(e->out.throttle_pct),
                 (unsigned)((e->now_ms - e->phase_ms < ESC_STICK_SIGNAL_MS)
                                ? ESC_STICK_SIGNAL_MS
                                      - (e->now_ms - e->phase_ms)
                                : 0u));
        break;
    case ESC_STICK_POWER:
        snprintf(line, sizeof(line), "Supply switching on at %u.%02u V.",
                 (unsigned)(e->out.supply_mv / 1000u),
                 (unsigned)(e->out.supply_mv % 1000u / 10u));
        break;
    case ESC_STICK_ENTRY: {
        const uint32_t in = e->now_ms - e->on_ms;
        const uint32_t left = (in < e->t.entry_ms) ? e->t.entry_ms - in : 0u;
        snprintf(line, sizeof(line), "Powered at %s; the menu is expected "
                 "in %u.%u s.", sp_pos(e->out.throttle_pct),
                 (unsigned)(left / 1000u), (unsigned)(left % 1000u / 100u));
        break;
    }
    case ESC_STICK_ITEMS:
        line[0] = '\0';
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                snprintf(line, sizeof(line), "Counting item groups; item %u "
                         "is taken with the stick to %s.",
                         (unsigned)e->p->items[e->ch[i].item].number,
                         sp_pos(esc_stick_pct(e->p->select_throttle)));
                break;
            }
        }
        break;
    case ESC_STICK_VALUES: {
        const esc_stick_change_t *ch = &e->ch[e->active];
        snprintf(line, sizeof(line), "Counting value groups; value %u is "
                 "stored with the stick to %s.",
                 (unsigned)e->p->items[ch->item].values[ch->value].number,
                 sp_pos(esc_stick_pct(
                     (e->kind == ESC_STICK_KIND_TWO_STAGE)
                         ? e->p->value_select_throttle
                         : e->p->select_throttle)));
        break;
    }
    case ESC_STICK_STORE:
        if (e->store_moved) {
            snprintf(line, sizeof(line), "Stick to %s, which stores the "
                     "selection; held %u ms.", sp_pos(e->out.throttle_pct),
                     (unsigned)e->t.store_ms);
        } else {
            snprintf(line, sizeof(line), "Held at the selection for %u ms "
                     "while the ESC answers it.", (unsigned)e->t.store_ms);
        }
        break;
    case ESC_STICK_CYCLE:
        snprintf(line, sizeof(line), "Supply off for %u ms before the next "
                 "power-up.", (unsigned)e->t.off_ms);
        break;
    case ESC_STICK_OFF:
        snprintf(line, sizeof(line), "Supply switching off; the stick stays "
                 "at %s until it is.", sp_pos(e->out.throttle_pct));
        break;
    default:
        line[0] = '\0';
        break;
    }
    sp_text(c, PAD + 12, y0 + pitch, line, 94, dim);

    if (e->groups == 0u) {
        snprintf(line, sizeof(line), "LAST GROUP  none yet");
    } else {
        snprintf(line, sizeof(line), "LAST GROUP  %u  %s",
                 (unsigned)e->last_count,
                 !e->last_valid ? "NOT A CLEAN GROUP"
                 : e->last_trusted ? "IN ORDER, TWO IN A ROW"
                 : e->last_in_order ? "IN ORDER" : "NOT IN ORDER");
    }
    gfx_text(c, PAD + 12, y0 + 2 * pitch, line, UI_FONT_LABEL, txt, 1);
    snprintf(line, sizeof(line), "GROUPS %u   POWER-UPS %u   THROTTLE %s"
             "   SUPPLY %s", (unsigned)e->groups, (unsigned)e->entries,
             sp_pos(e->out.throttle_pct), e->out.supply_on ? "ON" : "OFF");
    gfx_text(c, PAD + 12, y0 + 3 * pitch, line, UI_FONT_LABEL, dim, 1);
    snprintf(line, sizeof(line), "CURRENT %d mA   FLOOR %d mA   "
             "READINGS %u ms APART", (int)e->ma,
             (int)esc_det_floor_ma(&e->det), (unsigned)e->iv_ms);
    gfx_text(c, PAD + 12, y0 + 4 * pitch, line, UI_FONT_LABEL, dim, 1);

    gfx_text(c, PAD + 12, HELP_Y + 9,
             "STOP or ABORT ends the run: throttle to MIN, supply off, "
             "disarmed.", UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, s.write_btn, "ABORT", ui_theme_color(UI_C_DANGER), false,
              true);
}

static void sp_draw_result(gfx_canvas_t *c)
{
    const esc_stick_t *e = &s.st.run;
    const bool done = e->phase == ESC_STICK_DONE;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    char line[128];
    gfx_text(c, PAD + 12, PARM_Y + 12, "RESULT", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    snprintf(line, sizeof(line), "%s", done ? "DONE" : "ABORTED");
    if (!done) {
        snprintf(line, sizeof(line), "ABORTED: %s",
                 esc_stick_reason_text(e->reason));
    }
    gfx_text(c, PAD + 12, PARM_Y + 34, line, UI_FONT_HEAD,
             done ? ui_theme_color(UI_C_OK) : ui_theme_color(UI_C_DANGER),
             1);
    const int y0 = PARM_Y + 80;
    const int pitch = 22;
    snprintf(line, sizeof(line), "%u of %u selections made.",
             esc_stick_done_count(e), (unsigned)e->n);
    gfx_text(c, PAD + 12, y0, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    /* Five lines fit; past five, the fifth says how many more. */
    const unsigned shown = (e->n > 5u) ? 4u : e->n;
    for (unsigned i = 0; i < shown; ++i) {
        char what[96];
        sp_change_text(&e->ch[i], what, sizeof(what));
        snprintf(line, sizeof(line), "%s  %s", e->done[i] ? "MADE" : "NOT MADE",
                 what);
        sp_text(c, PAD + 12, y0 + (int)(i + 1u) * pitch, line, 94,
                e->done[i] ? ui_theme_color(UI_C_TEXT) : dim);
    }
    if (shown < e->n) {
        unsigned made = 0u;
        for (unsigned i = shown; i < e->n; ++i) {
            made += e->done[i] ? 1u : 0u;
        }
        snprintf(line, sizeof(line), "AND %u MORE, %u OF THEM MADE",
                 (unsigned)(e->n - shown), made);
        gfx_text(c, PAD + 12, y0 + 5 * pitch, line, UI_FONT_LABEL, dim, 1);
    }
    if (done) {
        gfx_text(c, PAD + 12, HELP_Y,
                 "The ESC's own tones after a selection are not checked:",
                 UI_FONT_LABEL, dim, 1);
        gfx_text(c, PAD + 12, HELP_Y + 18,
                 "listen for them, or read the ESC back with its program "
                 "card.", UI_FONT_LABEL, dim, 1);
    } else {
        gfx_text(c, PAD + 12, HELP_Y, sp_reason_help(e->reason),
                 UI_FONT_LABEL, dim, 1);
        gfx_text(c, PAD + 12, HELP_Y + 18,
                 "Throttle at MIN, supply off, bench disarmed.",
                 UI_FONT_LABEL, dim, 1);
    }
    ui_button(c, s.write_btn, "OK", ui_theme_color(UI_C_PANEL_HI), false,
              true);
}

static void sp_setting_text(setting_id_t id, char *out, size_t n)
{
    if (id == SET_STICK_V && !(settings_get(id) > 0.0f)) {
        snprintf(out, n, "FROM PROFILE");
        return;
    }
    char v[24];
    settings_value_text(id, v, sizeof(v));
    snprintf(out, n, "%s %s", v, settings_def(id)->unit);
}

static void sp_draw_timing(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD),
                             (int16_t)(PARM_Y + PARM_H - DEV_Y) },
            ui_theme_color(UI_C_PANEL));
    gfx_text(c, PAD + 12, DEV_Y + 12, "TIMING AND DETECTION", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, PAD + 12, DEV_Y + 34,
             "No ESC has been recorded: every value is a default, not a "
             "measurement.", UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    const int max_scroll = SP_SETTINGS - ROWS_MAX;
    ui_button(c, s.page_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->tscroll > 0);
    ui_button(c, s.page_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->tscroll < max_scroll);
    for (int i = 0; i < sp_rows_shown(SP_SETTINGS, t->tscroll); ++i) {
        const int idx = t->tscroll + i;
        const setting_id_t id = k_sp_settings[idx];
        const setting_def_t *d = settings_def(id);
        const int y = ROW_Y0 + i * ROW_H;
        if (idx == t->tpicked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        gfx_text(c, PAD + 12, y, d->label, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        char v[40];
        sp_setting_text(id, v, sizeof(v));
        const bool moved = settings_get(id) != d->def;
        widget_enum(c, y, v, moved ? ui_theme_color(UI_C_WARN)
                                   : ui_theme_color(UI_C_ACCENT));
        const float now = settings_get(id);
        ui_button(c, s.down[i], "-", ui_theme_color(UI_C_PANEL_HI), false,
                  now > d->min);
        ui_button(c, s.up[i], "+", ui_theme_color(UI_C_PANEL_HI), false,
                  now < d->max);
    }
    const setting_def_t *d = settings_def(k_sp_settings[t->tpicked]);
    gfx_text(c, PAD + 12, HELP_Y, d->label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, PAD + 12, HELP_Y + 18, d->help, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, s.read_btn, "DEFAULTS", ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, s.write_btn, "CLOSE", ui_theme_color(UI_C_PANEL_HI), false,
              true);
}

static void sp_draw_items(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    const int items = (int)p->item_count;
    gfx_text(c, PAD + 12, PARM_Y + 12, "CHANGE", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    const int max_scroll = (items > ROWS_MAX) ? items - ROWS_MAX : 0;
    char count[40];
    snprintf(count, sizeof(count), "%d-%d of %d", t->iscroll + 1,
             t->iscroll + sp_rows_shown(items, t->iscroll), items);
    gfx_text_in(c, (gfx_rect_t){ 420, (int16_t)(PARM_Y + 12), 260, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, s.page_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->iscroll > 0);
    ui_button(c, s.page_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->iscroll < max_scroll);

    for (int i = 0; i < sp_rows_shown(items, t->iscroll); ++i) {
        const int idx = t->iscroll + i;
        const esc_item_t *it = &p->items[idx];
        const int pick = t->pick[idx];
        const int y = ROW_Y0 + i * ROW_H;
        if (idx == t->picked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)it->number);
        gfx_text(c, PAD + 12, y, num, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
        if (pick >= 0) {
            gfx_fill_round_rect(c, PAD + 38, y + 2, 3, 12, 1,
                                ui_theme_color(UI_C_WARN));
        }
        sp_text(c, PAD + 48, y, it->name, (CTRL_X - PAD - 56) / 8,
                ui_theme_color(UI_C_TEXT));
        const bool action = esc_stick_is_action(it);
        char v[64];
        if (action) {
            snprintf(v, sizeof(v), "ACTION, NOT SET");
        } else if (pick < 0) {
            snprintf(v, sizeof(v), "KEEP");
        } else {
            snprintf(v, sizeof(v), "%u %s",
                     (unsigned)it->values[pick].number,
                     it->values[pick].name);
        }
        if ((int)strlen(v) > CTRL_W / 8) {
            v[CTRL_W / 8 - 2] = '.';
            v[CTRL_W / 8 - 1] = '.';
            v[CTRL_W / 8] = '\0';
        }
        widget_enum(c, y, v, (pick >= 0) ? ui_theme_color(UI_C_WARN)
                                         : ui_theme_color(UI_C_TEXT_FAINT));
        ui_button(c, s.down[i], "-", ui_theme_color(UI_C_PANEL_HI), false,
                  !action && pick > -1);
        ui_button(c, s.up[i], "+", ui_theme_color(UI_C_PANEL_HI), false,
                  !action && pick < (int)it->value_count - 1);
    }

    /* The picked item: its values, and where it applies. */
    const esc_item_t *it = &p->items[t->picked];
    char line[160];
    if (it->applies_when[0] != '\0') {
        snprintf(line, sizeof(line), "%u %s, only when %s",
                 (unsigned)it->number, it->name, it->applies_when);
    } else if (it->applies_count > 0u) {
        snprintf(line, sizeof(line), "%u %s, only on %u of the models",
                 (unsigned)it->number, it->name,
                 (unsigned)it->applies_count);
    } else {
        snprintf(line, sizeof(line), "%u %s", (unsigned)it->number,
                 it->name);
    }
    sp_text(c, PAD + 12, HELP_Y, line, 94, ui_theme_color(UI_C_TEXT_DIM));
    size_t at = 0;
    line[0] = '\0';
    for (unsigned k = 0; k < it->value_count && at + 1u < sizeof(line); ++k) {
        const int w = snprintf(line + at, sizeof(line) - at, "%s%u %s%s",
                               (k > 0u) ? "   " : "",
                               (unsigned)it->values[k].number,
                               it->values[k].name,
                               it->values[k].is_default ? " (default)" : "");
        if (w < 0) {
            break;
        }
        at += (size_t)w;
    }
    sp_text(c, PAD + 12, HELP_Y + 18, line, 94,
            ui_theme_color(UI_C_TEXT_FAINT));

    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    const bool can = sp_can_run();
    if (t->note[0] != '\0') {
        sp_text(c, PAD + 12, BTN_Y + 9, t->note, 56,
                ui_theme_color(UI_C_WARN));
    } else {
        sp_text(c, PAD + 12, BTN_Y + 9,
                (picked == 0u) ? "Pick a value to change; KEEP leaves it."
                               : "The profile is unverified.",
                56, ui_theme_color(UI_C_TEXT_FAINT));
    }
    char run[24];
    snprintf(run, sizeof(run), (picked > 0u) ? "RUN %u" : "RUN",
             (unsigned)picked);
    ui_button(c, s.write_btn, run,
              can ? ui_theme_color(UI_C_DANGER)
                  : ui_theme_color(UI_C_PANEL_HI), false, can);
}

static void sp_draw_device(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    char line[128];
    snprintf(line, sizeof(line), "ESC STICK  >  %s %s", p->brand,
             p->family);
    ui_button(c, s.back, "BACK", ui_theme_color(UI_C_PANEL_HI), false,
              !esc_stick_running(&t->run));
    sp_text(c, s.back.x + s.back.w + 16, CRUMB_Y + 8, line, 72,
            ui_theme_color(UI_C_TEXT_DIM));
    if (t->timing) {
        sp_draw_timing(c);
        return;
    }
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD), DEV_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, PARM_Y, (int16_t)(W - 2 * PAD), PARM_H },
            ui_theme_color(UI_C_PANEL));

    const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
    snprintf(line, sizeof(line), "%s   %s   %s",
             (kind == ESC_STICK_KIND_TWO_STAGE) ? "TWO-STAGE MENU"
                                                : "ONE-STAGE MENU",
             (p->encoding == ESC_ENC_SHORT_LONG) ? "SHORT AND LONG BEEPS"
                                                 : "COUNTED BEEPS",
             p->one_change_per_entry ? "ONE CHANGE A POWER-UP"
                                     : "MANY CHANGES A POWER-UP");
    gfx_text(c, PAD + 12, DEV_Y + 4, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    esc_stick_timing_t tm;
    sp_timing(&tm);
    if (esc_stick_running(&t->run) || t->shown) {
        mv = t->run.out.supply_mv;
        ma = t->run.out.supply_ma;
        tm.entry_ms = t->run.t.entry_ms;
    }
    snprintf(line, sizeof(line), "SUPPLY %u.%02u V  %u.%02u A   ENTRY "
             "%u.%u s%s", (unsigned)(mv / 1000u),
             (unsigned)(mv % 1000u / 10u), (unsigned)(ma / 1000u),
             (unsigned)(ma % 1000u / 10u), (unsigned)(tm.entry_ms / 1000u),
             (unsigned)(tm.entry_ms % 1000u / 100u),
             (p->entry_hold_ms != 0u) ? " (profile)" : " (setting)");
    gfx_text(c, PAD + 12, DEV_Y + 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, s.connect_btn, "TIMING", ui_theme_color(UI_C_PANEL_HI),
              false, !esc_stick_running(&t->run) && !t->shown);

    if (esc_stick_running(&t->run)) {
        sp_draw_progress(c);
    } else if (t->shown) {
        sp_draw_result(c);
    } else {
        sp_draw_items(c);
    }
}

/*
 * The warning a run needs before it starts: in the danger colour, over the
 * whole screen, saying what the run does to an ESC with a motor on it.
 */
static void sp_draw_warning(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_rect_t a = { PAD, PAD, (int16_t)(W - 2 * PAD),
                           (int16_t)(H - 2 * PAD) };
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_fill_rect(c, a.x, a.y, a.w, 52, red);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text_in(c, (gfx_rect_t){ a.x, (int16_t)(a.y + 12), a.w, 28 },
                "MOTOR REMOVED, LOAD FITTED?", UI_FONT_HEAD,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    static const char *const k_lines[] = {
        "The run powers this ESC from the supply and moves its throttle",
        "to MAX and back. A motor still connected starts at full throttle.",
        "Disconnect the motor and fit a resistor load in its place: the",
        "beeps are counted as current through it.",
    };
    for (int i = 0; i < 4; ++i) {
        gfx_text(c, a.x + 20, a.y + 68 + i * 22, k_lines[i], UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
    }
    char line[128];
    snprintf(line, sizeof(line), "%s %s", t->p->brand, t->p->family);
    sp_text(c, a.x + 20, a.y + 68 + 5 * 22, line, 90,
            ui_theme_color(UI_C_ACCENT));
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    snprintf(line, sizeof(line), "SUPPLY %u.%02u V  %u.%02u A   %u CHANGE%s",
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 10u),
             (unsigned)(ma / 1000u), (unsigned)(ma % 1000u / 10u),
             (unsigned)picked, (picked == 1u) ? "" : "S");
    gfx_text(c, a.x + 20, a.y + 68 + 6 * 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_VOLT), 1);
    gfx_text(c, a.x + 20, a.y + 68 + 7 * 22,
             "The profile is unverified and every beep time is a default.",
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_button(c, t->hold_btn, "HOLD TO RUN",
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), red,
                           t->hold.held_s),
              t->warn_down, true);
    ui_button(c, t->cancel_btn, "CANCEL", ui_theme_color(UI_C_PANEL_SUNK),
              false, true);
}

static void sp_render(gfx_canvas_t *c)
{
    if (s.st.warn && s.st.p != NULL) {
        sp_draw_warning(c);
        return;
    }
    if (s.stage == STAGE_PROTOCOL || s.st.p == NULL) {
        sp_draw_list(c);
        return;
    }
    sp_draw_device(c);
}

/* ------------------------------------------- the application's side ----- */

void programmer_screen_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             bool link_up)
{
    stick_t *t = &s.st;
    /* A stop ends a hold under way; the warning stays, and a new hold is
     * a new decision.  It ends a run too, with any ARM it queued and the
     * application has not taken: that one would be stamped with this
     * frame's count and clear the stop. */
    if (stops != t->stops) {
        sp_end_hold();
        sp_end_run(ESC_STICK_R_STOP);
    }
    t->now_ms = now_ms;
    t->armed = armed;
    t->stops = stops;
    t->link_up = link_up;
}

void programmer_screen_supply(const supply_state_t *st)
{
    stick_t *t = &s.st;
    if (st == NULL || !esc_stick_running(&t->run)) {
        return;
    }
    const esc_stick_sample_t x = {
        .seq = st->samples,
        .at_ms = st->taken_ms,
        .ma = (int32_t)lroundf(st->i * 1000.0f),
        .current_ok = (st->ok & SUPPLY_OK_CURRENT) != 0u,
        /* On as the supply reports it, not as it was asked: the PD mini's
         * output comes on a link exchange and a module read after the ON,
         * and readings from before it would set the floor at 0 A. */
        .output = st->output && st->mode != SUPPLY_MODE_OFF,
        .online = st->online,
    };
    esc_stick_sample(&t->run, &x);
    sp_follow();        /* a reading can end the run */
}

bool programmer_screen_poll_cmd(motor_cmd_t *out)
{
    stick_t *t = &s.st;
    if (t->qn == 0 || out == NULL) {
        return false;
    }
    *out = t->q[0];
    memmove(&t->q[0], &t->q[1], (size_t)(t->qn - 1) * sizeof(t->q[0]));
    t->qn--;
    return true;
}

const esc_profile_t *programmer_screen_stick_profile(void)
{
    return esc_stick_running(&s.st.run) ? s.st.run.p : NULL;
}

uint32_t programmer_screen_stick_runs(void) { return s.st.runs; }

const esc_stick_t *programmer_screen_stick(void) { return &s.st.run; }

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
    if (s.klass == CLASS_STICK) {
        sp_render(c);
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
    .leave  = sp_leave,
    .tick   = sp_tick,
    .event  = event,
    .cancel = sp_cancel,
    .render = render,
};

const ui_screen_t *programmer_screen(void) { return &k_screen; }
