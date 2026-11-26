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
 * - After INFO is taken on each link, and at each change of the panel's
 *   language, LANG goes to CONTROL with that language, after any RUN write
 *   owed: a display node (docs/Nodes.md, "The display node") shows its
 *   texts in it.  A node that refuses the write (one built before LANG)
 *   gets it again only at the next link or change.
 * - After INFO is taken on each link, and at each change of NODE SHUNT,
 *   SHUNT goes to CONTROL with the setting when INFO names another shunt,
 *   after any RUN write owed and never while the bench is ARMED or a
 *   switch to ARMED waits: a shunt changed under a run would rescale its
 *   readings.  A write the node takes is followed by a read of INFO, whose
 *   shunt the view shows and the meter calibrates with; that read waits
 *   while the bench is ARMED or the meter's run is live, so a run keeps
 *   its full scale.  A write refused with ATT error 0x13 (a node built
 *   before SHUNT, PNODE_ATT_NOT_ALLOWED) is counted for the band and sent
 *   again only at the next link or change.  A write that times out or
 *   fails otherwise may have been taken: INFO is read again (as above),
 *   and SHUNT goes again when INFO still names another shunt.  A write the
 *   link's drop ends leaves it to the next link's INFO.
 * - The node's own settings (node_cfg_link.h): after INFO, CONFIG is
 *   asked, twice at most a link.  Until CONFIG, its refusal or the wait
 *   decides it, no SHUNT goes.  With CONFIG known, the last change of the shunt wins
 *   (rcnode_shunt_sync()): the panel's NODE SHUNT against CONFIG's shunt,
 *   agreed shunt and age.  WRITE sends SHUNT with the setting; ADOPT
 *   names the node's shunt in the view (adopt_uohm, adopt_seq), which the
 *   frame task writes into NODE SHUNT while the bench is SAFE; ACK sends
 *   SHUNT with the value both hold, so the node marks it agreed.  Each
 *   CONFIG taken decides again, also one that comes after the fallback to
 *   INFO's rule.  adopt_uohm clears on a new link and once NODE SHUNT
 *   holds it, so the same node value is taken again after a panel change.
 *   The setting's age is the time since pnode_ctl_config() saw it change, not
 *   known before the first change since the panel's start.  A node that
 *   refuses CONFIG (one built before it) gets the rule above: SHUNT when
 *   INFO names another shunt.  Nothing of this runs while the bench is
 *   ARMED or a switch to ARMED waits.  NAME and INA228 ADDRESS are
 *   written on SETUP's request (pnode_ctl_name(), pnode_ctl_i228()).
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
#include "node_cfg_link.h"
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

/** @brief pnode_ctl_t's lang_sent before a LANG write on the link. */
#define PNODE_LANG_NONE 0xFFu

/** @brief A write's status for ATT error 0x13, "value not allowed", as the
 *         host stack reports it (NimBLE's BLE_HS_ATT_ERR(0x13)): the node
 *         does not take the operation. */
#define PNODE_ATT_NOT_ALLOWED 0x113u

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
    uint8_t       info_board;  /**< the board INFO named, 0 unread */
    uint8_t       info_i228;   /**< the INA228's address INFO named, 0 none
                                    found */
    uint16_t      info_shunt;  /**< the shunt INFO named, µΩ, 0 unread */
    uint16_t      shunt_sent;  /**< the shunt the SHUNT write last went
                                    with on this link; 0 before */
    bool          shunt_wr;    /**< a SHUNT write is in flight */
    bool          info_owed;   /**< INFO to read again once the bench is
                                    SAFE and the meter's run not live */
    uint32_t      shunt_refused; /**< SHUNT writes the node refused */
    uint8_t       known_board; /**< the board the bound address's last
                                    accepted INFO named; kept over link
                                    ends and reconnects, 0 after an
                                    address change */
    bool          was_up;      /**< a reading came on this link */
    bool          lost;        /**< the link dropped after a reading */
    bool          run_owed;    /**< the RUN write waits for the link */
    uint8_t       lang;        /**< the panel's language, RCNODE_LANG_* */
    uint8_t       lang_sent;   /**< the language LANG last went with on
                                    this link; PNODE_LANG_NONE before */
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
    uint32_t      scan_unready; /**< scans refused while OFF, held, without
                                     sync or with a scan running */
    uint32_t      bind_refused; /**< binds refused: a scan ran or the index
                                     was past the list */
    uint32_t      runs_sent;   /**< RUN writes handed to the central */
    uint32_t      info_failed; /**< INFO reads that failed, each a
                                    reconnect */
    bool          held;        /**< an update holds the link off
                                    (pnode_ctl_hold()) */
    ncl_t         nc;          /**< the node's own settings on this link */
    uint8_t       info_addr[6]; /**< the address INFO named */
    bool          shunt_set_known; /**< NODE SHUNT changed since the start */
    uint32_t      shunt_set_ms; /**< ...when */
    uint16_t      adopt_uohm;  /**< the node's shunt the panel is to take, 0
                                    none; cleared on a new link and once
                                    NODE SHUNT holds it */
    uint32_t      sync_taken;  /**< nc.taken the shunt was last decided on:
                                    a CONFIG after it decides again */
    uint32_t      adopt_seq;   /**< counts each value to take */
} pnode_ctl_t;

