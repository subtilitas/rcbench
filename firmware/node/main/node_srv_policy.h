/**
 * @file node_srv_policy.h
 * @brief The servo node's safety decisions: the panel's keep-alive, STOP,
 *        the throttle's age, the IOmcu's silence, the switch to ARMED and
 *        whether the heartbeat on IO21 edges.  Pure C, under the host
 *        suite (test_node_srv_policy).
 *
 * The node stands between the panel (Bluetooth LE, BLE) and the Core2350B
 * (the IOmcu of board 3, on a UART at 1Mbaud).  The panel's liveness
 * reaches the Core2350B as the heartbeat; the node's decisions about it are
 * here, the pins and the UART are the caller's.
 *
 * | Event | Result |
 * | --- | --- |
 * | no panel since the start (STANDALONE) | no heartbeat, ARM 0, every ARM refused |
 * | KEEPALIVE without the STOP flag, the IOmcu answering | heartbeat edges |
 * | no KEEPALIVE for NODE_SRV_KA_MS (300ms) | latched PANEL LOST: no heartbeat, ARM 0, the all-off writes |
 * | BLE disconnect | the same at once |
 * | STOP op or a KEEPALIVE with the STOP flag | the same at once, latched as STOP |
 * | no THROTTLE for NODE_SRV_THR_MS (150ms) while armed | THROTTLE 0 written; ARMED stays |
 * | no answer from the IOmcu for NODE_SRV_IOMCU_MS (1000ms) | IOMCU LOST: SAFE, no heartbeat, the all-off writes owed |
 * | no answer for NODE_SRV_RUN_MS (3000ms) | one 10ms pulse of RUN low, once per silence |
 *
 * The all-off writes are ARM 0 and THROTTLE 0 on CONTROL, RAIL 0 and
 * PORT_WANT 0 on POWER, and OUTPUT 0 on SUPPLY.  They are owed from the
 * start, so the first exchanges after every link-up are these.
 *
 * A latch (PANEL LOST or STOP) ends only with an ARM the node takes: the
 * panel's 2s ARM hold.  While it stands the heartbeat stays withheld.
 *
 * The switch to ARMED follows the panel's (docs/Safety.md, "The first switch
 * to ARMED after a stop"): the heartbeat resumes, the IOmcu needs
 * HEARTBEAT_SETTLE_MS (100ms) of edges before it trusts the line, so the
 * node waits that long and then until STATUS shows no heartbeat fault; it
 * then writes CLEAR (0x5AFE) and the frame that arms.  A line not trusted
 * NODE_SRV_ARM_WAIT_MS (300ms) after the ARM gives the switch up.
 *
 * Every time is a 32-bit ms clock; every comparison holds across its wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timeouts, ms. @{ */
#define NODE_SRV_KA_MS       300u  /**< T_ka: the panel's KEEPALIVE age that latches PANEL LOST */
#define NODE_SRV_THR_MS      150u  /**< a THROTTLE this old is written as 0 */
#define NODE_SRV_IOMCU_MS   1000u  /**< no answer this long: IOMCU LOST */
#define NODE_SRV_RUN_MS     3000u  /**< no answer this long: one RUN pulse */
#define NODE_SRV_RUN_PULSE_MS 10u  /**< RUN held low this long */
#define NODE_SRV_ARM_WAIT_MS 300u  /**< an ARM whose line is not trusted by then is given up */
/** @} */

/** @brief The IOmcu board the node takes: 3, the remote servo node's
 *         Core2350B (OUTBIND_BOARD_RSN in shared/outputs/out_bind.h). */
#define NODE_SRV_IOMCU_BOARD 3u
/** @brief The lowest protocol the node takes: 4.41, the first with board
 *         3's pages. @{ */
#define NODE_SRV_PROTO_MAJOR 4u   /**< the major, exactly */
#define NODE_SRV_PROTO_MINOR 41u  /**< the minor, at least */
/** @} */

