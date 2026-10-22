/**
 * @file
 * @brief The SERVO screen's private seam between servo_screen*.c, the screen,
 * servo_workbench.c, its task strip, TEST page and TARGET row
 * (servo_workbench_int.h), servo_settings_panel.c, its settings pages and
 * the panels they open, servo_test_panel.c, its automatic test, and
 * servo_limit_panel.c, its limit run: the layout they draw on, the
 * profiles, the pages' rows and presses, the state they read and write,
 * the functions each calls of the others, and the small helpers they
 * inline.  Nothing outside shared/ui includes this header; the screen's
 * interface is servo_screen.h.
 *
 * The state is one object, `s`, defined in servo_screen.c, so its .bss
 * stays where psram_bss.lf puts servo_screen's.  The helpers below are
 * static inline, as they were static in the one file the screen was: GCC
 * inlines them in each file as it did.  test_ended() is static inline as
 * well.  The one file inlined it in servo_screen_service() and called it
 * out of line from three places; servo_test_panel.c inlines it at those.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "kst_panel.h"
#include "servo_limrun.h"
#include "servo_screen.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_target_row.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#define W UI_PANEL_W /**< The panel's width, px: the screen's own. */
#define H UI_BODY_H  /**< The body's height under the band, px. */

#define PAD    UI_MARGIN /**< The screen's outer margin, px. */
/**
 * @brief The left card's width, px: the page area under the strip, where
 *        the dial, TEST's run cards, SETTINGS' pages and PROGRAM's list
 *        are, and the panels and the programmer's sessions over them.
 */
#define LCARD_W 620
#define RCARD_X (PAD + LCARD_W + 8) /**< The output card's left edge, px. */
#define RCARD_W (W - RCARD_X - PAD) /**< The output card's width: 380 px. */
/**
 * @brief SERVO's task strip at the 1024 x 600 sizes: four tabs of 150 x
 *        40 px, 4 px apart, from y 8, and the page from y 56.
 *
 * The shared UI_WB_ figures stay at their 24 px strip for MOTOR & ESC and
 * SUPPLY until those are laid out at the new sizes; these are SERVO's own,
 * the shared figures plus the difference.  SV_PAGE_Y is the top of the
 * page area a task and PROGRAM's sessions draw in.
 */
#define SV_STRIP_X UI_WB_STRIP_X
#define SV_STRIP_Y (UI_WB_STRIP_Y + 2)  /**< The strip's top: 8 px. */
#define SV_STRIP_H (UI_WB_STRIP_H + 16) /**< A tab's height: 40 px. */
#define SV_TAB_W   (UI_WB_TAB_W + 38)   /**< A tab's width: 150 px. */
#define SV_TAB_GAP UI_WB_TAB_GAP        /**< Between two tabs: 4 px. */
#define SV_PAGE_Y  (UI_WB_PAGE_Y + 18)  /**< The page's top: 56 px. */
/** @brief The left card's top: the page area under SERVO's strip. */
#define CARD_Y  SV_PAGE_Y
#define CARD_H  (H - CARD_Y - PAD) /**< The left card's height, px. */
/**
 * @brief The output card's top: the body's margin.  The strip spans the
 *        left card only, so the output card, the same on every task, runs
 *        from the top margin to the bottom one beside it.
 */
#define RCARD_Y PAD
#define RCARD_H (H - 2 * PAD) /**< The output card's height, px. */
/**
 * @brief The lowest row a control that stops what the bench drives may
 *        reach, in the screen's coordinates: 4 px above the alert band.
 */
#define STOP_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in.
 *
 * SV_FONT_LABEL (10 x 20) for every label, button, row and value;
 * SV_FONT_BODY (12 x 24) for prose read at a distance: the warnings and
 * their lines; SV_FONT_HEAD (20 x 36) for a warning's title and the
 * question's values; SV_FONT_NUM (32 x 44, digits, sign, point and space
 * only) for LIVE's three readings.
 */
#define SV_FONT_LABEL (&gfx_font_10x20)
#define SV_FONT_BODY  UI_FONT_BODY  /**< 12 x 24: warnings, run results. */
#define SV_FONT_HEAD  (&gfx_font_20x36) /**< 20 x 36: a warning's title. */
#define SV_FONT_NUM   (&gfx_font_num_32x44) /**< 32 x 44: LIVE's readings. */
#define SV_CELL       10 /**< SV_FONT_LABEL's cell width, px. */

/*
 * The output card, top to bottom: the type and frame rate in force, the
 * servo rail's live power and its plot, the supply's set points and output
 * switch, SPEED, the TARGET row, CENTRE / SWEEP / RELEASE and ARM.  Every
 * row tapped is UI_TAP_MIN or taller; ARM ends above the alert band.
 */
#define RC_X      (RCARD_X + 12) /**< The output card's inner column, px. */
#define RC_W      (RCARD_W - 24) /**< Its width: 356 px. */
#define TAG_Y     (RCARD_Y + 12) /**< The type pill's top, px. */
#define TAG_H     44             /**< The type pill's height, px. */
/** @brief The line over the plot: the meter's name and three readings. */
#define PWR_TXT_Y (TAG_Y + TAG_H + 8)
/**
 * @brief The meter's name before the readings (k_line_w): 90 px for PD
 *        MINI, 134 px for "INA 0x41 CH2", 12 cells of 10 px and a gap.
 */
