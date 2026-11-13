/**
 * @file
 * @brief The MOTOR & ESC screen's private seam between motor_screen.c, the screen
 * and LIVE's page (with motor_events.c and motor_draw.c, which share its
 * state through motor_live_int.h), motor_workbench.c, its task strip and
 * the CONTROL card's TARGET row, motor_program_page.c, PROGRAM's list and
 * its sessions, and motor_settings_page.c, the SETTINGS list the footer
 * opens: the layout they draw on and the functions each calls of the
 * others.  Nothing outside
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
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_footer.h"
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
 *        PROGRAM take 304px of the left column.  The shared
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
 * @brief The lowest row the pages and the rail reach: 8px over the footer
 *        (ui_footer.h), whose slots end 4px above the alert band.
 */
#define STOP_FLOOR UI_FOOTER_FLOOR

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
 * TARGET row and RESET PEAKS.  ARM and DISARM are the footer's middle slot
 * (ui_footer.h), the one place every screen has its primary hold.
 */
#define HDR_Y     (PAD + 4)               /**< The temperatures' line, px. */
#define HDR_H     20                      /**< Its height, px. */
#define HERO_Y0   (HDR_Y + HDR_H + 8)     /**< The first reading card, px. */
#define HERO_H    56                      /**< A reading card's height. */
#define HERO_GAP  4                       /**< Between two of them, px. */
/** @brief The CONTROL card's top: under the fourth reading card. */
#define CTL_Y     (HERO_Y0 + 4 * HERO_H + 4 * HERO_GAP)
#define CTL_H     (STOP_FLOOR - CTL_Y)    /**< To 8px over the footer. */
#define BTN_H     UI_TAP_MIN              /**< RESET PEAKS' height, px. */
#define TARGET_Y  (CTL_Y + TITLE_H + 8)   /**< The TARGET row, px. */
#define TARGET_H  UI_TAP_MIN              /**< Its height, px. */
#define RESET_Y   (TARGET_Y + TARGET_H + 10) /**< RESET PEAKS, px. */
_Static_assert(RESET_Y + BTN_H + 4 <= CTL_Y + CTL_H,
               "RESET PEAKS ends inside the CONTROL card");
/* RESET PEAKS and TARGET are UI_TAP_MIN tall by their defines; ARM is the
 * footer's 328 x 48px slot (ui_footer.h). */

/*
 * The left column on LIVE: the telemetry card, with its tag and the
 * derived figures on the title line, PLOT | TABLE with the poll rate beside
 * them, the legend and the plot or the table; under it the THROTTLE card,
 * the readout on the title line, the track with a step key at each end and
 * the hint under them, above the alert band.
 */
#define UP_Y      MO_PAGE_Y               /**< The telemetry card, px. */
#define UP_H      230                     /**< Its height, px. */
#define LO_Y      (UP_Y + UP_H + 8)       /**< The THROTTLE card: y 294. */
#define LO_H      (STOP_FLOOR - LO_Y)     /**< To 8px over the footer. */

/*
 * PROGRAM's page: the left column under the strip, down to 8px over the
 * footer.
 */
#define PAGE_X    LEFT_X                  /**< The page's left edge. */
#define PAGE_Y    MO_PAGE_Y               /**< Its top: 56px. */
#define PAGE_W    LEFT_W                  /**< Its width: 620px. */
#define PAGE_H    (STOP_FLOOR - PAGE_Y)   /**< Its height: 380px. */

_Static_assert(MO_STRIP_X + 2 * MO_TAB_W + MO_TAB_GAP + 3 <= RIGHT_X,
               "the strip ends left of the rail");
_Static_assert(MO_STRIP_Y + MO_STRIP_H + 3 <= MO_PAGE_Y,
               "the strip ends above its page");

/* --- motor_workbench.c: the strip and the TARGET row ---------------------- */

/** @brief What a touch event was to the strip or the TARGET row. */
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
/**
 * @brief Enters the strip's tasks: LIVE, unless the screen comes back from
 *         the OUTPUTS screen its TARGET row opened.
 */
void mwb_enter(void);
/**
 * @brief Drops the press the strip or the TARGET row holds, as touch events
 *         were lost.
 */
void mwb_cancel(void);
/**
 * @brief The TARGET row opened the OUTPUTS screen: the navigation count to come
 * back on.
 */
void mwb_opened_outputs(void);
/**
 * @brief Gives the task the strip shows.
 *
 * @return LIVE or PROGRAM
 */
ui_wb_task_t mwb_task(void);
/**
 * @brief A touch event.  @p strip: whether the strip takes presses (no keypad
 * over it); @p locked: whether the TARGET row does.
 *
 * @param evt the touch event
 * @param strip whether the strip takes presses, which a keypad over it stops
 * @param locked whether the TARGET row is locked
 * @return what the event was to the strip or the row
 */
