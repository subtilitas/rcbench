/*
 * The programmer screen: one renderer for several protocols.
 *
 * BLHeli_S and AM32 speak a one-wire bootloader at 19,200 baud; ESCape32
 * answers a text CLI (command-line interface); VESC uses framed packets at
 * 115,200 baud; a Hitec D-series servo has its own protocol.  They share a
 * connector and nothing else, so the screen asks in order: the device class,
 * the protocol, and only then what answered.
 *
 * The ESC and SERVO classes are a model.  No transport carries their
 * protocols: CONNECT, READ and WRITE change the screen's own copy of a
 * device and nothing goes on a wire.  Every page of the two classes carries
 * the MODELLED mark the overview's tiles use, the device page says that no
 * device is reached, and the device line never takes the OK colour, which
 * on every other screen means hardware answered.
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
 * through its throttle-stick menu.  Its pages are esc_stick_panel.c's; the
 * two files share programmer_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "programmer_screen.h"

#include <stdio.h>
#include <string.h>

#include "esc_profile.h"
#include "programmer_screen_int.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* The layout both files share is in programmer_screen_int.h. */

/* A parameter's help line: 96 cells from PAD + 12, inside the card's
 * edge. */
#define HELP_CELLS 96

/* The steppers, right of the control column. */
#define STEP_DN_X 636
#define STEP_UP_X 748

#define MAX_PARAMS 10

/* ------------------------------------------------------------- the model */

typedef enum {
    PARAM_ENUM = 0,   /* one of a named few */
    PARAM_BOOL,       /* on or off */
    PARAM_NUMBER,     /* a bounded number, with a unit */
} param_kind_t;

/* The names and the choices are the firmware's own and stay English in
 * every language, as its configurators show them; the group and the help
 * line are translated. */
typedef struct {
    ui_text_id_t group;        /* TX_COUNT continues the one above */
    const char  *name;
    param_kind_t kind;
    ui_text_id_t help;         /* what it does, in one line */

    const char *const *choices;/* ENUM */
    int          count;

    int          lo, hi, step; /* NUMBER, in tenths if decimals is 1 */
    int          decimals;
    const char  *unit;

    int          initial;
} param_def_t;

typedef struct {
    const char  *name;
    ui_text_id_t transport;
    ui_text_id_t device;
    const param_def_t *params;
    int         count;
    int         klass;
} proto_t;

typedef struct {
    const char  *name;         /* the class's name: English always */
    ui_text_id_t blurb;
} class_t;

static const class_t k_classes[CLASS_COUNT] = {
    { "ESC",       TX_PG_BLURB_ESC },
    { "SERVO",     TX_PG_BLURB_SERVO },
    { "ESC STICK", TX_PG_BLURB_STICK },
};

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
  { TX_PG_GROUP_MOTOR, "Motor direction", PARAM_ENUM,
    TX_PG_HELP_DIRECTION,
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Timing", PARAM_ENUM,
    TX_PG_HELP_TIMING_STEPS,
    k_timing, 5, 0,0,0,0, NULL, 2 },
  { TX_COUNT, "PWM frequency", PARAM_ENUM,
    TX_PG_HELP_PWM,
    k_pwm, 3, 0,0,0,0, NULL, 0 },
  { TX_PG_GROUP_STARTUP, "Startup power", PARAM_NUMBER,
    TX_PG_HELP_STARTUP,
    NULL, 0, 25, 150, 25, 0, "%", 100 },
  { TX_COUNT, "Demag compensation", PARAM_ENUM,
    TX_PG_HELP_DEMAG,
    k_demag, 3, 0,0,0,0, NULL, 1 },
  { TX_PG_GROUP_PROTECTION, "Brake on stop", PARAM_BOOL,
    TX_PG_HELP_BRAKE,
    NULL, 0, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Low voltage cut", PARAM_NUMBER,
    TX_PG_HELP_LVC,
    NULL, 0, 28, 38, 1, 1, "V", 35 },
  { TX_PG_GROUP_SOUND, "Beep volume", PARAM_NUMBER,
    TX_PG_HELP_BEEP,
    NULL, 0, 0, 100, 25, 0, "%", 50 },
};

