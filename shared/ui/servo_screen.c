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
 * START TEST, on the TEST page, runs the automatic test (servo_test.h): the
 * screen hands it the supply's samples and the clock, and does what it asks
 * -- the servo's position, SUPPLY's set points and output -- through the
 * same paths a finger uses.  Its progress and result are on the left card.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "log_name.h"
#include "outputs.h"
#include "rcbench_version.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_text.h"
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
 * force, the supply's live power, its set points and output switch, then the
 * servo's controls.
 */
#define RC_X      (RCARD_X + 12)
#define RC_W      (RCARD_W - 24)
#define SETB_W    96
#define TAG_Y     118
#define TAG_H     24
#define PWR_TXT_Y 150
#define PWR_Y     170
#define PWR_H     48
/* The supply's set points, each a value the keypad opens on, and OUTPUT ON
 * and OFF: the same set points and switch as SUPPLY's, not a copy. */
#define SUP_Y     224
#define SUP_H     26
#define SUP_VAL_X (RC_X + 32)
#define SUP_VAL_W 66
#define SUP_OUT_X (SUP_VAL_X + 2 * (SUP_VAL_W + 4))
/* The top of a standard servo's rating.  A set point raised past it is
 * applied only after the HV warning is held for two seconds. */
#define STD_SERVO_V_MAX 6.0f

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
static const ui_text_id_t k_pages[PG_COUNT] = {
    TX_SV_PG_OUTPUT, TX_SV_PG_TEST, TX_SV_PG_LIMITS, TX_SV_PG_DUT,
};

/* What a settings row edits. */
enum { R_TYPE = 0, R_RATE, R_MIN, R_CENTRE, R_MAX, R_TRIM, R_TRAVEL,
       R_REVERSE, R_SETTING, R_TEXT, R_HV };

typedef struct {
    uint8_t      page;
    uint8_t      kind;
    setting_id_t id;          /* R_SETTING                              */
    ui_text_id_t label;
    uint8_t      col, row;
    bool         wide;        /* the value takes both columns' width    */
} ov_row_t;

static const ov_row_t k_rows[] = {
    { PG_OUTPUT, R_TYPE,    SETTING_COUNT, TX_SV_ROW_TYPE,  0, 0, true  },
    { PG_OUTPUT, R_RATE,    SETTING_COUNT, TX_SV_ROW_RATE,  0, 1, true  },
    { PG_OUTPUT, R_MIN,     SETTING_COUNT, TX_SV_ROW_MIN,  0, 2, false },
    { PG_OUTPUT, R_CENTRE,  SETTING_COUNT, TX_SV_ROW_CENTRE,  1, 2, false },
    { PG_OUTPUT, R_MAX,     SETTING_COUNT, TX_SV_ROW_MAX,  0, 3, false },
    { PG_OUTPUT, R_TRIM,    SETTING_COUNT, TX_SV_ROW_TRIM,  1, 3, false },
    { PG_OUTPUT, R_TRAVEL,  SETTING_COUNT, TX_SV_ROW_TRAVEL,  0, 4, false },
    { PG_OUTPUT, R_REVERSE, SETTING_COUNT, TX_SV_ROW_REVERSE,  1, 4, false },

    { PG_TEST, R_SETTING, SET_SERVO_CURVE,       TX_SV_ROW_CURVE,  0, 0, false },
    { PG_TEST, R_SETTING, SET_SERVO_TEST_HZ,     TX_SV_ROW_SPEED,  0, 1, false },
    { PG_TEST, R_SETTING, SET_SERVO_TEST_RANGE,  TX_SV_ROW_RANGE,  0, 2, false },
    { PG_TEST, R_SETTING, SET_SERVO_LEN_BY,      TX_SV_ROW_LEN_BY,  0, 3, false },
    { PG_TEST, R_SETTING, SET_SERVO_LEN_S,       TX_SV_ROW_LEN_S,  0, 4, false },
    { PG_TEST, R_SETTING, SET_SERVO_LEN_MOVES,   TX_SV_ROW_MOVES,  0, 5, false },
    { PG_TEST, R_SETTING, SET_SERVO_DWELL_MS,    TX_SV_ROW_DWELL,  0, 6, false },
    { PG_TEST, R_SETTING, SET_SERVO_SETTLE_MS,   TX_SV_ROW_SETTLE,  0, 7, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_48,     TX_SV_ROW_STEP_48,  1, 0, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_60,     TX_SV_ROW_STEP_60,  1, 1, false },
    { PG_TEST, R_HV,      SETTING_COUNT,         TX_SV_ROW_HV,  1, 2, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_74,     TX_SV_ROW_STEP_74,  1, 3, false },
    { PG_TEST, R_SETTING, SET_SERVO_STEP_84,     TX_SV_ROW_STEP_84,  1, 4, false },
    { PG_TEST, R_SETTING, SET_SERVO_BROWNOUT,    TX_SV_ROW_BROWNOUT,  1, 5, false },

    { PG_LIMITS, R_SETTING, SET_SUPPLY_V_MAX,        TX_SV_ROW_V_MAX,  0, 0, false },
    { PG_LIMITS, R_SETTING, SET_SUPPLY_I_MAX,        TX_SV_ROW_I_MAX,  0, 1, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_STALL_A,       TX_SV_ROW_STALL,  0, 2, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_IDLE_MAX,      TX_SV_ROW_IDLE,  1, 0, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_HOLD_MAX,      TX_SV_ROW_HOLD,  1, 1, false },
    { PG_LIMITS, R_SETTING, SET_SERVO_TRAVEL_MAX_MS, TX_SV_ROW_TRAVEL_TIME,  1, 2, false },

    { PG_DUT, R_TEXT,    SETTING_COUNT,    TX_SV_ROW_NAME,  0, 0, true  },
    { PG_DUT, R_SETTING, SET_SERVO_REPORT, TX_SV_ROW_REPORT,  0, 1, false },
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
       OP_WARN_CANCEL, OP_SETTINGS, OP_SUP_V, OP_SUP_I, OP_ASK_APPLY,
       OP_ASK_CANCEL, OP_TEST_START };

/* What the list chooses, and what the keypad types. */
enum { CH_NONE = 0, CH_TYPE, CH_RATE, CH_ENUM };
enum { KT_NONE = 0, KT_MIN, KT_CENTRE, KT_MAX, KT_TRAVEL, KT_RATE,
       KT_SETTING, KT_SUP_V, KT_SUP_I };

/* What the question or the HV warning applies: a typed set point, the
 * supply's output switched on, or the automatic test started with steps
 * above 6.0 V. */
enum { ASK_SET = 0, ASK_ON, ASK_TEST };

#define CHOICE_MAX 10

/* What a command means as a pulse and an angle: the profile's pulses, trim,
 * reverse and travel, as they were when a command was sent. */
typedef struct {
    uint16_t min_us, centre_us, max_us;
    int16_t  trim_us;
    bool     reverse;
    float    travel_deg;
} servo_map_t;

/* A curve the far end runs and this screen no longer does; see s.dr. */
typedef struct {
    bool        on;
    sweep_t     sw;
    uint32_t    from_ms;
    int         speed_pct;
    servo_map_t map;
    float       out;
} sweep_drain_t;

/* The newest start or resume sent, as sent; see s.start_rec. */
typedef struct {
    bool        live;
    uint16_t    seq;
    sweep_cfg_t cfg;
    int         speed_pct;
} start_rec_t;

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
    /* The knob's position command waiting in `pending`: valid while
     * post_count is still knob_post, with what to put back if it is
     * withdrawn -- the commanded angle before the knob, and whether a
     * position was pending before it. */
    bool     knob_live;
    bool     knob_had_cmd;
    uint32_t knob_post;
    float    knob_prev_deg;
    /* A finger owned the dial at some point since knob_frame(). */
    bool     knob_finger;

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

    /*
     * The supply's set points and output, SUPPLY's own: OUTPUT ON is the
     * same two-second hold, OFF the same tap.  What is drawn is compared
     * with SUPPLY every frame, so a change made there, a cap that moved or
     * the output going off shows here at once.
     */
    gfx_rect_t out_btn;
    ui_hold_t  out_hold;
    bool       out_down;
    int        out_id;
    bool       out_press_on;  /**< the press began as OUTPUT OFF       */
    bool       out_on;        /**< the output as last drawn              */
    float      sup_v, sup_i;  /**< the set points as last seen           */
    uint32_t   sup_rev;
    uint32_t   drawn_sup[2];
    bool       kp_alone;      /**< the keypad opened the overlay itself  */
    /* A set point waiting for APPLY: typed while the output is live, or a
     * voltage raised past a standard servo's rating (hv), which takes the
     * two-second hold the profile warning takes.  OUTPUT ON with a set
     * point past that rating waits on the same hold (ASK_ON). */
    struct {
        bool      open;
        bool      hv;
        int       purpose;   /**< ASK_SET, ASK_ON or ASK_TEST          */
        int       target;    /**< KT_SUP_V or KT_SUP_I: what was typed */
        uint32_t  off_count; /**< supply_screen_off_count() at asking  */
        uint32_t  stops;     /**< the screen's stop count at asking     */
        float     v, i;
        ui_hold_t hold;
        bool      down;
        uint32_t  rev;
    } ask;
    uint32_t drawn_ask[2];
    /* Stops seen (servo_screen_cancel_arm()), for the question above. */
    uint32_t stops;

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
        char  title[48];
        char  labels[CHOICE_MAX][32];
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
    /*
     * The pause whose HOLD what the screen expects of the far end rests on,
     * or 0.  Set by a PAUSE of a sweep the far end is known to run.  Kept
     * by everything that carries that pause on: its resume and the resume
     * said again, a position said again under a changed profile, and a
     * further PAUSE before the resume is acknowledged -- that HOLD holds
     * what the first pause left.  Ended by the acknowledgement of a start
     * or a resume (the far end then moves on its own), and by anything
     * that sets a new origin: stop_sweep() -- a drag, CENTRE, RELEASE, a
     * curve restarted, a disarm, leaving, the link going -- and
     * start_sweep() -- SWEEP, or a PAUSED that starts a changed curve.
     * Every command carries it (from_pause); the panel drops those of a
     * pause it lets go of, and the screen takes that let-go exactly while
     * it is still set to that pause (servo_screen_released()).
     */
    uint16_t    lineage;
    bool        surfaces;        /* a surface is bound to sweep          */
    /* The tap's phase of the pause in force, and for a resume asked before
     * that pause's HOLD was acknowledged, where its replay starts: the
     * output the far end held, once that acknowledgement says it. */
    uint32_t    pause_tap_ms;
    bool        resume_from_set;
    uint16_t    resume_pause_seq;
    float       resume_from_cmd;
    bool        sweeping;
    bool        paused;          /* a sweep held by PAUSE: HOLD posted   */
    /* SPEED as the sweep ran when PAUSE was tapped: the far end slews at it
     * until the HOLD reaches it, whatever the slider says meanwhile, since
     * a paused slider sends nothing. */
    int         pause_speed_pct;
    /* And where the output was drawn at the tap, which the replay of the
     * acknowledgement interval starts from. */
    float       pause_from_cmd;
    /* Which pause stands, so an acknowledgement is applied to its own. */
    uint16_t    pause_seq;
    bool        sweep_ended;     /* the next command ends it             */
    sweep_t     sw;
    uint32_t    clock_ms;
    float       clock_frac_ms;
    /* Without feedback: the last second and a half of where the output
     * has been drawn, to start from where the far end's output starts. */
    struct { uint32_t t; float cmd; } trail[64];
    uint8_t     trail_head, trail_n;
    /*
     * A start or a resume asked for and not yet acknowledged: the far end
     * is not known to move yet, so the curve is not drawn; the output goes
     * on as it was (servo_screen_sweep_started()).  The phase the curve
     * would begin moving from: 0 for a start, the kept one for a resume.
     */
    bool        awaiting;
    uint32_t    await_phase_ms;
    /*
     * The far end runs a curve this screen has stopped drawing as its own:
     * a PAUSE whose HOLD is not yet acknowledged, or a curve replaced by a
     * start not yet acknowledged.  It is drawn on as the far end runs it --
     * its curve, SPEED and the pulse mapping its commands were sent under
     * -- for at most OUT_DEFAULT_TIMEOUT_MS, the longest the far end runs a
     * sweep it has not heard.  out is the output in that mapping's command
     * units, chased as the far end slews it.
     */
    sweep_drain_t dr;
    /* The mapping the last sweep command was sent under. */
    servo_map_t posted_map;
    /*
     * Sweep commands are numbered; the newest start or resume is kept with
     * the curve it was sent with, and the wait for it is ended by its own
     * acknowledgement or a later repeat's (servo_screen_sweep_started()).
     * The SPEED each of the last 8 sweep commands was sent with, by
     * number, for the one acknowledged.
     */
    uint16_t    start_seq;
    start_rec_t start_rec;
    struct { uint16_t seq; int16_t speed_pct; } sent_speed[8];
    /*
     * The sweep button's last tap and the screen as it was before it, while
     * its command has not left: a second tap in the same pass undoes both,
     * since the far end never heard the first.  post_count counts commands
     * posted, so another command posted since makes the tap's command gone.
     */
    struct {
        servo_cmd_t pending;
        sweep_t     sw;
        bool        sweeping, paused, awaiting, sweep_ended, driving;
        uint32_t    await_phase_ms;
        float       commanded_deg;
        sweep_drain_t dr;
        start_rec_t start_rec;
        uint16_t    pause_seq, resume_pause_seq, lineage;
        uint32_t    pause_tap_ms;
        float       pause_from_cmd, resume_from_cmd;
        int         pause_speed_pct;
        bool        resume_from_set;
    }           undo;
    bool        toggle_live;
    servo_cmd_kind_t toggle_kind;
    uint32_t    toggle_post;
    uint32_t    post_count;

    /* Changes to what a command carries -- the profile, the pulses, trim,
     * travel -- and how many there had been when ARM was asked for. */
    uint32_t profile_rev;
    uint32_t arm_profile_rev;
    /* An ARM posted whose arm has not landed yet: collected by the panel,
     * which arms only once the release it owes has been written. */
    bool     arm_in_flight;

    /*
     * The automatic test: the run, START TEST's hold, HV SERVO, and what the
     * left card shows of it.  HV SERVO is for the session only and off at
     * every restart, as a profile that can destroy a servo is.
     */
    servo_test_t test;
    bool      test_hv;       /**< HV SERVO: 7.4 and 8.4 V run          */
    ui_hold_t test_hold;
    bool      test_down;
    uint32_t  test_rev;
    uint32_t  drawn_test[2];
    uint32_t  now_ms;        /**< the panel's clock, servo_screen_clock() */
    bool      have_now;
    bool      link_up;
    int       test_note;     /**< servo_str_t the engine refused a START
                                  with, 0 none; test_blocked() is live  */
    int       test_file;     /**< its files' number; 0 not yet, -1 none */
    bool      test_report;   /**< the card took its .TXT whole          */
    int       test_blocked_seen; /**< test_blocked() at the last tick   */
    bool      test_online_seen;  /**< the supply answering, likewise    */
    bool      test_box;      /**< the left card shows the run          */
    bool      test_seen;     /**< the run was running when last looked */
    /*
     * SUPPLY's set points go back to what they were before a run once it
     * is over, whichever screen is up (servo_screen_service()): only once
     * its OFF has gone, a sample taken after that shows the output off,
     * nothing would switch it on, and nobody has set them since it ended.
     */
    bool      test_restore;
    uint8_t   restore_stage; /**< RS_* below                             */
    uint32_t  restore_ms;    /**< the clock at the stage's start         */
    float     test_v0, test_i0;  /**< before the run                     */
    float     test_v1, test_i1;  /**< as the run left them               */
    uint32_t  test_sig;      /**< what the box last showed              */

    uint32_t ctrl_rev;
    uint32_t drawn_ctrl[2];
    unsigned drawn_mask;
} s;

