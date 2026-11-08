/**
 * @file supply_screen_int.h
 * @brief The SUPPLY screen's private seam between supply_screen.c, the state,
 *        the commands and the screen's interface, supply_events.c, its touch
 *        events and frames, and supply_draw.c, its drawing: the layout the
 *        three work on, the state and the functions one calls in another;
 *        supply_settings.c, the SETTINGS list the footer opens.  Nothing outside shared/ui includes
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
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "prog5v_box.h"
#include "settings.h"
#include "supply_screen.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_footer.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#define W UI_PANEL_W /**< The panel's width, px: the screen's own. */
#define H UI_BODY_H   /**< The body's height under the band, px. */

/*
 * MOTOR & ESC's frame at the 1024 x 600 sizes: the page in a 620px left
 * column, the readings and the CONTROL card on a 380px rail on the right,
 * and the footer (ui_footer.h) [SUPPLY] [OUTPUT ON / OFF] [SETTINGS].
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
 * @brief The header row at SERVO's and MOTOR & ESC's strip sizes: from
 *        (9, 8), 40px tall; the page from y 56.  The shared UI_WB_ figures
 *        plus the difference.  SUPPLY has one task, so no tabs.
 */
#define SU_STRIP_X UI_WB_STRIP_X
#define SU_STRIP_Y (UI_WB_STRIP_Y + 2)  /**< The strip's top: 8px. */
#define SU_STRIP_H (UI_WB_STRIP_H + 16) /**< A tab's height: 40px. */
#define SU_TAB_W   (UI_WB_TAB_W + 38)   /**< A tab's width: 150px. */
#define SU_TAB_GAP UI_WB_TAB_GAP        /**< Between two tabs: 4px. */
#define SU_PAGE_Y  (UI_WB_PAGE_Y + 18)  /**< The page's top: 56px. */

/**
 * @brief The lowest row the cards and the left column's overlays reach: 8px
 *        over the footer, whose slots end 4px above the alert band.
 */
#define STOP_FLOOR UI_FOOTER_FLOOR

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
 * The header row: where the numbers come from, and the range the set
 * points may take.  In sight under the keypad and the question.
 */
/** @brief The header's left edge: the left column's padding. */
#define INFO_X    (LEFT_X + INNER)
/** @brief Its top: centered on the strip's 40px. */
#define INFO_Y    (SU_STRIP_Y + (SU_STRIP_H - LINE_H) / 2)

/** @brief A panel's title strip, the accent tag on its top edge, px. */
#define TITLE_H   20

/*
 * The rail, top down: four reading cards from the page's top, then the
 * CONTROL card with the totals on its title line, RESET PEAKS and the
 * ports' own 5V line.  OUTPUT ON / OFF is the footer's middle slot, where
 * MOTOR & ESC has ARM and DISARM.
 */
#define RAIL_Y    SU_PAGE_Y               /**< The first card, px. */
#define CARD_H    64                      /**< A reading card's height. */
#define CARD_GAP  4                       /**< Between two of them, px. */
/** @brief The CONTROL card's top: 4px under the fourth reading card. */
#define CTL_Y     (RAIL_Y + 4 * CARD_H + 3 * CARD_GAP + 4)
#define CTL_H     (STOP_FLOOR - CTL_Y)    /**< To 8px over the footer. */
#define BTN_H     UI_TAP_MIN              /**< RESET PEAKS' height, px. */
#define RESET_Y   (CTL_Y + TITLE_H + 6)   /**< RESET PEAKS: y 354. */
/** @brief The programming ports' own supply's line under RESET PEAKS, px. */
#define PROG5V_Y  (RESET_Y + BTN_H + 6)
_Static_assert(PROG5V_Y + LINE_H <= CTL_Y + CTL_H - 4,
               "the PROG PORTS 5V line ends inside CONTROL");

/*
 * The left column on LIVE: the output card, with PLOT | TABLE under its
 * title, the legend and the plot or the table; under it the SET POINTS
 * card.
 */