/** @brief What the band shows of the panel. */
typedef enum {
    NODE_SRV_STANDALONE = 0, /**< no KEEPALIVE since the start */
    NODE_SRV_LINKED,         /**< a panel, no latch */
    NODE_SRV_PANEL_LOST,     /**< latched: KEEPALIVE age or BLE disconnect */
    NODE_SRV_STOPPED,        /**< latched: STOP */
} node_srv_panel_t;

/** @brief The switch to ARMED. */
typedef enum {
    NODE_SRV_SAFE = 0,  /**< ARM 0 */
    NODE_SRV_ARM_WAIT,  /**< an ARM taken; the line is not yet trusted */
    NODE_SRV_ARM_WRITE, /**< CLEAR and the arming frame are due */
    NODE_SRV_ARMED,     /**< the IOmcu took the arming frame */
} node_srv_arm_t;

/** @brief Why an ARM is refused; NODE_SRV_ARM_OK takes it. */
typedef enum {
    NODE_SRV_ARM_OK = 0,   /**< taken, or the same roles already armed */
    NODE_SRV_ARM_NO_PANEL, /**< no BLE link or no KEEPALIVE within NODE_SRV_KA_MS */
    NODE_SRV_ARM_STOP,     /**< the last KEEPALIVE carries the STOP flag */
    NODE_SRV_ARM_NO_IOMCU, /**< the IOmcu lost or not board 3 at 4.41 or later */
    NODE_SRV_ARM_ROLES,    /**< not LINK_ARM_ALL and not a non-empty set of
                                LINK_ARM_THROTTLE and LINK_ARM_SURFACE */
    NODE_SRV_ARM_OFF_OWED, /**< the all-off writes have not all been answered */
    NODE_SRV_ARM_BUSY,     /**< a switch to ARMED waits, or other roles are armed */
} node_srv_arm_why_t;

/** @brief Events for the panel (SRV_EVENT), collected until taken. @{ */
#define NODE_SRV_EV_STOP        0x01u /**< a STOP taken */
#define NODE_SRV_EV_KA_LOST     0x02u /**< KEEPALIVE older than NODE_SRV_KA_MS */
#define NODE_SRV_EV_BLE_LOST    0x04u /**< the BLE link ended */
#define NODE_SRV_EV_IOMCU_LOST  0x08u /**< no answer for NODE_SRV_IOMCU_MS */
#define NODE_SRV_EV_ARM_FAILED  0x10u /**< a switch to ARMED given up or refused */
#define NODE_SRV_EV_ARMED       0x20u /**< the IOmcu took the arming frame */
/** @} */

/** @brief The policy's state.  The caller writes no field. */
typedef struct {
    node_srv_panel_t panel;   /**< the panel as the band shows it */
    node_srv_arm_t   arm;     /**< the switch to ARMED */
    uint16_t roles;           /**< the ARM value asked for or in force */
    bool     ble;             /**< a BLE central is connected */
    bool     ka_seen;         /**< a KEEPALIVE since the last latch or start */
    bool     ka_stop;         /**< the last KEEPALIVE carried the STOP flag */
    uint32_t ka_ms;           /**< when the last KEEPALIVE came */
    bool     thr_seen;        /**< a THROTTLE since the switch to ARMED */
    uint16_t thr;             /**< the last THROTTLE, 0 to 10000 */
    uint32_t thr_ms;          /**< when it came */
    bool     answered;        /**< the IOmcu answered since the start */
    uint32_t answer_ms;       /**< the last answer, or the start */
    bool     iomcu_lost;      /**< no answer for NODE_SRV_IOMCU_MS */
    bool     identity_ok;     /**< IDENTITY read since the last link-up: board 3, 4.41 or later */
    bool     run_done;        /**< the RUN pulse of this silence is out */
    uint32_t arm_ms;          /**< when the ARM was taken */
    uint16_t faults;          /**< STATUS FAULTS as last read */
    uint32_t faults_ms;       /**< when */
    bool     off_owed;        /**< the all-off writes are owed */
    uint8_t  off_gen;         /**< counts each time they are owed anew */
    bool     ctl_now;         /**< CONTROL is to be written at once */
    uint8_t  events;          /**< NODE_SRV_EV_*, not yet taken */
} node_srv_pol_t;