/* The restore's stages: the OFF the run's end posted not yet taken; taken,
 * waiting for the next frame; waiting for a sample taken since; a sample
 * showed the output off. */
enum { RS_OFF_POSTED = 0, RS_OFF_SENT, RS_SAMPLE, RS_OFF_SEEN };

/* The clock the run and the supply's readings share. */
static uint32_t test_now(void)
{
    return s.have_now ? s.now_ms : s.clock_ms;
}

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

/* The mapping in force now. */
static servo_map_t map_now(void)
{
    const servo_map_t m = { s.min_us, s.centre_us, s.max_us, s.trim_us,
                            s.reverse, s.travel_deg };
    return m;
}

/* A command of @p m's range as a pulse, and a pulse as @p m's angle: what
 * cmd_to_us() and us_to_deg_f() are for the mapping in force. */
static float map_cmd_to_us(const servo_map_t *m, float cmd)
{
    const unsigned below = (unsigned)m->centre_us - (unsigned)m->min_us;
    const unsigned above = (unsigned)m->max_us - (unsigned)m->centre_us;
    const float half = (float)((below > above) ? below : above);
    return (float)m->centre_us - half + 2.0f * half * cmd / (float)OUT_SPAN;
}

static float map_us_to_deg(const servo_map_t *m, float us)
{
    const float off = us - (float)m->centre_us - (float)m->trim_us;
    const float half = (off < 0.0f)
                       ? (float)m->centre_us - (float)m->min_us
                       : (float)m->max_us - (float)m->centre_us;
    if (half <= 0.0f) {
        return 0.0f;
    }
    float d = off * 90.0f / half;
    if (m->reverse) {
        d = -d;
    }
    if (d < -m->travel_deg) { d = -m->travel_deg; }
    if (d >  m->travel_deg) { d =  m->travel_deg; }
    return d;
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
 * SPEED_FULL_SPAN_S, two spans a second, is 360 degrees a second.  Below
 * 100% the bench ramps the command at that fraction of it, and a servo
 * asked for 30% takes three times as long to cross as one asked for 90%.
 *
 * 100% is immediate rather than two spans a second, at the far end and in
 * the drawing (slew_per_ms()).  It is the value the screen starts at, so
 * anybody who never touches the slider gets the servo at its own rate,
 * with nothing in front of it.
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
    /* A resume not yet taken stays one when the sweep is said again. */
    const bool resume = kind == SERVO_CMD_SWEEP
                        && s.pending.kind == SERVO_CMD_SWEEP
                        && s.pending.resume;
    /* And a start not yet taken stays the start, as it will be sent. */
    const bool carry = kind == SERVO_CMD_SWEEP
                       && s.pending.kind == SERVO_CMD_SWEEP
                       && s.start_rec.live
                       && s.pending.start_seq == s.start_rec.seq;
    /* Any command but the HOLD replaces the curve at the far end: what is
     * drawn stops being drawn on. */
    if (kind != SERVO_CMD_HOLD) {
        s.dr.on = false;
    }
    s.pending.kind       = kind;
    s.pending.value_us   = us;
    s.pending.resume     = resume;
    s.pending.from_pause = s.lineage;
    ++s.post_count;
    if (kind == SERVO_CMD_SWEEP) {
        ++s.start_seq;
        s.pending.start_seq = s.start_seq;
        s.sent_speed[s.start_seq % 8u].seq       = s.start_seq;
        s.sent_speed[s.start_seq % 8u].speed_pct = (int16_t)s.speed_pct;
        if (carry) {
            s.start_rec.seq       = s.start_seq;
            s.start_rec.cfg       = s.sw.cfg;
            s.start_rec.speed_pct = s.speed_pct;
        }
        s.posted_map = map_now();
    }
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
                 || kind == SERVO_CMD_SWEEP || kind == SERVO_CMD_HOLD);
    /* Paused while the hold is the command in force: anything sent after it
     * replaces it at the far end, and the button goes with it. */
    if (s.paused != (kind == SERVO_CMD_HOLD)) {
        s.paused = (kind == SERVO_CMD_HOLD);
        ++s.ctrl_rev;
    }
    const bool sw = kind == SERVO_CMD_SWEEP;
    s.pending.ends_sweep     = s.sweep_ended && !sw;
    if (!sw) {
        s.sweep_ended = false;
    }
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

/* The sweep ended, running or paused.  A pause is ended by the command
 * that follows; the button reads SWEEP from now. */
static void stop_sweep(void)
{
    s.lineage         = 0u;
    s.resume_from_set = false;
    s.awaiting       = false;
    s.dr.on          = false;
    s.start_rec.live = false;
    if (s.sweeping) {
        s.sweeping    = false;
        s.sweep_ended = true;
        ++s.ctrl_rev;
    }
    if (s.paused) {
        s.paused = false;
        ++s.ctrl_rev;
    }
}

/*
 * PAUSE: the sweep stops and every surface stays where its output had got
 * to, by the link's HOLD.  The coprocessor holds it there, because only it
 * knows where that is: SPEED can leave the output well behind the curve, and
 * without feedback the horn drawn here is an estimate.  The drawing stops
 * where it was.  The phase is kept, here and at the far end, for
 * resume_sweep().
 */
static void hold_sweep(void)
{
    /* A PAUSE before the resume of an earlier pause is acknowledged holds
     * what that pause left, and stays on it. */
    const uint16_t root = s.lineage;
    const bool moving = !s.awaiting;
    if (!moving) {
        /* Not yet moving at the far end as far as is known here: paused
         * where it would have begun, and the HOLD's acknowledgement says
         * how far it got. */
        s.sw.running   = false;
        s.sw.paused    = true;
        s.sw.paused_ms = s.await_phase_ms;
    } else {
        s.dr.sw = s.sw;
        sweep_pause(&s.sw, s.clock_ms);
    }
    s.pause_tap_ms = s.sw.paused_ms;
    s.pause_speed_pct = s.speed_pct;
    s.dr.speed_pct    = s.speed_pct;
    s.dr.map          = map_now();
    s.dr.out          = s.shown_cmd;
    /* With feedback the reading itself: tick() turns it into a command
     * only on its next pass. */
    s.pause_from_cmd  = s.have_feedback ? us_to_cmd(deg_to_us_f(s.shown_deg))
                                        : s.shown_cmd;
    stop_sweep();
    /* Drawn on along the curve until the HOLD is acknowledged, as the far
     * end runs it; held where it is if it was not yet moving. */
    s.dr.on      = moving;
    s.dr.from_ms = s.clock_ms;
    s.commanded_deg = clamp_travel(s.shown_deg);
    ++s.pause_seq;
    if (s.pause_seq == 0u) {
        s.pause_seq = 1u;       /* 0 is no pause */
    }
    s.lineage = root;
    post(SERVO_CMD_HOLD, 0);
    if (s.pending.kind == SERVO_CMD_HOLD) {
        s.pending.pause_seq = s.pause_seq;
    }
    s.lineage = (root != 0u) ? root : s.pause_seq;
    ++s.ctrl_rev;
}

/* The start or resume just posted, as it will be sent. */
static void record_start(void)
{
    if (s.pending.kind != SERVO_CMD_SWEEP) {
        return;
    }
    s.start_rec.live      = true;
    s.start_rec.seq       = s.pending.start_seq;
    s.start_rec.cfg       = s.sw.cfg;
    s.start_rec.speed_pct = s.speed_pct;
}

/*
 * Start the sweep, or carry on with a changed one from its beginning, as the
 * coprocessor does.  Only on an armed bench and a coprocessor that sweeps:
 * the far end refuses one otherwise.  Drawn once the start is acknowledged
 * (servo_screen_sweep_started()).
 */
static void start_sweep(void)
{
    if (!s.armed || !s.sweep_able || !s.surfaces || !s.link_up) {
        return;
    }
    const sweep_cfg_t cfg = sweep_cfg_now();
    if (!sweep_start(&s.sw, &cfg, s.clock_ms)) {
        return;
    }
    s.sweeping       = true;
    s.awaiting       = true;
    s.await_phase_ms = 0u;
    s.lineage        = 0u;      /* a new origin */
    post(SERVO_CMD_SWEEP, 0);
    s.pending.resume = false;   /* a start, whatever it replaced */
    record_start();
    ++s.ctrl_rev;
}

/*
 * A running sweep's curve changed: the new one from its beginning, as the
 * coprocessor starts it when it is written.  Until that start is
 * acknowledged the far end runs the old curve, so the old curve is drawn on
 * (the drain, as for a PAUSE on its way) at the SPEED it ran at, for at
 * most OUT_DEFAULT_TIMEOUT_MS.
 */
static void restart_sweep(void)
{
    const bool was_drawn = s.sweeping && !s.awaiting;
    const sweep_drain_t old = {
        .on = true, .sw = s.sw, .from_ms = s.clock_ms,
        .speed_pct = s.speed_pct, .map = s.posted_map, .out = s.shown_cmd,
    };
    stop_sweep();
    start_sweep();
    if (s.sweeping && was_drawn) {
        s.dr = old;
    }
}

/*
 * Whether SPEED is slower than the fastest change the TEST page's curve asks
 * for, so the slew and not the curve shapes the sweep.  From the settings,
 * so it shows before SWEEP is pressed as well as while one runs: a running
 * sweep is this curve, started over when a setting changes.  The sweep's
 * amplitude and SPEED's slew are in the same units, the command across the
 * range a command carries.
 */
static bool speed_limits_sweep(void)
{
    const sweep_cfg_t cfg = sweep_cfg_now();
    return sweep_slew_limited(&cfg, slew_of(s.speed_pct));
}

static bool same_sweep(const sweep_cfg_t *a, const sweep_cfg_t *b)
{
    return a->kind == b->kind && a->mhz == b->mhz
           && a->amplitude == b->amplitude && a->dwell_ms == b->dwell_ms;
}

/*
 * A tap on PAUSED: the paused sweep carried on from where it was paused.
 * The command asks the panel for a resume; a coprocessor older than 4.6, or
 * one that refuses, starts the curve over instead.  Either way the far end
 * stays held until then, so the horn does too, and the acknowledgement
 * (servo_screen_sweep_started()) draws it from there.  A curve changed on
 * the TEST page since the pause is a new sweep.
 */
static void resume_sweep(void)
{
    const sweep_cfg_t now = sweep_cfg_now();
    if (!s.armed || !s.sweep_able || !same_sweep(&now, &s.sw.cfg)
        || !sweep_resume(&s.sw, s.clock_ms)) {
        start_sweep();
        return;
    }
    s.sweeping       = true;
    s.awaiting       = true;
    s.await_phase_ms = s.sw.paused_ms;
    /* A HOLD not yet acknowledged: the curve stops being drawn on here, and
     * the resume's acknowledgement times it from the far end's phase. */
    s.dr.on       = false;
    s.commanded_deg  = clamp_travel(s.shown_deg);
    s.resume_from_set  = true;
    s.resume_from_cmd  = s.shown_cmd;
    s.resume_pause_seq = s.pause_seq;
    post(SERVO_CMD_SWEEP, 0);
    record_start();
    s.pending.resume = s.pending.kind == SERVO_CMD_SWEEP;
    ++s.ctrl_rev;
}

/*
 * The sweep button: PAUSE pauses a running sweep, PAUSED carries it on, and
 * SWEEP starts the curve.  A tap whose own command has not left yet -- two
 * taps drained in one pass after a late frame -- is undone with it instead:
 * the far end heard neither, so nothing is sent and nothing here moves,
 * the curve's clock included.
 */
