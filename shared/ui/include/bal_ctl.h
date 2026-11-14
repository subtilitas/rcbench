/**
 * @file
 * @brief The balance node's controller on the panel: the Bluetooth LE
 *        central of protocols/bleuart with the rcnode service's profile
 *        (bal_profile), the scan for balance nodes, the bind by address,
 *        the INFO check, the 24-byte reports of protocols/rcnode taken from
 *        STREAM with the node's STATUS among them, and the CONTROL
 *        writes.  Pure
 *        C: the panel's `blec` task (firmware/panel/main/panel_bal.c) hands
 *        in the host stack's events and performs the actions.
 *
 * The rules it holds (docs/Balance.md, docs/Nodes.md):
 *
 * - The node is a measurement.  It drives no output, and a lost link is no
 *   error: the readings go stale (node_balance.h) and nothing else
 *   happens.
 * - A scan while the bench is ARMED is refused (BAL_SCAN_ARMED); one that
 *   runs at the switch to ARMED is stopped.
 * - A scan lists a device whose manufacturer data names the balance role;
 *   its name may come in the scan response.  A name alone lists nothing.
 *   The power node and other devices are counted as hidden.
 * - One node is bound by its address; a bound node is connected again after
 *   each drop without a scan, after the central's backoff.
 * - Reports count only from a node whose INFO, read once after each
 *   subscribe, names RCNODE_PROTO and the balance role.  An INFO that does
 *   not read or decode ends the link, which comes back after the backoff.
 * - INFO's clock finds a node restart: more than BAL_BOOT_SLACK_US (2s)
 *   from the last report's clock plus the panel's time since, it counts in
 *   boots, and node_balance.h clears the captures on it.
 * - The node's battery comes from the STATUS packets it sends on STREAM
 *   beside its reports.  The STATUS characteristic is left to a phone.
 * - CONTROL: BLADES whenever the blade count differs from the one the node
 *   took; RPM with the bench's rpm once BAL_NOIDX_REPORTS reports in a row
 *   came without an index edge, and 0 (the index mode) once one comes with
 *   one.  At most one write each BAL_CTL_MS, so the link the Dial shares
 *   carries 4 writes a second at most.
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
#include "rcnode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The node's profile: the rcnode service, STREAM the STREAM,
 *         CONTROL the CMD, INFO the PARAM read once after each subscribe,
 *         STATUS not used; the IMU's connection parameters. */
extern const bleu_profile_t bal_profile;

/** @brief The balance node's owner on the panel's hub (bleuart_hub.h):
 *         the top 3 bits of its machine's tokens.  The IMU is 0, the Dial
 *         1; 2 is left for the power node. */
#define BAL_BLE_OWNER 3u

/** @brief Sizes and times. @{ */
#define BAL_ADDR_MAX  BLEU_ADDR_TEXT_MAX /**< "A0:B1:C2:7F:3A:10 R" + NUL */
#define BAL_NAME_MAX  BLEU_NAME_MAX      /**< a name from a scan + NUL */
#define BAL_FOUND_MAX BLEU_FOUND_MAX     /**< nodes a scan lists */
#define BAL_SEEN_MAX  32u                /**< other addresses a scan counts */
#define BAL_CTL_MS    250u   /**< the least time between two CONTROL writes */
#define BAL_STALE_MS  1000u  /**< a report older than this is stale: 4 periods */
#define BAL_BOOT_SLACK_US 2000000u /**< INFO's clock this far from the last
                                        report's plus the panel's time
                                        between: the node restarted */
#define BAL_NOIDX_REPORTS 2u /**< reports in a row without an index before
                                  the RPM write */
#define BAL_RPM_UNSENT 0xFFFFFFFFu /**< no RPM written on this link yet */
/** @} */