#define UP_Y      SU_PAGE_Y               /**< The output card, px. */
#define UP_H      182                     /**< Its height, px. */
#define LO_Y      (UP_Y + UP_H + 8)       /**< SET POINTS: y 246. */
#define LO_H      (STOP_FLOOR - LO_Y)     /**< To 8px over the footer. */

#define TAB_X     (LEFT_X + INNER)        /**< PLOT | TABLE, px. */
#define TAB_Y     (UP_Y + TITLE_H + 6)    /**< Their top: y 82. */
#define TAB_H     40                      /**< Their height, px. */
#define TAB_W     200                     /**< The row: two of 100px. */
#define LEG_Y     (TAB_Y + TAB_H + 4)     /**< The legend, px. */
#define LEG_H     20                      /**< Its height, px. */
/**
 * @brief The PROG 5V box on LIVE, on a board that switches the ports' own
 *        5V (prog5v_box.h): right of PLOT | TABLE, in the output card's
 *        head, ending where the legend starts.
 */
#define P5_X      (TAB_X + TAB_W + 8)
/** @brief The box's top: PROG5V_BOX_H over the legend. */
#define P5_Y      (LEG_Y - PROG5V_BOX_H)
/** @brief The box's width, up to the card's inner edge. */
#define P5_W      (LEFT_X + LEFT_W - INNER - P5_X)
_Static_assert(P5_W >= PROG5V_BOX_MIN_W, "the PROG 5V box fits the head");
_Static_assert(P5_Y >= UP_Y + TITLE_H, "the PROG 5V box is under the title");
#define PLOT_X    (LEFT_X + INNER)        /**< The plot's left edge, px. */
#define PLOT_W    (LEFT_W - 2 * INNER)    /**< Its width: 600px. */
#define PLOT_Y    (LEG_Y + LEG_H + 4)     /**< Its top, px. */
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y) /**< Its height: 78px. */
/** @brief TABLE's SET column, from PLOT_X: SUP_TABLE_HEAD's cell 15. */
#define TBL_SET_DX  150
#define TBL_NOW_DX  300                   /**< NOW: its cell 30, px. */
#define TBL_UNIT_DX 400                   /**< The unit, px. */
#define TBL_PITCH   24                    /**< From row to row, px. */
#define TBL_ROW_DY  28                    /**< The first row, from LEG_Y. */
_Static_assert(LEG_Y + TBL_ROW_DY + 2 * TBL_PITCH + LINE_H
                   <= UP_Y + UP_H - INNER,
               "TABLE's third row ends inside the output card");

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
#define SET_Y0      (LO_Y + TITLE_H + 4)  /**< The first label: y 270. */
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
 * The keypad and the question take the left column under the header row,
 * plot and set points, and leave the header, the rail and the footer --
 * the readings and OUTPUT OFF -- where they are and working.
 */
#define OV_X      LEFT_X                  /**< The left column, px. */
#define OV_Y      SU_PAGE_Y               /**< Under the strip: y 56. */
#define OV_W      LEFT_W                  /**< 620px. */
#define OV_H      (STOP_FLOOR - OV_Y)     /**< To 8px over the footer. */
/*
 * The question: its title, why it asks, the change, APPLY and CANCEL, and
 * at its foot where to turn it off, ending 4px above the alert band.
 */
#define ASK_BTN_W 240                     /**< APPLY and CANCEL, px. */
#define ASK_BTN_H 60                      /**< Their height, px. */
#define ASK_BTN_Y (OV_Y + 240)            /**< Their top: y 296. */
#define ASK_PITCH 26                      /**< Its sentences' lines, px. */

/* The sample rate of the panel's loop, which is the plot's time base. */
/**
 * @brief The panel loop's sample rate, 20Hz, which is the plot's time base.
 */
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
       P_VALUE_I, P_KEYPAD, P_APPLY, P_DISCARD, P_PROG5V, P_MODEL_LINE,
       P_SETTINGS };

