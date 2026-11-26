/**
 * @file
 * @brief SETUP CODE (SCREEN_SCAN_CODE), from SETUP > SENSORS > SCANNER's
 *        Show setup code and Factory reset: the GM861's "Serial Output"
 *        setup code, the steps that bring a module back to its UART, the
 *        scanner's state, and the factory reset's 2s hold.
 *
 * The code is scanner_codes.h's matrix at SCANNER_CODE_PX a module, dark
 * on white, with SCANNER_CODE_QUIET of white around it: 21 x 12 = 252px
 * and a 348px square in all, in the body's left column.  The module reads
 * it from the screen at 10cm and sends its codes on its UART again.
 *
 * HOLD 2s: FACTORY RESET takes a press while SETUP's Scanner row is ON;
 * held 2s it asks the panel for gm861_factory_reset() (zone 0x00D9 =
 * 0x50, then the sequence from its start).  A release before 2s sends
 * nothing and shows HOLD_HINT_SCN_RESET in the alert band.  BACK and the
 * home tag return to SETUP on SENSORS > SCANNER.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pixels a module, the matrix drawn. */
#define SCANNER_CODE_PX 12
/** @brief White around the matrix, px: 4 modules. */
#define SCANNER_CODE_QUIET 48

/** @brief The controls, for scanner_code_part(). */
typedef enum {
    SCANNER_PART_CODE = 0, /**< the white square with the code */
    SCANNER_PART_HOLD,     /**< HOLD 2s: FACTORY RESET */
    SCANNER_PART_BACK,     /**< BACK */
} scanner_part_t;

/**
 * @brief The screen's table, for the router.
 * @return The table.
 */
const ui_screen_t *scanner_code_screen(void);

/** @brief Open the screen over SETUP. */
void scanner_code_open(void);

/** @brief Draw the screen whole at its next render. */
void scanner_code_invalidate(void);

/**
 * @brief Where a part lies, in the body's coordinates.
 * @param part The part.
 * @return Its rectangle.
 */
gfx_rect_t scanner_code_part(scanner_part_t part);

/**
 * @brief Whether HOLD 2s: FACTORY RESET takes a press now.
 * @return true while SETUP's Scanner row is ON.
 */
bool scanner_code_hold_ready(void);

#ifdef __cplusplus
}
#endif
