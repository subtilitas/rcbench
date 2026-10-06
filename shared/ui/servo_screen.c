/*
 * The servo bench screen.
 *
 * The horn is the control: dragging the drawn arm commands the servo, and
 * the arm is drawn at the measured position, so a servo that is slow, stuck
 * or fighting a linkage lags the finger by that much.
 *
 * SETTINGS, top right, opens the servo's settings over the left card: its
 * type, frame rate and pulse widths, the automatic test, its limits and the
 * device under test.  A type or a frame rate that can destroy a servo not
 * made for it -- a heli profile, anything above 60 Hz -- is applied only
 * after a warning held for two seconds, and is never kept past a restart.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "outputs.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD    6
#define LCARD_W 488
#define RCARD_X (PAD + LCARD_W + 8)
#define RCARD_W (W - RCARD_X - PAD)

/* The output shaft, which everything on the left is drawn around.  It sits
 * on the top face of the case, where a servo's output is. */
#define SHAFT_X 300
/* Integer division on purpose: this is a pixel row, and H is even. */
/* NOLINTNEXTLINE(bugprone-integer-division) */
#define SHAFT_Y ((H) / 2)          /* centred in the card, not under it */
#define ARC_R   140
#define HORN_L  92
#define BODY_W  260
#define BODY_H  88
/*
 * The mounting flanges run the full width of the case end, as on a servo.
 *
 * The three clearances are the chosen numbers.  The flange width is what
 * holds a bore with TAB_MARGIN of material outside it and TAB_INNER between
 * it and the case, and the hole positions follow from the same margin top
 * and bottom.
 */
#define TAB_HOLE_R  12
#define TAB_MARGIN  6                  /* material outside the bore     */
#define TAB_INNER   8                  /* between the bore and the case */
#define TAB_OVER    6                  /* how far the case laps over it */
#define TAB_W       (TAB_MARGIN + 2 * TAB_HOLE_R + TAB_INNER + TAB_OVER)
#define TAB_H       BODY_H
#define TAB_HOLE_DY (BODY_H / 2 - TAB_MARGIN - TAB_HOLE_R)

/*
 * Screen angle from servo angle.
 *
 * The case lies along the card with its output at the right-hand end; zero
 * is the arm pointing straight out, and positive is counter-clockwise, the
 * convention every other angle on the bench uses.
 */
#define PHI(a) (a)

/*
 * The right card, top to bottom: the readings, the type and frame rate in
 * force, the supply's live power, then the controls.
 */
#define RC_X      (RCARD_X + 12)
#define RC_W      (RCARD_W - 24)
#define SETB_W    96
#define TAG_Y     118
#define TAG_H     24
#define PWR_TXT_Y 150
#define PWR_Y     172
#define PWR_H     72

/*
 * The overlay: the settings and the keypad, keyboard, list and warning they
 * open all cover the left card.  The right card -- ARM, CENTRE, RELEASE and
 * the readings -- stays where it is and works.
 */
#define OV_X      PAD
#define OV_Y      PAD
#define OV_W      LCARD_W
#define OV_H      (H - 2 * PAD)
#define OV_TAB_Y  (OV_Y + 8)
#define OV_TAB_H  26
#define OV_TAB_W  320
#define OV_ROW0   (OV_Y + 48)
#define OV_PITCH  42
#define OV_ROW_H  34
#define OV_COL_W  ((OV_W - 30) / 2)
#define OV_VAL_W  100
#define OV_NOTE_Y (OV_ROW0 + 5 * OV_PITCH + 6)

/* The sample rate of the panel's loop, which is the power plot's time base. */
#define SAMPLE_HZ 20.0f

/*
 * A pulse needs a pause before the next frame.  STANDARD PWM, WIDE and
 * NARROW 760 keep one of at least 1 ms -- a 100 % margin on the 1 ms a
 * standard servo's own decoder needs -- so their fastest frame rate is
 * 1 / (longest pulse + 1 ms): 333 Hz for 2000 us.  The heli profiles run
 * at their published rates, which leave less, so theirs is set by the
 * profile and a pause of at least 0.5 ms.
 */
#define PWM_MIN_PAUSE_US  1000u
#define HELI_MIN_PAUSE_US 500u
/* Above this a frame rate needs the warning: an analogue servo overheats. */
#define SAFE_RATE_HZ      60u

/*
 * The profiles the bench drives.  STANDARD PWM is the safe one and the one
 * every restart starts at.  The heli profiles are Rotorflight's for digital
 * cyclic and narrow-band tail servos: 1520 us centre, +/-700 us, up to
 * 333 Hz; and 760 us centre, +/-350 us, up to 560 Hz.
 */
typedef struct {
    const char     *name;
    uint16_t        min_us, centre_us, max_us;
    const uint16_t *rates;       /* the frame rates offered, Hz           */
    uint8_t         rate_count;
    uint16_t        default_hz;
    uint16_t        max_hz;      /* the profile's ceiling; 0 is the pause */
    bool            heli;        /* choosing it needs the warning          */
} servo_type_t;

static const uint16_t k_std_rates[]  = { 50, 60, 100, 150, 200, 250, 300, 333 };
static const uint16_t k_cyc_rates[]  = { 50, 120, 200, 333 };
static const uint16_t k_tail_rates[] = { 200, 333, 560 };

#define RATES(a) (a), (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const servo_type_t k_types[] = {
    { "STANDARD PWM",  1000, 1500, 2000, RATES(k_std_rates),  50,  0,   false },
    { "NARROW 760",    660,  760,  860,  RATES(k_std_rates),  50,  0,   false },
    { "WIDE",          800,  1500, 2200, RATES(k_std_rates),  50,  0,   false },
    { "HELI CYCLIC",   820,  1520, 2220, RATES(k_cyc_rates),  333, 333, true  },
    { "HELI TAIL 760", 410,  760,  1110, RATES(k_tail_rates), 560, 560, true  },
};
#define TYPE_COUNT ((int)(sizeof(k_types) / sizeof(k_types[0])))

/* The overlay's pages. */
enum { PG_OUTPUT = 0, PG_TEST, PG_LIMITS, PG_DUT, PG_COUNT };
static const char *const k_pages[PG_COUNT] = { "OUTPUT", "TEST", "LIMITS",
                                               "DUT" };

/* What a settings row edits. */
enum { R_TYPE = 0, R_RATE, R_MIN, R_CENTRE, R_MAX, R_TRIM, R_TRAVEL,
       R_REVERSE, R_SETTING, R_TEXT };

typedef struct {
    uint8_t      page;
    uint8_t      kind;
    setting_id_t id;          /* R_SETTING                              */
    const char  *label;
    uint8_t      col, row;
    bool         wide;        /* the value takes both columns' width    */
} ov_row_t;

static const ov_row_t k_rows[] = {
    { PG_OUTPUT, R_TYPE,    SETTING_COUNT, "TYPE",          0, 0, true  },
    { PG_OUTPUT, R_RATE,    SETTING_COUNT, "FRAME RATE",    0, 1, true  },
    { PG_OUTPUT, R_MIN,     SETTING_COUNT, "PULSE MIN",     0, 2, false },
    { PG_OUTPUT, R_CENTRE,  SETTING_COUNT, "PULSE CENTRE",  1, 2, false },
    { PG_OUTPUT, R_MAX,     SETTING_COUNT, "PULSE MAX",     0, 3, false },
    { PG_OUTPUT, R_TRIM,    SETTING_COUNT, "TRIM",          1, 3, false },
    { PG_OUTPUT, R_TRAVEL,  SETTING_COUNT, "TRAVEL",        0, 4, false },
    { PG_OUTPUT, R_REVERSE, SETTING_COUNT, "REVERSE",       1, 4, false },

    { PG_TEST, R_SETTING, SET_SERVO_CURVE,       "CURVE",      0, 0, false },
    { PG_TEST, R_SETTING, SET_SERVO_TEST_HZ,     "SPEED",      0, 1, false },
    { PG_TEST, R_SETTING, SET_SERVO_TEST_RANGE,  "RANGE",      0, 2, false },
    { PG_TEST, R_SETTING, SET_SERVO_LEN_BY,      "LENGTH BY",  0, 3, false },
    { PG_TEST, R_SETTING, SET_SERVO_LEN_S,       "TEST TIME",  0, 4, false },
    { PG_TEST, R_SETTING, SET_SERVO_LEN_MOVES,   "MOVEMENTS",  0, 5, false },
    { PG_TEST, R_SETTING, SET_SERVO_DWELL_MS,    "DWELL",      0, 6, false },
    { PG_TEST, R_SETTING, SET_SERVO_SETTLE_MS,   "SETTLE",     0, 7, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_48,     "STEP 4.8 V", 1, 0, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_60,     "STEP 6.0 V", 1, 1, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_74,     "STEP 7.4 V", 1, 2, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_84,     "STEP 8.4 V", 1, 3, false },
    { PG_TEST, R_SETTING, SET_SERVO_BROWNOUT,    "BROWN-OUT",  1, 4, false },

    { PG_LIMITS, R_SETTING, SET_SUPPLY_V_MAX,        "VOLTAGE MAX",  0, 0, false },
    { PG_LIMITS, R_SETTING, SET_SUPPLY_I_MAX,        "CURRENT MAX",  0, 1, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_STALL_A,       "STALL AT",     0, 2, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_IDLE_MAX,      "IDLE CURRENT", 1, 0, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_HOLD_MAX,      "HOLD CURRENT", 1, 1, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_TRAVEL_MAX_MS, "TRAVEL TIME",  1, 2, false },

    { PG_DUT, R_TEXT,    SETTING_COUNT,    "NAME",   0, 0, true  },
    { PG_DUT, R_SETTING, SET_SERVO_REPORT, "REPORT", 0, 1, false },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

/* The power plot's series: the supply's voltage, current and power. */
enum { PS_V = 0, PS_A, PS_W, PS_COUNT };
static const ui_plot_series_t k_power[PS_COUNT] = {
    { "V", "V", 0, 2, 5.0f, 0 },
    { "A", "A", 0, 2, 0.5f, 0 },
    { "W", "W", 0, 1, 5.0f, 0 },
};

/* What a press is on, in the overlay and the panels it opens. */
enum { OP_NONE = 0, OP_TAB, OP_CLOSE, OP_ROW, OP_TRIM_DN, OP_TRIM_UP,
       OP_KEYPAD, OP_TEXT, OP_CHOICE, OP_CHOICE_CANCEL, OP_WARN_APPLY,
       OP_WARN_CANCEL, OP_SETTINGS };

/* What the list chooses, and what the keypad types. */
enum { CH_NONE = 0, CH_TYPE, CH_RATE, CH_ENUM };
enum { KT_NONE = 0, KT_MIN, KT_CENTRE, KT_MAX, KT_TRAVEL, KT_RATE,
       KT_SETTING };

#define CHOICE_MAX 10

static struct {
    /* The profile in force, for this session only: STANDARD PWM at 50 Hz
     * at every restart. */
    int      type;
    uint16_t min_us, centre_us, max_us;
    uint16_t frame_hz;
    bool     reverse;
    int16_t  trim_us;      /**< added to centre                        */
    float    travel_deg;   /**< how far each way the horn is allowed   */
    int      speed_pct;    /**< how fast the bench slews the command   */

    float    commanded_deg;
    float    shown_deg;    /**< what the horn is drawn at              */
    float    shown_cmd;    /**< the same, as the far end's command     */
    float    measured_deg;
    float    current_a;
    bool     have_feedback;

    bool     dragging;
    int      drag_id;

    int      shown_q_deg; /**< the position as drawn, in tenths          */
    int      shown_q_a;   /**< the current as drawn, in hundredths        */
    bool     driving;     /**< the output is being held somewhere */
    float    pulse;       /**< phase of the grip's breathing      */
    int      drawn_pulse[2];

    servo_cmd_t pending;

    /*
     * Arming, which this screen needs as much as MOTOR & ESC does: until the
     * bench is armed the coprocessor writes a pulse of length zero to every
     * PWM pin, so dragging the horn moves nothing and shows nothing on a
     * scope.  The gesture is ui_widgets' -- the same two seconds and the same
     * fade as the other screen's, because it is the same control.
     */
    bool       armed;
    ui_hold_t  arm;
    bool       arm_down;   /**< a press is on the ARM button          */
    int        arm_id;     /**< which contact it is                   */
    uint32_t   arm_rev;
    uint32_t   drawn_arm[2];

    gfx_rect_t arm_btn, centre_btn, sweep_btn, release_btn, set_btn;
    ui_slider_t speed;

    /* The supply's live power, beside the servo it feeds. */
    ui_plot_t      power;
    supply_state_t sup;
    bool           have_sup;
    uint32_t       power_rev;
    uint32_t       drawn_power[2];

    /* The overlay and what it opens. */
    bool         ov_open;
    ui_tabs_t    tabs;
    int          ov_pressed;
    int          ov_row;
    uint8_t      ov_id;
    bool         ov_have;
    ui_keypad_t  kp;
    int          kp_target;
    setting_id_t kp_setting;
    ui_textkey_t tk;
    struct {
        bool  open;
        int   target;
        setting_id_t id;
        char  title[24];
        char  labels[CHOICE_MAX][20];
        int   values[CHOICE_MAX];
        int   count;
        int   pressed;
    } ch;
    struct {
        bool      open;
        int       type;
        uint16_t  hz;
        ui_hold_t hold;
        bool      down;
        uint8_t   id;
        uint32_t  rev;
    } warn;
    uint32_t drawn_warn[2];
    uint8_t  drawn_save[2];

    servo_rate_state_t rate_st;   /* what became of the rate rate_hz    */
    uint16_t           rate_hz;

    /* The sweep: the same curve the coprocessor runs, on this screen's own
     * clock, to draw the horn by. */
    bool        sweep_able;      /* the coprocessor speaks 4.2 or later  */
    bool        sweeping;
    sweep_t     sw;
    uint32_t    clock_ms;
    float       clock_frac_ms;

    /* Changes to what a command carries -- the profile, the pulses, trim,
     * travel -- and how many there had been when ARM was asked for. */
    uint32_t profile_rev;
    uint32_t arm_profile_rev;
    /* An ARM posted whose arm has not landed yet: collected by the panel,
     * which arms only once the release it owes has been written. */
    bool     arm_in_flight;

    uint32_t ctrl_rev;
    uint32_t drawn_ctrl[2];
    unsigned drawn_mask;
} s;

/* ------------------------------------------------------------- conversions */

static const servo_type_t *type(void) { return &k_types[s.type]; }

/*
 * Each side of centre runs to its own end: MIN, CENTRE and MAX are set one
 * by one, so -90 deg is MIN and +90 deg is MAX however far each lies from
 * the centre.
 */
static float half_travel_us(bool below)
{
    return below ? (float)s.centre_us - (float)s.min_us
                 : (float)s.max_us - (float)s.centre_us;
}

static float deg_to_us_f(float deg)
{
    const float d = s.reverse ? -deg : deg;
    float us = (float)s.centre_us + (float)s.trim_us
               + d / 90.0f * half_travel_us(d < 0.0f);
    if (us < (float)s.min_us) { us = (float)s.min_us; }
    if (us > (float)s.max_us) { us = (float)s.max_us; }
    return us;
}

static uint16_t deg_to_us(float deg)
{
    return (uint16_t)(deg_to_us_f(deg) + 0.5f);
}

static float us_to_deg_f(float us)
{
    const float off = us - (float)s.centre_us - (float)s.trim_us;
    const float half = half_travel_us(off < 0.0f);
    if (half <= 0.0f) {
        return 0.0f;
    }
    const float d = off * 90.0f / half;
    return s.reverse ? -d : d;
}

static float us_to_deg(uint16_t us)
{
    return us_to_deg_f((float)us);
}

/*
 * The range a command carries: the narrowest one centred on CENTRE that
 * holds MIN and MAX.  The coprocessor knows a channel only by its two
 * endpoints and rests a surface at their midpoint -- on RELEASE, before an
 * arm, after a silence -- so MIN..MAX itself would rest a servo whose CENTRE
 * is not midway between them away from its centre.  Commands never leave
 * MIN..MAX: deg_to_us() clamps them.  The PULSE keypads keep this range
 * inside OUT_FLOOR_US..OUT_CEILING_US.
 */
static void cmd_range(uint16_t *lo, uint16_t *hi)
{
    const unsigned below = (unsigned)s.centre_us - (unsigned)s.min_us;
    const unsigned above = (unsigned)s.max_us - (unsigned)s.centre_us;
    const unsigned half  = (below > above) ? below : above;
    *lo = (uint16_t)((unsigned)s.centre_us - half);
    *hi = (uint16_t)((unsigned)s.centre_us + half);
}

/* A pulse as the far end's command, 0..OUT_SPAN of the range a command
 * carries, and back: the units it slews in. */
static float us_to_cmd(float us)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (hi > lo) ? (us - (float)lo) * (float)OUT_SPAN / (float)(hi - lo)
                     : (float)OUT_SPAN / 2.0f;
}

