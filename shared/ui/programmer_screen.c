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
 * The profile list has two levels: the makers, alphabetical, and one
 * maker's models, by current, voltage and name, each opening its family's
 * profile.  A search field filters both.  Its keyboard docks on the right
 * while it is open and the rows narrow to its left, so the list filters
 * with every key and stays in view.  A run shows a stack light: green while
 * the detector holds a beep, red on a result that ended because something
 * was not as expected (esc_stick_reason_is_fault()).
 *
 * A profile whose ESC needs a person at it -- a jumper, a button -- lists
 * those steps (esc_profile_t's manual).  Its page carries MANUAL
 * INTERVENTION REQUIRED, which shows them over the screen, as does the
 * first opening; the warning lists those due before the power-up, and a
 * run that waits for one covers the page with DONE and ABORT.
 *
 * SPDX-License-Identifier: MIT
 */

#include "programmer_screen.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esc_profile.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
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

enum { CLASS_ESC = 0, CLASS_SERVO, CLASS_STICK, CLASS_COUNT };

static const class_t k_classes[CLASS_COUNT] = {
    { "ESC",       TX_PG_BLURB_ESC },
    { "SERVO",     TX_PG_BLURB_SERVO },
    { "ESC STICK", TX_PG_BLURB_STICK },
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
    /* A model name and nothing to translate: the device line is the name. */
    { "Hitec",    TX_PG_TRANSPORT_HITEC,   TX_COUNT,
      k_hitec,    8, CLASS_SERVO },
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

/* The search field in the crumb row, and the keyboard docked to the right
 * of SP_DOCK_X below it while the field is typed in. */
#define SP_FIND_MAX 16
#define SP_FIND_X   (W - PAD - 12 - 76 - 8 - SP_FIND_W)
#define SP_FIND_W   172
#define SP_DOCK_X   352
/* The trail left of the field, from the crumb's text: 48 cells. */
#define SP_TRAIL_CELLS ((SP_FIND_X - 8 - (PAD + 12 + 96 + 16)) / 8)
/* The footer under the rows: the count at its right, 45 cells, or at its
 * left beside the docked keyboard. */
#define SP_FOOT_Y   (SP_ROW_Y0 + SP_ROWS * SP_ROW_H + 6)
#define SP_COUNT_W  360
/* The most models one maker's list holds: as many as the registry lets a
 * maker have, so the count and the rows always agree. */
#define SP_MODELS   ((int)ESC_MAKER_MODELS_MAX)

/* The stack light at the right of the run's card: a cap, the red and the
 * green lens, each with a collar under it, and the base, px. */
#define SP_TOWER_W     44
#define SP_TOWER_X     (W - PAD - 16 - SP_TOWER_W)
#define SP_TOWER_Y     (PARM_Y + 14)
#define SP_TOWER_CAP   8
#define SP_TOWER_LENS  58
#define SP_TOWER_RING  3
#define SP_TOWER_BASE  26
/* What the run's and the result's lines may take beside it, in cells. */
#define SP_LINE_CELLS  86

/* The settings the TIMING page shows, in its order. */
static const setting_id_t k_sp_settings[] = {
    SET_STICK_V, SET_STICK_I, SET_STICK_BEEP_MIN, SET_STICK_GAP_MIN,
    SET_STICK_LONG, SET_STICK_LONG_MAX, SET_STICK_GROUP_GAP, SET_STICK_ENTRY,
    SET_STICK_STORE, SET_STICK_OFF, SET_STICK_SILENCE, SET_STICK_TIMEOUT,
    SET_STICK_THRESHOLD, SET_STICK_HYST,
};
#define SP_SETTINGS ((int)(sizeof(k_sp_settings) / sizeof(k_sp_settings[0])))

/* A maker on the list's first level: how many models it has, how many the
 * search finds, how many of those run, and whether any has manual steps. */
typedef struct {
    const char *name;
    uint16_t    models, found, runs;
    bool        manual;
} sp_maker_t;

/* A model on the second level: its profile's index in the registry and its
 * own index in the profile. */
typedef struct {
    uint16_t prof, model;
} sp_row_t;

typedef struct {
    /* The list in two levels.  Level 0 the makers, alphabetical; level 1
     * the models of one maker, by current, voltage and name.  count,
     * runnable and scroll are the level's showing; bscroll is the makers'
     * kept while a maker is open. */
    int        level;
    sp_maker_t makers[SP_MAX];
    sp_row_t   rows_m[SP_MODELS];
    const char *maker;      /* the maker open on level 1 */
    int        count, runnable, scroll, bscroll;
    int        model;       /* the page's model, -1 for none */

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
    uint32_t    built_key;  /* sp_list_key() when the list was built */

    /* The bench, as the application last said. */
    uint32_t now_ms, stops, pressed;
    bool     armed, link_up;

    /* The search: the pattern the list shows, what it was when the
     * keyboard opened (CANCEL goes back to it), and the keyboard. */
    char         find[SP_FIND_MAX + 1];
    char         find_was[SP_FIND_MAX + 1];
    ui_textkey_t tk;
    uint32_t     tk_rev;    /* the keyboard's revision last drawn */
    gfx_rect_t   find_box, find_clr;

    /* The stack light's green, as the last tick had it. */
    esc_stick_light_t light;
    bool              green;

    /* What the run has asked for, and what is still to be taken. */
    motor_cmd_t q[SP_QUEUE];
    int         qn;
    bool        sent_arm, sent_supply;
    float       sent_pct;

    /* The manual steps' pop-up and the profile it shows: the page's, or a
     * row's that does not run.  It opens by itself the first time a
     * profile with manual steps is opened, one bit a profile by its index
     * in the registry. */
    bool                 hand_open;
    const esc_profile_t *hand_p;
    int                  hand_model;    /* the model it was opened for, or
                                           -1: the family's lowest      */
    bool                 was_running;   /* the run, as the last tick saw */
    bool                 warn_read;     /* ALL STEPS read over the warning */

    /* The supply as its newest reading had it, run or no run: whether its
     * output is live, and since when it has read off. */
    bool                 sup_have, sup_live, sup_off_known, sup_gate;
    bool                 sup_off_drawn; /* what a result last showed     */
    uint32_t             sup_at, sup_off_since;
    uint8_t              hand_seen[SP_MAX / 8];

    gfx_rect_t rows[SP_ROWS], list_up, list_dn;
    gfx_rect_t hold_btn, cancel_btn, hand_btn, steps_btn;
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

/* The device line the protocol answers with: Hitec's is its model name. */
static const char *device_text(const proto_t *p)
{
    return (p->device != TX_COUNT) ? ui_tr(p->device) : "Hitec D956TW";
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
    s.st.find_box = (gfx_rect_t){ SP_FIND_X, CRUMB_Y, SP_FIND_W, 30 };
    s.st.find_clr = (gfx_rect_t){ (int16_t)(SP_FIND_X + SP_FIND_W - 30),
                                  (int16_t)(CRUMB_Y + 2), 28, 26 };
    s.st.list_up = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76), CRUMB_Y,
                                 STEP_W, 30 };
    s.st.list_dn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38), CRUMB_Y,
                                 STEP_W, 30 };
    s.st.hold_btn   = (gfx_rect_t){ PAD + 20, (int16_t)(H - PAD - 76),
                                    260, 56 };
    s.st.cancel_btn = (gfx_rect_t){ (int16_t)(W - PAD - 20 - 180),
                                    (int16_t)(H - PAD - 76), 180, 56 };
    /* Between HOLD TO RUN and CANCEL, on the warning. */
    s.st.steps_btn  = (gfx_rect_t){ PAD + 20 + 260 + 24,
                                    (int16_t)(H - PAD - 76), 266, 56 };
    /* In the item list's header, between CHANGE and the count. */
    s.st.hand_btn   = (gfx_rect_t){ PAD + 108, (int16_t)(PARM_Y + 4), 300,
                                    24 };
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
static void sp_build_list(bool keep_scroll);
static int  sp_runnable_count(void);
static void sp_render(gfx_canvas_t *c);
static void sp_find_event(const touch_event_t *evt);
static void sp_find_close(void);

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (evt->type != TOUCH_EVENT_DOWN) {
        /* Only the search's keys and the warning's hold follow a finger. */
        if (s.st.tk.open) {
            sp_find_event(evt);
        }
        sp_track(evt);
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
                    sp_build_list(false);
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

static void draw_crumb(gfx_canvas_t *c, const char *trail)
{
    ui_button(c, s.back, TR(LOG_BACK), ui_theme_color(UI_C_PANEL_HI), false,
              true);
    gfx_text(c, s.back.x + s.back.w + 16, CRUMB_Y + 8, trail, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

static void draw_classes(gfx_canvas_t *c)
{
    gfx_text(c, PAD + 12, 24, TR(PG_WHAT), UI_FONT_LABEL,
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
                    ui_tr(k_classes[i].blurb), UI_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_CENTER);
        int n = 0;
        for (int p = 0; p < PROTO_COUNT; ++p) {
            if (k_protos[p].klass == i) { ++n; }
        }
        char have[48];
        if (i == CLASS_STICK) {
            snprintf(have, sizeof(have), TR(PG_PROFILES_RUN),
                     sp_runnable_count(), (int)esc_profiles_count());
        } else {
            snprintf(have, sizeof(have),
                     (n == 1) ? TR(PG_PROTOCOL) : TR(PG_PROTOCOLS), n);
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
        gfx_text(c, r.x + 20, r.y + 38, ui_tr(k_protos[i].transport),
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
    gfx_text(c, x + 22, DEV_Y + 4, s.connected ? device_text(proto())
                                               : TR(PG_NOTHING_ANSWERED),
             UI_FONT_LABEL,
             s.connected ? ui_theme_color(UI_C_TEXT)
                         : ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, x + 22, DEV_Y + 24, ui_tr(proto()->transport), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
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
    gfx_text(c, PAD + 12, HELP_Y + 18, ui_tr(p->help), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);

    const int n = programmer_screen_dirty();
    char wr[32];
    if (n > 0) {
        snprintf(wr, sizeof(wr), TR(PG_WRITE_N), n);
    } else {
        snprintf(wr, sizeof(wr), "%s", TR(PG_WRITE));
    }
    ui_button(c, s.read_btn, TR(PG_READ), ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, s.write_btn, wr,
              (n > 0) ? ui_theme_color(UI_C_WARN)
                      : ui_theme_color(UI_C_PANEL_HI),
              false, n > 0);
}

/* ============================================== the stick class ======== */

/*
 * @p buf cut to @p max_chars cells, with ".." in place of what did not fit.
 * Cut between characters, never inside one: a cell is a code point and a
 * German letter is two bytes.
 */
static void sp_cut(char *buf, size_t n, int max_chars)
{
    if (max_chars < 3 || gfx_text_cells(buf) <= max_chars) {
        return;
    }
    const size_t at = gfx_text_prefix(buf, max_chars - 2);
    if (at + 3u <= n) {
        buf[at] = '.';
        buf[at + 1u] = '.';
        buf[at + 2u] = '\0';
    }
}

/*
 * A text cut to @p max_chars, with ".." in place of what did not fit: the
 * profiles' names are the manuals' and run long.
 */
static void sp_text(gfx_canvas_t *c, int x, int y, const char *text,
                    int max_chars, gfx_color_t ink)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "%s", (text != NULL) ? text : "");
    sp_cut(buf, sizeof(buf), max_chars);
    gfx_text(c, x, y, buf, UI_FONT_LABEL, ink, 1);
}

/* The bytes of the next line of @p at that fit @p cells: up to the last
 * space in it, or the whole width where no space falls after its start. */
static size_t sp_line_len(const char *at, int cells)
{
    if (gfx_text_cells(at) <= cells) {
        return strlen(at);
    }
    const size_t len = gfx_text_prefix(at, cells);
    size_t cut = len;
    while (cut > 0u && at[cut] != ' ') {
        --cut;
    }
    return (cut > 0u) ? cut : len;
}

/*
 * @p text in lines of at most @p cells, broken after a space where one
 * falls in the line and inside a word where none does, from (x, y) down by
 * @p pitch.  At most @p max_lines lines; the last of them ends in ".." when
 * the text goes on.  Returns the lines drawn.  A manual step is the
 * profile's own text and can be up to ESC_MANUAL_ACTION_MAX bytes, so it is
 * wrapped, not cut to one line.
 */
static int sp_wrap(gfx_canvas_t *c, int x, int y, int pitch,
                   const char *text, int cells, int max_lines,
                   gfx_color_t ink)
{
    int lines = 0;
    const char *at = (text != NULL) ? text : "";
    while (*at == ' ') {
        ++at;
    }
    while (*at != '\0' && lines < max_lines) {
        char buf[192];
        ++lines;
        if (lines == max_lines) {
            /* The last line takes the rest, cut with ".." if it is more
             * than fits. */
            snprintf(buf, sizeof(buf), "%s", at);
            sp_cut(buf, sizeof(buf), cells);
            at += strlen(at);
        } else {
            /* The line, no further than the text's end; the copy no
             * longer than the buffer. */
            const size_t rest = strlen(at);
            size_t len = sp_line_len(at, cells);
            len = (len < rest) ? len : rest;
            const size_t n = (len < sizeof(buf)) ? len : sizeof(buf) - 1u;
            memcpy(buf, at, n);
            buf[n] = '\0';
            at += len;
            while (*at == ' ') {
                ++at;
            }
        }
        gfx_text(c, x, y, buf, UI_FONT_LABEL, ink, 1);
        y += pitch;
    }
    return lines;
}

/* The lines sp_wrap() would draw, without drawing them. */
static int sp_wrap_lines(const char *text, int cells, int max_lines)
{
    int lines = 0;
    const char *at = (text != NULL) ? text : "";
    while (*at == ' ') {
        ++at;
    }
    while (*at != '\0' && lines < max_lines) {
        at += sp_line_len(at, cells);
        while (*at == ' ') {
            ++at;
        }
        ++lines;
    }
    return lines;
}

/* A manual step in the language showing: the profile's German where it
 * gives one and German shows, else its English. */
const char *programmer_screen_step_text(const esc_manual_t *m)
{
    if (m == NULL) {
        return "";
    }
    if (ui_text_language() == UI_LANG_DE && m->action_de != NULL
        && m->action_de[0] != '\0') {
        return m->action_de;
    }
    return (m->action != NULL) ? m->action : "";
}

static const char *sp_action(const esc_manual_t *m)
{
    return programmer_screen_step_text(m);
}

/* When a manual step is due, in the language showing. */
static const char *sp_when_text(const esc_manual_t *m)
{
    static char buf[96];
    switch (m->when) {
    case ESC_MANUAL_BEFORE_POWER:
        return TR(SP_HAND_WHEN_BEFORE_POWER);
    case ESC_MANUAL_AT_POWER_UP:
        if (m->hold_ms == 0u) {
            return TR(SP_HAND_WHEN_AT_POWER);
        }
        snprintf(buf, sizeof(buf), TR(SP_HAND_WHEN_AT_POWER_MS),
                 (unsigned)(m->hold_ms / 1000u),
                 (unsigned)(m->hold_ms % 1000u / 100u));
        return buf;
    case ESC_MANUAL_BEFORE_MENU:
        return TR(SP_HAND_WHEN_BEFORE_MENU);
    case ESC_MANUAL_DURING_MENU:
        return TR(SP_HAND_WHEN_DURING_MENU);
    case ESC_MANUAL_BEFORE_POWER_OFF:
        return TR(SP_HAND_WHEN_BEFORE_OFF);
    case ESC_MANUAL_AFTER_PROGRAMMING:
        return TR(SP_HAND_WHEN_AFTER);
    }
    return "?";
}

/*
 * The stick engine's refusals are English, held where the engine writes
 * them; the screen shows each in the language showing by matching its
 * English.  One the table does not know shows as the engine wrote it.
 */
static const ui_text_id_t k_why[] = {
    TX_ESC_WHY_NO_PROFILE, TX_ESC_WHY_PERSON, TX_ESC_WHY_NO_PROCEDURE,
    TX_ESC_WHY_AFTER_POWER, TX_ESC_WHY_MELODY, TX_ESC_WHY_YES_NO,
    TX_ESC_WHY_POSITION, TX_ESC_WHY_OWN_KIND, TX_ESC_WHY_PITCH,
    TX_ESC_WHY_NO_ITEMS, TX_ESC_WHY_NO_SELECT, TX_ESC_WHY_REST_ENTRY,
    TX_ESC_WHY_STORE_TWO, TX_ESC_WHY_TWO_MOVES, TX_ESC_WHY_SELECT_REST,
    TX_ESC_WHY_VALUE_SELECT, TX_ESC_WHY_ONE_MOVE, TX_ESC_WHY_MANY_ONE,
    TX_ESC_WHY_COUNTED_ONE, TX_ESC_WHY_REPEAT, TX_ESC_WHY_STORE_SELECT,
    TX_ESC_WHY_NO_TIMING, TX_ESC_WHY_NOTHING, TX_ESC_WHY_TOO_MANY,
    TX_ESC_WHY_NO_ITEM, TX_ESC_WHY_ACTIONS, TX_ESC_WHY_NO_VALUE,
    TX_ESC_WHY_ZERO, TX_ESC_WHY_ONE_PER_ITEM, TX_ESC_WHY_BEEP_GAP,
    TX_ESC_WHY_LONG, TX_ESC_WHY_LONG_MAX, TX_ESC_WHY_GROUP_GAP,
    TX_ESC_WHY_THRESHOLD, TX_ESC_WHY_ENTRY, TX_ESC_WHY_SELECT_WINDOW,
    TX_ESC_WHY_VALUE_WINDOW, TX_ESC_WHY_NO_RUN, TX_ESC_WHY_ONE_VALUE,
    TX_ESC_WHY_HAND, TX_ESC_WHY_ENTRY_POS, TX_ESC_WHY_ENTRY_TIME,
    TX_ESC_WHY_AFTER_TWO, TX_ESC_WHY_AFTER_NONE,
};

const char *programmer_screen_why_text(const char *why)
{
    if (why == NULL) {
        return TR(SP_REFUSED);
    }
    for (size_t i = 0; i < sizeof(k_why) / sizeof(k_why[0]); ++i) {
        if (strcmp(why, ui_tr_in(UI_LANG_EN, k_why[i])) == 0) {
            return ui_tr(k_why[i]);
        }
    }
    return why;
}

static const char *sp_why_text(const char *why)
{
    return programmer_screen_why_text(why);
}

/*
 * Why model @p model of a profile cannot run, into @p buf, or NULL when it
 * can: the engine's reason, or a voltage the supply cannot give --
 * VOLTAGE where it is set, else the model's cell count, or with no model
 * (-1) or none stated the family's lowest.
 */
static const char *sp_model_why(const esc_profile_t *p, int model, char *buf,
                                size_t n)
{
    const char *why = NULL;
    if (esc_stick_kind(p, &why) == ESC_STICK_KIND_NONE) {
        return sp_why_text(why);
    }
    const float v = settings_get(SET_STICK_V);
    const uint32_t mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                                   : esc_stick_model_mv(p, model);
    const unsigned cap = (unsigned)lroundf(supply_screen_caps().v_max
                                           * 1000.0f);
    if (mv > cap) {
        snprintf(buf, n, TR(SP_NEEDS_V),
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u),
                 cap / 1000u, cap % 1000u / 100u);
        return buf;
    }
    return NULL;
}

static const char *sp_why(const esc_profile_t *p, char *buf, size_t n)
{
    return sp_model_why(p, -1, buf, n);
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

/* The two things outside the profile that decide whether it runs: VOLTAGE
 * and the SUPPLY cap, mV. */
static uint32_t sp_list_key(void)
{
    const uint32_t v = (uint32_t)lroundf(settings_get(SET_STICK_V) * 1000.0f);
    const uint32_t cap = (uint32_t)lroundf(supply_screen_caps().v_max
                                           * 1000.0f);
    return (v << 16) ^ cap;
}

/* @p a against @p b with the letters A to Z folded, as the search folds
 * them: the makers' order and their grouping. */
static int sp_casecmp(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        int x = (unsigned char)*a;
        int y = (unsigned char)*b;
        x -= (x >= 'a' && x <= 'z') ? 32 : 0;
        y -= (y >= 'a' && y <= 'z') ? 32 : 0;
        if (x != y || x == 0) {
            return x - y;
        }
    }
}

static bool sp_model_runs(const esc_profile_t *p, int model)
{
    char buf[40];
    return sp_model_why(p, model, buf, sizeof(buf)) == NULL;
}

/*
 * The first level: every maker, alphabetical with case folded, that the
 * search finds a model of (esc_model_matches(); every one while it is
 * empty), with its models, those found and those of them that run.  A
 * card profile joins its maker as a built-in one does.
 */
static void sp_build_makers(stick_t *t)
{
    int n = 0;
    const size_t total = esc_profiles_count();
    for (size_t i = 0; i < total; ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        int at = 0;
        while (at < n && sp_casecmp(t->makers[at].name, p->brand) < 0) {
            ++at;
        }
        if (at == n || sp_casecmp(t->makers[at].name, p->brand) != 0) {
            if (n >= SP_MAX) {
                continue;
            }
            memmove(&t->makers[at + 1], &t->makers[at],
                    (size_t)(n - at) * sizeof(t->makers[0]));
            t->makers[at] = (sp_maker_t){ p->brand, 0u, 0u, 0u, false };
            ++n;
        }
        sp_maker_t *mk = &t->makers[at];
        for (unsigned m = 0; m < p->model_count; ++m) {
            mk->models++;
            if (!esc_model_matches(p, m, t->find)) {
                continue;
            }
            mk->found++;
            if (sp_model_runs(p, (int)m)) {
                mk->runs++;
            }
            mk->manual = mk->manual || p->manual_count > 0u;
        }
    }
    int k = 0;
    t->runnable = 0;
    for (int i = 0; i < n; ++i) {
        if (t->makers[i].found > 0u) {
            t->runnable += (t->makers[i].runs > 0u) ? 1 : 0;
            t->makers[k++] = t->makers[i];
        }
    }
    t->count = k;
}

/* One key of the models' order: smaller first, 0 -- not stated -- last. */
static int sp_key_cmp(uint32_t a, uint32_t b)
{
    if (a == b) {
        return 0;
    }
    if (a == 0u || b == 0u) {
        return (a == 0u) ? 1 : -1;
    }
    return (a < b) ? -1 : 1;
}

/* Current, then voltage, then name with case folded; a key a model does
 * not state sorts it last on that key.  Ties keep the registry's order. */
static int sp_by_model(const void *a, const void *b)
{
    const sp_row_t *x = a, *y = b;
    const esc_model_t *mx = &esc_profiles_at(x->prof)->models[x->model];
    const esc_model_t *my = &esc_profiles_at(y->prof)->models[y->model];
    int c = sp_key_cmp(mx->current_a, my->current_a);
    if (c == 0) {
        c = sp_key_cmp(mx->v_max_mv, my->v_max_mv);
    }
    if (c == 0) {
        c = sp_casecmp(mx->name, my->name);
    }
    if (c == 0) {
        c = (x->prof != y->prof) ? ((x->prof < y->prof) ? -1 : 1)
                                 : ((x->model < y->model) ? -1 : 1);
    }
    return c;
}

/*
 * The second level: the open maker's models the search finds, one row a
 * model, by current, voltage and name; each row is its family's profile.
 * The registry holds a maker to ESC_MAKER_MODELS_MAX, SP_MODELS, so every
 * one of them has a row.
 */
static void sp_build_models(stick_t *t)
{
    int n = 0;
    const size_t total = esc_profiles_count();
    for (size_t i = 0; i < total && t->maker != NULL; ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        if (sp_casecmp(p->brand, t->maker) != 0) {
            continue;
        }
        for (unsigned m = 0; m < p->model_count && n < SP_MODELS; ++m) {
            if (esc_model_matches(p, m, t->find)) {
                t->rows_m[n++] = (sp_row_t){ (uint16_t)i, (uint16_t)m };
            }
        }
    }
    qsort(t->rows_m, (size_t)n, sizeof(t->rows_m[0]), sp_by_model);
    t->runnable = 0;
    for (int i = 0; i < n; ++i) {
        t->runnable += sp_model_runs(esc_profiles_at(t->rows_m[i].prof),
                                     t->rows_m[i].model) ? 1 : 0;
    }
    t->count = n;
}

/*
 * The level showing, built again whenever the list comes back on screen,
 * the search changes, or VOLTAGE or the SUPPLY cap moves under it, so the
 * counts follow the reasons the rows draw.
 */
static void sp_build_list(bool keep_scroll)
{
    stick_t *t = &s.st;
    const int scroll = keep_scroll ? t->scroll : 0;
    t->built_key = sp_list_key();
    if (t->level == 1) {
        sp_build_models(t);
    } else {
        sp_build_makers(t);
    }
    t->scroll = (scroll < t->count) ? scroll : 0;
}

static void sp_enter(void)
{
    if (s.klass == CLASS_STICK && s.stage != STAGE_CLASS) {
        sp_build_list(true);
        ++s.rev;
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
    t->hand_open = false;
    t->model = -1;
    /* A profile with manual steps says so by itself the first time it is
     * opened; MANUAL INTERVENTION REQUIRED shows it again. */
    if (p != NULL && p->manual_count > 0u) {
        const size_t total = esc_profiles_count();
        for (size_t i = 0; i < total && i < SP_MAX; ++i) {
            if (esc_profiles_at(i) != p) {
                continue;
            }
            const uint8_t bit = (uint8_t)(1u << (i & 7u));
            if ((t->hand_seen[i >> 3] & bit) == 0u) {
                t->hand_seen[i >> 3] |= bit;
                t->hand_open = true;
                t->hand_p = p;
                t->hand_model = -1;     /* the caller names the model */
            }
            break;
        }
    }
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
                     : esc_stick_model_mv(s.st.p, s.st.model);
    *ma = (uint32_t)lroundf(settings_get(SET_STICK_I) * 1000.0f);
}

/*
 * Everything a run needs, and whether it can start.  When it cannot, the
 * note says why; with nothing picked the note stays empty and RUN is simply
 * not offered.  Refused, never adjusted: a voltage over the SUPPLY cap is
 * the operator's to change.
 */
static bool sp_supply_reads_off(void);
static bool sp_warn_steps_fit(const esc_profile_t *p);

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
        snprintf(st->note, sizeof(st->note), TR(SP_AT_MOST),
                 (unsigned)ESC_STICK_MAX_CHANGES);
        return false;
    }
    if (*mv == 0u) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_NO_CELLS));
        return false;
    }
    const supply_caps_t caps = supply_screen_caps();
    const unsigned vmax = (unsigned)lroundf(caps.v_max * 1000.0f);
    const unsigned vmin = (unsigned)lroundf(caps.v_min * 1000.0f);
    const unsigned imax = (unsigned)lroundf(caps.i_max * 1000.0f);
    if (*mv > vmax) {
        snprintf(st->note, sizeof(st->note), TR(SP_ABOVE_CAP),
                 (unsigned)(*mv / 1000u), (unsigned)(*mv % 1000u / 10u),
                 vmax / 1000u, vmax % 1000u / 10u);
        return false;
    }
    if (*mv < vmin) {
        snprintf(st->note, sizeof(st->note), TR(SP_BELOW_MIN),
                 vmin / 1000u, vmin % 1000u / 10u);
        return false;
    }
    if (*ma > imax) {
        snprintf(st->note, sizeof(st->note), TR(SP_I_ABOVE_CAP),
                 imax / 1000u, imax % 1000u / 10u);
        return false;
    }
    /* Asked on, or read live: the output on, or current through it. */
    if (supply_screen_output_live() || st->sup_live) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_OUTPUT_LIVE));
        return false;
    }
    /* No reading, an old one, or a supply that does not answer is not a
     * supply known off: a run moves the stick and may ask for a hand at
     * the ESC on that word. */
    if (!sp_supply_reads_off()) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_SUPPLY_NOT_OFF));
        return false;
    }
    const char *why = NULL;
    if (!esc_stick_check(st->p, ch, *n, t, &why)) {
        snprintf(st->note, sizeof(st->note), "%s", sp_why_text(why));
        return false;
    }
    return true;
}

