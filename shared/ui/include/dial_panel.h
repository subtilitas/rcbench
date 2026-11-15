/**
 * @file
 * @brief The frame task's side of the M5Stack Dial: a frame's Dial input
 *        applied to the bench screen on top through the wired knob's paths,
 *        and the screen model built for the stream (dial_stream.h).
 *
 * On the overview, SETUP, SERVO's hub and OUTPUTS' board view the Dial
 * navigates instead (ui_focus.h, dial_navigate()): the turn moves the focus,
 * PRESS taps the focused item, HOLD is BACK.  Never while ARMED.
 *
 * The Dial reaches an output only as the wired knob does:
 *
 * - its detents go through motor_screen_knob() and servo_screen_knob() as a
 *   fraction of the slider's span, one detent SET_DIAL_THR's throttle or
 *   SET_DIAL_SRV's horn angle; both move nothing while SAFE;
 * - PRESS is motor_screen_dial_press() (throttle 0%) or
 *   servo_screen_dial_press() (CENTER), HOLD motor_screen_dial_hold() or
 *   servo_screen_dial_hold() (DISARM).  Neither arms.  Navigation's SELECT
 *   is a tap of one call (ui_focus.h), so no hold on the panel completes
 *   from the Dial.
 *
 * Pure C: the panel's frame (firmware/panel/main/panel_render.c) calls it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bench_state.h"
#include "dial_ctl.h"
#include "dial_stream.h"
#include "screen_dim.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The fraction of the slider's span @p detents move on @p route:
 *        MOTOR & ESC's throttle span is 100%, SERVO's horn span twice its
 *        travel.
 * @param route      The screen the detents were turned on.
 * @param detents    The detents, clockwise positive.
 * @param thr_tenths A detent's throttle, 0.1%.
 * @param srv_tenths A detent's horn angle, 0.1°.
 * @return The fraction; 0 on every other screen.
 */
float dial_span(ui_screen_id_t route, int32_t detents, uint8_t thr_tenths,
                uint8_t srv_tenths);

/**
 * @brief Whether this frame's knob input reaches the screen: only a frame
 *        with input asks the router (ui_router_take_knob()), so a frame
 *        without one leaves an open help bubble open.  A HOLD is no input
 *        here: it acts whatever this returns (dial_gestures()).
 * @param knob_steps The wired knob's detents this frame.
 * @param in         The Dial's input this frame; NULL is none.
 * @return true when there was input and the router hands it on.
 */
bool dial_knob_take(int32_t knob_steps, const dial_input_t *in);

/**
 * @brief A frame's gestures on @p route: each HOLD a DISARM, each PRESS a
 *        throttle 0% or a CENTER when @p press_ok.  A HOLD acts whatever
 *        the frame lost or the router did: a DISARM is never refused.
 * @param route    The screen on top.
 * @param in       The input; NULL does nothing.
 * @param press_ok The frame's turn would be taken: no touch lost, no
 *                 navigation, the same screen as at the frame's start.
 * @return true when a HOLD disarmed or ended a sequence, or a PRESS moved
 *         the output.
 */
bool dial_gestures(ui_screen_id_t route, const dial_input_t *in,
                   bool press_ok);

/**
 * @brief A frame's input to the focus layer (ui_focus.h) while it
 *        navigates the screen on top: the turn moves the focus or the
 *        value, a PRESS is SELECT, a HOLD is dial_gestures()' DISARM where
 *        one acts and BACK where none does.  Off a navigated screen it
 *        does nothing and the bench path takes the input.
 * @param route  The screen the frame began on.
 * @param in     The input; NULL does nothing.
 * @param steady No touch lost, no navigation, the same screen as at the
 *               frame's start: BACK waits for such a frame.
 * @param taken  steady, and the router hands the frame's input on
 *               (dial_knob_take()): the turn and PRESS wait for it.
 * @return true when the focus layer navigates: the bench path must leave
 *         the Dial's input alone.
 */
bool dial_navigate(ui_screen_id_t route, const dial_input_t *in,
                   bool steady, bool taken);

/**
 * @brief Dim after's rule for the Dial's input (screen_dim.h): any turn,
 *        PRESS or HOLD wakes the backlight.  On a dim panel and an idle
 *        bench (screen_dim_idle()) the input that woke it is dropped, as a
 *        touch that wakes it is.  It acts on a bench that is not idle, so a
 *        PRESS for 0% or CENTER and the turn on an ARMED bench are never
 *        swallowed, and a HOLD on MOTOR & ESC or a SERVO screen always
 *        acts: its DISARM or the end of a sequence is never refused.
 * @param d      Dim after's state.
 * @param in     The frame's Dial input; NULL is none.
 * @param route  The screen on top.
 * @param facts  The frame's facts of the bench.
 * @param now_ms The clock.
 * @return The input that acts: @p in, or none.
 */
dial_input_t dial_dim_filter(screen_dim_t *d, const dial_input_t *in,
                             ui_screen_id_t route, const shot_facts_t *facts,
                             uint32_t now_ms);

/** @brief What the model is built from, besides the screens' getters. */
typedef struct {
    ui_screen_id_t       route;      /**< the screen on top */
    const char          *title;      /**< its band title */
    uint8_t              lang;       /**< DK_LANG_* */
    bool                 armed;      /**< the bench is ARMED */
    bool                 motor_armed; /**< the throttle outputs are */
    bool                 servo_armed; /**< the servo outputs are */
    bool                 fault;      /**< a fault is shown in the band */
    uint16_t             run_s;      /**< the run clock, s */
    const bench_state_t *bench;      /**< the readings; NULL none */
    bool                 supply_on;  /**< SUPPLY's output is on: its set-up
                                          gives the full scales */
    uint8_t              thr_tenths; /**< a detent's throttle, 0.1% */
    uint8_t              srv_tenths; /**< a detent's horn angle, 0.1° */
} dial_model_in_t;

/** @brief The builder's state between frames: the value on entry to a
 *         face, VALUE's was. */
typedef struct {
    uint8_t face;  /**< the face of the last build */
    int32_t was;   /**< the value when that face was entered */
} dial_builder_t;

/**
 * @brief The screen model of this frame: MOTOR on MOTOR & ESC's LIVE,
 *        SERVO where the knob moves the horn; where the focus layer
 *        navigates (ui_focus.h) VALUE with the setting's name as the title
 *        while a value is set, else NAV with the focused item, the items
 *        either side and its place in the list (HOME on the overview, with
 *        the same rows); HOME on the overview, OTHER with the screen's
 *        title everywhere else.  A bench face carries the
 *        knob's value (VALUE_META, VALUE), its readings (LIVE) and their
 *        full scales (SCALE): the speed the run's peak, else the plot's;
 *        the voltage and the current SUPPLY's Voltage max and Current trip
 *        while its output is on and they are set, else the plot's; the
 *        servo's travel and pulse ends the profile's; the rail's SUPPLY's
 *        while its output is on, else 0 (not known).
 * @param b   The builder's state.
 * @param in  The inputs.
 * @param out Receives the model.
 */
void dial_model_build(dial_builder_t *b, const dial_model_in_t *in,
                      dial_model_t *out);

#ifdef __cplusplus
}
#endif
