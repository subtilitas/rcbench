/**
 * @file
 * @brief The IMU screen's guide pages (ui_help.h): the left column's
 *        controls with the dock on the right, then the card and the
 *        footer with the dock on the left.
 *
 * The first page marks SCAN FOR SENSORS, ZERO and CLEAR ZERO in the live
 * half, x 0 to 528.  The second marks the card's sensor lines, the angles
 * and the rate line in the live half, x 496 to 1024, and the footer's
 * HOLD 2s: CALIBRATE and OPTIONS, which it marks where they show.  The
 * screen drives no output, so both pages show while ARMED.  The found
 * list has no pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "ui_help.h"
#include "ui_text.h"

#include "imu_screen_int.h"

/** @brief The pages the main view offers. */
#define GUIDE_PAGES 2

/**
 * @brief Card lines @p a to @p b as one rect.
 * @param a The first line, IMS_L_*.
 * @param b The last line, IMS_L_*.
 * @return Both and the lines between them.
 */
static gfx_rect_t lines(int a, int b)
{
    const gfx_rect_t ra = imu_screen_line_rect(a);
    const gfx_rect_t rb = imu_screen_line_rect(b);
    return gfx_rect_make(ra.x, ra.y, ra.w, rb.y + rb.h - ra.y);
}

/**
 * @brief The left column's page: SCAN FOR SENSORS, ZERO and CLEAR ZERO.
 * @param p The page.
 */
static void ctl_page(ui_help_page_t *p)
{
    p->title = TX_HG_IM_TITLE_CTL;
    p->side = UI_HELP_DOCK_RIGHT;
    (void)ui_help_add(p, TR(IMU_SCAN), TX_HG_IM_SCAN,
                      imu_screen_row_rect(IMS_ROW_SCAN));
    (void)ui_help_add(p, TR(IMU_ZERO), TX_HG_IM_ZERO,
                      imu_screen_row_rect(IMS_ROW_ZERO));
    (void)ui_help_add(p, TR(IMU_UNZERO), TX_HG_IM_UNZERO,
                      imu_screen_row_rect(IMS_ROW_UNZERO));
}

/**
 * @brief The card's and the footer's page: the sensor lines, the angles,
 *        the rate line, the calibration's hold and OPTIONS.
 * @param p The page.
 */
static void read_page(ui_help_page_t *p)
{
    p->title = TX_HG_IM_TITLE_READ;
    p->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add_at(p, "SENSOR", TX_HG_IM_CARD,
                         lines(IMS_L_SENSOR, IMS_L_STATE), UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, "X Y Z", TX_HG_IM_ANGLES, lines(IMS_L_X, IMS_L_Z),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, "RATE", TX_HG_IM_RATE,
                         lines(IMS_L_RATE, IMS_L_BATTERY), UI_HELP_AT_RIGHT);
    (void)ui_help_add(p, TR(IMU_CAL_HOLD), TX_HG_IM_CAL,
                      imu_screen_hold_rect());
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_IM_OPTIONS,
                      imu_screen_row_rect(IMS_ROW_SETTINGS));
}

int imu_help_count(void)
{
    return ims.list ? 0 : GUIDE_PAGES;
}

bool imu_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= imu_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    if (i == 0) {
        ctl_page(out);
    } else {
        read_page(out);
    }
    return true;
}
