/**
 * @file
 * @brief The IOmcu's decision to arm, once a pass and at every CONTROL write.
 *
 * The bank is armed while four things hold: the ARM register is set, the
 * heartbeat is trusted, the link is out of failsafe and the arm latch is
 * clear.  The latch is what keeps a cause that took the outputs down from
 * being undone by the ARM the panel goes on writing every 50 ms:
 *
 *     set      at start-up (link_dev_init()), on the edge into link silence,
 *              on the edge where the heartbeat stops being trusted, and by
 *              an ARM that is refused
 *     cleared  by CLEAR on the control page, and by nothing else
 *
 * The supply's ON and the STATUS page do not read the latch.  They read the
 * link failsafe and the heartbeat, so an IOmcu that has just started
 * takes a supply ON and reports no fault.
 *
 * Pure C with the clock passed in; the firmware's main loop samples the pin
 * and acts on what is returned.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "heartbeat.h"
#include "link_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a pass decided. */
typedef struct {
    /** The heartbeat stopped being trusted or the link went silent in this
     *  pass: the outputs come down now.  True on the edge only. */
    bool off;
    /** @brief Whether the bank is armed for this pass. */
    bool arm;
    /** The roles armed for this pass, LINK_ARM_ROLES bits: link_arm_roles()
     *  of ARM while arm is true, else 0.  An output of a role outside it
     *  carries no pulse. */
    uint16_t roles;
} safety_gate_t;

/**
 * @brief Whether a write of ARM is taken.
 *
 * Heartbeat trusted, link out of failsafe, latch clear.
 *
 * @param beat  Heartbeat monitor.
 * @param dev   Link device state.
 * @return      true when an ARM is taken.
 */
bool safety_gate_may_arm(const heartbeat_mon_t *beat, const link_dev_t *dev);

/**
 * @brief Whether the supply's output may be on.
 *
 * Heartbeat trusted and link out of failsafe.  The arm latch is not part of
 * it.
 *
 * @param beat  Heartbeat monitor.
 * @param dev   Link device state.
 * @return      true when the supply may be on.
 */
bool safety_gate_supply_ok(const heartbeat_mon_t *beat,
                           const link_dev_t *dev);

/**
 * @brief One pass of the main loop, after the link has been served.
 *
 * @p line_edge is whether the heartbeat pin changed level since the last
 * pass.  @p control is the CONTROL page, LINK_CT_COUNT registers: on an off
 * edge ARM and THROTTLE are zeroed there, so a read shows what the bank
 * holds.  Both watchdogs are judged before the arm, so a pass never arms on
 * the answer of the pass before.
 *
 * @param beat       Heartbeat monitor.
 * @param line_edge  true when the heartbeat pin changed level.
 * @param dev        Link device state.
 * @param control    CONTROL page, LINK_CT_COUNT registers.
 * @param now_ms     Current time, in milliseconds.
 * @return           The decision of this pass.
 */
safety_gate_t safety_gate_step(heartbeat_mon_t *beat, bool line_edge,
                               link_dev_t *dev, uint16_t *control,
                               uint32_t now_ms);

/**
 * @brief Whether the bank is armed for this pass.
 *
 * The pass's decision (safety_gate_t.arm, @p gate_arm) and two holds only the
 * firmware can see.
 *
 * Not while a programming port is out of PWM (@p quiet_mask, the ports
 * kst_port_quiet_mask() names, non-zero): programming and an armed bank
 * are exclusive at this end too, and the port takes no ENTER while the
 * bank is armed.
 *
 * Nor while a firmware update runs or its image waits for BUY
 * (@p update_holds): the control page refuses the ARM, and this holds the
 * bank down for an ARM written before the session began.
 *
 * True only when @p gate_arm is true, @p quiet_mask is 0 and
 * @p update_holds is false.
 *
 * @param gate_arm      The pass's arm decision.
 * @param quiet_mask    Programming ports out of PWM; 0 when none.
 * @param update_holds  true while a firmware update holds the bank down.
 * @return              true when the bank may be armed.
 */
bool safety_gate_arm_now(bool gate_arm, uint16_t quiet_mask,
                         bool update_holds);

/**
 * @brief A write to the CONTROL page.
 *
 * link_control_write() with this end's answer to whether the bench may arm,
 * and the CLEAR acted on.
 *
 * An ARM refused with LINK_NACK_NOT_ARMED sets the latch.  A CLEAR
 * acknowledged while the heartbeat is not yet trusted is followed by such a
 * refusal, and the line becoming trusted a few milliseconds later would
 * otherwise leave this end unlatched with nobody armed.
 *
 * @param control  CONTROL page, LINK_CT_COUNT registers.
 * @param off      First register of the window.
 * @param n        Registers in the window.
 * @param in       The window's values.
 * @param beat     Heartbeat monitor.
 * @param dev      Link device state.
 * @return         0 when the window is stored, else the NACK reason.
 */
uint8_t safety_gate_control_write(uint16_t *control, uint8_t off, uint8_t n,
                                  const uint16_t *in,
                                  const heartbeat_mon_t *beat,
                                  link_dev_t *dev);

/**
 * @brief The STATUS page's state register and the two fault bits this
 *        decision owns.
 *
 * The two bits are LINK_FAULT_LINK_SILENT and LINK_FAULT_HEARTBEAT.  The
 * latch is not reported: after a start the state is idle and both bits are
 * clear.
 *
 * @param beat     Heartbeat monitor.
 * @param dev      Link device state.
 * @param control  CONTROL page, LINK_CT_COUNT registers.
 * @param state    Receives the state register.
 * @param faults   Receives the fault bitmap.
 */
void safety_gate_status(const heartbeat_mon_t *beat, const link_dev_t *dev,
                        const uint16_t *control, uint16_t *state,
                        uint16_t *faults);

/**
 * @brief The panel's reading of a STATUS fault bitmap.
 *
 * Whether the far end trusts the heartbeat.
 *
 * @param faults  STATUS fault bitmap.
 * @return        true when the heartbeat is trusted.
 */
bool safety_gate_line_trusted(uint16_t faults);

#ifdef __cplusplus
}
#endif