/*
 * Whether the supply reads off now: a reading no older than
 * ESC_STICK_STALE_MS in which the supply itself reports its output off
 * with the current at or under ESC_STICK_OFF_MA, and has for
 * ESC_STICK_OFF_SETTLE_MS -- the rule a run holds the supply to.  The
 * warning shows a step at an unpowered ESC only then.
 */
static bool sp_supply_reads_off(void)
{
    const stick_t *t = &s.st;
    if (!t->sup_have || !t->sup_off_known || supply_screen_output_live()) {
        return false;
    }
    const int32_t age = (int32_t)(t->now_ms - t->sup_at);
    const uint32_t held = t->sup_at - t->sup_off_since;
    return age <= (int32_t)ESC_STICK_STALE_MS
           && held >= ESC_STICK_OFF_SETTLE_MS;
}

/* Whether the warning asks a step at an unpowered ESC of the operator and
 * may not, as the supply does not read off. */
static bool sp_warn_gated(void)
{
    const stick_t *t = &s.st;
    return t->p != NULL
           && esc_profile_manual_count(t->p, ESC_MANUAL_BEFORE_POWER) > 0u
           && !sp_supply_reads_off();
}

/*
 * Whether the warning shows every before-power step whole: under its
 * lines (sp_draw_warning(): five, the profile, the supply and the
 * unverified line from PAD + 68 at 22 px, then the steps' label), each
 * step wrapped to two lines, and room kept for the line about later steps,
 * above HOLD TO RUN.
 */
