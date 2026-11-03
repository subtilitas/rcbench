/**
 * @file panel_shot.h
 * @brief The screenshot on the panel: its memory, its card task and the
 *        facts of its gate (ui_shot.h, shot_gate.h).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief What the frame loop read of the bench for this frame's gate. */
typedef struct {
    bool     armed;        /**< the snapshot's ARMED                       */
    bool     arm_wanted;   /**< the snapshot's arm waiting                 */
    bool     supply_now;   /**< the snapshot's supply output               */
    bool     supply_seen;  /**< the newest sample's output                 */
    bool     psup_now;     /**< the PROG 5V box's output                   */
    bool     link_up;      /**< the snapshot's link                        */
    bool     simulated;    /**< the numbers are the panel's model          */
    bool     cu_active;    /**< the IOmcu's update runs                    */
    uint16_t faults;       /**< the snapshot's FAULT bits                  */
} panel_shot_in_t;

/** @brief Bind the screenshot to the panel's memory and card. */
void panel_shot_init(void);

/**
 * @brief The gate's facts for this frame, to the router.
 * @param in  what the frame loop read
 */
void panel_shot_frame(const panel_shot_in_t *in);
