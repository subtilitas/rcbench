/**
 * @file
 * @brief The MOTOR & ESC screen's private seam between motor_screen.c, the screen
 * and LIVE's page (with motor_events.c and motor_draw.c, which share its
 * state through motor_live_int.h), motor_workbench.c, its task strip and
 * the CONTROL card's TARGET row, motor_program_page.c, PROGRAM's list and
 * its sessions, and motor_settings_page.c, SETTINGS' page: the layout they
 * draw on and the functions each calls of the others.  Nothing outside
 * shared/ui includes this header; the screen's interface is
 * motor_screen.h.
 *
 * Each part keeps its own state; psram_bss.lf places each one's .bss.
 * Included last, or last but one before motor_live_int.h: its layout
 * macros have short names (W, H, PAD), which no other header included
 * after it may meet.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_screen.h"
#include "ui_workbench.h"

#define W UI_PANEL_W /**< The panel's width, px: the screen's own. */
#define H UI_BODY_H   /**< The body's height under the band, px. */

/*
 * Horizontal bands, not columns: seventeen full-height vertical lines cost
 * about 8,160 cache-line fills on this panel, and the same pixels drawn as
 * horizontal lines cost none.
 */
#define PAD       UI_MARGIN /**< The screen's outer margin, px. */
#define INNER     10        /**< A card's own padding, px. */

/**
 * @brief The left column's width, px: SERVO's left card, 620px.  The page
 *        on the left, the readouts and the CONTROL card on a rail on the
 *        right, so a glance at the numbers and a hand on the throttle do
 *        not compete for the same part of the screen.  The rail is the
 *        same on every task.
 */
#define LEFT_W    620
#define LEFT_X    PAD                      /**< The left column, px. */
#define RIGHT_X   (LEFT_X + LEFT_W + 8)    /**< The rail: x 636. */
#define RIGHT_W   (W - PAD - RIGHT_X)      /**< The rail's width: 380px. */
/** @brief The rail's inner column, as SERVO's output card's: x 648. */
#define RC_X      (RIGHT_X + 12)
#define RC_W      (RIGHT_W - 24)           /**< Its width: 356px. */

/**
 * @brief The task strip at the 1024 x 600 sizes, as SERVO's: tabs of 150 x
 *        40px, 4px apart, from y 8, and the page from y 56.  LIVE |
 *        PROGRAM | SETTINGS take 458px of the left column.  The shared
 *        UI_WB_ figures stay at their 24px strip for SUPPLY until it is
 *        laid out at the new sizes; these are MOTOR & ESC's own, the
 *        shared figures plus the difference.
 */
#define MO_STRIP_X UI_WB_STRIP_X
#define MO_STRIP_Y (UI_WB_STRIP_Y + 2)  /**< The strip's top: 8px. */
#define MO_STRIP_H (UI_WB_STRIP_H + 16) /**< A tab's height: 40px. */
#define MO_TAB_W   (UI_WB_TAB_W + 38)   /**< A tab's width: 150px. */
#define MO_TAB_GAP UI_WB_TAB_GAP        /**< Between two tabs: 4px. */
#define MO_PAGE_Y  (UI_WB_PAGE_Y + 18)  /**< The page's top: 56px. */

/**
 * @brief The lowest row a control that stops what the bench drives, or a
 *        line of text, may reach: 4px above the alert band.
 */
#define STOP_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in.
 *
 * MO_FONT_LABEL (10 x 20) for every label, button, row and value;
 * MO_FONT_FINE (8 x 16) for the data lines: the legend's scales, the IOmcu's
 * refusals and SETTINGS' help lines; MO_FONT_HEAD (20 x 36) for a reading
 * wider than its place; MO_FONT_NUM (32 x 44, digits, sign, point and
 * space only) for the four readings and the throttle.
 */
#define MO_FONT_LABEL (&gfx_font_10x20)
#define MO_FONT_FINE  UI_FONT_FINE           /**< 8 x 16: data lines. */
#define MO_FONT_HEAD  (&gfx_font_20x36)      /**< 20 x 36: a wide reading. */
#define MO_FONT_NUM   (&gfx_font_num_32x44)  /**< 32 x 44: the readings. */

