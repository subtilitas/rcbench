/**
 * @file node_srv.h
 * @brief The servo role (CONFIG_RCNODE_ROLE_SERVO): the remote servo
 *        node's ES3C28P.  How its parts meet.
 *
 * Task `srv` (node_srv_link.c) owns the UART link to the Core2350B, the
 * IOmcu of board 3, and the policy (node_srv_policy.h).  It polls the IOmcu
 * (node_srv_poll.h) and, between and during its exchanges, runs the pump:
 * the panel's events into the policy, the policy's step, the heartbeat on
 * IO21 and the RUN pulse on IO14 (node_srv_hb.c).  A pump runs at least
 * every 2ms while the task runs.
 *
 * The panel's operations reach `srv` through node_srv_post_*(), callable
 * from any task: a queue for KEEPALIVE, THROTTLE, ARM and SAFE, and a flag
 * for STOP that no full queue can drop.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_pages.h"
#include "node_srv_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The node's view of the IOmcu: the pages as last read. */
typedef struct {
    node_srv_pol_t pol;                     /**< the policy's state */
    bool     beat;                          /**< the heartbeat edges */
    bool     linked;                        /**< IDENTITY taken since the last loss */
    uint16_t identity[LINK_ID_ALL];         /**< IDENTITY */
    uint16_t status[LINK_ST_COUNT];         /**< STATUS */
    uint16_t power[LINK_PW_COUNT];          /**< POWER */
    uint16_t supply[LINK_SP_COUNT];         /**< SUPPLY */
    uint16_t win_a[LINK_MAX_REGS];          /**< SENSE_WIN_A */
    uint16_t win_b[LINK_MAX_REGS];          /**< SENSE_WIN_B */
    uint32_t exchanges;                     /**< requests answered, wrapping */
    uint32_t unanswered;                    /**< requests that ended unanswered */
    uint32_t nacks;                         /**< answers that were refusals */
    uint32_t refused;                       /**< frames dropped by the UART framing or decode */
    uint32_t preempted;                     /**< requests given up for the all-off writes */
    uint32_t run_pulses;                    /**< RUN pulses sent */
    uint32_t beats;                         /**< heartbeat edges sent */
} node_srv_view_t;

/* ------------------------------------------------------------ the panel */

/**
 * @brief A KEEPALIVE.
 * @param stop Its STOP flag.
 */
void node_srv_post_keepalive(bool stop);

/** @brief STOP: taken by the next pump whatever else is queued. */
void node_srv_post_stop(void);

/**
 * @brief An ARM, after the panel's 2s hold.
 * @param roles LINK_ARM_ALL, or LINK_ARM_THROTTLE and LINK_ARM_SURFACE.
 */
void node_srv_post_arm(uint16_t roles);

/** @brief SAFE (DISARM). */
void node_srv_post_safe(void);

/**
 * @brief A THROTTLE.
 * @param value 0 to 10000.
 */
void node_srv_post_throttle(uint16_t value);

/* ------------------------------------------------------------- the link */

/** @brief Start the UART and the `srv` task. */
void node_srv_link_start(void);

/**
 * @brief A copy of the view.
 * @param out Receives it.
 */
void node_srv_link_view(node_srv_view_t *out);

/**
 * @brief The console's keep-alive source: while on, `srv` takes a KEEPALIVE
 *        every 50ms and a link as from a connected panel; off is a
 *        disconnect.  For the bench without a panel; it arms nothing.
 * @param on On or off.
 */
void node_srv_link_console_ka(bool on);

/** @brief Whether the console's keep-alive source is on.  @return It. */
bool node_srv_link_console_ka_on(void);

/* ------------------------------------------------------------- the pins */

/** @brief IO21 (HEARTBEAT) and IO14 (RUN's MOSFET) as outputs, both low. */
void node_srv_hb_init(void);

/**
 * @brief Drive IO21.
 * @param level The level heartbeat_gen_step() returned.
 */
void node_srv_hb_set(bool level);

/** @brief Hold RUN low for NODE_SRV_RUN_PULSE_MS: IO14 high, then low. */
void node_srv_hb_run_pulse(void);

/** @brief Register the console's `srv` command. */
void node_srv_console_register(void);

#ifdef __cplusplus
}
#endif
