/*
 * The feature menu: eight tiles, five of them live.
 *
 * You pick the thing on the bench, not the capability.  The catalogue has
 * sixty-odd entries across measure, drive, listen, program and compute -- a
 * menu of capabilities would be a filing system, and a workshop tool is not
 * one.  A tile is a physical object you have in front of you and something you
 * want to know about it.
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
