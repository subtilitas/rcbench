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
#include "prog5v_box.h"
#include "servo_angle.h"
#include "servo_limrun.h"
#include "servo_rudder_int.h"
#include "servo_screen.h"
#include "servo_rudder_auto_int.h"
#include "servo_ready_int.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_footer.h"
#include "ui_setlist.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#define W UI_PANEL_W /**< The panel's width, px: the screen's own. */
#define H UI_BODY_H  /**< The body's height under the band, px. */

#define PAD    UI_MARGIN /**< The screen's outer margin, px. */
/**
 * @brief The left card's width, px: the page area where the dial, TEST's
 *        and LIMIT RUN's run card, RUDDER's readout and a run's settings
 *        are, and the panels and the programmers' sessions over them.
 */
#define LCARD_W 620
#define RCARD_X (PAD + LCARD_W + 8) /**< The output card's left edge, px. */
#define RCARD_W (W - RCARD_X - PAD) /**< The output card's width: 380px. */
/**
 * @brief The lowest row a control that stops what the bench drives may
 *        reach, in the screen's coordinates: 4px above the alert band.
 */
#define STOP_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)
/**
 * @brief The lowest row the cards reach: 8px over the footer
 *        (ui_footer.h), body y 436.  The footer holds the function's name
 *        and port line, ARM / DISARM and SETTINGS at body y 444 to 492.
 */
#define CARD_FLOOR UI_FOOTER_FLOOR
/** @brief The left card's top, px: the body's margin, level with the
 *         PORT button. */
#define SV_PAGE_Y  PAD
#define CARD_Y  SV_PAGE_Y  /**< The left card's top, px: SV_PAGE_Y. */
#define CARD_H  (CARD_FLOOR - CARD_Y) /**< The left card's height: 428px. */
/**
 * @brief The PORT button over the output card (screen 636, 68, 380, 48):
 *        PORT and the target the tester drives; a tap opens the choice of
 *        OUTPUTS and the programming ports.
 */
#define PORT_X  636
#define PORT_Y  CARD_Y              /**< Its top, px: body 8. */
#define PORT_W  380                 /**< Its width, px. */
#define PORT_H  UI_TAP_MIN          /**< Its height, px: 48. */
/** @brief The output card's top: 6px under the PORT button. */
#define RCARD_Y (PORT_Y + PORT_H + 6)
#define RCARD_H (CARD_FLOOR - RCARD_Y) /**< The output card's height, px. */
_Static_assert(PORT_X == RCARD_X && PORT_W == RCARD_W,
               "the PORT button spans the output card");

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
 * The output card, top to bottom: the servo rail's live power and its
 * plot, SPEED, the type and frame rate in force, and CENTER / SWEEP /
 * RELEASE.  Every row tapped is
 * UI_TAP_MIN or taller.  No function screen draws it: LIVE, SWEEP and
 * RUDDER show READY rows in its place (servo_drive_ready.c), RUDDER AUTO,
 * TEST, LIMIT RUN, KST and HITEC their READY card (servo_ready.c,
 * servo_prog_ready.c).  TEST draws the plot on its left card.
 */
#define RC_X      (RCARD_X + 12) /**< The output card's inner column, px. */
#define RC_W      (RCARD_W - 24) /**< Its width: 356px. */
/** @brief The line over the plot: the meter's name and three readings. */
#define PWR_TXT_Y (RCARD_Y + 10)
/**
 * @brief The meter's name before the readings (k_line_w): 90px for PD
 *        MINI, 134px for "INA 0x41 CH2", 12 cells of 10px and a gap.
 */
#define PWR_LAB_W     90
#define PWR_LAB_W_INA 134 /**< The same beside an INA3221 channel's name. */
#define PWR_Y     (PWR_TXT_Y + 24) /**< The power plot's top, px. */
#define PWR_H     70               /**< The power plot's height, px. */
/** @brief The PROG 5V box in the plot's place on a board that switches
 *         the ports' 5V (prog5v_box.h); the 20px under it hold the
 *         MODELLED pill when the panel's model stands in for the PD mini. */
#define P5_Y      (PWR_Y + 2)
_Static_assert(P5_Y + PROG5V_BOX_H + 20 <= PWR_Y + PWR_H,
               "the PROG 5V box and the pill fit the plot's place");
_Static_assert(RC_W >= PROG5V_BOX_MIN_W, "the PROG 5V box fits the card");
/** @brief SPEED's label row, px: UI_TAP_MIN + 18px under the plot. */
#define SPEED_Y   (PWR_Y + PWR_H + UI_TAP_MIN + 18)
#define SLIDER_Y  (SPEED_Y + 24)       /**< SPEED's track, px. */
#define SLIDER_H  40                   /**< SPEED's track's height, px. */
/**
 * @brief The type pill, the profile in force, clear of SPEED's thumb and
 *        its shadow, which paint UI_SLIDER_OVERHANG + 2px below the track.
 */
#define TAG_Y     (SLIDER_Y + SLIDER_H + UI_SLIDER_OVERHANG + 9)
#define TAG_H     44             /**< The type pill's height, px. */
#define BTN_Y     (TAG_Y + TAG_H + 12) /**< CENTER, SWEEP, RELEASE. */
#define BTN_H     UI_TAP_MIN           /**< Their height, px. */
#define BTN_GAP   8                    /**< Between two of them, px. */
/** @brief ARM and DISARM: the footer's middle slot, 328 x 48px. */
#define ARM_Y     UI_FOOTER_Y
#define ARM_H     UI_FOOTER_H          /**< ARM's height, px: 7.2mm. */
_Static_assert(ARM_Y + ARM_H <= STOP_FLOOR,
               "DISARM ends above the alert band");
_Static_assert(BTN_Y + BTN_H + 8 <= CARD_FLOOR,
               "CENTER, SWEEP and RELEASE end inside the output card");

/*
 * LIVE's card: the horn's angle in the bench's numerals top left, ARM
 * FIRST under it, the dial in the middle and, along the bottom, the three
 * readings in SV_FONT_NUM, each in a column of its own across the card's
 * 596px.  MEASURED's is the widest: -180.0 is 6 cells of 32px, 192px, and
 * 24px stay clear before CURRENT's numerals.
 */
#define READ_W0    192 /**< COMMANDED's column, px: SOLL-PULSBREITE and us. */
#define READ_W1    216 /**< MEASURED's column, px. */
/** @brief CURRENT's column, px: the rest of the card, 188px. */
#define READ_W2    (LCARD_W - 24 - READ_W0 - READ_W1)
/** @brief Reading @p k's column width, px (0 COMMANDED, 1 MEASURED,
 *         2 CURRENT). */
#define READ_W(k)  ((k) == 0 ? READ_W0 : (k) == 1 ? READ_W1 : READ_W2)
/** @brief Reading @p k's column (0 COMMANDED, 1 MEASURED, 2 CURRENT). */
#define READ_X(k)  (PAD + 12 + ((k) > 0 ? READ_W0 : 0) \
                    + ((k) > 1 ? READ_W1 : 0))
