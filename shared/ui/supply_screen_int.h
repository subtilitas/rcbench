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

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

#define PAD       6
#define INNER     6
#define COL_GAP   6
#define LEFT_X    PAD
#define LEFT_W    546
#define RIGHT_X   (LEFT_X + LEFT_W + COL_GAP)
#define RIGHT_W   (W - PAD - RIGHT_X)

/*
 * The task strip at the top (ui_workbench.h), LIVE | SETTINGS, as on MOTOR
 * & ESC and SERVO.  To its right on the same row the line that says where
 * the numbers come from, and the range the set points may take: in sight
 * on both tasks and under the keypad and the question.
 */
#define TASKS     (UI_WB_HAS(UI_WB_LIVE) | UI_WB_HAS(UI_WB_SETTINGS))
#define INFO_X    (UI_WB_STRIP_X + 2 * UI_WB_TAB_W + UI_WB_TAB_GAP + 3 + 12)
#define INFO_Y    (UI_WB_STRIP_Y + 4)

/* Under the strip, on LIVE: PLOT | TABLE, over the plot. */
#define HDR_Y     UI_WB_PAGE_Y
#define HDR_H     18

#define UP_Y      (HDR_Y + HDR_H + 4)
#define UP_H      (LO_Y - 6 - UP_Y)
/* The rail's four cards, from the page's top down to the plot's foot. */
#define RAIL_Y    UI_WB_PAGE_Y
#define CARD_GAP  4
#define CARD_H    ((UP_Y + UP_H - RAIL_Y - 3 * CARD_GAP) / 4)

/* The lower row: the set points on the left, the CONTROL card on the right,
 * at the height MOTOR & ESC puts its throttle and CONTROL card. */
#define LO_Y      272
#define LO_H      (H - LO_Y - PAD)

#define TITLE_H   20
#define LEG_Y     (UP_Y + TITLE_H + 4)
#define LEG_H     30
#define PLOT_X    (LEFT_X + INNER)
#define PLOT_W    (LEFT_W - 2 * INNER)
#define PLOT_Y    (LEG_Y + LEG_H + 4)
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y)

/*
 * The set points: two rows in the lower left, each a label and its value
 * over a track with a fine step at each end.  The step buttons move by a
 * tenth of a volt or of an amp, which the slider is too coarse to place;
 * the slider covers the range, and both snap to what the supply takes.  The
 * value itself opens the keypad.
 */
#define SET_ROW_H   62
#define SET_Y0      (LO_Y + TITLE_H + 4)
#define SET_TEXT_H  20
#define SET_CTRL_DY 24
#define SET_CTRL_H  30
#define NUDGE_W     54
#define SET_TRACK_X (LEFT_X + INNER + NUDGE_W + 6)
#define SET_TRACK_W (LEFT_W - 2 * INNER - 2 * (NUDGE_W + 6))
#define NUDGE_V     0.1f
#define NUDGE_I     0.1f

#define BTN_H     36
#define OUT_Y     (LO_Y + TITLE_H + 8)
#define TOTALS_Y  (OUT_Y + BTN_H + 10)
#define RESET_Y   (LO_H + LO_Y - PAD - BTN_H)

#define TAB_Y     HDR_Y
#define TAB_H     HDR_H
#define TAB_W     124

/*
 * The keypad, the question and SETTINGS' page all take the left column under
 * the strip, plot and set points, and leave the right column -- the readings
 * and OUTPUT OFF -- where it is and working.
 */
#define OV_X      LEFT_X
#define OV_Y      UI_WB_PAGE_Y
#define OV_W      LEFT_W
#define OV_H      (H - OV_Y - PAD)
#define OV_ROW_H  36
#define OV_VAL_W  100
/* The caps and start values on the left, 210 px; the trips, their help and
 * the confirmations on the right, 300 px, the 37 cells the help's lines
 * take. */
#define OV_COL_W  210
#define OV_COL1_X (OV_X + 10 + OV_COL_W + 16)
#define OV_COL1_W (OV_X + OV_W - 10 - OV_COL1_X)

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
       P_V_SLIDER, P_I_SLIDER, P_TABS, P_CARD_V, P_CARD_I, P_TEXT_V,
       P_TEXT_I, P_STRIP, P_ROW, P_KEYPAD, P_APPLY, P_DISCARD, P_MODRESET };

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
    { SET_SUPPLY_V_MAX,   TX_SUP_ROW_V_MAX,   0, 38,  2 },
    { SET_SUPPLY_I_MAX,   TX_SUP_ROW_I_MAX,   0, 80,  2 },
    { SET_SUPPLY_V_START, TX_SUP_ROW_V_START, 0, 156, 2 },
    { SET_SUPPLY_I_START, TX_SUP_ROW_I_START, 0, 198, 2 },
    { SET_SUPPLY_TRIP_I,  TX_SUP_ROW_TRIP_I,  1, 38,  2 },
    { SET_SUPPLY_TRIP_V,  TX_SUP_ROW_TRIP_V,  1, 80,  2 },
    { SET_SUPPLY_TRIP_MS, TX_SUP_ROW_TRIP_MS, 1, 122, 0 },
    { SET_SUPPLY_CONFIRM_SLIDE, TX_SUP_ROW_CONFIRM_SLIDE, 1, 260, 0 },
    { SET_SUPPLY_CONFIRM_KEYS,  TX_SUP_ROW_CONFIRM_KEYS,  1, 304, 0 },
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
gfx_rect_t sup_set_text_rect(int row);
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