static float cmd_to_us(float cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (float)lo + (float)(hi - lo) * cmd / (float)OUT_SPAN;
}

/*
 * The longest pulse the far end can render: the top of the range a command
 * carries, which is past PULSE MAX when CENTRE is off the middle.  A pulse
 * of an old span under a new range reaches it between two transactions, so
 * the frame rate's pause is kept from it rather than from PULSE MAX.
 */
static uint16_t cmd_top(void)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return hi;
}

static float clamp_travel(float deg)
{
    if (deg < -s.travel_deg) { return -s.travel_deg; }
    if (deg >  s.travel_deg) { return  s.travel_deg; }
    return deg;
}

/* The fastest frame rate the profile in force allows with these pulses:
 * the pause rule, under the profile's own ceiling. */
static uint16_t max_rate_for(int t, uint16_t max_us)
{
    const servo_type_t *ty = &k_types[t];
    const unsigned pause = ty->heli ? HELI_MIN_PAUSE_US : PWM_MIN_PAUSE_US;
    unsigned hz = 1000000u / ((unsigned)max_us + pause);
    if (ty->max_hz != 0u && hz > ty->max_hz) {
        hz = ty->max_hz;
    }
    return (uint16_t)hz;
}

/* A type or a rate that needs the warning before it is applied. */
static bool dangerous(int t, uint16_t hz)
{
    return k_types[t].heli || hz > SAFE_RATE_HZ;
}

static bool in_force_dangerous(void)
{
    return dangerous(s.type, s.frame_hz);
}

/*
 * SPEED as the bench's slew, in channel-span units a second.
 *
 * The horn's full travel is 180 degrees and a channel's span covers it, so
 * the 360 degrees a second the drawing uses at 100% is two spans a second:
 * SPEED_FULL_SPAN_S.  Below 100% the bench ramps the command at that
 * fraction of it, and a servo asked for 30% takes three times as long to
 * cross as one asked for 90%.
 *
 * 100% is immediate rather than two spans a second.  It is the value the
 * screen starts at, so anybody who never touches the slider gets what they
 * got before -- the servo at its own rate, with nothing in front of it.
 */
#define SPEED_FULL_SPAN_S (2u * OUT_SPAN)

static uint16_t slew_of(int pct)
{
    if (pct >= 100) {
        return 0u;
    }
    if (pct < 1) {
        pct = 1;
    }
    return (uint16_t)((unsigned)SPEED_FULL_SPAN_S * (unsigned)pct / 100u);
}

static void post(servo_cmd_kind_t kind, uint16_t us)
{
    /*
     * A pending disarm survives everything.  One command is held at a time,
     * so a position landing on top of a disarm would drive a bench somebody
     * has just asked to stop.
     */
    if (s.pending.kind == SERVO_CMD_DISARM && kind != SERVO_CMD_DISARM) {
        return;
    }
    /*
     * And an arm that has completed survives everything but a disarm.  The
     * application drains these between frames, so a hold that finishes in
     * tick() waits a frame to be read; a touch landing in that gap would
     * throw away two seconds of gesture and leave the bench unarmed with
     * nothing on screen to say why.
     */
    if (s.pending.kind == SERVO_CMD_ARM && kind != SERVO_CMD_DISARM
        && kind != SERVO_CMD_ARM) {
        return;
    }
    s.pending.kind     = kind;
    s.pending.value_us = us;
    if (kind == SERVO_CMD_ARM) {
        s.arm_profile_rev = s.profile_rev;
        s.arm_in_flight   = true;
    } else if (kind == SERVO_CMD_DISARM) {
        s.arm_in_flight = false;
    }
    /* The range travels with the pulse: the panel configures the channel
     * from it, and a narrow servo's 760 us centre is below a standard
     * servo's floor.  The frame rate goes with it. */
    cmd_range(&s.pending.min_us, &s.pending.max_us);
    s.pending.frame_hz = s.frame_hz;
    s.pending.slew_per_s = slew_of(s.speed_pct);
    /* The grip only breathes while something is actually being held, so this
     * has to follow the command rather than the screen being open. */
    s.driving = (kind == SERVO_CMD_POSITION || kind == SERVO_CMD_CENTRE
                 || kind == SERVO_CMD_SWEEP);
    const bool sw = kind == SERVO_CMD_SWEEP;
    s.pending.sweep_kind     = sw ? (uint16_t)s.sw.cfg.kind : 0u;
    s.pending.sweep_mhz      = sw ? s.sw.cfg.mhz : 0u;
    s.pending.sweep_span     = sw ? s.sw.cfg.amplitude : 0u;
    s.pending.sweep_dwell_ms = sw ? s.sw.cfg.dwell_ms : 0u;
}

/* ------------------------------------------------------------------- sweep */

/*
 * The sweep the TEST page describes, under the profile in force: CURVE,
 * SPEED and DWELL as they are, and RANGE as a share of the travel the
 * servo may make -- TRAVEL, and the nearer of PULSE MIN and MAX, so a sweep
 * about the centre reaches neither end it may not.  In the command units of
 * the range a command carries (cmd_range()), centred on PULSE CENTRE.  Trim
 * is not applied: the sweep turns about the centre a release rests at.
 */
static sweep_cfg_t sweep_cfg_now(void)
{
    sweep_cfg_t c;
    c.kind = (sweep_kind_t)(settings_get_int(SET_SERVO_CURVE) + 1);
    float mhz = settings_get(SET_SERVO_TEST_HZ) * 1000.0f;
    if (mhz < (float)SWEEP_MHZ_MIN) { mhz = (float)SWEEP_MHZ_MIN; }
    if (mhz > (float)SWEEP_MHZ_MAX) { mhz = (float)SWEEP_MHZ_MAX; }
    c.mhz = (uint16_t)(mhz + 0.5f);
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float half = (float)(hi - lo) * 0.5f;
    const float below = (float)s.centre_us - (float)s.min_us;
    const float above = (float)s.max_us - (float)s.centre_us;
    const float nearer = (below < above) ? below : above;
    float amp = 0.0f;
    if (half > 0.0f) {
        amp = settings_get(SET_SERVO_TEST_RANGE) / 100.0f
              * (s.travel_deg / 90.0f) * (nearer / half)
              * (float)SWEEP_AMPLITUDE_MAX;
    }
    if (amp > (float)SWEEP_AMPLITUDE_MAX) { amp = (float)SWEEP_AMPLITUDE_MAX; }
    c.amplitude = (uint16_t)(amp + 0.5f);
    int dwell = settings_get_int(SET_SERVO_DWELL_MS);
    if (dwell > (int)SWEEP_DWELL_MAX_MS) { dwell = (int)SWEEP_DWELL_MAX_MS; }
    c.dwell_ms = (uint16_t)((dwell > 0) ? dwell : 0);
    c.moves = 0u;     /* until it is stopped */
    return c;
}

/* A command of the sweep, as the angle it puts the horn at. */
static float sweep_deg(uint16_t cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float us = (float)lo + (float)(hi - lo) * (float)cmd
                                 / (float)(2u * SWEEP_CENTRE);
    return us_to_deg((uint16_t)(us + 0.5f));
}