/* Where a set point change came from, for the question it may need. */
enum { FROM_SLIDER = 0, FROM_KEYPAD };

/* What the keypad is typing. */
enum { KP_NONE = 0, KP_SET_V, KP_SET_I };

/** @brief Why the PD mini's rows of the SETTINGS list take no press now. */
typedef enum {
    SUP_PD_FREE = 0,    /**< they take one                                   */
    SUP_PD_LOCK_PORT,   /**< a programming port holds the supply             */
    SUP_PD_LOCK_LIVE,   /**< the output is on, or an ON is on its way        */
    SUP_PD_LOCK_ARMED,  /**< the bench is ARMED (settings_safe_only())       */
} sup_pd_lock_t;

/**
 * @brief The supply screen's state: the plot, the sliders, the set points
 *        and what the supply reports, the question about a change, the
 *        keypad and the revisions the two frame buffers last drew.
 */
typedef struct {
    ui_plot_t       plot;               /**< plot of the three readings and two set points */
    ui_tabs_t       tabs;               /**< PLOT | TABLE tabs, on LIVE */
    uint8_t         pd_lock;            /**< sup_pd_lock_t at the last tick */
    uint32_t        pd_sig;             /**< the wiring's rows as last looked at */
    ui_slider_t     v_slider;           /**< voltage slider */
    ui_slider_t     i_slider;           /**< current slider */
    supply_caps_t   caps;               /**< what the supply can be set to */
    supply_limits_t lim;                /**< what the operator allows */
    supply_caps_t   eff;                /**< the caps narrowed by the limits */
    supply_state_t  sup;                /**< the supply's reported state */
    /*
     * The set points the supply is given.  The sliders show them, except
     * while a drag is under way or a change waits for its confirmation;
     * see sup_propose().
     */
    float cv,                               /**< voltage set point given to the supply, V */
          ci;                               /**< current limit given to the supply, A */
    /* An ON this screen has asked for and not yet seen applied: the output
     * is treated as live from the moment it is asked; see
     * sup_confirm_needed(). */
    /** @brief Whether an ON this screen asked for is not yet seen applied */
    bool            on_asked;
    /* A servo programming port is out of PWM: the IOmcu takes an OFF
     * and nothing else, so this screen offers nothing else; see
     * supply_screen_set_port_held(). */
    /**
     * @brief Whether a servo programming port is out of PWM and holds the
     *        supply
     */
    bool            port_held;
    /** @brief The KST programmer's word on the hold */
    bool            held_kst;
    /** @brief The Hitec programmer's word on the hold */
    bool            held_hitec;
    /** @brief The KST programmer's LEAVE ended since its word on the hold
     *         last changed: the end of its hold is a LEAVE's */
    bool            left_kst;
    /** @brief The same for the Hitec programmer */
    bool            left_hitec;
    /** @brief The SUPPLY page's own word that a programming port holds it
     *         (LINK_SP_PORT), any programmer's port */
    bool            held_page;
    /** @brief The last programmer's word to let go was a LEAVE's: the
     *         release of the hold arms the restore offer */
    bool            leave_end;
    /** @brief The programming ports run from their own supply, not the
     *         PD mini: neither programmer's word holds this screen */
    bool            ports_own;
    /* The operator's set points while a programming port's stand in their
     * place, and the port's as this screen took them; see
     * supply_screen_port_done(). */
    /** @brief Whether a port's set points stand in for the operator's */
    bool            lent;
    float own_v,                            /**< the operator's voltage set point, V */
          own_i,                            /**< the operator's current limit, A */
          lent_v,                           /**< the port's voltage as taken, V */
          lent_i;                           /**< the port's current limit as taken, A */
    /* The operator's set points offered back onto the live output a port
     * left on: armed as a LEAVE ends the hold, standing while that run
     * lasts; see supply_screen_restore_offer(). */
    /** @brief Whether the offer to restore the operator's set points stands */
    bool            offer;
    uint32_t        offer_stops;        /**< stop count the offer was made under */
    uint32_t        offer_offs;         /**< off count the offer was made under */
    /* The screen whose ON the output is, while it is on or an ON is on
     * its way; and an OFF owed for leaving it while a programming port
     * held the supply, sent when the port lets go.  See
     * supply_screen_left(). */
    supply_by_t     owner;              /**< screen whose ON the output is */
    /**
     * @brief Whether an OFF is owed for leaving the screen while a port held
     *        the supply
     */
    bool            off_owed;
    bool            confirm_open;       /**< whether the question about a change is open */
    uint32_t        off_count;          /**< reported ON-to-OFF edges */
    uint32_t        off_asks;           /**< OFFs posted (sup_post_off()) */
    /* Stops seen (supply_screen_cancel_on()), and the count the question
     * was asked under: see sup_confirm_stands(). */
    uint32_t        stops;              /**< stops seen */
    uint32_t        confirm_stops;      /**< stop count the question was asked under */
    /* Whether OUTPUT's press began on a live output: an OFF tap rather
     * than an ON hold, whatever the output does before it lifts. */
    /** @brief Whether OUTPUT's press began on a live output (an OFF tap) */
    bool            press_on;
    float pend_v,                           /**< voltage change waiting for its question, V */
          pend_i;                           /**< current change waiting for its question, A */
    gfx_rect_t apply_btn,                   /**< APPLY of the question            */
               discard_btn;                 /**< CANCEL of the question           */
    bool            model;              /**< whether the panel runs against its model supply */
    uint32_t        baud;               /**< PD mini UART rate, baud; 0 when not shown */
    int tx1,                                /**< PD mini UART transmit pin, GPIO + 1; 0 for none */
        rx1;                                /**< PD mini UART receive pin, GPIO + 1; 0 for none */
    /* The output as this screen believes it: what the supply reported, and
     * an OFF this screen has asked for and not yet seen answered. */
    bool            on;                 /**< the output as this screen believes it */
    /* The output as the supply reports it, which bounds a run. */
    bool            out_reported;       /**< the output as the supply reports it */
    supply_cmd_t    pending;            /**< the commands waiting to go to the supply */
    /** @brief Bit per frame buffer: its title and strip were drawn */
    unsigned        drawn_mask;
    uint32_t        drawn_push[2];      /**< plot pushes each frame buffer last drew */
    uint32_t        drawn_plot[2];      /**< plot revision each frame buffer last drew */
    uint8_t         drawn_title[2];     /**< title tag each frame buffer last drew */
    uint8_t         drawn_online[2];    /**< whether the supply answered, as
                                             each buffer's heading row says */
    uint32_t        ctrl_rev;           /**< revision of the CONTROL card */
    uint32_t        prog_5v;            /**< the ports' own supply as last drawn:
                                             state << 8 | pin                */
    uint32_t        drawn_ctrl[2];      /**< CONTROL revision each frame buffer last drew */
    uint32_t        set_rev;            /**< revision of the set points */
    uint32_t        drawn_set[2];       /**< set point revision each frame buffer last drew */
    uint32_t        out_rev;            /**< revision of OUTPUT */
    prog5v_box_t    p5;                 /**< the PROG 5V box on LIVE */
    uint32_t        drawn_p5[2];        /**< its revision each buffer drew */
    bool            p5_shown;           /**< it showed at the last tick */
    uint32_t        drawn_out[2];       /**< OUTPUT revision each frame buffer last drew */
    uint32_t        ov_rev;             /**< revision of the question */
    uint32_t        drawn_ov[2];        /**< its revision each frame buffer last drew */
    uint32_t        drawn_list[2];      /**< the list's revision each buffer last drew */
    bool            drawn_list_shown[2]; /**< whether each buffer drew the list */
    uint32_t        drawn_kp[2];        /**< keypad revision each frame buffer last drew */
    ui_hold_t       hold;               /**< hold counter of OUTPUT ON */
    gfx_rect_t      out_rect;           /**< OUTPUT ON / OFF */
    gfx_rect_t      reset_rect;         /**< RESET PEAKS */
    gfx_rect_t v_down,                      /**< voltage fine step down           */
               v_up,                        /**< voltage fine step up             */
               i_down,                      /**< current fine step down           */
               i_up;                        /**< current fine step up             */
    ui_keypad_t     kp;                 /**< the keypad */
    int             kp_target;          /**< what the keypad types, KP_* */
    int             pressed;            /**< what the press is on, P_* */
    uint8_t         press_id;           /**< touch id of the press */
    bool            have_press;         /**< whether a press is on the glass */
} sup_state_t;