/** @brief What the caller does after node_srv_pol_step(). */
typedef struct {
    bool     beat;      /**< the heartbeat edges (heartbeat_gen_step()'s alive) */
    bool     off;       /**< the all-off writes are owed: before anything else */
    uint8_t  off_gen;   /**< their generation, for node_srv_pol_off_done() */
    bool     arm_write; /**< write CLEAR, then the arming frame */
    bool     ctl_now;   /**< write CONTROL now, not at its next 50ms */
    uint16_t arm;       /**< ARM for CONTROL's write: 0, or the roles while ARMED */
    uint16_t throttle;  /**< THROTTLE for it: 0 unless ARMED with a fresh THROTTLE */
    bool     run_pulse; /**< pulse RUN low for NODE_SRV_RUN_PULSE_MS now */
    bool     status_fast; /**< read STATUS every 50ms: a fault bit or an ARM waits */
} node_srv_out_t;

/**
 * @brief Start: STANDALONE, SAFE, the IOmcu not yet answered, the all-off
 *        writes owed.
 * @param p      The state.
 * @param now_ms The clock; the IOmcu's silence counts from here.
 */
void node_srv_pol_init(node_srv_pol_t *p, uint32_t now_ms);

/**
 * @brief A KEEPALIVE from the panel.
 * @param p      The state.
 * @param now_ms The clock.
 * @param stop   Its STOP flag: a STOP as node_srv_pol_stop().
 */
void node_srv_pol_keepalive(node_srv_pol_t *p, uint32_t now_ms, bool stop);

/**
 * @brief STOP: SAFE, the all-off writes, no heartbeat, latched until an ARM
 *        is taken.  Taken in every state, STANDALONE included.
 * @param p      The state.
 * @param now_ms The clock.
 */
void node_srv_pol_stop(node_srv_pol_t *p, uint32_t now_ms);

/**
 * @brief The BLE link: a disconnect is a panel loss at once.
 * @param p      The state.
 * @param now_ms The clock.
 * @param up     Whether a central is connected.
 */
void node_srv_pol_ble(node_srv_pol_t *p, uint32_t now_ms, bool up);

/**
 * @brief A THROTTLE from the panel.
 * @param p      The state.
 * @param now_ms The clock.
 * @param value  0 to 10000; above is taken as 10000.
 */
void node_srv_pol_throttle(node_srv_pol_t *p, uint32_t now_ms, uint16_t value);

/**
 * @brief The panel's ARM: checks the preconditions and, when they hold,
 *        ends a latch and starts the switch to ARMED.
 * @param p      The state.
 * @param now_ms The clock.
 * @param roles  The ARM value: LINK_ARM_ALL or LINK_ARM_THROTTLE and
 *               LINK_ARM_SURFACE.
 * @return NODE_SRV_ARM_OK, or why it is refused.
 */
node_srv_arm_why_t node_srv_pol_arm(node_srv_pol_t *p, uint32_t now_ms,
                                    uint16_t roles);

/**
 * @brief The panel's SAFE (DISARM): ARM 0 written at once.  No latch.
 * @param p The state.
 */
void node_srv_pol_safe(node_srv_pol_t *p);

/**
 * @brief An exchange with the IOmcu answered (a NACK included).
 * @param p      The state.
 * @param now_ms The clock.
 */
void node_srv_pol_answer(node_srv_pol_t *p, uint32_t now_ms);

