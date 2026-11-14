/**
 * @file
 * @brief The power node's controller on the panel: the Bluetooth LE central
 *        of protocols/bleuart with the rcnode profile (pnode_profile), the
 *        bind by address, INFO's check, the readings (pnode_meter.h) and
 *        the RUN marker at each switch to ARMED.  Pure C: the panel's `blec`
 *        task (firmware/panel/main/panel_pnode.c) hands in the host stack's
 *        events and performs the actions.
 *
 * The rules it holds (docs/Nodes.md, "The panel"):
 *
 * - A scan lists a device whose advertising names the rcnode service in a
 *   128-bit UUID list, as the node sends it (rcnode_adv_build()), and the
 *   power role, in its manufacturer data or else its name
 *   (rcnode_adv_match()).  A device whose reports name no role, or
 *   another, is not listed.
 * - A scan while the bench is ARMED is refused (PNODE_SCAN_ARMED), and a
 *   scan that runs is stopped at the switch to ARMED.
 * - One node is bound by its address, as the Dial; a bound node is
 *   connected again after each drop without a scan, on the central's
 *   backoff.
 * - STREAM packets count only from a node whose INFO, read once after each
 *   subscribe, names RCNODE_PROTO, the power role and an INA228 set-up the
 *   meter takes (pnode_meter_info()).  The node sends its STATUS packet on
 *   STREAM as well, as the central subscribes to STREAM alone.
 * - At each switch to ARMED the RUN marker goes to CONTROL with the run's
 *   tag, so the node's extremes are the run's; a link that is down then
 *   gets it when it comes back within the run, and a link that comes back
 *   within the run to a node whose RUN packets carry another tag (it
 *   started again) gets it again.
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
#include "imu_ble.h"
#include "pnode_meter.h"
#include "pnode_view.h"
#include "rcnode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The power node's profile: the rcnode service, STREAM the STREAM,
 *         CONTROL the CMD, INFO the PARAM read once after each subscribe,
 *         STATUS named only; the IMU's connection parameters. */
extern const bleu_profile_t pnode_profile;

/** @brief The power node's owner on the panel's hub (bleuart_hub.h): the
 *         IMU is owner 0, the Dial 1. */
#define PNODE_BLE_OWNER 2u

/** @brief Devices a scan keeps the role of (rcnode_adv_match()). */
#define PNODE_ROLES_MAX 16u

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    pnode_cfg_t   cfg;         /**< SETUP's values, the bind's after one */
    bool          configured;  /**< a first pnode_ctl_config() came */
    imu_radio_t   radio;       /**< the panel's Bluetooth */
    bleu_t        b;           /**< the central */
    pnode_meter_t m;           /**< the readings */
    bool          linked;      /**< b was connected at the last look */
    bool          info_read;   /**< INFO was read on this link */
    bool          info_ok;     /**< ...names the protocol and the role, and
                                    the meter took its set-up */
    uint8_t       info_proto;  /**< the protocol INFO named, 0 unread */
    uint8_t       info_role;   /**< the role INFO named, 0 unread */
    bool          was_up;      /**< a reading came on this link */
    bool          lost;        /**< the link dropped after a reading */
    bool          run_owed;    /**< the RUN write waits for the link */
    bool          scan_open;   /**< a scan started, its end unread */
    bool          scan_done;   /**< a scan ended since POWER NODE ON */
    bool          scan_cut;    /**< the bench ARMED stopped that scan */
    pnode_dev_t   found[PNODE_FOUND_MAX]; /**< the last scan's nodes */
    bleu_addr_t   role_addr[PNODE_ROLES_MAX]; /**< the scan's rcnode devices */
    uint8_t       role_of[PNODE_ROLES_MAX];   /**< ...and each one's role */
    uint8_t       roles_n;     /**< their count */
    uint8_t       found_n;     /**< their count */
    uint16_t      hidden;      /**< other devices the last scan heard */
    uint32_t      bind_seq;    /**< binds, counted */
    uint32_t      packets;     /**< STREAM packets the meter took */
    uint32_t      unread;      /**< STREAM packets dropped before INFO
                                    was read and taken */
    uint32_t      refused;     /**< scans refused while ARMED */
    uint32_t      runs_sent;   /**< RUN writes handed to the central */
    uint32_t      info_failed; /**< INFO reads that failed, each a
                                    reconnect */
    bool          held;        /**< an update holds the link off
                                    (pnode_ctl_hold()) */
} pnode_ctl_t;