static unsigned sp_later_steps(const esc_profile_t *p);

static bool sp_warn_steps_fit(const esc_profile_t *p)
{
    if (p == NULL || esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER)
                         == 0u) {
        return true;
    }
    const int last = s.st.hold_btn.y - 20;
    const unsigned later = sp_later_steps(p);
    const int keep = (later > 0u) ? 22 : 0;
    int y = PAD + 68 + 5 * 22 + 66 + 22;
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_BEFORE_POWER) {
            const int need = sp_wrap_lines(sp_action(m), 90, 2);
            if (y + (need - 1) * 22 > last - keep) {
                return false;
            }
            y += 22 * need;
        }
    }
    return true;
}

/* The steps the run stops for on its way, after the warning's: at the
 * power-up, before the menu, and before the supply goes off. */
static unsigned sp_later_steps(const esc_profile_t *p)
{
    return esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP)
           + esc_profile_manual_count(p, ESC_MANUAL_BEFORE_MENU)
           + esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER_OFF);
}

/* Whether HOLD TO RUN may not count: the supply does not read off, or the
 * before-power steps do not fit and ALL STEPS has not been read. */
static bool sp_hold_blocked(void)
{
    const stick_t *t = &s.st;
    return sp_warn_gated() || (!sp_warn_steps_fit(t->p) && !t->warn_read);
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
    if (!sp_plan(ch, &n, &tm, &mv, &ma) || sp_hold_blocked()) {
        return;     /* the note says why */
    }
    const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                  t->link_up, t->pressed };
    const char *why = NULL;
    if (!esc_stick_start(&t->run, t->p, ch, n, &tm, mv, ma, &b, &why)) {
        snprintf(t->note, sizeof(t->note), "%s", sp_why_text(why));
        return;
    }
    t->runs++;
    t->shown = true;
    esc_stick_light_reset(&t->light, &t->run);
    t->green = false;
    t->sent_arm = false;
    /* Sent as on, so the run's off goes out: the supply is asked off at
     * the start, whatever it was left as. */
    t->sent_supply = true;
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
        s.st.green ? 1u : 0u,
        esc_stick_hand_ready(e) ? 1u : 0u,
        (esc_stick_hand_left_ms(e) + 999u) / 1000u, e->hand,
        e->hand_menu ? 1u : 0u,
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
    if (s.klass == CLASS_STICK && s.stage == STAGE_PROTOCOL
        && sp_list_key() != t->built_key) {
        sp_build_list(true);
        ++s.rev;
    }
    if (t->shown && t->p != NULL && t->p->manual_count > 0u) {
        const bool off = sp_supply_reads_off();
        if (off != t->sup_off_drawn) {
            t->sup_off_drawn = off;
            ++s.rev;
        }
    }
    if (t->warn) {
        /* The steps at an unpowered ESC show, and HOLD TO RUN counts, only
         * while the supply reads off; a hold under way ends when it stops
         * reading so. */
        const bool gated = sp_hold_blocked();
        if (gated != t->sup_gate) {
            t->sup_gate = gated;
            ++s.rev;
        }
        if (gated) {
            sp_end_hold();
        }
    }
    if (t->warn && t->warn_down) {
        ++s.rev;            /* the hold's fill */
        if (ui_hold_tick(&t->hold, dt_s)) {
            sp_start();
        }
    }
    const bool running = esc_stick_running(&t->run);
    if (t->was_running && !running && t->shown && t->run.p != NULL
        && esc_profile_manual_count(t->run.p,
                                    ESC_MANUAL_AFTER_PROGRAMMING) > 0u) {
        /* The run is over: what is to be done now, every step of it. */
        t->hand_open = true;
        t->hand_p = t->run.p;
        t->hand_model = t->model;
        ++s.rev;
    }
    t->was_running = running;
    if (running) {
        const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                      t->link_up, t->pressed };
        esc_stick_step(&t->run, &b);
        sp_follow();
        /* A light that changes repaints both buffers: the signature moves
         * the revision, and each buffer redraws on a revision it has not
         * drawn. */
        t->green = esc_stick_light_green(&t->light, &t->run, t->now_ms);
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
    /* A warning not held is a run not started.  The search keeps what was
     * typed. */
    t->warn = false;
    t->hand_open = false;
    sp_end_hold();
    sp_find_close();
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
    if (t->tk.open && t->tk.pressed >= 0) {
        ui_textkey_cancel_press(&t->tk);
        t->tk_rev = t->tk.revision;
        ++s.rev;
    }
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

/* The search's keyboard, docked right of the narrowed rows. */
static gfx_rect_t sp_dock(void)
{
    return (gfx_rect_t){ SP_DOCK_X, (int16_t)(SP_ROW_Y0 - 2),
                         (int16_t)(W - PAD - SP_DOCK_X),
                         (int16_t)(H - PAD - SP_ROW_Y0 + 2) };
}

/* A profile row, narrowed to the left of the keyboard while it is open. */
static gfx_rect_t sp_row(int i)
{
    gfx_rect_t r = s.st.rows[i];
    if (s.st.tk.open) {
        r.w = (int16_t)(SP_DOCK_X - 8 - r.x);
    }
    return r;
}

/* The keyboard closes; the search stays as typed. */
static void sp_find_close(void)
{
    stick_t *t = &s.st;
    if (t->tk.open) {
        ui_textkey_close(&t->tk);
        t->tk_rev = t->tk.revision;
        ++s.rev;
    }
}

/*
 * One event for the search's keyboard.  Every key that changes the text
 * filters the list at once; OK keeps the text, CANCEL goes back to the
 * search the keyboard opened on.
 */
