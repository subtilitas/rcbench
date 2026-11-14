/**
 * @file
 * @brief The power node's readings on the panel: the rcnode STREAM packets
 *        of protocols/rcnode turned into MOTOR & ESC's voltage, current,
 *        power, peaks and totals, and the rule a run keeps for the node's
 *        dropouts.  Pure C: the power node's controller (pnode_ctl.h) feeds
 *        it in the `blec` task, and the control task writes a published
 *        copy into the bench's state (pnode_reading_bench()).
 *
 * The rules it holds (docs/Nodes.md, "The panel"):
 *
 * - The readings: a window (RCNODE_T_PWR_WIN) with the INA228 online and a
 *   sample is a reading.  The bench takes the mean of the windows that came
 *   since its last take, the last window's figures when none came, and
 *   nothing once the last reading is older than PNODE_STALE_MS (300ms).  A
 *   window whose every current sample sat at an end of the range reads
 *   that end, the INA228's full scale at INFO's ADCRANGE and shunt, marked
 *   clipped; a window with some clipped samples reads the mean of the rest,
 *   marked clipped.
 * - The totals: differences of the 40-bit accumulators (RCNODE_T_PWR_ACC)
 *   with both CHARGE_OK and ENERGY_OK, summed since the run's start.  An
 *   epoch change or a fall in ENERGY (rcnode_acc_restarted()) adds the new
 *   counts from 0.  The run starts at the last such packet before the
 *   switch to ARMED, up to PNODE_ACC_BASE_MS (500ms, RCNODE_ACC_MS) before
 *   it; without one the run has no node totals, and the panel counts from
 *   the readings.
 *   The register steps become µC and mJ with ina228_charge_uc() and
 *   ina228_energy_mj() on the calibration INFO names, which must be the
 *   one ina228_calibrate() gives for that shunt at that range's full
 *   scale.
 * - The peaks since the switch to ARMED: the windows' highest current, lowest
 *   voltage and highest mean power, and the node's own RCNODE_T_PWR_RUN
 *   extremes under the run's RUN tag, which hold the peaks of windows the
 *   radio lost.  The first RUN packet after a link comes up within a run
 *   that carries another tag (the node started again) asks for the RUN
 *   write again (tag_wrong).
 * - The switch to ARMED of a run that reads BENCH (MOTOR & ESC, ESC STICK)
 *   with INA228 SOURCE NODE is refused unless POWER NODE is ON and a
 *   reading came within PNODE_GAP_OPEN_MS (pnode_arm_refused()).
 * - A dropout while the run counts on the node (ARMED with INA228 SOURCE
 *   NODE, a run that reads BENCH; a SERVO or OUTPUTS run reads no INA228
 *   and counts none): no reading for more than PNODE_GAP_OPEN_MS (100ms)
 *   opens one, dated from the last reading, and the next reading closes
 *   it.  servo_gap.h decides:
 *   a dropout of up to SERVO_GAP_MAX_MS (300ms) rides, once per
 *   SERVO_GAP_EVERY_MS (10s); a longer one, or a second one that starts
 *   sooner, counts in errors once, which the panel takes as an error: SAFE
 *   and every output off.  A run that starts with no reading in the last
 *   PNODE_GAP_OPEN_MS opens a dropout at its start.
 *
 * Times are unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bench_state.h"
#include "ina228.h"
#include "rcnode.h"
#include "servo_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Times of the rules. @{ */
#define PNODE_STALE_MS     SERVO_GAP_MAX_MS /**< a reading older is none */
#define PNODE_GAP_OPEN_MS  100u  /**< no reading for longer opens a dropout */
#define PNODE_ACC_STALE_MS 1500u /**< totals older lose LINK_BN_TOTALS_OK:
                                      three RCNODE_ACC_MS periods */
#define PNODE_ACC_BASE_MS  RCNODE_ACC_MS /**< the oldest accumulator packet
                                      a run's totals start from */
/** @} */

/** @brief What the control task takes of the meter, published by the
 *         `blec` task.  The sums only grow, so a take is a difference of
 *         two copies. */
typedef struct {
    uint32_t win_n;     /**< reading windows taken since start-up */
    uint32_t v_n;       /**< ...with a voltage */
    int64_t  v_sum;     /**< their mean voltages summed, 2mV steps */
    uint32_t i_n;       /**< ...with a current, clipped ends included */
    int64_t  i_sum;     /**< their currents summed, µA */
    uint32_t clip_n;    /**< ...with a clipped sample */
    bool     have_win;  /**< a reading window came */
    uint32_t win_ms;    /**< the last one's arrival */
    bool     last_v;    /**< it carries a voltage */
    uint16_t last_v_steps; /**< its mean voltage, 2mV steps */
    bool     last_i;    /**< it carries a current */
    int32_t  last_i_ua; /**< its current, µA */
    bool     last_clip; /**< it had a clipped sample */
    bool     run;       /**< a run counts: peaks and totals are its own */
    bool     i_max_ok;  /**< i_max_ua is a reading */
    int32_t  i_max_ua;  /**< the run's highest current, µA */
    bool     v_min_ok;  /**< v_min_steps is a reading */
    uint16_t v_min_steps; /**< the run's lowest voltage, 2mV steps */
    bool     p_max_ok;  /**< p_max_mw is a reading */
    int64_t  p_max_mw;  /**< the run's highest window power, mW */
    bool     run_clip;  /**< a clip in the run */
    bool     totals_ok; /**< charge_uc and energy_mj are the run's */
    uint32_t acc_ms;    /**< the arrival of the last packet they count */
    int64_t  charge_uc; /**< the run's charge, µC */
    uint64_t energy_mj; /**< the run's energy, mJ */
} pnode_reading_t;