#define PWR_LAB_W     90
#define PWR_LAB_W_INA 134 /**< The same beside an INA3221 channel's name. */
#define PWR_Y     (PWR_TXT_Y + 24) /**< The power plot's top, px. */
#define PWR_H     70               /**< The power plot's height, px. */
/**
 * @brief The supply's set points, each a value the keypad opens on, and
 *        OUTPUT ON and OFF: the same set points and switch as SUPPLY's,
 *        not a copy.  SET (SOLL) takes the first 48 px, each value 84 px,
 *        8 px apart, and the switch the rest, 124 px: AUSGANG AUS is 11
 *        cells.
 */
#define SUP_Y     (PWR_Y + PWR_H + 8)
#define SUP_H     UI_TAP_MIN           /**< The SET row's height, px. */
#define SUP_VAL_X (RC_X + 48)          /**< The voltage's value box, px. */
#define SUP_VAL_W 84                   /**< A set point's box, px. */
#define SUP_GAP   8                    /**< Between the SET row's boxes. */
/** @brief OUTPUT ON and OFF, after the two set points. */
#define SUP_OUT_X (SUP_VAL_X + 2 * (SUP_VAL_W + SUP_GAP))
#define SPEED_Y   (SUP_Y + SUP_H + 10) /**< SPEED's label row, px. */
#define SLIDER_Y  (SPEED_Y + 24)       /**< SPEED's track, px. */
#define SLIDER_H  40                   /**< SPEED's track's height, px. */
/**
 * @brief The TARGET row, what the tester drives and the control that
 *        picks it, clear of SPEED's thumb and its shadow, which paint
 *        UI_SLIDER_OVERHANG + 2 px below the track.
 */
#define TARGET_Y  (SLIDER_Y + SLIDER_H + UI_SLIDER_OVERHANG + 7)
#define TARGET_H  UI_TAP_MIN           /**< The TARGET row's height, px. */
#define BTN_Y     (TARGET_Y + TARGET_H + 10) /**< CENTRE, SWEEP, RELEASE. */
#define BTN_H     UI_TAP_MIN           /**< Their height, px. */
#define BTN_GAP   8                    /**< Between two of them, px. */
#define ARM_Y     (BTN_Y + BTN_H + 10) /**< ARM and DISARM, px. */
#define ARM_H     60                   /**< ARM's height, px: 9.0 mm. */
_Static_assert(SV_STRIP_X + 4 * SV_TAB_W + 3 * SV_TAB_GAP + 3 <= RCARD_X,
               "SERVO's strip ends left of the output card");
_Static_assert(SV_STRIP_Y + SV_STRIP_H + 3 <= SV_PAGE_Y,
               "SERVO's strip ends above its page");
_Static_assert(ARM_Y + ARM_H <= STOP_FLOOR,
               "DISARM ends above the alert band");
_Static_assert(SUP_Y + SUP_H <= STOP_FLOOR,
               "OUTPUT OFF ends above the alert band");
_Static_assert(ARM_Y + ARM_H <= RCARD_Y + RCARD_H,
               "ARM ends inside the output card");

/*
 * LIVE's card: the horn's angle in the bench's numerals top left, ARM
 * FIRST under it, the dial in the middle and, along the bottom, the three
 * readings in SV_FONT_NUM, a third of the card each.
 */
#define READ_W     ((LCARD_W - 24) / 3) /**< A reading's column: 198 px. */
/** @brief Reading @p k's column (0 COMMANDED, 1 MEASURED, 2 CURRENT). */
#define READ_X(k)  (PAD + 12 + (k) * READ_W)
#define READ_NUM_H 44                    /**< A reading's numerals, px. */
#define READ_NUM_Y (H - PAD - 12 - READ_NUM_H) /**< Their top, px. */
#define READ_LBL_Y (READ_NUM_Y - 26)     /**< The label and unit row. */
#define SEG_X      (PAD + 16)            /**< The horn's angle, top left. */
#define SEG_Y      (CARD_Y + 14)         /**< Its top, px. */
#define ARM_NOTE_Y (CARD_Y + 58)         /**< ARM FIRST under it, px. */

/** @brief The top of a standard servo's rating.  A set point raised past
 *         it is applied only after the HV warning is held for two seconds. */
#define STD_SERVO_V_MAX 6.0f
_Static_assert(KST_PANEL_STD_MV == 6000,
               "the programmer's standard-servo ceiling is STD_SERVO_V_MAX");

/*
 * The panels: the keypad, keyboard, list, warnings, question and the servo
 * programmer cover the whole left column, the strip's tabs with it, so a
 * panel is answered before the task changes.  The output card -- ARM,
 * CENTRE, RELEASE and the readings -- stays where it is and works.
 */
