/**
 * @file
 * @brief The splash: the self-test report.
 *
 * Each subsystem reports its result as it initialises: board, display, touch,
 * storage, settings, link, IOmcu.  A missing card or an IOmcu with
 * another protocol version is reported here.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The steps the splash reports, in the order it lists them. */
typedef enum {
    SPLASH_STEP_BOARD = 0,
    SPLASH_STEP_DISPLAY,
    SPLASH_STEP_TOUCH,
    SPLASH_STEP_STORAGE,
    SPLASH_STEP_SETTINGS,
    SPLASH_STEP_LINK,     /**< the CAN (Controller Area Network) bus */
    SPLASH_STEP_IOMCU,    /**< IOmcu identity and protocol version */
    SPLASH_STEP_COUNT
} splash_step_t;

/** @brief A step's result. */
typedef enum {
    SPLASH_PENDING = 0,
    SPLASH_OK,
    SPLASH_WARN,   /**< usable without it: no card, no sensor              */
    SPLASH_FAIL,   /**< the bench will not arm                             */
} splash_result_t;

/**
 * @brief Bytes a step's detail keeps, terminator included.
 *
 * The detail is drawn
 * from x 432 to 24px short of the panel's edge, 47 cells of 12 x 24; a
 * German letter takes two bytes, so the CAN self-test's longest verdict
 * (48 bytes, 46 cells) fits whole.
 */
#define SPLASH_DETAIL_MAX 64

void splash_screen_set(splash_step_t step, splash_result_t result,
                       const char *detail);
/**
 * @brief Whether the splash may hand over.
 * @return true once every step has answered and the hold has expired, or
 *         a tap skipped the hold.
 */
bool splash_screen_done(void);
void splash_invalidate(void);

const ui_screen_t *splash_screen(void);

#ifdef __cplusplus
}
#endif
