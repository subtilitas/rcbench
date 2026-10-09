/*
 * SPDX-License-Identifier: MIT
 */

#include "safety_gate.h"

#include <stddef.h>

#include "link_control.h"
#include "link_msg.h"
#include "link_pages.h"

bool safety_gate_may_arm(const heartbeat_mon_t *beat, const link_dev_t *dev)
{
    return safety_gate_supply_ok(beat, dev) && !link_dev_arm_latched(dev);
}

bool safety_gate_supply_ok(const heartbeat_mon_t *beat, const link_dev_t *dev)
{
    return beat != NULL && dev != NULL && beat->alive && !dev->failsafe;
}

safety_gate_t safety_gate_step(heartbeat_mon_t *beat, bool line_edge,
                               link_dev_t *dev, uint16_t *control,
                               uint32_t now_ms)
{
    safety_gate_t g = { false, false };
    if (beat == NULL || dev == NULL || control == NULL) {
        return g;
    }

    /*
     * The line first.  Silence generates no event, so the monitor is asked
     * on every pass whether or not an edge arrived.  The edge out of trust
     * latches: the monitor trusts the line again after HEARTBEAT_GOOD_RUN
     * good intervals, 80 ms at the panel's rate, and the panel's next
     * write of ARM = 1 can be later than that.
     */
    const bool was_alive = beat->alive;
    if (line_edge) {
        heartbeat_mon_edge(beat, now_ms);
    }
    if (!heartbeat_mon_alive(beat, now_ms) && was_alive) {
        link_dev_latch_arm(dev);
        g.off = true;
    }
    /* Then the link; its edge latches in link_dev_tick(). */
    if (link_dev_tick(dev, now_ms)) {
        g.off = true;
    }

    if (g.off) {
        control[LINK_CT_ARM]      = 0u;
        control[LINK_CT_THROTTLE] = 0u;
    }
    g.arm = control[LINK_CT_ARM] != 0u && safety_gate_may_arm(beat, dev);
    return g;
}

uint8_t safety_gate_control_write(uint16_t *control, uint8_t off, uint8_t n,
                                  const uint16_t *in,
                                  const heartbeat_mon_t *beat,
                                  link_dev_t *dev)
{
    if (control == NULL || in == NULL || dev == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    bool cleared = false;
    const uint8_t nack = link_control_write(control, off, n, in,
                                            safety_gate_may_arm(beat, dev),
                                            &cleared);
    if (nack == LINK_NACK_NOT_ARMED) {
        link_dev_latch_arm(dev);
    }
    if (nack != 0u) {
        return nack;
    }
    if (cleared) {
        /*
         * The clock of this pass, as recorded by the dispatcher, not a
         * fresh read.  A fresh read is later than the `now` that
         * link_dev_tick() receives in the same pass, and the wrap-safe
         * comparison there reads a timestamp in the future as
         * 4,294,967,295 ms of silence, which fires the failsafe at once.
         */
        link_dev_clear_failsafe(dev, dev->last_request_ms);
    }
    return 0u;
}

void safety_gate_status(const heartbeat_mon_t *beat, const link_dev_t *dev,
                        const uint16_t *control, uint16_t *state,
                        uint16_t *faults)
{
    const bool silent = dev == NULL || dev->failsafe;
    const bool alive  = beat != NULL && beat->alive;
    if (state != NULL) {
        if (silent || !alive) {
            *state = (uint16_t)LINK_STATE_FAILSAFE;
        } else {
            *state = (control != NULL && control[LINK_CT_ARM] != 0u)
                         ? (uint16_t)LINK_STATE_ARMED
                         : (uint16_t)LINK_STATE_IDLE;
        }
    }
    if (faults != NULL) {
        uint16_t f = 0u;
        if (silent) {
            f |= (uint16_t)LINK_FAULT_LINK_SILENT;
        }
        if (!alive) {
            f |= (uint16_t)LINK_FAULT_HEARTBEAT;
        }
        *faults = f;
    }
}

bool safety_gate_line_trusted(uint16_t faults)
{
    return (faults & (uint16_t)LINK_FAULT_HEARTBEAT) == 0u;
}
