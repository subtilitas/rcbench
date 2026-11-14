/**
 * @file
 * @brief The M5Stack Dial's controller on the panel: the Bluetooth LE
 *        central of protocols/bleuart with the Dial's own profile
 *        (dial_profile), the bind by address, the turn tracker and the
 *        link's state of protocols/dialknob, and the stream of the screen
 *        model to the Dial (dial_stream.h).  Pure C: the panel's `blec`
 *        task (firmware/panel/main/panel_dial.c) hands in the host stack's
 *        events and performs the actions.
 *
 * The rules it holds (docs/Dial.md, docs/Safety.md):
 *
 * - An EVENT is relative: dk_track_feed() turns its counter into detents,
 *   and the first EVENT of a connection is a reference that moves nothing.
 *   The detents and the gestures PRESS and HOLD are counted for the frame
 *   task (dial_ctl_take()); the buttons' and the glass's held states are
 *   never handed on, so nothing the Dial sends completes a hold on the
 *   panel.
 * - The loss clock starts at the first EVENT of a connection: a connect
 *   without one is no loss.  No EVENT for DK_DEGRADED_MS (500ms) is
 *   DEGRADED, a notice; for DK_LOST_MS (1,000ms), or the disconnect, is
 *   LOST.  A move to LOST while the bench is ARMED counts in errors, which
 *   the panel takes as an error: SAFE and every output off.
 * - A scan while the bench is ARMED is refused (DIAL_SCAN_ARMED).
 * - One Dial is bound by its address, as the IMU; a bound Dial is
 *   connected again after each drop without a scan.
 * - EVENTs count only from a Dial whose INFO, read once after each
 *   subscribe, names this panel's DK_PROTOCOL.
 *
 * Every function of one controller is called from one task.  Times are
 * unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bleuart.h"
#include "dial_stream.h"
#include "dialknob.h"
#include "imu_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The Dial's profile: the dialknob service, EVENT the STREAM,
 *         SCREEN the CMD (WRITE NO RESPONSE), INFO the PARAM read once
 *         after each subscribe, STATUS named only; the IMU's connection
 *         parameters; a scan lists a device whose advertising data names
 *         the service in a 128-bit UUID list, as dk_adv_has_service()
 *         does. */
extern const bleu_profile_t dial_profile;

/** @brief The Dial's owner on the panel's hub (bleuart_hub.h): the top 3
 *         bits of its machine's tokens; the IMU is owner 0. */
#define DIAL_BLE_OWNER 1u

/** @brief Sizes. @{ */
#define DIAL_ADDR_MAX  BLEU_ADDR_TEXT_MAX /**< "A0:B1:C2:7F:3A:10 R" + NUL */
#define DIAL_NAME_MAX  BLEU_NAME_MAX      /**< a name from a scan + NUL */
#define DIAL_FOUND_MAX BLEU_FOUND_MAX     /**< Dials a scan lists */
/** @brief Sends of one panel packet the host refused (a full buffer)
 *         before it is dropped. */
#define DIAL_SEND_TRIES 3u
/** @} */

/** @brief What SETUP sets: SET_DIAL_EN, the bound Dial, the two steps. */
typedef struct {
    bool    en;                   /**< Dial ON */
    char    addr[DIAL_ADDR_MAX];  /**< SET_TEXT_DIAL_ADDR: "" none */
    char    name[DIAL_NAME_MAX];  /**< SET_TEXT_DIAL_NAME */
    uint8_t thr_tenths;           /**< a detent's throttle, 0.1%, 1 to 100 */
    uint8_t srv_tenths;           /**< a detent's horn angle, 0.1°, 1 to 100 */
} dial_cfg_t;

/** @brief A Dial a scan found. */
typedef struct {
    char   addr[DIAL_ADDR_MAX];   /**< its address with its type */
    char   name[DIAL_NAME_MAX];   /**< its name, "" when none came */
    int8_t rssi;                  /**< dBm of its last report, 127 none */
} dial_dev_t;

