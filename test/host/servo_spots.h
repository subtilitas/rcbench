/*
 * Where a finger lands on the SERVO workbench, in the screen's own
 * coordinates (the body under the band), mirrored from ui_workbench.h,
 * servo_screen_int.h and servo_workbench.c.  If the layout moves these move
 * with it.  Taken by servo_rig.h and render_screen.c.
 *
 *   - the strip: LIVE, TEST, PROGRAM, SETTINGS from x 9, 112 px each, 4 px
 *     apart, y 6 to 30;
 *   - PROGRAM's list (prog_registry.h): rows from x 16 to 484, y 48, 34 px
 *     each and 40 px apart, with no bar over them (9 rows or fewer that
 *     fit); the first row is the KST and Chaservo programmer;
 *   - SETTINGS' own page tabs OUTPUT, SWEEP, DUT from x 16, 104 px each,
 *     y 46 to 72, and their rows from y 82, 40 px apart;
 *   - TEST's two run cards from y 46 and 224, each with SETTINGS top right
 *     and its START 56 px down; a run's settings' rows from y 68, its
 *     CLOSE x 376 to 484, y 41 to 65;
 *   - the right card from SV_RCARD_X, its inner column SV_RC_X and
 *     SV_RC_W; CENTRE, SWEEP and RELEASE a third of it each, y 350 to 382;
 *     the SET row's values and OUTPUT ON at y 224 to 250;
 *   - the TARGET row's value box on the right card, from SV_RC_X + 64 to
 *     the column's end, y 326 to 348.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define SV_TAB_Y           18
#define SV_TAB_LIVE_X      65
#define SV_TAB_TEST_X      181
#define SV_TAB_PROGRAM_X   297
#define SV_TAB_SETTINGS_X  413
#define SV_PROG_ROW_X      250
#define SV_PROG_ROW_Y(r)   (65 + 40 * (r))

#define SV_PAGE_X(i)       (68 + 108 * (i))
#define SV_PAGE_Y          59
#define SV_ROW_Y(r)        (98 + 40 * (r))

#define SV_RUN_SET_X       420
#define SV_RUN_SET_Y(k)    (65 + 178 * (k))
#define SV_RUN_CLOSE_X     420
#define SV_RUN_CLOSE_Y     52
#define SV_START_X         146
#define SV_START_Y(k)      (120 + 178 * (k))
#define SV_LIST_ROW_Y(r)   (84 + 40 * (r))

#define SV_TARGET_X        680
#define SV_TARGET_Y        337

/* The left column the keypad, the keyboard and the programmer cover:
 * OV_X, OV_Y, OV_W and OV_H of servo_screen_int.h. */
#define SV_OV_AREA \
    ((gfx_rect_t){ UI_MARGIN, UI_MARGIN, 488, UI_BODY_H - 2 * UI_MARGIN })

/* RCARD_X, RCARD_W, RC_X and RC_W of servo_screen_int.h. */
#define SV_RCARD_X         (UI_MARGIN + 488 + 8)
#define SV_RCARD_W         (UI_PANEL_W - SV_RCARD_X - UI_MARGIN)
#define SV_RC_X            (SV_RCARD_X + 12)
#define SV_RC_W            (SV_RCARD_W - 24)
/* CENTRE, SWEEP and RELEASE (servo_screen.c), the centre of each. */
#define SV_BTN_Y           (350 + 16)
#define SV_CENTRE_X        (SV_RC_X + (SV_RC_W / 3 - 4) / 2)
#define SV_SWEEP_X         (SV_RC_X + SV_RC_W / 3 + 2 + (SV_RC_W / 3 - 4) / 2)
#define SV_RELEASE_X       (SV_RC_X + 2 * (SV_RC_W / 3) + 4 \
                            + (SV_RC_W - 2 * (SV_RC_W / 3) - 4) / 2)
/* The SET row: the voltage's and the current limit's value boxes, 66 px
 * each from SV_RC_X + 32, 4 px apart, and OUTPUT ON to the column's end. */
#define SV_SUP_Y           237
#define SV_SUP_V_X         (SV_RC_X + 32 + 33)
#define SV_SUP_I_X         (SV_RC_X + 32 + 70 + 33)
#define SV_SUP_OUT_X       ((SV_RC_X + 32 + 140 + SV_RC_X + SV_RC_W) / 2)
