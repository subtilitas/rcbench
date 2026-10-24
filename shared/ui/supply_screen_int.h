/**
 * @file supply_screen_int.h
 * @brief The SUPPLY screen's private seam between supply_screen.c, the state,
 *        the commands and the screen's interface, supply_events.c, its touch
 *        events and frames, and supply_draw.c, its drawing: the layout the
 *        three work on, the rows of SETTINGS' page, the state and the
 *        functions one calls in another.  Nothing outside shared/ui includes
 *        this header; the screen's interface is supply_screen.h.
 *
 * Included last: its layout macros have short names (W, H, PAD) and it
 * names the state s, which no header included after it may meet.  What
 * one file calls or reads in another carries the prefix sup_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "settings.h"
#include "supply_screen.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#define W UI_PANEL_W /**< The panel's width, px: the screen's own. */
#define H UI_BODY_H   /**< The body's height under the band, px. */

/*
 * MOTOR & ESC's frame at the 1024 x 600 sizes: the page in a 620px left
 * column, the readings and the CONTROL card on a 380px rail on the right,
 * the same on both tasks.
 */
#define PAD       UI_MARGIN               /**< The outer margin, px. */
#define INNER     10                      /**< A card's own padding, px. */
#define LEFT_W    620                     /**< The left column, px. */
#define LEFT_X    PAD                     /**< Its left edge, px. */
#define RIGHT_X   (LEFT_X + LEFT_W + 8)   /**< The rail: x 636. */
#define RIGHT_W   (W - PAD - RIGHT_X)     /**< Its width: 380px. */
#define RC_X      (RIGHT_X + 12)          /**< The rail's column: x 648. */
#define RC_W      (RIGHT_W - 24)          /**< Its width: 356px. */

/**
 * @brief The task strip, LIVE | SETTINGS, at SERVO's and MOTOR & ESC's
 *        sizes: tabs of 150 x 40px, 4px apart, from (9, 8); the page
 *        from y 56.  The shared UI_WB_ figures plus the difference.
 */
#define SU_STRIP_X UI_WB_STRIP_X
#define SU_STRIP_Y (UI_WB_STRIP_Y + 2)  /**< The strip's top: 8px. */
#define SU_STRIP_H (UI_WB_STRIP_H + 16) /**< A tab's height: 40px. */
#define SU_TAB_W   (UI_WB_TAB_W + 38)   /**< A tab's width: 150px. */
#define SU_TAB_GAP UI_WB_TAB_GAP        /**< Between two tabs: 4px. */
#define SU_PAGE_Y  (UI_WB_PAGE_Y + 18)  /**< The page's top: 56px. */

/**
 * @brief The lowest row a control that stops what the bench drives, or a
 *        line of text, may reach: 4px above the alert band.
 */
#define STOP_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in.
 *
 * SU_FONT_LABEL (10 x 20) for every label, button, row and value;
 * SU_FONT_FINE (8 x 16) for SETTINGS' help lines; SU_FONT_BODY (12 x 24)
 * for the question's sentences; SU_FONT_HEAD (20 x 36) for MODE's letters,
 * the question's title and its change; SU_FONT_NUM (32 x 44, digits,
 * sign, point and space only) for the readings.
 */
#define SU_FONT_LABEL (&gfx_font_10x20)
#define SU_FONT_FINE  UI_FONT_FINE           /**< 8 x 16: help lines. */
#define SU_FONT_BODY  UI_FONT_BODY           /**< 12 x 24: sentences. */
#define SU_FONT_HEAD  (&gfx_font_20x36)      /**< 20 x 36: MODE, titles. */
#define SU_FONT_NUM   (&gfx_font_num_32x44)  /**< 32 x 44: the readings. */
/** @brief A line of SU_FONT_LABEL, px. */
#define LINE_H    20

/*
 * The header right of the strip, on the strip's middle row: where the
 * numbers come from, and the range the set points may take.  In sight on
 * both tasks and under the keypad and the question.
 */