/* Defined in supply_screen.c. */
/** @brief The supply screen's state, in supply_screen.c. */
extern sup_state_t sup_s;
/** @brief Short name for sup_s inside the supply screen's files. */
#define s sup_s

/* --- supply_screen.c: the series, the geometry, the rules and the
 *     commands ------------------------------------------------------------ */

/**
 * @brief The plot's five series: voltage, current, power, and the voltage and
 *        current set points.
 */
extern const ui_plot_series_t sup_series[S_COUNT];

/**
 * @brief Sets the plot's series colours from the active theme.
 */
void sup_bind_colours(void);
gfx_rect_t sup_set_value_rect(int row);
/**
 * @brief Returns the rectangle of reading card @p i on the rail.
 * @param i card index, 0 to 3
 * @return the card's rectangle
 */
gfx_rect_t sup_card_rect(int i);
/**
 * @brief Returns whether LIVE shows: the SETTINGS list does not cover it.
 * @return true on LIVE, false under the list
 */
bool sup_on_live(void);
/**
 * @brief The press under way begins: its contact and what it is on.
 * @param evt the DOWN event
 * @param code what it is on, P_*
 */
void sup_take(const touch_event_t *evt, int code);
/**
 * @brief Where the SIMULATED line's button is, which opens the PD MINI tab.
 * @return the button as drawn; its height is the strip's
 */
