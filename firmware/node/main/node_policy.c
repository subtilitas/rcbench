/**
 * @file node_policy.c
 * @brief The node's battery and sleep decisions (node_policy.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_policy.h"

#include "rcnode.h"

void node_policy_init(node_policy_t *p, uint32_t now_ms)
{
    p->busy_ms   = now_ms;
    p->low_reads = 0u;
}

/**
 * @brief Whether a reading is a cell under the switch-off threshold.
 *
 * @param bat_mv The cell, mV.
 * @return true for NODE_BAT_NONE_MV up to RCNODE_BAT_OFF_MV, the latter
 *         excluded.
 */
static bool cell_empty(uint16_t bat_mv)
{
    return bat_mv >= NODE_BAT_NONE_MV && bat_mv < RCNODE_BAT_OFF_MV;
}

node_act_t node_policy_step(node_policy_t *p, const node_inputs_t *in)
{
    if (cell_empty(in->bat_mv)) {
        if (p->low_reads < NODE_OFF_READS) {
            ++p->low_reads;
        }
    } else {
        p->low_reads = 0u;
    }
    if (p->low_reads >= NODE_OFF_READS) {
        return NODE_OFF;
    }
    if (in->link || in->pack || in->stay) {
        p->busy_ms = in->now_ms;
        return NODE_RUN;
    }
    return (uint32_t)(in->now_ms - p->busy_ms) >= NODE_IDLE_MS ? NODE_SLEEP
                                                               : NODE_RUN;
}

node_act_t node_policy_wake(bool answered, int32_t vbus_uv, uint16_t bat_mv)
{
    if (cell_empty(bat_mv)) {
        return NODE_OFF;
    }
    if (answered && vbus_uv >= RCNODE_PACK_UV) {
        return NODE_RUN;
    }
    return NODE_SLEEP;
}

uint16_t node_bat_mv(uint32_t pin_mv)
{
    const uint64_t mv = (uint64_t)pin_mv * (NODE_DIV_TOP_KOHM + NODE_DIV_BOT_KOHM)
                        / NODE_DIV_BOT_KOHM;
    return mv > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)mv;
}

uint16_t node_bat_flags(uint16_t bat_mv)
{
    if (bat_mv < NODE_BAT_NONE_MV) {
        return RCNODE_ST_BAT_NONE;
    }
    if (bat_mv < RCNODE_BAT_LOW_MV) {
        return RCNODE_ST_BAT_LOW;
    }
    return 0u;
}