/** @brief The strip's tasks: LIVE and SETTINGS. */
#define TASKS     (UI_WB_HAS(UI_WB_LIVE) | UI_WB_HAS(UI_WB_SETTINGS))
/** @brief The header's left edge: 12px right of the strip's trough. */
#define INFO_X    (SU_STRIP_X + 2 * SU_TAB_W + SU_TAB_GAP + 3 + 12)
/** @brief Its top: centred on the strip's 40px. */
#define INFO_Y    (SU_STRIP_Y + (SU_STRIP_H - LINE_H) / 2)

/** @brief A panel's title strip, the accent tag on its top edge, px. */
#define TITLE_H   20

/*
 * The rail, top down: four reading cards from the page's top, then the
 * CONTROL card with the totals on its title line, RESET PEAKS, and OUTPUT
 * ON / OFF at the foot, where SERVO and MOTOR & ESC have ARM and DISARM.
 */
#define RAIL_Y    SU_PAGE_Y               /**< The first card, px. */
#define CARD_H    64                      /**< A reading card's height. */
#define CARD_GAP  4                       /**< Between two of them, px. */
/** @brief The CONTROL card's top: 8px under the fourth reading card. */
#define CTL_Y     (RAIL_Y + 4 * CARD_H + 3 * CARD_GAP + 8)
#define CTL_H     (H - PAD - CTL_Y)       /**< To the body's margin. */
#define BTN_H     UI_TAP_MIN              /**< RESET PEAKS' height, px. */
#define RESET_Y   (CTL_Y + TITLE_H + 14)  /**< RESET PEAKS: y 366. */
#define OUT_Y     (RESET_Y + BTN_H + 10)  /**< OUTPUT ON / OFF: y 424. */
#define OUT_H     60                      /**< Its height: 9.0mm. */
_Static_assert(OUT_Y + OUT_H <= STOP_FLOOR,
               "OUTPUT OFF ends above the alert band");
_Static_assert(OUT_H >= UI_TAP_MIN, "OUTPUT is a finger tall");

/*
 * The left column on LIVE: the output card, with PLOT | TABLE under its
 * title, the legend and the plot or the table; under it the SET POINTS
 * card.
 */
#define UP_Y      SU_PAGE_Y               /**< The output card, px. */
#define UP_H      240                     /**< Its height, px. */
#define LO_Y      (UP_Y + UP_H + 8)       /**< SET POINTS: y 304. */
#define LO_H      (H - PAD - LO_Y)        /**< To the body's margin. */

#define TAB_X     (LEFT_X + INNER)        /**< PLOT | TABLE, px. */
#define TAB_Y     (UP_Y + TITLE_H + 6)    /**< Their top: y 82. */
#define TAB_H     40                      /**< Their height, px. */
#define TAB_W     200                     /**< The row: two of 100px. */
#define LEG_Y     (TAB_Y + TAB_H + 4)     /**< The legend, px. */
#define LEG_H     20                      /**< Its height, px. */
#define PLOT_X    (LEFT_X + INNER)        /**< The plot's left edge, px. */
#define PLOT_W    (LEFT_W - 2 * INNER)    /**< Its width: 600px. */
#define PLOT_Y    (LEG_Y + LEG_H + 4)     /**< Its top, px. */
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y) /**< Its height: 136px. */
/** @brief TABLE's SET column, from PLOT_X: SUP_TABLE_HEAD's cell 15. */
#define TBL_SET_DX  150
#define TBL_NOW_DX  300                   /**< NOW: its cell 30, px. */
#define TBL_UNIT_DX 400                   /**< The unit, px. */
#define TBL_PITCH   30                    /**< From row to row, px. */

/*
 * The set points: two rows in SET POINTS, each its label over a row of
 * controls: the fine step down, the track, the fine step up, and the value,
 * which opens the keypad.  The step buttons move by a tenth of a volt or
 * of an amp, which the slider is too coarse to place; the slider covers
 * the range, and both snap to what the supply takes.
 *
 * The thumb stands UI_SLIDER_OVERHANG (5px) proud of the track and its
 * shadow 2px more under it, so the controls start 6px under the label
 * and a row's painted foot ends over the next row's label.
 */
