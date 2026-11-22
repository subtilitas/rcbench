/**
 * @file
 * @brief The remote servo node's catalogue and roles, for the board table in
 *        out_bind.c.
 *
 * Private to shared/outputs: everything else reaches the board through
 * outbind_board() and outbind_roles(), and its own pins through rsn_pins.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_BOARD_RSN_H
#define RCBENCH_OUT_BOARD_RSN_H

#include "out_bind.h"

/** @brief Entries in outbind_rsn_pins: the role pins of the Core2350B. */
#define OUTBIND_RSN_PIN_COUNT 27u

/** @brief The node's catalogue, in GPIO order. */
extern const outbind_pin_t outbind_rsn_pins[OUTBIND_RSN_PIN_COUNT];

/** @brief The pin of each role on the node. */
extern const outbind_roles_t outbind_rsn_roles;

#endif /* RCBENCH_OUT_BOARD_RSN_H */