static void toggle_sweep(void)
{
    if (s.toggle_live && s.post_count == s.toggle_post
        && s.pending.kind == s.toggle_kind) {
        s.pending        = s.undo.pending;
        s.sw             = s.undo.sw;
        s.sweeping       = s.undo.sweeping;
        s.paused         = s.undo.paused;
        s.awaiting       = s.undo.awaiting;
        s.dr             = s.undo.dr;
        s.start_rec      = s.undo.start_rec;
        s.await_phase_ms = s.undo.await_phase_ms;
        s.sweep_ended    = s.undo.sweep_ended;
        s.driving        = s.undo.driving;
        s.commanded_deg  = s.undo.commanded_deg;
        s.pause_seq        = s.undo.pause_seq;
        s.pause_tap_ms     = s.undo.pause_tap_ms;
        s.pause_from_cmd   = s.undo.pause_from_cmd;
        s.pause_speed_pct  = s.undo.pause_speed_pct;
        s.resume_from_set  = s.undo.resume_from_set;
        s.resume_from_cmd  = s.undo.resume_from_cmd;
        s.resume_pause_seq = s.undo.resume_pause_seq;
        s.lineage          = s.undo.lineage;
        s.toggle_live    = false;
        ++s.ctrl_rev;
        return;
    }
    s.undo.pending        = s.pending;
    s.undo.sw             = s.sw;
    s.undo.sweeping       = s.sweeping;
    s.undo.paused         = s.paused;
    s.undo.awaiting       = s.awaiting;
    s.undo.dr             = s.dr;
    s.undo.start_rec      = s.start_rec;
    s.undo.await_phase_ms = s.await_phase_ms;
    s.undo.sweep_ended    = s.sweep_ended;
    s.undo.driving        = s.driving;
    s.undo.commanded_deg  = s.commanded_deg;
    s.undo.pause_seq        = s.pause_seq;
    s.undo.pause_tap_ms     = s.pause_tap_ms;
    s.undo.pause_from_cmd   = s.pause_from_cmd;
    s.undo.pause_speed_pct  = s.pause_speed_pct;
    s.undo.resume_from_set  = s.resume_from_set;
    s.undo.resume_from_cmd  = s.resume_from_cmd;
    s.undo.resume_pause_seq = s.resume_pause_seq;
    s.undo.lineage          = s.lineage;
    const uint32_t before = s.post_count;
    if (s.sweeping) {
        hold_sweep();
    } else if (s.paused) {
        resume_sweep();
    } else {
        start_sweep();
    }
    s.toggle_live = s.post_count != before;
    s.toggle_kind = s.pending.kind;
    s.toggle_post = s.post_count;
}

bool servo_screen_sweeping(void) { return s.sweeping; }

bool servo_screen_paused(void) { return s.paused; }

uint32_t servo_screen_curve_ms(void) { return s.clock_ms - s.sw.start_ms; }

void servo_screen_released(uint16_t pause_seq)
{
    /*
     * Nothing held any more, and the horn goes to the centre the surfaces
     * were released to -- while the screen is still on that pause.  The
     * panel dropped everything it asked since (its lineage), so the
     * surfaces are at rest.  Left since, by a drag, CENTRE, a new pause or
     * a new start, the screen's newer command was sent and is what the
     * surfaces do, so this changes nothing.
     */
    s.toggle_live = false;      /* no undo across a notification */
    if (pause_seq == 0u || pause_seq != s.lineage) {
        return;
    }
    stop_sweep();
    s.driving       = false;
    s.paused        = false;
    s.dr.on      = false;
    s.commanded_deg = 0.0f;
    ++s.ctrl_rev;
}

/*
 * The drawn output's slew at @p speed_pct, command units a millisecond, as
 * the far end's: SPEED_FULL_SPAN_S times the share, and at 100 % none --
 * the command goes straight through (slew_of()), which is negative here.
 */
static float slew_per_ms(int speed_pct)
{
    if (speed_pct >= 100) {
        return -1.0f;
    }
    return (float)slew_of(speed_pct) / 1000.0f;
}

/* One step of the drawn output towards @p want, at most @p step command
 * units: the slew the far end applies, as the drawing models it.  A
 * negative step is no slew: there at once. */
static float chase_cmd(float from, float want, float step)
{
    const float d = want - from;
    if (step < 0.0f || fabsf(d) <= step) {
        return want;
    }
    return from + ((d > 0.0f) ? step : -step);
}

/* Where the horn was drawn @p ago_ms ago, from the trail; the oldest kept
 * for anything older. */
static float shown_cmd_ago(uint32_t ago_ms)
{
    const uint32_t when = s.clock_ms - ago_ms;
    float best = s.shown_cmd;
    for (unsigned k = 0; k < s.trail_n; ++k) {
        const unsigned i = (s.trail_head + 64u - 1u - k) % 64u;
        best = s.trail[i].cmd;
        if ((int32_t)(s.trail[i].t - when) <= 0) {
            break;
        }
    }
    return best;
}

static float drawn_after(const sweep_t *w, uint32_t phase_ms, uint32_t ms,
                         float from, int speed_pct);

void servo_screen_sweep_started(uint16_t start_seq, uint32_t age_ms,
                                servo_sweep_from_t from, uint32_t since_ms)
{
    s.toggle_live = false;      /* no undo across a notification */
    /*
     * The far end's curve has phase 0 age_ms ago: this one is timed from
     * then, rather than from the tap that asked for it a queue and a few
     * transactions earlier.  Until now the far end was not known to move,
     * so the drawn output went on as it was; with nothing measuring it, it
     * is now worked on from where it was drawn when the far end began to
     * move -- age_ms ago, or since_ms ago for a resume -- along the curve
     * at SPEED, which the sweep's command carries.  For a sweep that froze
     * unrepeated, from where it froze, since_ms ago.
     */
    if (!s.sweeping) {
        return;
    }
    /*
     * Waiting for a start or a resume, the wait ends with the
     * acknowledgement of that command or of any sweep command posted after
     * it: a write of the start can fail and a later repeat start the sweep
     * at the far end instead.  A repeat never changes the curve -- a changed
     * curve is a new start (restart_sweep()) -- so the start's curve stands,
     * at the SPEED of the command acknowledged.  An earlier start's
     * acknowledgement describes a curve since replaced and is not taken.
     * A sweep already drawn takes any start the far end makes -- one that
     * froze unrepeated, or a link that came back -- at the SPEED in force.
     */
    int speed = s.speed_pct;
    if (s.awaiting) {
        const uint16_t after  = (uint16_t)(start_seq - s.start_rec.seq);
        const uint16_t posted = (uint16_t)(s.start_seq - s.start_rec.seq);
        if (!s.start_rec.live || after > posted) {
            return;
        }
        s.sw.cfg = s.start_rec.cfg;
        speed    = s.start_rec.speed_pct;
        if (s.sent_speed[start_seq % 8u].seq == start_seq) {
            speed = s.sent_speed[start_seq % 8u].speed_pct;
        }
    }
    s.start_rec.live = false;
    const uint32_t moving = (from == SERVO_SWEEP_RESUMED && since_ms < age_ms)
                            ? since_ms : age_ms;
    s.sw.start_ms = s.clock_ms - age_ms;
    s.sw.running  = true;
    s.sw.paused   = false;
    s.awaiting    = false;
    s.lineage     = 0u;         /* the far end moves on its own now */
    s.dr.on    = false;
    ++s.ctrl_rev;
    /* A resume's origin is for this acknowledgement only. */
    const bool from_resume = from == SERVO_SWEEP_RESUMED && s.resume_from_set;
    s.resume_from_set = false;
    if (s.have_feedback) {
        return;
    }
    /* A resume from where the far end held the output, which a late HOLD
     * acknowledgement can have said since the tap. */
    const float from_cmd = (from == SERVO_SWEEP_FROM_FROZEN)
                           ? shown_cmd_ago(since_ms)
                           : from_resume ? s.resume_from_cmd
                                         : shown_cmd_ago(moving);
    const uint32_t ms = (moving > 5000u) ? 5000u : moving;
    s.shown_cmd = drawn_after(&s.sw, age_ms - moving, ms, from_cmd, speed);
    s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
}

/*
 * The drawn output after @p ms more of the sweep @p w from @p phase_ms in,
 * starting at @p from: each step the curve's command, chased as tick()
 * chases it at @p speed_pct -- for a pause, the SPEED in force at the tap,
 * the far end's slew until the HOLD reached it.  Steps of 4 ms, under a
 * frame, so a turn of the curve is followed as the drawing follows it.
 */
static float drawn_after(const sweep_t *w, uint32_t phase_ms, uint32_t ms,
                         float from, int speed_pct)
{
    sweep_t run = *w;
    run.running  = true;
    run.paused   = false;
    run.start_ms = 0u;
    const float per_ms = slew_per_ms(speed_pct);
    float shown = from;
    for (uint32_t done = 0u; done < ms;) {
        const uint32_t dt = (ms - done < 4u) ? ms - done : 4u;
        done += dt;
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        (void)sweep_step(&run, phase_ms + done, &cmd);
        const float want = us_to_cmd(deg_to_us_f(clamp_travel(sweep_deg(cmd))));
        shown = chase_cmd(shown, want, per_ms * (float)dt);
    }
    return shown;
}

void servo_screen_sweep_held(uint16_t pause_seq, uint32_t kept_ms)
{
    s.toggle_live = false;      /* no undo across a notification */
    /*
     * The far end ran its curve on until the HOLD reached it, a queue and an
     * exchange after the tap; at 5 Hz that is a visible share of a cycle.
     * The panel times the phase it kept from the acknowledgements, as it
     * times a start, so the two errors are the same and cancel.
     *
     * Without feedback the horn is the drawing's estimate of the output, so
     * it moves on to where the output had got by then: the curve from the
     * tap's phase to the acknowledged one, slewed at SPEED as the drawing
     * slews.  Worked out here rather than by drawing the curve on until the
     * acknowledgement, which would need an end for a HOLD that is never
     * acknowledged; without one, the tap's estimate stands.  At most 5 s is
     * worked through, 1250 steps.
     */
    if (pause_seq != s.pause_seq) {
        return;
    }
    if (!s.paused && s.awaiting && s.resume_from_set
        && s.resume_pause_seq == pause_seq) {
        /*
         * PAUSED was tapped before this arrived: the far end ran on from
         * the tap and froze here, and stays frozen until the RESUME reaches
         * it.  Without feedback the output is drawn there now, and the
         * resume's acknowledgement replays from it rather than from the
         * tap's estimate.
         */
        if (s.have_feedback) {
            /* The reading, kept for the resume's replay in case feedback
             * goes before the resume is acknowledged. */
            s.resume_from_cmd = us_to_cmd(deg_to_us_f(s.shown_deg));
        } else {
            const uint32_t late = kept_ms - s.pause_tap_ms;
            const uint32_t ms = ((int32_t)late <= 0) ? 0u
                                : (late > 5000u) ? 5000u : late;
            s.resume_from_cmd = drawn_after(&s.sw, s.pause_tap_ms, ms,
                                            s.pause_from_cmd,
                                            s.pause_speed_pct);
            s.shown_cmd = s.resume_from_cmd;
            s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
            s.commanded_deg = clamp_travel(s.shown_deg);
            ++s.ctrl_rev;
        }
        return;
    }
    if (!s.paused || !s.sw.paused) {
        return;
    }
    /* Behind the tap's phase by this screen's own timing error: nothing to
     * replay, and the kept phase stands. */
    const uint32_t extra = kept_ms - s.sw.paused_ms;
    if (s.have_feedback) {
        /* The servo is where it reports, and is held there. */
        s.shown_cmd = us_to_cmd(deg_to_us_f(s.shown_deg));
    } else {
        const uint32_t ms = ((int32_t)extra <= 0) ? 0u
                            : (extra > 5000u) ? 5000u : extra;
        s.shown_cmd = drawn_after(&s.sw, s.sw.paused_ms, ms,
                                  s.pause_from_cmd, s.pause_speed_pct);
        s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
    }
    /* Either way the held angle is the one a changed profile says again
     * (reissue()), and the curve is no longer drawn on. */
    s.dr.on = false;
    s.commanded_deg = clamp_travel(s.shown_deg);
    s.sw.paused_ms = kept_ms;
    ++s.ctrl_rev;
}