mwb_ev_t mwb_event(const touch_event_t *evt, bool strip, bool locked);
/**
 * @brief Whether the strip or the TARGET row owns a press under way.
 *
 * @return true while one does
 */
bool mwb_pressed(void);
/**
 * @brief Bumped when the strip's look changes.
 *
 * @return the count
 */
uint32_t mwb_revision(void);
/**
 * @brief Draws the strip's tabs.
 *
 * @param c the canvas to draw on
 */
void mwb_render_strip(gfx_canvas_t *c);
/**
 * @brief Draws the TARGET row.
 *
 * @param c the canvas to draw on
 * @param value its value text
 * @param warn the value is drawn in the warning color
 * @param locked the row is drawn dim
 */
void mwb_render_target(gfx_canvas_t *c, const char *value, bool warn,
                       bool locked);
/* The strip's row with its trough, and the TARGET row. */
/**
 * @brief The strip's row with its trough.
 *
 * @return the rectangle
 */
gfx_rect_t mwb_strip_area(void);
/**
 * @brief The TARGET row.
 *
 * @return the rectangle
 */
gfx_rect_t mwb_target_rect(void);

/* --- motor_program_page.c: PROGRAM's list and its sessions --------------- */

/* Laid out, each MOTOR session in the body under the output strip (reset);
 * the screen entered; left (each session closes, a run ends); touch events
 * lost; the bench armed or asked to arm; a frame. */
/** @brief Lays each MOTOR session out in the body under the output strip. */
void mpp_reset(void);
/** @brief Tells the sessions the screen was entered. */
void mpp_enter(void);
/** @brief Leaves the screen: each session closes and a run ends. */
void mpp_leave(void);
/** @brief Drops the presses the sessions hold, as touch events were lost. */
void mpp_cancel(void);
/**
 * @brief Tells the sessions whether the bench is armed or asked to arm.
 *
 * @param armed true when it is
 */
void mpp_armed(bool armed);
/**
 * @brief Runs the sessions for one frame.
 *
 * @param dt_s seconds since the last call
 */
void mpp_tick(float dt_s);
/**
 * @brief The session shown on PROGRAM, or NULL: the list shows.
 *
 * @return the session
 */
const prog_page_set_t *mpp_shown(void);
/**
 * @brief The home tag in a session: its back() (one level up; from its top the
 * session closes to the list).  Whether there was a session.
 *
 * @return true when there was a session
 */
bool mpp_back(void);
/**
 * @brief A touch event on the list; whether it was the list's.  A tap on a live
 * row opens its session.
 *
 * @param evt the touch event
 * @return true when it was the list's
 */
bool mpp_event(const touch_event_t *evt);
/**
 * @brief Whether a press on the list is under way; drop it.
 *
 * @return true while one does
 */
bool mpp_pressed(void);
/** @brief Drops the press on the list. */
void mpp_cancel_press(void);
/**
 * @brief Bumped when the list's look changes.
 *
 * @return the count
 */
uint32_t mpp_revision(void);
/**
 * @brief The @p k-th row PROGRAM's list shows, headings not counted.
 * @param k   The row, from 0.
 * @param r   Takes its rect.
 * @param row Takes its registry row.
 * @return False past the last row shown.
 */
bool mpp_row(int k, gfx_rect_t *r, const prog_reg_row_t **row);
/**
 * @brief Draws PROGRAM's list, or the session shown.
 *
 * @param c the canvas to draw on
 */
void mpp_render_page(gfx_canvas_t *c);
/* The output strip above a full-width session (motor_output_strip()), and
 * a number that changes whenever its text does. */
void mpp_render_strip(gfx_canvas_t *c, uint8_t proto, uint64_t pins,
                      bool known, bool armed);
/**
 * @brief A number that changes whenever the output strip's text does.
 *
 * @param proto the throttle's protocol
 * @param pins the throttle pins, bit n for GPIO n
 * @param known the binding is read
 * @param armed the bench is armed
 * @return the number
 */
uint32_t mpp_strip_signature(uint8_t proto, uint64_t pins, bool known,
                             bool armed);

/* --- motor_settings_page.c: the SETTINGS list ---------------------------- */

/**
 * @brief The list laid out, closed (the screen's reset).
 */
void msp_reset(void);
/**
 * @brief The list opened at its top, from the footer's SETTINGS.
 */
void msp_open(void);
/**
 * @brief The list closed with what covers it; the values stay as they are,
 *        unsaved where they are (the screen left, the bench left SAFE).
 */