static void sp_find_event(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    char out[SP_FIND_MAX + 1];
    const ui_textkey_result_t r = ui_textkey_event(&t->tk, evt, out,
                                                   sizeof(out));
    const char *typed = (r == UI_TEXTKEY_OK)        ? out
                      : (r == UI_TEXTKEY_CANCELLED) ? t->find_was
                                                    : t->tk.text;
    if (strcmp(typed, t->find) != 0) {
        snprintf(t->find, sizeof(t->find), "%.*s", SP_FIND_MAX, typed);
        sp_build_list(false);           /* a new search starts at the top */
        if (t->level == 0) {
            t->bscroll = 0;
        }
        ++s.rev;
    }
    if (t->tk.revision != t->tk_rev) {
        t->tk_rev = t->tk.revision;
        ++s.rev;
    }
}

static bool sp_down(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    const int px = evt->point.x, py = evt->point.y;

    /* The manual steps' pop-up covers the screen; OK closes it. */
    if (t->hand_open) {
        if (gfx_rect_contains(t->cancel_btn, px, py)) {
            t->hand_open = false;
            /* Read over the warning: the hold may count. */
            t->warn_read = t->warn_read || t->warn;
            ++s.rev;
        }
        return true;
    }

    /* The warning covers the screen, BACK included. */
    if (t->warn) {
        if (!sp_warn_steps_fit(t->p) && gfx_rect_contains(t->steps_btn, px, py)) {
            t->hand_open = true;            /* ALL STEPS, over the warning */
            t->hand_p = t->p;
            t->hand_model = t->model;
            sp_end_hold();
            ++s.rev;
            return true;
        }
        if (gfx_rect_contains(t->hold_btn, px, py) && !sp_hold_blocked()) {
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
        if (t->tk.open && gfx_rect_contains(t->tk.area, px, py)) {
            sp_find_event(evt);
            return true;
        }
        if (gfx_rect_contains(s.back, px, py)) {
            /* BACK climbs a level: a maker's models to the makers, with
             * the makers' scroll and the search kept; the makers to the
             * classes. */
            sp_find_close();
            if (t->level == 1) {
                t->level = 0;
                t->scroll = t->bscroll;
                sp_build_list(true);
            } else {
                s.stage = STAGE_CLASS;
            }
            ++s.rev;
            return true;
        }
        if (!t->tk.open && t->find[0] != '\0'
            && gfx_rect_contains(t->find_clr, px, py)) {
            t->find[0] = '\0';                  /* X: the whole list */
            sp_build_list(false);
            ++s.rev;
            return true;
        }
        if (!t->tk.open && gfx_rect_contains(t->find_box, px, py)) {
            memcpy(t->find_was, t->find, sizeof(t->find_was));
            ui_textkey_open_search(&t->tk, sp_dock(), TR(SP_FIND_TITLE),
                                   t->find, SP_FIND_MAX);
            t->tk_rev = t->tk.revision;
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
            if (!gfx_rect_contains(sp_row(i), px, py)) {
                continue;
            }
            const int at = t->scroll + i;
            /* A maker opens its models, the search kept; the makers'
             * scroll is kept for BACK.  No maker opens by itself: one the
             * search narrows to is shown, and a tap opens it. */
            if (t->level == 0) {
                sp_find_close();
                t->maker = t->makers[at].name;
                t->bscroll = t->scroll;
                t->level = 1;
                sp_build_list(false);
                ++s.rev;
                return true;
            }
            /* A model opens its family's profile.  One whose profile the
             * engine cannot run says why on its row and goes no further,
             * unless the profile has manual steps: those it shows.  One
             * picked while the keyboard is open closes it, the search
             * kept. */
            const esc_profile_t *p = esc_profiles_at(t->rows_m[at].prof);
            const int model = t->rows_m[at].model;
            if (sp_model_runs(p, model)) {
                sp_find_close();
                sp_pick_profile(p);
                t->model = model;
                t->hand_model = model;
                s.stage = STAGE_DEVICE;
                ++s.rev;
            } else if (p->manual_count > 0u) {
                /* The steps, and why this model -- not the family -- does
                 * not run: the row's own reason. */
                sp_find_close();
                t->hand_open = true;
                t->hand_p = p;
                t->hand_model = model;
                ++s.rev;
            }
            return true;
        }
        return true;
    }

    /* The device page.  A run under way takes ABORT and nothing else; STOP
     * is in the band, and leaving the screen aborts. */
    if (esc_stick_running(&t->run)) {
        /* A manual step asked covers the page: DONE or ABORT.  DONE is
         * acted on in the next tick, after the bench has been judged. */
        if (esc_stick_hand(&t->run) != NULL) {
            if (gfx_rect_contains(t->hold_btn, px, py)) {
                if (esc_stick_confirm(&t->run)) {
                    ++s.rev;
                }
            } else if (gfx_rect_contains(t->cancel_btn, px, py)) {
                sp_end_run(ESC_STICK_R_USER);
            }
            return true;
        }
        if (gfx_rect_contains(s.write_btn, px, py)) {
            esc_stick_abort(&t->run, ESC_STICK_R_USER);
            sp_follow();
            ++s.rev;
        }
        return true;
    }
    if (t->shown) {
        if (t->p->manual_count > 0u && gfx_rect_contains(t->hand_btn, px, py)) {
            t->hand_open = true;            /* every step, after included */
            t->hand_p = t->p;
            t->hand_model = t->model;
            ++s.rev;
            return true;
        }
        const bool back = gfx_rect_contains(s.back, px, py);
        if (back || gfx_rect_contains(s.write_btn, px, py)) {
            t->shown = false;               /* OK: back to the menu */
            if (back) {
                s.stage = STAGE_PROTOCOL;
                sp_build_list(true);
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
        sp_build_list(true);
        ++s.rev;
        return true;
    }
    if (t->p->manual_count > 0u && gfx_rect_contains(t->hand_btn, px, py)) {
        t->hand_open = true;                /* MANUAL INTERVENTION REQUIRED */
        t->hand_p = t->p;
        t->hand_model = t->model;
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
            t->warn_read = false;
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
        if (by != 0 && esc_stick_not_offered(&t->p->items[idx]) == NULL) {
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

/* The search field: what is searched for, or SEARCH; X clears it while
 * the keyboard is closed. */
static void sp_draw_find(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_rect_t b = t->find_box;
    const bool any = t->find[0] != '\0';
    const bool clr = any && !t->tk.open;
    gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                        t->tk.open ? ui_theme_color(UI_C_ACCENT)
                                   : ui_theme_color(UI_C_EDGE));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(b.x + 8), b.y,
                                 (int16_t)(b.w - 16 - (clr ? 28 : 0)),
                                 b.h },
                any ? t->find : TR(SP_FIND), UI_FONT_LABEL,
                any ? ui_theme_color(UI_C_TEXT)
                    : ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_LEFT);
    if (clr) {
        ui_button(c, t->find_clr, "X", ui_theme_color(UI_C_PANEL_HI), false,
                  true);
    }
}

/* The red MANUAL tag, its right edge at @p right; its width, px. */
static int sp_draw_tag(gfx_canvas_t *c, int right, int y)
{
    const char *tag = TR(SP_HAND_TAG);
    const int w = gfx_text_cells(tag) * 8 + 12;
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_round_rect(c, right - w, y, w, 20, 4, red);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(right - w), (int16_t)y,
                                 (int16_t)w, 20 },
                tag, UI_FONT_LABEL,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    return w;
}

/* "55 A" and "25 V" or "25.2 V" for a model's current and voltage; "-"
 * where it states none. */
static void sp_amps(char *out, size_t n, const esc_model_t *m)
{
    if (m->current_a == 0u) {
        snprintf(out, n, "-");
    } else {
        snprintf(out, n, "%u A", (unsigned)m->current_a);
    }
}

static void sp_volts(char *out, size_t n, const esc_model_t *m)
{
    if (m->v_max_mv == 0u) {
        snprintf(out, n, "-");
    } else if (m->v_max_mv % 1000u == 0u) {
        snprintf(out, n, "%u V", (unsigned)(m->v_max_mv / 1000u));
    } else {
        snprintf(out, n, "%u.%u V", (unsigned)(m->v_max_mv / 1000u),
                 (unsigned)(m->v_max_mv % 1000u / 100u));
    }
}

/* The run mark of a narrowed row: filled in the accent, or a ring. */
static void sp_mark_runs(gfx_canvas_t *c, gfx_rect_t r, bool runs)
{
    const int mx = r.x + r.w - 14, my = r.y + r.h / 2;
    if (runs) {
        gfx_fill_circle(c, mx, my, 5, ui_theme_color(UI_C_ACCENT));
    } else {
        gfx_draw_circle(c, mx, my, 5, ui_theme_color(UI_C_TEXT_FAINT));
    }
}

/* A maker's row: its name, the MANUAL tag where a model it lists has
 * manual steps, and its models: those that run of all, or while a search
 * is typed those found of all and those of them that run. */
static void sp_draw_maker(gfx_canvas_t *c, gfx_rect_t r, const sp_maker_t *m,
                          bool dock, bool searching)
{
    const bool runs = m->runs > 0u;
    ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                       : ui_theme_color(UI_C_PANEL_SUNK));
    const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    char right[64];
    if (dock) {
        snprintf(right, sizeof(right), "%u", (unsigned)m->found);
        const int tag = m->manual ? sp_draw_tag(c, r.x + r.w - 28, r.y + 6)
                                        + 8 : 0;
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 28 - tag - 48),
                                     (int16_t)(r.y + 8), 40, 16 },
                    right, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                    GFX_ALIGN_RIGHT);
        sp_text(c, r.x + 8, r.y + 8, m->name,
                (r.w - 8 - 28 - tag - 56) / 8, ink);
        sp_mark_runs(c, r, runs);
        return;
    }
    sp_text(c, r.x + 12, r.y + 8, m->name, 30, ink);
    if (m->manual) {
        (void)sp_draw_tag(c, r.x + r.w - 312 - 8, r.y + 6);
    }
    if (searching) {
        snprintf(right, sizeof(right), TR(SP_MAKER_FOUND),
                 (unsigned)m->found, (unsigned)m->models, (unsigned)m->runs);
    } else {
        snprintf(right, sizeof(right), TR(SP_MAKER_MODELS),
                 (unsigned)m->runs, (unsigned)m->models);
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 312),
                                 (int16_t)(r.y + 8), 300, 16 },
                right, UI_FONT_LABEL,
                runs ? ui_theme_color(UI_C_ACCENT)
                     : ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);
}

/* A model's row: its name, current and voltage, its family, the MANUAL
 * tag, and what its family's profile is or why it does not run. */