void servo_screen_set_sweep(bool able)
{
    if (able == s.sweep_able) {
        return;
    }
    s.sweep_able = able;
    if (!able && (s.sweeping || s.paused)) {
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
static void test_end_now(servo_test_abort_t why);

static void reissue(void)
{
    /* A run times a servo driven as it was when it started. */
    test_end_now(SERVO_TEST_AB_SETTINGS);
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
            restart_sweep();
        }
        return;
    }
    if (s.driving) {
        /* Paused with the curve still drawn on: held where it is drawn. */
        if (s.dr.on) {
            s.dr.on = false;
            s.commanded_deg = clamp_travel(s.shown_deg);
        }
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
    /* Counted: a question about the live output stands only while this
     * count is the one it was asked under (ask_stands()). */
    ++s.stops;
    s.arm_in_flight = false;   /* the stop ends the arm on its way too */
    /* A run ends, and START TEST's hold with it. */
    test_end_now(SERVO_TEST_AB_STOP);
    if (s.test_down || s.test_hold.held_s > 0.0f) {
        ui_hold_reset(&s.test_hold);
        s.test_down = false;
        ++s.test_rev;
    }
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
    /* OUTPUT ON's hold too: the stop cuts the output, and a hold that ran
     * on would switch it back on.  An ON already posted is SUPPLY's to drop
     * (supply_screen_cancel_on()). */
    if (s.out_down || s.out_hold.held_s > 0.0f) {
        ui_hold_reset(&s.out_hold);
        s.out_down = false;
        ++s.sup_rev;
    }
    /* And the two warnings' holds: a voltage past a standard servo's rating
     * or a profile that can destroy one is not applied by a hold the stop
     * interrupted.  The warnings stay open; a new hold applies them. */
    /* An APPLY being pressed -- a tap on the question as well as a hold --
     * is let go of: its release must not apply a set point after the stop. */
    if (s.ov_have && (s.ov_pressed == OP_ASK_APPLY
                      || s.ov_pressed == OP_WARN_APPLY
                      || s.ov_pressed == OP_TEST_START)) {
        s.ov_have    = false;           /* the press is over, as ov_let_go() */
        s.ov_pressed = OP_NONE;
        ++s.ctrl_rev;
    }
    if (s.ask.down || s.warn.down) {
        ui_hold_reset(&s.ask.hold);
        ui_hold_reset(&s.warn.hold);
        s.ask.down  = false;
        s.warn.down = false;
        ++s.ask.rev;
        ++s.warn.rev;
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
    s.knob_live = false;
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
        /*
         * Paused, the servo is held where it is, and it goes on moving while
         * the HOLD is on its way and while it settles on the output held:
         * the angle a changed profile says again (reissue()) is the one it
         * reports, not the one it had at the tap.
         */
        if (s.paused) {
            const uint16_t was = deg_to_us(s.commanded_deg);
            s.commanded_deg = clamp_travel(deg);
            if (deg_to_us(s.commanded_deg) != was) {
                ++s.ctrl_rev;       /* COMMANDED shows it */
            }
        }
    }
    if (!same) {
        s.shown_q_deg = q_deg;
        s.shown_q_a   = q_a;
        ++s.ctrl_rev;
    }
}

static servo_test_reading_t test_reading_of(const supply_state_t *st)
{
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v        = st->v;
    r.i        = st->i;
    r.set_v    = st->set_v;
    r.set_i    = st->set_i;
    r.mode     = (uint8_t)st->mode;
    r.output   = st->output;
    r.online   = st->online;
    r.ok       = (st->ok & (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT))
                 == (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    r.trip     = st->trip;
    r.samples  = st->samples;
    /* Stamped where the panel had it; a sample without a stamp at its
     * arrival. */
    r.taken_ms = (st->taken_ms != 0u) ? st->taken_ms : test_now();
    return r;
}

void servo_screen_supply(const supply_state_t *st)
{
    if (st == NULL) {
        return;
    }
    s.sup = *st;
    s.have_sup = true;
    /* A restore waits for a sample taken after the run's OFF went in which
     * the supply itself reports the output off.  The panel's own flag is
     * its request: the PD mini switches off a link exchange and a module
     * transaction behind it, and a set point put back before then reaches
     * an output that is still on. */
    if (s.test_restore && s.restore_stage == RS_SAMPLE && !st->output
        && st->online && st->mode == SUPPLY_MODE_OFF
        && (int32_t)(test_reading_of(st).taken_ms - s.restore_ms) >= 0) {
        s.restore_stage = RS_OFF_SEEN;
    }
    /* The run's reading, with the horn's position where it is measured. */
    const servo_test_reading_t r = test_reading_of(st);
    servo_test_reading(&s.test, &r,
                       s.have_feedback ? deg_to_us(s.measured_deg) : 0u);
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

uint16_t servo_screen_drawn(void) { return deg_to_us(s.shown_deg); }

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
        s.drawn_sup[b]   = UINT32_MAX;
        s.drawn_warn[b]  = UINT32_MAX;
        s.drawn_ask[b]   = UINT32_MAX;
        s.drawn_test[b]  = UINT32_MAX;
        s.drawn_save[b]  = 0xFFu;
        /* No step the arm can be drawn at, so the next frame draws it. */
        s.drawn_pulse[b] = -1;
    }
}

static gfx_rect_t overlay_area(void)
{
    return (gfx_rect_t){ OV_X, OV_Y, OV_W, OV_H };
}

static gfx_rect_t sup_row_rect(void)
{
    return (gfx_rect_t){ RC_X, SUP_Y, RC_W, SUP_H };
}

/* The voltage's value (0) or the current limit's (1): tapped, the keypad. */
static gfx_rect_t sup_val_rect(int k)
{
    return (gfx_rect_t){ (int16_t)(SUP_VAL_X + k * (SUP_VAL_W + 4)), SUP_Y,
                         SUP_VAL_W, SUP_H };
}

static uint32_t test_signature(void);

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    servo_test_init(&s.test);
    s.test_sig = test_signature();
    servo_invalidate();
    s.drawn_mask    = 0;
    s.travel_deg    = 90.0f;
    s.speed_pct     = 100;
    s.surfaces      = true;
    s.link_up       = true;     /* until the panel says otherwise */
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
    s.out_btn     = (gfx_rect_t){ SUP_OUT_X, SUP_Y,
                                  (int16_t)(RC_X + RC_W - SUP_OUT_X), SUP_H };
    s.set_btn     = (gfx_rect_t){ (int16_t)(RCARD_X + RCARD_W - 12 - SETB_W),
                                  12, SETB_W, 24 };

    ui_plot_init(&s.power, k_power, PS_COUNT, (float)RC_W / SAMPLE_HZ);
    ui_tabs_init_text(&s.tabs, k_pages, PG_COUNT,
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

/* START TEST, under the TEST page's right column. */
static gfx_rect_t test_start_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + 20 + OV_COL_W),
                         (int16_t)(OV_ROW0 + 6 * OV_PITCH), (int16_t)OV_COL_W,
                         OV_ROW_H };
}

/* The run on the left card, clear of the dial and the case, and its one
 * button: STOP TEST while it runs, CLOSE after. */
static gfx_rect_t test_box_rect(void)
{
    return (gfx_rect_t){ PAD + 10, PAD + 10, 270, 112 };
}

static gfx_rect_t test_btn_rect(void)
{
    const gfx_rect_t b = test_box_rect();
    return (gfx_rect_t){ (int16_t)(b.x + 10), (int16_t)(b.y + 72), 130, 32 };
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
    s.ask.open  = false;    /* a set point not applied is dropped */
    s.ask.down  = false;
    ui_hold_reset(&s.ask.hold);
}

/* The overlay goes when the keypad that opened it for a set point is done:
 * the settings were not open, so there is nothing to go back to. */
static void close_alone(void)
{
    if (s.kp_alone) {
        s.kp_alone = false;
        s.ov_open  = false;
        close_panels();
        servo_invalidate();
    }
}

/* A set point tapped: the keypad, over the left card as the settings' is. */
static void open_set_point(int target)
{
    if (!s.ov_open) {
        s.ov_open  = true;
        s.kp_alone = true;
    }
    close_panels();
    const supply_caps_t caps = supply_screen_caps();
    if (target == KT_SUP_V) {
        ui_keypad_open(&s.kp, overlay_area(), TR(SUP_VOLTAGE), "V",
                       supply_screen_set_v(), caps.v_min, caps.v_max, 2);
    } else {
        ui_keypad_open(&s.kp, overlay_area(), TR(SUP_CURRENT_LIMIT), "A",
                       supply_screen_set_i(), caps.i_min, caps.i_max, 2);
    }
    s.kp_target = target;
    servo_invalidate();
}

/* A set point typed: to the supply, or held for the question SUPPLY asks
 * before a live output changes. */
static void set_point_typed(int target, float typed)
{
    const supply_caps_t caps = supply_screen_caps();
    float v = supply_screen_set_v();
    float i = supply_screen_set_i();
    if (target == KT_SUP_V) {
        v = supply_snap(typed, caps.v_min, caps.v_max, caps.v_step);
    } else {
        i = supply_snap(typed, caps.i_min, caps.i_max, caps.i_step);
    }
    if (v == supply_screen_set_v() && i == supply_screen_set_i()) {
        close_alone();
        return;
    }
    /* Raised past a standard servo's rating, from at or under it: the HV
     * warning, which stands in for the live output's question too. */
    const bool hv = v > STD_SERVO_V_MAX + 0.001f
                    && supply_screen_set_v() <= STD_SERVO_V_MAX + 0.001f;
    if (hv || supply_screen_typed_asks()) {
        s.ask.open      = true;
        s.ask.hv        = hv;
        s.ask.purpose   = ASK_SET;
        s.ask.target    = target;
        s.ask.off_count = supply_screen_off_count();
        s.ask.stops     = s.stops;
        s.ask.v      = v;
        s.ask.i      = i;
        s.ask.down = false;
        ui_hold_reset(&s.ask.hold);
        servo_invalidate();
        return;
    }
    supply_screen_put(v, i);
    close_alone();
}

/* --------------------------------------------------------- the automatic test */

/* The steps the TEST page names: 7.4 and 8.4 V only while HV SERVO is on. */
static uint8_t test_steps(float *v)
{
    static const struct { setting_id_t id; float v; bool hv; } k[] = {
        { SET_SERVO_STEP_48, 4.8f, false },
        { SET_SERVO_STEP_60, 6.0f, false },
        { SET_SERVO_STEP_74, 7.4f, true  },
        { SET_SERVO_STEP_84, 8.4f, true  },
    };
    uint8_t n = 0u;
    for (size_t j = 0; j < sizeof(k) / sizeof(k[0]); ++j) {
        if (settings_get_bool(k[j].id) && (!k[j].hv || s.test_hv)) {
            if (v != NULL) {
                v[n] = k[j].v;
            }
            ++n;
        }
    }
    return n;
}

/* The highest voltage a run would ask for: its top step, or the brown-out's
 * start. */
static float test_top_v(void)
{
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t n = test_steps(v);
    float top = settings_get_bool(SET_SERVO_BROWNOUT)
                    ? SERVO_TEST_BROWNOUT_START_V : 0.0f;
    for (uint8_t k = 0; k < n; ++k) {
        if (v[k] > top) {
            top = v[k];
        }
    }
    return top;
}

/* A run with a step past a standard servo's rating starts only through the
 * HV warning. */
static bool test_needs_hv(void)
{
    return test_top_v() > STD_SERVO_V_MAX + 0.001f;
}

/* What the run is told: the TEST and LIMITS pages, the servo's profile, and
 * its ends -- the sweep's, RANGE of the travel either side of PULSE CENTRE. */
static void test_ends(uint16_t *end_lo, uint16_t *end_hi)
{
    const sweep_cfg_t sw = sweep_cfg_now();
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float span = (float)(hi - lo);
    const float k = 2.0f * (float)SWEEP_CENTRE;
    *end_lo = (uint16_t)lroundf(
        (float)lo + span * (float)(SWEEP_CENTRE - sw.amplitude) / k);
    *end_hi = (uint16_t)lroundf(
        (float)lo + span * (float)(SWEEP_CENTRE + sw.amplitude) / k);
}

static void test_cfg(servo_test_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->step_count = test_steps(c->steps_v);
    c->brownout   = settings_get_bool(SET_SERVO_BROWNOUT);
    c->i_limit    = supply_screen_set_i();
    c->centre_us = s.centre_us;
    test_ends(&c->end_lo_us, &c->end_hi_us);
    c->settle_ms     = (uint16_t)settings_get_int(SET_SERVO_SETTLE_MS);
    c->dwell_ms      = (uint16_t)settings_get_int(SET_SERVO_DWELL_MS);
    c->by_moves      = settings_get_int(SET_SERVO_LEN_BY) != 0;
    c->moves         = (uint16_t)settings_get_int(SET_SERVO_LEN_MOVES);
    c->time_s        = (uint16_t)settings_get_int(SET_SERVO_LEN_S);
    c->idle_max_a    = settings_get(SET_SERVO_IDLE_MAX);
    c->hold_max_a    = settings_get(SET_SERVO_HOLD_MAX);
    c->travel_max_ms = (uint16_t)settings_get_int(SET_SERVO_TRAVEL_MAX_MS);
    c->stall_a       = settings_get(SET_SERVO_STALL_A);
    c->report        = settings_get_bool(SET_SERVO_REPORT);
    snprintf(c->dut, sizeof(c->dut), "%s", settings_text(SET_TEXT_DUT_NAME));
    snprintf(c->type, sizeof(c->type), "%s", type()->name);
    if (in_force_dangerous()) {
        snprintf(c->danger, sizeof(c->danger), "%s %u Hz", type()->name,
                 (unsigned)s.frame_hz);
    }
    c->hv         = s.test_hv;
    c->min_us     = s.min_us;
    c->max_us     = s.max_us;
    c->frame_hz   = s.frame_hz;
    c->trim_us    = s.trim_us;
    c->reverse    = s.reverse;
    c->travel_deg = (uint8_t)s.travel_deg;
    c->range_pct  = (uint8_t)settings_get_int(SET_SERVO_TEST_RANGE);
    c->model      = supply_screen_model();
    /* The current is the PD mini's, whose travel times are an upper bound.
     * The model stands in for it: no lag of its own, and its travel times
     * held to what a run on the PD mini can check. */
    servo_test_meter_pdmini(&c->meter);
    if (c->model) {
        c->meter.lag_ms  = 0u;
        c->meter.repeats = false;
    }
    snprintf(c->firmware, sizeof(c->firmware), "%s", RCBENCH_VERSION_STRING);
    /* The report in the language showing at the start; the CSV in English
     * whatever it is. */
    c->text = ui_servo_table();
}

/* What the run asked for, done the way a finger does it here. */
static void test_apply(const servo_test_do_t *d)
{
    if (d->set) {
        supply_screen_put(d->set_v, d->set_i);
    }
    if (d->on) {
        supply_screen_ask_on();
    }
    if (d->command) {
        s.commanded_deg = us_to_deg(d->cmd_us);
        post(SERVO_CMD_POSITION, d->cmd_us);
        /* A step: the servo's own travel is what is timed, not SPEED's. */
        s.pending.slew_per_s = 0u;
        ++s.ctrl_rev;
    }
    if (d->off) {
        supply_screen_ask_off();
    }
    if (d->release) {
        post(SERVO_CMD_RELEASE, 0);
        s.commanded_deg = 0.0f;
        ++s.ctrl_rev;
    }
}

/* A run that was running is over, however it ended: its OFF is posted
 * (test_apply()), and the restore of the set points starts waiting. */
static void test_ended(void)
{
    if (!s.test_seen || servo_test_running(&s.test)) {
        return;
    }
    s.test_seen     = false;
    s.test_restore  = true;
    s.restore_stage = RS_OFF_POSTED;
    s.test_v1       = supply_screen_set_v();
    s.test_i1       = supply_screen_set_i();
    ++s.test_rev;
    ++s.ctrl_rev;
}

/* The set points back to what they were before the run, in stages; see
 * test_restore.  Dropped if they were set since the run ended. */
