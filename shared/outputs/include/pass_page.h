/**
 * @file
 * @brief The PASS and PASS_STAT pages (0x41, 0x42, protocol 4.41) of the
 *        remote servo node's IOmcu (board 3): each channel's source, the
 *        capture's figures in windows of 50ms, and the counts.
 *
 * The registers are in link_pages_pass.h, the decoder in
 * protocols/passthru.  The rules:
 *
 *   - SOURCE takes OFF, PASS-THROUGH or BENCH (0 to 2; above:
 *     LINK_NACK_BAD_VALUE).  A write that changes a SOURCE while the bank
 *     is ARMED is refused with LINK_NACK_BUSY; one that writes the
 *     SOURCEs in force is taken.  A write is taken whole or not at all.
 *   - OFF and PASS-THROUGH hold the channel's output pin: a SOURCE moving
 *     from BENCH to either, on a pin an output slot, a programming port or
 *     another page holds, is refused with LINK_NACK_BUSY.  BENCH leaves the
 *     pin to the bank.
 *   - PASS-THROUGH is refused with LINK_NACK_BAD_VALUE when the capture
 *     and MIRROR do not run.
 *   - Every start is BENCH on every channel; nothing is kept.
 *   - pass_page_mirror(): the channels whose pad MIRROR drives: PASS-
 *     THROUGH while the bank is ARMED.  Every other held channel is low.
 *   - The figures are published when a window of LINK_PA_WINDOW_MS closes:
 *     WINDOW counts up, FLAGS holds what pass_page_flag() reported during
 *     it.  A window that closes more than one length late starts the next
 *     one at its close.
 *   - PASS_STAT's RESET (1) takes the decoder's counts as the new zero for
 *     MISSED, FRAMES and GLITCHES at once; 0 is taken and does nothing;
 *     above 1 LINK_NACK_BAD_VALUE.  Every other register of both pages is
 *     read only (LINK_NACK_READ_ONLY).
 *
 * Pure logic, host-tested in test_pass_page.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PASS_PAGE_H
#define RCBENCH_PASS_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "pass_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a SOURCE write is judged against. */
typedef struct {
    bool     armed;  /**< the bank is ARMED */
    uint16_t busy;   /**< bit k: output k's pin is held by something else */
} pass_ctx_t;

/** @brief The pages. */
typedef struct {
    uint16_t source[LINK_PA_CHANNELS];   /**< SOURCE in force */
    uint16_t pass[LINK_PA_COUNT];        /**< PASS as published */
    uint16_t stat[LINK_PT_COUNT];        /**< PASS_STAT as published */
    uint32_t base_missed[LINK_PA_CHANNELS]; /**< MISSED's zero */
    uint32_t base_frames[LINK_PA_CHANNELS]; /**< FRAMES' zero */
    uint32_t base_glitches;              /**< GLITCHES' zero */
    uint32_t glitch_seen[LINK_PA_CHANNELS]; /**< counts at the window's start */
    uint32_t win_start_ms;               /**< the open window's start */
    uint16_t window;                     /**< the last window closed */
    uint16_t acc;                        /**< LINK_PA_F_* of the open window */
    bool     capture;                    /**< the capture and MIRROR run */
} pass_page_t;

/**
 * @brief The pages at a start: BENCH everywhere, no figure.
 *
 * @param p        The pages.
 * @param capture  The capture and MIRROR run.
 * @param now_ms   The first window's start.
 */
void pass_page_init(pass_page_t *p, bool capture, uint32_t now_ms);

/**
 * @brief A write to PASS.
 *
 * @param p    The pages.
 * @param off  First register.
 * @param n    Registers.
 * @param in   The values.
 * @param c    The bank's state and the pins held elsewhere.
 * @return     0, or the LINK_NACK_* code.
 */
uint8_t pass_page_write(pass_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const pass_ctx_t *c);

/**
 * @brief A read of PASS.
 *
 * @param p    The pages.
 * @param off  First register.
 * @param n    Registers.
 * @param out  Receives them.
 */
void pass_page_read(const pass_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out);

/**
 * @brief A write to PASS_STAT.
 *
 * @param p    The pages.
 * @param off  First register.
 * @param n    Registers.
 * @param in   The values.
 * @param d    The decoder, for RESET's zero.
 * @return     0, or the LINK_NACK_* code.
 */
uint8_t pass_stat_write(pass_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const pass_dec_t *d);

/**
 * @brief A read of PASS_STAT.
 *
 * @param p    The pages.
 * @param off  First register.
 * @param n    Registers.
 * @param out  Receives them.
 */
void pass_stat_read(const pass_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out);

/**
 * @brief Something that makes the open window's figures not known, or
 *        says why: LINK_PA_F_LAPPED, _SESSION, _STALL.
 *
 * @param p      The pages.
 * @param flags  The bits.
 */
void pass_page_flag(pass_page_t *p, uint16_t flags);

/**
 * @brief Close the open window when its time is up and publish the
 *        decoder's figures.
 *
 * @param p       The pages.
 * @param d       The decoder.
 * @param now_ms  The pass's clock.
 * @return        true when a window closed.
 */
bool pass_page_tick(pass_page_t *p, const pass_dec_t *d, uint32_t now_ms);

/**
 * @brief The channels MIRROR drives: PASS-THROUGH while ARMED.
 *
 * @param p      The pages.
 * @param armed  The bank is ARMED.
 * @return       Bit k for channel k.
 */
uint16_t pass_page_mirror(const pass_page_t *p, bool armed);

/**
 * @brief The channels whose output pin the pages hold: OFF and
 *        PASS-THROUGH.
 *
 * @param p  The pages.
 * @return   Bit k for channel k.
 */
uint16_t pass_page_held(const pass_page_t *p);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PASS_PAGE_H */