#define OV_X      PAD             /**< The left column's panels, px. */
#define OV_Y      PAD             /**< Their top, px. */
#define OV_W      LCARD_W         /**< Their width, px. */
#define OV_H      (H - 2 * PAD)   /**< Their height, px. */
/*
 * The pages of rows, in the page area under the strip.  SETTINGS' pages
 * under their own tabs (OUTPUT, SWEEP, DUT): two columns, each row its
 * label on the left and its value on the right.  A run's settings, one
 * level down from its card on TEST, under the run's name: three columns,
 * each row its label over its value, so the automatic test's 15 rows fit
 * in 5 rows of UI_TAP_MIN.
 */
#define OV_TAB_Y  (CARD_Y + 8)    /**< SETTINGS' page tabs, px. */
#define OV_TAB_H  40              /**< Their height, px. */
#define OV_TAB_W  (OV_W - 20)     /**< Their row: three of 200 px. */
#define OV_ROW0   (CARD_Y + 52)   /**< SETTINGS' first row, px. */
#define OV_PITCH  52              /**< From row to row, px. */
#define OV_ROW_H  UI_TAP_MIN      /**< A row's height, px. */
#define OV_COL_W  ((OV_W - 30) / 2) /**< A column of SETTINGS: 295 px. */
#define OV_VAL_W  140             /**< A row's value, px. */
/** @brief The OUTPUT page's notes, 2 px under its fifth row. */
#define OV_NOTE_Y (OV_ROW0 + 4 * OV_PITCH + OV_ROW_H + 2)
#define NOTE_PITCH 21             /**< A note's line pitch, px. */
#define RL_ROW0   (CARD_Y + 56)   /**< A run's first row, px. */
#define RL_PITCH  71              /**< From row to row: label and value. */
#define RL_LBL_H  21              /**< A run's row's label over its value. */
#define RL_COL_W  ((OV_W - 40) / 3) /**< A column of a run's: 193 px. */
#define RL_ROWS   5               /**< The rows a column holds. */
/** @brief A run's note under its rows: the automatic test's stall note,
 *         4 px under the last value. */
#define RL_NOTE_Y (RL_ROW0 + RL_ROWS * RL_PITCH + 2)
/** @brief The save line under SETTINGS' rows and notes, 20 px tall,
 *         ending 4 px above the alert band. */
#define OV_SAVE_Y (STOP_FLOOR - 20)
_Static_assert(OV_NOTE_Y + 4 * NOTE_PITCH + 20 <= OV_SAVE_Y,
               "the OUTPUT page's five note lines end above the save line");
_Static_assert(RL_NOTE_Y + 20 <= STOP_FLOOR,
               "a run's note ends above the alert band");

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

/* A profile the bench drives; k_types in servo_screen.c holds them. */
typedef struct {
    const char     *name;
    uint16_t        min_us, centre_us, max_us;
    const uint16_t *rates;       /* the frame rates offered, Hz           */
    uint8_t         rate_count;
    uint16_t        default_hz;
    uint16_t        max_hz;      /* the profile's ceiling; 0 is the pause */
    bool            heli;        /* choosing it needs the warning          */
} servo_type_t;

/* The profiles, in servo_screen.c. */
#define TYPE_COUNT 5
extern const servo_type_t k_types[TYPE_COUNT];

/* SETTINGS' pages, under its own tabs, and the two runs' settings, each
 * opened from its card's SETTINGS on TEST. */
enum { PG_OUTPUT = 0, PG_SWEEP, PG_DUT, PG_COUNT,
       PG_AUTO = PG_COUNT, PG_LIMRUN };

/* Which run's settings TEST shows, one level down from its card. */
enum { RL_NONE = 0, RL_AUTO, RL_LIMIT };

/* What a settings row edits. */
enum { R_TYPE = 0, R_RATE, R_MIN, R_CENTRE, R_MAX, R_TRIM, R_TRAVEL,
       R_REVERSE, R_SETTING, R_TEXT, R_HV, R_ENC_CENTRE, R_LIM_MODE,
       R_LIM_PULSES };

typedef struct {
    uint8_t      page;
    uint8_t      kind;
    setting_id_t id;          /* R_SETTING                              */
    ui_text_id_t label;
    uint8_t      col, row;
    bool         wide;        /* the value takes both columns' width    */
} ov_row_t;

/* What a press is on, in the pages and the panels they open. */
enum { OP_NONE = 0, OP_TAB, OP_ROW, OP_TRIM_DN, OP_TRIM_UP, OP_KEYPAD,
       OP_TEXT, OP_CHOICE, OP_CHOICE_CANCEL, OP_WARN_APPLY, OP_WARN_CANCEL,
       OP_SUP_V, OP_SUP_I, OP_ASK_APPLY, OP_ASK_CANCEL, OP_TEST_START,
       OP_LIM_START, OP_RUN_SET, OP_RUN_CLOSE };

/* What the list chooses, and what the keypad types. */
enum { CH_NONE = 0, CH_TYPE, CH_RATE, CH_ENUM };
enum { KT_NONE = 0, KT_MIN, KT_CENTRE, KT_MAX, KT_TRAVEL, KT_RATE,
       KT_SETTING, KT_SUP_V, KT_SUP_I, KT_TRIM };

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

