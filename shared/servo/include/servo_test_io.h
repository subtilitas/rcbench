/**
 * @file
 * @brief What the automatic servo test reads and what it hands out: the
 *        supply's readings, the INA3221's windows and the encoder's readings it
 *        is fed, what it wants done, and the items of its outbox.  A caller
 *        that feeds a run or carries its files to the card, and steps none,
 *        includes this alone, as servo_screen.h does; the engine is
 *        servo_test.h.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------- what it reads */

/** @brief One reading of the supply, as SUPPLY has it (supply_state_t). */
typedef struct {
    float    v, i;          /**< at the output, V and A                 */
    float    set_v;         /**< the voltage set point, read back       */
    float    set_i;         /**< the current limit, read back           */
    uint8_t  mode;          /**< 0 off, 1 CV, 2 CC (supply_mode_t)      */
    bool     output;        /**< switched on                            */
    bool     online;        /**< the supply answers                     */
    bool     ok;            /**< voltage and current both arrived       */
    uint8_t  trip;          /**< nonzero: tripped (supply_trip_kind_t)  */
    uint16_t samples;       /**< readings the supply took, mod 65536    */
    uint32_t taken_ms;      /**< when the panel had it, on the run's clock */
} servo_test_reading_t;

/** @brief One 50ms window of INA3221 CH1, as the window ring carries it
 *         (sense_link_win_t).
 *
 * A clipped sample counts in the three currents at the end of the range it
 * read. */
typedef struct {
    uint16_t number;        /**< the window's, modulo 65536             */
    bool     current;       /**< it holds current samples               */
    bool     voltage;       /**< it holds bus voltage samples           */
    uint8_t  clipped;       /**< samples at an end of the range         */
    int16_t  mean_ma;       /**< the mean current, mA, signed           */
    int16_t  max_ma;        /**< the highest current, mA, signed        */
    int16_t  min_ma;        /**< the lowest current, mA, signed         */
    uint16_t mean_mv;       /**< the mean bus voltage at the load side
                                 of the shunt, mV                       */
    uint16_t min_mv;        /**< the lowest of it, mV                   */
    /** @brief The latest it can have closed, on the run's clock
     *         (sense_link_win_t.taken_ms): windows that arrive together carry
     *         times 50ms apart.
     *
     * The window began SERVO_TEST_WIN_MS before. */
    uint32_t taken_ms;
} servo_test_win_t;

/** @brief Readings of the encoder kept for matching with the supply's rows.
 *
 * The panel has one about every 40ms, so 16 span 640ms: more than
 * SERVO_TEST_ENC_STALE_MS. */
#define SERVO_TEST_ENC_HIST          16u

/** @brief A kept reading. */
typedef struct {
    uint32_t ms;            /**< when the panel had it                   */
    uint16_t raw;           /**< RAW ANGLE, 0 to 4095                    */
} servo_test_enc_sample_t;

/** @brief One reading of the output encoder, as the SENSE page has it. */
typedef struct {
    bool     valid;         /**< the part answers, detects its magnet and
                                 has an angle                           */
    uint16_t raw;           /**< RAW ANGLE, 0 to 4095                   */
    uint16_t still_ms;      /**< the angle within the tolerance for this
                                 long at the reading                    */
    uint32_t taken_ms;      /**< when the panel had it, on the run's clock */
    bool     gap;           /**< readings before this one were lost: the
                                 angle between is not known, as after a
                                 reading that is not valid             */
    bool     no_magnet;     /**< not valid because the part answers and
                                 detects no magnet (STATUS MD clear)   */
    bool weak,                              /**< valid, with the field too weak (ML): the angle's noise is not specified */
         strong;                            /**< valid, with the field too strong (MH): likewise */
} servo_test_enc_t;

/**
 * @brief @p raw from @p centre on the circle, degrees, -180 to just under 180:
 *        360 / 4096 a count.  The difference of any two counts the shortest way
 *        round.
 *
 * @param raw    Encoder count, 0 to 4095.
 * @param centre Centre count, 0 to 4095.
 * @return angle, degrees, -180 up to just under 180.
 */
float servo_test_enc_deg(uint16_t raw, uint16_t centre);

/**
 * @brief @p deg on the circle: -180 to just under 180.
 *
 * @param deg Angle, degrees.
 * @return angle, degrees, -180 up to just under 180.
 */
float servo_test_enc_wrap(float deg);

/** @brief What the run wants done, from servo_test_step(). */
typedef struct {
    bool     set;           /**< the set points to set_v and set_i      */
    float set_v,                            /**< the voltage to set, V                  */
          set_i;                            /**< the current limit to set, A            */
    bool     on;            /**< the output on, after the set points    */
    bool     off;           /**< the output off                         */
    bool     command;       /**< the servo to cmd_us, at once (no slew) */
    uint16_t cmd_us;                    /**< the pulse, µs */
    bool     release;       /**< the servo let go, to its centre        */
    bool     disarm;        /**< the bench to SAFE: the run ended on an
                                 error (servo_test_abort_error())       */
} servo_test_do_t;

/** @brief What the caller knows each step. */
typedef struct {
    bool  armed;
    float v_max;            /**< the voltage cap in force: SUPPLY's caps,
                                 VOLTAGE MAX and the module's input clamp */
} servo_test_in_t;

/* --------------------------------------------------- what it hands out */

/** @brief The longest line a log row or a report line takes, terminator
 *         included: the CSV's header with the encoder's columns is 219
 *         characters. */
#define SERVO_TEST_LINE_MAX 224u

typedef enum {
    SERVO_TEST_OUT_NONE = 0,
    SERVO_TEST_OUT_OPEN,    /**< a run begins: its files are opened     */
    SERVO_TEST_OUT_CSV,     /**< one line of the CSV                    */
    SERVO_TEST_OUT_TXT,     /**< one line of the report                 */
    SERVO_TEST_OUT_END,     /**< the run's files are complete           */
    SERVO_TEST_OUT_OPEN_TXT,/**< a report alone begins: a TXT, no CSV    */
} servo_test_out_t;

#ifdef __cplusplus
}
#endif
