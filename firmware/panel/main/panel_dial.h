/**
 * @file panel_dial.h
 * @brief The M5Stack Dial on the panel: the client of the `blec` task
 *        (panel_blec.h) that runs the controller (dial_ctl.h) over
 *        Bluetooth LE (panel_ble.h), the frame task's side of it (SETUP >
 *        WIRELESS' view and commands, the bind kept in SETUP, the screen
 *        model for the stream, the turn and the gestures for the knob's
 *        path), and the control task's side (a Dial lost while ARMED is an
 *        error).
 *
 * The `blec` task owns the controller, through the panel_dial_blec_*()
 * functions.  The frame task posts SETUP's values and the commands through
 * a queue, the screen model and its input through s_mux; the control task
 * publishes whether the bench is ARMED and follows the controller's count
 * of losses while ARMED (panel_dial_errors()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"
#include "dial_ctl.h"
#include "dial_panel.h"
#include "settings.h"

/**
 * @brief Set up the controller and its queue, and hand it SETUP's values.
 *        Once, at start-up, after the settings are loaded and before
 *        panel_blec_start().  Bluetooth starts at the first Dial ON.
 */
void panel_dial_start(void);

/**
 * @brief The `blec` task's turn, first: SETUP's values and the commands to
 *        the controller, a first Dial ON starting Bluetooth, the panel's
 *        Bluetooth state, the frame's model.
 * @param now The clock, ms.
 */
void panel_dial_blec_poll(uint32_t now);

/**
 * @brief An event of the host stack the hub routes to the Dial.
 * @param e   The event.
 * @param now The clock, ms.
 */
void panel_dial_blec_event(const bleu_evt_t *e, uint32_t now);

/**
 * @brief The controller's next action, the stream's writes among them.
 * @param now The clock, ms.
 * @param out Receives the action.
 * @return true when @p out holds one.
 */
bool panel_dial_blec_step(uint32_t now, bleu_act_t *out);

/**
 * @brief The `blec` task's turn, last: the view and the input published for
 *        the frame task, a change of the link logged, and once a second
 *        the counters.
 * @param now The clock, ms.
 */
void panel_dial_blec_publish(uint32_t now);

/**
 * @brief A value changed: SET_DIAL_* go to the task.
 * @param id The setting.
 */
void panel_dial_settings_changed(setting_id_t id);

/**
 * @brief A string changed: the bound Dial's address and name go to the
 *        task.
 * @param id The string.
 */
void panel_dial_text_changed(setting_text_id_t id);

/**
 * @brief The frame task's input of this frame, taken once at its start
 *        (knob_take()): detents and gestures since the last take.
 * @return The input; zero before panel_dial_start().
 */
dial_input_t panel_dial_take(void);

/**
 * @brief The frame task's turn: the view to SETUP, a bind kept in SETUP
 *        with a save asked, SETUP's commands to the task, the band's
 *        notices of the link, and this frame's screen model.
 * @param in What the model is built from (dial_model_build()).
 */
void panel_dial_frame(const dial_model_in_t *in);

/**
 * @brief The control task's turn: whether the bench is ARMED or a switch to
 *        ARMED waits, for the loss rule and the scan's refusal.
 * @param live ARMED, or an arm waiting.
 */
void panel_dial_set_live(bool live);

/**
 * @brief Losses while ARMED since start-up, each an error the control task
 *        takes once (dial_faults_follow()).
 * @return The count.
 */
uint32_t panel_dial_errors(void);