_Static_assert(6 * 32 + 24 <= READ_W1,
               "-180.0 in SV_FONT_NUM ends 24px before CURRENT");
#define READ_NUM_H 44                    /**< A reading's numerals, px. */
/** @brief Their top, px: they end 8px over the card's floor, 16px over
 *         the footer. */
#define READ_NUM_Y (CARD_FLOOR - 8 - READ_NUM_H)
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
 * programmer cover the whole left column down to the card's floor.  The
 * output card -- CENTER, RELEASE and the readings -- and the footer's ARM
 * stay where they are and work.
 */
#define OV_X      PAD             /**< The left column's panels, px. */
#define OV_Y      PAD             /**< Their top, px. */
#define OV_W      LCARD_W         /**< Their width, px. */
#define OV_H      (CARD_FLOOR - OV_Y) /**< Their height, px: to the floor. */
/*
 * The pages of rows, in the page area: a run's settings, one level down
 * from its card on TEST or LIMIT RUN, and KST's EXPERT MODE, under the
 * page's name: three columns, each row its label over its value, so the
 * automatic test's 15 rows fit in 5 rows of UI_TAP_MIN.
 */
#define OV_ROW_H  UI_TAP_MIN      /**< A row's value's height, px. */
#define OV_COL_W  ((OV_W - 30) / 2) /**< A column of the choice list: 295px. */
#define NOTE_PITCH 21             /**< A note's line pitch, px. */
#define RL_ROW0   (CARD_Y + 56)   /**< A run's first row, px. */
#define RL_PITCH  70              /**< From row to row: label and value. */
#define RL_LBL_H  21              /**< A run's row's label over its value. */
#define RL_COL_W  ((OV_W - 40) / 3) /**< A column of a run's: 193px. */
#define RL_ROWS   5               /**< The rows a column holds. */
/** @brief A run's note under its rows: the automatic test's stall note,
 *         4px under the last value. */
#define RL_NOTE_Y (RL_ROW0 + RL_ROWS * RL_PITCH + 2)
_Static_assert(RL_NOTE_Y + 20 <= CARD_FLOOR,
               "a run's note ends above the footer");

/*
 * A pulse needs a pause before the next frame.  STANDARD PWM, WIDE and
 * NARROW 760 keep one of at least 1ms -- a 100% margin on the 1ms a
 * standard servo's own decoder needs -- so their fastest frame rate is
 * 1 / (longest pulse + 1ms): 333Hz for 2000us.  The heli profiles run
 * at their published rates, which leave less, so theirs is set by the
 * profile and a pause of at least 0.5ms.
 */
#define PWM_MIN_PAUSE_US  1000u
#define HELI_MIN_PAUSE_US 500u
/* Above this a frame rate needs the warning: an analogue servo overheats. */
#define SAFE_RATE_HZ      60u

/* A profile the bench drives; k_types in servo_screen.c holds them. */
typedef struct {
    const char     *name;
    uint16_t        min_us;      /**< PULSE MIN, us                       */
    uint16_t        center_us;   /**< PULSE CENTER, us                    */
    uint16_t        max_us;      /**< PULSE MAX, us                       */
    const uint16_t *rates;       /* the frame rates offered, Hz           */
    uint8_t         rate_count;
    uint16_t        default_hz;
    uint16_t        max_hz;      /* the profile's ceiling; 0 is the pause */
    bool            heli;        /* choosing it needs the warning          */
} servo_type_t;

/* The profiles, in servo_screen.c. */
#define TYPE_COUNT 5
extern const servo_type_t k_types[TYPE_COUNT];

/* The rows of the servo profile, which the hub's SETTINGS list edits
 * (PG_OUTPUT, no page of its own) and KST's EXPERT MODE, from the
 * footer's SETTINGS on KST.  TEST's and LIMIT RUN's settings are lists
 * (servo_test_list.c, servo_lists.c). */
enum { PG_OUTPUT = 0, PG_COUNT, PG_KST = PG_COUNT };

/* Which page of rows shows, one level down from its screen: KST's EXPERT
 * MODE.  The numbers are the ones servo_screen_run_settings() hands out. */
enum { RL_NONE = 0, RL_KST = 1 };

/* What a settings row edits. */
enum { R_TYPE = 0, R_RATE, R_MIN, R_CENTER, R_MAX, R_TRIM, R_TRAVEL,
       R_REVERSE, R_SETTING, R_TEXT, R_HV, R_ENC_CENTER, R_KST_EXPERT };

typedef struct {
    uint8_t      page;
    uint8_t      kind;
    setting_id_t id;          /* R_SETTING                              */
    ui_text_id_t label;
    uint8_t      col, row;
    bool         wide;        /* the value takes both columns' width    */
} ov_row_t;

/* What a press is on, in the pages and the panels they open. */
enum { OP_NONE = 0, OP_ROW, OP_TRIM_DN, OP_TRIM_UP, OP_KEYPAD,
       OP_TEXT, OP_CHOICE, OP_CHOICE_CANCEL, OP_WARN_APPLY, OP_WARN_CANCEL,
       OP_ASK_APPLY, OP_ASK_CANCEL, OP_TEST_START,
       OP_LIM_START, OP_RUN_CLOSE, OP_OPEN_CAPS,
       OP_KST_EXPERT };

/* What the list chooses -- the PORT button's port, the programmer family
 * (servo_pick.c) and the angle source (servo_angle_pick.c) among them --
 * and what the keypad types. */
enum { CH_NONE = 0, CH_TYPE, CH_RATE, CH_ENUM, CH_PORT, CH_FAMILY,
       CH_SERIES, CH_ANGLE, CH_ANGLE_FIX };
enum { KT_NONE = 0, KT_MIN, KT_CENTER, KT_MAX, KT_TRAVEL, KT_RATE,
       KT_SETTING, KT_TRIM };

/* What the question or the HV warning applies: the supply's output
 * switched on, the automatic test started with steps above 6.0V, or the
 * operator's set points back onto the live output a programming port left
 * on (both set points). */
enum { ASK_ON = 0, ASK_TEST, ASK_RESTORE };

#define CHOICE_MAX 10

/* What a command means as a pulse and an angle: the profile's pulses, trim,
 * reverse and travel, as they were when a command was sent. */
