/**
 * @file
 * @brief The rbmod carrier's catalogue and roles, for the board table in
 *        out_bind.c.
 *
 * Private to shared/outputs: everything else reaches the carrier through
 * outbind_board() and outbind_roles().
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_BOARD_RBMOD_H
#define RCBENCH_OUT_BOARD_RBMOD_H

#include "out_bind.h"

/** @brief Entries in outbind_rbmod_pins: the module's 26 header pins. */
#define OUTBIND_RBMOD_PIN_COUNT 26u

/** @brief The carrier's catalogue, in GPIO order. */
extern const outbind_pin_t outbind_rbmod_pins[OUTBIND_RBMOD_PIN_COUNT];

/** @brief The pin of each role on the carrier. */
extern const outbind_roles_t outbind_rbmod_roles;

#endif /* RCBENCH_OUT_BOARD_RBMOD_H */
