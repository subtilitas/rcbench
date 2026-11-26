/**
 * @file panel_print.h
 * @brief The label printer on the panel: the client of the `blec` task
 *        (panel_blec.h) that runs the controller (print_ctl.h) over
 *        Bluetooth LE (panel_ble.h), and the frame task's side of it.
 *
 * The `blec` task owns the controller, through the panel_print_blec_*()
 * functions.  The frame task hands it the requests of print_ctl_label(),
 * print_ctl_test(), print_ctl_query() and print_ctl_abort(), and the
 * console its own, through a queue; SETUP's printer address and name go
 * the same way.  The view comes back under a spinlock (print_ctl_view()).
 * The heap block of a label (print_ctl_block_want()) comes from PSRAM, or
 * internal RAM when PSRAM has none, and goes back at the job's end.  The
 * first request starts Bluetooth when it has not started.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"
#include "settings.h"

/**
 * @brief Set up the controller and its queue, and hand it SETUP's printer.
 *        Once, at start-up, after the settings are loaded and before
 *        panel_blec_start().
 */
void panel_print_start(void);

/**
 * @brief The `blec` task's turn, first: requests, the printer's address,
 *        the panel's Bluetooth state, the bench and the device update to
 *        the controller, its poll, and the heap block it asks for or hands
 *        back.  Nothing before panel_print_start().
 * @param now The clock, ms.
 */
void panel_print_blec_poll(uint32_t now);

/**
 * @brief An event of the host stack the hub routes to the printer.
 * @param e   The event.
 * @param now The clock, ms.
 */
void panel_print_blec_event(const bleu_evt_t *e, uint32_t now);

/**
 * @brief The controller's next action.
 * @param now The clock, ms.
 * @param out Receives the action.
 * @return true when @p out holds one.
 */
bool panel_print_blec_step(uint32_t now, bleu_act_t *out);

/**
 * @brief The bytes of the printer's long write (BLEU_ACT_WRITE_EXT).
 * @param a The action.
 * @return The bytes, NULL none.
 */
const uint8_t *panel_print_blec_ext(const bleu_act_t *a);

/**
 * @brief The `blec` task's turn, last: the view published, and each change
 *        of phase on the console with its times.
 * @param now The clock, ms.
 */
void panel_print_blec_publish(uint32_t now);

/**
 * @brief Whether a request runs: a device update's start is refused.  Any
 *        task.
 * @return true from a request's start to its end.
 */
bool panel_print_busy(void);

/**
 * @brief The frame task's turn: the waiting request to the `blec` task,
 *        the published view to print_ctl_show(), a console bind kept in
 *        SETUP.
 */
void panel_print_frame(void);

/**
 * @brief A string changed: the printer's address and name go to the task.
 * @param id The string.
 */
void panel_print_text_changed(setting_text_id_t id);

/**
 * @brief A console line that starts with `print`, on the console's task:
 *        `print` (the view), `print test`, `print id ID`, `print query`,
 *        `print stop`, `print bind ADDRESS`, `print unbind`.
 * @param line The line.
 */
void panel_print_console(const char *line);
