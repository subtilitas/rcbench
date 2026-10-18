/*
 * Where a finger lands on the SERVO workbench, in the screen's own
 * coordinates (the body under the band), mirrored from ui_workbench.h,
 * servo_screen_int.h and servo_workbench.c.  If the layout moves these move
 * with it.  Taken by servo_rig.h and render_screen.c.
 *
 *   - the strip: LIVE, TEST, SETTINGS from x 9, 112 px each, 4 px apart,
 *     y 6 to 30; PROGRAM in the same row over the right card;
 *   - SETTINGS' own page tabs OUTPUT, SWEEP, DUT from x 16, 104 px each,
 *     y 46 to 72, and their rows from y 82, 40 px apart;
 *   - TEST's two run cards from y 46 and 224, each with SETTINGS top right
 *     and its START 56 px down; a run's settings' rows from y 68;
 *   - the TARGET row's value box on the right card, x 578 to 782,
 *     y 326 to 348.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#define SV_TAB_Y           18
#define SV_TAB_LIVE_X      65
#define SV_TAB_TEST_X      181
#define SV_TAB_SETTINGS_X  297
#define SV_PROGRAM_X       740

#define SV_PAGE_X(i)       (68 + 108 * (i))
#define SV_PAGE_Y          59
#define SV_ROW_Y(r)        (98 + 40 * (r))

#define SV_RUN_SET_X       420
#define SV_RUN_SET_Y(k)    (65 + 178 * (k))
#define SV_START_X         146
#define SV_START_Y(k)      (120 + 178 * (k))
#define SV_LIST_ROW_Y(r)   (84 + 40 * (r))

#define SV_TARGET_X        680
#define SV_TARGET_Y        337