void msp_close(void);
/**
 * @brief Whether the list shows: it covers the body.
 * @return True while it does.
 */
bool msp_shown(void);
/**
 * @brief Touch events lost: the list's press drops.
 */
void msp_cancel(void);
/**
 * @brief The home tag over the list: what covers it closes; else the leave
 *        warning holds it while something is unsaved; else it closes.
 * @return True when the list was shown: the tag stays on the screen.
 */
bool msp_back(void);
/**
 * @brief A touch event on the list.
 * @param evt  The event.
 * @param safe Whether the bench is SAFE, nothing asked of it
 *             (ui_value_gate.h's OFF), and ARM is not held: the throttle's
 *             endpoints take a press, and their keypad's OK, only then.
 */
void msp_event(const touch_event_t *evt, bool safe);
/**
 * @brief A frame: the key repeat, SAVE's state, a keypad of an endpoint
 *        closed once @p safe is false.
 * @param dt_s The frame's time, s.
 * @param safe As msp_event()'s.
 */
void msp_tick(float dt_s, bool safe);
/**
 * @brief Bumped when the list's look changes.
 * @return The revision.
 */
uint32_t msp_revision(void);
struct ui_setlist;
/**
 * @brief The list, for the guide's pages over it (ui_setlist_help_page()).
 * @return The list.
 */
const struct ui_setlist *msp_list(void);
/**
 * @brief The list over the body.
 * @param c The canvas.
 */
void msp_render(gfx_canvas_t *c);

/* --- motor_screen.c: what the list and the footer read -------------------- */

/**
 * @brief What the throttle drives, as the footer's and the list's line
 *        under the name: `protocol pins`, 31 cells of the 10 x 20 face.
 * @param out  Takes the text.
 * @param n    Bytes at @p out.
 * @param warn Takes whether it is the warning's: no pin, or not read.
 * @return True: the line is always there.
 */
bool mo_target_line(char *out, size_t n, bool *warn);

/* --- motor_logname.c: RENAME LOG and its keyboard ------------------------- */

/**
 * @brief RENAME LOG in the strip row, right of LIVE | PROGRAM: 180 x 40px,
 *        its right edge the left column's (x 449 to 629).
 */
#define MO_LOG_W 180
#define MO_LOG_X (LEFT_X + LEFT_W - MO_LOG_W) /**< Its left edge: x 449. */
_Static_assert(MO_LOG_X >= MO_STRIP_X + 2 * MO_TAB_W + MO_TAB_GAP + 3,
               "RENAME LOG starts right of the strip's trough");
_Static_assert(MO_LOG_X + MO_LOG_W == LEFT_X + LEFT_W
                   && MO_LOG_X + MO_LOG_W < RIGHT_X,
               "RENAME LOG ends with the left column, left of the rail");

/**
 * @brief Laid out: the keyboard closed, no press, no log, no request.
 */
void mln_reset(void);
/**
 * @brief Whether the keyboard is open: it covers the body.
 * @return True while it is.
 */
bool mln_shown(void);
/**
 * @brief A touch event: the open keyboard takes every one; else a press on
 *        RENAME LOG is the button's.  Call it only where the button shows:
 *        LIVE and PROGRAM's list.
 * @param evt The event.
 * @return True when the event was the keyboard's or the button's.
 */
bool mln_event(const touch_event_t *evt);
/**
 * @brief The keyboard closes, nothing renamed; the button's press drops.
 */
void mln_close(void);
/**
 * @brief Touch events lost: the button's and the keyboard's press drop.
 */
void mln_cancel(void);
/**
 * @brief A frame: a bench that leaves SAFE closes the keyboard, and the
 *        button follows whether it is live.
 */
void mln_tick(void);
/**
 * @brief Nothing RENAME LOG drew is on the glass any more (the screen's
 *        motor_invalidate()): the button and the open keyboard are drawn
 *        again in both framebuffers, whatever their revisions say.
 */
void mln_invalidate(void);
/**
 * @brief Framebuffer @p buf was painted whole: the button is drawn again.
 * @param buf The framebuffer, 0 or 1.
 */
void mln_forget(int buf);
/**
 * @brief RENAME LOG into the strip row, when its look changed since
 *        framebuffer @p buf last showed it.
 * @param c   The canvas.
 * @param buf The framebuffer, 0 or 1.
 */
void mln_render_button(gfx_canvas_t *c, int buf);
/**
 * @brief The keyboard over the body while it is open.  Closing it repaints
 *        the screen whole (motor_invalidate()).
 * @param c   The canvas.
 * @param buf The framebuffer, 0 or 1.
 * @return True while the keyboard is open: nothing else is drawn.
 */
bool mln_render_over(gfx_canvas_t *c, int buf);
