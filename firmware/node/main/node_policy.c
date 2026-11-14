/**
 * @file node_policy.c
 * @brief The node's battery and sleep decisions (node_policy.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_policy.h"

#include "rcnode.h"

void node_policy_init(node_policy_t *p, uint32_t now_ms, uint8_t low_reads)
{
    p->busy_ms   = now_ms;
    p->low_reads = low_reads;
}

/**
 * @brief Whether a reading is a cell under the switch-off threshold.
 *
 * @param bat_mv The cell, mV.
 * @param off_mv The threshold, mV.
 * @return true for NODE_BAT_NONE_MV up to @p off_mv, the latter excluded.
 */
static bool cell_empty(uint16_t bat_mv, uint16_t off_mv)
{
    return bat_mv >= NODE_BAT_NONE_MV && bat_mv < off_mv;
}

/**
 * @brief Count one battery reading.
 *
 * @param p The policy.
 * @param bat_mv The cell, mV, or NODE_BAT_ERR.
 * @param off_mv The threshold, mV.
 * @return true at NODE_OFF_READS low readings in a row.
 */
static bool count_low(node_policy_t *p, uint16_t bat_mv, uint16_t off_mv)
{
    if (bat_mv == NODE_BAT_ERR) {
        /* No reading: the count stands. */
    } else if (cell_empty(bat_mv, off_mv)) {
        if (p->low_reads < NODE_OFF_READS) {
            ++p->low_reads;
        }
    } else {
        p->low_reads = 0u;
    }
    return p->low_reads >= NODE_OFF_READS;
}

node_act_t node_policy_step(node_policy_t *p, const node_inputs_t *in)
{
    if (count_low(p, in->bat_mv,
                  in->update ? NODE_UPD_OFF_MV : RCNODE_BAT_OFF_MV)) {
        return NODE_OFF;
    }
    if (in->link || in->pack || in->stay || in->update) {
        p->busy_ms = in->now_ms;
        return NODE_RUN;
    }
    return (uint32_t)(in->now_ms - p->busy_ms) >= NODE_IDLE_MS ? NODE_SLEEP
                                                               : NODE_RUN;
}

node_act_t node_policy_wake(node_policy_t *p, bool answered, int32_t vbus_uv,
                            uint16_t bat_mv)
{
    if (count_low(p, bat_mv, RCNODE_BAT_OFF_MV)) {
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
    return mv >= NODE_BAT_ERR ? (uint16_t)(NODE_BAT_ERR - 1u) : (uint16_t)mv;
}

uint16_t node_bat_flags(uint16_t bat_mv)
{
    if (bat_mv == NODE_BAT_ERR) {
        return RCNODE_ST_BAT_ERR;
    }
    if (bat_mv < NODE_BAT_NONE_MV) {
        return RCNODE_ST_BAT_NONE;
    }
    if (bat_mv < RCNODE_BAT_LOW_MV) {
        return RCNODE_ST_BAT_LOW;
    }
    return 0u;
}
