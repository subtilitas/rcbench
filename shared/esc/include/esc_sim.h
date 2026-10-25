/**
 * @file
 * @brief Simulated ESC in its throttle-stick menu, with its supply current.
 *
 * A simulated ESC (electronic speed controller) in its throttle-stick menu,
 * as the supply current shows it: an idle current, and a pulse on top for
 * every beep.
 *
 * It follows a profile the way the stick programmer reads one: power on at
 * the entry position, or at a position a value is programmed from (its
 * entry_throttle), enters the menu; the groups loop, a move to the select
 * position after a group selects it, and in a two-stage menu a move to the
 * value_select position after a value group stores the value.  Where the
 * profile names a store move, or the value moves after its selection
 * (after_select), the selection waits for each in order and is lost if the
 * power goes first.  An item keyed exit leaves the menu when it is
 * selected, and one keyed reset clears what was stored.  A value stored
 * from a power-up at another position than its own is counted as
 * misplaced: the ESC would teach that position.  What it stores is
 * kept across power cycles, as an ESC keeps it.
 *
 * The timing is made up, as the stick programmer's defaults are: no ESC has
 * been recorded.  The host suite runs the engine against it end to end, and
 * the panel runs it as the ESC on its modelled supply.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ESC_SIM_H
#define RCBENCH_ESC_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esc_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timing and fault settings of the simulated ESC. */
typedef struct {
    uint32_t beep_ms;     /**< a short beep                              */
    uint32_t gap_ms;      /**< silence between two beeps of a group      */
    uint32_t long_ms;     /**< a long beep, and the stored tone          */
    uint32_t pause_ms;    /**< silence between two groups                */
    uint32_t entry_ms;    /**< power on to the first group; 0: the
                               profile's hold, or 3000ms without one     */
    uint32_t window_ms;   /**< how long after a group a move selects it;
                               0: the profile's window, or until the
                               next group begins                         */
    uint32_t idle_ma;     /**< the current the ESC draws idle            */
    uint32_t beep_ma;     /**< on top of idle_ma while a beep sounds     */
    uint32_t noise_ma;    /**< uniform, either way                       */
    uint32_t seed;        /**< the noise generator's seed                */
    uint8_t  extra;       /**< groups the menu sounds past the profile's
                               highest number                            */
    int32_t  drop_group;  /**< this menu group, from 0 since power-on,
                               loses its last beep; -1 none              */
    bool     deaf;        /**< never takes a selection                   */
    bool     mute;        /**< never beeps                               */
    bool     wait_hand;   /**< where the profile has a before_menu step,
                               the menu waits for esc_sim_hand(), answers
                               it with three beeps and starts at once    */
} esc_sim_cfg_t;

/** @brief Mode of the simulated ESC's menu. */
typedef enum {
    ESC_SIM_OFF = 0,
    ESC_SIM_IDLE,         /**< powered, not in the menu                  */
    ESC_SIM_ENTRY,
    ESC_SIM_WAIT_LISTEN,  /**< the menu waits for the stick at its rest  */
    ESC_SIM_ITEMS,
    ESC_SIM_VALUES,
    ESC_SIM_PENDING,      /**< selected, waiting for the store move      */
    ESC_SIM_DONE,         /**< stored; one change per power-up           */
} esc_sim_mode_t;

/** @brief Most group numbers in the loop being sounded. */
#define ESC_SIM_LOOP_MAX  64u
/** @brief Most pulses in one group's beep sequence. */
#define ESC_SIM_PULSE_MAX 16u