/* The screen's state, the pages' with it. */
typedef struct {
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
    bool     have_feedback;
    /* The output encoder's last reading (servo_screen_encoder()). */
    bool     enc_valid;
    bool     enc_no_magnet; /**< not valid: the sensor sees no magnet     */
    uint16_t enc_raw;
    int      shown_q_enc;  /**< the angle as drawn, in tenths             */

    bool     dragging;
    int      drag_id;

    int      shown_q_deg; /**< the position as drawn, in tenths          */
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
    bool     knob_had_driving;
    uint32_t knob_post;
    float    knob_prev_deg;
    /* A finger owned the dial at some point since knob_frame(). */
    bool     knob_finger;
    /* The settings, a sweep or a test owned the horn at some point since
     * knob_frame(), even if it let go again before the turn is applied. */
    bool     knob_owned;

    /*
     * Arming, which this screen needs as much as MOTOR & ESC does: until the
     * bench is armed the IOmcu writes a pulse of length zero to every
     * PWM pin, so dragging the horn moves nothing and shows nothing on a
     * scope.  The gesture is ui_widgets' -- the same two seconds and the same
     * fade as the other screen's, because it is the same control.
     */
    /* Whether the commanded value follows its controls (ui_value_gate.h):
     * the bench's report, an ARM posted whose arm has not landed yet --
     * collected by the panel, which arms only once the release it owes has
     * been written -- and a DISARM or STOP not yet answered.  gate.armed is
     * what the ARM button reads. */
    ui_value_gate_t gate;
    /* The bench is armed for any role, the motor's included: what keeps the
     * programmer off the port while the motor is armed and this screen's
     * surfaces are not (servo_screen_set_bench_armed()). */
    bool       bench_armed;
    /* The commanded value is the surface's rest and nothing has been
     * commanded since: it follows the rest through a change of profile. */
    bool       at_rest;
    ui_hold_t  arm;
    bool       arm_down;   /**< a press is on the ARM button          */
    int        arm_id;     /**< which contact it is                   */
    uint32_t   arm_rev;
    uint32_t   drawn_arm[2];

    gfx_rect_t arm_btn, centre_btn, sweep_btn, release_btn;
    bool       kst_shown;        /* a session had the left column      */
    bool       kst_held;         /* and a port: ARM takes no press      */
    bool       sup_held;         /* the port holds the supply: SET is dim */
    uint32_t   drawn_kst[2], drawn_kst_hold[2];
    ui_slider_t speed;

    /* The servo rail's live power, from its meter (rail_now()): the line
     * of readings and the plot under it. */
    ui_plot_t      power;
    supply_state_t sup;
    bool           have_sup;
    uint32_t       power_rev;
    uint32_t       drawn_power[2];
    /* The supply was the panel's model when the line was drawn: the pill. */
    bool           model_shown;
    /* The meter, as the panel last said it (servo_screen_source()), and the
     * INA3221's last CH1 window (servo_screen_window()). */
    servo_source_id_t source;
    /* The change count the meter in force was decided at
     * (servo_source_changes()): an answer from an earlier poll is not taken
     * over it. */
    bool              have_changes;
    uint32_t          changes;
    sense_link_win_t  win;
    bool              have_win;
    /* Why the INA3221 is not the meter now, and why and at which change
     * count it last stopped being it (servo_screen_source_why(),
     * servo_screen_source_dropped()). */
    servo_source_why_t src_why;
    bool               have_drop;
    servo_source_why_t drop_why;
    uint32_t           drop_at;
    /* The CURRENT row: what it shows (cur_key_of()), and its own count of
     * changes, so a changed digit repaints its value and not the card. */
    int32_t        cur_key;
    uint32_t       cur_rev;
    uint32_t       drawn_cur[2];

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

    /*
     * The task strip (ui_workbench.h): LIVE, TEST, PROGRAM, SETTINGS.  A
     * tile opens LIVE; each task keeps its state for the session: SETTINGS
     * its page (tabs), TEST the run whose settings are open (run_list),
     * PROGRAM its list.  The strip's count of changes repaints it.
     */
    ui_wb_strip_t wb;
    uint8_t      run_list;      /* RL_*: a run's settings open on TEST    */
    uint32_t     strip_rev;
    uint32_t     drawn_strip[2];
    /* PROGRAM's list (prog_registry.h): its view, and the rows last drawn. */
    prog_view_t  prog_view;
    uint32_t     prog_sig;
    /* Whether a panel or the programmer covered the left column when the
     * buffer was last drawn: a change repaints it whole. */
    bool         drawn_cover[2];
    /* The TARGET row on the right card: what the tester drives, and the
     * control that opens the choice of target. */
    ui_target_row_t trow;
    /* SETTINGS' pages and the panels they open. */
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
        uint32_t  rev;
    } warn;
    uint32_t drawn_warn[2];
    uint8_t  drawn_save[2];

    servo_rate_state_t rate_st;   /* what became of the rate rate_hz    */
    uint16_t           rate_hz;

    /* The sweep: the same curve the IOmcu runs, on this screen's own
     * clock, to draw the horn by. */
    bool        sweep_able;      /* the IOmcu speaks 4.2 or later  */
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
    bool      enc_held;      /**< IOmcu holds the encoder on (set_enc_held) */
    int       test_note;     /**< servo_str_t the engine refused a START
                                  with, 0 none; test_blocked() is live  */
    int       test_file;     /**< its files' number; 0 not yet, -1 none */
    bool      test_report;   /**< the card took its .TXT whole          */
    bool      test_off;      /**< USER LOGS off at its start: no file  */
    int       test_blocked_seen; /**< test_blocked() at the last tick   */
    bool      test_online_seen;  /**< the supply answering, likewise    */
    bool      test_box;      /**< the left card shows the run          */
    bool      test_seen;     /**< the run was running when last looked */
    bool      test_on_ina;   /**< the run reads the INA3221's windows   */
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

    /*
     * The limit run (servo_limrun.h): the limit search on the INA3221's
     * windows, and with LIMIT RUN at SEARCH+PROG, LEFT ANGLE and RIGHT
     * ANGLE of a KST or Chaservo servo on the programming port lowered.
     * Its START is the LIMIT RUN card's, held 2 s; its box is the test's
     * place on LIVE's card.  One of the two runs at a time.
     */
    servo_limrun_t lim;
    bool      lim_prog;      /**< LIMIT RUN: SEARCH+PROG, for the session */
    bool      lim_pulses;    /**< SERVO PULSES at 1000-2000, session */
    ui_hold_t lim_hold;
    bool      lim_down;
    bool      lim_box;       /**< the left card shows the limit run     */
    bool      lim_seen;      /**< it was running when last looked       */
    /** The run ended in a session and its LEAVE is on its way: the box,
     *  not the programmer, stays on the left card, and the programmer's
     *  run ends once the LEAVE has (lim_end_due). */
    bool      lim_leaving;
    bool      lim_end_due;
    int       lim_file;      /**< its report's number; 0 not yet, -1 none */
    bool      lim_report;    /**< the card took the report whole       */
    bool      lim_off;       /**< USER LOGS was off at its start        */
    const char *lim_note;    /**< why the last START was refused, or NULL */
    uint32_t  lim_changes;   /**< the meter's change count at the start */
    uint32_t  lim_sig;
    /* The outbox the logger last peeked, and whose files' number comes
     * back (servo_screen_test_files()). */
    bool      peek_lim;
    servo_test_out_t peek_kind;
    bool      files_lim;
    /* The logger has an OPEN or an END of the last run not answered yet
     * (servo_screen_test_logger_busy()). */
    bool      logger_busy;

    uint32_t ctrl_rev;
    uint8_t  drive_target; /* 0 the outputs, 1 + n programming port n */
    uint8_t  drive_pin;    /* that port's GPIO, or SERVO_SCREEN_NO_PIN */
    bool     drive_pwm;    /* that port renders its pulse              */
    uint64_t pins;         /* GPIOs bound as SERVO PWM, bit n = GPIO n */
    bool     pins_known;   /* a binding was read                       */
    uint32_t drawn_ctrl[2];
    unsigned drawn_mask;
} servo_screen_state_t;