/** @brief Clear: POWER NODE OFF, nothing bound.  @param c NULL does
 *         nothing. */
void pnode_ctl_init(pnode_ctl_t *c);

/**
 * @brief Take SETUP's values.  OFF stops a scan and ends the link; ON
 *        connects a bound node once Bluetooth is synced.  A new address
 *        binds it.
 * @param c      The controller.
 * @param cfg    The values; NULL does nothing.
 * @param now_ms The clock.
 */
void pnode_ctl_config(pnode_ctl_t *c, const pnode_cfg_t *cfg,
                      uint32_t now_ms);

/**
 * @brief The panel's Bluetooth state.
 * @param c      The controller; NULL does nothing.
 * @param radio  The state.
 * @param now_ms The clock.
 */
void pnode_ctl_radio(pnode_ctl_t *c, imu_radio_t radio, uint32_t now_ms);

/**
 * @brief The node's update holds the link (panel_devupd.c): ON ends the
 *        link and keeps it ended, as POWER NODE OFF does, with SETUP's
 *        values kept and SETUP's later values taken; OFF gives the link
 *        back to them.  Neither counts as a loss, so no STALE or LOST
 *        follows from the update.
 * @param c      The controller; NULL does nothing.
 * @param on     The hold.
 * @param now_ms The clock.
 */
void pnode_ctl_hold(pnode_ctl_t *c, bool on, uint32_t now_ms);

/**
 * @brief What the node's update needs of the link.
 * @param c       The controller.
 * @param addr    Receives the bound node's address.
 * @param linked  Receives whether the node is connected, its INFO taken
 *                and a STATUS packet on this link named its version.
 * @param version Receives that version, MAJOR, MINOR, PATCH.
 * @param down    Receives whether no link to the node stands or is under
 *                way.
 * @return false when no node is bound, or an argument is NULL.
 */
bool pnode_ctl_target(const pnode_ctl_t *c, bleu_addr_t *addr, bool *linked,
                      uint8_t version[3], bool *down);

/**
 * @brief SCAN FOR NODE: the list cleared and a scan of BLEU_SCAN_MS.
 * @param c      The controller.
 * @param armed  The bench is ARMED, or a switch to ARMED waits.
 * @param now_ms The clock.
 * @return PNODE_SCAN_ARMED while @p armed, whatever else holds.
 */
pnode_scan_t pnode_ctl_scan(pnode_ctl_t *c, bool armed, uint32_t now_ms);

/**
 * @brief Bind node @p i of the last scan, at once.
 * @param c      The controller.
 * @param i      Its index; one past the list is refused.
 * @param now_ms The clock.
 * @return Whether it bound; cfg.addr and cfg.name then hold it.
 */
bool pnode_ctl_bind(pnode_ctl_t *c, uint8_t i, uint32_t now_ms);

/**
 * @brief One event of the host stack: to the central; a notification of
 *        STREAM to the meter; INFO's read taken.
 * @param c      The controller.
 * @param e      The event; NULL does nothing.
 * @param now_ms The clock.
 */
void pnode_ctl_event(pnode_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief Advance to @p now_ms: the meter's run and dropout rule, a running
 *        scan stopped while @p armed, then the central's next action, else
 *        the RUN write owed.  Call until it returns false, at least every
 *        10ms.
 * @param c         The controller.
 * @param now_ms    The clock.
 * @param armed     The bench is ARMED, or a switch to ARMED waits.
 * @param bench_run The run reads BENCH's readings (pnode_meter_step()).
 * @param out       Receives the action.
 * @return true when @p out holds one.
 */
bool pnode_ctl_step(pnode_ctl_t *c, uint32_t now_ms, bool armed,
                    bool bench_run, bleu_act_t *out);

/**
 * @brief Where the node stands.
 * @param c      The controller; NULL is PNODE_LINK_OFF.
 * @param now_ms The clock, for the readings' age.
 * @return The state.
 */
pnode_link_t pnode_ctl_link(const pnode_ctl_t *c, uint32_t now_ms);

/**
 * @brief What SETUP shows.
 * @param c      The controller; NULL gives an OFF view.
 * @param now_ms The clock.
 * @param out    Receives the view.
 */
void pnode_ctl_view(const pnode_ctl_t *c, uint32_t now_ms, pnode_view_t *out);

#ifdef __cplusplus
}
#endif
