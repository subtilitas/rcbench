/**
 * @file
 * @brief RUDDER's AUTO box (servo_rudder_auto_panel.c): its state in the
 *        screen's one object, its controls' places, and the calls the
 *        screen's files make of it.
 *
 * AUTO on RUDDER runs servo_rudder_auto.h on the servo the output card
 * names.  The box takes the place of LENGTH, the drive buttons and the
 * state lines; the readout, the ZERO row, RUDDER's side view of the
 * surface and the output card stay.  The box, in the screen's
 * coordinates, every control inside x 18 to 518:
 *
 *   - TARGET 1000us and TARGET 2000us, y 168 to 216, 245px each;
 *   - MARGIN and TOLERANCE, y 228 to 276, 245px each;
 *   - (the ZERO row, y 288 to 336: ZERO, RESET MAX, MANUAL);
 *   - HOLD 2s TO START, y 348 to 396, x 18 to 418: STOP RUN while it runs;
 *   - three lines, x 18 to 418, y 404 to 492: the step or why a START is
 *     refused, the last result, the report.  Each wraps at a space; all
 *     three in the 10 x 20 face, 20px a row, or in the 8 x 16 face, 16px a
 *     row, where the first takes more than the 88px.
 *   - (the side view, x 428 to 618, y 340 to 492, with AUTO's targets.)
 *
 * Its run moves the servo, switches the bench and asks the programmer the
 * way the limit run does (servo_limit_panel.c), through the screen's own
 * command path: post(), release() and the supply's OFF.  A STOP reaches it
 * through test_end_now() (servo_screen_cancel_arm()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "servo_rudder_auto.h"
#include "servo_screen.h"
#include "touch_types.h"
#include "ui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The box's controls. */
typedef enum {
    RA_C_NONE = 0,  /**< no control                        */
    RA_C_LO,        /**< TARGET 1000us: its keypad         */
    RA_C_HI,        /**< TARGET 2000us                     */
    RA_C_MARGIN,    /**< MARGIN                            */
    RA_C_TOL,       /**< TOLERANCE                         */
    RA_C_START,     /**< HOLD 2s TO START, STOP RUN        */
    RA_C_COUNT      /**< the count, RA_C_NONE included     */
} rauto_ctl_t;

/** @brief The lines the box draws. */
#define RA_LINES 3

/** @brief The box's state, the member `ra` of the screen's state. */
typedef struct {
    servo_rauto_t run;      /**< the run                                */
    bool      view;         /**< RUDDER shows the box                   */
    ui_hold_t hold;         /**< START's 2s hold                        */
    bool      down;         /**< START is held                          */
    uint8_t   pressed;      /**< rauto_ctl_t a tap is on                */
    uint8_t   press_id;     /**< that contact's track id                */
    bool      seen;         /**< the run was running when last looked   */
    bool      leaving;      /**< the ended run's LEAVE is on its way    */
    bool      end_due;      /**< the programmer's run ends after it     */
    int       file;         /**< the report's number; 0 not yet, -1 none */
    bool      report;       /**< the card took the report whole         */
    bool      off;          /**< USER LOGS was off at the start         */
    const char *note;       /**< why the last START was refused, or NULL */
    uint32_t  changes;      /**< the meter's change count at the start  */
    uint16_t  still_ms;     /**< the angle's still time, last reading   */
    uint32_t  sig;          /**< rauto_sig() when last looked           */
    /* The supply as at the run's first reading (servo_limrun_supply()'s
     * rule). */
    bool      sup_watch;    /**< the first reading was taken            */
    bool      sup_online;   /**< the supply answered then               */
    bool      sup_out;      /**< its output was on                      */
    float     sup_v,        /**< its voltage set point, V               */
              sup_i;        /**< its current limit, A                   */
} rauto_ui_t;

/** @brief The box at the screen's start: no run, MANUAL. */
void rauto_init(void);
/** @brief Whether AUTO's box is in sight and takes presses.
 *  @return true on RUDDER with the box shown and nothing over it */
bool rauto_shown(void);
/** @brief Show the box, or MANUAL's controls.  Refused while a run runs.
 *  @param on true for the box */
void rauto_view(bool on);
/** @brief A control's place.  @param k the control
 *  @return its rectangle; empty for RA_C_NONE and RA_C_COUNT */
gfx_rect_t rauto_rect(rauto_ctl_t k);
/** @brief The lines' place.  @return their rectangle */
gfx_rect_t rauto_lines_rect(void);
/** @brief Line @p i of the box as drawn now.
 *  @param i the line, from 0  @param buf where it goes
 *  @param n its size  @return @p buf */
const char *rauto_line(int i, char *buf, size_t n);
/** @brief Why a START cannot run now, or NULL.  @return the line */
const char *rauto_blocked(void);
/** @brief A DOWN on the box: a row's keypad on its release, START's hold,
 *         STOP RUN at once.  @param evt the DOWN
 *  @return whether a control of the box took it */
bool rauto_down(const touch_event_t *evt);
/** @brief The moves and the release of the press rauto_down() took.
 *  @param evt the event  @return whether it was that press's */
bool rauto_rest(const touch_event_t *evt);
/** @brief A press under way dropped, START's hold with it. */
void rauto_cancel(void);
/** @brief The box drawn into @p c.  @param c the canvas */
void rauto_render(gfx_canvas_t *c);
/**
 * @brief The face the box's lines are drawn in: the 10 x 20 face while
 *        the lines, each wrapped at a space to 400px and 4px apart, fit
 *        the 88px down to the alert band, else the 8 x 16 face while they
 *        fit there.
 * @param t the three lines, "" for one not drawn
 * @param pitch receives the face's row pitch, px; may be NULL
 * @return the face; NULL when neither fits, or for NULL @p t
 */
const gfx_font_t *rauto_lines_face(const char *const t[RA_LINES], int *pitch);
/**
 * @brief AUTO's two targets as the side view draws them: TARGET 1000us
 *        and TARGET 2000us, signed by the side's direction the last run
 *        measured, else 1000us negative and 2000us positive.
 * @param deg receives them, degrees: [0] 1000us, [1] 2000us
 * @return false for NULL
 */
bool rauto_targets(float deg[2]);
/** @brief One number for what the box shows.  @return the number */
uint32_t rauto_sig(void);
/** @brief START's hold against the clock and the run every frame.
 *  @param dt_s the frame's time, s */
void rauto_frame(float dt_s);
/** @brief The ended run's LEAVE, whichever screen is up. */
void rauto_leave_follow(void);
/** @brief End the run now as @p why and do what that asks.  A disarm while
 *         the run switches the bench is its own and ends nothing.
 *  @param why the reason */
void rauto_end_now(servo_test_abort_t why);
/** @brief The meter changed under the run.
 *  @param id the meter now  @param changes its change count */
void rauto_meter_follow(servo_source_id_t id, uint32_t changes);
/** @brief A reading of the angle source, for the run's gaps and its still
 *         time.  @param e the reading */
void rauto_angle(const servo_test_enc_t *e);
/** @brief The supply's reading, held to what it was at the start outside
 *         the programmer's sessions.  @param r the reading */
void rauto_supply(const servo_test_reading_t *r);

#ifdef __cplusplus
}
#endif