static void test_restore_service(void)
{
    if (!s.test_restore) {
        return;
    }
    if (supply_screen_set_v() != s.test_v1
        || supply_screen_set_i() != s.test_i1) {
        s.test_restore = false;         /* the operator's, now */
        return;
    }
    switch (s.restore_stage) {
    case RS_OFF_POSTED:
        if (!supply_screen_off_pending()) {
            s.restore_stage = RS_OFF_SENT;
            s.restore_ms    = test_now();
        }
        break;
    case RS_OFF_SENT:
        /* The frame after the one the OFF went in: a sample stamped from
         * here on was taken after it was sent. */
        if (test_now() != s.restore_ms) {
            s.restore_stage = RS_SAMPLE;
            s.restore_ms    = test_now();
        }
        break;
    case RS_OFF_SEEN:
        /* Not under OUTPUT ON's hold, here or on SUPPLY, nor with an ON
         * on its way: the set point an ON lands at is the one the operator
         * held for. */
        if (!s.out_down && !supply_screen_output_held()
            && !supply_screen_output_live()) {
            s.test_restore = false;
            supply_screen_put(s.test_v0, s.test_i0);
        }
        break;
    default:
        break;                          /* RS_SAMPLE: servo_screen_supply() */
    }
}

/* End a run now, and do what that asks -- the output off, the servo let
 * go -- before whatever ended it acts. */
static void test_end_now(servo_test_abort_t why)
{
    if (!servo_test_running(&s.test)) {
        return;
    }
    servo_test_abort(&s.test, why, test_now());
    const servo_test_in_t in = { s.armed, supply_screen_caps().v_max };
    servo_test_do_t d;
    servo_test_step(&s.test, test_now(), &in, &d);
    test_apply(&d);
    test_ended();
}

/* Why a START cannot run now, before any hold: SERVO_STR_*, or 0. */
static int test_blocked(void)
{
    if (!s.armed) {
        return SERVO_STR_START_NOT_ARMED;
    }
    if (!servo_test_drained(&s.test)) {
        return SERVO_STR_START_BUSY;
    }
    /* What the engine would refuse, said live: a refusal stays on the
     * line only while it holds, and goes with the edit that answers it. */
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t n = test_steps(v);
    if (n == 0u && !settings_get_bool(SET_SERVO_BROWNOUT)) {
        return SERVO_STR_START_NO_STEPS;
    }
    if (!s.have_sup || !s.sup.online) {
        return SERVO_STR_START_NO_SUPPLY;
    }
    uint16_t lo, hi;
    test_ends(&lo, &hi);
    if (!(lo < s.centre_us && s.centre_us < hi)) {
        return SERVO_STR_START_BAD_ENDS;
    }
    /* A step the supply's caps do not reach: refused here, before a
     * warning names a voltage the run would not use. */
    const supply_caps_t caps = supply_screen_caps();
    for (uint8_t k = 0; k < n; ++k) {
        if (v[k] > caps.v_max + 0.001f || v[k] < caps.v_min - 0.001f) {
            return SERVO_STR_START_ABOVE_CAP;
        }
    }
    return 0;
}

/* START TEST's hold, or the HV warning's, completed: the run starts, and
 * the settings close so the left card shows it. */
static void test_begin(void)
{
    /* The hold that started it is spent, whatever comes of it: a press
     * the overlay let go of never reaches ui_hold_end(). */
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    ++s.test_rev;
    s.test_note = 0;
    if (test_blocked() != 0) {
        ++s.ctrl_rev;                   /* the line under it says why */
        return;
    }
    servo_test_cfg_t c;
    test_cfg(&c);
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    if (s.have_sup) {
        last = test_reading_of(&s.sup);
    }
    const supply_caps_t caps = supply_screen_caps();
    const float v0 = supply_screen_set_v();
    const float i0 = supply_screen_set_i();
    const servo_test_start_t why = servo_test_start(
        &s.test, &c, test_now(), &last, caps.v_min, caps.v_max);
    if (why != SERVO_TEST_START_OK) {
        s.test_note = (int)SERVO_STR_START_OK + (int)why;
        ++s.ctrl_rev;
        return;
    }
    stop_sweep();
    s.test_v0      = v0;
    s.test_i0      = i0;
    s.test_restore = false;
    s.test_seen    = true;
    s.test_file    = 0;
    s.test_report  = false;
    s.test_box     = true;
    s.ov_open      = false;
    s.kp_alone     = false;
    close_panels();
    servo_invalidate();
    ++s.ctrl_rev;
}

/* The HV warning, for a run with a step past a standard servo's rating. */
static void ask_hv_test(void)
{
    close_panels();
    s.ask.open      = true;
    s.ask.hv        = true;
    s.ask.purpose   = ASK_TEST;
    s.ask.target    = KT_NONE;
    s.ask.off_count = supply_screen_off_count();
    s.ask.stops     = s.stops;
    s.ask.v         = test_top_v();
    s.ask.i         = supply_screen_set_i();
    s.ask.down      = false;
    ui_hold_reset(&s.ask.hold);
    servo_invalidate();
}

bool servo_screen_testing(void) { return servo_test_running(&s.test); }

servo_test_out_t servo_screen_test_peek(const char **text)
{
    return servo_test_peek(&s.test, text);
}

void servo_screen_test_pop(void) { servo_test_pop(&s.test); }

void servo_screen_test_files(int number, bool report)
{
    if (number != s.test_file || report != s.test_report) {
        s.test_file   = number;
        s.test_report = report;
        ++s.ctrl_rev;
    }
}

void servo_screen_service(void)
{
    test_ended();
    test_restore_service();
}

void servo_screen_clock(uint32_t now_ms)
{
    s.now_ms   = now_ms;
    s.have_now = true;
}

void servo_screen_set_link(bool up)
{
    if (s.link_up && !up) {
        test_end_now(SERVO_TEST_AB_LINK);
        /*
         * A sweep or a pause: the far end stops a sweep and lets a hold go
         * 500 ms after the last write it heard, and the surfaces rest.  The
         * screen ends them as well and draws the output at rest, so nothing
         * stays PAUSED over an output that has gone, and the next tap starts
         * a sweep rather than resuming one.
         */
        /* And one not yet taken is not sent once the link is back. */
        if (!servo_cmd_survives_link_loss(&s.pending)) {
            s.pending.kind = SERVO_CMD_NONE;
            s.toggle_live  = false;
        }
        if (s.sweeping || s.paused || s.awaiting || s.dr.on) {
            stop_sweep();
            s.paused        = false;
            s.driving       = false;
            s.toggle_live   = false;
            s.sw.running    = false;
            s.sw.paused     = false;
            if (!s.have_feedback) {
                s.shown_cmd = (float)OUT_SPAN / 2.0f;
                s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
            }
            s.commanded_deg = clamp_travel(s.shown_deg);
            ++s.ctrl_rev;
        }
    }
    s.link_up = up;
}

void servo_screen_set_surfaces(bool any)
{
    if (any == s.surfaces) {
        return;
    }
    s.surfaces = any;
    ++s.ctrl_rev;                       /* SWEEP greys or comes back */
}

void servo_screen_sweep_refused(void)
{
    s.toggle_live = false;      /* no undo across a notification */
    /* Nothing bound to sweep: no start comes, nothing moves, and nothing
     * is driven for a change of profile to say again. */
    if (s.sweeping || s.paused) {
        stop_sweep();
        s.paused  = false;
        s.driving = false;
        s.commanded_deg = clamp_travel(s.shown_deg);
        ++s.ctrl_rev;
    }
}

uint16_t servo_cmd_pause_root(const servo_cmd_t *c)
{
    if (c == NULL) {
        return 0u;
    }
    return (c->from_pause != 0u) ? c->from_pause : c->pause_seq;
}

bool servo_cmd_stale(servo_pause_end_t *e, const servo_cmd_t *c)
{
    if (e == NULL || c == NULL || !e->on) {
        return false;
    }
    const bool drive = c->kind == SERVO_CMD_POSITION
                       || c->kind == SERVO_CMD_CENTRE
                       || c->kind == SERVO_CMD_SWEEP
                       || c->kind == SERVO_CMD_HOLD;
    if (!drive) {
        return false;
    }
    if (c->from_pause != 0u && c->from_pause == e->pause_seq) {
        return true;
    }
    e->on = false;              /* from after it: nothing of it is queued */
    return false;
}

bool servo_cmd_survives_link_loss(const servo_cmd_t *c)
{
    return c != NULL && c->kind != SERVO_CMD_SWEEP
           && c->kind != SERVO_CMD_HOLD;
}

/* Whether the voltage set point in force is past a standard servo's
 * rating, which OUTPUT ON on this screen applies only through the HV
 * warning. */
static bool hv_set_point(void)
{
    return supply_screen_set_v() > STD_SERVO_V_MAX + 0.001f;
}

/*
 * OUTPUT ON with a set point past a standard servo's rating: the HV warning
 * and its two-second hold, in place of the ordinary hold.  A set point made
 * on SUPPLY, or one already in force before this screen was opened, reaches
 * the servo only through it.
 */
static void ask_hv_on(void)
{
    if (!s.ov_open) {
        s.ov_open  = true;
        s.kp_alone = true;
    }
    close_panels();
    s.ask.open      = true;
    s.ask.hv        = true;
    s.ask.purpose   = ASK_ON;
    s.ask.target    = KT_NONE;
    s.ask.off_count = supply_screen_off_count();
    s.ask.stops     = s.stops;
    s.ask.v         = supply_screen_set_v();
    s.ask.i         = supply_screen_set_i();
    s.ask.down      = false;
    ui_hold_reset(&s.ask.hold);
    servo_invalidate();
}

/* Whether the question about a live output still stands: the output on
 * or coming, not gone off since it was asked -- an OFF and a new ON
 * between two frames start a run the question was not about -- and no
 * stop since.  A stop switches the output off, but the supply reports
 * that a sample later; an APPLY drained in the frame of the stop finds
 * the output still live and is refused by the count. */
static bool ask_stands(void)
{
    return supply_screen_output_live()
           && supply_screen_off_count() == s.ask.off_count
           && s.stops == s.ask.stops;
}