/** @brief Full state of one simulated ESC. */
typedef struct {
    const esc_profile_t *p;           /**< the profile being followed      */
    esc_sim_cfg_t        c;           /**< timing and fault settings       */
    esc_sim_mode_t       mode;        /**< current menu mode               */
    bool                 powered;     /**< supply on                       */
    uint32_t             on_ms;       /**< when the supply came on         */
    esc_throttle_t       pos;         /**< stick position                  */
    esc_throttle_t       entry;       /**< where this power-up entered     */

    /* What is being sounded. */
    uint8_t              loop[ESC_SIM_LOOP_MAX];  /**< group numbers, in the
                                                       loop                  */
    uint8_t              loop_n;  /**< groups in the loop                    */
    uint8_t              loop_i;  /**< group sounding                        */
    uint8_t              rep;     /**< repeats done                          */
    uint8_t              pulse[ESC_SIM_PULSE_MAX];  /**< 1 long, 0 short   */
    uint8_t              pulse_n; /**< pulses in the group                   */
    uint8_t              pulse_i; /**< index of the pulse sounding           */
    uint8_t              seg;          /**< esc_sim.c's seg_t               */
    uint32_t             seg_end;      /**< when the current segment ends   */
    bool                 menu_group;   /**< the group is a menu entry       */
    uint8_t              sounding;     /**< its number                      */
    uint8_t              ended;        /**< the last menu group to end      */
    uint32_t             ended_ms;     /**< when that group ended           */
    uint8_t              item;         /**< VALUES: the item selected       */
    uint8_t              pend_item;    /**< PENDING: what the move stores   */
    uint8_t              pend_value;   /**< PENDING: the value to store     */
    uint8_t              pend_step;    /**< PENDING: the store moves made   */
    uint32_t             resets;       /**< reset items taken               */
    uint32_t             menu_groups;  /**< menu groups sounded             */
    bool                 dropped;      /**< this beep draws nothing         */
    bool                 tones_done;   /**< the entry's own tones           */
    bool                 hand_done;    /**< the person's action, this
                                            power-up                        */
    bool                 answering;    /**< sounding the answer to it       */

    /* What it keeps, by item number. */
    uint8_t              stored[256];  /**< value stored for each item      */
    esc_throttle_t       stored_from[256];  /**< the power-up's position    */
    uint32_t             stores;       /**< values stored                   */
    uint32_t             misplaced;    /**< stored from a position not the
                                            value's own                     */
    uint32_t             lcg;          /**< state of the noise generator    */
} esc_sim_t;

/**
 * @brief Fill a simulator config with its default settings.
 *
 * @param c  Receives the defaults.
 */
void esc_sim_defaults(esc_sim_cfg_t *c);

/**
 * @brief Start a simulated ESC on a profile.
 *
 * @param s  The simulator to set up.
 * @param p  The profile the simulated ESC follows.
 * @param c  The timing and fault settings.
 */
void esc_sim_init(esc_sim_t *s, const esc_profile_t *p,
                  const esc_sim_cfg_t *c);

/**
 * @brief Advance the simulated ESC to a time and return its supply current.
 *
 * Advance to @p now_ms with the supply @p powered and the signal at
 * @p throttle_pct, negative for no signal.  Returns the current it draws,
 * mA.  Call it often enough for its pulses: in the host suite every few
 * milliseconds, on the panel at the supply's 50ms.
 *
 * @param s            The simulator.
 * @param now_ms       Time of this step, ms.
 * @param powered      Whether the supply is on.
 * @param throttle_pct Throttle signal, percent; negative for no signal.
 * @return The current drawn, mA.
 */
int32_t esc_sim_step(esc_sim_t *s, uint32_t now_ms, bool powered,
                     float throttle_pct);

/**
 * @brief Record the person's action at the ESC.
 *
 * The person's action at the ESC -- the jumper pulled, the button
 * pressed -- at @p now_ms.  With wait_hand, a menu waiting for it starts;
 * one done before the entry ends lets the menu start when it does.
 *
 * @param s       The simulator.
 * @param now_ms  Time of the action, ms.
 */
void esc_sim_hand(esc_sim_t *s, uint32_t now_ms);

/**
 * @brief List the moves that store a selected value.
 *
 * The moves that store value @p value of the item numbered @p item once it
 * is selected, into @p out: the profile's store move, then the value's own
 * after_select, of the first item and value of those numbers -- items
 * that apply to different models may share both.  At most @p cap are
 * written; returns how many.
 *
 * @param p      The profile.
 * @param item   Item number.
 * @param value  Value number.
 * @param out    Receives the moves.
 * @param cap    Capacity of @p out, in moves.
 * @return The number of moves written.
 */
unsigned esc_sim_store_moves(const esc_profile_t *p, uint8_t item,
                             uint8_t value, esc_throttle_t *out, size_t cap);

/**
 * @brief Value stored for an item.
 *
 * The value stored for the item numbered @p item, 0 when none was.
 *
 * @param s     The simulator.
 * @param item  Item number.
 * @return The stored value, or 0 when none was stored.
 */
uint8_t esc_sim_stored(const esc_sim_t *s, uint8_t item);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_SIM_H */
