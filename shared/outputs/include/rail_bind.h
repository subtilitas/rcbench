/**
 * @file
 * @brief What a binding write may do on the remote servo node's common rail
 *        (board 3) while a programming session is open, and what a bind
 *        does to a powered port at once.
 *
 * Without the port switches (POWER's FITTED 0) the servo rail is one
 * supply for all 9 ports, and the session holds it: every other channel
 * stays off for the whole session.  CONNECT is refused while another
 * channel is active (prog_rail_connect()); from CONNECT until the session
 * ends no write activates one (rail_bind_lock()):
 *
 *   - an OUTPUTS or OUTPUTS_HI write, or a BIND commit, that binds a
 *     driver on an output no slot drove before it (rail_bind_slots()):
 *     refused LINK_NACK_BUSY, binding WHY LINK_BW_PORT;
 *   - a PROG write that gives a port with a pin a function, or moves a
 *     port with a function to an output no port's function held before it
 *     (rail_bind_prog()): LINK_NACK_BUSY;
 *   - a PASS write that sets a SOURCE to PASS-THROUGH (pass_ctx_t.locked):
 *     LINK_NACK_BUSY.
 *
 * Each is judged at the write against the sessions as they stand at that
 * moment.  A write that leaves the active channels as they are, or
 * switches one off, is taken.  With the switches (FITTED 0x1FF) nothing
 * is locked.
 *
 * A port that is on and whose output a taken write makes a throttle goes
 * off in the same write (rail_bind_hold()), before the next pass renders
 * a frame on it.
 *
 * Pure logic, host-tested in test_rail_bind.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RAIL_BIND_H
#define RCBENCH_RAIL_BIND_H

#include <stdbool.h>
#include <stdint.h>

#include "out_bind.h"
#include "outputs.h"
#include "power_page.h"
#include "prog_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The ports whose channel no write may activate now: without the
 *        switches, while a session is open, every port but the session's.
 *
 * @param pg   The POWER page.
 * @param open A KST, Hitec or ESC wire session is open (from CONNECT or
 *             OPEN until it lets go).
 * @param k    The session's port, 0 to 8; any other value is none, and
 *             then all 9 are locked.
 * @return     Bit k for port k; 0 with the switches, with no session, or
 *             for a NULL @p pg.
 */
uint16_t rail_bind_lock(const power_page_t *pg, bool open, int k);

/**
 * @brief The ports a set of OUTPUTS registers binds a driver on.
 *
 * @param slots LINK_OS_ALL registers: both OUTPUTS pages.
 * @param r     The board's roles: port k is r->out[k].
 * @return      Bit k for port k; 0 for a NULL argument.
 */
uint16_t rail_bind_slots(const uint16_t *slots, const outbind_roles_t *r);

/**
 * @brief The ports a programming port with a pin and a function holds.
 *
 * @param pp The programming ports.
 * @param r  The board's roles.
 * @return   Bit k for port k; 0 for a NULL argument.
 */
uint16_t rail_bind_prog(const prog_port_t *pp, const outbind_roles_t *r);

/**
 * @brief Whether a write activates a locked channel.
 *
 * @param lock   rail_bind_lock().
 * @param before The active ports before the write.
 * @param after  The active ports the write leaves.
 * @return       true when a port of @p lock is in @p after and not in
 *               @p before.
 */
bool rail_bind_refuses(uint16_t lock, uint16_t before, uint16_t after);

/**
 * @brief A taken bind's throttles off at once: power_hold() with
 *        power_throttle() of the bank and the ports as the write left them.
 *
 * @param pg The POWER page.
 * @param o  The bank.
 * @param pp The programming ports; NULL for none.
 * @param r  The board's roles.
 * @return   The ports switched off; 0 for none or a NULL argument.
 */
uint16_t rail_bind_hold(power_page_t *pg, const outputs_t *o,
                        const prog_port_t *pp, const outbind_roles_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_RAIL_BIND_H */
