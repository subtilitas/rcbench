/*
 * The coprocessor's decision to arm, once a pass and at every CONTROL
 * write.
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
 * link failsafe and the heartbeat, so a coprocessor that has just started
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

/** What a pass decided. */
typedef struct {
    /** The heartbeat stopped being trusted or the link went silent in this
     *  pass: the outputs come down now.  True on the edge only. */
    bool off;
    /** Whether the bank is armed for this pass. */
    bool arm;
} safety_gate_t;

/**
 * Whether a write of ARM is taken: heartbeat trusted, link out of failsafe,
 * latch clear.
 */
bool safety_gate_may_arm(const heartbeat_mon_t *beat, const link_dev_t *dev);

/**
 * Whether the supply's output may be on: heartbeat trusted and link out of
 * failsafe.  The arm latch is not part of it.
 */
bool safety_gate_supply_ok(const heartbeat_mon_t *beat,
                           const link_dev_t *dev);

/**
 * One pass of the main loop, after the link has been served.
 *
 * @p line_edge is whether the heartbeat pin changed level since the last
 * pass.  @p control is the CONTROL page, LINK_CT_COUNT registers: on an off
 * edge ARM and THROTTLE are zeroed there, so a read shows what the bank
 * holds.  Both watchdogs are judged before the arm, so a pass never arms on
 * the answer of the pass before.
 */
safety_gate_t safety_gate_step(heartbeat_mon_t *beat, bool line_edge,
                               link_dev_t *dev, uint16_t *control,
                               uint32_t now_ms);

/**
 * A write to the CONTROL page: link_control_write() with this end's answer
 * to whether the bench may arm, and the CLEAR acted on.
 *
 * An ARM refused with LINK_NACK_NOT_ARMED sets the latch.  A CLEAR
 * acknowledged while the heartbeat is not yet trusted is followed by such a
 * refusal, and the line becoming trusted a few milliseconds later would
 * otherwise leave this end unlatched with nobody armed.
 *
 * Returns 0 when the window is stored, else the NACK reason.
 */
uint8_t safety_gate_control_write(uint16_t *control, uint8_t off, uint8_t n,
                                  const uint16_t *in,
                                  const heartbeat_mon_t *beat,
                                  link_dev_t *dev);

/**
 * The STATUS page's state register and the two fault bits this decision
 * owns (LINK_FAULT_LINK_SILENT, LINK_FAULT_HEARTBEAT).  The latch is not
 * reported: after a start the state is idle and both bits are clear.
 */
void safety_gate_status(const heartbeat_mon_t *beat, const link_dev_t *dev,
                        const uint16_t *control, uint16_t *state,
                        uint16_t *faults);

/**
 * The panel's reading of a STATUS fault bitmap: whether the far end trusts
 * the heartbeat.
 */
bool safety_gate_line_trusted(uint16_t faults);

#ifdef __cplusplus
}
#endif