static const param_def_t k_am32[] = {
  { TX_PG_GROUP_MOTOR, "Motor direction", PARAM_ENUM,
    TX_PG_HELP_DIRECTION,
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Timing advance", PARAM_NUMBER,
    TX_PG_HELP_TIMING_DEG,
    NULL, 0, 0, 30, 1, 0, "deg", 22 },
  { TX_COUNT, "PWM frequency", PARAM_ENUM,
    TX_PG_HELP_PWM,
    k_pwm, 3, 0,0,0,0, NULL, 1 },
  { TX_PG_GROUP_STARTUP, "Sinusoidal startup", PARAM_BOOL,
    TX_PG_HELP_SINE,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Startup power", PARAM_NUMBER,
    TX_PG_HELP_STARTUP,
    NULL, 0, 25, 150, 25, 0, "%", 75 },
  { TX_PG_GROUP_PROTECTION, "Complementary PWM", PARAM_BOOL,
    TX_PG_HELP_COMPLEMENTARY,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Low voltage cut", PARAM_NUMBER,
    TX_PG_HELP_LVC,
    NULL, 0, 28, 38, 1, 1, "V", 33 },
};

static const param_def_t k_escape32[] = {
  { TX_PG_GROUP_MOTOR, "Motor direction", PARAM_ENUM,
    TX_PG_HELP_DIRECTION,
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Timing", PARAM_NUMBER,
    TX_PG_HELP_TIMING_DEG,
    NULL, 0, 0, 30, 1, 0, "deg", 18 },
  { TX_COUNT, "PWM frequency", PARAM_ENUM,
    TX_PG_HELP_PWM,
    k_pwm, 3, 0,0,0,0, NULL, 2 },
  { TX_PG_GROUP_STARTUP, "Sine startup", PARAM_BOOL,
    TX_PG_HELP_SINE,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_PG_GROUP_PROTECTION, "Brake on stop", PARAM_BOOL,
    TX_PG_HELP_BRAKE,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Telemetry", PARAM_BOOL,
    TX_PG_HELP_TELEMETRY,
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const param_def_t k_vesc[] = {
  { TX_PG_GROUP_MOTOR, "Motor type", PARAM_ENUM,
    TX_PG_HELP_MOTOR_TYPE,
    k_mtype, 2, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Current limit", PARAM_NUMBER,
    TX_PG_HELP_CURRENT,
    NULL, 0, 10, 100, 10, 0, "A", 40 },
  { TX_PG_GROUP_PROTECTION, "Regen braking", PARAM_BOOL,
    TX_PG_HELP_REGEN,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Low voltage cut", PARAM_NUMBER,
    TX_PG_HELP_LVC,
    NULL, 0, 28, 38, 1, 1, "V", 34 },
  { TX_PG_GROUP_REPORTING, "Telemetry", PARAM_BOOL,
    TX_PG_HELP_TELEMETRY,
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const param_def_t k_hitec[] = {
  { TX_PG_GROUP_TRAVEL, "Centre", PARAM_NUMBER,
    TX_PG_HELP_CENTRE,
    NULL, 0, -50, 50, 5, 0, "us", 0 },
  { TX_COUNT, "Endpoint travel", PARAM_NUMBER,
    TX_PG_HELP_ENDPOINT,
    NULL, 0, 50, 150, 10, 0, "%", 100 },
  { TX_COUNT, "Direction", PARAM_ENUM,
    TX_PG_HELP_HORN,
    k_dir, 2, 0,0,0,0, NULL, 0 },
  { TX_PG_GROUP_RESPONSE, "Speed", PARAM_NUMBER,
    TX_PG_HELP_SPEED,
    NULL, 0, 20, 100, 10, 0, "%", 100 },
  { TX_COUNT, "Dead band", PARAM_NUMBER,
    TX_PG_HELP_DEADBAND,
    NULL, 0, 1, 10, 1, 0, "us", 2 },
  { TX_COUNT, "Resolution", PARAM_ENUM,
    TX_PG_HELP_RESOLUTION,
    k_res, 2, 0,0,0,0, NULL, 0 },
  { TX_PG_GROUP_PROTECTION, "Overload protect", PARAM_BOOL,
    TX_PG_HELP_OVERLOAD,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Fail-safe", PARAM_BOOL,
    TX_PG_HELP_FAILSAFE,
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const proto_t k_protos[] = {
    { "BLHeli_S", TX_PG_TRANSPORT_ONEWIRE, TX_PG_DEVICE_BLHELI,
      k_blheli,   8, CLASS_ESC },
    { "AM32",     TX_PG_TRANSPORT_ONEWIRE, TX_PG_DEVICE_AM32,
      k_am32,     7, CLASS_ESC },
    { "ESCape32", TX_PG_TRANSPORT_CLI,     TX_PG_DEVICE_ESCAPE32,
      k_escape32, 6, CLASS_ESC },
    { "VESC",     TX_PG_TRANSPORT_PACKETS, TX_PG_DEVICE_VESC,
      k_vesc,     5, CLASS_ESC },
    /* A maker's servos and no firmware name: the row is named in the
     * panel's language, by proto_name().  The device line names the
     * maker and the series, no model. */
    { NULL,       TX_PG_TRANSPORT_HITEC,   TX_PG_DEVICE_HITEC,
      k_hitec,    8, CLASS_SERVO },
};
#define PROTO_COUNT ((int)(sizeof(k_protos) / sizeof(k_protos[0])))

/* --------------------------------------------------------------- the state */

/* The part esc_stick_panel.c reads and writes as well. */
programmer_shared_t pg;

static struct {
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
    gfx_rect_t connect_btn;

    uint32_t drawn[2];
    unsigned drawn_mask;
} s;

static const proto_t *proto(void) { return &k_protos[s.proto]; }

/* The name on the protocol's row and in the crumb.  A firmware's name is
 * the same in every language.  The servo row has none: it says whose
 * servos the protocol is for, 42 cells of the heading face at most (the
 * row's 684 px from its text to the chevron). */
static const char *proto_name(const proto_t *p)
{
    return (p->name != NULL) ? p->name : TR(PG_NAME_HITEC);
}

/* The device line the protocol answers with: a firmware and its
 * processor, or for the servo protocol the maker and the series.  80 cells
 * of the label face at most. */
static const char *device_text(const proto_t *p)
{
    return ui_tr(p->device);
}

/* The ESC and SERVO classes: a model with no transport (see the top). */
static bool modelled(void) { return pg.klass != CLASS_STICK; }

/* The MODELLED mark: the overview's pill, 88 px or as wide as a longer
 * word, 20 px high. */
#define MARK_H 20

static int mark_w(void)
{
    const int w = 24 + gfx_text_width(UI_FONT_LABEL, TR(PG_MODELLED), 1);
    return (w < 88) ? 88 : w;
}

static void draw_mark(gfx_canvas_t *c, int x, int y, gfx_color_t dot)
{
    ui_pill(c, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)mark_w(),
                             MARK_H },
            TR(PG_MODELLED), dot, ui_theme_color(UI_C_PANEL_SUNK));
}

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
    memset(&pg, 0, sizeof(pg));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    pg.stage   = STAGE_CLASS;

    const int tw = (W - 2 * PAD - 24 - 16 * (CLASS_COUNT - 1)) / CLASS_COUNT;
    for (int i = 0; i < CLASS_COUNT; ++i) {
        s.tile[i] = (gfx_rect_t){ (int16_t)(PAD + 12 + i * (tw + 16)),
                                  90, (int16_t)tw, 180 };
    }
    pg.back       = (gfx_rect_t){ (int16_t)(PAD + 12), CRUMB_Y, 96, 30 };
    s.connect_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                  (int16_t)(DEV_Y + 2), 168, 36 };
    pg.read_btn   = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 350),
                                  (int16_t)BTN_Y, 168, 34 };
    pg.write_btn  = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                  (int16_t)BTN_Y, 168, 34 };
    pg.page_up    = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76),
                                  (int16_t)(PARM_Y + 6), STEP_W, 24 };
    pg.page_dn    = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38),
                                  (int16_t)(PARM_Y + 6), STEP_W, 24 };
    for (int i = 0; i < ROWS_MAX; ++i) {
        const int y = ROW_Y0 + i * ROW_H;
        pg.row[i]  = (gfx_rect_t){ (int16_t)(PAD + 8), (int16_t)(y - 4),
                                   (int16_t)(CTRL_X - PAD - 16), ROW_H - 2 };
        pg.down[i] = (gfx_rect_t){ STEP_DN_X, (int16_t)(y - 2), STEP_W, 24 };
        pg.up[i]   = (gfx_rect_t){ STEP_UP_X, (int16_t)(y - 2), STEP_W, 24 };
    }
    esc_stick_panel_reset();
    adopt_device();
}