/** @brief The meter.  Its members are private but for the tests. */
typedef struct {
    bool             cal_ok;   /**< INFO named a calibration taken */
    ina228_cal_t     cal;      /**< that calibration */
    int32_t          fs_ua;    /**< its full scale, µA */
    bool             have_seq; /**< seq holds the last WIN/ACC/RUN's */
    uint8_t          seq;      /**< STREAM's seq over WIN, ACC and RUN */
    uint32_t         lost;     /**< WIN, ACC and RUN packets lost, by seq */
    uint32_t         bad;      /**< packets refused by rcnode_decode() */
    uint32_t         windows;  /**< windows taken, readings or not */
    bool             have_any; /**< a window came, a reading or not */
    uint32_t         any_ms;   /**< the last one's arrival */
    pnode_reading_t  r;        /**< what the control task takes */
    bool             have_acc; /**< acc holds a packet with both _OK */
    rcnode_pwr_acc_t acc;      /**< the last such packet */
    int64_t          q_steps;  /**< CHARGE steps since the run's start */
    int64_t          e_steps;  /**< ENERGY steps since the run's start */
    uint32_t         restarts; /**< accumulator restarts taken */
    bool             have_status; /**< status holds a STATUS packet */
    rcnode_status_t  status;   /**< the last STATUS */
    uint8_t          tag;      /**< the run's RUN tag, 1 to 255; 0 none */
    bool             tag_check; /**< the next RUN packet is the first since
                                     the link came up within a run */
    bool             tag_wrong; /**< it carried another tag: the RUN write
                                     is owed again */
    bool             live;     /**< ARMED at the last step */
    bool             rule;     /**< the run counts on the node */
    uint32_t         run_ms;   /**< the run's start */
    bool             ended;    /**< the run's error is counted */
    servo_gap_t      gap;      /**< the run's dropouts */
    uint32_t         errors;   /**< dropouts that end a run, counted */
} pnode_meter_t;

/** @brief Clear: no calibration, no reading.  @param m NULL does nothing. */
void pnode_meter_init(pnode_meter_t *m);

/**
 * @brief The link came up: the seq count starts again; the calibration
 *        waits for INFO.
 * @param m The meter; NULL does nothing.
 */
void pnode_meter_link(pnode_meter_t *m);

/**
 * @brief INFO's INA228 set-up: taken when ina228_calibrate() gives the same
 *        ADCRANGE for the shunt at that range's full scale.
 * @param m The meter.
 * @param i The INFO; its role must be RCNODE_ROLE_POWER.
 * @return Whether it was taken; the meter takes no packet without it.
 */
bool pnode_meter_info(pnode_meter_t *m, const rcnode_info_t *i);

/**
 * @brief One STREAM notification: decoded and taken by type.
 * @param m      The meter.
 * @param b      Its bytes.
 * @param n      Their count.
 * @param now_ms Its arrival.
 * @return The packet's type, 0 for one refused or without a calibration.
 */
uint8_t pnode_meter_packet(pnode_meter_t *m, const uint8_t *b, size_t n,
                           uint32_t now_ms);

/**
 * @brief Advance to @p now_ms: a switch to ARMED starts a run (peaks,
 *        totals, the dropout rule and the next RUN tag); while @p live,
 *        @p src_node and @p bench_run, the dropout rule.
 * @param m         The meter.
 * @param now_ms    The clock.
 * @param live      ARMED, or a switch to ARMED waits.
 * @param src_node  INA228 SOURCE is NODE.
 * @param bench_run The run reads BENCH's readings: MOTOR & ESC or ESC
 *                  STICK armed the throttle.  A SERVO or OUTPUTS run reads
 *                  no INA228 and does not count on the node.
 * @return true at the step a run starts: the RUN write with m->tag is
 *         owed.
 */
bool pnode_meter_step(pnode_meter_t *m, uint32_t now_ms, bool live,
                      bool src_node, bool bench_run);

/**
 * @brief Whether a switch to ARMED of a run that reads BENCH is refused for
 *        the node: INA228 SOURCE NODE, and POWER NODE OFF or no reading
 *        within PNODE_GAP_OPEN_MS.
 * @param src_node INA228 SOURCE is NODE.
 * @param node_on  POWER NODE is ON.
 * @param r        The meter's copy; NULL has no reading.
 * @param now_ms   The clock.
 * @return true when the arm is refused.
 */
bool pnode_arm_refused(bool src_node, bool node_on, const pnode_reading_t *r,
                       uint32_t now_ms);

/**
 * @brief The copy the control task takes.
 * @param m   The meter; NULL gives a zero copy.
 * @param out Receives it.
 */
void pnode_meter_reading(const pnode_meter_t *m, pnode_reading_t *out);

/**
 * @brief The node's figures into @p b, as an IOmcu's INA228 would put
 *        them: LINK_BN_SENSED, voltage, current and power with their _OK
 *        bits over the windows since @p was, the run's peaks, the totals
 *        with LINK_BN_TOTALS_OK, the clip, and from_node.  BENCH's own
 *        voltage and current, which are the ESC's while no INA228 is the
 *        source, move to esc_voltage and esc_current first.
 * @param was    The copy of the last take; NULL is none.
 * @param now    The copy now.
 * @param now_ms The clock.
 * @param b      The state; NULL does nothing.
 * @return Whether a reading younger than PNODE_STALE_MS was there.
 */
bool pnode_reading_bench(const pnode_reading_t *was,
                         const pnode_reading_t *now, uint32_t now_ms,
                         bench_state_t *b);

#ifdef __cplusplus
}
#endif