static void stop_sweep(void)
{
    if (s.sweeping) {
        s.sweeping = false;
        ++s.ctrl_rev;
    }
}

/*
 * Start the sweep, or carry on with a changed one from its beginning, as the
 * coprocessor does.  Only on an armed bench and a coprocessor that sweeps:
 * the far end refuses one otherwise.
 */
static void start_sweep(void)
{
    if (!s.armed || !s.sweep_able) {
        return;
    }
    const sweep_cfg_t cfg = sweep_cfg_now();
    if (!sweep_start(&s.sw, &cfg, s.clock_ms)) {
        return;
    }
    s.sweeping = true;
    post(SERVO_CMD_SWEEP, 0);
    ++s.ctrl_rev;
}

static bool same_sweep(const sweep_cfg_t *a, const sweep_cfg_t *b)
{
    return a->kind == b->kind && a->mhz == b->mhz
           && a->amplitude == b->amplitude && a->dwell_ms == b->dwell_ms;
}

bool servo_screen_sweeping(void) { return s.sweeping; }

void servo_screen_sweep_started(uint32_t age_ms)
{
    /* The far end's curve began age_ms ago: this one is drawn from then,
     * rather than from the tap that asked for it a queue and a few
     * transactions earlier. */
    if (s.sweeping) {
        s.sw.start_ms = s.clock_ms - age_ms;
    }
}

void servo_screen_set_sweep(bool able)
{
    if (able == s.sweep_able) {
        return;
    }
    s.sweep_able = able;
    if (!able && s.sweeping) {
        /* The panel would go on repeating a sweep the coprocessor no longer
         * takes: what it holds is ended, and the surfaces rest. */
        stop_sweep();
        post(SERVO_CMD_RELEASE, 0);
    }
    stop_sweep();
    ++s.ctrl_rev;
}

/*
 * Say the position again under a mapping that has just changed.
 *
 * The pulse a command carries is the angle put through the profile, the
 * trim, the reverse and the travel; change any of them while an output is
 * held and the pulse on the pin belongs to the old one.  Switching a held
 * servo from STANDARD PWM to NARROW 760 would otherwise leave 1500 us on a
 * servo whose maximum is 860 while the screen shows the new range.
 */
static void reissue(void)
{
    ++s.profile_rev;
    if (s.pending.kind == SERVO_CMD_ARM) {
        /*
         * An arm asked for and not yet collected carries the profile it was
         * asked under, and the panel centres the surfaces under it before it
         * arms: it carries the one in force instead.  One already collected
         * is answered when the arm lands; see servo_screen_set_armed().
         */
        cmd_range(&s.pending.min_us, &s.pending.max_us);
        s.pending.frame_hz   = s.frame_hz;
        s.pending.slew_per_s = slew_of(s.speed_pct);
        s.arm_profile_rev    = s.profile_rev;
        return;
    }
    if (s.sweeping) {
        /*
         * Said again under the profile now in force -- its range and its
         * frame rate, which a changed type or rate moves even where the
         * curve in command units stays the same.  A curve that did change
         * is started over by tick(); one that did not carries on, here and
         * at the far end.
         */
        const sweep_cfg_t now = sweep_cfg_now();
        if (same_sweep(&now, &s.sw.cfg)) {
            post(SERVO_CMD_SWEEP, 0);
        } else {
            /* The curve changed with the range -- MIN, CENTRE, MAX or
             * TRAVEL move its amplitude -- so it starts over now, rather
             * than going out once with the old amplitude under the new
             * range before tick() catches it. */
            stop_sweep();
            start_sweep();
        }
        return;
    }
    if (s.driving) {
        post(SERVO_CMD_POSITION, deg_to_us(s.commanded_deg));
    } else if (s.armed || s.arm_in_flight) {
        /*
         * Resting on an armed bench: the rest restated under the profile now
         * in force, its range and its frame rate, which the panel orders so
         * a faster rate never meets wider pulses.  Left alone, a narrow
         * servo would rest at a standard servo's centre, past its stop, and
         * a servo would go on running at a heli rate after STANDARD PWM was
         * chosen.
         *
         * An arm on its way is answered the same way, at once: the panel
         * takes commands in order and arms only once the release it owes is
         * written, so a release that reaches it before the arm lands is the
         * profile the pins arm under.
         */
        post(SERVO_CMD_RELEASE, 0);
        s.arm_profile_rev = s.profile_rev;
    }
}

static void command(float deg)
{
    s.commanded_deg = clamp_travel(deg);
    post(SERVO_CMD_POSITION, deg_to_us(s.commanded_deg));
    ++s.ctrl_rev;
}

void servo_screen_set_armed(bool armed)
{
    if (s.armed == armed) {
        return;
    }
    s.armed = armed;
    s.arm_in_flight = false;
    /*
     * Nothing is being held across this edge, in either direction.  An arm
     * starts from nothing -- the panel drops the position and the slot on the
     * way through -- and a disarm holds nothing by definition.  A screen that
     * went on believing it was driving would say a discarded position again
     * on the next change of profile, trim or travel, onto a bench that is
     * now armed.
     */
    s.driving = false;
    stop_sweep();
    if (armed) {
        ui_hold_reached(&s.arm);
        /*
         * The profile changed after ARM was asked for and nothing answered
         * it yet: the rest is restated under the profile in force, its
         * range and rate in the panel's order.  reissue() answers a change
         * while the arm is on its way, so this is the backstop.
         */
        if (s.profile_rev != s.arm_profile_rev) {
            post(SERVO_CMD_RELEASE, 0);
            s.arm_profile_rev = s.profile_rev;
        }
    } else {
        /*
         * Disarmed, however it happened -- this screen's button, a STOP, a
         * dead touch, or the far end.  Nothing is being held any more: the
         * rings must stop pulsing, and a change to the profile, the trim or
         * the travel must not say a position again and rebuild a command
         * the stop had just released.
         */
        if (ui_hold_left(&s.arm)) {
            /* The bench disarmed under a finger still down on the button, and
             * the hold ended with it; see ui_hold_left(). */
            s.arm_down = false;
        }
    }
    ++s.arm_rev;
    ++s.ctrl_rev;
}

void servo_screen_cancel_arm(void)
{
    /*
     * A stop latched, so a hold under way is abandoned and an arm it has
     * already produced is dropped.
     *
     * The command is dealt with first and on its own account.  A hold that
     * completed and whose finger has since lifted leaves nothing held and
     * nothing counting -- the release cleared both -- while its arm is still
     * waiting to be read, and that is precisely the one that would be
     * forwarded a frame later and clear the latch the stop had just set.
     */
    bool changed = false;
    s.arm_in_flight = false;   /* the stop ends the arm on its way too */
    if (s.pending.kind == SERVO_CMD_ARM) {
        s.pending.kind = SERVO_CMD_NONE;
        changed = true;
    }
    if (s.arm_down || s.arm.held_s > 0.0f) {
        ui_hold_reset(&s.arm);
        s.arm_down = false;
        changed = true;
    }
    if (changed) {
        ++s.arm_rev;
    }
    /*
     * And nothing is being held any more.  The armed state need not have
     * moved -- a bench that was not armed is stopped just the same, and the
     * panel centres the surface either way -- so this cannot wait for that
     * edge: the rings would go on pulsing, and the next change of profile,
     * trim or travel would say the released position again.
     */
    if (s.driving) {
        s.driving = false;
        ++s.ctrl_rev;
    }
    stop_sweep();
}

bool servo_screen_take(servo_cmd_t *out)
{
    if (out == NULL || s.pending.kind == SERVO_CMD_NONE) {
        return false;
    }
    *out = s.pending;
    s.pending.kind = SERVO_CMD_NONE;
    return true;
}

void servo_screen_feedback(uint16_t position_us, float current_a, bool valid)
{
    const float deg = us_to_deg(position_us);

    /*
     * A reading counts as new only if it is drawn differently: compare at
     * the precision shown, tenths of a degree and hundredths of an amp.
     * Feedback arrives at the poll rate whether or not the servo moved, and
     * a revision bump per reading would repaint the 488x418 card at that
     * rate and never reach the grip's clipped repaint.
     */
    const int q_deg = (int)(deg * 10.0f + (deg >= 0.0f ? 0.5f : -0.5f));
    const int q_a   = (int)(current_a * 100.0f + 0.5f);
    const bool same = valid && s.have_feedback
                      && q_deg == s.shown_q_deg && q_a == s.shown_q_a;

    s.measured_deg  = deg;
    s.current_a     = current_a;
    s.have_feedback = valid;
    /*
     * The arm is drawn where the servo reports it, with no easing: easing
     * would add the screen's lag to the servo's, and the two are
     * indistinguishable when drawn.  The smoothing in tick() applies only
     * while nothing is reporting.
     */
    if (valid) {
        s.shown_deg = deg;
    }
    if (!same) {
        s.shown_q_deg = q_deg;
        s.shown_q_a   = q_a;
        ++s.ctrl_rev;
    }
}