int  programmer_screen_protocol(void)  { return s.proto; }
bool programmer_screen_connected(void) { return s.connected; }

const char *programmer_screen_mark(void)
{
    return (pg.stage != STAGE_CLASS && modelled()) ? TR(PG_MODELLED) : NULL;
}

const char *programmer_screen_note(void)
{
    return (pg.stage == STAGE_DEVICE && modelled()) ? TR(PG_MODEL_NOTE)
                                                   : NULL;
}

const char *programmer_screen_device_line(void)
{
    if (pg.stage != STAGE_DEVICE || !modelled()) {
        return NULL;
    }
    return s.connected ? device_text(proto()) : TR(PG_NOTHING_ANSWERED);
}

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
        ++pg.rev;
    }
}

/* The tap walk, hit() and the press it arms are programmer_screen_int.h's;
 * the stick class's half of the events is esc_stick_panel.c's. */

/* What decides which control lies under a point.  A press and its release
 * with a different answer are not a tap: the control has changed under the
 * finger. */
static uint32_t showing(void)
{
    const uint32_t parts[] = {
        (uint32_t)pg.stage, (uint32_t)pg.klass, (uint32_t)s.proto,
        s.connected ? 1u : 0u, (uint32_t)s.scroll,
    };
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        h = (h ^ parts[i]) * 16777619u;
    }
    /* The stick class's part, hashed on in the same walk. */
    return esc_stick_panel_showing(h);
}