static void sp_draw_model(gfx_canvas_t *c, gfx_rect_t r, const sp_row_t *row,
                          bool dock)
{
    const esc_profile_t *p = esc_profiles_at(row->prof);
    const esc_model_t *m = &p->models[row->model];
    char cap[40];
    const char *why = sp_model_why(p, row->model, cap, sizeof(cap));
    const bool runs = why == NULL;
    ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                       : ui_theme_color(UI_C_PANEL_SUNK));
    const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    const gfx_color_t dim = runs ? ui_theme_color(UI_C_TEXT_DIM)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    char amps[16], volts[16];
    sp_amps(amps, sizeof(amps), m);
    sp_volts(volts, sizeof(volts), m);
    if (dock) {
        const int tag = (p->manual_count > 0u)
                            ? sp_draw_tag(c, r.x + r.w - 28, r.y + 6) + 8
                            : 0;
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 28 - tag - 56),
                                     (int16_t)(r.y + 8), 48, 16 },
                    amps, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
        sp_text(c, r.x + 8, r.y + 8, m->name,
                (r.w - 8 - 28 - tag - 64) / 8, ink);
        sp_mark_runs(c, r, runs);
        return;
    }
    sp_text(c, r.x + 12, r.y + 8, m->name, 26, ink);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 228), (int16_t)(r.y + 8),
                                 56, 16 },
                amps, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 288), (int16_t)(r.y + 8),
                                 64, 16 },
                volts, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
    const int tag = (p->manual_count > 0u)
                        ? sp_draw_tag(c, r.x + r.w - 236, r.y + 6) + 8
                        : 0;
    sp_text(c, r.x + 368, r.y + 8, p->family, (r.w - 368 - 236 - tag) / 8,
            ui_theme_color(UI_C_TEXT_FAINT));
    char right[80];
    if (runs) {
        char items[24];
        const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
        snprintf(items, sizeof(items),
                 (p->item_count == 1u) ? TR(SP_ITEM) : TR(SP_ITEMS),
                 (unsigned)p->item_count);
        snprintf(right, sizeof(right), "%s%s%s  %s",
                 esc_profiles_is_override(p) ? TR(SP_CARD) : "",
                 esc_profiles_is_override(p) ? "  " : "", items,
                 (kind == ESC_STICK_KIND_TWO_STAGE) ? TR(SP_TWO_STAGE)
                                                    : TR(SP_ONE_STAGE));
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

/*
 * The list: the crumb and the search field above, a level's rows, and the
 * footer: the count at the right under the rows, or beside the docked
 * keyboard at the left; the line that no profile is verified while the
 * keyboard is closed.
 */
static void sp_draw_list(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const bool dock = t->tk.open;
    const bool searching = t->find[0] != '\0';
    char trail[96];
    if (t->level == 1 && t->maker != NULL) {
        snprintf(trail, sizeof(trail), "ESC STICK  >  %s", t->maker);
    } else {
        snprintf(trail, sizeof(trail), "ESC STICK");
    }
    sp_cut(trail, sizeof(trail), SP_TRAIL_CELLS);
    draw_crumb(c, trail);
    sp_draw_find(c);
    ui_button(c, t->list_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll > 0);
    ui_button(c, t->list_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll + SP_ROWS < t->count);

    if (t->count == 0) {
        gfx_text(c, PAD + 12, SP_ROW_Y0 + 8, TR(SP_FIND_NONE), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    for (int i = 0; i < SP_ROWS && t->scroll + i < t->count; ++i) {
        if (t->level == 1) {
            sp_draw_model(c, sp_row(i), &t->rows_m[t->scroll + i], dock);
        } else {
            sp_draw_maker(c, sp_row(i), &t->makers[t->scroll + i], dock,
                          searching);
        }
    }

    char count[64];
    const int last = (t->scroll + SP_ROWS < t->count) ? t->scroll + SP_ROWS
                                                      : t->count;
    const int first = (t->count > 0) ? t->scroll + 1 : 0;
    if (t->level == 1 && searching) {
        snprintf(count, sizeof(count), TR(SP_LIST_FOUND), first, last,
                 t->count, t->runnable);
    } else if (t->level == 1) {
        snprintf(count, sizeof(count), TR(SP_LIST_COUNT), first, last,
                 t->count, t->runnable);
    } else if (searching) {
        snprintf(count, sizeof(count), TR(SP_MAKERS_FOUND), first, last,
                 t->count, t->runnable);
    } else {
        snprintf(count, sizeof(count), TR(SP_MAKERS_COUNT), first, last,
                 t->count, t->runnable);
    }
    if (dock) {
        gfx_text_in(c, (gfx_rect_t){ PAD + 12, SP_FOOT_Y,
                                     (int16_t)(SP_DOCK_X - 8 - PAD - 12),
                                     16 },
                    count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_LEFT);
        ui_textkey_render(&t->tk, c);
        return;
    }
    gfx_text(c, PAD + 12, SP_FOOT_Y, TR(SP_UNVERIFIED_ALL), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - PAD - 12 - SP_COUNT_W),
                                 SP_FOOT_Y, SP_COUNT_W, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * One lens or the base of the stack light: a cylinder seen from the side.
 * Lit, the colour runs to a near-white band down the middle; unlit, the
 * same colour greyed and dark.  Both darken toward the edges, and a darker
 * line every 6 px is a rib of the lens.  Drawn row by row in runs of one
 * colour, since the panel's frame buffer is cached by rows.
 */
static void sp_cylinder(gfx_canvas_t *c, int x, int y, int w, int h,
                        gfx_color_t base, bool lit, bool ribs)
{
    gfx_color_t col[64];
    gfx_color_t rib[64];
    if (w > 64) {
        w = 64;
    }
    for (int i = 0; i < w; ++i) {
        /* 0 in the middle, 255 at either edge, in eight steps. */
        const int off = (2 * i + 1 - w < 0) ? w - 2 * i - 1 : 2 * i + 1 - w;
        const int d = ((off * 8 / (w + 1)) * 255) / 7;
        gfx_color_t k;
        if (d < 64) {
            k = gfx_lerp(base, GFX_WHITE, lit ? 120 : 24);
        } else {
            k = gfx_lerp(base, GFX_BLACK, (uint8_t)((d - 64) * 150 / 191));
        }
        col[i] = k;
        rib[i] = gfx_lerp(k, GFX_BLACK, 80);
    }
    for (int row = 0; row < h; ++row) {
        const bool dark = row == 0 || row == h - 1
                          || (ribs && row % 6 == 5);
        const gfx_color_t *line = dark ? rib : col;
        int run = 0;
        for (int i = 1; i <= w; ++i) {
            if (i == w || line[i] != line[run]) {
                gfx_hline(c, x + run, y + row, i - run, line[run]);
                run = i;
            }
        }
    }
}

/* A lens, lit or dark, with a glow around it when lit. */
static void sp_lens(gfx_canvas_t *c, int x, int y, gfx_color_t colour,
                    bool lit)
{
    const gfx_color_t panel = ui_theme_color(UI_C_PANEL);
    if (lit) {
        gfx_fill_rect(c, x - 4, y - 3, SP_TOWER_W + 8, SP_TOWER_LENS + 6,
                      gfx_lerp(panel, colour, 60));
        gfx_fill_rect(c, x - 2, y - 1, SP_TOWER_W + 4, SP_TOWER_LENS + 2,
                      gfx_lerp(panel, colour, 130));
        sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_LENS, colour, true, true);
        return;
    }
    /* Off: the colour greyed, then darkened. */
    const gfx_color_t off = gfx_lerp(gfx_lerp(colour, GFX_GREY(110), 150),
                                     GFX_BLACK, 150);
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_LENS, off, false, true);
}

/*
 * The stack light: red over green on a light grey base, as a signal tower
 * on a machine.  Green: the detector holds a beep.  Red: the run ended for
 * a reason esc_stick_reason_is_fault() names.
 */
static void sp_draw_tower(gfx_canvas_t *c, bool red, bool green)
{
    const int x = SP_TOWER_X;
    int y = SP_TOWER_Y;
    const gfx_color_t grey = GFX_GREY(196);
    sp_cylinder(c, x + 6, y, SP_TOWER_W - 12, SP_TOWER_CAP, GFX_GREY(90),
                false, false);
    y += SP_TOWER_CAP;
    sp_lens(c, x, y, ui_theme_color(UI_C_DANGER), red);
    y += SP_TOWER_LENS;
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_RING, grey, false, false);
    y += SP_TOWER_RING;
    sp_lens(c, x, y, ui_theme_color(UI_C_OK), green);
    y += SP_TOWER_LENS;
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_RING, grey, false, false);
    y += SP_TOWER_RING;
    sp_cylinder(c, x - 2, y, SP_TOWER_W + 4, SP_TOWER_BASE, grey, false,
                false);
    y += SP_TOWER_BASE;
    sp_cylinder(c, x - 8, y, SP_TOWER_W + 16, 4, GFX_GREY(160), false,
                false);
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
    case ESC_STICK_R_STOP:        return TR(SP_WHY_STOP);
    case ESC_STICK_R_BENCH_STOP:  return TR(SP_WHY_BENCH_STOP);
    case ESC_STICK_R_DISARMED:    return TR(SP_WHY_DISARMED);
    case ESC_STICK_R_LINK:        return TR(SP_WHY_LINK);
    case ESC_STICK_R_SUPPLY_OFF:  return TR(SP_WHY_SUPPLY_OFF);
    case ESC_STICK_R_SUPPLY_LOST: return TR(SP_WHY_SUPPLY_LOST);
    case ESC_STICK_R_STALE:       return TR(SP_WHY_STALE);
    case ESC_STICK_R_RATE:        return TR(SP_WHY_RATE);
    case ESC_STICK_R_NOT_ARMED:   return TR(SP_WHY_NOT_ARMED);
    case ESC_STICK_R_NO_POWER:    return TR(SP_WHY_NO_POWER);
    case ESC_STICK_R_NO_BEEPS:    return TR(SP_WHY_NO_BEEPS);
    case ESC_STICK_R_HIGH:        return TR(SP_WHY_HIGH);
    case ESC_STICK_R_TIMEOUT:     return TR(SP_WHY_TIMEOUT);
    case ESC_STICK_R_HAND: {
        static char buf[128];
        snprintf(buf, sizeof(buf), TR(SP_WHY_HAND),
                 (unsigned)(ESC_STICK_HAND_WAIT_MS / 1000u));
        return buf;
    }
    case ESC_STICK_R_SUPPLY_ON:   return TR(SP_WHY_SUPPLY_ON);
    case ESC_STICK_R_TOUCH:       return TR(SP_WHY_TOUCH);
    case ESC_STICK_R_USER:        return TR(SP_WHY_USER);
    case ESC_STICK_R_LEFT:        return TR(SP_WHY_LEFT);
    default:                      return "";
    }
}

/* The run's phase and its end in the language showing; the engine's
 * esc_stick_phase_text() and esc_stick_reason_text() are the English. */
static const char *sp_phase_text(esc_stick_phase_t ph)
{
    switch (ph) {
    case ESC_STICK_IDLE:    return TR(SP_PH_READY);
    case ESC_STICK_ARMING:  return TR(SP_PH_ARMING);
    case ESC_STICK_SIGNAL:  return TR(SP_PH_SIGNAL);
    case ESC_STICK_POWER:   return TR(SP_PH_POWER);
    case ESC_STICK_ENTRY:   return TR(SP_PH_ENTRY);
    case ESC_STICK_ITEMS:   return TR(SP_PH_ITEMS);
    case ESC_STICK_VALUES:  return TR(SP_PH_VALUES);
    case ESC_STICK_STORE:   return TR(SP_PH_STORE);
    case ESC_STICK_CYCLE:   return TR(SP_PH_CYCLE);
    case ESC_STICK_HAND_OFF: return TR(SP_PH_HAND_OFF);
    case ESC_STICK_HAND_ON: return TR(SP_PH_HAND_ON);
    case ESC_STICK_HAND_END: return TR(SP_PH_HAND_END);
    case ESC_STICK_OFF:     return TR(SP_PH_OFF);
    case ESC_STICK_DONE:    return TR(SP_PH_DONE);
    case ESC_STICK_ABORTED: return TR(SP_PH_ABORTED);
    }
    return "?";
}

static const char *sp_reason_text(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:        return "";
    case ESC_STICK_R_STOP:        return TR(SP_R_STOP);
    case ESC_STICK_R_BENCH_STOP:  return TR(SP_R_BENCH_STOP);
    case ESC_STICK_R_DISARMED:    return TR(SP_R_DISARMED);
    case ESC_STICK_R_LINK:        return TR(SP_R_LINK);
    case ESC_STICK_R_SUPPLY_OFF:  return TR(SP_R_SUPPLY_OFF);
    case ESC_STICK_R_SUPPLY_LOST: return TR(SP_R_SUPPLY_LOST);
    case ESC_STICK_R_STALE:       return TR(SP_R_STALE);
    case ESC_STICK_R_RATE:        return TR(SP_R_RATE);
    case ESC_STICK_R_NOT_ARMED:   return TR(SP_R_NOT_ARMED);
    case ESC_STICK_R_NO_POWER:    return TR(SP_R_NO_POWER);
    case ESC_STICK_R_SUPPLY_ON:   return TR(SP_R_SUPPLY_ON);
    case ESC_STICK_R_TOUCH:       return TR(SP_R_TOUCH);
    case ESC_STICK_R_NO_BEEPS:    return TR(SP_R_NO_BEEPS);
    case ESC_STICK_R_HIGH:        return TR(SP_R_HIGH);
    case ESC_STICK_R_TIMEOUT:     return TR(SP_R_TIMEOUT);
    case ESC_STICK_R_HAND:        return TR(SP_R_HAND);
    case ESC_STICK_R_USER:        return TR(SP_R_USER);
    case ESC_STICK_R_LEFT:        return TR(SP_R_LEFT);
    }
    return "?";
}