/* The state, in servo_screen.c. */
extern servo_screen_state_t s;

/* --- the screen's side, servo_screen*.c, which the other four files call */

/* The profile in force (apply_profile()) and the one asked for, which
 * opens the warning first where it can destroy a servo (ask_profile()). */
void apply_profile(int t, uint16_t hz);
void ask_profile(int t, uint16_t hz);
/* The commanded position said again under a changed profile. */
void reissue(void);
/**
 * @brief @p text in lines of at most @p cells cells of face @p f, from
 *        (@p x, @p y) down by @p pitch px, each broken at the last double
 *        space that keeps it within them, or else at the last single space.
 * @param c the canvas, or NULL to count the lines only
 * @param x the lines' left edge, px
 * @param y the first line's top, px
 * @param pitch from line to line, px
 * @param text the text
 * @param cells the cells a line holds
 * @param ink the text's colour
 * @param f the face
 * @return the lines it takes
 */
int wrap_text(gfx_canvas_t *c, int x, int y, int pitch, const char *text,
              int cells, gfx_color_t ink, const gfx_font_t *f);

/* A pulse as an angle (us_to_deg() below), and the commands a run posts
 * as a finger's: a command (post()), RELEASE (release()), and the sweep
 * ended (stop_sweep()) or as SETUP sets it (sweep_cfg_now()). */
float us_to_deg_f(float us);
void post(servo_cmd_kind_t kind, uint16_t us);
void release(void);
void stop_sweep(void);
sweep_cfg_t sweep_cfg_now(void);

/* Whether a run is under way, told to the panel where it changes, and a
 * supply sample as a run reads it: both runs' (servo_screen_on_testing()). */
void test_tell(void);
servo_test_reading_t test_reading_of(const supply_state_t *st);

/* What an edge of the value's state does: into LIVE, the value is the
 * rest; out of LIVE, a run ends as @p why, and so does every motion. */
void value_edge(ui_value_edge_t edge, servo_test_abort_t why);