static void press_at(const touch_event_t *evt);

/* The rest of the press that may become a tap: its moves and its release. */
static void tap_follow(const touch_event_t *evt)
{
    if (!pg.tap.live || evt->point.id != pg.tap.id) {
        return;
    }
    int dx = evt->point.x - pg.tap.x, dy = evt->point.y - pg.tap.y;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    const bool on = dx <= TAP_SLOP && dy <= TAP_SLOP
                    && gfx_rect_contains(pg.tap.rect, evt->point.x,
                                         evt->point.y);
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (!on) {
            pg.tap.live = false;     /* moved off: the press ends */
        }
        return;
    }
    pg.tap.live = false;
    if (!on || !touch_event_is_tap_up(evt) || showing() != pg.tap.showing) {
        return;
    }
    touch_event_t press = *evt;
    press.type    = TOUCH_EVENT_DOWN;
    press.point.x = pg.tap.x;
    press.point.y = pg.tap.y;
    pg.pass = PASS_TAP;
    press_at(&press);
    pg.pass = PASS_PRESS;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (evt->type != TOUCH_EVENT_DOWN) {
        /* The search's keys, the warning's hold and a press that may
         * become a tap follow a finger. */
        esc_stick_panel_follow(evt);
        tap_follow(evt);
        return;
    }
    /* A DOWN with the id of the press held says that contact has gone and
     * its release with it. */
    if (pg.tap.live && evt->point.id == pg.tap.id) {
        pg.tap.live = false;
    }
    pg.pass  = PASS_PRESS;
    pg.found = false;
    press_at(evt);
    /* One press at a time: a second contact arms nothing.  The controls
     * that act on the press have acted above, whichever contact it is. */
    if (pg.found && !pg.tap.live) {
        pg.tap.live    = true;
        pg.tap.id      = evt->point.id;
        pg.tap.x       = evt->point.x;
        pg.tap.y       = evt->point.y;
        pg.tap.rect    = pg.found_rect;
        pg.tap.showing = showing();
    }
}

/* The controls under the point of @p evt, in the order they cover each
 * other; see hit(). */
