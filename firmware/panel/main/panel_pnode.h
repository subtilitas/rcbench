/**
 * @file panel_pnode.h
 * @brief The power node on the panel: the client of the `blec` task
 *        (panel_blec.h) that runs the controller (pnode_ctl.h) over
 *        Bluetooth LE (panel_ble.h), the frame task's side of it (SETUP >
 *        WIRELESS' view and commands, the bind kept in SETUP, the band's
 *        notices), and the control task's side (the node's readings into
 *        the bench's state while INA228 SOURCE is NODE, and a run's
 *        dropout as an error).
 *
 * The `blec` task owns the controller, through the panel_pnode_blec_*()
 * functions.  The frame task posts SETUP's values and the commands through
 * a queue; the control task publishes whether the bench is ARMED, takes
 * the readings (panel_pnode_bench()) and follows the controller's count of
 * dropouts that end a run (panel_pnode_errors()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bench_state.h"
#include "bleuart.h"
#include "pnode_ctl.h"
#include "settings.h"

/**
 * @brief Set up the controller and its queue, and hand it SETUP's values.
 *        Once, at start-up, after the settings are loaded and before
 *        panel_blec_start().  Bluetooth starts at the first POWER NODE ON.
 */
void panel_pnode_start(void);

/**
 * @brief The `blec` task's turn, first: SETUP's values and the commands to
 *        the controller, a first POWER NODE ON starting Bluetooth, the
 *        panel's Bluetooth state.
 * @param now The clock, ms.
 */
void panel_pnode_blec_poll(uint32_t now);

/**
 * @brief An event of the host stack the hub routes to the power node.
 * @param e   The event.
 * @param now The clock, ms.
 */
void panel_pnode_blec_event(const bleu_evt_t *e, uint32_t now);

/**
 * @brief The controller's next action, the RUN write among them.
 * @param now The clock, ms.
 * @param out Receives the action.
 * @return true when @p out holds one.
 */
bool panel_pnode_blec_step(uint32_t now, bleu_act_t *out);

/**
 * @brief The `blec` task's turn, last: the view and the readings published,
 *        a change of the link logged, and once a second the counters.
 * @param now The clock, ms.
 */
void panel_pnode_blec_publish(uint32_t now);

/**
 * @brief The `blec` task's hold on the power node for its update: ON
 *        ends the link and keeps it ended, as POWER NODE OFF does, SETUP's
 *        values kept and no loss counted (pnode_ctl_hold());
 *        OFF gives the link back to SETUP's values.
 * @param on  The hold.
 * @param now The clock, ms.
 */
void panel_pnode_blec_hold(bool on, uint32_t now);

/**
 * @brief What the power node's update needs of the link, in the
 *        `blec` task (pnode_ctl_target()).
 * @param addr    Receives the bound node's address.
 * @param linked  Receives whether the node is connected, its INFO taken
 *                and its version read from a STATUS on this link.
 * @param version Receives that version.
 * @param down    Receives whether no link to the node stands or is under
 *                way.
 * @return false when no node is bound.
 */
bool panel_pnode_blec_target(bleu_addr_t *addr, bool *linked,
                          uint8_t version[3], bool *down);

/**
 * @brief A value changed: SET_PNODE_EN and SET_I228_SRC go to the task.
 * @param id The setting.
 */
void panel_pnode_settings_changed(setting_id_t id);

/**
 * @brief A string changed: the bound node's address and name go to the
 *        task.
 * @param id The string.
 */
void panel_pnode_text_changed(setting_text_id_t id);

/**
 * @brief The frame task's turn: the view to SETUP, a bind kept in SETUP
 *        with a save asked, SETUP's commands to the task, the band's
 *        notices of the link.
 * @param armed The bench is ARMED, or a switch to ARMED waits.
 */
void panel_pnode_frame(bool armed);

/**
 * @brief The control task's turn after each BENCH read: while INA228
 *        SOURCE is NODE, the node's readings over @p b
 *        (pnode_reading_bench()); while WIRED, from_node cleared.
 * @param b The bench's state.
 */
void panel_pnode_bench(bench_state_t *b);

/**
 * @brief The control task's turn: whether the bench is ARMED or a switch to
 *        ARMED waits, for the run, the dropout rule and the scan's refusal,
 *        and whether the run reads BENCH, for the rule.
 * @param live      ARMED, or an arm waiting.
 * @param bench_run The arm is the throttle's: MOTOR & ESC or ESC STICK.
 */
void panel_pnode_set_live(bool live, bool bench_run);

/**
 * @brief The control task's check before a switch to ARMED that reads
 *        BENCH: INA228 SOURCE NODE with POWER NODE OFF or no reading within
 *        100ms refuses it (pnode_arm_refused()).
 * @return true when the arm is refused.
 */
bool panel_pnode_arm_refused(void);

/**
 * @brief Dropouts that ended a run since start-up, each an error the
 *        control task takes once (pnode_faults_follow()).
 * @return The count.
 */
uint32_t panel_pnode_errors(void);
