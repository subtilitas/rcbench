/**
 * @file
 * @brief The IMU screen (SETUP's IMU tile): the sensor bound to the HC-05,
 *        the scan and its list, the HC-05's version and state, the
 *        calibration hold, ZERO, battery and the live angles.  Fed a view
 *        each frame; its commands are taken by the control task.
 *
 * The view's types name no radio: the link under the controller
 * (imu_hc05.h) converts its devices, errors and version into them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "imu.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Sizes of the view's texts and list, NUL included
 *  @{ */
#define IMU_ADDR_MAX    18u  /**< "00:0C:BF:08:19:29" + NUL */
#define IMU_NAME_MAX    32u  /**< a device's name, 31 bytes + NUL */
#define IMU_VERSION_MAX 24u  /**< the radio's firmware version + NUL */
#define IMU_FOUND_MAX   8u   /**< devices a scan lists */
/** @} */

/** @name imu_dev_t.flags
 *  @{ */
#define IMU_DEV_UUID_18F0 0x01u  /**< advertises the 16-bit UUID 0x18F0 */
#define IMU_DEV_ISSC      0x02u  /**< advertises the ISSC UART service */
/** @} */

/** @brief A device of a scan, whichever radio found it. */
typedef struct {
    char    addr[IMU_ADDR_MAX];  /**< the address as text, colons */
    uint8_t addr_type;           /**< 0 public, 1 random; 0 for a Classic
                                      address */
    char    name[IMU_NAME_MAX];  /**< its name, "" when none came */
    int16_t rssi;                /**< the received signal strength, dBm;
                                      the HC-05's +INQ RSSI word as it came */
    uint8_t flags;               /**< IMU_DEV_* */
} imu_dev_t;

/** @brief Why the link's last operation failed. */
typedef enum {
    IMU_ERR_NONE = 0,     /**< none, or ended by IMU OFF */
    IMU_ERR_NO_AT,        /**< the HC-05 answered AT at no baud */
    IMU_ERR_TIMEOUT,      /**< the HC-05's answer did not come */
    IMU_ERR_REFUSED,      /**< ERROR:(code), or FAIL with code 0 */
    IMU_ERR_INQ_REFUSED,  /**< AT+INQ answered ERROR:(1F) */
    IMU_ERR_PAIR,         /**< AT+PAIR did not end in OK */
    IMU_ERR_NONE_FOUND,   /**< the scan ended with no device */
    IMU_ERR_ARG,          /**< a bad address or PIN */
} imu_err_t;

/** @brief The link's state, as the controller sees it (imu_ctl.h). */
typedef enum {
    IMU_LINK_OFF = 0,     /**< IMU off in SETUP */
    IMU_LINK_NO_MODULE,   /**< PROBE found no HC-05 */
    IMU_LINK_READY,       /**< AT answers; no sensor bound */
    IMU_LINK_SCANNING,
    IMU_LINK_BINDING,
    IMU_LINK_BOUND,       /**< bound; no frames yet */
    IMU_LINK_CONNECTED,   /**< frames within IMU_STALE_MS */
    IMU_LINK_NO_FRAMES,   /**< bound, frames stopped */
} imu_link_t;

/** @brief The accelerometer calibration: none yet, running, done, failed. */
typedef enum {
    IMU_CAL_NONE = 0, IMU_CAL_RUNNING, IMU_CAL_DONE, IMU_CAL_FAILED,
} imu_cal_t;

/** @brief What the screen shows: the panel hands it over each frame. */
typedef struct {
    imu_link_t    link;            /**< the link's state */
    char          version[IMU_VERSION_MAX];  /**< the radio's, "" unknown */
    char          bound_addr[IMU_ADDR_MAX];  /**< "" none, "*" any */
    char          bound_name[IMU_NAME_MAX];  /**< from the scan, "" none */
    uint8_t       found_n;         /**< devices of the last scan */
    imu_dev_t     found[IMU_FOUND_MAX];      /**< those devices */
    imu_err_t     err;             /**< the last operation's */
    uint8_t       err_code;        /**< its ERROR:(hh) */
    bool          inq_refused;     /**< offer CONNECT TO ANY SENSOR */
    imu_cal_t     cal;             /**< the calibration */
    uint32_t      cal_left_ms;     /**< of the 3s hold still */
    imu_reading_t r;               /**< the reading */
} imu_view_t;

/** @brief What the screen asks the panel for. */
typedef enum {
    IMU_CMD_NONE = 0,    /**< nothing */
    IMU_CMD_SCAN,        /**< SCAN */
    IMU_CMD_BIND,        /**< arg: index into found */
    IMU_CMD_ANY,         /**< AT+CMODE=1 */
    IMU_CMD_CALIBRATE,   /**< CALSW 1, 3s, CALSW 0, SAVE */
    IMU_CMD_ZERO,        /**< the current angles as zero */
    IMU_CMD_UNZERO,      /**< forget the zero */
} imu_cmd_kind_t;

/** @brief A command: its kind and its argument. */
typedef struct {
    imu_cmd_kind_t kind;   /**< the command */
    uint8_t        arg;    /**< BIND's index into found */
} imu_cmd_t;

/** @brief The frame's view. @param v The view, copied; NULL does nothing. */
void imu_screen_set(const imu_view_t *v);
/** @brief Take the next command. @param out Receives it.
 *  @return false when there is none. */
bool imu_screen_take(imu_cmd_t *out);
/** @brief The IMU screen, for the router. @return The screen. */
const ui_screen_t *imu_screen(void);
/** @brief Repaint on the next frame. */
void imu_screen_invalidate(void);
/* --- for the tests: rows by index, the list, the hold button --- */
/** @brief A control of the screen. @param row Its index (imu_screen_int.h).
 *  @return Its rectangle in the body; empty for none. */
gfx_rect_t imu_screen_row_rect(int row);
/** @brief Row @p i of the found list. @param i 0 to IMU_FOUND_MAX - 1.
 *  @return Its rectangle in the body; empty past the list. */
gfx_rect_t imu_screen_choice_rect(int i);
/** @brief The calibration's hold. @return Its rectangle in the body. */
gfx_rect_t imu_screen_hold_rect(void);
/** @brief State line @p i as it reads now. @param i The line.
 *  @return The line; NULL past the last. */
const char *imu_screen_line(int i);    /**< state lines; NULL past the last */

#ifdef __cplusplus
}
#endif