/* --- servo_settings_panel.c's side, which the screen calls --------------- */

/* Every panel shut: the keypad, the keyboard, the list, the warning and the
 * question (close_panels()); and, once a set point's keypad is done, the
 * page under them repainted (close_alone()). */
static inline void close_panels(void)
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

/* A set point's keypad, or the question after it, is done: the task's page
 * shows again under the panel that went. */
static inline void close_alone(void)
{
    close_panels();
    servo_invalidate();
}

/** @brief The rows of the pages, page by page, and their count. */
extern const ov_row_t k_rows[];
extern const int k_row_count; /**< The rows k_rows holds. */

/**
 * @brief Row @p i's rectangle: on SETTINGS' pages its column, or both for a
 *        wide one, UI_TAP_MIN tall; on a run's, its label over its value.
 * @param i the row in k_rows
 * @return the row: a tap anywhere on it edits it
 */
gfx_rect_t row_rect(int i);
/**
 * @brief Where row @p i's value is drawn: right in its row on SETTINGS'
 *        pages, under its label on a run's.
 * @param i the row in k_rows
 * @return the value's box
 */
gfx_rect_t value_rect(int i);
/**
 * @brief TRIM's two steps, inside its value, UI_TAP_MIN px wide each.
 * @param i TRIM's row in k_rows
 * @param up true for +, false for -
 * @return the step
 */
gfx_rect_t trim_rect(int i, bool up);
/**
 * @brief The list's choice @p i: two columns of CHOICE_MAX / 2.
 * @param i the choice
 * @return its button
 */
gfx_rect_t choice_rect(int i);
/** @brief The list's CANCEL, bottom right. @return the button */
gfx_rect_t choice_cancel_rect(void);
/** @brief A warning's or the question's CANCEL, bottom right.
 *  @return the button */
gfx_rect_t warn_cancel_rect(void);
/**
 * @brief The decimals a setting's value is typed and drawn with.
 * @param id the setting
 * @return 2 for a float stepping under 1, else 0
 */
int decimals_of(setting_id_t id);
/**
 * @brief Whether a row is a TARGET or PORT PWM setting under its lock.
 * @param r the row
 * @return true when it takes no tap now
 */
bool target_locked(const ov_row_t *r);

/* OUTPUT ON with a set point past a standard servo's rating: the HV
 * warning.  The question or the warning answered: what it was about. */
void ask_hv_on(void);
void ask_apply(void);
/* A press on a page or a panel: its DOWN, and its moves and its release. */
void ov_down(const touch_event_t *evt);
void ov_rest(const touch_event_t *evt);
/* The panel open over the left column, and the parts a frame repaints by
 * themselves. */
void draw_overlay(gfx_canvas_t *c);
void draw_save_line(gfx_canvas_t *c);
void draw_warn_apply(gfx_canvas_t *c);
void draw_ask_apply(gfx_canvas_t *c);


/* --- servo_test_panel.c's side, which the other files call -------------- */

/* The restore's stages: the OFF the run's end posted not yet taken; taken,
 * waiting for the next frame; waiting for a sample taken since; a sample
 * showed the output off. */
enum { RS_OFF_POSTED = 0, RS_OFF_SENT, RS_SAMPLE, RS_OFF_SEEN };

/* The automatic test. */
int test_blocked(void);
float test_top_v(void);
void test_begin(void);
void test_end_now(servo_test_abort_t why);
void ask_hv_test(void);
void draw_test_start(gfx_canvas_t *c);
void draw_test_lines(gfx_canvas_t *c);
/* A STALL AT no reading can pass, at (@p x, @p y), or nothing. */
void draw_test_stall(gfx_canvas_t *c, int x, int y);

/* The run against the meter in force (test_meter_follow()); every frame
 * (test_tick()) and whichever screen is up (test_restore_service()); what
 * the box shows of it, as one number (test_signature()); and the box. */
void test_meter_follow(servo_source_id_t id, uint32_t changes);
void test_restore_service(void);
uint32_t test_signature(void);
void test_tick(void);
void draw_test_box(gfx_canvas_t *c);

/* A run that was running is over, however it ended: its OFF is posted
 * (test_apply()), and the restore of the set points starts waiting. */
static inline void test_ended(void)
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

/* --- servo_limit_panel.c's side, which the other three files call -------- */

/* Why the programming is not offered, why a run cannot start now, or
 * NULL; the run ended now. */
const char *lim_prog_why(void);
const char *lim_blocked(void);
void lim_end_now(servo_test_abort_t why);
/* START LIMIT RUN's hold completed; the run every frame; the ended run's
 * LEAVE, whichever screen is up; the meter changed under the run. */
void lim_begin(void);
void lim_tick(void);
void lim_leave_follow(void);
void lim_meter_follow(servo_source_id_t id, uint32_t changes);
/* What the box and the LIMIT RUN card show of the run, as one number. */
uint32_t lim_signature(void);
/* The run's box on LIVE's card; START LIMIT RUN and its card's lines on
 * TEST; the mode's note on the run's settings at (@p x, @p y). */