#define SET_ROW_H   82                    /**< From row to row, px. */
#define SET_Y0      (LO_Y + TITLE_H + 4)  /**< The first label: y 328. */
#define SET_TEXT_H  LINE_H                /**< A label's line, px. */
#define SET_CTRL_DY 26                    /**< From label to controls. */
#define SET_CTRL_H  UI_TAP_MIN            /**< Keys, track, value, px. */
#define NUDGE_W     72                    /**< A step key's width, px. */
#define SET_VAL_W   120                   /**< The value's box, px. */
/** @brief The value's box: at the card's padding on the right. */
#define SET_VAL_X   (LEFT_X + LEFT_W - INNER - SET_VAL_W)
/** @brief The fine step up: 8px left of the value. */
#define SET_UP_X    (SET_VAL_X - 8 - NUDGE_W)
#define SET_TRACK_X (LEFT_X + INNER + NUDGE_W + 8) /**< The track: x 98. */
#define SET_TRACK_W (SET_UP_X - 8 - SET_TRACK_X)   /**< Its length: 312. */
#define NUDGE_V     0.1f                  /**< The voltage's step, V. */
#define NUDGE_I     0.1f                  /**< The current's step, A. */
_Static_assert(SET_Y0 + SET_ROW_H + SET_CTRL_DY + SET_CTRL_H
                   + UI_SLIDER_OVERHANG + 2 <= STOP_FLOOR,
               "the current limit's row ends above the alert band");
_Static_assert(SET_CTRL_DY + SET_CTRL_H + UI_SLIDER_OVERHANG + 2
                   <= SET_ROW_H,
               "a row's thumb ends over the next row's label");
_Static_assert(SET_CTRL_DY >= SET_TEXT_H + UI_SLIDER_OVERHANG + 1,
               "the thumb ends under its label");

/*
 * The keypad, the question and SETTINGS' page all take the left column
 * under the strip, plot and set points, and leave the strip and the rail
 * -- the readings and OUTPUT OFF -- where they are and working.
 */
#define OV_X      LEFT_X                  /**< The left column, px. */
#define OV_Y      SU_PAGE_Y               /**< Under the strip: y 56. */
#define OV_W      LEFT_W                  /**< 620px. */
#define OV_H      (H - OV_Y - PAD)        /**< To the body's margin. */
/*
 * SETTINGS' page: two columns of 48px rows at a 56px pitch, each a label
 * left of its 120px value.  The caps and start values on the left, 260
 * px, for labels of up to 13 cells; the trips, their help in the 8 x 16
 * face and the confirmations on the right, 324px, for labels of up to 19
 * cells and help lines of up to 36.  The save line at the page's foot
 * ends 4px above the alert band.
 */
#define OV_ROW_H  UI_TAP_MIN              /**< A row's height, px. */
#define OV_PITCH  56                      /**< From row to row, px. */
#define OV_HEAD_H 28                      /**< A heading's line, px. */
#define OV_VAL_W  120                     /**< A row's value, px. */
#define OV_COL_W  260                     /**< The left column, px. */
#define OV_COL0_X (OV_X + INNER)          /**< Its left edge: x 18. */
#define OV_COL1_X (OV_COL0_X + OV_COL_W + 16)      /**< The right: 294. */
#define OV_COL1_W (OV_X + OV_W - INNER - OV_COL1_X) /**< Its width: 324. */
#define OV_SAVE_Y (STOP_FLOOR - LINE_H)   /**< The save line, px. */
#define OV_HELP_PITCH 18                  /**< The help's lines, px. */
/* From the page's top: the headings, the trips' help, RESET PD MINI. */
#define OV_CAPS_DY     8                  /**< LIMITS and TRIPS, px. */
#define OV_RESTART_DY  152                /**< AFTER RESTART, px. */
#define OV_HELP_DY     204                /**< The trips' help, px. */
#define OV_CONFIRM_DY  268                /**< CONFIRM WHILE ON, px. */
#define OV_MODRESET_DY 300                /**< RESET PD MINI, px. */
_Static_assert(OV_Y + 352 + OV_ROW_H <= OV_SAVE_Y,
               "KEYPAD's row ends over the save line");
