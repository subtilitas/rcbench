/**
 * @file
 * @brief Which of two wires carries SCL, read from I2C (Inter-Integrated
 *        Circuit) traffic the injector only listens to.  Pure C: the RP2350
 *        build feeds it the edges core 1 recorded on GP4 and GP5, the host
 *        suite feeds it synthetic ones.
 *
 * Rules, per transaction (a stretch of activity between two idle spans):
 *
 *   - Idle is both lines high for at least 10us.  A repeated START keeps
 *     both high for half a bit: 1.25us at 400kHz, 5us at 100kHz.
 *   - START: out of idle, the line that falls first is SDA.
 *   - STOP: into idle, the line that rises last is SDA.
 *   - A transaction names a line when both rules name it.  A transaction
 *     cut by the start or the end of the window is not counted.
 *   - A level that lasts less than INJ_LINE_GLITCH samples is a glitch and
 *     is dropped before any rule is applied.
 *
 * Verdict: at least INJ_LINE_MIN_TX transactions that name a line, and at
 * least half of all transactions; of those that name a line, at least
 * INJ_LINE_MIN_AGREE_PCT percent name the same one.  A transaction that
 * names no line, such as a poll of an absent address, is left out of the
 * agreement.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INJ_LINE_GLITCH         2u    /**< samples                       */
#define INJ_LINE_IDLE_NS        10000u /**< both high this long is idle  */
#define INJ_LINE_MIN_TX         20u   /**< transactions that name a line */
#define INJ_LINE_MIN_AGREE_PCT  95u   /**< of them, those that agree     */

/* Bit 0 of a state is GP4, bit 1 GP5; 1 is high. */
#define INJ_LINE_GP4 0x1u
#define INJ_LINE_GP5 0x2u

/** One level change: from @c at on, the lines read @c state. */
typedef struct {
    uint32_t at;     /* sample index */
    uint8_t  state;
} inj_edge_t;

typedef enum {
    INJ_WIRING_NONE = 0,  /* no check run since reset                 */
    INJ_WIRING_OK,        /* GP4 is SDA, GP5 is SCL                   */
    INJ_WIRING_SWAPPED,   /* GP5 is SDA, GP4 is SCL                   */
    INJ_WIRING_IDLE,      /* both lines high, no traffic              */
    INJ_WIRING_LOW,       /* a line low for the whole window          */
    INJ_WIRING_NOISE,     /* edges, but no verdict                    */
} inj_wiring_t;

typedef struct {
    inj_wiring_t verdict;
    uint32_t     tx;          /* transactions seen whole              */
    uint32_t     agree_pct;   /* majority share of the named, %       */
    uint32_t     edges4;      /* level changes on GP4, after glitches */
    uint32_t     edges5;
    uint8_t      end_state;   /* the lines at the end of the window   */
    uint32_t     rate_ksps;   /* sample rate, ksample/s (as given)    */
    uint32_t     named;       /* transactions that name a line        */
} inj_line_result_t;

/**
 * Classify @p n edges taken over @p samples samples at @p rate_ksps
 * ksample/s.  @p first is the state at sample 0; @p edges are in order of
 * @c at, each a change from the state before it (a repeat is ignored).
 */
void inj_line_classify(uint8_t first, const inj_edge_t *edges, uint32_t n,
                       uint32_t samples, uint32_t rate_ksps,
                       inj_line_result_t *out);

/** The verdict's word: ok, swapped, idle, low, noise, or none. */
const char *inj_wiring_name(inj_wiring_t v);

#ifdef __cplusplus
}
#endif