/* "3 Cutoff mode -> 2 hard cutoff" for a change. */
static void sp_change_text(const esc_stick_change_t *ch, char *out, size_t n)
{
    const esc_item_t *it = &s.st.p->items[ch->item];
    const esc_value_t *v = &it->values[ch->value];
    snprintf(out, n, "%u %s -> %u %s", (unsigned)it->number, it->name,
             (unsigned)v->number, v->name);
}

/*
 * "POWER-UP AT MID" for the positions the power-ups of @p n changes take,
 * in the order first met: "MAX, MID" where a one-stage run takes both.
 * False, and nothing written, when every one is MIN.
 */
static bool sp_power_up_text(const esc_profile_t *p,
                             const esc_stick_change_t *ch, size_t n,
                             char *out, size_t size)
{
    bool seen[ESC_THR_NONE + 1] = { false };
    char list[24] = "";
    bool other = false;
    for (size_t i = 0; i < n; ++i) {
        const esc_throttle_t at = esc_stick_change_entry(p, &ch[i]);
        if (seen[at]) {
            continue;
        }
        seen[at] = true;
        other = other || at != ESC_THR_MIN;
        const size_t len = strlen(list);
        snprintf(list + len, sizeof(list) - len, "%s%s",
                 (len > 0u) ? ", " : "", sp_pos(esc_stick_pct(at)));
    }
    if (!other) {
        return false;
    }
    snprintf(out, size, TR(SP_POWER_UP_AT), list);
    return true;
}

/* The longest hold of the profile's at_power_up steps, ms; 0 for none. */
static uint32_t sp_hold_ms(const esc_profile_t *p)
{
    uint32_t hold = 0u;
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        if (p->manual[i].when == ESC_MANUAL_AT_POWER_UP
            && p->manual[i].hold_ms > hold) {
            hold = p->manual[i].hold_ms;
        }
    }
    return hold;
}

static void sp_draw_progress(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    const gfx_color_t txt = ui_theme_color(UI_C_TEXT);
    char line[128];

    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_RUN), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    if (e->entry != ESC_THR_MIN) {
        snprintf(line, sizeof(line), TR(SP_POWER_UP_AT),
                 sp_pos(esc_stick_pct(e->entry)));
        gfx_text(c, PAD + 12 + gfx_text_cells(TR(SP_RUN)) * 8 + 24,
                 PARM_Y + 12, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
    gfx_text(c, PAD + 12, PARM_Y + 34, sp_phase_text(e->phase),
             UI_FONT_HEAD, txt, 1);
    /* The count beside the stack light. */
    snprintf(line, sizeof(line), "%u", esc_stick_beeps(e));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(SP_TOWER_X - 24 - 210),
                                 (int16_t)(PARM_Y + 14), 210, 34 },
                line, UI_FONT_NUM, ui_theme_color(UI_C_CURR), 1,
                GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(SP_TOWER_X - 24 - 210),
                                 (int16_t)(PARM_Y + 50), 210, 16 },
                TR(SP_BEEPS_GROUP), UI_FONT_LABEL,
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
    snprintf(line, sizeof(line), TR(SP_SELECTION),
             (done < e->n) ? done + 1u : (unsigned)e->n, (unsigned)e->n,
             what);
    sp_text(c, PAD + 12, y0, line, SP_LINE_CELLS, txt);

    switch (e->phase) {
    case ESC_STICK_ARMING:
        snprintf(line, sizeof(line), "%s", TR(SP_DO_ARMING));
        break;
    case ESC_STICK_SIGNAL:
        snprintf(line, sizeof(line), TR(SP_DO_SIGNAL),
                 sp_pos(e->out.throttle_pct),
                 (unsigned)((e->now_ms - e->phase_ms < ESC_STICK_SIGNAL_MS)
                                ? ESC_STICK_SIGNAL_MS
                                      - (e->now_ms - e->phase_ms)
                                : 0u));
        break;
    case ESC_STICK_POWER:
        snprintf(line, sizeof(line), TR(SP_DO_POWER),
                 (unsigned)(e->out.supply_mv / 1000u),
                 (unsigned)(e->out.supply_mv % 1000u / 10u));
        break;
    case ESC_STICK_ENTRY: {
        const uint32_t in = e->now_ms - e->on_ms;
        const uint32_t wait = e->entry_wait;
        const uint32_t left = (in < wait) ? wait - in : 0u;
        /* A button held while the supply came on: how long still, then
         * that it can go. */
        const uint32_t hold = sp_hold_ms(e->p);
        if (hold > in) {
            snprintf(line, sizeof(line), TR(SP_DO_HOLD),
                     (unsigned)((hold - in) / 1000u),
                     (unsigned)((hold - in) % 1000u / 100u));
        } else if (hold > 0u) {
            snprintf(line, sizeof(line), TR(SP_DO_LET_GO),
                     (unsigned)(left / 1000u),
                     (unsigned)(left % 1000u / 100u));
        } else {
            snprintf(line, sizeof(line), TR(SP_DO_ENTRY),
                     sp_pos(e->out.throttle_pct),
                     (unsigned)(left / 1000u),
                     (unsigned)(left % 1000u / 100u));
        }
        break;
    }
    case ESC_STICK_ITEMS:
        line[0] = '\0';
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                snprintf(line, sizeof(line), TR(SP_DO_ITEMS),
                         (unsigned)e->p->items[e->ch[i].item].number,
                         sp_pos(esc_stick_pct(e->p->select_throttle)));
                break;
            }
        }
        break;
    case ESC_STICK_VALUES: {
        const esc_stick_change_t *ch = &e->ch[e->active];
        snprintf(line, sizeof(line), TR(SP_DO_VALUES),
                 (unsigned)e->p->items[ch->item].values[ch->value].number,
                 sp_pos(esc_stick_pct(
                     (e->kind == ESC_STICK_KIND_TWO_STAGE)
                         ? e->p->value_select_throttle
                         : e->p->select_throttle)));
        break;
    }
    case ESC_STICK_STORE:
        if (e->store_step > 0u) {
            snprintf(line, sizeof(line), TR(SP_DO_STORE_MOVE),
                     sp_pos(e->out.throttle_pct),
                     (unsigned)e->t.store_ms);
        } else {
            snprintf(line, sizeof(line), TR(SP_DO_STORE_HOLD),
                     (unsigned)e->t.store_ms);
        }
        break;
    case ESC_STICK_CYCLE:
        snprintf(line, sizeof(line), TR(SP_DO_CYCLE), (unsigned)e->t.off_ms);
        break;
    case ESC_STICK_OFF:
        snprintf(line, sizeof(line), TR(SP_DO_OFF),
                 sp_pos(e->out.throttle_pct));
        break;
    default:
        line[0] = '\0';
        break;
    }
    sp_text(c, PAD + 12, y0 + pitch, line, SP_LINE_CELLS, dim);

    if (e->groups == 0u) {
        snprintf(line, sizeof(line), "%s", TR(SP_LAST_NONE));
    } else {
        snprintf(line, sizeof(line), TR(SP_LAST),
                 (unsigned)e->last_count,
                 !e->last_valid ? TR(SP_NOT_CLEAN)
                 : e->last_trusted ? TR(SP_TWO_IN_ROW)
                 : e->last_in_order ? TR(SP_IN_ORDER) : TR(SP_NOT_IN_ORDER));
    }
    gfx_text(c, PAD + 12, y0 + 2 * pitch, line, UI_FONT_LABEL, txt, 1);
    snprintf(line, sizeof(line), TR(SP_STATS), (unsigned)e->groups,
             (unsigned)e->entries, sp_pos(e->out.throttle_pct),
             ui_on_off(e->out.supply_on));
    gfx_text(c, PAD + 12, y0 + 3 * pitch, line, UI_FONT_LABEL, dim, 1);
    snprintf(line, sizeof(line), TR(SP_CURRENT), (int)e->ma,
             (int)esc_det_floor_ma(&e->det), (unsigned)e->iv_ms);
    gfx_text(c, PAD + 12, y0 + 4 * pitch, line, UI_FONT_LABEL, dim, 1);
    /* After the lines, so a line that reached under it would show as
     * painted over in the fit check. */
    sp_draw_tower(c, false, t->green);

    gfx_text(c, PAD + 12, HELP_Y + 9,
             TR(SP_ENDS_RUN), UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT),
             1);
    ui_button(c, s.write_btn, TR(SP_ABORT), ui_theme_color(UI_C_DANGER),
              false, true);
}

/* The red light: a result on screen, until OK, of a run that ended on
 * something not as expected. */
static bool sp_red(void)
{
    const esc_stick_t *e = &s.st.run;
    return s.st.shown && e->phase == ESC_STICK_ABORTED
           && esc_stick_reason_is_fault(e->reason);
}

static void sp_draw_result(gfx_canvas_t *c)
{
    const esc_stick_t *e = &s.st.run;
    const bool done = e->phase == ESC_STICK_DONE;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    char line[128];
    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_RESULT), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    snprintf(line, sizeof(line), "%s", TR(SP_PH_DONE));
    if (!done) {
        snprintf(line, sizeof(line), TR(SP_ABORTED_WHY),
                 sp_reason_text(e->reason));
    }
    gfx_text(c, PAD + 12, PARM_Y + 34, line, UI_FONT_HEAD,
             done ? ui_theme_color(UI_C_OK) : ui_theme_color(UI_C_DANGER),
             1);
    const int y0 = PARM_Y + 80;
    const int pitch = 22;
    snprintf(line, sizeof(line), TR(SP_MADE_N),
             esc_stick_done_count(e), (unsigned)e->n);
    gfx_text(c, PAD + 12, y0, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    /* Five lines fit; past five, the fifth says how many more. */
    const unsigned shown = (e->n > 5u) ? 4u : e->n;
    for (unsigned i = 0; i < shown; ++i) {
        char what[96];
        sp_change_text(&e->ch[i], what, sizeof(what));
        snprintf(line, sizeof(line), "%s  %s",
                 e->done[i] ? TR(SP_MADE) : TR(SP_NOT_MADE), what);
        sp_text(c, PAD + 12, y0 + (int)(i + 1u) * pitch, line,
                SP_LINE_CELLS,
                e->done[i] ? ui_theme_color(UI_C_TEXT) : dim);
    }
    if (shown < e->n) {
        unsigned made = 0u;
        for (unsigned i = shown; i < e->n; ++i) {
            made += e->done[i] ? 1u : 0u;
        }
        snprintf(line, sizeof(line), TR(SP_MORE),
                 (unsigned)(e->n - shown), made);
        gfx_text(c, PAD + 12, y0 + 5 * pitch, line, UI_FONT_LABEL, dim, 1);
    }
    /* Below the changes, two lines: what the profile has a person do once
     * the run is over, and after an abort that what was fitted before
     * the power-up may still be there.  Steps that need more than the two
     * lines are counted there instead, and MANUAL INTERVENTION REQUIRED
     * above -- open by itself when the run ended -- lists every one. */
    const esc_profile_t *p = e->p;
    if (p->manual_count > 0u) {
        ui_button(c, s.st.hand_btn, TR(SP_HAND_BTN),
                  ui_theme_color(UI_C_DANGER), false, true);
    }
    int y = y0 + 6 * pitch;
    int room = 2;
    int need = 0;
    if (p->manual_count > 0u && !sp_supply_reads_off()) {
        /* An abort asks the supply off; until it reads off, no hand at the
         * ESC for anything that follows. */
        sp_text(c, PAD + 12, y, TR(SP_HAND_OVER_LIVE), 93,
                ui_theme_color(UI_C_DANGER));
        y += pitch;
        room--;
    }
    char what[ESC_MANUAL_MAX][256];
    unsigned after = 0u;
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_AFTER_PROGRAMMING && after < ESC_MANUAL_MAX) {
            snprintf(what[after], sizeof(what[after]), "%s: %s",
                     sp_when_text(m), sp_action(m));
            need += sp_wrap_lines(what[after], 93, 2);
            ++after;
        }
    }
    if (need > room && room > 0) {
        char line2[128];
        snprintf(line2, sizeof(line2), TR(SP_HAND_AFTER_N), after);
        sp_text(c, PAD + 12, y, line2, 93, ui_theme_color(UI_C_WARN));
        y += pitch;
        room--;
    } else {
        for (unsigned i = 0; i < after; ++i) {
            const int n = sp_wrap(c, PAD + 12, y, pitch, what[i], 93, 2,
                                  ui_theme_color(UI_C_WARN));
            y += n * pitch;
            room -= n;
        }
    }
    if (!done && room > 0
        && (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u
            || esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP) > 0u)) {
        sp_text(c, PAD + 12, y, TR(SP_HAND_UNDO), 93,
                ui_theme_color(UI_C_WARN));
    }
    sp_draw_tower(c, sp_red(), false);
    if (done) {
        gfx_text(c, PAD + 12, HELP_Y,
                 TR(SP_TONES_1),
                 UI_FONT_LABEL, dim, 1);
        gfx_text(c, PAD + 12, HELP_Y + 18,
                 TR(SP_TONES_2), UI_FONT_LABEL, dim, 1);
    } else {
        gfx_text(c, PAD + 12, HELP_Y, sp_reason_help(e->reason),
                 UI_FONT_LABEL, dim, 1);
        /* The supply went off while the ESC was to confirm: a Kontronik
         * ESC takes that for programming broken off and locks itself. */
        if (esc_stick_lock_risk(e)) {
            gfx_text(c, PAD + 12, HELP_Y + 18, TR(SP_HAND_LOCK),
                     UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
        } else {
            gfx_text(c, PAD + 12, HELP_Y + 18, TR(SP_SAFE_NOW),
                     UI_FONT_LABEL, dim, 1);
        }
    }
    ui_button(c, s.write_btn, "OK", ui_theme_color(UI_C_PANEL_HI), false,
              true);
}