gfx_rect_t sup_model_line_rect(void);
/**
 * @brief Returns whether the PROG 5V box shows: on LIVE, with neither the
 *        keypad nor the question over it, on a board that switches the
 *        ports' own 5V.
 * @return true while it shows
 */
bool sup_p5_shown(void);
/**
 * @brief Returns the area the keypad and the question take.
 * @return the left column under the header row, 620px wide
 */
gfx_rect_t sup_overlay_area(void);
/**
 * @brief Re-reads the operator's limits from the settings and narrows the
 *        caps.
 * @param persist true asks for a save when a start value is corrected (a
 *                restart or a reload); false leaves the correction for the
 *                SETTINGS list's SAVE or DISCARD.
 */
void sup_refresh_limits(bool persist);
/**
 * @brief Clears the owner and any owed OFF once the output is off with no ON
 *        asked.
 */
void sup_owner_settle(void);
/**
 * @brief Asks for the output ON, unless an OFF is pending or a programming
 *        port holds the supply.
 * @param by the screen the ON belongs to
 */
void sup_post_on(supply_by_t by);
/**
 * @brief Asks for the output OFF.
 */
void sup_post_off(void);
/**
 * @brief Ends the press on a slider or key and lets the set points follow the
 *        supply.
 */
void sup_let_go(void);
/**
 * @brief Returns whether a change waits for a confirmation: only while the
 *        output is on and as the operator's settings ask.
 * @param from where the change came from, FROM_SLIDER or FROM_KEYPAD
 * @return true when the question is asked
 */
bool sup_confirm_needed(int from);
/**
 * @brief Gives a set point change to the supply, or opens the question when
 *        one is needed.
 * @param from where the change came from, FROM_SLIDER or FROM_KEYPAD
 */
void sup_propose(int from);
/**
 * @brief Returns whether the question about a live output still stands: the
 *        output on or an ON on its way, and no stop since it was asked.
 * @return true when APPLY may still apply the change
 */
bool sup_confirm_stands(void);
/**
 * @brief Closes the question, applying the pending change or dropping it.
 * @param apply true for APPLY, false for CANCEL
 */