/**
 * @brief IDENTITY read after a link-up.
 * @param p        The state.
 * @param major    LINK_ID_PROTOCOL_MAJOR.
 * @param minor    LINK_ID_PROTOCOL_MINOR.
 * @param hardware LINK_ID_HARDWARE, the board.
 * @return Whether the node takes this IOmcu.
 */
bool node_srv_pol_identity(node_srv_pol_t *p, uint16_t major, uint16_t minor,
                           uint16_t hardware);

/**
 * @brief STATUS FAULTS as read.
 * @param p      The state.
 * @param now_ms The clock.
 * @param faults The register.
 */
void node_srv_pol_status(node_srv_pol_t *p, uint32_t now_ms, uint16_t faults);

/**
 * @brief The all-off writes were all answered.  A generation other than the
 *        latest changes nothing: they were owed again while on their way.
 * @param p   The state.
 * @param gen node_srv_out_t's off_gen when they started.
 */
void node_srv_pol_off_done(node_srv_pol_t *p, uint8_t gen);

/**
 * @brief CONTROL was written after node_srv_out_t's ctl_now.
 * @param p The state.
 */
void node_srv_pol_ctl_done(node_srv_pol_t *p);

/**
 * @brief How CLEAR and the arming frame ended.
 * @param p      The state.
 * @param now_ms The clock.
 * @param taken  Both acknowledged: ARMED.  Otherwise SAFE and
 *               NODE_SRV_EV_ARM_FAILED.
 */
void node_srv_pol_arm_done(node_srv_pol_t *p, uint32_t now_ms, bool taken);

/**
 * @brief The IOmcu refused CONTROL's ARM while ARMED (it restarted, or its
 *        heartbeat or silence latch fell): SAFE, the all-off writes and
 *        NODE_SRV_EV_ARM_FAILED.
 * @param p The state.
 */
void node_srv_pol_refused(node_srv_pol_t *p);

/** @brief What an exchange with the IOmcu writes, for node_srv_pol_keep(). */
typedef enum {
    NODE_SRV_X_READ = 0, /**< a read: writes nothing */
    NODE_SRV_X_OFF,      /**< one of the all-off writes */
    NODE_SRV_X_ARMING,   /**< CLEAR or the arming frame */
    NODE_SRV_X_CONTROL,  /**< CONTROL's ARM and THROTTLE at the poll */
} node_srv_x_t;

/**
 * @brief Whether an exchange may go on, checked before it is sent and at
 *        every pump while it waits.  An exchange that may not is given up
 *        at once, unsent or unanswered, so a SAFE, a STOP or a loss taken
 *        while it waits never lets a write from before it reach the IOmcu
 *        later, and the writes that follow from it are not held up.
 *
 * | Exchange | Given up when |
 * | --- | --- |
 * | an all-off write | never |
 * | any other | the all-off writes are owed |
 * | CLEAR or the arming frame | the switch to ARMED is no longer at its writes (SAFE, STOP, a loss, the 300ms) |
 * | CONTROL with a non-zero ARM | the policy's ARM differs from that value |
 * | a read | CONTROL is owed at once (a SAFE's ARM 0) |
 *
 * @param p    The state.
 * @param out  The last node_srv_pol_step().
 * @param kind What the exchange writes.
 * @param arm  The ARM value a CONTROL exchange carries; 0 otherwise.
 * @return true while it may go on.
 */
bool node_srv_pol_keep(const node_srv_pol_t *p, const node_srv_out_t *out,
                       node_srv_x_t kind, uint16_t arm);

/**
 * @brief Advance the clocks and say what to do now.
 * @param p      The state.
 * @param now_ms The clock.
 * @param out    Receives what to do.
 */
void node_srv_pol_step(node_srv_pol_t *p, uint32_t now_ms, node_srv_out_t *out);

/**
 * @brief Take the events collected since the last call.
 * @param p The state.
 * @return NODE_SRV_EV_* bits.
 */
uint8_t node_srv_pol_events(node_srv_pol_t *p);

#ifdef __cplusplus
}
#endif
