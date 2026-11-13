/**
 * @file
 * @brief The lines of the IOmcu's 3s console report, formatted from one
 *        snapshot of the values they print.
 *
 * Pure: report_line() formats line i of the report into a buffer and
 * touches nothing else.  The firmware takes the snapshot at the pass the
 * report is due (report_steps.h), so one report shows one instant although
 * its lines leave over several passes.
 *
 * The text is what docs/Bringup.md quotes and log readers parse; a line
 * changes only with them.  Each line ends with "\n"; the USB console sends
 * it as "\r\n".
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pass_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The longest line, its "\n" and terminator included, in bytes.
 *        report_line() formats into a buffer of this size, and the
 *        transport takes a line only with this much room.
 */
#define REPORT_LINE_MAX 256u

/**
 * @brief The whole report at its widest, in bytes with "\n" line ends:
 *        every line that can appear together, every field at its widest.
 *        The USB console adds one "\r" a line, 10 bytes at most.
 */
#define REPORT_BYTES_MAX 1350u

/** @brief Channels of the INA3221 map line: three parts of three. */
#define REPORT_MAP_CHANNELS 12u

/** @brief The lines in the order they leave; some apply only at times. */
typedef enum {
    REPORT_L_STORE_OFF = 0, /**< the flash store is off: core 1 did not
                                 register for the lock-out              */
    REPORT_L_SLOT,          /**< the running slot, or no partition table */
    REPORT_L_RESET,         /**< why this start happened                */
    REPORT_L_BOOT,          /**< the boot ROM's record of this boot     */
    REPORT_L_CAN_DOWN,      /**< the controller did not answer on SPI   */
    REPORT_L_CAN_UP,        /**< the CAN counters, with the controller up */
    REPORT_L_CAN_OVERRUN,   /**< receive overruns, when there were any  */
    REPORT_L_SENSE,         /**< SENSE and BENCH, a current monitor on  */
    REPORT_L_SENSE_WINDOW,  /**< SERVO_SENSE's window and capture       */
    REPORT_L_SENSE_MAP,     /**< the INA3221 map and SENSE_LIMIT's OVER */
    REPORT_L_LOOP,          /**< the main loop's pass times             */
    REPORT_LINES            /**< the number of lines                    */
} report_line_t;

/** @brief Every value the report prints, as the pages held it. */
typedef struct {
    bool        store_off;  /**< the flash store is off                 */
    int8_t      slot;       /**< the running slot, 0 for A and 1 for B;
                                 -1 without a partition table           */
    bool        on_trial;   /**< the slot runs on trial                 */
    const char *reset;      /**< reset_cause_full()'s text; NULL reads "" */
    const char *boot;       /**< boot_diag_text()'s text; NULL reads ""  */

    bool     can_up;        /**< the controller answered at boot        */
    uint32_t bitrate;       /**< the CAN bit rate, bit/s                */
    uint32_t requests;      /**< requests served                        */
    uint32_t echoes;        /**< self-test echoes                       */
    uint8_t  tec;           /**< the controller's transmit error counter */
    uint8_t  rec;           /**< the controller's receive error counter  */
    uint8_t  eflg;          /**< the controller's EFLG register         */
    uint32_t tx_drop;       /**< reply frames dropped                   */
    uint8_t  txq_max;       /**< the most frames queued at once         */
    uint32_t overruns;      /**< receive overruns                       */

    bool     sense_on;      /**< a current monitor is enabled           */
    uint16_t sn_flags;      /**< SENSE FLAGS                            */
    uint16_t sn_present;    /**< SENSE PRESENT                          */
    uint16_t i228_id;       /**< the INA228's ID                        */
    uint16_t i3221_id;      /**< the INA3221's ID                       */
    uint16_t sn_errors;     /**< SENSE ERRORS                           */
    uint8_t  i228_resets;   /**< the INA228's reset count               */
    uint8_t  i3221_resets;  /**< the INA3221's reset count              */
    int16_t  temp_dc;       /**< the INA228's temperature, 0.1 degC     */
    uint16_t bn_cv;         /**< BENCH voltage, 10mV                    */
    uint16_t bn_ca;         /**< BENCH current, 10mA                    */
    uint16_t bn_mah;        /**< BENCH charge, mAh                      */
    uint16_t bn_dwh;        /**< BENCH energy, 0.1Wh                    */
    uint16_t bn_flags;      /**< BENCH FLAGS                            */
    uint16_t ss_window;     /**< SERVO_SENSE WINDOW                     */
    int16_t  ch1_ma;        /**< CH1's mean, mA                         */
    uint16_t ch1_mv;        /**< CH1's mean, mV                         */
    uint16_t ch_flags;      /**< CH1's flags                            */
    uint16_t cap_state;     /**< the capture's state                    */
    uint16_t cap_seq;       /**< the capture's sequence number          */
    uint16_t cap_move;      /**< the capture's move time, 0.1ms         */
    uint16_t cap_arrive;    /**< the capture's arrival time, 0.1ms      */
    uint16_t map_fit;       /**< SENSE_MAP ENABLE: the parts fitted     */
    uint16_t map_fast;      /**< SENSE_MAP FAST                         */
    uint16_t map_flags;     /**< SENSE_MAP FLAGS                        */
    int16_t  map_ma[REPORT_MAP_CHANNELS]; /**< each channel's mean, mA  */
    uint16_t over;          /**< SENSE_LIMIT OVER                       */
    uint16_t over_count;    /**< SENSE_LIMIT OVER_COUNT                 */

    pass_figures_t loop;    /**< the main loop's figures (pass_stats.h) */
} report_snap_t;

/**
 * @brief Formats one line of the report.
 *
 * @param s    The snapshot.
 * @param i    The line, a report_line_t.
 * @param buf  Receives the line and a terminator.
 * @param cap  Bytes @p buf holds; REPORT_LINE_MAX holds every line whole.
 * @return The bytes @p buf holds, the terminator not counted: the line,
 *         cut to @p cap - 1 bytes when it is longer.  0 for a line that does
 *         not apply to @p s, for @p i out of range, a NULL @p s or @p buf,
 *         and @p cap 0.
 */
size_t report_line(const report_snap_t *s, unsigned i, char *buf,
                   size_t cap);

#ifdef __cplusplus
}
#endif