typedef struct {
    uint16_t min_us;     /**< PULSE MIN, us    */
    uint16_t center_us;  /**< PULSE CENTER, us */
    uint16_t max_us;     /**< PULSE MAX, us    */
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

/**
 * @brief The drag's time constant, ms: the dial's target follows the finger
 *        through a one-pole filter this slow.
 *
 * The GT911 is read every 10.5ms and the panel draws every 16.6ms, so a
 * finger moving at a steady 90deg a second reaches the screen as 0.5
 * to 2.1deg a frame where 1.5 is right; the servo is sent the same
 * beat.  20ms is about one touch read and one frame: the steps even out,
 * and the command lags a steady drag by 20ms, 1.8deg at 90deg
 * a second.
 */
#define SV_DRAG_TAU_MS 20.0f

/** @brief How close the filtered target comes to the finger's before it
 *         is set to it, degrees: well under a pulse's microsecond. */
#define SV_DRAG_SNAP_DEG 0.01f

/**
 * @brief The drag's filtered target (svs_drag_tick()).
 *
 * On from the press on the dial while the value is live, through the drag
 * and after the finger lifts until the target has reached the finger's
 * last angle.  Any other command, a stop, a disarm or a value that is no
 * longer live ends it at once: it never outlasts an arm cycle.
 */
typedef struct {
    bool  on;      /**< the filter commands the dial               */
    bool  posting; /**< the filter's own post is under way          */
    bool  restating; /**< reissue() says the held position again    */
    float goal;    /**< the finger's angle, held to TRAVEL, degrees  */
    float out;     /**< the filtered target last commanded, degrees  */
} svs_drag_filter_t;

/* The screen's state, the pages' with it. */
/** @brief The outboxes servo_screen_test_peek() hands out, in its order. */
enum { PEEK_TEST = 0, PEEK_LIM, PEEK_RAUTO };

typedef struct {
    /* The profile in force, for this session only: STANDARD PWM at 50Hz
     * at every restart. */
    int      type;
    uint16_t min_us;     /**< PULSE MIN, us    */
    uint16_t center_us;  /**< PULSE CENTER, us */
    uint16_t max_us;     /**< PULSE MAX, us    */
    uint16_t frame_hz;
    bool     reverse;
    int16_t  trim_us;      /**< added to center                        */
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
    bool     enc_exact;    /**< enc_deg holds the IMU's degrees as read   */
    float    enc_deg;      /**< those degrees, from count 0               */
    int      shown_q_enc;  /**< the angle as drawn, in hundredths         */

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

    gfx_rect_t arm_btn;          /**< ARM / DISARM                     */
    gfx_rect_t center_btn;       /**< CENTER; empty off LIVE           */
    gfx_rect_t sweep_btn;        /**< SWEEP                            */
    gfx_rect_t release_btn;      /**< RELEASE                          */
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
    /** @brief The programming ports' own 5V on a board that switches it
     *  (the rbmod carrier): the PROG 5V box in the power plot's place
     *  (prog5v_box.h). */
    prog5v_box_t p5;
    /* The question or the HV warning waiting for its answer: OUTPUT ON
     * with a set point past a standard servo's rating (ASK_ON) and TEST's
     * steps past it (ASK_TEST), each on the HV warning's two-second hold
     * (hv), the hold the profile warning takes; or the restore question,
     * YES or NO, and the HV warning after a YES past that rating
     * (ASK_RESTORE). */
    struct {
        bool      open;
        bool      hv;
        int       purpose;   /**< ASK_ON, ASK_TEST or ASK_RESTORE      */
        uint32_t  off_count; /**< supply_screen_off_count() at asking  */
        uint32_t  stops;     /**< the screen's stop count at asking     */
        uint32_t  off_asks;  /**< supply_screen_off_asks() at asking    */
        float     v, i;
        ui_hold_t hold;
        bool      down;
        uint32_t  rev;
    } ask;
    uint32_t drawn_ask[2];
    /* Stops seen (servo_screen_cancel_arm()), for the question above. */
    uint32_t stops;

    /**
     * @brief The function shown (servo_fn_t): the hub, or the function
     *        screen entered last.
     *
     * The task under it (ui_workbench.h's strip, never drawn) is the one
     * the function's panels draw on: LIVE for LIVE and SWEEP, TEST for TEST
     * and LIMIT RUN, RUDDER for RUDDER and RUDDER AUTO, PROGRAM for KST and
     * HITEC.
     */
    uint8_t      fn;
    ui_wb_strip_t wb;
    uint8_t      run_list;      /**< RL_*: a page of rows open */
    /* Whether a panel or the programmer covered the left column when the
     * buffer was last drawn: a change repaints it whole. */
    bool         drawn_cover[2];
    /* The footer's and the PORT button's press, and the hub's tiles':
     * FP_*, the contact, and a count of their look. */
    uint8_t      fp_pressed;    /**< FP_*: what is pressed */
    uint8_t      fp_id;         /**< the contact pressing it */
    int16_t      fp_tile;       /**< the tile pressed, -1 for none */
    uint32_t     fp_rev;        /**< a count of the presses' look */
    uint32_t     drawn_fp[2];   /**< foot_sig() as last drawn, per buffer */
    /* The SETTINGS list open on the hub or a function screen
     * (ui_setlist.h), and its count of changes. */
    ui_setlist_t sl;            /**< the SETTINGS list (ui_setlist.h) */
    uint32_t     sl_rev;        /**< a count of the list's changes */
    uint32_t     drawn_sl[2];   /**< sl_rev as last drawn, per buffer */
    uint32_t     drawn_hub[2];  /**< what the hub drew last, per buffer */
    /* The panels over the left column. */
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
        char  labels[CHOICE_MAX][64]; /**< each choice's text */
        int   values[CHOICE_MAX];
        int   count;
        int   pressed;
        /** @brief Bit k: choice k is refused, its label the reason. */
        uint16_t refused;
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
    uint16_t           rate_hz;   /**< the rate asked for, Hz */

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
     * that sets a new origin: stop_sweep() -- a drag, CENTER, RELEASE, a
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
    bool      test_hv;       /**< HV SERVO: 7.4 and 8.4V run           */
    ui_hold_t test_hold;
    bool      test_down;
    uint32_t  test_rev;
    uint32_t  drawn_test[2];
    /*
     * DUT's KST EXPERT MODE row (SET_KST_EXPERT): ON after a 2s hold, OFF
     * with a tap.  kx_down while a hold of it runs; kx_rev counts what its
     * value shows, drawn_kx what each buffer has of it.
     */
    ui_hold_t kx_hold;       /**< the row's 2s hold to ON           */
    bool      kx_down;       /**< that hold runs                     */
    uint32_t  kx_rev;        /**< counts what the row's value shows  */
    uint32_t  drawn_kx[2];   /**< kx_rev as each buffer drew it      */
    uint32_t  now_ms;        /**< the panel's clock, servo_screen_clock() */
    bool      have_now;
    bool      link_up;
    bool      enc_held;      /**< IOmcu holds the encoder on (set_enc_held) */
    uint8_t   imu_axis;      /**< the IMU's AXIS, 0 X (set_imu_axis)   */
    bool      imu_neg;       /**< its SIGN negative                     */
    uint32_t  imu_zero_gen;  /**< the IMU's zero generation (set_imu_zero) */
    uint32_t  imu_settle_ms; /**< the IMU filter's settle, ms (set_imu_settle) */
    int       test_note;     /**< servo_str_t the engine refused a START
                                  with, 0 none; test_why() is live      */
    int       test_file;     /**< its files' number; 0 not yet, -1 none */
    bool      test_report;   /**< the card took its .TXT whole          */
    bool      test_off;      /**< USER LOGS off at its start: no file  */
    const char *test_blocked_seen; /**< test_why() at the last tick     */
    bool      test_online_seen;  /**< the supply answering, likewise    */
    bool      test_box;      /**< the left card shows the run          */
    bool      test_seen;     /**< the run was running when last looked */
    bool      test_on_ina;   /**< the run reads the INA3221's windows   */
    /*
     * ARM + START on TEST (servo_test_ready.c): the press on the footer's
     * hold is the overlay's (OP_TEST_START); the run's settings as they
     * read when the hold fired, which the run starts on after the steps.
     */
    servo_test_cfg_t test_snap;  /**< the settings at the hold's end    */
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
     * LIMIT RUN's ARM + START starts it; its left card shows it.  One of
     * the runs at a time.
     */
    servo_limrun_t lim;
    bool      lim_prog;      /**< LIMIT RUN: SEARCH+PROG, for the session */
    bool      lim_pulses;    /**< SERVO PULSES at 1000-2000, session */
    ui_hold_t lim_hold;      /**< ARM + START on LIMIT RUN              */
    bool      lim_down;      /**< a finger on it, the hold not fired    */
    /** @brief The run the hold fired: the settings as they read then, the
     *  mode and SERVO PULSES as chosen (lim_start() judges them). */
    servo_limrun_cfg_t lim_cfg;
    bool      lim_seen;      /**< it was running when last looked       */
    bool      lim_safe_due;  /**< it ended: SAFE once its last command
                                  is taken (lim_safe_follow())           */
    /** The run ended in a session and its LEAVE is on its way: the box,
     *  not the programmer, stays on the left card, and the programmer's
     *  run ends once the LEAVE has (lim_end_due). */
    bool      lim_leaving;
    bool      lim_end_due;
    /** @brief It ended as FAIL: the supplies go off once the programmer's run
     *  has ended (lim_seam_ended()), after a LEAVE on its way. */
    bool      lim_off_due;
    int       lim_file;      /**< its report's number; 0 not yet, -1 none */
    bool      lim_report;    /**< the card took the report whole       */
    bool      lim_off;       /**< USER LOGS was off at its start        */
    const char *lim_note;    /**< why the last START was refused, or NULL */
    uint32_t  lim_changes;   /**< the meter's change count at the start */
    uint32_t  lim_sig;       /**< lim_signature() as last drawn         */
    /* The outbox the logger last peeked, PEEK_*, and whose files' number
     * comes back (servo_screen_test_files()). */
    uint8_t   peek_from;     /**< the outbox last peeked, PEEK_*     */
    servo_test_out_t peek_kind;
    uint8_t   files_from;    /**< the outbox of the last OPEN taken  */
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
    /**
     * @brief The dial's own count of changes: the horn's angle, the
     * commanded angle and the MEASURED reading, which move every frame of a
     * sweep or a drag.  On LIVE with nothing over the card a change repaints the
     * horns' boxes, as the buffer drew them and as they are now, and the
     * numerals that follow them -- not the two cards (svs_render()).
     */
    uint32_t dial_rev;
    uint32_t drawn_dial[2];  /**< dial_rev as each buffer drew it        */
    float    drawn_deg[2];   /**< the horn's angle as each buffer drew it */
    float    drawn_cmd[2];   /**< the commanded one behind it            */
    bool     drawn_ghost[2]; /**< and whether that one was drawn         */
    svs_drag_filter_t df;    /**< the drag's filtered target, svs_drag_tick() */
    rudder_t rud;            /**< RUDDER's task (servo_rudder_int.h)     */
    rauto_ui_t ra;           /**< RUDDER AUTO (servo_rudder_auto_int.h) */
    svs_ready_t rdy;         /**< READY and ARM + START (servo_ready_int.h) */
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
/** @brief DISARM asked of an armed bench: the value leaves LIVE now.  Also
 *         the automatic test's end on an error (servo_test_abort_error()). */
void svs_ask_disarm(void);
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
 * @return the digits of its step (settings_decimals()): 2 for 0.05, 1
 *         for 0.1, 0 for a whole step
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
/**
 * @brief The question after a programming port let go of a live output at
 *        its own set points (supply_screen_restore_offer()): asked once no
 *        panel, press or OUTPUT ON hold stands, none inside a limit run,
 *        and closed, the HV warning after YES included, when a port holds
 *        the supply again or the output is no longer live.  Every frame.
 */
void ask_restore_follow(void);
/* A press on a page or a panel: its DOWN, and its moves and its release. */
void ov_down(const touch_event_t *evt);
void ov_rest(const touch_event_t *evt);
/**
 * @brief Row @p r's value as text.
 * @param r the row @param buf takes the text @param n its size
 */
void ov_row_value(const ov_row_t *r, char *buf, size_t n);
/**
 * @brief Whether the frame rate reached the pins, in two lines.
 * @param l1 takes the first @param l2 the second @param n each one's size
 * @return true in the warning colour
 */
bool svs_rate_note(char *l1, char *l2, size_t n);
/**
 * @brief The servo profile's row @p kind's editor opened over the left
 *        column, for the hub's list: the list, the keypad or the profile
 *        warning.
 * @param kind R_TYPE to R_REVERSE, or R_HV
 */
void svs_profile_edit(int kind);
/** @brief The choice list opened over the left column, empty, for
 *  @p target (CH_*), @p id with CH_ENUM, under @p title.
 *  @param target what it chooses @param id the setting @param title its
 *  title */
void open_choice(int target, setting_id_t id, const char *title);
/** @brief Choice @p value added under @p label, CHOICE_MAX at most.
 *  @param label its text @param value what it takes */
void choice_add(const char *label, int value);
/**
 * @brief One step of profile row @p kind: 10us for a pulse, 5us for TRIM,
 *        5° for TRAVEL, a switch for REVERSE and HV SERVO.
 * @param kind R_TYPE to R_REVERSE, or R_HV
 * @param dir +1 up, -1 down
 */
void svs_profile_step(int kind, int dir);
/**
 * @brief Profile row @p kind's value as text.
 * @param kind R_TYPE to R_REVERSE, or R_HV
 * @param buf takes it
 * @param n its size
 */
void svs_profile_value(int kind, char *buf, size_t n);
/* The panel open over the left column, and the parts a frame repaints by
 * themselves. */
void draw_overlay(gfx_canvas_t *c);
void draw_save_line(gfx_canvas_t *c);
void draw_warn_apply(gfx_canvas_t *c);
void draw_ask_apply(gfx_canvas_t *c);
/**
 * @brief The KST EXPERT MODE row's hold, each frame: SET_KST_EXPERT ON
 *        and saved when it completes, the hold dropped with its press.
 * @param dt_s the frame's time, s
 */
void kx_tick(float dt_s);
/**
 * @brief The KST EXPERT MODE row's value on DUT, the part a hold repaints.
 * @return the value's rectangle
 */
gfx_rect_t kx_value_rect(void);
/**
 * @brief Draws the KST EXPERT MODE row's value: ON in the danger colour,
 *        OFF sunk, the fill fading towards ON while its hold runs.
 * @param c the canvas
 */
void draw_kx_value(gfx_canvas_t *c);


/* --- servo_test_panel.c's side, which the other files call -------------- */

/* The restore's stages: the OFF the run's end posted not yet taken; taken,
 * waiting for the next frame; waiting for a sample taken since; a sample
 * showed the output off. */
enum { RS_OFF_POSTED = 0, RS_OFF_SENT, RS_SAMPLE, RS_OFF_SEEN };

/* The automatic test. */
/** @brief Why a run cannot start now, the first check of TEST's gate
 *         (servo_test_ready.c) that is not met.  @return the text; NULL
 *         when it may */
const char *test_why(void);
float test_top_v(void);
/** @brief The steps the run's settings name, 7.4 and 8.4V only with HV
 *         SERVO on.  @param v receives their voltages; may be NULL
 *  @return their count */
uint8_t test_steps(float *v);
/**
 * @brief The steps, short: `4.8-6.0V, BROWN-OUT`, `4.8V`, `BROWN-OUT`.
 * @param b where it goes
 * @param n its size
 * @param v the steps' voltages
 * @param count how many
 * @param bo whether the brown-out walk follows
 */
void test_steps_text(char *b, size_t n, const float *v, uint8_t count,
                     bool bo);
/** @brief The run's ends: SWEEP's RANGE of the travel either side of
 *         PULSE CENTER.  @param end_lo receives the low end, us
 *  @param end_hi receives the high end, us */
void test_ends(uint16_t *end_lo, uint16_t *end_hi);
/** @brief Whether a run started now reads the INA3221.  @return true
 *         while it is the rail's meter of a measured supply */
bool test_on_ina_now(void);
/**
 * @brief The run's configuration: the settings part, as the hold reads
 *        them when it fires, then the bench part, read again as the run
 *        starts: the meter, the angle source, the firmware and the
 *        report's language.
 * @param c receives it
 */
void test_cfg(servo_test_cfg_t *c);
/** @brief The bench part of test_cfg() alone, into @p c.  @param c the
 *         configuration */
void test_cfg_bench(servo_test_cfg_t *c);
/**
 * @brief The run started on @p c, ARMED: SUPPLY's set points kept for the
 *        restore, the box shown.  A refusal of the engine is kept in
 *        s.test_note.
 * @param c the configuration
 * @return false when the engine refused it
 */
bool test_launch(const servo_test_cfg_t *c);
void test_end_now(servo_test_abort_t why);
/** @brief HV SERVOS ONLY opened for a run with a step past 6.0V, naming
 *         the highest: its 2s hold arms and starts (test_hold_fired()). */
void ask_hv_test(void);
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
/**
 * @brief Why a run on the programming port writes nothing to the servo,
 *        neither SEARCH+PROG nor SERVO PULSES: without a PD mini the run
 *        cannot switch the servo off and on, and a written value acts only
 *        after that.
 * @return the line that says so, or NULL where the run may write
 */
const char *lim_write_why(void);
/**
 * @brief Why the target's pulse is off: a programmer session holding the
 *        port, PORT PWM off, or PORT PWM on and the pulse waiting.  A pulse
 *        comes on only on a bench at rest (servo_link_prog_service()); the
 *        carrier's panel asks for it only with the PROG 5V on, so a PROG
 *        5V switched on while ARMED leaves it off until a DISARM.
 * @return OUT_PORT_HELD's line while a programmer holds the port;
 *         with PORT PWM on, SERVO_STR_START_PROG_5V's while the carrier's
 *         PROG 5V box is off, else SV_LIM_PWM_SAFE's; with PORT PWM off,
 *         SV_LIM_PWM_OFF_ARMED's while ARMED (TARGET is locked), else
 *         SV_LIM_PWM_OFF's: TARGET opens the OUTPUTS entry that switches it
 */
const char *lim_pwm_why(void);
/**
 * @brief The run drives the rbmod carrier's programming port, fed from the
 *        PROG 5V box: a PD mini on the servo rails is not its supply, and
 *        the run neither asks nor watches it.
 * @return true on the carrier with a port as the drive's target
 */
bool lim_fixed_supply(void);
/**
 * @brief Why a limit run cannot start now, before any hold: the first of
 *        the gate's checks that is not met (servo_limit_ready.c).
 * @return the text; NULL when every check is met
 */
const char *lim_blocked(void);
/**
 * @brief Why the INA3221 is not the meter a run may start on.
 * @return the line, or NULL
 */
const char *lim_meter_why(void);
/**
 * @brief A PD mini feeds the servo a run reads: one answers, and the port
 *        is not the rbmod carrier's.
 * @return true when it does
 */
bool lim_pd_feeds(void);
void lim_end_now(servo_test_abort_t why);
/* ARM + START's hold completed; the run every frame; the ended run's
 * LEAVE, whichever screen is up; the meter changed under the run. */
void lim_begin(void);
void lim_tick(void);
void lim_leave_follow(void);
void lim_meter_follow(servo_source_id_t id, uint32_t changes);
/**
 * @brief A DOWN on LIMIT RUN's ARM + START: STOP RUN and a sequence under
 *        way stop on it, else the hold begins where no row refuses it.
 * @param evt the DOWN
 * @return whether it landed on the hold
 */
bool lim_hold_down(const touch_event_t *evt);
/* What the left card shows of the run, as one number. */
uint32_t lim_signature(void);

/* --- servo_limit_card.c: LIMIT RUN's left card and hold ------------------ */

/** @brief LIMIT RUN's left card whole.  @param c the canvas */
void lim_render(gfx_canvas_t *c);
/** @brief ARM + START, the step under way, or STOP RUN in the footer.
 *  @param c the canvas */
void lim_draw_hold(gfx_canvas_t *c);
/** @brief LOW END and HIGH END together.  @return their rectangle */
gfx_rect_t lim_ends_rect(void);
/** @brief The state lines' place.  @return its rectangle */
gfx_rect_t lim_lines_rect(void);

/* --- servo_limit_seam.c: the programmer a run's sessions go to ----------- */

/**
 * @brief The family a run started now takes on the screen in front: RUDDER
 *        AUTO's own SERVO (SET_RAUTO_FAMILY) on RUDDER AUTO, LIMIT RUN's
 *        (SET_LIMIT_FAMILY) elsewhere.  servo_pick.c.
 * @return servo_limrun_family_t
 */
uint8_t lim_family(void);
/** @brief The family of the run under way or last run, else lim_family().
 *  @return servo_limrun_family_t */
uint8_t lim_family_now(void);
/* Each lim_seam_*() call names the family of the run it serves
 * (servo_limrun_family_t): KST/CHASERVO goes to kst_panel.h's run seam,
 * either Hitec one to hitec_panel_run.h's. */
/** @brief The programmer can take a run's CONNECT.
 *  @param fam the family
 *  @return true when its port is in PWM with nothing under way */
bool lim_seam_ready(uint8_t fam);
/** @brief A run with a read begins on its family's programmer.
 *  @param fam the family */
void lim_seam_begin(uint8_t fam);
/** @brief CONNECT on the run's programmer, its session's supply @p mv and
 *         @p ma whether or not a PD mini feeds the servo.
 *  @param fam the family
 *  @param mv the programming voltage, mV: SERVO_LIMRUN_SUPPLY_MV
 *  @param ma the current limit, mA
 *  @return true when it started */
bool lim_seam_connect(uint8_t fam, uint16_t mv, uint16_t ma);

/* --- servo_run_supply.c: the runs' 5.0V ---------------------------------- */

/** @brief Whether a PD mini feeds the servo for the run: one answers, on
 *         or off on a rail that reads under 3.0V (svs_seq_need()).
 *  @return true then; false on the carrier's PROG 5V or another feed */
bool rsup_pd_used(void);
/** @brief Why the PD mini feeding the servo cannot take the run's 5.0V:
 *         SUPPLY's caps leave it out.  @return the reason, NULL with no
 *         PD mini or one that takes it */
const char *rsup_why(void);
/** @brief START: the PD mini feeding the servo to 5.0V, its current limit
 *         kept; it stays there after the run.  @return false when SUPPLY
 *         did not take 5.0V; true with no PD mini */
bool rsup_begin(void);
/** @brief The start sequence's read-back of the PD mini: on LIMIT RUN and
 *         RUDDER AUTO its reading must hold the run's 5.0V as well as the
 *         output on.
 *  @param set_v the set point the PD mini's reading holds, V
 *  @return true where it does, and on every other SERVO function */
bool rsup_read_back(float set_v);
/** @brief A run session's current limit: the PD mini's set point where it
 *         feeds the servo, else SET_SERVO_PROG_I.  @return mA */
uint16_t rsup_ma(void);
/** @brief The run's supply as the READY cards show it, `5.0V`.
 *  @param b where it goes @param n its size */
void rsup_text(char *b, size_t n);
/** @brief The two travel values, in the family's units.
 *  @param fam the family
 *  @param v LEFT and RIGHT
 *  @return true when the write started */
bool lim_seam_write(uint8_t fam, const uint16_t v[2]);
/** @brief KST's PULSE LOWER and PULSE UPPER, 0.1us.
 *  @param fam the family
 *  @param p the two pulses
 *  @return true when the write started; false for a Hitec family */
bool lim_seam_pulses(uint8_t fam, const uint16_t p[2]);
/** @brief LEAVE on the run's programmer.
 *  @param fam the family
 *  @return true when it started */
bool lim_seam_leave(uint8_t fam);
/** @brief The run's programmer is still leaving.
 *  @param fam the family
 *  @return true while its LEAVE runs */
bool lim_seam_leaving(uint8_t fam);
/** @brief The step under way, polled.
 *  @param fam the family
 *  @param ok receives whether it ended well
 *  @return true once it has ended, false while none runs or it runs */
bool lim_seam_poll(uint8_t fam, bool *ok);
/** @brief The CONNECT found no servo answering.
 *  @param fam the family
 *  @return true for none */
bool lim_seam_no_servo(uint8_t fam);
/** @brief The servo the programmer holds, as the limit run takes it:
 *         travel values, the hard rules' range, and the pulses (a Hitec
 *         servo the family's model, its DIRECTION CCW as REVERSION).
 *  @param fam the family
 *  @param sv receives it
 *  @return sv->ok: for a Hitec family, a servo of its series */
bool lim_seam_servo(uint8_t fam, servo_limrun_servo_t *sv);
/** @brief The run's backup file.
 *  @param fam the family
 *  @return its name, "" for none */
const char *lim_seam_backup(uint8_t fam);
/** @brief The file that holds the servo as a run found it, for a run that
 *         ended with a write on its way: the run's backup, or with none
 *         handed over yet the session's own file saved before that write.
 *  @param fam the family
 *  @return its name, "" or NULL for none */
const char *lim_seam_backup_kept(uint8_t fam);
/** @brief The run is over on its programmer.
 *  @param fam the family */
void lim_seam_end(uint8_t fam);
/** @brief A programmer page open and not holding its port closes as a run
 *         starts. */
void lim_seam_close_pages(void);

/* --- helpers the four files inline --------------------------------------- */

static inline const servo_type_t *type(void) { return &k_types[s.type]; }

/*
 * The range a command carries: the narrowest one centered on CENTER that
 * holds MIN and MAX.  The IOmcu knows a channel only by its two
 * endpoints and rests a surface at their midpoint -- on RELEASE, before an
 * arm, after a silence -- so MIN..MAX itself would rest a servo whose CENTER
 * is not midway between them away from its center.  Commands never leave
 * MIN..MAX: svs_deg_to_us() clamps them.  The PULSE keypads keep this
 * range inside OUT_FLOOR_US..OUT_CEILING_US.
 */
static inline void cmd_range(uint16_t *lo, uint16_t *hi)
{
    const unsigned below = (unsigned)s.center_us - (unsigned)s.min_us;
    const unsigned above = (unsigned)s.max_us - (unsigned)s.center_us;
    const unsigned half  = (below > above) ? below : above;
    *lo = (uint16_t)((unsigned)s.center_us - half);
    *hi = (uint16_t)((unsigned)s.center_us + half);
}

/*
 * The longest pulse the far end can render: the top of the range a command
 * carries, which is past PULSE MAX when CENTER is off the middle.  A pulse
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

/** @brief The angle source in force: SETUP > SENSORS > IMU's ANGLE SOURCE.
 *  @return SERVO_ANGLE_IMU or SERVO_ANGLE_AS5600. */
static inline servo_angle_src_t enc_src(void)
{
    return (settings_get_int(SET_ANGLE_SOURCE) == (int)SERVO_ANGLE_IMU)
               ? SERVO_ANGLE_IMU : SERVO_ANGLE_AS5600;
}

/* Whether the encoder's last reading is on show and may be used: one the
 * link carried, with the encoder on in SETUP; or the IMU's, which the
 * panel hands in valid only while it is on and connected.  A link that
 * goes down takes the reading with it (servo_screen_set_link()). */
static inline bool enc_shown(void)
{
    return s.enc_valid && s.link_up
           && (enc_src() == SERVO_ANGLE_IMU || settings_get_bool(SET_ENC_EN));
}

/** @brief Whether a run started now reads angles: the AS5600 on in SETUP
 *         and held on by the IOmcu, whose set-up is written only while
 *         SAFE; the IMU with a reading on show.
 *  @return true when the run's cfg takes enc_on. */
static inline bool enc_on_now(void)
{
    return (enc_src() == SERVO_ANGLE_IMU)
               ? enc_shown()
               : (settings_get_bool(SET_ENC_EN) && s.enc_held);
}

/**
 * @brief The source's name for a run's report and CSV: "AS5600", "IMU X+".
 * @param buf Receives the name.
 * @param n   The size of @p buf.
 * @return @p buf.
 */
static inline const char *enc_name(char *buf, size_t n)
{
    return servo_angle_name(enc_src(), s.imu_axis, s.imu_neg, buf, n);
}

static inline gfx_rect_t overlay_area(void)
{
    return (gfx_rect_t){ OV_X, OV_Y, OV_W, OV_H };
}

/** @brief The bottom row of a warning or the question: its buttons' top,
 *         so they end 8px inside the left column, over the footer. */
#define WARN_BTN_Y (CARD_FLOOR - 8 - 56)

/**
 * @brief HOLD TO APPLY, or APPLY, bottom left of a warning or the question.
 * @return the button, 240 x 56px
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
 *        CLOSE after.  Its right edge stays 36px left of the shaft, so the
 *        horn, its boss and its rings show at every angle while the box is
 *        up; it covers the case's left half.
 */
#define BOX_W     346
#define BOX_CELLS ((BOX_W - 20) / SV_CELL) /**< A line of the box: 32. */
#define BOX_PITCH 24                       /**< Its line pitch, px. */
#define BOX_BTN_DY 112                     /**< Its button's top, in it. */

/** @brief The run's box on TEST, under the caps button, in the screen's
 *         coordinates. */
#define BOX_Y_TEST (CARD_Y + 196)
#define BOX_H      (BOX_BTN_DY + UI_TAP_MIN + 10) /**< Its height: 170px. */
_Static_assert(BOX_Y_TEST + BOX_H <= CARD_FLOOR,
               "the run's box ends above the footer");

/** @brief The run's box. @return the box, in the screen's coordinates */
static inline gfx_rect_t test_box_rect(void)
{
    return (gfx_rect_t){ PAD + 10, (int16_t)BOX_Y_TEST, BOX_W, BOX_H };
}

/** @brief The box's button. @return STOP TEST or CLOSE, 150 x 48px */
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

/**
 * @brief The SERVO row reads HITEC: a run started now goes to the Hitec
 *        programmer.
 * @return true for a Hitec family
 */
static inline bool lim_hitec_now(void)
{
    return lim_family() != 0u;
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

/** @brief The count the source's degrees are taken from: AS5600 center for
 *         the AS5600, 0 for the IMU, whose zero is taken before its counts.
 *  @return the center count, 0 to 4095. */
static inline uint16_t enc_center(void)
{
    return servo_angle_center(enc_src(),
                              (uint16_t)settings_get_int(SET_ENC_CENTER));
}

/**
 * @brief Whether the last reading's degrees are the IMU's as read: finer
 *        than the 0.088° of its counts.  Its center is count 0, so they are
 *        from the center as they are.
 * @return Whether they are, with ANGLE SOURCE IMU.
 */
static inline bool enc_exact(void)
{
    return s.enc_exact && enc_src() == SERVO_ANGLE_IMU;
}

/**
 * @brief The source's angle from its count 0, in its own sign.
 * @return degrees, -180 up to 180
 */
static inline float enc_abs_now(void)
{
    return enc_exact() ? s.enc_deg : servo_test_enc_deg(s.enc_raw, 0u);
}

/* The encoder's angle from its center, degrees, in the direction of the
 * commanded angle: negated with REVERSE on, as us_to_deg() is. */
static inline float enc_deg_now(void)
{
    const float d = enc_exact() ? s.enc_deg
                                : servo_test_enc_deg(s.enc_raw, enc_center());
    return s.reverse ? -d : d;
}

/**
 * @brief An angle of the source as the readouts show it: the IMU's in
 *        hundredths (imu_deg_text()), the AS5600's in tenths.
 * @param buf  Takes the text.
 * @param n    Its size, bytes.
 * @param deg  The angle, °.
 * @param plus A '+' before a value that is not negative.
 * @return @p buf.
 */
const char *svs_angle_text(char *buf, size_t n, float deg, bool plus);

/**
 * @brief Whether a finger is on RUDDER AUTO's or LIMIT RUN's ARM + START
 *        before it fires, or its sequence is under way: the output it arms
 *        is the one shown when the hold began.
 * @return true then
 */
static inline bool svs_start_pending(void)
{
    return s.ra.down || s.lim_down || svs_seq_busy();
}

/**
 * @brief Whether a finger is on a hold that has not fired: OUTPUT ON,
 *        PROG 5V ON, ARM, a test's or a limit run's, RUDDER AUTO's ARM +
 *        START.  An overlay that takes every event opens on none of them:
 *        it would take the hold's release, and the hold would fire without
 *        its finger.
 * @return true then
 */
static inline bool svs_hold_down(void)
{
    return s.out_down || s.arm_down || s.test_down || s.lim_down
           || s.ra.down;
}

/* Armed, asked to arm, or a finger on ARM, or the bench armed for the
 * motor: the programmer takes no port. */
static inline bool bench_arming(void)
{
    return s.gate.armed || s.gate.arm_asked || s.arm_down || s.bench_armed;
}

/* --- the hooks the hub and the function screens share (servo_fn.c) ----- */

/* servo_screen.c's: the state at its start, a screen entered and left,
 * touch events lost, a frame, an event. */
void svs_reset(void);
/** @brief A SERVO screen entered: the hub's or a function's. */
void svs_enter(void);
/** @brief A SERVO screen left: DISARM posted, the panels closed. */
void svs_leave(void);
/** @brief Touch events lost: every press and drag ends. */
void svs_cancel(void);
/** @brief A frame of @p dt_s seconds. @param dt_s the frame */
void svs_tick(float dt_s);
/** @brief A touch event on a SERVO screen. @param evt the event */
void svs_event(const touch_event_t *evt);

/** @brief Whether KST or HITEC shows no session, panel or page any more:
 *         its own CLOSE closed it, and the screen goes to the hub.
 *  @return true when it does (servo_fn.c) */
bool svs_session_gone(void);

/** @brief The footer's and the PORT button's presses (FP_*). */
enum { FP_NONE = 0, FP_PORT, FP_SETTINGS, FP_TILE };

/* --- servo_test_list.c: TEST's SETTINGS list ------------------------------ */

/** @brief TEST's list rows: four headings and the 17 rows. */
#define SVS_TEST_ROWS 21
/** @brief TEST's rows (ui_setlist.h), in the list's order. */
extern const ui_setlist_row_t svs_test_rows[SVS_TEST_ROWS];
/** @brief Why a setting of TEST's list takes no press now.
 *  @param id the setting  @return the reason; NULL while it takes one */
const char *svs_test_locked(setting_id_t id);

/* --- servo_lists.c: the SETTINGS lists (ui_setlist.h) -------------------- */

/** @brief The list at its start: closed, the hub's host. */
void svs_lists_init(void);
/** @brief Whether the function shown has a SETTINGS list of its own, a page
 *         of rows or none.  @return true when SETTINGS takes a press */
bool svs_list_offered(void);
/** @brief Why SETTINGS takes no press now, NULL while it takes one: a
 *         press held on the screen, or svs_list_bench_why().
 *  @return the reason */
const char *svs_list_refused(void);
/** @brief Why no list or page of rows stays open: a session holding its
 *         port, or on a function screen a bench that is not SAFE, a run, a
 *         sweep.  @return the reason, NULL for none */
const char *svs_list_bench_why(void);
/** @brief Whether a finger holds a press anywhere on the screen: a hold,
 *         a tap's control, a panel's press, the dial, SPEED or a
 *         programmer's session.  @return true then */
bool svs_press_held(void);
/** @brief SETTINGS tapped: the function's list, or its page of rows. */
void svs_list_open(void);
/** @brief The list closed, nothing typed taken. */
void svs_list_close(void);
/** @brief Whether the list shows. @return true while open */
bool svs_list_shown(void);
/** @brief The home tag over the list: a keypad, a choice list or the leave
 *         warning closes; with nothing over it the list closes, through the
 *         leave warning over an unsaved change.  @return true when it
 *         took the tag */
bool svs_list_back(void);
/** @brief A frame of the list, @p dt_s seconds. @param dt_s the frame */
void svs_list_tick(float dt_s);

/* --- servo_pick.c: the programmer each run writes with ------------------ */

/** @brief The setting that holds function @p fn's SERVO: SET_RAUTO_FAMILY
 *         for RUDDER AUTO, else SET_LIMIT_FAMILY.
 *  @param fn the function  @return the setting */
setting_id_t svs_family_id(servo_fn_t fn);
/** @brief Function @p fn's SERVO as saved.
 *  @param fn the function  @return servo_limrun_family_t */
uint8_t svs_family_of(servo_fn_t fn);
/** @brief A family's name on the cards and lists: KST/CHASERVO, HITEC D
 *         SERIES or HITEC CLASSIC.
 *  @param fam servo_limrun_family_t  @return the name */
const char *svs_family_name(uint8_t fam);
/** @brief Why SERVO takes no pick now: svs_list_refused(), else no
 *         programming port.  @return the reason, NULL while it takes one */
const char *svs_family_refused(void);
/** @brief Whether the screen's list shows D RECEIVE: LIMIT RUN or RUDDER
 *         AUTO with its SERVO at HITEC D SERIES.  @return true then */
bool svs_family_d_receive(void);
/** @brief The READY card's SERVO tapped: the three families in the choice
 *         list, or the refusal in the band. */
void svs_family_open(void);
/** @brief A family picked: written to @p id and saved, unless refused.
 *  @param id SET_LIMIT_FAMILY or SET_RAUTO_FAMILY
 *  @param value servo_limrun_family_t */
void svs_family_pick(setting_id_t id, int value);
/** @brief The settings list's SERVO row: the screen's family, a tap opens
 *         the three as a choice list. */
extern const ui_setlist_custom_t svs_family_row;
/** @brief HITEC's READY card SERIES tapped: D SERIES and CLASSIC in the
 *         choice list, or the refusal in the band. */
void svs_series_open(void);
/** @brief A series picked on HITEC's card: SET_HITEC_SERIES written and
 *         saved, the page's options reloaded, unless refused.
 *  @param value hitec_family_t */
void svs_series_pick(int value);

/* --- servo_footer.c: the footer and the PORT button --------------------- */

/** @brief Function @p fn's name, in the footer and on its tile.
 *  @param fn the function @return the name */
const char *svs_fn_name(servo_fn_t fn);
/* ARM or DISARM in the footer's middle slot (servo_screen_draw.c). */
void draw_arm(gfx_canvas_t *c);
/** @brief The PORT button. @return its rectangle, in the body */
static inline gfx_rect_t port_rect(void)
{
    return (gfx_rect_t){ PORT_X, PORT_Y, PORT_W, PORT_H };
}
/**
 * @brief What the footer and the PORT button show, as one number: a change
 *        of it repaints them.
 * @return the signature
 */
uint32_t foot_sig(void);
/** @brief The footer's name, port line and SETTINGS, and the PORT button,
 *         on a function screen. @param c the canvas */
void draw_footer(gfx_canvas_t *c);
/** @brief Whether the hub names PROG 5V ON in its footer's middle slot: the
 *         ports' 5V reads ON while the hub shows.  @return true then */
bool svs_hub_p5_on(void);
/** @brief The PORT button: PORT and the port line. @param c the canvas */
void draw_port(gfx_canvas_t *c);
/** @brief The port line: what the tester drives, servo_target_line()'s
 *         words, `SERVO PWM GPIO 2` or `PROG PORT 1 GPIO 13`, `NO SERVO
 *         PWM PIN` with none.
 * @param out takes it @param n its size
 * @return true in the warning colour: a port, no pin, no link */
bool svs_port_line(char *out, size_t n);
/** @brief A DOWN on the PORT button or the footer's SETTINGS, and the rest
 *         of a press on either. @param evt the event
 *  @return whether it was theirs */
bool foot_down(const touch_event_t *evt);
/** @brief The rest of a press foot_down() took: a tap's UP opens the port
 *         list or the screen's SETTINGS list. @param evt the event
 *  @return whether it was theirs */
bool foot_rest(const touch_event_t *evt);
/** @brief The PORT button's choice list: OUTPUTS and PROG PORT 1 to 4,
 *         a refused port naming why; the TARGET lock names itself in the
 *         alert band. */
void port_list_open(void);
/** @brief Choice @p value of the PORT list taken: 0 OUTPUTS, 1 to 4 a
 *         port. @param value the choice */
void port_list_pick(int value);

/* --- servo_hub.c: the hub's tiles ---------------------------------------- */

struct ui_help_page;

/** @brief The hub drawn: its tiles and footer, the list, or a panel over
 *         the list. @param c the canvas @param buf the buffer */
void hub_render(gfx_canvas_t *c, int buf);
/** @brief An event on the hub. @param evt the event */
void hub_event(const touch_event_t *evt);
/** @brief The guide's pages on the hub. @return the count */
int hub_help_count(void);
/** @brief Page @p i of the hub's guide. @param i the page
 *  @param out the page @return false past the last */
bool hub_help_page(int i, struct ui_help_page *out);

/* The workbench's own: the strip, the TARGET row, TEST's run cards. */
#include "servo_workbench_int.h"
