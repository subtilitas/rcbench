/**
 * @file
 * @brief The IMU screen's state and geometry, shared by imu_screen.c (the
 *        model, the touches and the texts) and imu_screen_draw.c (the
 *        drawing).  Private to those two files.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imu_screen.h"
#include "ui_layout.h"
#include "ui_widgets.h"

/*
 * The main view, in the body below the band, 1024 x 540px:
 * The controls on the left, 460 x 56px at a 64px pitch from y 16: SCAN FOR
 * SENSORS, CONNECT TO ANY SENSOR (only after the HC-05 refused a scan),
 * the calibration's hold, ZERO and CLEAR ZERO side by side, 222px each,
 * and IMU SETTINGS.  Under them the calibration's line and the last
 * operation's, in the label face, wrapped to 2 lines each.  The card on
 * the right, x 484 to 1016, y 16 to 488: the sensor, the HC-05, the state,
 * the angles in the head face, the rate, the age and the battery.
 */
#define IMS_BX      16   /**< the controls' left edge, px */
#define IMS_BW      460  /**< their width, px */
#define IMS_BH      56   /**< their height, px */
#define IMS_BPITCH  64   /**< the step from one to the next, px */
#define IMS_RY      16   /**< the first control's top and the card's, px */
#define IMS_HALF_W  222  /**< ZERO's and CLEAR ZERO's width, px */
#define IMS_LINE_Y  344  /**< the calibration's line, px */
#define IMS_ERR_Y   404  /**< the last operation's line, px */
#define IMS_LPITCH  24   /**< the label face's line pitch, px */
#define IMS_RX      484  /**< the card's left edge, px */
#define IMS_RW      532  /**< its width, px */
#define IMS_RH      472  /**< its height, px */
#define IMS_RPAD    16   /**< its inner padding, px */
#define IMS_PITCH   28   /**< a body face line's pitch, px */
#define IMS_APITCH  40   /**< an angle line's pitch, px */

/*
 * The found list: the heading at y 12, then up to 8 rows of 640 x 48px at
 * a 52px pitch from y 44 on the choice list's ground; SCAN AGAIN and
 * CONNECT TO ANY SENSOR on the right, 328 x 56px, and CLOSE at the foot.
 */
#define IMS_LX      16   /**< the rows' left edge, px */
#define IMS_LW      640  /**< a row's width, px */
#define IMS_LH      48   /**< a row's height, px */
#define IMS_LP      52   /**< the rows' pitch, px */
#define IMS_LY      44   /**< the first row's top, px */
#define IMS_HEAD_Y  12   /**< the heading's top, px */
#define IMS_SX      680  /**< the list's buttons' left edge, px */
#define IMS_SW      328  /**< their width, px */
/** @brief The body row 4px above the alert band: every control ends over it. */
#define IMS_FLOOR   (UI_ALERT_Y - UI_BAND_H - 4)

/** @brief The left column's rows, imu_screen_row_rect()'s index. */
enum {
    IMS_ROW_SCAN = 0,   /**< SCAN FOR SENSORS */
    IMS_ROW_ANY,        /**< CONNECT TO ANY SENSOR */
    IMS_ROW_CAL,        /**< the calibration's hold */
    IMS_ROW_ZERO,       /**< ZERO */
    IMS_ROW_UNZERO,     /**< CLEAR ZERO, beside it */
    IMS_ROW_SETTINGS,   /**< IMU SETTINGS */
    IMS_ROW_RESCAN,     /**< the list's SCAN AGAIN */
    IMS_ROW_LIST_ANY,   /**< the list's CONNECT TO ANY SENSOR */
    IMS_ROW_CLOSE,      /**< the list's CLOSE */
    IMS_ROW_COUNT
};

/** @brief What a press landed on: a row, or IMS_HIT_DEV0 + a device. */
enum { IMS_HIT_NONE = -1, IMS_HIT_DEV0 = 100 };

/** @brief The lines of the card, imu_screen_line()'s index. */
enum {
    IMS_L_SENSOR = 0,   /**< SENSOR: the bound name */
    IMS_L_ADDR,         /**< its address */
    IMS_L_HC05,         /**< HC-05: the version */
    IMS_L_STATE,        /**< the link state */
    IMS_L_X,            /**< the angles from the zero */
    IMS_L_Y,
    IMS_L_Z,
    IMS_L_RATE,         /**< RATE and AGE */
    IMS_L_BATTERY,      /**< BATTERY, or "" above IMS_BATTERY_MAX_V */
    IMS_L_CAL,          /**< the calibration's line, left column */
    IMS_L_ERR,          /**< the last operation's line, left column */
    IMS_L_HEAD,         /**< the found list's heading */
    IMS_L_COUNT
};

/** @brief BATTERY is hidden above this: 0xFFFF reads 655.35V. */
#define IMS_BATTERY_MAX_V 6.0f
/** @brief Commands the screen holds until the panel takes them. */
#define IMS_CMDS 4

/** @brief The screen's state. */
typedef struct {
    imu_view_t v;            /**< what the panel last handed over */
    bool       list;         /**< the found list is open */
    bool       have_press;   /**< a contact pressed a control */
    uint8_t    press_id;     /**< that contact */
    int        hit;          /**< IMS_HIT_NONE, a row or IMS_HIT_DEV0 + i */
    ui_hold_t  hold;         /**< CALIBRATE ACCELEROMETER */
    unsigned   drawn_mask;   /**< framebuffers drawn since the last change */
    imu_cmd_t  cmds[IMS_CMDS];  /**< commands not yet taken, oldest first */
    uint8_t    cmd_n;        /**< how many */
} ims_state_t;

/** @brief The state, imu_screen.c's. */
extern ims_state_t ims;

/** @brief Whether row @p row stands now and takes a press.
 *  @param row IMS_ROW_*.  @return Whether it does. */
bool ims_row_enabled(int row);
/** @brief Whether row @p row stands now. @param row IMS_ROW_*.
 *  @return Whether it does. */
bool ims_row_shown(int row);
/**
 * @brief Card line or left-column line @p i as it reads now.
 * @param i   IMS_L_*.
 * @param buf Takes the line.
 * @param n   Its size, bytes.
 * @param ink Takes its colour; NULL takes none.
 * @return @p buf.
 */
const char *ims_line(int i, char *buf, size_t n, gfx_color_t *ink);
/**
 * @brief Draw the screen.
 * @param c            The body's canvas.
 * @param buffer_index The framebuffer.
 */
void ims_render(gfx_canvas_t *c, int buffer_index);