void servo_screen_supply(const supply_state_t *st)
{
    if (st == NULL) {
        return;
    }
    s.sup = *st;
    s.have_sup = true;
    /* Only what arrived: a reading that did not is a gap, not a zero. */
    const bool v_ok = st->online && (st->ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = st->online && (st->ok & SUPPLY_OK_CURRENT) != 0u;
    const float v[PS_COUNT] = {
        v_ok ? st->v : NAN,
        i_ok ? st->i : NAN,
        (v_ok && i_ok) ? st->p : NAN,
    };
    ui_plot_push(&s.power, v);
    ui_plot_update_scales(&s.power, RC_W);
    ++s.power_rev;
}

uint16_t servo_screen_commanded(void) { return deg_to_us(s.commanded_deg); }

uint16_t servo_screen_frame_hz(void) { return s.frame_hz; }

const char *servo_screen_type_name(void) { return type()->name; }

void servo_screen_rate(servo_rate_state_t st, uint16_t hz)
{
    if (st == s.rate_st && hz == s.rate_hz) {
        return;
    }
    s.rate_st = st;
    s.rate_hz = hz;
    if (s.ov_open) {
        ++s.ctrl_rev;   /* the OUTPUT page's note says it */
    }
}

void servo_screen_set_commanded(float deg)
{
    command(deg);
}

/* ------------------------------------------------------------- the profile */

/*
 * A profile goes into force: a type with its own pulse widths when it is a
 * different type, and a frame rate no faster than its pulses allow.
 */
static void apply_profile(int t, uint16_t hz)
{
    if (t != s.type) {
        s.type      = t;
        s.min_us    = k_types[t].min_us;
        s.centre_us = k_types[t].centre_us;
        s.max_us    = k_types[t].max_us;
    }
    const uint16_t top = max_rate_for(s.type, cmd_top());
    s.frame_hz = (hz > top) ? top : hz;
    reissue();
    ++s.ctrl_rev;
}

/*
 * A profile asked for: one that can destroy a servo not made for it opens
 * the warning, and goes into force only once that has been held; any other
 * goes into force at once.
 */
static void ask_profile(int t, uint16_t hz)
{
    const uint16_t pulses = (t != s.type) ? k_types[t].max_us : cmd_top();
    const uint16_t top = max_rate_for(t, pulses);
    if (hz > top) {
        hz = top;
    }
    if (dangerous(t, hz)) {
        s.warn.open = true;
        s.warn.type = t;
        s.warn.hz   = hz;
        s.warn.down = false;
        ui_hold_reset(&s.warn.hold);
        ++s.warn.rev;
        ++s.ctrl_rev;
        return;
    }
    apply_profile(t, hz);
}

/* ------------------------------------------------------------------ layout */

void servo_invalidate(void)
{
    s.drawn_mask = 0;
    for (int b = 0; b < 2; ++b) {
        s.drawn_ctrl[b]  = UINT32_MAX;
        s.drawn_arm[b]   = UINT32_MAX;
        s.drawn_power[b] = UINT32_MAX;
        s.drawn_warn[b]  = UINT32_MAX;
        s.drawn_save[b]  = 0xFFu;
        /* No step the arm can be drawn at, so the next frame draws it. */
        s.drawn_pulse[b] = -1;
    }
}

static gfx_rect_t overlay_area(void)
{
    return (gfx_rect_t){ OV_X, OV_Y, OV_W, OV_H };
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    servo_invalidate();
    s.drawn_mask    = 0;
    s.travel_deg    = 90.0f;
    s.speed_pct     = 100;
    s.shown_cmd     = (float)OUT_SPAN / 2.0f;
    /* STANDARD PWM at 50 Hz: what every restart starts at, whatever the
     * session before it used. */
    s.type      = 0;
    s.min_us    = k_types[0].min_us;
    s.centre_us = k_types[0].centre_us;
    s.max_us    = k_types[0].max_us;
    s.frame_hz  = k_types[0].default_hz;

    ui_slider_init(&s.speed, (gfx_rect_t){ RC_X, 296, RC_W, 22 },
                   10.0f, 100.0f, 0);
    s.speed.value = 100.0f;
    ui_slider_set_ticks(&s.speed, 0);
    /* A sweep speed commands nothing on its own, and the track is 22 px: a
     * tap is how it is used.  The throttle's rule is the throttle's. */
    ui_slider_set_tap_to_set(&s.speed, true);

    s.centre_btn  = (gfx_rect_t){ RC_X, 350, (int16_t)(RC_W / 3 - 4), 32 };
    s.sweep_btn   = (gfx_rect_t){ (int16_t)(RC_X + RC_W / 3 + 2), 350,
                                  (int16_t)(RC_W / 3 - 4), 32 };
    s.release_btn = (gfx_rect_t){ (int16_t)(RC_X + 2 * (RC_W / 3) + 4), 350,
                                  (int16_t)(RC_W - 2 * (RC_W / 3) - 4), 32 };
    /* Full width and last, under the three that only shape what is commanded:
     * this is the one that decides whether anything is driven at all. */
    s.arm_btn     = (gfx_rect_t){ RC_X, 388, RC_W, 32 };
    s.set_btn     = (gfx_rect_t){ (int16_t)(RCARD_X + RCARD_W - 12 - SETB_W),
                                  12, SETB_W, 24 };

    ui_plot_init(&s.power, k_power, PS_COUNT, (float)RC_W / SAMPLE_HZ);
    ui_tabs_init(&s.tabs, k_pages, PG_COUNT,
                 (gfx_rect_t){ (int16_t)(OV_X + 10), OV_TAB_Y, OV_TAB_W,
                               OV_TAB_H });
    s.kp.pressed = -1;
    s.tk.pressed = -1;
    s.ch.pressed = -1;
}

/* --------------------------------------------------------------- the overlay */

/* A row's rectangle: its column, or both for a wide one. */
static gfx_rect_t row_rect(int i)
{
    const ov_row_t *r = &k_rows[i];
    const int x = (r->col == 0) ? OV_X + 10 : OV_X + 20 + OV_COL_W;
    const int w = r->wide ? OV_W - 20 : OV_COL_W;
    return (gfx_rect_t){ (int16_t)x, (int16_t)(OV_ROW0 + r->row * OV_PITCH),
                         (int16_t)w, OV_ROW_H };
}

/* Where a row's value is drawn and tapped. */
static gfx_rect_t value_rect(int i)
{
    const gfx_rect_t r = row_rect(i);
    const int w = k_rows[i].wide ? r.w - 124 : OV_VAL_W;
    return (gfx_rect_t){ (int16_t)(r.x + r.w - w), r.y, (int16_t)w, r.h };
}

/* TRIM's two steps, inside its value. */
static gfx_rect_t trim_rect(int i, bool up)
{
    const gfx_rect_t v = value_rect(i);
    return (gfx_rect_t){ (int16_t)(up ? v.x + v.w - 30 : v.x), v.y, 30, v.h };
}

static gfx_rect_t close_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 10 - 90), (int16_t)(OV_Y + 6),
                         90, 30 };
}

static gfx_rect_t choice_rect(int i)
{
    const int per_col = 5;
    const int col = i / per_col;
    const int line = i % per_col;
    return (gfx_rect_t){ (int16_t)(OV_X + 10 + col * (OV_COL_W + 10)),
                         (int16_t)(OV_Y + 48 + line * 46),
                         (int16_t)OV_COL_W, 40 };
}

static gfx_rect_t choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 10 - 110),
                         (int16_t)(OV_Y + OV_H - 46), 110, 36 };
}

static gfx_rect_t warn_apply_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + 20), (int16_t)(OV_Y + OV_H - 76),
                         240, 56 };
}

static gfx_rect_t warn_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 20 - 180),
                         (int16_t)(OV_Y + OV_H - 76), 180, 56 };
}

static int decimals_of(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    return (d->type == SET_TYPE_FLOAT && d->step < 1.0f) ? 2 : 0;
}

/* The longest pulse the frame rate in force leaves room for. */
static uint16_t max_pulse_for_rate(void)
{
    const unsigned pause = type()->heli ? HELI_MIN_PAUSE_US : PWM_MIN_PAUSE_US;
    const unsigned period = 1000000u / (unsigned)s.frame_hz;
    unsigned top = (period > pause) ? period - pause : 0u;
    if (top > OUT_CEILING_US) {
        top = OUT_CEILING_US;   /* what the coprocessor takes */
    }
    return (uint16_t)top;
}

static void close_panels(void)
{
    ui_keypad_close(&s.kp);
    ui_textkey_close(&s.tk);
    s.kp_target = KT_NONE;
    s.ch.open   = false;
    s.warn.open = false;
    s.warn.down = false;
    ui_hold_reset(&s.warn.hold);
}

static void open_choice(int target, setting_id_t id, const char *title)
{
    s.ch.open    = true;
    s.ch.target  = target;
    s.ch.id      = id;
    s.ch.count   = 0;
    s.ch.pressed = -1;
    snprintf(s.ch.title, sizeof(s.ch.title), "%s", title);
}

static void choice_add(const char *label, int value)
{
    if (s.ch.count < CHOICE_MAX) {
        snprintf(s.ch.labels[s.ch.count], sizeof(s.ch.labels[0]), "%s", label);
        s.ch.values[s.ch.count] = value;
        ++s.ch.count;
    }
}

static void open_keypad(int target, const char *title, const char *unit,
                        float value, float lo, float hi, int decimals)
{
    ui_keypad_open(&s.kp, overlay_area(), title, unit, value, lo, hi,
                   decimals);
    s.kp_target = target;
}

/* A settings row tapped: the editor its kind takes. */
static void edit_row(int i)
{
    const ov_row_t *r = &k_rows[i];
    switch (r->kind) {
    case R_TYPE:
        open_choice(CH_TYPE, SETTING_COUNT, "SERVO TYPE");
        for (int t = 0; t < TYPE_COUNT; ++t) {
            choice_add(k_types[t].name, t);
        }
        break;
    case R_RATE: {
        open_choice(CH_RATE, SETTING_COUNT, "FRAME RATE");
        const uint16_t top = max_rate_for(s.type, cmd_top());
        for (int k = 0; k < type()->rate_count; ++k) {
            if (type()->rates[k] <= top) {
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "%u Hz", (unsigned)type()->rates[k]);
                choice_add(lbl, type()->rates[k]);
            }
        }
        choice_add("CUSTOM", -1);
        break;
    }
    /*
     * Each end at least 50 us from the centre, and the range a command
     * carries -- centred on CENTRE, out to the further end; see cmd_range()
     * -- inside what the coprocessor takes and what the frame rate leaves
     * a pause after (max_pulse_for_rate(), never above the ceiling).  So an
     * end may lie no further from the centre than the centre lies from the
     * floor or from that top.
     */
    case R_MIN: {
        const unsigned c = s.centre_us;
        const unsigned top = max_pulse_for_rate();
        const unsigned lo = (2u * c > top + OUT_FLOOR_US) ? 2u * c - top
                                                          : OUT_FLOOR_US;
        open_keypad(KT_MIN, "PULSE MIN", "us", (float)s.min_us, (float)lo,
                    (float)(c - 50u), 0);
        break;
    }
    case R_CENTRE: {
        unsigned lo = (s.max_us + OUT_FLOOR_US + 1u) / 2u;
        unsigned hi = (max_pulse_for_rate() + s.min_us) / 2u;
        if (lo < s.min_us + 50u) { lo = s.min_us + 50u; }
        if (hi > s.max_us - 50u) { hi = s.max_us - 50u; }
        open_keypad(KT_CENTRE, "PULSE CENTRE", "us", (float)s.centre_us,
                    (float)lo, (float)hi, 0);
        break;
    }
    case R_MAX: {
        const unsigned c = s.centre_us;
        unsigned top = max_pulse_for_rate();
        if (top > 2u * c - OUT_FLOOR_US) { top = 2u * c - OUT_FLOOR_US; }
        open_keypad(KT_MAX, "PULSE MAX", "us", (float)s.max_us,
                    (float)(c + 50u),
                    (float)((top > c + 50u) ? top : c + 50u), 0);
        break;
    }
    case R_TRAVEL:
        open_keypad(KT_TRAVEL, "TRAVEL", "deg", s.travel_deg, 10.0f, 90.0f, 0);
        break;
    case R_REVERSE:
        s.reverse = !s.reverse;
        reissue();
        break;
    case R_TEXT:
        ui_textkey_open(&s.tk, overlay_area(), "DEVICE UNDER TEST",
                        settings_text(SET_TEXT_DUT_NAME), UI_TEXTKEY_MAX);
        break;
    case R_SETTING: {
        const setting_def_t *d = settings_def(r->id);
        if (d->type == SET_TYPE_BOOL) {
            settings_set(r->id, settings_get_bool(r->id) ? 0.0f : 1.0f);
            settings_request_save();
        } else if (d->type == SET_TYPE_ENUM) {
            open_choice(CH_ENUM, r->id, r->label);
            for (int k = 0; k < d->option_count; ++k) {
                choice_add(d->options[k], k);
            }
        } else {
            open_keypad(KT_SETTING, r->label, d->unit, settings_get(r->id),
                        d->min, d->max, decimals_of(r->id));
            s.kp_setting = r->id;
        }
        break;
    }
    default:
        break;
    }
    ++s.ctrl_rev;
}

static void choose(int k)
{
    const int v = s.ch.values[k];
    const int target = s.ch.target;
    const setting_id_t id = s.ch.id;
    s.ch.open = false;
    if (target == CH_TYPE) {
        ask_profile(v, k_types[v].default_hz);
    } else if (target == CH_RATE) {
        if (v < 0) {
            open_keypad(KT_RATE, "FRAME RATE", "Hz", (float)s.frame_hz, 50.0f,
                        (float)max_rate_for(s.type, cmd_top()), 0);
        } else {
            ask_profile(s.type, (uint16_t)v);
        }
    } else if (target == CH_ENUM) {
        settings_set(id, (float)v);
        settings_request_save();
    }
    ++s.ctrl_rev;
}

static void keypad_done(ui_keypad_result_t r, float v)
{
    if (r == UI_KEYPAD_NONE) {
        return;
    }
    const int target = s.kp_target;
    s.kp_target = KT_NONE;
    if (r == UI_KEYPAD_OK) {
        const uint16_t us = (uint16_t)lroundf(v);
        switch (target) {
        case KT_MIN:    s.min_us = us;    reissue(); break;
        case KT_CENTRE: s.centre_us = us; reissue(); break;
        case KT_MAX:
            s.max_us = us;
            apply_profile(s.type, s.frame_hz);   /* a rate it no longer fits
                                                  * comes down; never up */
            break;
        case KT_TRAVEL:
            s.travel_deg = (float)lroundf(v);
            s.commanded_deg = clamp_travel(s.commanded_deg);
            reissue();
            break;
        case KT_RATE:
            ask_profile(s.type, us);
            break;
        case KT_SETTING: {
            const setting_def_t *d = settings_def(s.kp_setting);
            /* A cap rounds down onto its step, as on SUPPLY. */
            if (d->step > 0.0f && (s.kp_setting == SET_SUPPLY_V_MAX
                                   || s.kp_setting == SET_SUPPLY_I_MAX)) {
                v = d->min + floorf((v - d->min) / d->step + 1e-3f) * d->step;
            }
            settings_set(s.kp_setting, v);
            if (s.kp_setting == SET_SUPPLY_V_MAX
                || s.kp_setting == SET_SUPPLY_I_MAX) {
                supply_screen_limits_changed();
            }
            settings_request_save();
            break;
        }
        default:
            break;
        }
    }
    ++s.ctrl_rev;
}

