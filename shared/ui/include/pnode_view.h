/**
 * @file
 * @brief What SETUP > WIRELESS shows of the power node and asks of it: the
 *        link's states, the view, the commands and SETUP's values
 *        (pnode_ctl.h holds the controller that fills and takes them).
 *        Types alone, so the pane's code needs neither the central nor the
 *        meter.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sizes. @{ */
#define PNODE_ADDR_MAX  20u /**< "A0:B1:C2:7F:3A:10 P" + NUL: BLEU_ADDR_TEXT_MAX */
#define PNODE_NAME_MAX  24u /**< a name from a scan + NUL: BLEU_NAME_MAX */
#define PNODE_FOUND_MAX 8u  /**< nodes a scan lists: BLEU_FOUND_MAX */
/** @} */

/** @brief What SETUP sets: SET_PNODE_EN, the bound node, SET_I228_SRC. */
typedef struct {
    bool en;                    /**< POWER NODE ON */
    bool src_node;              /**< INA228 SOURCE is NODE */
    char addr[PNODE_ADDR_MAX];  /**< SET_TEXT_PNODE_ADDR: "" none */
    char name[PNODE_NAME_MAX];  /**< SET_TEXT_PNODE_NAME */
    uint32_t bind_seq;          /**< the binds SETUP had kept when these
                                     values were read (pnode_ctl_t's
                                     bind_seq): values read before the
                                     last bind was kept carry an address
                                     older than it, which is not taken */
} pnode_cfg_t;

/** @brief A node a scan found. */
typedef struct {
    char   addr[PNODE_ADDR_MAX];  /**< its address with its type */
    char   name[PNODE_NAME_MAX];  /**< its name, "" when none came */
    int8_t rssi;                  /**< dBm of its last report, 127 none */
} pnode_dev_t;

/** @brief Where the node stands, as SETUP's entry and the band say. */
typedef enum {
    PNODE_LINK_OFF = 0,    /**< POWER NODE OFF */
    PNODE_LINK_STARTING,   /**< Bluetooth LE starts */
    PNODE_LINK_NO_BT,      /**< the host has no sync with the C6 */
    PNODE_LINK_C6_FAILED,  /**< the C6's link failed: until a restart */
    PNODE_LINK_NONE,       /**< no node bound */
    PNODE_LINK_SCANNING,   /**< a scan runs */
    PNODE_LINK_CONNECTING, /**< bound, no link */
    PNODE_LINK_NOT_NODE,   /**< the bound device has no rcnode service, or
                                its INFO names another role */
    PNODE_LINK_WAITING,    /**< linked, INFO unread or no window yet */
    PNODE_LINK_PROTO,      /**< INFO names another protocol */
    PNODE_LINK_SETUP,      /**< INFO's INA228 set-up is not one
                                ina228_calibrate() gives */
    PNODE_LINK_NO_SENSOR,  /**< windows come; the INA228 is not online */
    PNODE_LINK_UP,         /**< a reading within PNODE_STALE_MS */
    PNODE_LINK_STALE,      /**< linked; no reading for PNODE_STALE_MS */
    PNODE_LINK_LOST,       /**< the link dropped after a reading */
} pnode_link_t;

/** @brief What a scan request got. */
typedef enum {
    PNODE_SCAN_STARTED = 0, /**< the scan runs */
    PNODE_SCAN_ARMED,       /**< refused: the bench is ARMED */
    PNODE_SCAN_NOT_READY,   /**< refused: POWER NODE OFF, Bluetooth not
                                 synced or a scan runs */
} pnode_scan_t;

/** @brief What SETUP > WIRELESS shows of the node. */
typedef struct {
    uint8_t     link;       /**< pnode_link_t */
    bool        scan_done;  /**< a scan ended since POWER NODE ON */
    bool        scan_cut;   /**< that scan was stopped by a switch to ARMED */
    uint8_t     found_n;    /**< the nodes it found */
    uint8_t     scan_left_s; /**< the running scan's time left, s, rounded
                                  up; 0 with none running */
    pnode_dev_t found[PNODE_FOUND_MAX]; /**< strongest first */
    uint16_t    hidden;     /**< other devices it heard */
    char        bound_addr[PNODE_ADDR_MAX]; /**< the bound node, "" none */
    char        bound_name[PNODE_NAME_MAX]; /**< its name, "" none */
    uint8_t     proto;      /**< the protocol its INFO named, 0 unread */
    bool        have_status; /**< a STATUS came on this link */
    bool        bat_low;    /**< it says RCNODE_ST_BAT_LOW: under 3.5V */
    bool        bat_none;   /**< it says RCNODE_ST_BAT_NONE: no cell */
    bool        bat_err;    /**< it says RCNODE_ST_BAT_ERR: read failed */
    uint16_t    vbat_mv;    /**< its cell voltage, mV */
    uint16_t    bind_refused; /**< binds the controller refused, counted:
                                   a change is a refusal to show */
    uint16_t    scan_armed; /**< scans the controller refused while ARMED
                                 or an arm waits, counted */
    uint16_t    scan_unready; /**< scans the controller refused while OFF,
                                   held, without sync or with a scan
                                   running, counted */
} pnode_view_t;

/** @brief A command of SETUP > WIRELESS for the controller. */
typedef enum {
    PNODE_CMD_NONE = 0, /**< none */
    PNODE_CMD_SCAN,     /**< SCAN FOR NODE */
    PNODE_CMD_BIND,     /**< bind the found node arg */
    PNODE_CMD_UNBIND,   /**< forget the bound node, end its link */
} pnode_cmd_kind_t;

/** @brief One command. */
typedef struct {
    uint8_t kind; /**< pnode_cmd_kind_t */
    uint8_t arg;  /**< PNODE_CMD_BIND: the index in the found list */
} pnode_cmd_t;

#ifdef __cplusplus
}
#endif
