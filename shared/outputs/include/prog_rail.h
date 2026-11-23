/**
 * @file
 * @brief Programming on the remote servo node's common rail (board 3): a
 *        KST, Hitec or ESC wire session's supply is the PD mini behind the
 *        master rail switch and the port switches of the POWER page.
 *
 * The session's port k is the output whose pin its programming port (or
 * the ESC wire target) holds.  What the session sees as its supply
 * (prog_rail_view()) is the PD mini's view through that port's switch:
 * on only while the PD mini is on and port k is on, off as soon as port k
 * is off.  What it asks (prog_rail_set()) acts on the switches:
 *
 *   - OFF, the first half of every power cycle (KST rail cycle 1000ms,
 *     classic Hitec entry 500ms): without the port switches (FITTED 0)
 *     RAIL 0, so every servo on the rail cycles; with them PORT_WANT bit k
 *     only.  The PD mini stays as it is.  An OFF is always taken.
 *   - ON at a voltage: refused (false, nothing switched) for a port that
 *     is not known, after the panel's OFF of the port during the session
 *     (until the session lets go), without a live heartbeat or on a chip
 *     before A4, before the INA3221s hold their critical limits, for a
 *     port whose output is a throttle, while the BEC check found
 *     LINK_PW_BEC_MV or more on any port, and without the switches while
 *     another port's channel is active.  Otherwise the PD mini is set when
 *     its voltage differs or no other port shares it, and RAIL 1 and (with
 *     the switches) bit k are driven.  With the switches, a voltage other
 *     than the PD mini's set point switches every other port off first
 *     and keeps them off until the session lets go: they come back only
 *     with the panel's next write, its 2s hold.  The PD mini is set off and
 *     on again only while port k is off; with port k on its own rule holds
 *     (supply_page_port_set(): a voltage is never raised under a servo).
 *
 * CONNECT (KST ENTER and REBOOT from PWM, Hitec OPEN, ESC wire OPEN) is
 * refused before anything is switched by prog_rail_connect(): a BEC found,
 * and without the switches another active channel.
 *
 * The panel's POWER writes go through prog_rail_write(): while a session
 * holds the port, a write that would switch port k on is refused
 * (LINK_NACK_BUSY), and so is a new PORT_WANT bit while the session runs
 * the rail at another voltage.  The panel's OFF is taken at once for every
 * other port; port k stays on while the session writes to the servo's
 * memory (prog_rail_tell()), SUPPLY_PAGE_WRITE_WAIT_MS (10000ms) at most
 * from the OFF, as the SUPPLY page holds the PD mini (supply_page.h).  From
 * a RAIL 0, or a PORT_WANT without bit k while it was set, the session's
 * own ON is refused until it lets go.  A lost heartbeat, the link failsafe
 * and a trip are not held (power_off(), prog_rail_off()).
 *
 * Pure logic, host-tested in test_prog_rail, test_prog_rail_kst and
 * test_prog_rail_hitec.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG_RAIL_H
#define RCBENCH_PROG_RAIL_H

#include <stdbool.h>
#include <stdint.h>

#include "power_page.h"
#include "supply_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief No session port: prog_rail_t.port. */
#define PROG_RAIL_NO_PORT (-1)

/** @brief Why a session's CONNECT or ON is refused. */
typedef enum {
    PROG_RAIL_OK = 0,        /**< taken                                    */
    PROG_RAIL_PORT,          /**< no port known                            */
    PROG_RAIL_OFF_STANDS,    /**< the panel's OFF stands for the session   */
    PROG_RAIL_UNSAFE,        /**< no live heartbeat, or a chip before A4   */
    PROG_RAIL_LIMITS,        /**< the critical limits are not held         */
    PROG_RAIL_THROTTLE,      /**< the port's output is a throttle          */
    PROG_RAIL_BEC,           /**< the BEC check found a port at 0.5V       */
    PROG_RAIL_OTHERS,        /**< no switches and another channel active   */
} prog_rail_refusal_t;

/** @brief What a session is judged against, besides the pages. */
typedef struct {
    power_ctx_t power;   /**< heartbeat, critical limits, throttle ports   */
    uint16_t    active;  /**< bit k: port k's channel is active            */
} prog_rail_ctx_t;

/** @brief The session's state on the rail. */
typedef struct {
    int8_t   port;     /**< the session's port k, 0 to 8, or
                            PROG_RAIL_NO_PORT                           */
    bool     held;     /**< a session holds the port's supply           */
    bool     writing;  /**< it writes to the servo's memory             */
    bool     sticky;   /**< the panel's OFF stands: no ON from the
                            session until it lets go                    */
    bool     alone;    /**< the session's voltage differs from the
                            rail's: the other ports stay off            */
    uint16_t dropped;  /**< the ports the session switched off          */
    bool     kept;     /**< a panel's OFF waits for the write           */
    uint32_t kept_t0;  /**< when that OFF was written, ms               */
} prog_rail_t;