static void ov_take(const touch_event_t *evt, int code, int which)
{
    s.ov_have    = true;
    s.ov_id      = evt->point.id;
    s.ov_pressed = code;
    s.ov_row     = which;
    ++s.ctrl_rev;
}

static void ov_let_go(void)
{
    if (s.ov_have) {
        s.ov_have    = false;
        s.ov_pressed = OP_NONE;
        ++s.ctrl_rev;
    }
}

/* A press landing in the overlay: on whichever of its panels is up. */
static void ov_down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    if (s.warn.open) {
        if (gfx_rect_contains(warn_apply_rect(), x, y)) {
            ov_take(evt, OP_WARN_APPLY, -1);
            s.warn.down = true;
            ui_hold_begin(&s.warn.hold);
            ++s.warn.rev;
        } else if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            ov_take(evt, OP_WARN_CANCEL, -1);
        }
        return;
    }
    if (s.kp.open) {
        ov_take(evt, OP_KEYPAD, -1);
        (void)ui_keypad_event(&s.kp, evt, NULL);
        return;
    }
    if (s.tk.open) {
        ov_take(evt, OP_TEXT, -1);
        (void)ui_textkey_event(&s.tk, evt, NULL, 0);
        return;
    }
    if (s.ch.open) {
        for (int k = 0; k < s.ch.count; ++k) {
            if (gfx_rect_contains(choice_rect(k), x, y)) {
                ov_take(evt, OP_CHOICE, k);
                return;
            }
        }
        if (gfx_rect_contains(choice_cancel_rect(), x, y)) {
            ov_take(evt, OP_CHOICE_CANCEL, -1);
        }
        return;
    }
    if (gfx_rect_contains((gfx_rect_t){ (int16_t)(OV_X + 10), OV_TAB_Y,
                                        OV_TAB_W, OV_TAB_H }, x, y)) {
        ov_take(evt, OP_TAB, -1);
        (void)ui_tabs_event(&s.tabs, evt);
        return;
    }
    if (gfx_rect_contains(close_rect(), x, y)) {
        ov_take(evt, OP_CLOSE, -1);
        return;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].page != s.tabs.selected) {
            continue;
        }
        if (k_rows[i].kind == R_TRIM) {
            /* The steps act on the press, as the old CENTRE buttons did:
             * a fine step should feel immediate. */
            const bool up = gfx_rect_contains(trim_rect(i, true), x, y);
            if (up || gfx_rect_contains(trim_rect(i, false), x, y)) {
                ov_take(evt, up ? OP_TRIM_UP : OP_TRIM_DN, i);
                s.trim_us = (int16_t)(s.trim_us + (up ? 5 : -5));
                if (s.trim_us > 200) { s.trim_us = 200; }
                if (s.trim_us < -200) { s.trim_us = -200; }
                reissue();
                return;
            }
            continue;
        }
        if (gfx_rect_contains(row_rect(i), x, y)) {
            ov_take(evt, OP_ROW, i);
            return;
        }
    }
}

/* The rest of an overlay press: its moves and its release. */
static void ov_rest(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const bool up = (evt->type == TOUCH_EVENT_UP);
    const int was = s.ov_pressed;
    const int which = s.ov_row;
    switch (was) {
    case OP_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&s.kp, evt, &v);
        if (up) {
            ov_let_go();
        }
        keypad_done(r, v);
        ++s.ctrl_rev;
        return;
    }
    case OP_TEXT: {
        char name[SETTINGS_TEXT_MAX];
        const ui_textkey_result_t r =
            ui_textkey_event(&s.tk, evt, name, sizeof(name));
        if (up) {
            ov_let_go();
        }
        if (r == UI_TEXTKEY_OK) {
            settings_set_text(SET_TEXT_DUT_NAME, name);
            settings_request_save();
        }
        ++s.ctrl_rev;
        return;
    }
    case OP_TAB:
        if (ui_tabs_event(&s.tabs, evt)) {
            ++s.ctrl_rev;
        }
        if (up) {
            ov_let_go();
        }
        return;
    case OP_WARN_APPLY:
        if (!up) {
            /* A finger that leaves APPLY abandons the hold, as on ARM. */
            if (!gfx_rect_contains(warn_apply_rect(), x, y)
                && ui_hold_leave(&s.warn.hold)) {
                s.warn.down = false;
                ++s.warn.rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.warn.hold);
        s.warn.down = false;
        ++s.warn.rev;
        ov_let_go();
        return;
    default:
        break;
    }
    if (!up) {
        return;
    }
    ov_let_go();
    switch (was) {
    case OP_CLOSE:
        if (gfx_rect_contains(close_rect(), x, y)) {
            s.ov_open = false;
            close_panels();
            servo_invalidate();
        }
        break;
    case OP_ROW:
        if (which >= 0 && gfx_rect_contains(row_rect(which), x, y)) {
            edit_row(which);
        }
        break;
    case OP_CHOICE:
        if (which >= 0 && which < s.ch.count
            && gfx_rect_contains(choice_rect(which), x, y)) {
            choose(which);
        }
        break;
    case OP_CHOICE_CANCEL:
        if (gfx_rect_contains(choice_cancel_rect(), x, y)) {
            s.ch.open = false;
        }
        break;
    case OP_WARN_CANCEL:
        if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            s.warn.open = false;
        }
        break;
    case OP_SETTINGS:
        if (gfx_rect_contains(s.set_btn, x, y)) {
            /* SETTINGS opens the overlay and closes it, taking whatever is
             * open in it; a warning not held is a profile not applied.  The
             * servo's lead runs out past the left card, where the overlay
             * does not reach, so the change is a whole repaint. */
            s.ov_open = !s.ov_open;
            close_panels();
            servo_invalidate();
        }
        break;
    default:
        break;
    }
    ++s.ctrl_rev;
}

/* ------------------------------------------------------------------ events */

/* Whether a touch is close enough to the horn's sweep to mean the horn. */
static bool on_the_dial(int px, int py, float *deg)
{
    const float dx = (float)(px - SHAFT_X);
    const float dy = (float)(SHAFT_Y - py);
    const float r  = sqrtf(dx * dx + dy * dy);
    /* Generous inside, bounded outside: a finger landing short of the horn
     * still means the horn, and one landing well past the arc means the
     * card behind it. */
    if (r < 30.0f || r > (float)ARC_R + 34.0f) {
        return false;
    }
    if (dx < -30.0f) {
        return false;      /* left of the shaft is the case, not the sweep */
    }
    const float phi = atan2f(dy, dx) * 180.0f / 3.14159265358979f;
    if (phi < -104.0f || phi > 104.0f) {
        return false;
    }
    *deg = phi;
    return true;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int px = evt->point.x, py = evt->point.y;

    /* The overlay's own press first: its moves and its release are its. */
    if (s.ov_have && evt->point.id == s.ov_id
        && evt->type != TOUCH_EVENT_DOWN) {
        ov_rest(evt);
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.ov_open && gfx_rect_contains(overlay_area(), px, py)) {
            /* One press at a time in the overlay. */
            if (!s.ov_have) {
                ov_down(evt);
            }
            return;
        }
        if (gfx_rect_contains(s.set_btn, px, py)) {
            if (!s.ov_have) {
                ov_take(evt, OP_SETTINGS, -1);
            }
            return;
        }
        float deg;
        if (!s.ov_open && on_the_dial(px, py, &deg)) {
            /* A finger on the dial takes the horn from a sweep. */
            stop_sweep();
            s.dragging = true;
            s.drag_id  = evt->point.id;
            command(deg);
            return;
        }
        if (gfx_rect_contains(s.centre_btn, px, py)) {
            stop_sweep();
            s.commanded_deg = 0.0f;
            post(SERVO_CMD_CENTRE, deg_to_us(0.0f));
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.sweep_btn, px, py)) {
            if (s.sweeping) {
                /* HOLD: the sweep stops where the horn is, and holds it --
                 * where the output has got to, which SPEED can leave well
                 * behind the curve. */
                stop_sweep();
                command(s.shown_deg);
            } else {
                start_sweep();
            }
        } else if (gfx_rect_contains(s.release_btn, px, py)) {
            stop_sweep();
            post(SERVO_CMD_RELEASE, 0);
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.arm_btn, px, py)) {
            if (s.arm_down) {
                /* The gesture belongs to the contact that began it.  A second
                 * finger, or a palm, taking it over would leave the first
                 * one's release ignored and arm the bench from a contact
                 * nobody made deliberately. */
                return;
            }
            s.arm_down = true;
            s.arm_id   = evt->point.id;
            ui_hold_begin(&s.arm);
            ++s.arm_rev;
        }
    }

    if (s.arm_down && evt->point.id == s.arm_id) {
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* A finger that leaves ARM abandons the hold -- see
             * ui_hold_leave().  While the bench is armed the same press is a
             * disarm, whose release is checked against the rectangle. */
            if (!s.armed && !gfx_rect_contains(s.arm_btn, px, py)
                && ui_hold_leave(&s.arm)) {
                s.arm_down = false;
                ++s.arm_rev;
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            const bool fired = ui_hold_end(&s.arm);
            s.arm_down = false;
            ++s.arm_rev;
            /*
             * Disarming is a press; arming is a hold that has already sent
             * its command by the time the finger lifts.
             */
            if (s.armed && !fired && gfx_rect_contains(s.arm_btn, px, py)) {
                post(SERVO_CMD_DISARM, 0);
            }
            return;
        }
    }

    if (s.dragging && evt->point.id == s.drag_id) {
        if (evt->type == TOUCH_EVENT_MOVE) {
            float deg;
            if (on_the_dial(px, py, &deg)) {
                command(deg);
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            s.dragging = false;
            return;
        }
    }

    if (ui_slider_event(&s.speed, evt)) {
        s.speed_pct = (int)(s.speed.value + 0.5f);
        /* The rate is part of the command, so a held output takes the new
         * one rather than waiting for the next drag. */
        reissue();
        ++s.ctrl_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

static void at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    const float phi = PHI(deg) * k;
    *x = SHAFT_X + (int)((float)r * cosf(phi) + 0.5f);
    *y = SHAFT_Y - (int)((float)r * sinf(phi) + 0.5f);
}

/* The case, its tabs and its boss.  Drawn per framebuffer, not per frame:
 * thing on this card that moves is the horn. */
static void draw_body(gfx_canvas_t *c)
{
    const gfx_color_t shell = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const int bx = SHAFT_X - BODY_W + 24;
    const int by = SHAFT_Y - BODY_H / 2;

    /* Mounting tabs first, so the case overlaps them. */
    for (int i = 0; i < 2; ++i) {
        /* Two holes, and the flange centred on the case: a servo's mounting
         * lugs are symmetric about its centreline. */
        const int tx = (i == 0) ? bx - TAB_W + TAB_OVER
                                : bx + BODY_W - TAB_OVER;
        const int ty = by;
        gfx_fill_round_rect(c, tx, ty, TAB_W, TAB_H, 5, shell);
        gfx_draw_round_rect(c, tx, ty, TAB_W, TAB_H, 5, edge);
        /*
         * Open slots, not drilled holes: a servo's lugs are cut through to
         * the outer edge so it drops into a mount whose screws are already
         * in.
         */
        /* Set in from the flange's own outer edge, so the bore keeps its
         * distance from the case whichever end it is on. */
        const int cxh   = (i == 0) ? tx + TAB_MARGIN + TAB_HOLE_R
                                   : tx + TAB_W - TAB_MARGIN - TAB_HOLE_R;
        const int mouth = (i == 0) ? tx : tx + TAB_W;   /* the open edge */
        /*
         * The bore is darker than both the lug and the card behind it: at
         * these two greys a hole showing the card reads as a smudge, and a
         * recess reads as a hole.
         */
        const gfx_color_t bore = ui_theme_color(UI_C_PANEL_SUNK);
        for (int h = 0; h < 2; ++h) {
            const int hy = by + BODY_H / 2
                           + ((h == 0) ? -TAB_HOLE_DY : TAB_HOLE_DY);
            for (int pass = 0; pass < 2; ++pass) {
                const gfx_color_t ink = (pass == 0) ? edge : bore;
                const int r  = TAB_HOLE_R - pass;
                /* The throat is a fraction of the bore, not a fixed
                 * inset off it: subtracting a couple of pixels from a large
                 * hole leaves a slot with no waist at all. */
                const int nk = TAB_HOLE_R * 3 / 5 - pass;
                gfx_fill_circle_aa(c, cxh, hy, r, ink);
                /* Both passes run flush to the lug's edge, so the mouth is
                 * open rather than capped by its own outline. */
                const int x0 = (mouth < cxh) ? mouth : cxh;
                const int x1 = (mouth < cxh) ? cxh : mouth;
                gfx_fill_rect(c, x0, hy - nk, x1 - x0, 2 * nk, ink);
            }
        }
    }

    gfx_fill_round_rect(c, bx, by, BODY_W, BODY_H, 8, shell);
    gfx_draw_round_rect(c, bx, by, BODY_W, BODY_H, 8, edge);
    gfx_hline(c, bx + 8, by + 1, BODY_W - 16, gfx_lerp(shell, GFX_WHITE, 34));
    /* The band across the case, which is the one place a servo has colour. */
    gfx_fill_rect(c, bx + 26, by + 1, 24, BODY_H - 2,
                  ui_theme_color(UI_C_ACCENT));
    /* The label. */
    gfx_fill_round_rect(c, bx + 74, by + 20, 92, 46, 4,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, bx + 74, by + 20, 92, 46, 4, edge);
    /* The lead, leaving the case at the end away from the output. */
    for (int i = 0; i < 3; ++i) {
        gfx_hline(c, bx - 62, by + 32 + i * 8, 34,
                  ui_theme_color(UI_C_TEXT_FAINT));
    }
}

/*
 * Where a ring of radius @p r has to stop so that a constant band of dark
 * card shows between its end and the arm.
 *
 * A fixed angle does not do it: the arm is the same width at every radius,
 * so it subtends less angle further out, and two rings sharing one angular
 * gap leave the outer one further clear of the arm than the inner.  The
 * outer ring is therefore the longer of the two.
 */
static float ring_gap_deg(float r, float half_w, float dark_px)
{
    const float k = 180.0f / 3.14159265358979f;
    float sine = half_w / r;
    if (sine > 0.99f) {
        sine = 0.99f;
    }
    return (asinf(sine) + dark_px / r) * k;
}

static void draw_horn(gfx_canvas_t *c, float deg, gfx_color_t col)
{
    int tx, ty;
    at(deg, HORN_L, &tx, &ty);

    /*
     * The unlit rings are drawn first and whole, so the arm is drawn over
     * them and they pass behind it.
     */
    const gfx_color_t ghost =
        gfx_lerp(ui_theme_color(UI_C_PANEL), col, 46);
    gfx_arc(c, tx, ty, 25, 3, 0.0f, 360.0f, ghost);
    gfx_arc(c, tx, ty, 33, 2, 0.0f, 360.0f, ghost);

    gfx_capsule_aa(c, SHAFT_X, SHAFT_Y, tx, ty, 26, col);
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 25, col);

    /* Holes along the arm, spaced from the tip inwards so the outermost stays
     * on the arm whatever HORN_L is; a hole drawn past the tip reads as a
     * bite out of the card. */
    for (int i = 1; i <= 3; ++i) {
        int hx, hy;
        at(deg, HORN_L - 12 - (3 - i) * 14, &hx, &hy);
        gfx_fill_circle_aa(c, hx, hy, 5, ui_theme_color(UI_C_PANEL));
    }
    /* And the outermost one, which is the hole the ring is pointing at. */
    gfx_fill_circle_aa(c, tx, ty, 5, ui_theme_color(UI_C_PANEL));

    /*
     * The lit part of each ring, over the top.  It is split on the side the
     * arm comes in from, with a constant 9 px band of card either side of
     * the metal (ring_gap_deg), and each end fades into the unlit ring
     * beneath.
     *
     * The rings breathe while the output is being held: the on-picture sign
     * that the servo is under command.
     */
    const float breath = s.driving
                             ? 0.42f + 0.58f * (0.5f + 0.5f * sinf(s.pulse))
                             : 0.62f;
    const gfx_color_t grip =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(150.0f + 100.0f * breath));
    const gfx_color_t faint =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(90.0f + 90.0f * breath));

    const float half_w = 13.0f;   /* the arm's half width, as drawn */
    const float dark   = 9.0f;    /* the band of card between ring and arm */
    const float g_in   = ring_gap_deg(25.0f, half_w, dark);
    const float g_out  = ring_gap_deg(33.0f, half_w, dark);
    gfx_arc_fade(c, tx, ty, 25, 3, deg + 180.0f + g_in,
                 deg + 540.0f - g_in, grip, ghost, 78.0f);
    gfx_arc_fade(c, tx, ty, 33, 2, deg + 180.0f + g_out,
                 deg + 540.0f - g_out, faint, ghost, 78.0f);

    /* The boss and its splines. */
    /* An outline is two discs, not a circle walked round: an arc closed on
     * itself lays some pixels twice and misses others, and at two pixels wide
     * that stipples. */
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 17, gfx_lerp(col, GFX_BLACK, 60));
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 15,
                       ui_theme_color(UI_C_PANEL_SUNK));
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 6, col);
}