/** @brief What SETUP sets: SET_BNODE_EN and the bound node. */
typedef struct {
    bool en;                     /**< BALANCE NODE ON */
    char addr[BAL_ADDR_MAX];     /**< SET_TEXT_BNODE_ADDR: "" none */
    char name[BAL_NAME_MAX];     /**< SET_TEXT_BNODE_NAME */
} bal_cfg_t;

/** @brief A node a scan found. */
typedef struct {
    char   addr[BAL_ADDR_MAX];   /**< its address with its type */
    char   name[BAL_NAME_MAX];   /**< its name, "" when none came */
    int8_t rssi;                 /**< dBm of its last report, 127 none */
} bal_dev_t;

/** @brief Where the node stands, as SETUP's entry says. */
typedef enum {
    BAL_LINK_OFF = 0,     /**< BALANCE NODE OFF */
    BAL_LINK_STARTING,    /**< Bluetooth LE starts */
    BAL_LINK_NO_BT,       /**< the host has no sync with the C6 */
    BAL_LINK_C6_FAILED,   /**< the C6's link failed: until a restart */
    BAL_LINK_NONE,        /**< no node bound */
    BAL_LINK_SCANNING,    /**< a scan runs */
    BAL_LINK_CONNECTING,  /**< bound, no link */
    BAL_LINK_NOT_NODE,    /**< the bound device has no rcnode service */
    BAL_LINK_WAITING,     /**< linked, INFO not read yet */
    BAL_LINK_WRONG,       /**< linked; INFO names another role or protocol */
    BAL_LINK_QUIET,       /**< linked, no report for BAL_STALE_MS */
    BAL_LINK_UP,          /**< reports within BAL_STALE_MS */
} bal_link_t;

/** @brief What a scan request got. */
typedef enum {
    BAL_SCAN_STARTED = 0, /**< the scan runs */
    BAL_SCAN_ARMED,       /**< refused: the bench is ARMED */
    BAL_SCAN_NOT_READY,   /**< refused: OFF, Bluetooth not synced or a scan
                               runs */
} bal_scan_t;

/** @brief What SETUP > WIRELESS shows of the node. */
typedef struct {
    uint8_t   link;       /**< bal_link_t */
    bool      scan_done;  /**< a scan ended since ON: found is its */
    bool      scan_cut;   /**< that scan was stopped by a switch to ARMED */
    uint8_t   found_n;    /**< the nodes it found */
    bal_dev_t found[BAL_FOUND_MAX]; /**< strongest first */
    uint16_t  hidden;     /**< other devices it heard */
    char      bound_addr[BAL_ADDR_MAX]; /**< the bound node, "" none */
    char      bound_name[BAL_NAME_MAX]; /**< its name, "" none */
    uint8_t   info_role;  /**< the role its INFO named, 0 unread */
    uint8_t   info_proto; /**< the protocol its INFO named, 0 unread */
    bool      bat_known;  /**< a STATUS came on this link */
    uint16_t  bat_mv;     /**< its cell, mV; 0 with RCNODE_ST_BAT_NONE */
    uint16_t  bat_flags;  /**< its RCNODE_ST_* flags */
    uint32_t  boots;      /**< node restarts found since power-up */
    bool      held;       /**< an update holds the link off: link reads
                               OFF while BALANCE NODE stays ON */
} bal_view_t;

/** @brief A command of SETUP > WIRELESS for the controller. */
typedef enum {
    BAL_CMD_NONE = 0, /**< none */
    BAL_CMD_SCAN,     /**< SCAN FOR NODE */
    BAL_CMD_BIND,     /**< bind the found node arg */
} bal_cmd_kind_t;