/** @brief Where the Dial stands, as SETUP's entry and the band say. */
typedef enum {
    DIAL_LINK_OFF = 0,    /**< Dial OFF */
    DIAL_LINK_STARTING,   /**< Bluetooth LE starts */
    DIAL_LINK_NO_BT,      /**< the host has no sync with the C6 */
    DIAL_LINK_C6_FAILED,  /**< the C6's link failed: until a restart */
    DIAL_LINK_NONE,       /**< no Dial bound */
    DIAL_LINK_SCANNING,   /**< a scan runs */
    DIAL_LINK_CONNECTING, /**< bound, no link */
    DIAL_LINK_NOT_DIAL,   /**< the bound device has no dialknob service */
    DIAL_LINK_WAITING,    /**< linked, no EVENT taken yet */
    DIAL_LINK_PROTO,      /**< linked; INFO names another protocol */
    DIAL_LINK_UP,         /**< EVENTs within DK_DEGRADED_MS */
    DIAL_LINK_DEGRADED,   /**< none for DK_DEGRADED_MS: a notice */
    DIAL_LINK_LOST,       /**< none for DK_LOST_MS, or the disconnect */
} dial_link_t;

/** @brief What a scan request got. */
typedef enum {
    DIAL_SCAN_STARTED = 0, /**< the scan runs */
    DIAL_SCAN_ARMED,       /**< refused: the bench is ARMED */
    DIAL_SCAN_NOT_READY,   /**< refused: Dial OFF, Bluetooth not synced or
                                a scan runs */
} dial_scan_t;

/** @brief The frame task's share of the Dial's input since its last
 *         take: detents and gestures, never a held state. */
typedef struct {
    int32_t  detents; /**< detents turned, clockwise positive */
    uint32_t presses; /**< PRESS gestures: the screen button released
                           before DK_HOLD_MS */
    uint32_t holds;   /**< HOLD gestures: the screen button down for
                           DK_HOLD_MS, once a press */
} dial_input_t;

/** @brief What SETUP > WIRELESS shows of the Dial. */
typedef struct {
    uint8_t    link;       /**< dial_link_t */
    bool       scan_done;  /**< a scan ended since Dial ON: found is its */
    bool       scan_cut;   /**< that scan was stopped by a switch to ARMED */
    uint8_t    found_n;    /**< the Dials it found */
    dial_dev_t found[DIAL_FOUND_MAX]; /**< strongest first */
    uint16_t   hidden;     /**< other devices it heard */
    char       bound_addr[DIAL_ADDR_MAX]; /**< the bound Dial, "" none */
    char       bound_name[DIAL_NAME_MAX]; /**< its name, "" none */
    uint8_t    proto;      /**< the protocol its INFO named, 0 unread */
} dial_view_t;

/** @brief A command of SETUP > WIRELESS for the controller. */
typedef enum {
    DIAL_CMD_NONE = 0, /**< none */
    DIAL_CMD_SCAN,     /**< SCAN FOR DIAL */
    DIAL_CMD_BIND,     /**< bind the found Dial arg */
} dial_cmd_kind_t;

/** @brief One command. */
typedef struct {
    uint8_t kind; /**< dial_cmd_kind_t */
    uint8_t arg;  /**< DIAL_CMD_BIND: the index in the found list */
} dial_cmd_t;

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    dial_cfg_t    cfg;         /**< SETUP's values, the bind's after one */
    bool          configured;  /**< a first dial_ctl_config() came */
    imu_radio_t   radio;       /**< the panel's Bluetooth */
    bleu_t        b;           /**< the central */
    dk_track_t    track;       /**< the turn tracker */
    dk_link_t     link;        /**< the link's state */
    dial_stream_t stream;      /**< the packets to the Dial */
    dial_model_t  model;       /**< the frame task's last model */
    bool          linked;      /**< b was connected at the last look */
    bool          info_read;   /**< INFO was read on this link */
    bool          info_ok;     /**< ...and names DK_PROTOCOL */
    dk_info_t     info;        /**< its value, with info_ok */
    uint8_t       info_proto;  /**< the protocol INFO named, 0 unread */
    bool          scan_open;   /**< a scan started, its end unread */
    bool          scan_done;   /**< a scan ended since Dial ON */
    bool          scan_cut;    /**< the bench ARMED stopped that scan */
    dial_dev_t    found[DIAL_FOUND_MAX]; /**< the last scan's Dials */
    uint8_t       found_n;     /**< their count */
    uint16_t      hidden;      /**< other devices the last scan heard */
    uint32_t      bind_seq;    /**< binds, counted */
    dial_input_t  in;          /**< input not yet taken */
    uint32_t      events;      /**< EVENTs decoded and taken */
    uint32_t      bad;         /**< stream bytes that are no EVENT */
    uint32_t      unread;      /**< EVENTs dropped before INFO was read
                                    and matched */
    uint32_t      taps;        /**< TAP gestures on the glass, not used */
    uint32_t      refused;     /**< scans refused while ARMED */
    uint32_t      errors;      /**< losses while ARMED: each one an error */
    uint32_t      writes;      /**< panel packets handed to the central */
    uint8_t       pend[DK_PKT_MAX]; /**< the last packet sent */
    uint8_t       pend_len;    /**< its length, 0 none */
    uint8_t       pend_tries;  /**< its sends so far */
    uint32_t      resent;      /**< packets sent again after a refusal */
    uint32_t      info_failed; /**< INFO reads that failed, each a reconnect */
    uint16_t      rtt_ms;      /**< the last round trip: a STATE's send to
                                    the first EVENT that echoes it, so the
                                    link's plus up to DK_KEEPALIVE_MS until
                                    the Dial's next EVENT */
    bool          rtt_valid;   /**< rtt_ms is a measurement */
    uint16_t      echo_seen;   /**< the echo the last EVENT carried */
} dial_ctl_t;