static void press_at(const touch_event_t *evt)
{
    const int px = evt->point.x, py = evt->point.y;

    if (pg.klass == CLASS_STICK && pg.stage != STAGE_CLASS
        && esc_stick_panel_down(evt)) {
        return;
    }

    /* Back climbs one level; the band's home tag leaves the screen. */
    if (pg.stage != STAGE_CLASS && hit(pg.back, px, py)) {
        pg.stage = (pg.stage == STAGE_DEVICE) ? STAGE_PROTOCOL : STAGE_CLASS;
        if (pg.stage == STAGE_PROTOCOL) {
            s.connected = false;
        }
        ++pg.rev;
        return;
    }

    if (pg.stage == STAGE_CLASS) {
        for (int i = 0; i < CLASS_COUNT; ++i) {
            if (hit(s.tile[i], px, py)) {
                pg.klass = i;
                pg.stage = STAGE_PROTOCOL;
                if (i == CLASS_STICK) {
                    esc_stick_panel_build_list(false);
                }
                ++pg.rev;
                return;
            }
        }
        return;
    }

    if (pg.stage == STAGE_PROTOCOL) {
        for (int i = 0; i < PROTO_COUNT; ++i) {
            if (k_protos[i].klass == pg.klass
                && hit(proto_row(i), px, py)) {
                s.proto = i;
                s.connected = false;
                adopt_device();
                pg.stage = STAGE_DEVICE;
                ++pg.rev;
                return;
            }
        }
        return;
    }

    if (hit(s.connect_btn, px, py)) {
        s.connected = !s.connected;
        if (s.connected) {
            adopt_device();
        }
        ++pg.rev;
        return;
    }
    if (!s.connected) {
        return;
    }

    /* READ takes what the device says and discards staged edits. */
    if (hit(pg.read_btn, px, py)) {
        for (int i = 0; i < proto()->count; ++i) {
            s.value[i] = s.device[i];
        }
        ++pg.rev;
        return;
    }
    /* And writing makes the device agree with the screen. */
    if (hit(pg.write_btn, px, py)) {
        for (int i = 0; i < proto()->count; ++i) {
            s.device[i] = s.value[i];
        }
        ++pg.rev;
        return;
    }

    const int max_scroll = (proto()->count > ROWS_MAX)
                               ? proto()->count - ROWS_MAX : 0;
    if (hit(pg.page_up, px, py)) {
        if (s.scroll > 0) { --s.scroll; ++pg.rev; }
        return;
    }
    if (hit(pg.page_dn, px, py)) {
        if (s.scroll < max_scroll) { ++s.scroll; ++pg.rev; }
        return;
    }
    for (int i = 0; i < rows_shown(); ++i) {
        if (hit(pg.down[i], px, py)) { step(i, -1); return; }
        if (hit(pg.up[i],   px, py)) { step(i, +1); return; }
        /* Touching the name asks what it does. */
        if (hit(pg.row[i], px, py)) {
            s.picked = s.scroll + i;
            ++pg.rev;
            return;
        }
    }
}

/* ----------------------------------------------------------------- drawing */

static void fmt_value(const param_def_t *d, int v, char *out, size_t n)
{
    switch (d->kind) {
    case PARAM_BOOL:
        snprintf(out, n, "%s", ui_on_off(v));
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

/*
 * A class's line in @p r, centred: one line in the middle of it, or two
 * 18 px apart about the middle where it needs more than the width.
 */
static void draw_blurb(gfx_canvas_t *c, gfx_rect_t r, const char *text)
{
    const gfx_color_t ink = ui_theme_color(UI_C_TEXT_DIM);
    const int cells = r.w / 8;
    const size_t len = sp_line_len(text, cells);
    if (text[len] == '\0') {
        gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + r.h / 2 - 8), r.w,
                                     16 },
                    text, UI_FONT_LABEL, ink, 1, GFX_ALIGN_CENTER);
        return;
    }
    char first[64];
    const size_t n = (len < sizeof(first)) ? len : sizeof(first) - 1u;
    memcpy(first, text, n);
    first[n] = '\0';
    const char *rest = text + len;
    while (*rest == ' ') {
        ++rest;
    }
    gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + r.h / 2 - 17), r.w,
                                 16 },
                first, UI_FONT_LABEL, ink, 1, GFX_ALIGN_CENTER);
    gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + r.h / 2 + 1), r.w, 16 },
                rest, UI_FONT_LABEL, ink, 1, GFX_ALIGN_CENTER);
}

