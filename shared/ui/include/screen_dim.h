/**
 * @file
 * @brief Dim after: when the panel's backlight dims, what wakes it and which
 *        touch events a wake keeps from the screens.
 *
 * The backlight drops to SCREEN_DIM_PCT after SETUP > PANEL's Dim after
 * (SET_DIM_AFTER, minutes) without a touch event, and only while the bench
 * is idle as the screenshot popup's gate decides it (shot_gate.h): SAFE, no
 * output on, no session, run, update, FAULT bit or recording.  FAULT 01,
 * LINK_FAULT_LINK_SILENT, does not count here (shot_facts_t::no_lit_fault):
 * the IOmcu reports it after a restart of the panel, and it stands until
 * the next ARM.  Every other FAULT bit keeps the screen lit, and the band's
 * FAULT chip shows every bit.  One fact of the gate does not count here
 * either: a line on its way to the card's fault log
 * (shot_facts_t::no_card_fault).  The panel's link report writes one every
 * 60s while the link is up and every 5s while it is down, and the gate
 * waits for it only so that the screenshot and the line do not share the
 * card (screen_dim_idle()).  0 minutes never dims.  A frame that finds the
 * bench not idle restores SCREEN_FULL_PCT at once and starts the minutes
 * again, so an output that comes on is never driven behind a dim screen.
 *
 * Any touch event restores SCREEN_FULL_PCT and starts the minutes again.
 * A press that comes down on a dim screen only wakes it: that contact's
 * DOWN, MOVEs and UP reach no screen, so the press cannot act on a control
 * the operator could not see.  A press on the band's STOP is the exception
 * and always goes on: the control task has already applied it, and the
 * router latches it and consumes the control task's marker for it
 * (panel_control.c).  So is a press in a frame whose facts say the bench
 * is not idle: the screen it lands on may drive an output, and its OFF,
 * DISARM or ABORT acts at the first press.  A contact that came down
 * before the screen dimmed, and every press after the wake, go on as
 * usual.
 *
 * The firmware calls screen_dim_touch() for each event before the router
 * and sets the backlight to what screen_dim_step() returns.  Pure C, no
 * ESP-IDF (Espressif Internet-of-Things Development Framework).  Render
 * task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shot_gate.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The backlight's PWM (pulse-width modulation) duty awake, %. */
#define SCREEN_FULL_PCT 100u
/** @brief The backlight's PWM duty dimmed, %.  The light it gives is not
 *         measured. */
#define SCREEN_DIM_PCT  20u
/** @brief The milliseconds of one minute of Dim after. */
#define SCREEN_DIM_MS_PER_MIN 60000u

/** @brief What last started the minutes again, for the console's line
 *         (screen_dim_line()). */
typedef enum {
    SCREEN_DIM_WHY_START = 0, /**< screen_dim_init(), or nothing since     */
    SCREEN_DIM_WHY_TOUCH,     /**< a touch event                           */
    SCREEN_DIM_WHY_BUSY,      /**< a frame that found the bench not idle   */
    SCREEN_DIM_WHY_OFF,       /**< a frame with Dim after at 0 minutes     */
} screen_dim_why_t;

/** @brief The state of the rule. */
typedef struct {
    uint32_t last_ms;                    /**< the last touch, or the last
                                              frame that kept it awake     */
    bool     dimmed;                     /**< the backlight is dim         */
    uint8_t  held[TOUCH_MAX_POINTS];     /**< contacts whose press woke it */
    uint8_t  held_n;                     /**< entries of held in use       */
    uint8_t  why;                        /**< screen_dim_why_t of last_ms  */
    uint8_t  why_type;                   /**< WHY_TOUCH: touch_event_type_t */
    uint8_t  why_id;                     /**< WHY_TOUCH: the contact's id  */
    uint8_t  why_wait;                   /**< WHY_BUSY: the shot_wait_t of
                                              screen_dim_idle()'s gate     */
} screen_dim_t;

/** @brief Bytes screen_dim_line() needs at most, the terminator
 *         included. */
#define SCREEN_DIM_LINE_MAX 96u

/**
 * @brief Whether the bench is idle for Dim after: the screenshot gate's
 *        facts with a fault log line on its way to the card counted as
 *        none.
 * @param facts  this frame's facts; NULL is a bench that is not idle
 * @return true when the minutes may run
 */
bool screen_dim_idle(const shot_facts_t *facts);

/**
 * @brief The first fact that keeps Dim after's minutes from running.
 * @param facts  this frame's facts; NULL waits as for no link
 * @return SHOT_IDLE, or the gate's first fact, never SHOT_WAIT_CARD_FAULT
 */
shot_wait_t screen_dim_wait(const shot_facts_t *facts);

/**
 * @brief The console's line of Dim after's state, without a newline.
 *
 * "dim: after 5min awake, 37s since touch UP id 0x81 injector": the
 * setting, dim or awake, the whole seconds since the minutes last started,
 * and what started them: "start", "touch <DOWN|MOVE|UP> id 0x<id>
 * <glass|injector>" (an id with TOUCH_MERGE_INJECT_ID set is the touch
 * injector's beside the glass), "busy <fact>" (shot_wait_name()) or "off"
 * (0 minutes).
 *
 * @param d          the state
 * @param now_ms     the clock, ms
 * @param dim_after  SET_DIM_AFTER, minutes
 * @param out        takes the line; at least SCREEN_DIM_LINE_MAX bytes
 *                   hold every line
 * @param n          bytes of @p out
 * @return the bytes written, the terminator not counted; 0 with @p d or
 *         @p out NULL or @p n 0
 */
size_t screen_dim_line(const screen_dim_t *d, uint32_t now_ms, int dim_after,
                       char *out, size_t n);

/**
 * @brief Awake, nothing held, the minutes counted from @p now_ms.
 * @param d       the state
 * @param now_ms  the clock, ms
 */
void screen_dim_init(screen_dim_t *d, uint32_t now_ms);

/**
 * @brief Once a frame, after the frame's touch: the backlight's duty.
 * @param d          the state
 * @param now_ms     the clock, ms; wraps after 2^32ms
 * @param dim_after  SET_DIM_AFTER, minutes; 0 or less never dims
 * @param facts      this frame's facts of the screenshot gate; NULL is
 *                   a bench that is not idle
 * @return SCREEN_DIM_PCT or SCREEN_FULL_PCT
 */
unsigned screen_dim_step(screen_dim_t *d, uint32_t now_ms, int dim_after,
                         const shot_facts_t *facts);

/**
 * @brief One touch event, before the router: wakes the screen and says
 *        whether the event goes on.
 * @param d          the state
 * @param evt        the event; NULL goes nowhere and changes nothing
 * @param stop_live  a STOP is drawn to press (ui_router_stop_live())
 * @param facts      this frame's facts of the screenshot gate; NULL is a
 *                   bench that is not idle
 * @param now_ms     the clock, ms
 * @return true when the router takes the event; false for a press that
 *         came down on a dim screen off STOP on an idle bench, its MOVEs
 *         and its UP
 */
bool screen_dim_touch(screen_dim_t *d, const touch_event_t *evt,
                      bool stop_live, const shot_facts_t *facts,
                      uint32_t now_ms);

/**
 * @brief Whether the backlight is dim.
 * @param d  the state
 * @return true between the step that dimmed it and the next wake
 */
bool screen_dim_is_dim(const screen_dim_t *d);

#ifdef __cplusplus
}
#endif