/** @brief One command. */
typedef struct {
    uint8_t kind; /**< bal_cmd_kind_t */
    uint8_t arg;  /**< BAL_CMD_BIND: the index in the found list */
} bal_cmd_t;

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    bal_cfg_t   cfg;          /**< SETUP's values, the bind's after one */
    bool        configured;   /**< a first bal_ctl_config() came */
    imu_radio_t radio;        /**< the panel's Bluetooth */
    bleu_t      b;            /**< the central */
    bool        linked;       /**< b was up at the last look */
    bool        info_read;    /**< INFO was read on this link */
    bool        info_ok;      /**< ...and names the protocol and the role */
    uint8_t     info_role;    /**< the role INFO named, 0 unread */
    uint8_t     info_proto;   /**< the protocol INFO named, 0 unread */
    bool        scan_open;    /**< a scan started, its end unread */
    bool        scanning;     /**< ADV reports go to found */
    bool        scan_done;    /**< a scan ended since ON */
    bool        scan_cut;     /**< the bench ARMED stopped that scan */
    bal_dev_t   found[BAL_FOUND_MAX]; /**< the last scan's nodes */
    uint8_t     found_n;      /**< their count */
    bleu_addr_t found_addr[BAL_FOUND_MAX]; /**< their addresses */
    bleu_addr_t seen[BAL_SEEN_MAX]; /**< other addresses the scan heard */
    uint8_t     seen_n;       /**< their count */
    uint16_t    hidden;       /**< other devices the last scan heard */
    uint32_t    bind_seq;     /**< binds, counted */
    bool        have_rep;     /**< rep holds a report of this power-up */
    rcnode_bal_report_t rep;  /**< the last report taken */
    uint32_t    rep_ms;       /**< when it came */
    uint32_t    reports;      /**< reports taken, counted */
    uint32_t    lost;         /**< reports missing by their seq */
    uint32_t    bad;          /**< stream packets that are no report */
    uint32_t    unread;       /**< reports dropped before INFO matched */
    uint32_t    refused;      /**< scans refused while ARMED */
    uint32_t    info_failed;  /**< INFO reads that failed, each a reconnect */
    uint32_t    boots;        /**< node restarts found: INFO's clock not
                                   where the last report's left it */
    uint8_t     blades;       /**< the blade count the frame wants, 1 to 12 */
    uint16_t    bench_rpm;    /**< the bench's rpm, 0 not known */
    uint8_t     noidx_n;      /**< reports in a row without an index */
    bool        noidx;        /**< the no-index mode is wanted */
    uint8_t     blades_sent;  /**< the count the node took, 0 none yet */
    uint32_t    rpm_sent;     /**< the rpm it took, BAL_RPM_UNSENT none */
    uint8_t     wr_op;        /**< the write in flight, 0 none */
    uint32_t    wr_val;       /**< its value */
    bool        wr_any;       /**< a write went on this link */
    uint32_t    t_wr;         /**< when it went */
    uint32_t    writes;       /**< CONTROL writes handed to the central */
    uint32_t    wr_failed;    /**< writes refused or timed out */
    bool        bat_known;    /**< a STATUS came on this link */
    uint16_t    bat_mv;       /**< its cell, mV */
    uint16_t    bat_flags;    /**< its RCNODE_ST_* flags */
    uint8_t     fw[3];        /**< its firmware, MAJOR, MINOR, PATCH */
    bool        held;         /**< an update holds the link off
                                   (bal_ctl_hold()) */
} bal_ctl_t;

/** @brief Clear: OFF, nothing bound, 2 blades.  @param c NULL does
 *         nothing. */
void bal_ctl_init(bal_ctl_t *c);

/**
 * @brief Take SETUP's values.  OFF stops a scan and ends the link; ON
 *        connects a bound node once Bluetooth is synced.  A new address
 *        binds it.
 * @param c      The controller.
 * @param cfg    The values; NULL does nothing.
 * @param now_ms The clock.
 */
void bal_ctl_config(bal_ctl_t *c, const bal_cfg_t *cfg, uint32_t now_ms);

/**
 * @brief The panel's Bluetooth state.
 * @param c      The controller; NULL does nothing.
 * @param radio  The state.
 * @param now_ms The clock.
 */
void bal_ctl_radio(bal_ctl_t *c, imu_radio_t radio, uint32_t now_ms);