static void draw_dial(gfx_canvas_t *c)
{
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_FAINT);

    /* The travel the settings allow, over the travel the servo has. */
    gfx_arc(c, SHAFT_X, SHAFT_Y, ARC_R, 2, PHI(90.0f), PHI(-90.0f),
            ui_theme_color(UI_C_GRID));
    gfx_arc(c, SHAFT_X, SHAFT_Y, ARC_R, 4, PHI(s.travel_deg),
            PHI(-s.travel_deg), ui_theme_color(UI_C_EDGE_HI));

    for (int i = -2; i <= 2; ++i) {
        const float a = (float)i * 45.0f;
        int x0, y0, x1, y1;
        at(a, ARC_R - 10, &x0, &y0);
        at(a, ARC_R + 8, &x1, &y1);
        gfx_thick_line(c, x0, y0, x1, y1, (i == 0) ? 3 : 2,
                       (i == 0) ? ui_theme_color(UI_C_TEXT_DIM) : dim);

        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%+d", (int)a);
        int lx, ly;
        at(a, ARC_R + 26, &lx, &ly);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(lx - 24), (int16_t)(ly - 8),
                                     48, 16 },
                    lbl, UI_FONT_LABEL, dim, 1, GFX_ALIGN_CENTER);
    }
}

static void draw_left(gfx_canvas_t *c)
{
    /* Everything that moves lives in this rectangle, so this is what gets
     * cleared -- the case and the dial below it are chrome. */
    gfx_fill_rect(c, PAD + 1, PAD + 1, LCARD_W - 2, H - 2 * PAD - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_dial(c);
    /* The case first: the horn bolts to the top of it and must be drawn over
     * it, not under. */
    draw_body(c);

    /* The commanded position behind the measured one, so a lag is visible as
     * two arms rather than as a number that disagrees with a picture. */
    if (fabsf(s.shown_deg - s.commanded_deg) > 1.0f) {
        draw_horn(c, s.commanded_deg,
                  gfx_lerp(ui_theme_color(UI_C_PANEL),
                           ui_theme_color(UI_C_ACCENT), 70));
    }
    draw_horn(c, s.shown_deg, ui_theme_color(UI_C_ACCENT));

    char deg[16];
    snprintf(deg, sizeof(deg), "%+.1f", (double)s.shown_deg);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int dw = gfx_seg_width(deg, &seg);
    gfx_seg_text(c, PAD + 46, H - 84, deg, &seg,
                 ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_PANEL),
                          ui_theme_color(UI_C_ACCENT), 30));
    gfx_text(c, PAD + 52 + dw, H - 70, "DEG",
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
}

static void row(gfx_canvas_t *c, int y, const char *label, const char *value)
{
    gfx_text(c, RC_X, y + 5, label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    if (value != NULL) {
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(RC_X + 80), (int16_t)(y + 5),
                                     (int16_t)(RC_W - 80), 16 },
                    value, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
    }
}

/*
 * The same fill as MOTOR & ESC's ARM, from the same widget: armed is the
 * danger red the press fades towards, and the flash is the whole button.
 */
static gfx_color_t arm_fill(void)
{
    gfx_color_t fill = s.armed ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_OK);
    if (s.arm.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.arm.flash_left);
    }
    if (!s.armed && s.arm.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.arm.held_s);
    }
    return fill;
}

static void draw_arm(gfx_canvas_t *c)
{
    ui_button(c, s.arm_btn, s.armed ? "DISARM" : "ARM", arm_fill(),
              s.arm_down, true);
}

static gfx_rect_t power_rect(void)
{
    return (gfx_rect_t){ RC_X, (int16_t)(PWR_TXT_Y - 2), RC_W,
                         (int16_t)(PWR_Y + PWR_H - PWR_TXT_Y + 2) };
}

/* The supply's voltage, current and power, read and plotted. */
static void draw_power(gfx_canvas_t *c)
{
    const gfx_rect_t r = power_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    gfx_text(c, RC_X, PWR_TXT_Y, "SUPPLY", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    const bool v_ok = s.have_sup && s.sup.online
                      && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.have_sup && s.sup.online
                      && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    char v[12], a[12], w[12];
    if (v_ok) { snprintf(v, sizeof(v), "%.2f V", (double)s.sup.v); }
    else      { snprintf(v, sizeof(v), "-- V"); }
    if (i_ok) { snprintf(a, sizeof(a), "%.2f A", (double)s.sup.i); }
    else      { snprintf(a, sizeof(a), "-- A"); }
    if (v_ok && i_ok) { snprintf(w, sizeof(w), "%.1f W", (double)s.sup.p); }
    else              { snprintf(w, sizeof(w), "-- W"); }
    /* Each in its trace's colour, so the numbers name the lines. */
    const gfx_color_t col[PS_COUNT] = { ui_theme_color(UI_C_VOLT),
                                        ui_theme_color(UI_C_CURR),
                                        ui_theme_color(UI_C_POWER) };
    const char *txt[PS_COUNT] = { v, a, w };
    for (int k = 0; k < PS_COUNT; ++k) {
        s.power.series[k].color = col[k];
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(RC_X + 64 + k * 68), PWR_TXT_Y,
                                     68, 16 },
                    txt[k], UI_FONT_LABEL, col[k], 1, GFX_ALIGN_RIGHT);
    }
    ui_plot_render(&s.power, c, (gfx_rect_t){ RC_X, PWR_Y, RC_W, PWR_H });
}

static gfx_rect_t tag_rect(void)
{
    return (gfx_rect_t){ RC_X, TAG_Y, RC_W, TAG_H };
}

/* The profile in force, in the danger colour when it is one that can
 * destroy a servo not made for it. */
static void draw_tag(gfx_canvas_t *c)
{
    const gfx_rect_t r = tag_rect();
    const bool danger = in_force_dangerous();
    const gfx_color_t fill = danger ? ui_theme_color(UI_C_DANGER)
                                    : ui_theme_color(UI_C_PANEL_SUNK);
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, 4, fill);
    char line[40];
    snprintf(line, sizeof(line), "%s  %u Hz", type()->name,
             (unsigned)s.frame_hz);
    gfx_text_in(c, r, line, UI_FONT_LABEL,
                danger ? GFX_WHITE : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
}

/*
 * The right card.  The power plot only when @p power: a drag of the horn
 * repaints this card every frame for the COMMANDED line, and the plot
 * beside it has not moved -- its own samples repaint it, clipped.
 */