/* The question answered with APPLY, or the HV warning's hold completed. */
static void ask_apply(void)
{
    s.ask.open = false;
    s.ask.down = false;
    /* Only the set point that was typed: the other may have moved while
     * the question stood -- a cap that follows the PD mini's input -- and
     * the value it had then is not one anybody asked for now. */
    if (s.ask.purpose == ASK_TEST) {
        test_begin();
    } else if (s.ask.purpose == ASK_ON) {
        supply_screen_ask_on();
    } else if (s.ask.target == KT_SUP_V) {
        supply_screen_put(s.ask.v, supply_screen_set_i());
    } else {
        supply_screen_put(supply_screen_set_v(), s.ask.i);
    }
    close_alone();
    servo_invalidate();
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
        open_choice(CH_TYPE, SETTING_COUNT, TR(SV_SERVO_TYPE));
        for (int t = 0; t < TYPE_COUNT; ++t) {
            choice_add(k_types[t].name, t);
        }
        break;
    case R_RATE: {
        open_choice(CH_RATE, SETTING_COUNT, TR(SV_ROW_RATE));
        const uint16_t top = max_rate_for(s.type, cmd_top());
        for (int k = 0; k < type()->rate_count; ++k) {
            if (type()->rates[k] <= top) {
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "%u Hz", (unsigned)type()->rates[k]);
                choice_add(lbl, type()->rates[k]);
            }
        }
        choice_add(TR(SV_CUSTOM), -1);
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
        open_keypad(KT_MIN, TR(SV_ROW_MIN), "us", (float)s.min_us, (float)lo,
                    (float)(c - 50u), 0);
        break;
    }
    case R_CENTRE: {
        unsigned lo = (s.max_us + OUT_FLOOR_US + 1u) / 2u;
        unsigned hi = (max_pulse_for_rate() + s.min_us) / 2u;
        if (lo < s.min_us + 50u) { lo = s.min_us + 50u; }
        if (hi > s.max_us - 50u) { hi = s.max_us - 50u; }
        open_keypad(KT_CENTRE, TR(SV_ROW_CENTRE), "us", (float)s.centre_us,
                    (float)lo, (float)hi, 0);
        break;
    }
    case R_MAX: {
        const unsigned c = s.centre_us;
        unsigned top = max_pulse_for_rate();
        if (top > 2u * c - OUT_FLOOR_US) { top = 2u * c - OUT_FLOOR_US; }
        open_keypad(KT_MAX, TR(SV_ROW_MAX), "us", (float)s.max_us,
                    (float)(c + 50u),
                    (float)((top > c + 50u) ? top : c + 50u), 0);
        break;
    }
    case R_TRAVEL:
        open_keypad(KT_TRAVEL, TR(SV_ROW_TRAVEL), "deg", s.travel_deg, 10.0f,
                    90.0f, 0);
        break;
    case R_REVERSE:
        s.reverse = !s.reverse;
        reissue();
        break;
    case R_HV:
        /* For a run started after it; one under way keeps its steps. */
        s.test_hv = !s.test_hv;
        break;
    case R_TEXT:
        ui_textkey_open(&s.tk, overlay_area(), TR(SV_DUT_TITLE),
                        settings_text(SET_TEXT_DUT_NAME), UI_TEXTKEY_MAX);
        break;
    case R_SETTING: {
        const setting_def_t *d = settings_def(r->id);
        if (d->type == SET_TYPE_BOOL) {
            settings_set(r->id, settings_get_bool(r->id) ? 0.0f : 1.0f);
            settings_request_save();
        } else if (d->type == SET_TYPE_ENUM) {
            open_choice(CH_ENUM, r->id, ui_tr(r->label));
            for (int k = 0; k < d->option_count; ++k) {
                choice_add(ui_setting_option(r->id, k), k);
            }
        } else {
            open_keypad(KT_SETTING, ui_tr(r->label), d->unit,
                        settings_get(r->id),
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
            open_keypad(KT_RATE, TR(SV_ROW_RATE), "Hz", (float)s.frame_hz,
                        50.0f,
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
    if (target == KT_SUP_V || target == KT_SUP_I) {
        if (r == UI_KEYPAD_OK) {
            set_point_typed(target, v);
        } else {
            close_alone();
        }
        ++s.ctrl_rev;
        return;
    }
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
    if (s.ask.open) {
        if (gfx_rect_contains(warn_apply_rect(), x, y)) {
            ov_take(evt, OP_ASK_APPLY, -1);
            if (s.ask.hv) {
                s.ask.down = true;
                ui_hold_begin(&s.ask.hold);
                ++s.ask.rev;
            }
        } else if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            ov_take(evt, OP_ASK_CANCEL, -1);
        }
        return;
    }
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
    if (s.tabs.selected == PG_TEST
        && gfx_rect_contains(test_start_rect(), x, y)) {
        ov_take(evt, OP_TEST_START, -1);
        /* A hold, unless a run is under way (a tap stops it), a step is
         * past 6.0 V (a tap opens the HV warning, whose hold starts it), or
         * it cannot run now (a tap says why). */
        s.test_note = 0;
        if (!servo_test_running(&s.test) && test_blocked() == 0
            && !test_needs_hv()) {
            s.test_down = true;
            ui_hold_begin(&s.test_hold);
        }
        ++s.test_rev;
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
    case OP_ASK_APPLY:
        if (!s.ask.hv) {
            break;                      /* a tap: the release applies it */
        }
        if (!up) {
            /* A finger that leaves APPLY abandons the hold, as on ARM. */
            if (!gfx_rect_contains(warn_apply_rect(), x, y)
                && ui_hold_leave(&s.ask.hold)) {
                s.ask.down = false;
                ++s.ask.rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.ask.hold);
        s.ask.down = false;
        ++s.ask.rev;
        ov_let_go();
        return;
    case OP_TEST_START:
        if (!s.test_down) {
            break;                      /* a tap: the release acts */
        }
        if (!up) {
            /* A finger that leaves START TEST abandons the hold. */
            if (!gfx_rect_contains(test_start_rect(), x, y)
                && ui_hold_leave(&s.test_hold)) {
                s.test_down = false;
                ++s.test_rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.test_hold);
        s.test_down = false;
        ++s.test_rev;
        ov_let_go();
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
            s.kp_alone = false;
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
    case OP_SUP_V:
    case OP_SUP_I: {
        const int k = (was == OP_SUP_V) ? 0 : 1;
        if (gfx_rect_contains(sup_val_rect(k), x, y)) {
            open_set_point((k == 0) ? KT_SUP_V : KT_SUP_I);
        }
        break;
    }
    case OP_ASK_APPLY:
        /* Only while the question still stands: a release drained in the
         * frame that took the live output away must not apply it. */
        if (gfx_rect_contains(warn_apply_rect(), x, y) && s.ask.open
            && (s.ask.hv || ask_stands())) {
            ask_apply();
        }
        break;
    case OP_ASK_CANCEL:
        if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            s.ask.open = false;
            close_alone();
            servo_invalidate();
        }
        break;
    case OP_TEST_START:
        if (!gfx_rect_contains(test_start_rect(), x, y)) {
            break;
        }
        if (servo_test_running(&s.test)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
        } else if (test_blocked() == 0 && test_needs_hv()) {
            ask_hv_test();
        }
        ++s.test_rev;
        break;
    case OP_SETTINGS:
        if (gfx_rect_contains(s.set_btn, x, y)) {
            /* SETTINGS opens the overlay and closes it, taking whatever is
             * open in it; a warning not held is a profile not applied.  The
             * servo's lead runs out past the left card, where the overlay
             * does not reach, so the change is a whole repaint. */
            s.ov_open = !s.ov_open;
            s.kp_alone = false;     /* whatever opened it, it is shut now */
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

void servo_screen_knob_frame(void)
{
    s.knob_finger = false;
}

void servo_screen_knob_cancel(void)
{
    if (!s.knob_live) {
        return;
    }
    s.knob_live = false;
    if (s.post_count != s.knob_post || s.pending.kind != SERVO_CMD_POSITION) {
        return;     /* something else was posted over it since */
    }
    s.commanded_deg = s.knob_prev_deg;
    ++s.ctrl_rev;
    if (s.knob_had_cmd) {
        s.pending.value_us = deg_to_us(s.commanded_deg);
    } else {
        s.pending.kind = SERVO_CMD_NONE;
    }
}

void servo_screen_knob(float span_fraction)
{
    if (span_fraction == 0.0f || s.dragging || s.knob_finger || s.ov_open
        || servo_test_running(&s.test) || s.sweeping || s.paused) {
        return;
    }
    /*
     * Posts only into an empty slot or over a position, which it may replace
     * as a drag does.  A release, an arm, a disarm, a hold or a sweep waiting
     * to be taken is not the knob's to overwrite; its delta is dropped.
     */
    if (s.pending.kind != SERVO_CMD_NONE
        && s.pending.kind != SERVO_CMD_POSITION) {
        return;
    }
    const float before = s.commanded_deg;
    const bool  was_knob = s.knob_live && s.post_count == s.knob_post;
    const bool  had_cmd  = s.pending.kind == SERVO_CMD_POSITION;
    const uint32_t posts = s.post_count;
    command(before + span_fraction * 2.0f * s.travel_deg);
    if (s.post_count != posts) {
        if (!was_knob) {
            s.knob_prev_deg = before;
            s.knob_had_cmd  = had_cmd;
        }
        s.knob_live = true;
        s.knob_post = s.post_count;
    }
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
        if (gfx_rect_contains(s.out_btn, px, py)) {
            /* Off, with a set point past a standard servo's rating: the HV
             * warning, whose hold switches it on.  One press in the
             * overlay at a time. */
            if (!s.out_down && !supply_screen_output_on()
                && hv_set_point()) {
                if (!s.ov_have) {
                    ask_hv_on();
                }
                return;
            }
            /* One contact owns the switch, as on ARM. */
            if (!s.out_down) {
                s.out_down     = true;
                s.out_id       = evt->point.id;
                s.out_press_on = supply_screen_output_on();
                if (!s.out_press_on) {
                    ui_hold_begin(&s.out_hold);
                }
                ++s.sup_rev;
            }
            return;
        }
        for (int k = 0; k < 2; ++k) {
            if (gfx_rect_contains(sup_val_rect(k), px, py)) {
                /* The run owns the set points while it runs. */
                test_end_now(SERVO_TEST_AB_OPERATOR);
                if (!s.ov_have) {
                    ov_take(evt, (k == 0) ? OP_SUP_V : OP_SUP_I, -1);
                }
                return;
            }
        }
        if (s.test_box && !s.ov_open
            && gfx_rect_contains(test_btn_rect(), px, py)) {
            /* STOP TEST ends the run on the press, as a stop does; CLOSE
             * puts the result away. */
            if (servo_test_running(&s.test)) {
                test_end_now(SERVO_TEST_AB_OPERATOR);
            } else {
                s.test_box = false;
            }
            ++s.ctrl_rev;
            return;
        }
        float deg;
        if (!s.ov_open && on_the_dial(px, py, &deg)) {
            /* A finger on the dial takes the horn from a sweep, and from a
             * run. */
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            s.dragging = true;
            s.knob_finger = true;
            s.drag_id  = evt->point.id;
            command(deg);
            return;
        }
        if (gfx_rect_contains(s.centre_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            s.commanded_deg = 0.0f;
            post(SERVO_CMD_CENTRE, deg_to_us(0.0f));
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.sweep_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
            toggle_sweep();
        } else if (gfx_rect_contains(s.release_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
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

    if (s.out_down && evt->point.id == s.out_id) {
        const bool on = supply_screen_output_on();
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* Off the switch abandons the hold; a press that began on a
             * live output is an OFF, whose release is checked against the
             * switch. */
            if (!s.out_press_on && !gfx_rect_contains(s.out_btn, px, py)
                && ui_hold_leave(&s.out_hold)) {
                s.out_down = false;
                ++s.sup_rev;
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            const bool fired = ui_hold_end(&s.out_hold);
            s.out_down = false;
            ++s.sup_rev;
            /* Off is a tap; on is a hold that has already asked by the time
             * the finger lifts.  Which one is the press's: an earlier ON
             * reported during a second hold does not make its release an
             * OFF. */
            if (s.out_press_on && on && !fired
                && gfx_rect_contains(s.out_btn, px, py)) {
                supply_screen_ask_off();
            }
            return;
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
        s.knob_finger = true;
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
         * one rather than waiting for the next drag.  Not a paused sweep:
         * the hold moves nothing, and the resume carries the new rate. */
        if (!s.paused) {
            reissue();
        }
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

/* A file's name for the run's number: BENCHnnn.CSV. */
static void test_file_name(char *b, size_t n)
{
    if (s.test_file > 0) {
        log_run_name(b, n, s.test_file);
    } else {
        snprintf(b, n, "%s", (s.test_file < 0) ? TR(SV_NOT_RECORDED)
                                                : TR(SV_WRITING));
    }
}

/*
 * The run on the left card: the step and phase while it runs, with STOP
 * TEST; PASS, FAIL or ABORTED and why, and the files, after it, with CLOSE.
 */
static void draw_test_box(gfx_canvas_t *c)
{
    const gfx_rect_t b = test_box_rect();
    const servo_test_t *t = &s.test;
    const bool running = servo_test_running(t);
    gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, 6,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, b.x, b.y, b.w, b.h, 6, ui_theme_color(UI_C_EDGE));
    char l1[64], l2[64], l3[64];
    gfx_color_t head = ui_theme_color(UI_C_ACCENT);
    if (running) {
        snprintf(l1, sizeof(l1), "%s", TR(SV_AUTO_TEST));
        const servo_test_step_t *st = &t->steps[t->step];
        snprintf(l2, sizeof(l2), TR(SV_STEP_OF),
                 ui_servo_str(st->brownout ? SERVO_STR_TEST_BROWNOUT
                                           : SERVO_STR_TEST_STEP),
                 servo_test_step_now(t), servo_test_steps_planned(t),
                 (double)st->set_v);
        const bool moving = t->phase == SERVO_TEST_PH_MOVE
                            || t->phase == SERVO_TEST_PH_HOLD;
        if (moving && t->counted && !st->brownout && t->cfg.by_moves) {
            snprintf(l3, sizeof(l3), TR(SV_PHASE_OF),
                     ui_servo_str(servo_test_phase_str(t->phase)),
                     (unsigned)t->moves_done + 1u, (unsigned)t->cfg.moves);
        } else if (moving && t->counted) {
            snprintf(l3, sizeof(l3), "%s %u",
                     ui_servo_str(servo_test_phase_str(t->phase)),
                     (unsigned)t->moves_done + 1u);
        } else {
            snprintf(l3, sizeof(l3), "%s",
                     ui_servo_str(servo_test_phase_str(t->phase)));
        }
    } else {
        const servo_test_verdict_t v = servo_test_verdict(t);
        head = (v == SERVO_TEST_PASS)   ? ui_theme_color(UI_C_OK)
               : (v == SERVO_TEST_FAIL) ? ui_theme_color(UI_C_DANGER)
                                        : ui_theme_color(UI_C_WARN);
        snprintf(l1, sizeof(l1), TR(SV_TEST_VERDICT),
                 ui_servo_str(servo_test_verdict_str(v)));
        uint32_t ms = 0u;
        float hold = 0.0f;
        if (v == SERVO_TEST_ABORTED) {
            snprintf(l2, sizeof(l2), "%s",
                     ui_servo_str(servo_test_abort_str(t->why)));
        } else if (servo_test_max_travel(t, &ms) && servo_test_max_hold(t, &hold)) {
            snprintf(l2, sizeof(l2), TR(SV_TRAVEL_HOLD),
                     (unsigned long)ms, (double)hold);
        } else {
            l2[0] = '\0';
        }
        char f[LOG_RUN_NAME_MAX + 4];
        test_file_name(f, sizeof(f));
        snprintf(l3, sizeof(l3), "%s%s", f,
                 (s.test_file > 0 && s.test_report) ? " + .TXT" : "");
    }
    gfx_text(c, b.x + 10, b.y + 8, l1, &gfx_font_8x16, head, 1);
    gfx_text(c, b.x + 10, b.y + 28, l2, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    gfx_text(c, b.x + 10, b.y + 48, l3, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_button(c, test_btn_rect(), running ? TR(SV_STOP_TEST) : TR(SUP_CLOSE),
              running ? ui_theme_color(UI_C_DANGER)
                      : ui_theme_color(UI_C_PANEL_HI), false, true);
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
    if (s.test_box) {
        draw_test_box(c);
    }
}

static void row_in(gfx_canvas_t *c, int y, const char *label,
                   gfx_color_t label_col, const char *value)
{
    gfx_text(c, RC_X, y + 5, label, UI_FONT_LABEL, label_col, 1);
    if (value != NULL) {
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(RC_X + 80), (int16_t)(y + 5),
                                     (int16_t)(RC_W - 80), 16 },
                    value, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
    }
}

static void row(gfx_canvas_t *c, int y, const char *label, const char *value)
{
    row_in(c, y, label, ui_theme_color(UI_C_TEXT_DIM), value);
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
    gfx_text(c, RC_X, PWR_TXT_Y, TR(SV_SUPPLY), UI_FONT_LABEL,
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

/* OUTPUT ON and OFF in SUPPLY's colours: off is the green the hold fades
 * from, on the danger red it fades to. */
static gfx_color_t out_fill(void)
{
    if (s.out_hold.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER),
                             s.out_hold.flash_left);
    }
    gfx_color_t fill = s.out_on ? ui_theme_color(UI_C_DANGER)
                                : ui_theme_color(UI_C_OK);
    if (!s.out_on && s.out_hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER),
                            s.out_hold.held_s);
    }
    return fill;
}

/* SET, the two set points, and the output switch. */
static void draw_sup_row(gfx_canvas_t *c)
{
    const gfx_rect_t r = sup_row_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    gfx_text(c, RC_X, SUP_Y + 5, TR(SV_SET), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    char v[12], a[12];
    snprintf(v, sizeof(v), "%.2f V", (double)supply_screen_set_v());
    snprintf(a, sizeof(a), "%.2f A", (double)supply_screen_set_i());
    const struct { const char *txt; gfx_color_t col; int op; } k[2] = {
        { v, ui_theme_color(UI_C_VOLT), OP_SUP_V },
        { a, ui_theme_color(UI_C_CURR), OP_SUP_I },
    };
    for (int i = 0; i < 2; ++i) {
        const gfx_rect_t b = sup_val_rect(i);
        const bool down = s.ov_have && s.ov_pressed == k[i].op;
        gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, 4,
                            down ? ui_theme_color(UI_C_PANEL_HI)
                                 : ui_theme_color(UI_C_PANEL_SUNK));
        gfx_text_in(c, b, k[i].txt, UI_FONT_LABEL, k[i].col, 1,
                    GFX_ALIGN_CENTER);
    }
    ui_button(c, s.out_btn, s.out_on ? TR(SUP_OUTPUT_OFF) : TR(SUP_OUTPUT_ON),
              out_fill(),
              s.out_down, true);
    if (s.out_hold.flash_left > 0) {
        ui_hold_flash_step(&s.out_hold);
    }
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
    ui_button(c, s.set_btn, TR(SUP_SETTINGS),
              s.ov_open ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_SETTINGS, true);

    char buf[24];
    snprintf(buf, sizeof(buf), "%u us", (unsigned)deg_to_us(s.commanded_deg));
    row(c, 44, TR(SV_COMMANDED), buf);
    if (s.have_feedback) {
        snprintf(buf, sizeof(buf), "%+.1f deg", (double)s.measured_deg);
        row(c, 68, TR(SV_MEASURED), buf);
        snprintf(buf, sizeof(buf), "%.2f A", (double)s.current_a);
        row(c, 92, TR(SV_CURRENT), buf);
    } else {
        row(c, 68, TR(SV_MEASURED), "---");
        row(c, 92, TR(SV_CURRENT), "---");
    }

    draw_tag(c);
    if (power) {
        draw_power(c);
    }
    draw_sup_row(c);

    /* SPEED's row says when SPEED, not CURVE, decides how a sweep moves:
     * the curves then look alike. */
    snprintf(buf, sizeof(buf), "%d %%", s.speed_pct);
    if (speed_limits_sweep()) {
        row_in(c, 264, TR(SV_SPEED_LIMITS), ui_theme_color(UI_C_WARN), buf);
    } else {
        row(c, 264, TR(SV_SPEED), buf);
    }
    s.speed.color = ui_theme_color(UI_C_ACCENT);
    ui_slider_render(&s.speed, c);

    snprintf(buf, sizeof(buf), "%u - %u us", (unsigned)s.min_us,
             (unsigned)s.max_us);
    row(c, 322, TR(SV_RANGE), buf);

    ui_button(c, s.centre_btn, TR(SV_CENTRE_BTN), ui_theme_color(UI_C_ACCENT),
              false, true);
    /* PAUSE while a sweep runs, in the accent; PAUSED (PAUSIERT) while it
     * is paused, filled in the warning colour, so the two read apart by
     * fill as well as by the word. */
    const gfx_color_t sweep_fill = s.sweeping ? ui_theme_color(UI_C_ACCENT)
                                   : s.paused ? ui_theme_color(UI_C_WARN)
                                              : ui_theme_color(UI_C_PANEL_HI);
    ui_button(c, s.sweep_btn,
              s.paused ? TR(SV_PAUSED) : s.sweeping ? TR(SV_PAUSE) : "SWEEP", sweep_fill,
              false, s.sweeping || s.paused
                     || (s.armed && s.sweep_able && s.surfaces
                         && s.link_up));
    ui_button(c, s.release_btn, TR(SV_RELEASE), ui_theme_color(UI_C_PANEL_HI),
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
    static const ui_text_id_t k_save[] = {
        TX_SUP_SAVED, TX_SUP_SAVE_WAITING, TX_SUP_NOT_SAVED,
        TX_SUP_SETUP_NOT_SAVED,
    };
    const gfx_rect_t r = save_line_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    const uint8_t st = save_state();
    gfx_text(c, r.x, r.y + 1, ui_tr(k_save[st]), &gfx_font_8x16,
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
           || (r->id == SET_SERVO_LEN_MOVES && by == 0)
           || ((r->id == SET_SERVO_STEP_74 || r->id == SET_SERVO_STEP_84)
               && !s.test_hv);
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
    case R_REVERSE: snprintf(buf, n, "%s", ui_on_off(s.reverse)); return;
    case R_TEXT:    snprintf(buf, n, "%s", settings_text(SET_TEXT_DUT_NAME));
                    return;
    case R_HV:      snprintf(buf, n, "%s", ui_on_off(s.test_hv)); return;
    default:
        break;
    }
    const setting_def_t *d = settings_def(r->id);
    const float v = settings_get(r->id);
    const bool limit = (r->id == SET_SERVO_IDLE_MAX
                        || r->id == SET_SERVO_HOLD_MAX
                        || r->id == SET_SERVO_TRAVEL_MAX_MS);
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", ui_on_off(v != 0.0f));
    } else if (d->type == SET_TYPE_ENUM) {
        snprintf(buf, n, "%s",
                 ui_setting_option(r->id, settings_get_int(r->id)));
    } else if (limit && !(v > 0.0f)) {
        snprintf(buf, n, "%s", TR(OFF));
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
    char l1[96], l2[96];
    bool warn = true;
    switch (st) {
    case SERVO_RATE_IN_FORCE:
        snprintf(l1, sizeof(l1), TR(SV_IN_FORCE_1), (unsigned)s.frame_hz);
        snprintf(l2, sizeof(l2), "%s", TR(SV_IN_FORCE_2));
        warn = false;
        break;
    case SERVO_RATE_REFUSED:
        snprintf(l1, sizeof(l1), "%s", TR(SV_REFUSED_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_REFUSED_2));
        break;
    case SERVO_RATE_UNSUPPORTED:
        snprintf(l1, sizeof(l1), "%s", TR(SV_UNSUPPORTED_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_UNSUPPORTED_2));
        break;
    case SERVO_RATE_UNSENT:
    default:
        snprintf(l1, sizeof(l1), "%s", TR(SV_UNSENT_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_UNSENT_2));
        warn = false;
        break;
    }
    const gfx_color_t col = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    gfx_text(c, x, y, l1, &gfx_font_8x16, col, 1);
    gfx_text(c, x, y + 18, l2, &gfx_font_8x16, col, 1);
}

/* START TEST: green, fading to the danger red across its hold, as ARM;
 * STOP TEST in red while a run is under way. */
static void draw_test_start(gfx_canvas_t *c)
{
    const bool running = servo_test_running(&s.test);
    gfx_color_t fill = ui_theme_color(UI_C_OK);
    if (running) {
        fill = ui_theme_color(UI_C_DANGER);
    } else if (s.test_hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER),
                            s.test_hold.held_s);
    }
    ui_button(c, test_start_rect(),
              running ? TR(SV_STOP_TEST) : TR(SV_START_TEST), fill,
              s.ov_have && s.ov_pressed == OP_TEST_START,
              running || test_blocked() == 0);
}

/* Under START TEST: what a run would do, and what the last one did or why
 * one did not start. */
static void draw_test_lines(gfx_canvas_t *c)
{
    const int x = OV_X + 20 + OV_COL_W;
    const int y = OV_ROW0 + 7 * OV_PITCH + 2;
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t n = test_steps(v);
    char plan[40] = "";
    int k = 0;
    for (uint8_t j = 0; j < n && k >= 0 && (size_t)k < sizeof(plan); ++j) {
        k += snprintf(plan + k, sizeof(plan) - (size_t)k, "%.1f ", (double)v[j]);
    }
    if (k >= 0 && (size_t)k < sizeof(plan)) {
        snprintf(plan + k, sizeof(plan) - (size_t)k, "%s%s",
                 (n > 0u) ? "V" : "",
                 settings_get_bool(SET_SERVO_BROWNOUT)
                     ? ((n > 0u) ? " BROWN-OUT" : "BROWN-OUT") : "");
    }
    if (n == 0u && !settings_get_bool(SET_SERVO_BROWNOUT)) {
        snprintf(plan, sizeof(plan), "%s",
                 ui_servo_str(SERVO_STR_START_NO_STEPS));
    }
    gfx_text(c, x, y, plan, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);

    char line[64];
    gfx_color_t col = ui_theme_color(UI_C_TEXT_FAINT);
    const int blocked = servo_test_running(&s.test) ? 0 : test_blocked();
    if (blocked != 0 || s.test_note != 0) {
        snprintf(line, sizeof(line), "%s",
                 ui_servo_str((servo_str_t)((blocked != 0) ? blocked
                                                           : s.test_note)));
        col = ui_theme_color(UI_C_WARN);
    } else if (servo_test_running(&s.test)) {
        snprintf(line, sizeof(line), TR(SV_RUNNING),
                 servo_test_step_now(&s.test),
                 servo_test_steps_planned(&s.test));
    } else if (s.test.state == SERVO_TEST_DONE) {
        char f[LOG_RUN_NAME_MAX + 4];
        test_file_name(f, sizeof(f));
        snprintf(line, sizeof(line), TR(SV_LAST),
                 ui_servo_str(servo_test_verdict_str(servo_test_verdict(
                     &s.test))), f);
    } else if (test_blocked() != 0) {
        snprintf(line, sizeof(line), "%s",
                 ui_servo_str((servo_str_t)test_blocked()));
    } else {
        snprintf(line, sizeof(line), "%s",
                 test_needs_hv() ? TR(SV_TAP_HV) : TR(SV_HOLD_START));
    }
    gfx_text(c, x, y + 18, line, &gfx_font_8x16, col, 1);
}

static void draw_page(gfx_canvas_t *c)
{
    ui_tabs_render(&s.tabs, c);
    ui_button(c, close_rect(), TR(SUP_CLOSE), ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_CLOSE, true);
    for (int i = 0; i < ROW_COUNT; ++i) {
        const ov_row_t *r = &k_rows[i];
        if (r->page != s.tabs.selected) {
            continue;
        }
        const gfx_rect_t rr = row_rect(i);
        const gfx_rect_t vr = value_rect(i);
        const bool faint = row_unused(r);
        gfx_text(c, rr.x, rr.y + 9, ui_tr(r->label), &gfx_font_8x16,
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
        const bool danger = ((r->kind == R_TYPE || r->kind == R_RATE)
                             && in_force_dangerous())
                            || (r->kind == R_HV && s.test_hv);
        ui_button(c, vr, v,
                  danger ? ui_theme_color(UI_C_DANGER)
                         : ui_theme_color(UI_C_PANEL_SUNK),
                  pressed, !faint);
    }

    const int nx = OV_X + 10;
    if (s.tabs.selected == PG_OUTPUT) {
        char l1[96], l2[96];
        snprintf(l1, sizeof(l1), TR(SV_FASTEST),
                 (unsigned)max_rate_for(s.type, cmd_top()),
                 type()->heli ? "0.5 ms" : "1 ms");
        snprintf(l2, sizeof(l2), "%s", TR(SV_AT_START));
        const char *const lines[] = { l1, l2 };
        draw_note(c, nx, OV_NOTE_Y, lines, 2);
        draw_rate_note(c, nx, OV_NOTE_Y + 2 * 18);
    } else if (s.tabs.selected == PG_TEST) {
        draw_test_start(c);
        draw_test_lines(c);
    } else if (s.tabs.selected == PG_LIMITS) {
        const char *const lines[] = {
            TR(SV_LIMITS_1), TR(SV_LIMITS_2), TR(SV_LIMITS_3),
            TR(SV_LIMITS_4),
        };
        draw_note(c, nx, OV_ROW0 + 3 * OV_PITCH + 6, lines, 4);
    } else {
        const char *const lines[] = {
            TR(SV_DUT_NOTE),
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
    ui_button(c, choice_cancel_rect(), TR(CANCEL),
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
    ui_button(c, warn_apply_rect(), TR(SV_HOLD_TO_APPLY), warn_fill(),
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
    gfx_text(c, a.x + 20, a.y + 20, TR(SV_DESTROY), &gfx_font_8x16,
             red, 2);
    const servo_type_t *t = &k_types[s.warn.type];
    char what[80];
    snprintf(what, sizeof(what), TR(SV_DESTROY_WHAT),
             t->name, (unsigned)s.warn.hz,
             (unsigned)((s.warn.type == s.type) ? s.min_us : t->min_us),
             (unsigned)((s.warn.type == s.type) ? s.max_us : t->max_us));
    gfx_text(c, a.x + 20, a.y + 66, what, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    const char *const lines[] = {
        TR(SV_DESTROY_1), TR(SV_DESTROY_2), TR(SV_DESTROY_3),
        TR(SV_DESTROY_4), TR(SV_DESTROY_5),
    };
    for (int i = 0; i < 5; ++i) {
        gfx_text(c, a.x + 20, a.y + 104 + i * 22, lines[i], &gfx_font_8x16,
                 (i < 3) ? ui_theme_color(UI_C_TEXT)
                         : ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    draw_warn_apply(c);
    ui_button(c, warn_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_WARN_CANCEL, true);
}

static void draw_ask_apply(gfx_canvas_t *c)
{
    ui_button(c, warn_apply_rect(), TR(SV_HOLD_TO_APPLY),
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK),
                           ui_theme_color(UI_C_DANGER), s.ask.hold.held_s),
              s.ask.down, true);
}

/*
 * A voltage raised past a standard servo's rating: in the danger colour of
 * the profile warning, saying what it does to a servo not rated for it.
 */
static void draw_hv(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text(c, a.x + 20, a.y + 20, TR(SV_HV_ONLY), &gfx_font_8x16, red, 2);
    char what[64];
    if (s.ask.purpose == ASK_ON) {
        snprintf(what, sizeof(what), TR(SV_HV_ON_AT),
                 (double)supply_screen_set_v());
    } else if (s.ask.purpose == ASK_TEST) {
        snprintf(what, sizeof(what), TR(SV_HV_STEPS),
                 (double)s.ask.v);
    } else {
        snprintf(what, sizeof(what), TR(SV_HV_VOLTAGE),
                 (double)supply_screen_set_v(), (double)s.ask.v);
    }
    gfx_text(c, a.x + 20, a.y + 66, what, &gfx_font_8x16,
             ui_theme_color(UI_C_VOLT), 1);
    const char *const lines[] = {
        TR(SV_HV_1), TR(SV_HV_2), TR(SV_HV_3), TR(SV_HV_4), TR(SV_HV_5),
    };
    for (int i = 0; i < 5; ++i) {
        gfx_text(c, a.x + 20, a.y + 104 + i * 22, lines[i], &gfx_font_8x16,
                 (i < 4) ? ui_theme_color(UI_C_TEXT)
                         : ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    if (s.ask.purpose == ASK_SET && supply_screen_output_on()) {
        gfx_text(c, a.x + 20, a.y + 104 + 5 * 22 + 8,
                 TR(SV_HV_LIVE),
                 &gfx_font_8x16, ui_theme_color(UI_C_WARN), 1);
    }
    draw_ask_apply(c);
    ui_button(c, warn_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true);
}

/*
 * SUPPLY's question before a set point changes a live output, with its
 * words: the change reaches the load at once.
 */
static void draw_ask(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    if (s.ask.hv) {
        draw_hv(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_WARN));
    gfx_text(c, a.x + 20, a.y + 20, TR(SUP_OUTPUT_IS_ON), &gfx_font_8x16,
             ui_theme_color(UI_C_WARN), 2);
    gfx_text(c, a.x + 20, a.y + 70, TR(SUP_ASK_WHY),
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    int y = a.y + 120;
    const struct { const char *label; float was, now; const char *unit;
                   gfx_color_t col; } k[] = {
        { TR(SUP_VOLTAGE), supply_screen_set_v(), s.ask.v, "V",
          ui_theme_color(UI_C_VOLT) },
        { TR(SUP_CURRENT_LIMIT), supply_screen_set_i(), s.ask.i, "A",
          ui_theme_color(UI_C_CURR) },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (k[i].was == k[i].now) {
            continue;
        }
        gfx_text(c, a.x + 20, y + 8, k[i].label, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[48];
        snprintf(line, sizeof(line), "%.2f -> %.2f %s", (double)k[i].was,
                 (double)k[i].now, k[i].unit);
        gfx_text(c, a.x + 150, y, line, &gfx_font_8x16, k[i].col, 2);
        y += 50;
    }
    ui_button(c, warn_apply_rect(), TR(SUP_APPLY), ui_theme_color(UI_C_WARN),
              s.ov_have && s.ov_pressed == OP_ASK_APPLY, true);
    ui_button(c, warn_cancel_rect(), TR(CANCEL), ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true);
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
    if (s.ask.open) {
        draw_ask(c);
        return;
    }
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

/* What the box and the TEST page show of the run, as one number. */
static uint32_t test_signature(void)
{
    const servo_test_t *t = &s.test;
    const uint32_t f[] = {
        (uint32_t)t->state, (uint32_t)t->phase, (uint32_t)t->step,
        (uint32_t)t->moves_done, (uint32_t)(s.test_file + 2),
        s.test_report ? 1u : 0u, (uint32_t)test_blocked(),
        (uint32_t)s.test_note,
    };
    uint32_t h = 2166136261u;           /* FNV-1a over the fields */
    for (size_t k = 0; k < sizeof(f) / sizeof(f[0]); ++k) {
        h = (h ^ f[k]) * 16777619u;
    }
    return h;
}

/*
 * The run, every frame: its timers against the panel's clock and the cap in
 * force, and what it asks done.  Once it is over and the output reads off,
 * SUPPLY's set points go back to what they were before it.
 */
static void test_tick(void)
{
    const servo_test_in_t in = { s.armed, supply_screen_caps().v_max };
    servo_test_do_t d;
    servo_test_step(&s.test, test_now(), &in, &d);
    test_apply(&d);
    test_ended();
    test_restore_service();
    /* A refusal the engine gave goes once what it was about changes: the
     * bench armed or disarmed, a report taken, a cap moved, the supply
     * answering or not. */
    const int blocked = test_blocked();
    const bool online = s.have_sup && s.sup.online;
    if (blocked != s.test_blocked_seen || online != s.test_online_seen) {
        s.test_blocked_seen = blocked;
        s.test_online_seen  = online;
        s.test_note = 0;
        ++s.test_rev;
    }
    /* The box and the TEST page say the step, the phase and the move. */
    const uint32_t sig = test_signature();
    if (sig != s.test_sig) {
        s.test_sig = sig;
        ++s.ctrl_rev;
    }
}

/* The trail servo_screen_sweep_started() reads where a frozen output was. */
static void remember_shown(void)
{
    s.trail[s.trail_head].t   = s.clock_ms;
    s.trail[s.trail_head].cmd = s.shown_cmd;
    s.trail_head = (uint8_t)((s.trail_head + 1u) % 64u);
    if (s.trail_n < 64u) {
        ++s.trail_n;
    }
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
            restart_sweep();
        }
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        if (s.sweeping && !s.awaiting
            && sweep_step(&s.sw, s.clock_ms, &cmd)) {
            s.commanded_deg = clamp_travel(sweep_deg(cmd));
            ++s.ctrl_rev;
        }
    }
    if (s.dr.on) {
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        if ((uint32_t)(s.clock_ms - s.dr.from_ms) > OUT_DEFAULT_TIMEOUT_MS
            || !sweep_step(&s.dr.sw, s.clock_ms, &cmd)) {
            /* Unheard for that long the far end has stopped too. */
            s.dr.on = false;
            s.commanded_deg = clamp_travel(s.shown_deg);
        } else {
            /*
             * In the mapping it was sent under, and slewed in its units at
             * its SPEED, as the far end renders it.  The curve's target
             * stays here: the held angle (commanded_deg) is what is drawn,
             * the reading or the estimate, which a change of profile while
             * paused says again (reissue()) -- never a point the curve was
             * still heading for.
             */
            const servo_map_t *m = &s.dr.map;
            if (!s.have_feedback) {
                const float per_ms = slew_per_ms(s.dr.speed_pct);
                s.dr.out = chase_cmd(s.dr.out, (float)cmd,
                                     (per_ms < 0.0f) ? -1.0f
                                                     : per_ms * dt_s * 1000.0f);
                const float us = map_cmd_to_us(m, s.dr.out);
                s.shown_deg = map_us_to_deg(m, us);
                s.shown_cmd = us_to_cmd(us);
            }
            s.commanded_deg = clamp_travel(s.shown_deg);
        }
        ++s.ctrl_rev;
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

    /* The supply as SUPPLY holds it: its output and its set points. */
    const bool on = supply_screen_output_on();
    if (on != s.out_on) {
        s.out_on = on;
        if (on) {
            ui_hold_reached(&s.out_hold);
        } else {
            /* The flash says the output came on; off, it is over, so both
             * buffers end on OUTPUT ON's own colour. */
            s.out_hold.flash_left = 0;
            if (ui_hold_left(&s.out_hold) && s.out_down) {
                s.out_down = false;
            }
        }
        if (s.ask.open && s.ask.hv) {
            ++s.ctrl_rev;   /* the warning says whether the output is on */
        }
        ++s.sup_rev;
    }
    /* The question is about a live output: once there is none -- off, and
     * no ON on its way, which a STOP or a lost touch can drop -- it goes,
     * unanswered, with the change it held, as on SUPPLY.  The HV warning
     * is about the servo, and stays. */
    /* A cap that came down while the question stands takes the waiting
     * value down with it, for good, as SUPPLY does with its own: one that
     * recovers before APPLY does not bring the old value back. */
    /* Not the run's warning: it names the run's top step, which the run
     * either reaches or refuses, never a value snapped under it. */
    if (s.ask.open && s.ask.purpose != ASK_TEST) {
        const supply_caps_t caps = supply_screen_caps();
        const float v = supply_snap(s.ask.v, caps.v_min, caps.v_max,
                                    caps.v_step);
        const float i = supply_snap(s.ask.i, caps.i_min, caps.i_max,
                                    caps.i_step);
        if (v != s.ask.v || i != s.ask.i) {
            s.ask.v = v;
            s.ask.i = i;
            ++s.ctrl_rev;               /* the question shows the value */
        }
    }
    if (s.ask.open && !s.ask.hv && !ask_stands()) {
        s.ask.open = false;
        if (s.ov_pressed == OP_ASK_APPLY || s.ov_pressed == OP_ASK_CANCEL) {
            ov_let_go();                /* its buttons have gone with it */
        }
        close_alone();
        servo_invalidate();
    }
    if (s.out_down && !s.out_press_on && !on) {
        ++s.sup_rev;
        if (ui_hold_tick(&s.out_hold, dt_s)) {
            /* The set point is looked at again as the hold completes: it
             * can have risen past a standard servo's rating since the press
             * -- a run's end putting it back, or SUPPLY -- and then the ON
             * goes through the HV warning instead. */
            if (hv_set_point()) {
                ui_hold_reset(&s.out_hold);
                s.out_down = false;
                if (!s.ov_have) {
                    ask_hv_on();
                }
            } else {
                supply_screen_ask_on();
            }
        }
    }
    if (s.out_hold.flash_left > 0) {
        ++s.sup_rev;
    }
    if (supply_screen_set_v() != s.sup_v || supply_screen_set_i() != s.sup_i) {
        s.sup_v = supply_screen_set_v();
        s.sup_i = supply_screen_set_i();
        ++s.sup_rev;
    }

    /* The HV warning's hold: the voltage goes to the supply when it
     * completes. */
    if (s.ask.open && s.ask.down) {
        ++s.ask.rev;
        if (ui_hold_tick(&s.ask.hold, dt_s)) {
            ov_let_go();
            ask_apply();
        }
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

    /* START TEST's hold: the run starts when it completes. */
    if (s.test_down) {
        ++s.test_rev;
        if (ui_hold_tick(&s.test_hold, dt_s)) {
            s.test_down = false;
            ov_let_go();
            test_begin();
        }
    }
    test_tick();

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
        remember_shown();
        return;
    }
    /*
     * In the units the far end slews in -- its command across the range a
     * command carries -- and at its rate, slew_of(); at 100% there is none
     * and the output is drawn at the command at once.  An angle a second
     * would not be: with CENTRE off the middle a degree is a different share
     * of the command on either side, and PAUSE, which keeps the servo where
     * the horn is drawn, would keep it somewhere else.
     */
    if (s.dr.on) {
        remember_shown();       /* drawn above, in its own mapping */
        return;
    }
    const float per_ms = slew_per_ms(s.speed_pct);
    const float step = (per_ms < 0.0f) ? -1.0f : per_ms * dt_s * 1000.0f;
    const float want = us_to_cmd(deg_to_us_f(s.commanded_deg));
    const float next = chase_cmd(s.shown_cmd, want, step);

    if (next == want) {
        s.shown_cmd = want;
        if (s.shown_deg != s.commanded_deg) {
            s.shown_deg = s.commanded_deg;
            ++s.ctrl_rev;
        }
        remember_shown();
        return;
    }
    s.shown_cmd = next;
    s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
    ++s.ctrl_rev;
    remember_shown();
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
    return s.ov_open && !s.warn.open && !s.kp.open && !s.tk.open && !s.ch.open
           && !s.ask.open;
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
        s.drawn_sup[buf]   = s.sup_rev;
        s.drawn_power[buf] = s.power_rev;
        s.drawn_warn[buf]  = s.warn.rev;
        s.drawn_ask[buf]   = s.ask.rev;
        s.drawn_test[buf]  = s.test_rev;
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
    if (s.drawn_sup[buf] != s.sup_rev) {
        s.drawn_sup[buf] = s.sup_rev;
        clipped(c, sup_row_rect(), draw_sup_row);
    }
    if (s.ov_open && s.warn.open && s.drawn_warn[buf] != s.warn.rev) {
        s.drawn_warn[buf] = s.warn.rev;
        clipped(c, warn_apply_rect(), draw_warn_apply);
    }
    if (s.ov_open && s.ask.open && s.ask.hv
        && s.drawn_ask[buf] != s.ask.rev) {
        s.drawn_ask[buf] = s.ask.rev;
        clipped(c, warn_apply_rect(), draw_ask_apply);
    }
    if (page_shown() && s.tabs.selected == PG_TEST
        && s.drawn_test[buf] != s.test_rev) {
        s.drawn_test[buf] = s.test_rev;
        clipped(c, test_start_rect(), draw_test_start);
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
    test_end_now(SERVO_TEST_AB_LEFT);
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    post(SERVO_CMD_DISARM, 0);
    s.armed = false;
    ui_hold_reset(&s.arm);
    s.arm_down = false;
    /* The supply's output stays as it is, as leaving SUPPLY keeps it; a
     * press on OUTPUT OFF as the screen goes is the OFF being made. */
    if (s.out_down && s.out_press_on) {
        supply_screen_ask_off();
    }
    ui_hold_reset(&s.out_hold);
    s.out_down = false;
    ++s.sup_rev;
    /* The sweep and everything kept about it: a reopened screen draws no
     * curve, drain or acknowledgement of this one. */
    stop_sweep();
    s.paused      = false;
    s.toggle_live = false;
    s.sw.running  = false;
    s.sw.paused   = false;
    s.ov_open = false;
    s.kp_alone = false;
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
    /* And a sweep: the event that went missing may be the PAUSE that was to
     * stop it, and the panel would go on repeating it.  Paused where the
     * output has got to, as PAUSE pauses it. */
    if (s.sweeping) {
        hold_sweep();
    }
    /* And a run, for the same reason: the lost event may be STOP TEST.
     * START TEST's hold goes with the other holds. */
    test_end_now(SERVO_TEST_AB_TOUCH);
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    ++s.test_rev;
    ui_slider_release(&s.speed);
    ui_hold_reset(&s.arm);
    s.arm_down = false;
    /* OUTPUT OFF is a press too, and its lost release an OFF made; OUTPUT
     * ON's hold is dropped, and SUPPLY drops an ON not yet collected. */
    if (s.out_down && s.out_press_on) {
        supply_screen_ask_off();
    }
    ui_hold_reset(&s.out_hold);
    s.out_down = false;
    ++s.sup_rev;
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
    ui_hold_reset(&s.ask.hold);
    s.ask.down = false;
    ++s.ask.rev;
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
