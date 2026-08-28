/*
 * The splash, which is the self-test.
 *
 * Not decoration.  Each subsystem reports its own result as it initialises,
 * so a board with no card, a touch controller that did not answer, or a
 * coprocessor speaking a protocol version this panel does not, says so on the
 * way past -- rather than looking mysteriously broken three screens later.
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
    SPLASH_STEP_LINK,     /**< the CAN bus                                   */
    SPLASH_STEP_COPRO,    /**< who answered, and whether we speak its dialect */
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
