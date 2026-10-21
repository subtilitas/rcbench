/**
 * @file
 * @brief The panel's half of SENSE_WIN_A and SENSE_WIN_B (0x37, 0x38, protocol
 *        4.25): every INA3221 channel's last complete 50 ms window, for the
 *        log's columns per watched target.
 *
 * SENSE_WIN_A carries channels 0 to 5 (the parts at 0x40 and 0x41),
 * SENSE_WIN_B channels 6 to 11 (0x42 and 0x43).  A page is read whole, 32
 * registers in one exchange, every SENSE_WIN_LINK_READ_MS while a part it
 * carries is enabled in the map in force, and not at all otherwise.
 * An IOmcu older than 4.25 has neither page: nothing is sent to it.
 *
 * Each read keeps the page's registers as read, and the read before it
 * while that held another window.  sense_win_link_window() hands out the
 * channels of the pages whose last or previous read holds window number
 * @p window, so a row of the log gets the figures of its own window or
 * none: a window that closed and was followed by the next between two
 * reads gives its row nothing.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bench_state.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A page is read this often while a part it carries is enabled: the
 *        IOmcu closes a window every 50 ms, and a gate under the poll's 50 ms
 *        lets every poll read, whatever its offset in the pass.
 */
#define SENSE_WIN_LINK_READ_MS 40u

#define SENSE_WIN_LINK_ACK       0
#define SENSE_WIN_LINK_NO_ANSWER (-1)

/** @brief One read the caller makes: registers 0 to n - 1 of @p page. */
typedef struct {
    uint8_t page;
    uint8_t n;
} sense_win_link_op_t;

/**
 * @brief The panel's state of the SENSE_WIN link pages.
 */
typedef struct {
    bool     up;              /**< an IOmcu answers                         */
    bool     page;            /**< it has the pages (4.25)                  */
    uint8_t  want;            /**< bit 0 SENSE_WIN_A, bit 1 SENSE_WIN_B     */
    uint8_t  pending;         /**< the page being read, 0 or 1; 0xFF none   */
    uint32_t read_ms[2];      /**< when each page was last asked for        */
    bool     asked[2];
    bool     have[2];         /**< regs holds a read under want             */
    bool     have_prev[2];    /**< prev holds the read of the window before */
    uint16_t regs[2][LINK_SX_COUNT];
    uint16_t prev[2][LINK_SX_COUNT];
} sense_win_link_t;

/**
 * @brief Sets up the SENSE_WIN link state.
 *
 * @param s The link state to set up.
 */
void sense_win_link_init(sense_win_link_t *s);

/**
 * @brief The link went: nothing read is the IOmcu's now.
 *
 * @param s The link state.
 */
void sense_win_link_lost(sense_win_link_t *s);

/**
 * @brief An IOmcu answered with protocol minor @p minor; below
 *        LINK_MINOR_SENSE_MAP nothing is sent to it.
 *
 * @param s     The link state.
 * @param minor The IOmcu's protocol minor version.
 */
void sense_win_link_came_up(sense_win_link_t *s, uint16_t minor);

/**
 * @brief The parts enabled in the map the IOmcu holds, bit a for 0x40 + a: the
 *        pages to read.
 *
 * A page no longer wanted forgets its last read.
 *
 * @param s     The link state.
 * @param parts The parts to read: bit a for part a.
 */
void sense_win_link_want(sense_win_link_t *s, uint8_t parts);

/**
 * @brief The next read owed at @p now_ms into @p op; false for none.
 *
 * With both
 *  pages due, a second call gives the other.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param op     Receives the read owed.
 * @return true when a read is owed.
 */
bool sense_win_link_next(sense_win_link_t *s, uint32_t now_ms,
                         sense_win_link_op_t *op);

/**
 * @brief How it went: SENSE_WIN_LINK_ACK with the registers read, a NACK's
 *        reason, or SENSE_WIN_LINK_NO_ANSWER.
 *
 * BAD_PAGE stops the reads until
 *  the next link-up.
 *
 * @param s      The link state.
 * @param result The result: an acknowledgement, a NACK's reason or
 *               SENSE_WIN_LINK_NO_ANSWER.
 * @param regs   The registers read, or NULL for a write.
 */
void sense_win_link_done(sense_win_link_t *s, int result,
                         const uint16_t *regs);

/**
 * @brief The channels whose last or previous read holds window number @p window
 *        into @p out: out->ok bit c for channel c with readings, its current
 *        and voltage flags, and the figures; every other channel 0.
 *
 * Returns whether any channel has
 * readings.  @p out is always written.
 *
 * @param s      The link state.
 * @param window The window number to find.
 * @param out    Receives the channels.
 * @return true when any channel has readings.
 */
bool sense_win_link_window(const sense_win_link_t *s, uint16_t window,
                           bench_chw_t *out);

/**
 * @brief FAST's channel @p fast (0 to 11) of @p out from the window ring's
 *        window that @p b carries in its first channel
 *        (sense_link_win_bench()): the ring holds every window of FAST, the
 *        pages only the last.
 *
 * Nothing for a
 * @p fast of LINK_SM_FAST_NONE or a @p b with no new window.
 *
 * @param out  Receives the channels.
 * @param fast FAST's channel, 0 to 11, or LINK_SM_FAST_NONE.
 * @param b    The state that carries the window.
 */
void sense_win_link_ring(bench_chw_t *out, unsigned fast,
                         const bench_state_t *b);

#ifdef __cplusplus
}
#endif
