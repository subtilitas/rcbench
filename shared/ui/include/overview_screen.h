/*
 * The feature menu: eight tiles, one per object on the bench (motor and ESC
 * (electronic speed controller), servo, receiver, logs, setup, battery,
 * balance, programmer).  A tile carries a SOON badge when its screen does not
 * exist and a MODELLED badge when its hardware is not fitted.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

void overview_invalidate(void);
const ui_screen_t *overview_screen(void);

#ifdef __cplusplus
}
#endif