void draw_lim_box(gfx_canvas_t *c);
void draw_lim_start(gfx_canvas_t *c);
void draw_lim_lines(gfx_canvas_t *c);
void draw_lim_mode_note(gfx_canvas_t *c, int x, int y);

/* --- helpers the four files inline --------------------------------------- */

static inline const servo_type_t *type(void) { return &k_types[s.type]; }

/*
 * The range a command carries: the narrowest one centred on CENTRE that
 * holds MIN and MAX.  The IOmcu knows a channel only by its two
 * endpoints and rests a surface at their midpoint -- on RELEASE, before an
 * arm, after a silence -- so MIN..MAX itself would rest a servo whose CENTRE
 * is not midway between them away from its centre.  Commands never leave
 * MIN..MAX: svs_deg_to_us() clamps them.  The PULSE keypads keep this
 * range inside OUT_FLOOR_US..OUT_CEILING_US.
 */
static inline void cmd_range(uint16_t *lo, uint16_t *hi)
{
    const unsigned below = (unsigned)s.centre_us - (unsigned)s.min_us;
    const unsigned above = (unsigned)s.max_us - (unsigned)s.centre_us;
    const unsigned half  = (below > above) ? below : above;
    *lo = (uint16_t)((unsigned)s.centre_us - half);
    *hi = (uint16_t)((unsigned)s.centre_us + half);
}

/*
 * The longest pulse the far end can render: the top of the range a command
 * carries, which is past PULSE MAX when CENTRE is off the middle.  A pulse
 * of an old span under a new range reaches it between two transactions, so
 * the frame rate's pause is kept from it rather than from PULSE MAX.
 */
static inline uint16_t cmd_top(void)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return hi;
}

static inline float us_to_deg(uint16_t us)
{
    return us_to_deg_f((float)us);
}

static inline float clamp_travel(float deg)
{
    if (deg < -s.travel_deg) { return -s.travel_deg; }
    if (deg >  s.travel_deg) { return  s.travel_deg; }
    return deg;
}

/*
 * Whether the commanded value follows its controls: only in UI_VALUE_LIVE.
 * The one question every input, the drawing, a run and a late callback ask:
 * not on a disarmed bench, whose pins carry no pulse, not while an ARM is
 * on its way, and not from a DISARM or a STOP until the bench reports
 * itself disarmed.
 */
static inline bool value_live(void)
{
    return ui_value_live(&s.gate);
}

/* The fastest frame rate the profile in force allows with these pulses:
 * the pause rule, under the profile's own ceiling. */
static inline uint16_t max_rate_for(int t, uint16_t max_us)
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
static inline bool dangerous(int t, uint16_t hz)
{
    return k_types[t].heli || hz > SAFE_RATE_HZ;
}

static inline bool in_force_dangerous(void)
{
    return dangerous(s.type, s.frame_hz);
}

/* Whether the encoder's last reading is on show and may be used: one the
 * link carried, with the encoder on in SETUP.  A link that goes down takes
 * the reading with it (servo_screen_set_link()). */
static inline bool enc_shown(void)
{
    return s.enc_valid && s.link_up && settings_get_bool(SET_ENC_EN);
}

static inline gfx_rect_t overlay_area(void)
{
    return (gfx_rect_t){ OV_X, OV_Y, OV_W, OV_H };
}

/**
 * @brief The voltage's value or the current limit's on the SET row.
 * @param k 0 the voltage, 1 the current limit
 * @return the box a tap opens the keypad from
 */
static inline gfx_rect_t sup_val_rect(int k)
{
    return (gfx_rect_t){ (int16_t)(SUP_VAL_X + k * (SUP_VAL_W + SUP_GAP)),
                         SUP_Y, SUP_VAL_W, SUP_H };
}

/** @brief The bottom row of a warning or the question: its buttons' top,
 *         so they end with ARM above the alert band. */
#define WARN_BTN_Y (STOP_FLOOR - 56)

/**
 * @brief HOLD TO APPLY, or APPLY, bottom left of a warning or the question.
 * @return the button, 240 x 56 px
 */
static inline gfx_rect_t warn_apply_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + 20), WARN_BTN_Y, 240, 56 };
}

/* The automatic test's and the limit run's: the clock they and the
 * supply's readings share, the abort for what took the INA3221 off the
 * rail, and the box on the left card both draw in. */

/* The clock the run and the supply's readings share. */
static inline uint32_t test_now(void)
{
    return s.have_now ? s.now_ms : s.clock_ms;
}

/* A run's abort for the condition that took the INA3221 off the rail. */
static inline servo_test_abort_t test_abort_of(servo_source_why_t why)
{
    switch (why) {
    case SERVO_SOURCE_WHY_NO_LINK:   return SERVO_TEST_AB_LINK;
    case SERVO_SOURCE_WHY_OFF:
    case SERVO_SOURCE_WHY_OLD:
    case SERVO_SOURCE_WHY_NOT_HELD:  return SERVO_TEST_AB_INA_SETUP;
    case SERVO_SOURCE_WHY_SILENT:    return SERVO_TEST_AB_INA_SILENT;
    case SERVO_SOURCE_WHY_NO_WINDOW: return SERVO_TEST_AB_INA_NO_WINDOW;
    case SERVO_SOURCE_WHY_RESET:     return SERVO_TEST_AB_INA_RESET;
    default:                         return SERVO_TEST_AB_INA_METER;
    }
}

