/*
 * The programmer screen: one renderer for several protocols.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

/** Drop the cached chrome, so the next frame repaints it. */
void programmer_invalidate(void);

const ui_screen_t *programmer_screen(void);

/** Which protocol is selected, for the application and for tests. */
int  programmer_screen_protocol(void);
/** True after a device has answered on the selected protocol. */
bool programmer_screen_connected(void);
/** The staged value of a parameter, or -1 if there is no such row. */
int  programmer_screen_value(int param);
/** How many staged values differ from what was read off the device. */
int  programmer_screen_dirty(void);