static void draw_right(gfx_canvas_t *c, bool power)
{
    const gfx_rect_t pr = power_rect();
    if (power) {
        gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2, H - 2 * PAD - 2,
                      ui_theme_color(UI_C_PANEL));
    } else {
        /* Around the plot, which keeps what it shows. */
        gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2,
                      pr.y - PAD - 1, ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, RCARD_X + 1, pr.y + pr.h, RCARD_W - 2,
                      H - PAD - 1 - (pr.y + pr.h), ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, RCARD_X + 1, pr.y, pr.x - RCARD_X - 1, pr.h,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, pr.x + pr.w, pr.y, RCARD_X + RCARD_W - 1 - (pr.x + pr.w),
                      pr.h, ui_theme_color(UI_C_PANEL));
    }

    gfx_text(c, RC_X, 18, "SERVO", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    ui_button(c, s.set_btn, "SETTINGS",
              s.ov_open ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_SETTINGS, true);

    char buf[24];
    snprintf(buf, sizeof(buf), "%u us", (unsigned)deg_to_us(s.commanded_deg));
    row(c, 44, "COMMANDED", buf);
    if (s.have_feedback) {
        snprintf(buf, sizeof(buf), "%+.1f deg", (double)s.measured_deg);
        row(c, 68, "MEASURED", buf);
        snprintf(buf, sizeof(buf), "%.2f A", (double)s.current_a);
        row(c, 92, "CURRENT", buf);
    } else {
        row(c, 68, "MEASURED", "---");
        row(c, 92, "CURRENT", "---");
    }

    draw_tag(c);
    if (power) {
        draw_power(c);
    }

    snprintf(buf, sizeof(buf), "%d %%", s.speed_pct);
    row(c, 264, "SPEED", buf);
    s.speed.color = ui_theme_color(UI_C_ACCENT);
    ui_slider_render(&s.speed, c);

    snprintf(buf, sizeof(buf), "%u - %u us", (unsigned)s.min_us,
             (unsigned)s.max_us);
    row(c, 322, "RANGE", buf);

    ui_button(c, s.centre_btn, "CENTRE", ui_theme_color(UI_C_ACCENT),
              false, true);
    ui_button(c, s.sweep_btn, s.sweeping ? "HOLD" : "SWEEP",
              s.sweeping ? ui_theme_color(UI_C_ACCENT)
                         : ui_theme_color(UI_C_PANEL_HI),
              false, s.sweeping || (s.armed && s.sweep_able));
    ui_button(c, s.release_btn, "RELEASE", ui_theme_color(UI_C_PANEL_HI),
              false, true);
    draw_arm(c);
}

/* Whether the settings reached the medium: 0 saved, 1 waiting for a quiet
 * moment, 2 refused, 3 changed on SETUP and not asked to be saved. */
static uint8_t save_state(void)
{
    if (settings_save_failed()) {
        return 2u;
    }
    if (settings_save_asked()) {
        return 1u;
    }
    return settings_dirty() ? 3u : 0u;
}

static gfx_rect_t save_line_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + 10), (int16_t)(OV_Y + OV_H - 24),
                         (int16_t)(OV_W / 2), 18 };
}