/**
 * @brief The run's box on LIVE's card, left, under the horn's angle and
 *        ARM FIRST: four lines of BOX_CELLS cells of SV_FONT_LABEL,
 *        BOX_PITCH px apart, over its one button, STOP TEST while it runs,
 *        CLOSE after.  Its right edge stays 36 px left of the shaft, so the
 *        horn, its boss and its rings show at every angle while the box is
 *        up; it covers the case's left half.
 */
#define BOX_W     346
#define BOX_CELLS ((BOX_W - 20) / SV_CELL) /**< A line of the box: 32. */
#define BOX_PITCH 24                       /**< Its line pitch, px. */
#define BOX_BTN_DY 112                     /**< Its button's top, in it. */

/** @brief The run's box. @return the box, in the screen's coordinates */
static inline gfx_rect_t test_box_rect(void)
{
    return (gfx_rect_t){ PAD + 10, ARM_NOTE_Y + 28, BOX_W,
                         BOX_BTN_DY + UI_TAP_MIN + 10 };
}

_Static_assert(ARM_NOTE_Y + 28 + BOX_BTN_DY + UI_TAP_MIN + 10
                   <= READ_LBL_Y - 8,
               "the run's box ends above LIVE's readings");

/** @brief The box's button. @return STOP TEST or CLOSE, 150 x 48 px */
static inline gfx_rect_t test_btn_rect(void)
{
    const gfx_rect_t b = test_box_rect();
    return (gfx_rect_t){ (int16_t)(b.x + 10), (int16_t)(b.y + BOX_BTN_DY),
                         150, UI_TAP_MIN };
}

/* A run with a step past a standard servo's rating starts only through the
 * HV warning. */
static inline bool test_needs_hv(void)
{
    return test_top_v() > STD_SERVO_V_MAX + 0.001f;
}

/* The run drives the programming port: a KST or Chaservo servo is read
 * first and searched within its own pulses. */
static inline bool lim_reads(void)
{
    return s.drive_target != 0u
        && s.drive_target != SERVO_SCREEN_TARGET_UNKNOWN
        && s.drive_target != SERVO_SCREEN_TARGET_NO_LINK;
}

/* SERVO PULSES is there only for the programming port. */
static inline bool row_hidden(const ov_row_t *r)
{
    return r->kind == R_LIM_PULSES && !lim_reads();
}

/* SEARCH+PROG chosen and offered. */
static inline bool lim_programs(void)
{
    return s.lim_prog && lim_prog_why() == NULL;
}

/* Whether the voltage set point in force is past a standard servo's
 * rating, which OUTPUT ON on this screen applies only through the HV
 * warning. */
static inline bool hv_set_point(void)
{
    return supply_screen_set_v() > STD_SERVO_V_MAX + 0.001f;
}

/* Whether the question about a live output still stands: the output on
 * or coming, not gone off since it was asked -- an OFF and a new ON
 * between two frames start a run the question was not about -- and no
 * stop since.  A stop switches the output off, but the supply reports
 * that a sample later; an APPLY drained in the frame of the stop finds
 * the output still live and is refused by the count. */
static inline bool ask_stands(void)
{
    return supply_screen_output_live()
           && supply_screen_off_count() == s.ask.off_count
           && s.stops == s.ask.stops;
}

static inline void ov_take(const touch_event_t *evt, int code, int which)
{
    s.ov_have    = true;
    s.ov_id      = evt->point.id;
    s.ov_pressed = code;
    s.ov_row     = which;
    ++s.ctrl_rev;
}

static inline void ov_let_go(void)
{
    if (s.ov_have) {
        s.ov_have    = false;
        s.ov_pressed = OP_NONE;
        ++s.ctrl_rev;
    }
}

/* Whether the settings reached the medium: 0 saved, 1 waiting for a quiet
 * moment, 2 refused, 3 changed on SETUP and not asked to be saved. */
static inline uint8_t save_state(void)
{
    if (settings_save_failed()) {
        return 2u;
    }
    if (settings_save_asked()) {
        return 1u;
    }
    return settings_dirty() ? 3u : 0u;
}

/* --- helpers the screen and the limit run inline ------------------------- */

/* The encoder's angle from its centre, degrees, in the direction of the
 * commanded angle: negated with REVERSE on, as us_to_deg() is. */
static inline float enc_deg_now(void)
{
    const float d = servo_test_enc_deg(
        s.enc_raw, (uint16_t)settings_get_int(SET_ENC_CENTRE));
    return s.reverse ? -d : d;
}

/* Armed, asked to arm, or a finger on ARM, or the bench armed for the
 * motor: the programmer takes no port. */
static inline bool bench_arming(void)
{
    return s.gate.armed || s.gate.arm_asked || s.arm_down || s.bench_armed;
}

/* The workbench's own: the strip, the TARGET row, TEST's run cards. */
#include "servo_workbench_int.h"