_Static_assert(OV_HELP_DY + 3 * OV_HELP_PITCH <= OV_CONFIRM_DY,
               "the trips' help ends over CONFIRM WHILE ON");

/*
 * The question: its title, why it asks, the change, APPLY and CANCEL, and
 * at its foot where to turn it off, ending 4px above the alert band.
 */
#define ASK_BTN_W 240                     /**< APPLY and CANCEL, px. */
#define ASK_BTN_H 60                      /**< Their height, px. */
#define ASK_BTN_Y (OV_Y + 240)            /**< Their top: y 296. */
#define ASK_PITCH 26                      /**< Its sentences' lines, px. */

/* The sample rate of the panel's loop, which is the plot's time base. */
#define SAMPLE_HZ 20.0f

/*
 * Three readings, and the two set points drawn dashed on their readings'
 * scales: a supply in CC (constant current) shows its voltage falling away
 * from the line it was set to.
 */
enum { S_VOLT = 0, S_CURR, S_POWER, S_READINGS, S_VSET = S_READINGS, S_ISET,
       S_COUNT };

/* What a press is on.  One contact holds the screen at a time. */
enum { P_NONE = 0, P_OUTPUT, P_RESET, P_V_DOWN, P_V_UP, P_I_DOWN, P_I_UP,
       P_V_SLIDER, P_I_SLIDER, P_TABS, P_CARD_V, P_CARD_I, P_VALUE_V,
       P_VALUE_I, P_STRIP, P_ROW, P_KEYPAD, P_APPLY, P_DISCARD, P_MODRESET };

/* Where a set point change came from, for the question it may need. */
enum { FROM_SLIDER = 0, FROM_KEYPAD };

/* What the keypad is typing. */
enum { KP_NONE = 0, KP_SET_V, KP_SET_I, KP_SETTING };

/*
 * The settings SETTINGS' page edits, in two columns.  A start value is
 * capped like the set point it starts, and a trip of 0 is off.
 */
typedef struct {
    setting_id_t id;
    ui_text_id_t label;
    int          col;
    int          y;          /* from the overlay's top */
    int          decimals;
} limit_row_t;