static void sp_setting_text(setting_id_t id, char *out, size_t n)
{
    if (id == SET_STICK_V && !(settings_get(id) > 0.0f)) {
        snprintf(out, n, "%s", TR(SP_FROM_PROFILE));
        return;
    }
    char v[24];
    ui_setting_value(id, v, sizeof(v));
    snprintf(out, n, "%s %s", v, settings_def(id)->unit);
}

static void sp_draw_timing(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD),
                             (int16_t)(PARM_Y + PARM_H - DEV_Y) },
            ui_theme_color(UI_C_PANEL));
    gfx_text(c, PAD + 12, DEV_Y + 12, TR(SP_TIMING_TITLE), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, PAD + 12, DEV_Y + 34,
             TR(SP_TIMING_NOTE), UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
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
        gfx_text(c, PAD + 12, y, ui_setting_label(id), UI_FONT_LABEL,
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
    const setting_id_t picked = k_sp_settings[t->tpicked];
    gfx_text(c, PAD + 12, HELP_Y, ui_setting_label(picked), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, PAD + 12, HELP_Y + 18, ui_setting_help(picked),
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, s.read_btn, TR(SP_DEFAULTS), ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, s.write_btn, TR(SUP_CLOSE), ui_theme_color(UI_C_PANEL_HI),
              false, true);
}

static void sp_draw_items(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    const int items = (int)p->item_count;
    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_CHANGE), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    if (p->manual_count > 0u) {
        ui_button(c, t->hand_btn, TR(SP_HAND_BTN),
                  ui_theme_color(UI_C_DANGER), false, true);
    }
    const int max_scroll = (items > ROWS_MAX) ? items - ROWS_MAX : 0;
    char count[40];
    snprintf(count, sizeof(count), TR(PG_RANGE_OF), t->iscroll + 1,
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
        const bool action = esc_stick_not_offered(it) != NULL;
        char v[64];
        if (action) {
            snprintf(v, sizeof(v), "%s", esc_stick_is_action(it)
                                             ? TR(SP_ACTION)
                                             : TR(SP_NOTHING_TO_CHOOSE));
        } else if (pick < 0) {
            snprintf(v, sizeof(v), "%s", TR(SP_KEEP));
        } else {
            snprintf(v, sizeof(v), "%u %s",
                     (unsigned)it->values[pick].number,
                     it->values[pick].name);
        }
        sp_cut(v, sizeof(v), CTRL_W / 8);
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
        snprintf(line, sizeof(line), TR(SP_ONLY_WHEN),
                 (unsigned)it->number, it->name, it->applies_when);
    } else if (it->applies_count > 0u) {
        snprintf(line, sizeof(line), TR(SP_ONLY_ON),
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
        const int w = snprintf(line + at, sizeof(line) - at, "%s%u %s%s%s",
                               (k > 0u) ? "   " : "",
                               (unsigned)it->values[k].number,
                               it->values[k].name,
                               it->values[k].is_default ? " " : "",
                               it->values[k].is_default
                                   ? TR(SP_DEFAULT_MARK) : "");
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
                (picked == 0u) ? TR(SP_PICK_VALUE) : TR(SP_UNVERIFIED),
                56, ui_theme_color(UI_C_TEXT_FAINT));
    }
    char run[32];
    if (picked > 0u) {
        snprintf(run, sizeof(run), TR(SP_RUN_N), (unsigned)picked);
    } else {
        snprintf(run, sizeof(run), "%s", TR(SP_RUN_BTN));
    }
    ui_button(c, s.write_btn, run,
              can ? ui_theme_color(UI_C_DANGER)
                  : ui_theme_color(UI_C_PANEL_HI), false, can);
}

/* " (" ... ")" around a word, as the line after a number carries it. */
static const char *sp_mark(ui_text_id_t id)
{
    static char buf[40];
    snprintf(buf, sizeof(buf), " %s", ui_tr(id));
    return buf;
}

/*
 * The entry time the page shows: during a run and on its result, the wait
 * of the run's power-up, kept after its change is made; else what the
 * first change picked would wait, or with none picked the entry's.
 */
static uint32_t sp_entry_shown(void)
{
    const stick_t *t = &s.st;
    if (esc_stick_running(&t->run) || t->shown) {
        return t->run.entry_wait;
    }
    esc_stick_timing_t tm;
    sp_timing(&tm);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    const size_t n = sp_changes(ch, &picked);
    return esc_stick_change_entry_ms(t->p, (n > 0u) ? &ch[0] : NULL, &tm);
}

static void sp_draw_device(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    char line[128];
    if (t->model >= 0 && (unsigned)t->model < p->model_count) {
        snprintf(line, sizeof(line), "ESC STICK  >  %s  >  %s  (%s)",
                 p->brand, p->models[t->model].name, p->family);
    } else {
        snprintf(line, sizeof(line), "ESC STICK  >  %s %s", p->brand,
                 p->family);
    }
    ui_button(c, s.back, TR(LOG_BACK), ui_theme_color(UI_C_PANEL_HI), false,
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
             (kind == ESC_STICK_KIND_TWO_STAGE) ? TR(SP_TWO_STAGE_MENU)
                                                : TR(SP_ONE_STAGE_MENU),
             (p->encoding == ESC_ENC_SHORT_LONG) ? TR(SP_SHORT_LONG)
                                                 : TR(SP_COUNTED),
             p->one_change_per_entry ? TR(SP_ONE_CHANGE)
                                     : TR(SP_MANY_CHANGES));
    gfx_text(c, PAD + 12, DEV_Y + 4, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    esc_stick_timing_t tm;
    sp_timing(&tm);
    if (esc_stick_running(&t->run) || t->shown) {
        mv = t->run.out.supply_mv;
        ma = t->run.out.supply_ma;
    }
    tm.entry_ms = sp_entry_shown();
    snprintf(line, sizeof(line), TR(SP_SUPPLY_ENTRY), (unsigned)(mv / 1000u),
             (unsigned)(mv % 1000u / 10u), (unsigned)(ma / 1000u),
             (unsigned)(ma % 1000u / 10u), (unsigned)(tm.entry_ms / 1000u),
             (unsigned)(tm.entry_ms % 1000u / 100u),
             (p->entry_hold_ms != 0u) ? sp_mark(TX_SP_FROM_PROFILE_MARK)
                                      : sp_mark(TX_SP_FROM_SETTING_MARK));
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
 * whole screen, saying what the run does to the ESC and what may happen to
 * a motor on it.
 */
/* A panel over the whole screen, edged and titled in the danger colour:
 * the warning's, the manual steps' and the prompt's.  Its area. */
static gfx_rect_t sp_panel(gfx_canvas_t *c, const char *title)
{
    const gfx_rect_t a = { PAD, PAD, (int16_t)(W - 2 * PAD),
                           (int16_t)(H - 2 * PAD) };
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_fill_rect(c, a.x, a.y, a.w, 52, red);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text_in(c, (gfx_rect_t){ a.x, (int16_t)(a.y + 12), a.w, 28 },
                title, UI_FONT_HEAD,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    return a;
}

/*
 * The before-power-up steps of the profile, under the warning's lines: the
 * hold that starts the run is the operator's word that they are done.
 * Then, where the run asks for more on its way, a line that it will.
 */
static void sp_draw_warning_hand(gfx_canvas_t *c, const esc_profile_t *p,
                                 gfx_rect_t a, int y)
{
    const int last = s.st.hold_btn.y - 20;     /* the last line's top */
    const unsigned later = sp_later_steps(p);
    if (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u
        && sp_warn_gated()) {
        /* No hand at the ESC until the supply reads off. */
        (void)sp_wrap(c, a.x + 20, y, 22, TR(SP_WARN_HAND_WAIT), 92, 2,
                      ui_theme_color(UI_C_DANGER));
        return;
    }
    if (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u) {
        gfx_text(c, a.x + 20, y, TR(SP_WARN_HAND), UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
        y += 22;
        if (!sp_warn_steps_fit(p)) {
            /* Not every step fits: none is shown cut, and ALL STEPS shows
             * them, as HOLD TO RUN needs. */
            char line[128];
            snprintf(line, sizeof(line), TR(SP_WARN_HAND_READ),
                     esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER));
            sp_text(c, a.x + 44, y, line, 90, ui_theme_color(UI_C_TEXT));
            y += 22;
        } else {
            for (unsigned i = 0; i < p->manual_count; ++i) {
                const esc_manual_t *m = &p->manual[i];
                if (m->when == ESC_MANUAL_BEFORE_POWER) {
                    y += 22 * sp_wrap(c, a.x + 44, y, 22, sp_action(m), 90,
                                      2, ui_theme_color(UI_C_TEXT));
                }
            }
        }
    }
    if (later > 0u && y <= last) {
        gfx_text(c, a.x + 20, y, TR(SP_WARN_HAND_LATER), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

static void sp_draw_warning(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    const gfx_rect_t a = sp_panel(c, TR(SP_WARN_TITLE));
    static const ui_text_id_t k_lines[] = {
        TX_SP_WARN_1, TX_SP_WARN_2, TX_SP_WARN_3, TX_SP_WARN_4,
        TX_SP_WARN_5,
    };
    const int n = (int)(sizeof(k_lines) / sizeof(k_lines[0]));
    for (int i = 0; i < n; ++i) {
        gfx_text(c, a.x + 20, a.y + 68 + i * 22, ui_tr(k_lines[i]),
                 UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
    }
    char line[128];
    /* A profile with manual steps gives up the blank line above its name
     * to the steps below. */
    const int top = a.y + 68 + ((t->p->manual_count > 0u) ? n : n + 1) * 22;
    snprintf(line, sizeof(line), "%s %s", t->p->brand, t->p->family);
    sp_text(c, a.x + 20, top, line, 90, ui_theme_color(UI_C_ACCENT));
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    snprintf(line, sizeof(line),
             (picked == 1u) ? TR(SP_WARN_SUPPLY) : TR(SP_WARN_SUPPLY_N),
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 10u),
             (unsigned)(ma / 1000u), (unsigned)(ma % 1000u / 10u),
             (unsigned)picked);
    gfx_text(c, a.x + 20, top + 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_VOLT), 1);
    /* Where the stick is when the supply comes on, beside the supply,
     * when it is not MIN: every position the run's power-ups take. */
    char at[40];
    if (sp_power_up_text(t->p, ch, picked < ESC_STICK_MAX_CHANGES
                                       ? picked : ESC_STICK_MAX_CHANGES,
                         at, sizeof(at))) {
        gfx_text(c, a.x + 20 + gfx_text_cells(line) * 8 + 24, top + 22, at,
                 UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    }
    gfx_text(c, a.x + 20, top + 44, TR(SP_WARN_UNVERIFIED),
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    sp_draw_warning_hand(c, t->p, a, top + 66);
    ui_button(c, t->hold_btn, TR(SP_HOLD_TO_RUN),
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), red,
                           t->hold.held_s),
              t->warn_down, !sp_hold_blocked());
    if (!sp_warn_steps_fit(t->p)) {
        ui_button(c, t->steps_btn, TR(SP_STEPS_BTN),
                  t->warn_read ? ui_theme_color(UI_C_PANEL_HI) : red, false,
                  true);
    }
    ui_button(c, t->cancel_btn, TR(CANCEL), ui_theme_color(UI_C_PANEL_SUNK),
              false, true);
}

/*
 * The manual steps of one profile: when each is due and what it is, and
 * whether a run asks for them.  Opened by MANUAL INTERVENTION REQUIRED,
 * by the first opening of the profile, or by a tap on a row the bench does
 * not run.
 */
static void sp_draw_hand(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->hand_p;
    const gfx_rect_t a = sp_panel(c, TR(SP_HAND_BTN));
    char line[256];
    snprintf(line, sizeof(line), "%s %s", p->brand, p->family);
    sp_text(c, a.x + 20, a.y + 64, line, 92, ui_theme_color(UI_C_ACCENT));
    /* ESC_MANUAL_MAX steps of a label and two lines each fit above the
     * buttons at a pitch of 19. */
    int y = a.y + 90;
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        snprintf(line, sizeof(line), "%u  %s", i + 1u, sp_when_text(m));
        gfx_text(c, a.x + 20, y, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
        y += 19;
        y += 19 * sp_wrap(c, a.x + 44, y, 19, sp_action(m), 90, 2,
                          ui_theme_color(UI_C_TEXT));
        y += 4;
    }
    char why[48];
    const char *no = sp_model_why(p, t->hand_model, why, sizeof(why));
    if (t->shown && t->p == p && !sp_supply_reads_off()) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_OVER_LIVE));
    } else if (t->shown && t->p == p) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_OVER));
    } else if (no == NULL) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_ASKS));
    } else {
        snprintf(line, sizeof(line), TR(SP_HAND_NOT_RUN), no);
    }
    sp_wrap(c, a.x + 20, t->cancel_btn.y + 8, 20, line,
            (t->cancel_btn.x - a.x - 36) / 8, 2,
            (t->shown && t->p == p && !sp_supply_reads_off())
                ? ui_theme_color(UI_C_DANGER)
                : ui_theme_color(UI_C_TEXT_DIM));
    ui_button(c, t->cancel_btn, "OK", ui_theme_color(UI_C_PANEL_HI), false,
              true);
}

