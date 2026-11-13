/**
 * @file
 * @brief The IOmcu main loop's pass times: the longest pass, the passes
 *        over PASS_STATS_OVER_US, and the longest run of each timed
 *        section, since the last read.
 *
 * Pure: the caller hands in readings of a 32-bit microsecond clock.  Every
 * figure is a wrap-safe unsigned difference of two readings, so it holds
 * across the clock's wrap at 2^32us (71.6min) for spans under 2^32us.
 *
 * A pass is the time from one pass_stats_pass() to the next, so the pass in
 * progress at a read is counted at the read after it.  The console report
 * reads and clears the figures every 3s (pass_stats_take()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A pass longer than this counts as over, in us: half of the DShot
 *        frame period of 1000us (docs/DShot.md).
 */
#define PASS_STATS_OVER_US 500u

/** @brief The timed sections of a pass. */
typedef enum {
    PASS_SEC_CAN = 0, /**< can_service() and the transmit pump, each call */
    PASS_SEC_REPORT,  /**< the 3s console report's work in one pass       */
    PASS_SEC_COUNT    /**< the number of sections                         */
} pass_sec_t;

/** @brief The figures since the last read. */
typedef struct {
    uint32_t max_us;                   /**< the longest pass, us          */
    uint32_t over;                     /**< passes over PASS_STATS_OVER_US */
    uint32_t sec_max_us[PASS_SEC_COUNT]; /**< each section's longest run, us */
} pass_figures_t;

/** @brief The running state. */
typedef struct {
    bool           started;  /**< a pass has begun                         */
    uint32_t       start_us; /**< when the pass in progress began, us      */
    pass_figures_t fig;      /**< the figures since the last read          */
} pass_stats_t;

/**
 * @brief Clears the state: no pass begun, every figure 0.
 *
 * @param s  The state.
 */
void pass_stats_init(pass_stats_t *s);

/**
 * @brief Begins a pass: ends the one before it, whose length counts.
 *
 * The first call after pass_stats_init() only begins a pass.
 *
 * @param s       The state.
 * @param now_us  The clock at the top of the pass, us.
 */
void pass_stats_pass(pass_stats_t *s, uint32_t now_us);

/**
 * @brief Notes one run of a section.
 *
 * @param s         The state.
 * @param sec       The section; one out of range is ignored.
 * @param start_us  The clock before the section ran, us.
 * @param end_us    The clock after it ran, us.
 */
void pass_stats_section(pass_stats_t *s, pass_sec_t sec, uint32_t start_us,
                        uint32_t end_us);

/**
 * @brief Reads the figures and clears them; the pass in progress goes on.
 *
 * @param s    The state.
 * @param out  Receives the figures since the last read.
 */
void pass_stats_take(pass_stats_t *s, pass_figures_t *out);

#ifdef __cplusplus
}
#endif
