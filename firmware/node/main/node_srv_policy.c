/**
 * @file node_srv_policy.c
 * @brief The servo node's safety decisions (node_srv_policy.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_srv_policy.h"

#include <string.h>

#include "heartbeat.h"
#include "link_pages.h"
#include "mstime.h"

/**
 * @brief Whether an ARM value is one the IOmcu takes: LINK_ARM_ALL, or a
 *        non-empty set of LINK_ARM_ROLES.
 * @param roles The value.
 * @return true when it is.
 */
static bool roles_valid(uint16_t roles)
{
    if (roles == LINK_ARM_ALL) {
        return true;
    }
    return roles != 0u && (roles & (uint16_t)~LINK_ARM_ROLES) == 0u;
}

/**
 * @brief SAFE: no switch to ARMED, no throttle; CONTROL written at once
 *        when it was not SAFE.
 * @param p The state.
 */
static void disarm(node_srv_pol_t *p)
{
    if (p->arm != NODE_SRV_SAFE) {
        p->ctl_now = true;
    }
    p->arm = NODE_SRV_SAFE;
    p->thr_seen = false;
    p->thr = 0u;
}

/**
 * @brief Owe the all-off writes again, with a new generation.
 * @param p The state.
 */
static void owe_off(node_srv_pol_t *p)
{
    p->off_owed = true;
    ++p->off_gen;
}

/**
 * @brief A latch: SAFE, the all-off writes, no heartbeat until an ARM.
 * @param p    The state.
 * @param kind NODE_SRV_PANEL_LOST or NODE_SRV_STOPPED.
 * @param ev   The event to report, NODE_SRV_EV_*.
 */
static void latch(node_srv_pol_t *p, node_srv_panel_t kind, uint8_t ev)
{
    disarm(p);
    if (p->panel != kind) {
        p->events |= ev;
        owe_off(p);
    }
    p->panel = kind;
}

void node_srv_pol_init(node_srv_pol_t *p, uint32_t now_ms)
{
    memset(p, 0, sizeof *p);
    p->panel = NODE_SRV_STANDALONE;
    p->arm = NODE_SRV_SAFE;
    p->answer_ms = now_ms;
    p->off_owed = true;
}

void node_srv_pol_stop(node_srv_pol_t *p, uint32_t now_ms)
{
    (void)now_ms;
    latch(p, NODE_SRV_STOPPED, NODE_SRV_EV_STOP);
}

void node_srv_pol_keepalive(node_srv_pol_t *p, uint32_t now_ms, bool stop)
{
    p->ka_seen = true;
    p->ka_ms = now_ms;
    p->ka_stop = stop;
    if (stop) {
        node_srv_pol_stop(p, now_ms);
    } else if (p->panel == NODE_SRV_STANDALONE) {
        p->panel = NODE_SRV_LINKED;
    }
}

void node_srv_pol_ble(node_srv_pol_t *p, uint32_t now_ms, bool up)
{
    (void)now_ms;
    if (!up && p->ble && p->panel == NODE_SRV_LINKED) {
        latch(p, NODE_SRV_PANEL_LOST, NODE_SRV_EV_BLE_LOST);
    }
    if (!up) {
        /* A KEEPALIVE from before the disconnect proves nothing after it. */
        p->ka_seen = false;
    }
    p->ble = up;
}

void node_srv_pol_throttle(node_srv_pol_t *p, uint32_t now_ms, uint16_t value)
{
    if (p->arm != NODE_SRV_ARMED) {
        return;
    }
    p->thr = value > LINK_THROTTLE_MAX ? (uint16_t)LINK_THROTTLE_MAX : value;
    p->thr_ms = now_ms;
    p->thr_seen = true;
}

node_srv_arm_why_t node_srv_pol_arm(node_srv_pol_t *p, uint32_t now_ms,
                                    uint16_t roles)
{
    if (!roles_valid(roles)) {
        return NODE_SRV_ARM_ROLES;
    }
    if (!p->ble || !p->ka_seen || mstime_elapsed(now_ms, p->ka_ms, NODE_SRV_KA_MS)) {
        return NODE_SRV_ARM_NO_PANEL;
    }
    if (p->ka_stop) {
        return NODE_SRV_ARM_STOP;
    }
    if (!p->answered || p->iomcu_lost || !p->identity_ok
        || mstime_elapsed(now_ms, p->answer_ms, NODE_SRV_IOMCU_MS)) {
        return NODE_SRV_ARM_NO_IOMCU;
    }
    if (p->arm == NODE_SRV_ARMED) {
        return roles == p->roles ? NODE_SRV_ARM_OK : NODE_SRV_ARM_BUSY;
    }
    if (p->arm != NODE_SRV_SAFE) {
        return NODE_SRV_ARM_BUSY;
    }
    if (p->off_owed) {
        return NODE_SRV_ARM_OFF_OWED;
    }
    p->panel = NODE_SRV_LINKED;
    p->arm = NODE_SRV_ARM_WAIT;
    p->arm_ms = now_ms;
    p->roles = roles;
    p->thr_seen = false;
    p->thr = 0u;
    return NODE_SRV_ARM_OK;
}