/** @brief Clear: Dial OFF, nothing bound.  @param c NULL does nothing. */
void dial_ctl_init(dial_ctl_t *c);

/**
 * @brief Take SETUP's values.  OFF stops a scan and ends the link; ON
 *        connects a bound Dial once Bluetooth is synced.  A new address
 *        binds it.
 * @param c      The controller.
 * @param cfg    The values; NULL does nothing.
 * @param now_ms The clock.
 */
void dial_ctl_config(dial_ctl_t *c, const dial_cfg_t *cfg, uint32_t now_ms);

/**
 * @brief The panel's Bluetooth state.
 * @param c      The controller; NULL does nothing.
 * @param radio  The state.
 * @param now_ms The clock.
 */
void dial_ctl_radio(dial_ctl_t *c, imu_radio_t radio, uint32_t now_ms);

/**
 * @brief SCAN FOR DIAL: the list cleared and a scan of BLEU_SCAN_MS.
 * @param c      The controller.
 * @param armed  The bench is ARMED, or a switch to ARMED waits.
 * @param now_ms The clock.
 * @return DIAL_SCAN_ARMED while @p armed, whatever else holds.
 */
dial_scan_t dial_ctl_scan(dial_ctl_t *c, bool armed, uint32_t now_ms);

/**
 * @brief Bind Dial @p i of the last scan, at once.
 * @param c      The controller.
 * @param i      Its index; one past the list is refused.
 * @param now_ms The clock.
 * @return Whether it bound; cfg.addr and cfg.name then hold it.
 */
bool dial_ctl_bind(dial_ctl_t *c, uint8_t i, uint32_t now_ms);

/**
 * @brief One event of the host stack: to the central; a notification of
 *        EVENT decoded, tracked and counted; INFO's read taken.
 * @param c      The controller.
 * @param e      The event; NULL does nothing.
 * @param now_ms The clock.
 */
void dial_ctl_event(dial_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief The frame task's model of the screen, for the stream.
 * @param c The controller.
 * @param m The model; NULL does nothing.
 */
void dial_ctl_model(dial_ctl_t *c, const dial_model_t *m);

/**
 * @brief Advance to @p now_ms: the link's state and its loss, then the
 *        central's next action, else the stream's next packet as a write.
 *        Call until it returns false, at least every 10ms.
 * @param c      The controller.
 * @param now_ms The clock.
 * @param armed  The bench is ARMED, or a switch to ARMED waits: a loss now
 *               counts in errors.
 * @param out    Receives the action.
 * @return true when @p out holds one.
 */
bool dial_ctl_step(dial_ctl_t *c, uint32_t now_ms, bool armed,
                   bleu_act_t *out);

/**
 * @brief The input since the last take, then cleared.  Nothing comes
 *        while Dial is OFF; no detent and no PRESS while the link is not UP
 *        or DEGRADED.  A HOLD, which only disarms, comes in any link state.
 * @param c   The controller.
 * @param out Receives it.
 */
void dial_ctl_take(dial_ctl_t *c, dial_input_t *out);

/**
 * @brief Where the Dial stands.
 * @param c The controller; NULL is DIAL_LINK_OFF.
 * @return The state.
 */
dial_link_t dial_ctl_link(const dial_ctl_t *c);

/**
 * @brief What SETUP shows, as of the last call.
 * @param c   The controller; NULL gives an OFF view.
 * @param out Receives the view.
 */
void dial_ctl_view(const dial_ctl_t *c, dial_view_t *out);

/**
 * @brief A step option of SET_DIAL_THR or SET_DIAL_SRV in tenths.
 * @param option 0 0.5, 1 1, 2 2, 3 5; another is 1.
 * @return 5, 10, 20 or 50.
 */
uint8_t dial_step_tenths(int option);

#ifdef __cplusplus
}
#endif
