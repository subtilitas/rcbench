/**
 * @file node_policy.h
 * @brief The node's decisions that need no hardware: the battery from the
 *        divider, the battery words, and when the node sleeps or switches
 *        itself off.  Pure C, under the host suite (test_node_policy).
 *
 * Sleep policy (docs/Nodes.md, "Sleep and wake"): the node sleeps when no
 * link stands, no pack is on the XT60 and the console does not hold it awake
 * for NODE_IDLE_MS.  Asleep, it wakes every NODE_WAKE_S on the RTC (real-time
 * clock) timer, reads the bus voltage once and goes back to sleep unless a
 * pack is present.  A cell under RCNODE_BAT_OFF_MV for NODE_OFF_READS
 * readings in a row switches the node off: no timer, only the RESET key or
 * a power cycle starts it again.  The count runs on across timer wakes (the
 * caller keeps node_policy_t's low_reads in RTC memory), so a single low
 * reading at a wake switches nothing off.  A divider reading under
 * NODE_BAT_NONE_MV is no cell (the board on USB alone) and switches nothing
 * off.  A failed read (NODE_BAT_ERR) neither counts nor clears the count.
 * While a firmware update's transfer or install runs (node_inputs_t's
 * update), the node does not sleep and switches off only for a cell under
 * NODE_UPD_OFF_MV, NODE_OFF_READS readings in a row: the cell's protection
 * stands, the 3.3V switch-off waits for the update's end.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timing of the sleep policy. @{ */
#define NODE_IDLE_MS   120000u  /**< no link and no pack this long: sleep */
#define NODE_WAKE_S        10u  /**< the RTC timer's period while asleep */
/** @} */

/** @brief The battery divider from the CD42's BAT to the ADC
 *  (analogue-to-digital converter) pin, kΩ: BAT to 200kΩ to the pin, the pin
 *  to 200kΩ to GND, 100nF from the pin to GND. @{ */
#define NODE_DIV_TOP_KOHM 200u  /**< BAT to the pin */
#define NODE_DIV_BOT_KOHM 200u  /**< the pin to GND */
/** @} */

/** @brief node_bat_read()'s value for a failed read; a reading is at most
 *  one below it. */
#define NODE_BAT_ERR 0xFFFFu
/** @brief A cell reading under this, mV, is no cell. */
#define NODE_BAT_NONE_MV 1000u
/** @brief Readings in a row under RCNODE_BAT_OFF_MV before the node switches off. */
#define NODE_OFF_READS 3u
/** @brief While an update runs, the switch-off threshold, mV: a cell this
 *  low is empty; the update ends with the node and the image it ran
 *  stays. */
#define NODE_UPD_OFF_MV 3000u

/** @brief What the node does next. */
typedef enum {
    NODE_RUN = 0,  /**< stay awake */
    NODE_SLEEP,    /**< deep sleep, wake on the NODE_WAKE_S timer */
    NODE_OFF,      /**< deep sleep without a wake source */
} node_act_t;

/** @brief What the policy sees once a second. */
typedef struct {
    uint32_t now_ms;  /**< the node's ms clock; wraps are handled */
    bool     link;    /**< a central is connected */
    bool     pack;    /**< a pack is on the XT60 (the power role's bus voltage is 5.0V or more) */
    bool     stay;    /**< the console holds the node awake */
    uint16_t bat_mv;  /**< the cell, mV, from node_bat_mv(), or NODE_BAT_ERR */
    bool     update;  /**< a firmware update's transfer or install runs */
} node_inputs_t;

/** @brief The policy's memory. */
typedef struct {
    uint32_t busy_ms;    /**< the last time a link, a pack or the console held the node */
    uint8_t  low_reads;  /**< readings in a row under RCNODE_BAT_OFF_MV, across timer wakes */
} node_policy_t;

/**
 * @brief A policy that counts the idle time from @p now_ms, with the low
 *        readings counted before.
 *
 * @param p The policy.
 * @param now_ms The node's ms clock.
 * @param low_reads Low readings in a row before this start (from RTC
 *        memory), 0 after a power-on.
 */
void node_policy_init(node_policy_t *p, uint32_t now_ms, uint8_t low_reads);

/**
 * @brief One step, once a second.
 *
 * @param p The policy.
 * @param in What the node sees.
 * @return NODE_OFF after NODE_OFF_READS low readings in a row (under
 *         NODE_UPD_OFF_MV while an update runs), NODE_SLEEP after
 *         NODE_IDLE_MS without a link, a pack, the console's hold or an
 *         update, NODE_RUN otherwise.
 */
node_act_t node_policy_step(node_policy_t *p, const node_inputs_t *in);

/**
 * @brief The decision at a timer wake.  The battery reading counts as in
 *        node_policy_step().
 *
 * @param p The policy, with low_reads as kept through the sleep.
 * @param answered The INA228 answered the bus voltage read.
 * @param vbus_uv The bus voltage it read, µV.
 * @param bat_mv The cell, mV, or NODE_BAT_ERR.
 * @return NODE_OFF at the NODE_OFF_READS-th low reading in a row, NODE_RUN
 *         for a pack present, NODE_SLEEP otherwise.
 */
node_act_t node_policy_wake(node_policy_t *p, bool answered, int32_t vbus_uv,
                            uint16_t bat_mv);

/**
 * @brief The cell's voltage from the ADC pin's.
 *
 * @param pin_mv The pin's voltage, mV, as the ADC's calibration gives it.
 * @return The cell, mV, clamped to NODE_BAT_ERR - 1.
 */
uint16_t node_bat_mv(uint32_t pin_mv);

/**
 * @brief The battery's RCNODE_ST_* flags.
 *
 * @param bat_mv The cell, mV.
 * @return RCNODE_ST_BAT_ERR for NODE_BAT_ERR, RCNODE_ST_BAT_NONE under
 *         NODE_BAT_NONE_MV, RCNODE_ST_BAT_LOW under RCNODE_BAT_LOW_MV, 0
 *         otherwise.
 */
uint16_t node_bat_flags(uint16_t bat_mv);

#ifdef __cplusplus
}
#endif
