/**
 * @file
 * @brief The Bluetooth IMU as the panel's screens see it: the view the
 *        panel hands over each frame (the bound sensor, the Bluetooth LE
 *        (BLE) scan and its list, the C6's version, the link state, the
 *        calibration, the battery and the live angles) and the commands
 *        the screens queue for the panel's `blec` task.
 *
 * SETUP > WIRELESS > IMU (wireless_imu.c) shows the view and queues SCAN,
 * BIND, ZERO, CLEAR ZERO and CALIBRATE; SERVO's angle source picker
 * (servo_angle_pick.c) and the READY tags (servo_drive_ready.c) read it
 * and queue SCAN, BIND and ZERO.  The view's types name no radio: the
 * link under the controller (imu_ble.h) converts its devices, errors and
 * version into them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "imu.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Sizes of the view's texts and list, NUL included
 *  @{ */
#define IMU_ADDR_MAX    20u  /**< "00:0C:BF:08:19:29 P" + NUL */
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
    char    addr[IMU_ADDR_MAX];  /**< the address as text, colons, no type */
    uint8_t addr_type;           /**< 0 public, 1 random */
    char    name[IMU_NAME_MAX];  /**< its name, "" when none came */
    int8_t  rssi;                /**< the received signal strength, dBm;
                                      127 unknown */
    uint8_t flags;               /**< IMU_DEV_* */
} imu_dev_t;

/** @brief Why the link's last operation failed. */
typedef enum {
    IMU_ERR_NONE = 0,     /**< none, or ended by IMU OFF */
    IMU_ERR_CONNECT,      /**< a connect, a GATT step or the link failed;
                               the link retries */
    IMU_ERR_NOT_WIT,      /**< the bound device lacks the ISSC UART
                               service: no retry until a new bind */
    IMU_ERR_WRITE,        /**< a sensor command was not taken; the rest
                               held was dropped */
    IMU_ERR_NONE_FOUND,   /**< the scan ended with no sensor listed */
    IMU_ERR_SCAN,         /**< the scan did not run: err_code */
} imu_err_t;

/** @brief The link's state, as the controller sees it (imu_ctl.h). */
typedef enum {
    IMU_LINK_OFF = 0,     /**< IMU off in SETUP */
    IMU_LINK_STARTING,    /**< Bluetooth LE starts: the C6's link, the host */
    IMU_LINK_NO_BT,       /**< the host has no sync with the C6 */
    IMU_LINK_C6_FAILED,   /**< the C6's link failed: until a restart */
    IMU_LINK_READY,       /**< Bluetooth runs; no sensor bound */
    IMU_LINK_SCANNING,    /**< a scan runs */
    IMU_LINK_CONNECTING,  /**< bound: a connect and its GATT steps run */
    IMU_LINK_BOUND,       /**< subscribed, no frame yet; or not a
                               WitMotion sensor */
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
    char          version[IMU_VERSION_MAX];  /**< the C6's, "" unknown */
    char          bound_addr[IMU_ADDR_MAX];  /**< "" none */
    char          bound_name[IMU_NAME_MAX];  /**< from the scan, "" none */
    uint8_t       scan_left_s;     /**< the scan's time left, s; 0 when
                                        none runs */
    bool          scan_done;       /**< a scan ended since IMU ON */
    bool          scan_cut;        /**< that scan was stopped by ARMED or
                                        IMU OFF */
    uint8_t       found_n;         /**< sensors of the last scan */
    imu_dev_t     found[IMU_FOUND_MAX];      /**< those sensors */
    uint16_t      hidden;          /**< other devices the scan heard */
    imu_err_t     err;             /**< the last error */
    uint16_t      err_code;        /**< the host stack's status for it */
    imu_cal_t     cal;             /**< the calibration */
    uint32_t      cal_left_ms;     /**< of the 3s hold still */
    /**
     * @brief Scans the controller refused while ARMED or while an arm
     *        waits (imu_ctl_live()), counted: a change raises
     *        IMU_SCAN_ARMED on the band (imu_view_set()).
     */
    uint16_t      scan_armed;
    imu_reading_t r;               /**< the reading */
} imu_view_t;