/*
 * A run waiting for a manual step: what to do, where the supply and the
 * stick are, and how long it waits.  DONE goes on, ABORT ends the run;
 * STOP in the band ends it too.  DONE is dark for ESC_STICK_HAND_MIN_MS
 * after the step is asked.
 */
static void sp_draw_prompt(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const esc_manual_t *m = esc_stick_hand(e);
    const gfx_rect_t a = sp_panel(c, TR(SP_PROMPT_TITLE));
    char line[256];
    snprintf(line, sizeof(line), "%s %s", e->p->brand, e->p->family);
    sp_text(c, a.x + 20, a.y + 64, line, 92, ui_theme_color(UI_C_ACCENT));
    gfx_text(c, a.x + 20, a.y + 92, sp_when_text(m), UI_FONT_LABEL,
             ui_theme_color(UI_C_WARN), 1);
    int y = a.y + 118;
    y += 22 * sp_wrap(c, a.x + 20, y, 22, sp_action(m), 92, 3,
                      ui_theme_color(UI_C_TEXT));
    y += 12;
    const char *pos = sp_pos(e->out.throttle_pct);
    if (e->hand_menu) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_LISTEN), pos);
    } else if (e->phase == ESC_STICK_HAND_ON) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_ON), pos);
    } else if (e->phase == ESC_STICK_HAND_END) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_END), pos);
    } else if (m->when == ESC_MANUAL_AT_POWER_UP) {
        snprintf(line, sizeof(line), "%s", TR(SP_PROMPT_AT_POWER));
    } else {
        snprintf(line, sizeof(line), TR(SP_PROMPT_OFF), pos);
    }
    sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_DIM));
    y += 22;
    if (m->when == ESC_MANUAL_AT_POWER_UP && m->hold_ms != 0u) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_HOLD),
                 (unsigned)(m->hold_ms / 1000u),
                 (unsigned)(m->hold_ms % 1000u / 100u));
        sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_DIM));
        y += 22;
    }
    if (e->hand_menu) {
        sp_text(c, a.x + 20, y, TR(SP_PROMPT_HEARD), 92,
                ui_theme_color(UI_C_TEXT_DIM));
        y += 22;
    }
    /* Before the supply goes off, an end switches it off under the ESC's
     * confirmation: said here, as the result says it after. */
    snprintf(line, sizeof(line),
             (e->phase == ESC_STICK_HAND_END) ? TR(SP_PROMPT_LEFT_END)
                                              : TR(SP_PROMPT_LEFT),
             (unsigned)((esc_stick_hand_left_ms(e) + 999u) / 1000u));
    sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_FAINT));
    const bool ready = esc_stick_hand_ready(e);
    ui_button(c, t->hold_btn, TR(SP_HAND_DONE),
              ready ? ui_theme_color(UI_C_OK)
                    : ui_theme_color(UI_C_PANEL_SUNK), false, ready);
    ui_button(c, t->cancel_btn, TR(SP_ABORT), ui_theme_color(UI_C_DANGER),
              false, true);
}

static void sp_render(gfx_canvas_t *c)
{
    if (s.st.hand_open && s.st.hand_p != NULL) {
        sp_draw_hand(c);
        return;
    }
    if (s.stage == STAGE_DEVICE && s.st.p != NULL
        && esc_stick_hand(&s.st.run) != NULL) {
        sp_draw_prompt(c);
        return;
    }
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
                             uint32_t pressed, bool link_up)
{
    stick_t *t = &s.st;
    /* A stop ends a hold under way; the warning stays, and a new hold is
     * a new decision.  It ends a run too, with any ARM it queued and the
     * application has not taken: that one would be stamped with this
     * frame's count and clear the stop. */
    if (stops != t->stops) {
        sp_end_hold();
        sp_end_run(esc_stick_stop_reason(stops - t->stops,
                                         pressed - t->pressed));
    }
    t->now_ms = now_ms;
    t->armed = armed;
    t->stops = stops;
    t->pressed = pressed;
    t->link_up = link_up;
}

void programmer_screen_supply(const supply_state_t *st)
{
    stick_t *t = &s.st;
    if (st == NULL) {
        return;
    }
    /* Every reading, run or no run: whether the supply is live, and since
     * when it has read off -- its own state, the current at or under
     * ESC_STICK_OFF_MA. */
    const bool reads_off = st->online && !st->output
                           && st->mode == SUPPLY_MODE_OFF
                           && (st->ok & SUPPLY_OK_CURRENT) != 0u
                           && lroundf(st->i * 1000.0f) <= ESC_STICK_OFF_MA;
    t->sup_have = true;
    t->sup_at = st->taken_ms;
    t->sup_live = !reads_off && st->online;
    if (!reads_off) {
        t->sup_off_known = false;
    } else if (!t->sup_off_known) {
        t->sup_off_known = true;
        t->sup_off_since = st->taken_ms;
    }
    if (!esc_stick_running(&t->run)) {
        return;
    }
    const esc_stick_sample_t x = {
        .seq = st->samples,
        .at_ms = st->taken_ms,
        .ma = (int32_t)lroundf(st->i * 1000.0f),
        .current_ok = (st->ok & SUPPLY_OK_CURRENT) != 0u,
        /* What the panel asked, and apart from it what the supply reports:
         * the PD mini's output follows a link exchange and a module
         * transaction behind the request, on and off alike.  On counts
         * only when both say so, off only when the supply says so. */
        .output = st->output,
        .reported_on = st->online && st->mode != SUPPLY_MODE_OFF,
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

const char *programmer_screen_stick_search(void) { return s.st.find; }

bool programmer_screen_stick_typing(void) { return s.st.tk.open; }

int programmer_screen_stick_listed(int *top)
{
    if (top != NULL) {
        *top = s.st.scroll;
    }
    return s.st.count;
}

int programmer_screen_stick_level(void) { return s.st.level; }

const char *programmer_screen_stick_maker(void)
{
    return (s.st.level == 1 && s.st.maker != NULL) ? s.st.maker : "";
}

int programmer_screen_stick_model(void)
{
    return (s.klass == CLASS_STICK && s.stage == STAGE_DEVICE) ? s.st.model
                                                               : -1;
}

const esc_profile_t *programmer_screen_stick_row(int i, int *model)
{
    const stick_t *t = &s.st;
    if (t->level != 1 || i < 0 || i >= t->count) {
        return NULL;
    }
    if (model != NULL) {
        *model = t->rows_m[i].model;
    }
    return esc_profiles_at(t->rows_m[i].prof);
}

const char *programmer_screen_stick_maker_at(int i)
{
    const stick_t *t = &s.st;
    return (t->level == 0 && i >= 0 && i < t->count) ? t->makers[i].name
                                                     : NULL;
}

uint32_t programmer_screen_stick_entry_shown(void)
{
    return (s.st.p != NULL) ? sp_entry_shown() : 0u;
}

bool programmer_screen_stick_supply_reads_off(void)
{
    return sp_supply_reads_off();
}

const char *programmer_screen_stick_hand_why(void)
{
    const stick_t *t = &s.st;
    static char why[48];
    if (!t->hand_open || t->hand_p == NULL) {
        return NULL;
    }
    return sp_model_why(t->hand_p, t->hand_model, why, sizeof(why));
}

const esc_profile_t *programmer_screen_stick_page(void)
{
    return (s.klass == CLASS_STICK && s.stage == STAGE_DEVICE) ? s.st.p
                                                               : NULL;
}

bool programmer_screen_stick_hand_shown(void)
{
    return s.st.hand_open && s.st.hand_p != NULL;
}

void programmer_screen_stick_lights(bool *red, bool *green)
{
    if (red != NULL) {
        *red = sp_red();
    }
    if (green != NULL) {
        *green = esc_stick_running(&s.st.run) && s.st.green;
    }
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
    .enter  = sp_enter,
    .leave  = sp_leave,
    .tick   = sp_tick,
    .event  = event,
    .cancel = sp_cancel,
    .render = render,
};

const ui_screen_t *programmer_screen(void) { return &k_screen; }
