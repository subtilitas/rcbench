/**
 * @file
 * @brief LAYOUT: the connectors of a board with soldered pins, each
 *        position with its role, its GPIO, the net on its + and what it
 *        carries now.
 *
 * A SETUP tile of its own on a board whose pins are soldered (the rbmod
 * carrier).
 * It draws what the board table says (outbind_roles_t's connectors) and
 * the binding OUTPUTS holds; it drives nothing and takes no tap.  On a
 * board without soldered pins it says so.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LAYOUT_SCREEN_H
#define RCBENCH_LAYOUT_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The screen.
 * @return its table, for the router
 */
const ui_screen_t *layout_screen(void);

/** @brief Every framebuffer's picture is stale: draw again. */
void layout_screen_invalidate(void);

/** @brief One connector position as the screen draws it. */
typedef struct {
    char        role[8];   /**< "PWM01", "PROGP04"                        */
    uint8_t     gpio;      /**< its signal pin                            */
    const char *rail;      /**< the net on its +                          */
    char        state[24]; /**< what it carries now: a protocol, PROGRAMMING,
                                NOT READ                                  */
} layout_pos_t;

/**
 * @brief Position @p pos (0 is position 1) of connector @p conn of the
 *        recorded board (ui_board()).
 * @param conn index into the board's connectors
 * @param pos the position, from 0
 * @param out filled on success
 * @return false for a board without soldered pins, a connector or position
 *         it lacks, or a NULL @p out
 */
bool layout_screen_pos(int conn, int pos, layout_pos_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LAYOUT_SCREEN_H */