/** @brief What the screen asks the panel for. */
typedef enum {
    IMU_CMD_NONE = 0,    /**< nothing */
    IMU_CMD_SCAN,        /**< SCAN */
    IMU_CMD_BIND,        /**< arg: index into found */
    IMU_CMD_CALIBRATE,   /**< CALSW 1, 3s, CALSW 0, SAVE */
    IMU_CMD_ZERO,        /**< the current angles as zero */
    IMU_CMD_UNZERO,      /**< forget the zero */
} imu_cmd_kind_t;

/** @brief A command: its kind and its argument. */
typedef struct {
    imu_cmd_kind_t kind;   /**< the command */
    uint8_t        arg;    /**< BIND's index into found */
} imu_cmd_t;

/**
 * @brief The frame's view.  A calibration that ends says so on the band:
 *        DONE as a notice, FAILED as an alert.
 * @param v The view, copied; NULL does nothing.
 */
void imu_view_set(const imu_view_t *v);
/** @brief Take the next command. @param out Receives it.
 *  @return false when there is none. */
bool imu_view_take(imu_cmd_t *out);
/** @brief The view the panel handed over last (imu_view_set()).
 *  @return The view; never NULL. */
const imu_view_t *imu_view_get(void);
/**
 * @brief Bind sensor @p i of the last scan.
 * @param i The index into the view's found list.
 * @return Whether the BIND was queued: @p i is listed, Bluetooth runs, no
 *         scan and no calibration runs.
 */
bool imu_view_bind(uint8_t i);
/** @brief Whether a BIND is taken now: Bluetooth runs, no scan and no
 *         calibration runs (imu_view_bind()).
 *  @return Whether it is. */
bool imu_view_bind_free(void);
/** @brief ZERO: the current angles as the IMU's zero.
 *  @return Whether the ZERO was queued: the view holds a valid reading. */
bool imu_view_zero(void);
/** @brief CLEAR ZERO: the IMU's zero forgotten.
 *  @return Whether the UNZERO was queued: the reading is zeroed. */
bool imu_view_unzero(void);
/** @brief Whether CALIBRATE is taken now: the sensor sends frames
 *         (CONNECTED) and no calibration runs.
 *  @return Whether it is. */
bool imu_view_cal_free(void);
/** @brief CALIBRATE, the accelerometer's calibration of 3s.
 *  @return Whether it was queued (imu_view_cal_free()). */
bool imu_view_calibrate(void);
/**
 * @brief Whether a sensor is bound and holds the link: subscribed, with
 *        frames or without.  A connected Bluetooth LE sensor does not
 *        advertise, so no scan lists it.
 * @param v The view; NULL gives false.
 * @return true for a bound address and BOUND, CONNECTED or NO FRAMES.
 */
bool imu_view_bound_linked(const imu_view_t *v);
/**
 * @brief Search for sensors: IMU ON in SETUP when it is off (saved), and
 *        the SCAN queued once Bluetooth LE runs and no scan or calibration
 *        does.  Refused while ARMED, with IMU_SCAN_ARMED on the band.  A
 *        scan already under way, no sync with the C6 and a C6 that failed
 *        take none; a search owed waits through them.
 * @return Whether a scan is queued or owed.
 */
bool imu_view_search(void);
/** @brief Whether a search waits for Bluetooth LE to start before its SCAN
 *         is queued (imu_view_search()).
 *  @return Whether it waits. */
bool imu_view_search_owed(void);
/**
 * @brief The time since the newest angle frame reached the panel, ms,
 *        99999 at most; moved on by imu_view_tick().
 * @return The age.
 */
uint32_t imu_view_age_ms(void);
/**
 * @brief Time passed: the reading's age moves on while it is valid.
 * @param dt_s The frame's time, s.
 */
void imu_view_tick(float dt_s);
/**
 * @brief Counts the views that changed more than the reading: the link,
 *        the scan, the bind, the calibration, the error.
 * @return The count.
 */
uint32_t imu_view_rev(void);
/**
 * @brief Counts the views that changed the reading alone.
 * @return The count.
 */
uint32_t imu_view_read_rev(void);
/** @brief Back to the start: no view, no command, no search owed. */
void imu_view_reset(void);

#ifdef __cplusplus
}
#endif
