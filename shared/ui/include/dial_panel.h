/**
 * @file
 * @brief The frame task's side of the M5Stack Dial: a frame's Dial input
 *        applied to the bench screen on top through the wired knob's paths,
 *        and the screen model built for the stream (dial_stream.h).
 *
 * The Dial reaches an output only as the wired knob does:
 *
 * - its detents go through motor_screen_knob() and servo_screen_knob() as a
 *   fraction of the slider's span, one detent SET_DIAL_THR's throttle or
 *   SET_DIAL_SRV's horn angle; both move nothing while SAFE;
 * - PRESS is motor_screen_dial_press() (throttle 0%) or
 *   servo_screen_dial_press() (CENTER), HOLD motor_screen_dial_hold() or
 *   servo_screen_dial_hold() (DISARM).  Neither arms, and no Dial input is
 *   a touch, so no hold on the panel completes from the Dial.
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
 * @brief A frame's gestures on @p route: each HOLD a DISARM, each PRESS a
 *        throttle 0% or a CENTER when @p press_ok.  A HOLD acts whatever
 *        the frame lost or the router did: a DISARM is never refused.
 * @param route    The screen on top.
 * @param in       The input; NULL does nothing.
 * @param press_ok The frame's turn would be taken: no touch lost, no
 *                 navigation, the same screen as at the frame's start.
 */
void dial_gestures(ui_screen_id_t route, const dial_input_t *in,
                   bool press_ok);

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
 *        SERVO where the knob moves the horn, HOME on the overview, OTHER
 *        with the screen's title everywhere else.  A bench face carries the
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