static void draw_save_line(gfx_canvas_t *c)
{
    static const char *const k_save[] = { "SAVED", "SAVE WAITING", "NOT SAVED",
                                          "SETUP CHANGES NOT SAVED" };
    const gfx_rect_t r = save_line_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    const uint8_t st = save_state();
    gfx_text(c, r.x, r.y + 1, k_save[st], &gfx_font_8x16,
             (st == 2u) ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* A test setting that the length rule leaves unused is drawn faint. */
static bool row_unused(const ov_row_t *r)
{
    if (r->kind != R_SETTING) {
        return false;
    }
    const int by = settings_get_int(SET_SERVO_LEN_BY);
    return (r->id == SET_SERVO_LEN_S && by != 0)
           || (r->id == SET_SERVO_LEN_MOVES && by == 0);
}

static void row_value(const ov_row_t *r, char *buf, size_t n)
{
    switch (r->kind) {
    case R_TYPE:    snprintf(buf, n, "%s", type()->name); return;
    case R_RATE:    snprintf(buf, n, "%u Hz", (unsigned)s.frame_hz); return;
    case R_MIN:     snprintf(buf, n, "%u us", (unsigned)s.min_us); return;
    case R_CENTRE:  snprintf(buf, n, "%u us", (unsigned)s.centre_us); return;
    case R_MAX:     snprintf(buf, n, "%u us", (unsigned)s.max_us); return;
    case R_TRIM:    snprintf(buf, n, "%+d", (int)s.trim_us); return;
    case R_TRAVEL:  snprintf(buf, n, "+/-%d", (int)s.travel_deg); return;
    case R_REVERSE: snprintf(buf, n, "%s", s.reverse ? "ON" : "OFF"); return;
    case R_TEXT:    snprintf(buf, n, "%s", settings_text(SET_TEXT_DUT_NAME));
                    return;
    default:
        break;
    }
    const setting_def_t *d = settings_def(r->id);
    const float v = settings_get(r->id);
    const bool limit = (r->id == SET_SERVO_IDLE_MAX
                        || r->id == SET_SERVO_HOLD_MAX
                        || r->id == SET_SERVO_TRAVEL_MAX_MS);
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", (v != 0.0f) ? "ON" : "OFF");
    } else if (d->type == SET_TYPE_ENUM) {
        const int k = settings_get_int(r->id);
        snprintf(buf, n, "%s", (k >= 0 && k < d->option_count)
                                   ? d->options[k] : "?");
    } else if (limit && !(v > 0.0f)) {
        snprintf(buf, n, "OFF");
    } else {
        snprintf(buf, n, "%.*f %s", decimals_of(r->id), (double)v, d->unit);
    }
}

static void draw_note(gfx_canvas_t *c, int x, int y, const char *const *lines,
                      int count)
{
    for (int i = 0; i < count; ++i) {
        gfx_text(c, x, y + i * 18, lines[i], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

/* Whether the frame rate reached the pins, in two lines. */
static void draw_rate_note(gfx_canvas_t *c, int x, int y)
{
    const servo_rate_state_t st = (s.rate_hz == s.frame_hz
                                   || s.rate_st == SERVO_RATE_UNSUPPORTED)
                                      ? s.rate_st
                                      : SERVO_RATE_UNSENT;
    char l1[64], l2[64];
    bool warn = true;
    switch (st) {
    case SERVO_RATE_IN_FORCE:
        snprintf(l1, sizeof(l1), "In force: every PWM surface runs at %u Hz.",
                 (unsigned)s.frame_hz);
        snprintf(l2, sizeof(l2), "A PPM output keeps its own frame.");
        warn = false;
        break;
    case SERVO_RATE_REFUSED:
        snprintf(l1, sizeof(l1), "REFUSED: a surface shares a PWM slice with");
        snprintf(l2, sizeof(l2), "an output at another rate. The pins kept theirs.");
        break;
    case SERVO_RATE_UNSUPPORTED:
        snprintf(l1, sizeof(l1), "This coprocessor takes no frame rate: every");
        snprintf(l2, sizeof(l2), "PWM output runs at its binding's, 50 Hz.");
        break;
    case SERVO_RATE_UNSENT:
    default:
        snprintf(l1, sizeof(l1), "The rate goes to the coprocessor with the");
        snprintf(l2, sizeof(l2), "next position.");
        warn = false;
        break;
    }
    const gfx_color_t col = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    gfx_text(c, x, y, l1, &gfx_font_8x16, col, 1);
    gfx_text(c, x, y + 18, l2, &gfx_font_8x16, col, 1);
}

static void draw_page(gfx_canvas_t *c)
{
    ui_tabs_render(&s.tabs, c);
    ui_button(c, close_rect(), "CLOSE", ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_CLOSE, true);
    for (int i = 0; i < ROW_COUNT; ++i) {
        const ov_row_t *r = &k_rows[i];
        if (r->page != s.tabs.selected) {
            continue;
        }
        const gfx_rect_t rr = row_rect(i);
        const gfx_rect_t vr = value_rect(i);
        const bool faint = row_unused(r);
        gfx_text(c, rr.x, rr.y + 9, r->label, &gfx_font_8x16,
                 faint ? ui_theme_color(UI_C_TEXT_FAINT)
                       : ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[32];
        row_value(r, v, sizeof(v));
        const bool pressed = s.ov_have && s.ov_pressed == OP_ROW
                             && s.ov_row == i;
        if (r->kind == R_TRIM) {
            gfx_fill_rect(c, vr.x, vr.y, vr.w, vr.h,
                          ui_theme_color(UI_C_PANEL_SUNK));
            gfx_text_in(c, vr, v, &gfx_font_8x16, ui_theme_color(UI_C_TEXT),
                        1, GFX_ALIGN_CENTER);
            ui_button(c, trim_rect(i, false), "-",
                      ui_theme_color(UI_C_PANEL_HI),
                      s.ov_have && s.ov_pressed == OP_TRIM_DN, true);
            ui_button(c, trim_rect(i, true), "+",
                      ui_theme_color(UI_C_PANEL_HI),
                      s.ov_have && s.ov_pressed == OP_TRIM_UP, true);
            continue;
        }
        /* The type and the rate in force in the danger colour when they
         * are ones that can destroy a servo not made for them. */
        const bool danger = (r->kind == R_TYPE || r->kind == R_RATE)
                            && in_force_dangerous();
        ui_button(c, vr, v,
                  danger ? ui_theme_color(UI_C_DANGER)
                         : ui_theme_color(UI_C_PANEL_SUNK),
                  pressed, !faint);
    }

    const int nx = OV_X + 10;
    if (s.tabs.selected == PG_OUTPUT) {
        char l1[64], l2[64];
        snprintf(l1, sizeof(l1), "Fastest with these pulses: %u Hz (%s pause).",
                 (unsigned)max_rate_for(s.type, cmd_top()),
                 type()->heli ? "0.5 ms" : "1 ms");
        snprintf(l2, sizeof(l2), "Type and rate are STANDARD PWM 50 Hz at start.");
        const char *const lines[] = { l1, l2 };
        draw_note(c, nx, OV_NOTE_Y, lines, 2);
        draw_rate_note(c, nx, OV_NOTE_Y + 2 * 18);
    } else if (s.tabs.selected == PG_TEST) {
        const char *const lines[] = {
            "SWEEP runs CURVE, SPEED,",
            "RANGE and DWELL until",
            "HOLD. No automatic test",
            "runs in this build.",
        };
        draw_note(c, OV_X + 20 + OV_COL_W, OV_ROW0 + 5 * OV_PITCH + 6, lines,
                  4);
    } else if (s.tabs.selected == PG_LIMITS) {
        const char *const lines[] = {
            "VOLTAGE MAX and CURRENT MAX are the SUPPLY",
            "screen's caps.  A pass/fail limit of 0 is",
            "not checked.  Above STALL AT the servo counts",
            "as stalled.",
        };
        draw_note(c, nx, OV_ROW0 + 3 * OV_PITCH + 6, lines, 4);
    } else {
        const char *const lines[] = {
            "The name heads each test report.",
        };
        draw_note(c, nx, OV_ROW0 + 2 * OV_PITCH + 6, lines, 1);
    }
    draw_save_line(c);
}

static void draw_choice(gfx_canvas_t *c)
{
    gfx_text(c, OV_X + 10, OV_Y + 16, s.ch.title, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    for (int k = 0; k < s.ch.count; ++k) {
        bool current = false;
        if (s.ch.target == CH_TYPE) {
            current = (s.ch.values[k] == s.type);
        } else if (s.ch.target == CH_RATE) {
            current = (s.ch.values[k] == (int)s.frame_hz);
        } else if (s.ch.target == CH_ENUM) {
            current = (s.ch.values[k] == settings_get_int(s.ch.id));
        }
        ui_button(c, choice_rect(k), s.ch.labels[k],
                  current ? ui_theme_color(UI_C_ACCENT)
                          : ui_theme_color(UI_C_PANEL_SUNK),
                  s.ov_have && s.ov_pressed == OP_CHOICE && s.ov_row == k,
                  true);
    }
    ui_button(c, choice_cancel_rect(), "CANCEL",
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_CHOICE_CANCEL, true);
}

static gfx_color_t warn_fill(void)
{
    return ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK),
                        ui_theme_color(UI_C_DANGER), s.warn.hold.held_s);
}

static void draw_warn_apply(gfx_canvas_t *c)
{
    ui_button(c, warn_apply_rect(), "HOLD TO APPLY", warn_fill(),
              s.warn.down, true);
}

/*
 * The warning a type or a frame rate that can destroy a servo needs before
 * it is applied: in the danger colour, saying what it is and what it does
 * to a servo not made for it.
 */
static void draw_warning(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text(c, a.x + 20, a.y + 20, "CAN DESTROY THE SERVO", &gfx_font_8x16,
             red, 2);
    const servo_type_t *t = &k_types[s.warn.type];
    char what[64];
    snprintf(what, sizeof(what), "%s at %u Hz, %u-%u us",
             t->name, (unsigned)s.warn.hz,
             (unsigned)((s.warn.type == s.type) ? s.min_us : t->min_us),
             (unsigned)((s.warn.type == s.type) ? s.max_us : t->max_us));
    gfx_text(c, a.x + 20, a.y + 66, what, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    const char *const lines[] = {
        "Only for a servo made for it: check its datasheet.",
        "A servo that is not overheats or jams within",
        "seconds, and is destroyed.",
        "An analogue servo takes no more than 60 Hz.",
        "Every restart goes back to STANDARD PWM 50 Hz.",
    };
    for (int i = 0; i < 5; ++i) {
        gfx_text(c, a.x + 20, a.y + 104 + i * 22, lines[i], &gfx_font_8x16,
                 (i < 3) ? ui_theme_color(UI_C_TEXT)
                         : ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    draw_warn_apply(c);
    ui_button(c, warn_cancel_rect(), "CANCEL",
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_WARN_CANCEL, true);
}

static void draw_overlay(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    if (s.kp.open) {
        ui_keypad_render(&s.kp, c);
        return;
    }
    if (s.tk.open) {
        ui_textkey_render(&s.tk, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (s.warn.open) {
        draw_warning(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (s.ch.open) {
        draw_choice(c);
        return;
    }
    draw_page(c);
}

static void tick(float dt_s)
{
    /* The screen's own clock, for the sweep it draws. */
    if (dt_s > 0.0f) {
        s.clock_frac_ms += dt_s * 1000.0f;
        const uint32_t whole = (uint32_t)s.clock_frac_ms;
        s.clock_ms += whole;
        s.clock_frac_ms -= (float)whole;
    }
    if (s.sweeping) {
        /* A sweep whose curve a setting or the profile changed starts over,
         * as the coprocessor's does when it is written. */
        const sweep_cfg_t now = sweep_cfg_now();
        if (!same_sweep(&now, &s.sw.cfg)) {
            stop_sweep();
            start_sweep();
        }
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        if (s.sweeping && sweep_step(&s.sw, s.clock_ms, &cmd)) {
            s.commanded_deg = clamp_travel(sweep_deg(cmd));
            ++s.ctrl_rev;
        }
    }
    if (s.arm_down && !s.armed) {
        ++s.arm_rev;
        if (ui_hold_tick(&s.arm, dt_s)) {
            post(SERVO_CMD_ARM, 0);
        }
    }
    if (s.arm.flash_left > 0) {
        ++s.arm_rev;   /* keep the frames coming while it flashes */
    }

    /* The warning's hold: the profile goes into force when it completes. */
    if (s.warn.open && s.warn.down) {
        ++s.warn.rev;
        if (ui_hold_tick(&s.warn.hold, dt_s)) {
            s.warn.open = false;
            s.warn.down = false;
            ov_let_go();
            apply_profile(s.warn.type, s.warn.hz);
        }
    }

    if (s.driving) {
        s.pulse += dt_s * 3.6f;         /* a little under two seconds a cycle */
        if (s.pulse > 6.28318f) {
            s.pulse -= 6.28318f;
        }
    }

    /*
     * With nothing reporting, the arm still must not arrive before a servo
     * could: it chases the command at the configured speed instead of
     * snapping to it, which is the only thing standing in for travel time
     * when there is no hardware to measure.  With feedback this does nothing,
     * because the measurement has already placed the arm.
     */
    if (s.have_feedback) {
        s.shown_cmd = us_to_cmd(deg_to_us_f(s.shown_deg));
        return;
    }
    /*
     * In the units the far end slews in -- its command across the range a
     * command carries -- and at its rate, SPEED_FULL_SPAN_S at 100%.  An
     * angle a second would not be: with CENTRE off the middle a degree is a
     * different share of the command on either side, and HOLD, which keeps
     * the servo where the horn is drawn, would keep it somewhere else.
     */
    const float step = (float)SPEED_FULL_SPAN_S * (float)s.speed_pct / 100.0f
                       * dt_s;
    const float want = us_to_cmd(deg_to_us_f(s.commanded_deg));
    const float d = want - s.shown_cmd;

    if (fabsf(d) <= step) {
        s.shown_cmd = want;
        if (s.shown_deg != s.commanded_deg) {
            s.shown_deg = s.commanded_deg;
            ++s.ctrl_rev;
        }
        return;
    }
    s.shown_cmd += (d > 0.0f) ? step : -step;
    s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
    ++s.ctrl_rev;
}

/* Draw @p fn clipped to @p box. */
static void clipped(gfx_canvas_t *c, gfx_rect_t box, void (*fn)(gfx_canvas_t *))
{
    gfx_rect_t old_clip = c->clip;
    if (gfx_clip_set(c, box)) {
        fn(c);
    }
    c->clip = old_clip;
}

static bool page_shown(void)
{
    return s.ov_open && !s.warn.open && !s.kp.open && !s.tk.open && !s.ch.open;
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_card(c, (gfx_rect_t){ PAD, PAD, LCARD_W, (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        ui_card(c, (gfx_rect_t){ RCARD_X, PAD, RCARD_W,
                                 (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        s.drawn_mask |= bit;
        s.drawn_ctrl[buf]  = UINT32_MAX;
        s.drawn_power[buf] = UINT32_MAX;
    }

    /* The keypad and the keyboard keep their own counts of what changed. */
    const uint32_t ov_rev = s.ctrl_rev + s.kp.revision + s.tk.revision;
    if (s.drawn_ctrl[buf] != ov_rev) {
        /* The plot when it moved or this buffer has never had it. */
        const bool power = (s.drawn_power[buf] != s.power_rev);
        s.drawn_ctrl[buf]  = ov_rev;
        s.drawn_arm[buf]   = s.arm_rev;
        s.drawn_power[buf] = s.power_rev;
        s.drawn_warn[buf]  = s.warn.rev;
        s.drawn_save[buf]  = save_state();
        s.drawn_pulse[buf] = (int)(s.pulse * 8.0f);
        if (s.ov_open) {
            draw_overlay(c);
        } else {
            draw_left(c);
        }
        draw_right(c, power);
        if (s.arm.flash_left > 0) {
            ui_hold_flash_step(&s.arm);
        }
        return;
    }

    /*
     * Otherwise only what moved on its own, each clipped to itself: ARM's
     * fade, the warning's hold, the power plot, the save line and the
     * grip.  The right card is 292 x 420 and the fades run for two seconds:
     * asking for whole cards every frame would spend most of the panel's
     * bandwidth on a button.
     */
    if (s.drawn_arm[buf] != s.arm_rev) {
        s.drawn_arm[buf] = s.arm_rev;
        clipped(c, s.arm_btn, draw_arm);
        if (s.arm.flash_left > 0) {
            ui_hold_flash_step(&s.arm);
        }
    }
    if (s.drawn_power[buf] != s.power_rev) {
        s.drawn_power[buf] = s.power_rev;
        clipped(c, power_rect(), draw_power);
    }
    if (s.ov_open && s.warn.open && s.drawn_warn[buf] != s.warn.rev) {
        s.drawn_warn[buf] = s.warn.rev;
        clipped(c, warn_apply_rect(), draw_warn_apply);
    }
    if (page_shown() && s.drawn_save[buf] != save_state()) {
        s.drawn_save[buf] = save_state();
        clipped(c, save_line_rect(), draw_save_line);
    }
    if (s.ov_open) {
        return;
    }

    /*
     * The grip, only while it breathes.  Clipped to the tip: repainting the
     * 488x418 card at the frame rate to animate a ring of 36 px radius would
     * cost most of the panel's bandwidth.
     */
    const int grip_r = 36;
    const int step = (int)(s.pulse * 8.0f);
    if (!s.driving || s.drawn_pulse[buf] == step) {
        return;
    }
    s.drawn_pulse[buf] = step;
    int tx, ty;
    at(s.shown_deg, HORN_L, &tx, &ty);
    const gfx_rect_t box = { (int16_t)(tx - grip_r), (int16_t)(ty - grip_r),
                             (int16_t)(grip_r * 2), (int16_t)(grip_r * 2) };
    clipped(c, box, draw_left);
}

/*
 * Leaving releases the output: a screen that is not visible must not hold
 * the servo somewhere, the same rule as the motor bench's disarm on leave.
 * The overlay closes with it, and a warning not held is a profile not
 * applied.
 */
static void leave(void)
{
    /* No release arrives for a finger on the speed slider as the screen
     * changes, and a latched drag outlives the gesture. */
    ui_slider_release(&s.speed);
    /* Disarm rather than release: navigating away from an armed bench must
     * not leave it armed behind a screen that is not visible, and the
     * disarm lets go of the output on its way. */
    post(SERVO_CMD_DISARM, 0);
    s.armed = false;
    ui_hold_reset(&s.arm);
    s.arm_down = false;
    s.sweeping = false;
    s.ov_open = false;
    close_panels();
    ui_tabs_cancel(&s.tabs);
    s.ov_have = false;
    s.ov_pressed = OP_NONE;
    ++s.arm_rev;
    ++s.ctrl_rev;
}

/*
 * Touch events were lost between two frames, so this screen's record of what
 * is on the glass cannot be trusted.  Drop the gesture rather than let a
 * hold that completes on a timer finish on a contact that may have gone.
 * Nothing is commanded here: a gesture abandoned part way asks for nothing,
 * which is what letting go early already does.
 */
static void cancel(void)
{
    /* Disarming is a press, so its release is the whole command: one that
     * went missing is a DISARM the operator made and the bench never saw.
     * Arming has already sent its command by the time the finger lifts, so
     * cancelling one part way asks for nothing, which is correct. */
    if (s.armed && s.arm_down && !s.arm.fired) {
        post(SERVO_CMD_DISARM, 0);
    }
    /* And an arm posted but not yet collected: a command is forwarded on the
     * frame after the one that posted it, and the frame that observes a loss
     * cancels before that forwarding.  A disarm is kept. */
    if (s.pending.kind == SERVO_CMD_ARM) {
        s.pending.kind  = SERVO_CMD_NONE;
        s.arm_in_flight = false;
    }
    /* And a sweep: the event that went missing may be the HOLD that was to
     * stop it, and the panel would go on repeating it.  Held where the
     * output has got to, as HOLD holds it. */
    if (s.sweeping) {
        stop_sweep();
        command(s.shown_deg);
    }
    ui_slider_release(&s.speed);
    ui_hold_reset(&s.arm);
    s.arm_down = false;
    /*
     * And the dial.  A drag left latched owns its track id, and the GT911
     * reuses ids: a later contact that began somewhere else would satisfy
     * the drag path and command a position with no press on the dial, which
     * on an armed bench moves the servo.
     */
    s.dragging = false;
    /* And the overlay's press: the warning's hold above all, which must not
     * complete on a contact that may have gone. */
    ui_keypad_cancel_press(&s.kp);
    ui_textkey_cancel_press(&s.tk);
    ui_tabs_cancel(&s.tabs);
    ui_hold_reset(&s.warn.hold);
    s.warn.down = false;
    ++s.warn.rev;
    s.ov_have = false;
    s.ov_pressed = OP_NONE;
    ++s.arm_rev;
    ++s.ctrl_rev;
}

static const ui_screen_t k_screen = {
    .title  = "SERVO",
    .reset  = reset,
    .enter  = NULL,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *servo_screen(void) { return &k_screen; }