static DRAWER void draw_classes(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, 24, TR(PG_WHAT), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    for (int i = 0; i < CLASS_COUNT; ++i) {
        const gfx_rect_t r = s.tile[i];
        ui_card(c, r, ui_theme_color(UI_C_PANEL_HI));
        if (i != CLASS_STICK) {
            draw_mark(c, r.x + (r.w - mark_w()) / 2, r.y + 18, 0);
        }
        gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + 62), r.w, 28 },
                    k_classes[i].name, UI_FONT_HEAD,
                    ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_CENTER);
        /* Under the name, above the count: 34 px for one line or two. */
        draw_blurb(c, (gfx_rect_t){ (int16_t)(r.x + 12), (int16_t)(r.y + 95),
                                    (int16_t)(r.w - 24), 34 },
                   ui_tr(k_classes[i].blurb));
        int n = 0;
        for (int p = 0; p < PROTO_COUNT; ++p) {
            if (k_protos[p].klass == i) { ++n; }
        }
        char have[48];
        if (i == CLASS_STICK) {
            snprintf(have, sizeof(have), TR(PG_PROFILES_RUN),
                     esc_stick_panel_runnable_count(),
                     (int)esc_profiles_count());
        } else {
            snprintf(have, sizeof(have),
                     (n == 1) ? TR(PG_PROTOCOL) : TR(PG_PROTOCOLS), n);
        }
        gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(r.y + 138), r.w, 16 },
                    have, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_CENTER);
    }
}

static DRAWER void draw_protocol_list(gfx_canvas_t *c)
{
    draw_crumb(c, k_classes[pg.klass].name);
    for (int i = 0; i < PROTO_COUNT; ++i) {
        if (k_protos[i].klass != pg.klass) {
            continue;
        }
        const gfx_rect_t r = proto_row(i);
        ui_card(c, r, ui_theme_color(UI_C_PANEL));
        gfx_text(c, r.x + 20, r.y + 12, proto_name(&k_protos[i]),
                 UI_FONT_HEAD,
                 ui_theme_color(UI_C_TEXT), 1);
        /* The transport, on every row: the protocols are different
         * conversations, not variants of one, so there is no autodetect. */
        gfx_text(c, r.x + 20, r.y + 38, ui_tr(k_protos[i].transport),
                 UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 60),
                                     (int16_t)(r.y + 20), 40, 20 },
                    ">", UI_FONT_HEAD, ui_theme_color(UI_C_ACCENT), 1,
                    GFX_ALIGN_CENTER);
        draw_mark(c, r.x + r.w - 60 - 12 - mark_w(),
                  r.y + (r.h - MARK_H) / 2, 0);
    }
}

/*
 * The device line: the MODELLED mark where a connection's dot would be,
 * its dot amber while a modelled session is open and faint while none is,
 * never the OK colour; then the model's device and the transport it would
 * use.  Under the parameters, left of READ, the sentence that no device is
 * reached, on every state of the page.
 */