/** @brief Clear: POWER NODE OFF, nothing bound.  @param c NULL does
 *         nothing. */
void pnode_ctl_init(pnode_ctl_t *c);

/**
 * @brief Take SETUP's values.  OFF stops a scan and ends the link; ON
 *        connects a bound node once Bluetooth is synced.  A new address
 *        binds it, unless the values were read before SETUP kept the last
 *        bind (cfg->bind_seq behind the controller's): then the bound
 *        address and name stay, and the other values are taken.
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
 * @brief The board the bound node's last accepted INFO named
 *        (RCNODE_BOARD_*), for UPDATES' choice of image
 *        (devimg_pnode_image()).  It stays over a link's end, the update's
 *        hold and a reconnect of the same address, while that link's INFO
 *        is still unread: a reconnect is no change of board.  A new INFO
 *        sets it; only an address change (another bind, an unbind, SETUP's
 *        address) clears it.  A bind of the address already bound keeps
 *        it.
 * @param c The controller.
 * @return the board; 0 before any INFO of the bound address, or when @p c
 *         is NULL
 */
uint8_t pnode_ctl_board(const pnode_ctl_t *c);

/**
 * @brief SCAN FOR NODE: the list cleared and a scan of BLEU_SCAN_MS.
 * @param c      The controller.
 * @param armed  The bench is ARMED, or a switch to ARMED waits.
 * @param now_ms The clock.
 * @return PNODE_SCAN_ARMED while @p armed, whatever else holds, counted
 *         in refused; PNODE_SCAN_NOT_READY counted in scan_unready.  The
 *         view carries both counts to the band.
 */
pnode_scan_t pnode_ctl_scan(pnode_ctl_t *c, bool armed, uint32_t now_ms);

/**
 * @brief Bind node @p i of the last scan, at once.
 * @param c      The controller.
 * @param i      Its index; one past the list is refused.
 * @param now_ms The clock.
 * @return Whether it bound; cfg.addr and cfg.name then hold it.  A
 *         refusal while a scan runs or past the list counts in
 *         bind_refused, which the view carries to the band.
 */
bool pnode_ctl_bind(pnode_ctl_t *c, uint8_t i, uint32_t now_ms);

/**
 * @brief SEARCH's UNBIND: forget the bound node, at once.  The link ends
 *        (a DISCONNECT, or the connect cancelled), cfg.addr and cfg.name
 *        are cleared, the board is no longer known and bind_seq counts
 *        it, so the frame writes the empty address back to SETUP with a
 *        save.  The link's end is no loss (no LOST, as for another bind),
 *        but the readings stop: a run that counts on the node meets the
 *        dropout rule (pnode_meter.h) as on a lost link and ends as an
 *        error once the gap passes SERVO_GAP_MAX_MS (300ms).
 * @param c      The controller.
 * @param now_ms The clock.
 * @return false with nothing bound or @p c NULL: nothing changes.
 */
bool pnode_ctl_unbind(pnode_ctl_t *c, uint32_t now_ms);

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
 *        the RUN write owed, else the node's own settings' write owed
 *        (node_cfg_link.h), else the SHUNT write owed, else the LANG
 *        write owed.  Call until it returns false, at least every 10ms.
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
 * @brief The panel's language, for the LANG write (above).
 * @param c    The controller; NULL does nothing.
 * @param lang RCNODE_LANG_EN or RCNODE_LANG_DE; another value is English.
 */
void pnode_ctl_lang(pnode_ctl_t *c, uint8_t lang);

/**
 * @brief SETUP's NAME for the linked node (node_cfg_link.h).
 * @param c     The controller.
 * @param name  The name; "" clears it.
 * @param armed The bench is ARMED, or a switch to ARMED waits.
 * @return NCL_REQ_OK when queued, or the refusal; NCL_REQ_UNKNOWN for a
 *         NULL controller.
 */
ncl_req_t pnode_ctl_name(pnode_ctl_t *c, const char *name, bool armed);

/**
 * @brief SETUP's INA228 ADDRESS for the linked node.
 * @param c     The controller.
 * @param a     RCNODE_I228_AUTO or 0x40 to 0x4F.
 * @param armed The bench is ARMED, or a switch to ARMED waits.
 * @return As pnode_ctl_name().
 */
ncl_req_t pnode_ctl_i228(pnode_ctl_t *c, uint8_t a, bool armed);

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
