/*
 * The splash: the self-test report.
 *
 * Each subsystem reports its result as it initialises: board, display, touch,
 * storage, settings, link, coprocessor.  A missing card or a coprocessor with
 * another protocol version is reported here.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPLASH_STEP_BOARD = 0,
    SPLASH_STEP_DISPLAY,
    SPLASH_STEP_TOUCH,
    SPLASH_STEP_STORAGE,
    SPLASH_STEP_SETTINGS,
    SPLASH_STEP_LINK,     /**< the CAN (Controller Area Network) bus */
    SPLASH_STEP_IOMCU,    /**< coprocessor identity and protocol version */
    SPLASH_STEP_COUNT
} splash_step_t;

typedef enum {
    SPLASH_PENDING = 0,
    SPLASH_OK,
    SPLASH_WARN,   /**< usable without it: no card, no sensor              */
    SPLASH_FAIL,   /**< the bench will not arm                             */
} splash_result_t;

void splash_screen_set(splash_step_t step, splash_result_t result,
                       const char *detail);
/** True once every step has answered and the hold has expired. */
bool splash_screen_done(void);
void splash_invalidate(void);

const ui_screen_t *splash_screen(void);

#ifdef __cplusplus
}
#endif