/** @brief A panel's title strip, the accent tag on its top edge, px. */
#define TITLE_H   20

/*
 * The rail, top down: the temperatures over it on every task, four reading
 * cards, and the CONTROL card with its totals on the title line, the
 * TARGET row, RESET PEAKS and ARM.  The TARGET row, RESET PEAKS' row and
 * ARM are where SERVO's output card has its TARGET row, CENTRE / SWEEP /
 * RELEASE and ARM, so ARM and DISARM are in one place on both workbenches.
 */
#define HDR_Y     (PAD + 4)               /**< The temperatures' line, px. */
#define HDR_H     20                      /**< Its height, px. */
#define HERO_Y0   (HDR_Y + HDR_H + 8)     /**< The first reading card, px. */
#define HERO_H    56                      /**< A reading card's height. */
#define HERO_GAP  4                       /**< Between two of them, px. */
/** @brief The CONTROL card's top: under the fourth reading card. */
#define CTL_Y     (HERO_Y0 + 4 * HERO_H + 4 * HERO_GAP)
#define CTL_H     (H - PAD - CTL_Y)       /**< To the body's margin. */
#define BTN_H     UI_TAP_MIN              /**< RESET PEAKS' height, px. */
#define TARGET_Y  (CTL_Y + TITLE_H + 8)   /**< The TARGET row, px. */
#define TARGET_H  UI_TAP_MIN              /**< Its height, px. */
#define RESET_Y   (TARGET_Y + TARGET_H + 10) /**< RESET PEAKS, px. */
#define ARM_Y     (RESET_Y + BTN_H + 10)  /**< ARM and DISARM, px. */
#define ARM_H     60                      /**< ARM's height: 9.0mm. */
_Static_assert(ARM_Y + ARM_H <= STOP_FLOOR,
               "DISARM ends above the alert band");
_Static_assert(ARM_Y + ARM_H <= CTL_Y + CTL_H,
               "ARM ends inside the CONTROL card");
/* RESET PEAKS and TARGET are UI_TAP_MIN tall by their defines. */
_Static_assert(ARM_H >= UI_TAP_MIN, "ARM is a finger tall");

/*
 * The left column on LIVE: the telemetry card, with its tag and the
 * derived figures on the title line, PLOT | TABLE with the poll rate beside
 * them, the legend and the plot or the table; under it the THROTTLE card,
 * the readout on the title line, the track with a step key at each end and
 * the hint under them, above the alert band.
 */
#define UP_Y      MO_PAGE_Y               /**< The telemetry card, px. */
#define UP_H      288                     /**< Its height, px. */
#define LO_Y      (UP_Y + UP_H + 8)       /**< The THROTTLE card, px. */
#define LO_H      (H - PAD - LO_Y)        /**< To the body's margin. */

/*
 * SETTINGS' and PROGRAM's page: the left column under the strip, down to
 * the screen's margin.  SETTINGS' keypad covers the strip as well, so no
 * tab switches the task under an open keypad.
 */
#define PAGE_X    LEFT_X                  /**< The page's left edge. */
#define PAGE_Y    MO_PAGE_Y               /**< Its top: 56px. */
#define PAGE_W    LEFT_W                  /**< Its width: 620px. */
#define PAGE_H    (H - PAGE_Y - PAD)      /**< Its height: 476px. */

_Static_assert(MO_STRIP_X + 3 * MO_TAB_W + 2 * MO_TAB_GAP + 3 <= RIGHT_X,
               "the strip ends left of the rail");
_Static_assert(MO_STRIP_Y + MO_STRIP_H + 3 <= MO_PAGE_Y,
               "the strip ends above its page");

/* --- motor_workbench.c: the strip and the TARGET row ---------------------- */