void node_srv_pol_safe(node_srv_pol_t *p)
{
    disarm(p);
    p->ctl_now = true;
}

void node_srv_pol_answer(node_srv_pol_t *p, uint32_t now_ms)
{
    p->answered = true;
    p->answer_ms = now_ms;
    p->iomcu_lost = false;
    p->run_done = false;
}

bool node_srv_pol_identity(node_srv_pol_t *p, uint16_t major, uint16_t minor,
                           uint16_t hardware)
{
    p->identity_ok = major == NODE_SRV_PROTO_MAJOR
                     && minor >= NODE_SRV_PROTO_MINOR
                     && hardware == NODE_SRV_IOMCU_BOARD;
    if (!p->identity_ok && p->arm != NODE_SRV_SAFE) {
        disarm(p);
        owe_off(p);
    }
    return p->identity_ok;
}

void node_srv_pol_status(node_srv_pol_t *p, uint32_t now_ms, uint16_t faults)
{
    p->faults = faults;
    if (p->arm == NODE_SRV_ARM_WAIT
        && mstime_elapsed(now_ms, p->arm_ms, HEARTBEAT_SETTLE_MS)
        && !mstime_elapsed(now_ms, p->arm_ms, NODE_SRV_ARM_WAIT_MS)
        && (faults & (uint16_t)LINK_FAULT_HEARTBEAT) == 0u) {
        p->arm = NODE_SRV_ARM_WRITE;
    }
}

void node_srv_pol_off_done(node_srv_pol_t *p, uint8_t gen)
{
    if (gen == p->off_gen) {
        p->off_owed = false;
    }
}

void node_srv_pol_ctl_done(node_srv_pol_t *p)
{
    p->ctl_now = false;
}

void node_srv_pol_arm_done(node_srv_pol_t *p, uint32_t now_ms, bool taken)
{
    (void)now_ms;
    if (p->arm != NODE_SRV_ARM_WRITE) {
        /* A STOP, a loss or a SAFE came while CLEAR and the arming frame
         * were on their way; the all-off writes or CONTROL's ARM 0 follow. */
        return;
    }
    if (taken) {
        p->arm = NODE_SRV_ARMED;
        p->events |= NODE_SRV_EV_ARMED;
    } else {
        disarm(p);
        p->events |= NODE_SRV_EV_ARM_FAILED;
    }
}

void node_srv_pol_refused(node_srv_pol_t *p)
{
    if (p->arm == NODE_SRV_ARMED) {
        disarm(p);
        owe_off(p);
        p->events |= NODE_SRV_EV_ARM_FAILED;
    }
}

void node_srv_pol_step(node_srv_pol_t *p, uint32_t now_ms, node_srv_out_t *out)
{
    memset(out, 0, sizeof *out);
    if (p->panel == NODE_SRV_LINKED && p->ka_seen
        && mstime_elapsed(now_ms, p->ka_ms, NODE_SRV_KA_MS)) {
        latch(p, NODE_SRV_PANEL_LOST, NODE_SRV_EV_KA_LOST);
    }
    if (!p->iomcu_lost && mstime_elapsed(now_ms, p->answer_ms, NODE_SRV_IOMCU_MS)) {
        p->iomcu_lost = true;
        p->identity_ok = false;
        p->events |= NODE_SRV_EV_IOMCU_LOST;
        disarm(p);
        owe_off(p);
    }
    if (!p->run_done && mstime_elapsed(now_ms, p->answer_ms, NODE_SRV_RUN_MS)) {
        p->run_done = true;
        out->run_pulse = true;
    }
    if (p->arm == NODE_SRV_ARM_WAIT
        && mstime_elapsed(now_ms, p->arm_ms, NODE_SRV_ARM_WAIT_MS)) {
        disarm(p);
        p->events |= NODE_SRV_EV_ARM_FAILED;
    }
    out->beat = p->panel == NODE_SRV_LINKED && p->ble && p->ka_seen
                && !p->ka_stop && p->answered && !p->iomcu_lost
                && p->identity_ok;
    if (!out->beat && p->arm != NODE_SRV_SAFE) {
        /* Every path that ends the beat disarms on its own; this holds the
         * rule for one that does not. */
        disarm(p);
        owe_off(p);
    }
    out->off = p->off_owed;
    out->off_gen = p->off_gen;
    out->arm_write = p->arm == NODE_SRV_ARM_WRITE;
    out->ctl_now = p->ctl_now;
    if (p->arm == NODE_SRV_ARMED) {
        out->arm = p->roles;
        if (p->thr_seen && !mstime_elapsed(now_ms, p->thr_ms, NODE_SRV_THR_MS)) {
            out->throttle = p->thr;
        }
    }
    out->status_fast = p->faults != 0u || p->arm == NODE_SRV_ARM_WAIT;
}

uint8_t node_srv_pol_events(node_srv_pol_t *p)
{
    const uint8_t ev = p->events;
    p->events = 0u;
    return ev;
}