/**
 * @brief The node's update holds the link (panel_devupd.c): ON ends the
 *        link and keeps it ended, as BALANCE NODE OFF does, with SETUP's
 *        values kept and SETUP's later values taken, and hides the last
 *        report; OFF gives the link back to SETUP's values.  A node that
 *        installed restarts on its new image: INFO after the take-back
 *        finds the restart from its clock, counts it in boots and drops
 *        the report, as at any restart.
 * @param c      The controller; NULL does nothing.
 * @param on     The hold.
 * @param now_ms The clock.
 */
void bal_ctl_hold(bal_ctl_t *c, bool on, uint32_t now_ms);

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
bool bal_ctl_target(const bal_ctl_t *c, bleu_addr_t *addr, bool *linked,
                    uint8_t version[3], bool *down);

/**
 * @brief SCAN FOR NODE: the list cleared and a scan of BLEU_SCAN_MS.
 * @param c      The controller.
 * @param armed  The bench is ARMED, or a switch to ARMED waits.
 * @param now_ms The clock.
 * @return BAL_SCAN_ARMED while @p armed, whatever else holds.
 */
bal_scan_t bal_ctl_scan(bal_ctl_t *c, bool armed, uint32_t now_ms);

/**
 * @brief Bind node @p i of the last scan, at once.
 * @param c      The controller.
 * @param i      Its index; one past the list is refused.
 * @param now_ms The clock.
 * @return Whether it bound; cfg.addr and cfg.name then hold it.
 */
bool bal_ctl_bind(bal_ctl_t *c, uint8_t i, uint32_t now_ms);

/**
 * @brief One event of the host stack: an advertising report to the scan's
 *        list, the rest to the central; a notification of STREAM decoded
 *        and a report or the battery taken; INFO's read taken.
 * @param c      The controller.
 * @param e      The event; NULL does nothing.
 * @param now_ms The clock.
 */
void bal_ctl_event(bal_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief What the frame wants written: the blade count and the bench's
 *        rpm for the no-index mode.
 * @param c      The controller; NULL does nothing.
 * @param blades The rotor's blades; held to 1 to RCNODE_BLADES_MAX.
 * @param rpm    The bench's rpm, 0 when not known; held to
 *               RCNODE_RPM_MAX.
 */
void bal_ctl_drive(bal_ctl_t *c, uint8_t blades, uint32_t rpm);

/**
 * @brief Advance to @p now_ms: a scan stopped at ARMED, the central's next
 *        action, else a CONTROL write when one is due.
 *        Call until it returns false, at least every 10ms.
 * @param c      The controller.
 * @param now_ms The clock.
 * @param armed  The bench is ARMED, or a switch to ARMED waits.
 * @param out    Receives the action.
 * @return true when @p out holds one.
 */
bool bal_ctl_step(bal_ctl_t *c, uint32_t now_ms, bool armed, bleu_act_t *out);

/**
 * @brief Where the node stands at @p now_ms.
 * @param c      The controller; NULL is BAL_LINK_OFF.
 * @param now_ms The clock.
 * @return The state.
 */
bal_link_t bal_ctl_link(const bal_ctl_t *c, uint32_t now_ms);

/**
 * @brief What SETUP shows at @p now_ms.
 * @param c      The controller; NULL gives an OFF view.
 * @param now_ms The clock.
 * @param out    Receives the view.
 */
void bal_ctl_view(const bal_ctl_t *c, uint32_t now_ms, bal_view_t *out);

/**
 * @brief The last report and its age.
 * @param c      The controller.
 * @param now_ms The clock.
 * @param out    Receives it.
 * @param age_ms Receives its age, ms.
 * @return false, and nothing written, without a report since ON, or while
 *         OFF.
 */
bool bal_ctl_report(const bal_ctl_t *c, uint32_t now_ms,
                    rcnode_bal_report_t *out, uint32_t *age_ms);

#ifdef __cplusplus
}
#endif