/* What a touch event was to the strip or the TARGET row. */
typedef enum {
    MWB_NONE = 0,     /* neither's                                   */
    MWB_TAKEN,        /* theirs; nothing to do                       */
    MWB_TASK,         /* the strip switched the task                 */
    MWB_TARGET,       /* a tap on the TARGET row: open the choice    */
} mwb_ev_t;

/* Laid out, LIVE shown (reset); a tile's entry: LIVE, unless the screen
 * comes back from the OUTPUTS screen its TARGET row opened; touch events
 * lost. */
void mwb_reset(void);
void mwb_enter(void);
void mwb_cancel(void);
/* The TARGET row opened the OUTPUTS screen: the navigation count to come
 * back on. */
void mwb_opened_outputs(void);
ui_wb_task_t mwb_task(void);
/* A touch event.  @p strip: whether the strip takes presses (no keypad
 * over it); @p locked: whether the TARGET row does. */
mwb_ev_t mwb_event(const touch_event_t *evt, bool strip, bool locked);
/* Whether the strip or the TARGET row owns a press under way. */
bool mwb_pressed(void);
/* Bumped when the strip's look changes. */
uint32_t mwb_revision(void);
void mwb_render_strip(gfx_canvas_t *c);
void mwb_render_target(gfx_canvas_t *c, const char *value, bool warn,
                       bool locked);
/* The strip's row with its trough, and the TARGET row. */
gfx_rect_t mwb_strip_area(void);
gfx_rect_t mwb_target_rect(void);

/* --- motor_program_page.c: PROGRAM's list and its sessions --------------- */

/* Laid out, each MOTOR session in the body under the output strip (reset);
 * the screen entered; left (each session closes, a run ends); touch events
 * lost; the bench armed or asked to arm; a frame. */
void mpp_reset(void);
void mpp_enter(void);
void mpp_leave(void);
void mpp_cancel(void);
void mpp_armed(bool armed);
void mpp_tick(float dt_s);
/* The session shown on PROGRAM, or NULL: the list shows. */
const prog_page_set_t *mpp_shown(void);
/* The home tag in a session: its back() (one level up; from its top the
 * session closes to the list).  Whether there was a session. */
bool mpp_back(void);
/* A touch event on the list; whether it was the list's.  A tap on a live
 * row opens its session. */
bool mpp_event(const touch_event_t *evt);
/* Whether a press on the list is under way; drop it. */
bool mpp_pressed(void);
void mpp_cancel_press(void);
/* Bumped when the list's look changes. */
uint32_t mpp_revision(void);
void mpp_render_page(gfx_canvas_t *c);
/* The output strip above a full-width session (motor_output_strip()), and
 * a number that changes whenever its text does. */
void mpp_render_strip(gfx_canvas_t *c, uint8_t proto, uint64_t pins,
                      bool known, bool armed);
uint32_t mpp_strip_signature(uint8_t proto, uint64_t pins, bool known,
                             bool armed);

/* --- motor_settings_page.c: SETTINGS' page -------------------------------- */

/* Laid out (reset); the screen left; touch events lost. */
void msp_reset(void);
void msp_leave(void);
void msp_cancel(void);
/* Whether the keypad is open over the page and the strip. */
bool msp_modal(void);
/* The home tag: the keypad closes, as its CANCEL does.  Whether it did. */
bool msp_back(void);
/* A touch event on the page.  @p safe: the bench is SAFE, nothing asked of
 * it (ui_value_gate.h's OFF), and ARM is not held; the pulse rows take a
 * press, and their keypad's OK, only then. */
void msp_event(const touch_event_t *evt, bool safe);
/* Whether a press of the page's is under way. */
bool msp_pressed(void);
/* A frame: a keypad for a pulse row closes once @p safe is false, and the
 * save line follows the store. */
void msp_tick(bool safe);
/* Bumped when the page's look changes. */
uint32_t msp_revision(void);
/**
 * @brief SETTINGS' page in its two columns, or the keypad over it.
 * @param c the canvas
 * @param safe whether the pulse rows take a press
 */
void msp_render(gfx_canvas_t *c, bool safe);
