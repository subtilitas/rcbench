/**
 * @file panel_bal.h
 * @brief The balance node on the panel: the client of the `blec` task that
 *        runs bal_ctl.h over Bluetooth LE, the frame task's glue to SETUP,
 *        the BALANCE screen and MOTOR & ESC > BALANCE (node_balance.h), and
 *        the logger task's CSV (comma-separated values) row of each
 *        solution.
 *
 * The node is a measurement: nothing here drives an output, and its loss
 * is no error.  A scan is refused while ARMED and a running one stops at
 * the switch to ARMED, as the Dial's.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"
#include "settings.h"

/** @brief The CSV file of the solutions, under CARD_BALANCE. */
#define PANEL_BAL_CSV "BALANCE.CSV"

/**
 * @brief Set up the controller and its queues and hand it SETUP's values;
 *        before panel_blec_start().
 */
void panel_bal_start(void);

/**
 * @brief `blec` task: SETUP's values and commands, the Bluetooth state and
 *        the frame's blade count and rpm to the controller.
 * @param now The clock, ms.
 */
void panel_bal_blec_poll(uint32_t now);

/**
 * @brief `blec` task: one host stack event the hub routed to the node.
 * @param e   The event.
 * @param now The clock, ms.
 */
void panel_bal_blec_event(const bleu_evt_t *e, uint32_t now);

/**
 * @brief `blec` task: the controller's next action.
 * @param now The clock, ms.
 * @param out Receives it.
 * @return true when @p out holds one.
 */
bool panel_bal_blec_step(uint32_t now, bleu_act_t *out);

/**
 * @brief `blec` task: the view and the last report, published for the
 *        frame task.
 * @param now The clock, ms.
 */
void panel_bal_blec_publish(uint32_t now);

/**
 * @brief A setting changed: SET_BNODE_EN goes to the controller.
 * @param id The setting.
 */
void panel_bal_settings_changed(setting_id_t id);

/**
 * @brief A text setting changed: the bound node goes to the controller.
 * @param id The text.
 */
void panel_bal_text_changed(setting_text_id_t id);

/**
 * @brief Frame task, each frame: the view to SETUP, a bind kept, SETUP's
 *        commands to the controller (a scan refused while ARMED), the
 *        captures' context to node_balance_context(), the report to
 *        node_balance_feed(), the blade count and the rpm to the
 *        controller, a solution's CSV row to the logger.
 * @param armed  The bench is ARMED or an arm waits.
 * @param blades The BALANCE screen's blade count.
 * @param rpm    The bench's rpm, 0 when not known.
 */
void panel_bal_frame(bool armed, int blades, uint32_t rpm);

/**
 * @brief Control task: ARMED or an arm waiting, for the scan's stop.
 * @param live The bench is live.
 */
void panel_bal_set_live(bool live);

/**
 * @brief Logger task: the CSV rows waiting, appended to
 *        CARD_BALANCE/PANEL_BAL_CSV with its header in a new file.  A row
 *        without a card is dropped with a console line.
 */
void panel_bal_card_serve(void);