static DRAWER void draw_device(gfx_canvas_t *c)
{
    const int x = PAD + 12;
    const int tx = x + mark_w() + 10;
    draw_mark(c, x, DEV_Y + (DEV_H - MARK_H) / 2,
              s.connected ? ui_theme_color(UI_C_WARN)
                          : ui_theme_color(UI_C_TEXT_FAINT));
    gfx_text(c, tx, DEV_Y + 4, programmer_screen_device_line(),
             UI_FONT_LABEL,
             s.connected ? ui_theme_color(UI_C_TEXT)
                         : ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, tx, DEV_Y + 24, ui_tr(proto()->transport), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    sp_note(c, x, pg.read_btn.x - 8, TR(PG_MODEL_NOTE),
            ui_theme_color(UI_C_WARN));
    ui_button(c, s.connect_btn, s.connected ? TR(PG_DISCONNECT)
                                            : TR(PG_CONNECT),
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

static DRAWER void draw_params(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, PARM_Y + 12, TR(PG_PARAMETERS), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    if (!s.connected) {
        gfx_text_in(c, (gfx_rect_t){ PAD, (int16_t)(PARM_Y + PARM_H / 2 - 8),
                                     (int16_t)(W - 2 * PAD), 16 },
                    TR(PG_NO_DEVICE), UI_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
        return;
    }

    const int max_scroll = (proto()->count > ROWS_MAX)
                               ? proto()->count - ROWS_MAX : 0;
    char count[40];
    snprintf(count, sizeof(count), TR(PG_RANGE_OF),
             s.scroll + 1, s.scroll + rows_shown(), proto()->count);
    gfx_text_in(c, (gfx_rect_t){ 420, (int16_t)(PARM_Y + 12), 260, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, pg.page_up, "^", ui_theme_color(UI_C_PANEL_HI),
              false, s.scroll > 0);
    ui_button(c, pg.page_dn, "v", ui_theme_color(UI_C_PANEL_HI),
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
        if (d->group != TX_COUNT) {
            gfx_text(c, PAD + 12, y, ui_tr(d->group), UI_FONT_LABEL,
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
        ui_button(c, pg.down[i], "-", ui_theme_color(UI_C_PANEL_HI),
                  false, s.value[idx] > lo);
        ui_button(c, pg.up[i], "+", ui_theme_color(UI_C_PANEL_HI),
                  false, s.value[idx] < hi);
    }

    /*
     * The selected parameter's help line.
     */
    const param_def_t *p = &proto()->params[s.picked];
    const char *help = ui_tr(p->help);
    if (sp_wrap_lines(help, HELP_CELLS, 2) > 1) {
        /* Two lines of help take the name's line: the row above shows the
         * name picked. */
        (void)sp_wrap(c, PAD + 12, HELP_Y, 18, help, HELP_CELLS, 2,
                      ui_theme_color(UI_C_TEXT_FAINT));
    } else {
        gfx_text(c, PAD + 12, HELP_Y, p->name, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, PAD + 12, HELP_Y + 18, help, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }

    const int n = programmer_screen_dirty();
    char wr[32];
    if (n > 0) {
        snprintf(wr, sizeof(wr), TR(PG_WRITE_N), n);
    } else {
        snprintf(wr, sizeof(wr), "%s", TR(PG_WRITE));
    }
    ui_button(c, pg.read_btn, TR(PG_READ), ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, pg.write_btn, wr,
              (n > 0) ? ui_theme_color(UI_C_WARN)
                      : ui_theme_color(UI_C_PANEL_HI),
              false, n > 0);
}

/* A protocol's page: the crumb and the two cards. */
static DRAWER void draw_page(gfx_canvas_t *c)
{
    char trail[64];
    snprintf(trail, sizeof(trail), "%s  >  %s",
             k_classes[pg.klass].name, proto_name(proto()));
    draw_crumb(c, trail);
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD), DEV_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, PARM_Y, (int16_t)(W - 2 * PAD), PARM_H },
            ui_theme_color(UI_C_PANEL));
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s.drawn_mask |= bit;
    }
    if (s.drawn[buf] == pg.rev) {
        return;
    }
    s.drawn[buf] = pg.rev;
    gfx_clear(c, ui_theme_color(UI_C_BG));

    if (pg.stage == STAGE_CLASS) {
        draw_classes(c);
        return;
    }
    if (pg.klass == CLASS_STICK) {
        esc_stick_panel_render(c);
        return;
    }
    if (pg.stage == STAGE_PROTOCOL) {
        draw_protocol_list(c);
        return;
    }
    draw_page(c);
    draw_device(c);
    draw_params(c);
}

static const ui_screen_t k_screen = {
    .title  = "PROGRAMMER",
    .reset  = reset,
    .enter  = esc_stick_panel_enter,
    .leave  = esc_stick_panel_leave,
    .tick   = esc_stick_panel_tick,
    .event  = event,
    .cancel = esc_stick_panel_cancel,
    .render = render,
};

const ui_screen_t *programmer_screen(void) { return &k_screen; }