static const limit_row_t k_rows[] = {
    { SET_SUPPLY_V_MAX,   TX_SUP_ROW_V_MAX,   0, 36,  2 },
    { SET_SUPPLY_I_MAX,   TX_SUP_ROW_I_MAX,   0, 92,  2 },
    { SET_SUPPLY_V_START, TX_SUP_ROW_V_START, 0, 180, 2 },
    { SET_SUPPLY_I_START, TX_SUP_ROW_I_START, 0, 236, 2 },
    { SET_SUPPLY_TRIP_I,  TX_SUP_ROW_TRIP_I,  1, 36,  2 },
    { SET_SUPPLY_TRIP_V,  TX_SUP_ROW_TRIP_V,  1, 92,  2 },
    { SET_SUPPLY_TRIP_MS, TX_SUP_ROW_TRIP_MS, 1, 148, 0 },
    { SET_SUPPLY_CONFIRM_SLIDE, TX_SUP_ROW_CONFIRM_SLIDE, 1, 296, 0 },
    { SET_SUPPLY_CONFIRM_KEYS,  TX_SUP_ROW_CONFIRM_KEYS,  1, 352, 0 },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

typedef struct {
    ui_plot_t       plot;
    ui_wb_strip_t   wb;       /* LIVE | SETTINGS                        */
    uint32_t        strip_rev;
    uint32_t        drawn_strip[2];
    ui_tabs_t       tabs;     /* PLOT | TABLE, on LIVE                  */
    ui_slider_t     v_slider;
    ui_slider_t     i_slider;
    supply_caps_t   caps;     /* what the supply can be set to          */
    supply_limits_t lim;      /* what the operator allows               */
    supply_caps_t   eff;      /* the caps narrowed by the limits        */
    supply_state_t  sup;
    /*
     * The set points the supply is given.  The sliders show them, except
     * while a drag is under way or a change waits for its confirmation;
     * see sup_propose().
     */
    float           cv, ci;
    /* An ON this screen has asked for and not yet seen applied: the output
     * is treated as live from the moment it is asked; see
     * sup_confirm_needed(). */
    bool            on_asked;
    /* A servo programming port is out of PWM: the IOmcu takes an OFF
     * and nothing else, so this screen offers nothing else; see
     * supply_screen_set_port_held(). */
    bool            port_held;
    /* The screen whose ON the output is, while it is on or an ON is on
     * its way; and an OFF owed for leaving it while a programming port
     * held the supply, sent when the port lets go.  See
     * supply_screen_left(). */
    supply_by_t     owner;
    bool            off_owed;
    bool            confirm_open;
    uint32_t        off_count;      /* reported ON-to-OFF edges */
    /* Stops seen (supply_screen_cancel_on()), and the count the question
     * was asked under: see sup_confirm_stands(). */
    uint32_t        stops;
    uint32_t        confirm_stops;
    /* Whether OUTPUT's press began on a live output: an OFF tap rather
     * than an ON hold, whatever the output does before it lifts. */
    bool            press_on;
    float           pend_v, pend_i;
    gfx_rect_t      apply_btn, discard_btn;
    bool            model;
    uint32_t        baud;          /* PD mini UART rate; 0: not shown */
    int             tx1, rx1;      /* PD mini UART pins, GPIO + 1; 0 none */
    /* The output as this screen believes it: what the supply reported, and
     * an OFF this screen has asked for and not yet seen answered. */
    bool            on;
    /* The output as the supply reports it, which bounds a run. */
    bool            out_reported;
    supply_cmd_t    pending;
    unsigned        drawn_mask;
    uint32_t        drawn_push[2];
    uint32_t        drawn_plot[2];
    uint8_t         drawn_title[2];
    uint32_t        ctrl_rev;
    uint32_t        drawn_ctrl[2];
    uint32_t        set_rev;
    uint32_t        drawn_set[2];
    uint32_t        out_rev;
    uint32_t        drawn_out[2];
    uint32_t        ov_rev;
    uint32_t        drawn_ov[2];
    uint8_t         drawn_save[2];
    uint32_t        drawn_kp[2];
    ui_hold_t       hold;
    gfx_rect_t      out_rect;
    gfx_rect_t      reset_rect;
    gfx_rect_t      modreset_btn;   /* RESET PD MINI, on SETTINGS */
    gfx_rect_t      v_down, v_up, i_down, i_up;
    ui_keypad_t     kp;
    int             kp_target;
    setting_id_t    kp_setting;
    int             pressed;
    int             press_row;
    uint8_t         press_id;
    bool            have_press;
} sup_state_t;

/* Defined in supply_screen.c. */
extern sup_state_t sup_s;
#define s sup_s

/* --- supply_screen.c: the series, the geometry, the rules and the
 *     commands ------------------------------------------------------------ */

extern const ui_plot_series_t sup_series[S_COUNT];

void sup_bind_colours(void);
gfx_rect_t sup_set_value_rect(int row);
gfx_rect_t sup_card_rect(int i);
gfx_rect_t sup_strip_area(void);
bool sup_on_live(void);
gfx_rect_t sup_overlay_area(void);
gfx_rect_t sup_row_rect(int i);
gfx_rect_t sup_row_value_rect(int i);
void sup_refresh_limits(void);
void sup_owner_settle(void);
void sup_post_on(supply_by_t by);
void sup_post_off(void);
void sup_let_go(void);
bool sup_confirm_needed(int from);
void sup_propose(int from);
bool sup_confirm_stands(void);
void sup_confirm_close(bool apply);

/* --- supply_events.c ---------------------------------------------------- */

void sup_drop_press(void);
bool sup_safe_at(int x, int y);
void sup_event(const touch_event_t *evt);
void sup_tick(float dt_s);

/* --- supply_draw.c ------------------------------------------------------ */

void sup_render(gfx_canvas_t *c, int buffer_index);
