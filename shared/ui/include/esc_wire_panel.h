/**
 * @file
 * @brief The render loop's ESC programmer: one model (esc_prog.h), fed from the
 *        control task, with no screen of its own yet.  A held pin counts for ARM
 *        and the runs that arm the bench through prog_reg_servo_holds().
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_WIRE_PANEL_H
#define RCBENCH_ESC_WIRE_PANEL_H

#include <stdbool.h>

#include "esc_prog.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets up the model with the gate its commands are claimed at.
 * @param g The gate.
 */
void esc_wire_panel_gate(esc_prog_gate_t *g);

/**
 * @brief The frame's feed.
 * @param f What the control task published.
 */
void esc_wire_panel_feed(const esc_prog_feed_t *f);

/**
 * @brief The command for the control task, once.
 * @param out Receives it.
 * @return true when there is one.
 */
bool esc_wire_panel_next_cmd(esc_prog_cmd_t *out);

/**
 * @brief Whether the ESC_WIRE page holds a pin, any writer's, or is asked to.
 * @return true then.
 */
bool esc_wire_panel_holds_port(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_WIRE_PANEL_H */
