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

/** Whether SUPPLY drives the PD mini; the tile is marked MODELLED while it
 *  does not. */
void overview_screen_set_supply_real(bool real);

#ifdef __cplusplus
}
#endif