void sup_confirm_close(bool apply);

/* --- supply_events.c ---------------------------------------------------- */

/**
 * @brief Drops the press as a lost release: a gesture asks for nothing, except
 *        a tap on OUTPUT OFF, which is sent.
 */
void sup_drop_press(void);
/**
 * @brief Returns whether (@p x, @p y) is on OUTPUT OFF, the one control here
 *        whose press stops what the bench drives.
 * @param x column, px
 * @param y row, px
 * @return true when the point is on OUTPUT OFF while the output is on
 */
bool sup_safe_at(int x, int y);
/**
 * @brief Takes a touch event: presses, drags and releases on the screen's
 *        controls.
 * @param evt the event
 */
void sup_event(const touch_event_t *evt);
/**
 * @brief Advances the screen by one frame: holds, repeats and the question
 *        about a live output.
 * @param dt_s time since the last tick, s
 */
void sup_tick(float dt_s);

/* --- supply_settings.c: the SETTINGS list ----------------------------- */

/**
 * @brief Why the PD mini's rows take no press now.
 * @return the lock, SUP_PD_FREE with none
 */
sup_pd_lock_t sup_pd_lock(void);
/**
 * @brief The frame's look at the lock and at the wiring: a change repaints
 *        the list.
 */
void sup_pd_tick(void);
/** @brief The list laid out, closed (the screen's reset). */
void sup_list_reset(void);
/**
 * @brief The list opened at its top, or at the PD mini's heading.
 * @param pdmini At the PD mini's heading.
 */
void sup_list_open(bool pdmini);
/** @brief The list closed with what covers it; the values stay. */
void sup_list_close(void);
/**
 * @brief Whether the list shows: it covers the body.
 * @return true while it does
 */
bool sup_list_shown(void);
/**
 * @brief The home tag over the list: what covers it closes; else the leave
 *        warning holds it while something is unsaved; else it closes.
 * @return true when the list was shown
 */
bool sup_list_back(void);
/**
 * @brief A touch event on the list.
 * @param evt the event
 */
void sup_list_event(const touch_event_t *evt);
/** @brief Touch events lost: the list's press drops. */
void sup_list_cancel(void);
/**
 * @brief A frame: the key repeat and SAVE's state.
 * @param dt_s the frame's time, s
 */
void sup_list_tick(float dt_s);
/**
 * @brief Bumped when the list's look changes.
 * @return the revision
 */
uint32_t sup_list_revision(void);
/** @brief The list repaints: what it shows changed outside it. */
void sup_list_repaint(void);
/**
 * @brief The list over the body.
 * @param c the canvas
 */
void sup_list_render(gfx_canvas_t *c);

/* --- supply_draw.c: what the footer and the list read ------------------- */

/**
 * @brief Where the output comes from, as the footer's and the list's line
 *        under SUPPLY: SIMULATED, the PD mini's pins, or no pins.
 * @param out  takes the text
 * @param n    bytes at @p out
 * @param warn takes whether it is the warning's: no pins, or no answer
 * @return true: the line is always there
 */
bool sup_source_line(char *out, size_t n, bool *warn);

/* --- supply_guide.c: the screen guide's pages (ui_help.h) --------------- */

struct ui_help_page;

/**
 * @brief The guide's pages for what SUPPLY shows now: the task's, then the
 *        rail's two; 0 under the keypad or the question.
 * @return the count
 */
int supply_help_count(void);

/**
 * @brief Page @p i of those supply_help_count() counts.
 * @param i the page, from 0
 * @param out the page
 * @return false past the last or for NULL
 */
bool supply_help_page(int i, struct ui_help_page *out);

/* --- supply_draw.c ------------------------------------------------------ */

void sup_render(gfx_canvas_t *c, int buffer_index);/**
 * @brief The ports' own supply as published (supply_screen_set_prog_5v()).
 * @return state << 8 | pin
 */
uint32_t sup_prog_5v(void);