/**
 * @brief No session, no port.
 *
 * @param r The state, overwritten; nothing for NULL.
 */
void prog_rail_init(prog_rail_t *r);

/**
 * @brief The port the session works on.
 *
 * @param r The state; nothing for NULL.
 * @param k The port, 0 to 8; any other value is none.
 */
void prog_rail_port(prog_rail_t *r, int k);

/**
 * @brief CONNECT's rules: a BEC found on any port; without the switches,
 *        another port's channel active.
 *
 * @param pg  The POWER page.
 * @param k   The session's port; a value past 8 is judged as none.
 * @param ctx The active ports.
 * @return    PROG_RAIL_OK, PROG_RAIL_BEC or PROG_RAIL_OTHERS;
 *            PROG_RAIL_PORT for a NULL argument.
 */
prog_rail_refusal_t prog_rail_connect(const power_page_t *pg, int k,
                                      const prog_rail_ctx_t *ctx);

/**
 * @brief Every rule of a session's ON, in the order of the enum.
 *
 * @param r   The state.
 * @param pg  The POWER page.
 * @param ctx What the ON is judged against.
 * @return    PROG_RAIL_OK, or the first rule that refuses.
 */
prog_rail_refusal_t prog_rail_judge(const prog_rail_t *r,
                                    const power_page_t *pg,
                                    const prog_rail_ctx_t *ctx);

/**
 * @brief The supply as the session sees it: the PD mini's view through
 *        port k's switch.
 *
 * @param r   The state.
 * @param pg  The POWER page.
 * @param pd  The PD mini's view (supply_page_port_view()).
 * @param out Takes it; nothing for a NULL argument.
 */
void prog_rail_view(const prog_rail_t *r, const power_page_t *pg,
                    const supply_port_view_t *pd, supply_port_view_t *out);

/**
 * @brief The session's request, in place of the PD mini's.
 *
 * @param r   The state.
 * @param pg  The POWER page.
 * @param sp  The SUPPLY page.
 * @param on  true for ON.
 * @param mv  The voltage for ON, mV.
 * @param ma  The current limit for ON, mA.
 * @param ctx What the ON is judged against.
 * @return    true when taken; an OFF always is (without a port it switches
 *            nothing); false for a NULL argument.
 */
bool prog_rail_set(prog_rail_t *r, power_page_t *pg, supply_page_t *sp,
                   bool on, uint16_t mv, uint16_t ma,
                   const prog_rail_ctx_t *ctx);

/**
 * @brief The panel's POWER write with the session's rules (power_write()).
 *
 * @param r    The state.
 * @param pg   The POWER page.
 * @param off  First register.
 * @param n    Registers.
 * @param in   The values.
 * @param ctx  What the write is judged against.
 * @param now  The clock, ms: an OFF held for a write counts from here.
 * @return     power_write()'s answer, or LINK_NACK_BUSY for a write that
 *             would switch the session's port on, or that adds a PORT_WANT
 *             bit while the session runs the rail at another voltage; a
 *             write starting with RAIL 0 switches the rail off even then.
 *             LINK_NACK_BAD_RANGE for a NULL argument.
 */
uint8_t prog_rail_write(prog_rail_t *r, power_page_t *pg, uint8_t off,
                        uint8_t n, const uint16_t *in,
                        const prog_rail_ctx_t *ctx, uint32_t now);

/**
 * @brief The session's hold and write, after every page write that can
 *        move them and once a pass: what the panel's OFF waits for.  The
 *        session letting go ends its standing OFF and its other-ports
 *        rule; a held OFF ends with the write or SUPPLY_PAGE_WRITE_WAIT_MS
 *        after it was written.
 *
 * @param r       The state; nothing for NULL.
 * @param pg      The POWER page; nothing for NULL.
 * @param held    A session holds the port's supply.
 * @param writing It writes to the servo's memory.
 * @param now     The clock, ms.
 */
void prog_rail_tell(prog_rail_t *r, power_page_t *pg, bool held,
                    bool writing, uint32_t now);

/**
 * @brief A lost heartbeat, the link failsafe or a trip: the
 *        caller's power_off() is not held, and a session that holds the
 *        port gets no ON until it lets go.
 *
 * @param r The state; nothing for NULL.
 */
void prog_rail_off(prog_rail_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG_RAIL_H */
