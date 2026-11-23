/**
 * @file link_pages_pass.h
 * @brief The PASS and PASS_STAT pages (0x41, 0x42, protocol 4.41): the
 *        remote servo node's pass-through and its pulse capture (board 3).
 *
 * Included by link_pages.h.  The rules in code are shared/outputs/
 * pass_page.h, the decoder protocols/passthru; docs/Link.md describes the
 * pages.  Only board 3 serves them: an IOmcu of board 1 or 2 refuses them
 * with LINK_NACK_BAD_PAGE.
 *
 * PASS:
 *
 *   SOURCE(k)  channel k's output: LINK_PA_SRC_OFF, _PASS or _BENCH.
 *              Written while SAFE; a change while ARMED is refused with
 *              LINK_NACK_BUSY.  BENCH at every start, not kept.
 *   IN_US(k)   the last pulse on input k, us; 0 for none or lost.
 *   OUT_US(k)  the last pulse on output k as captured, us; 0 for none,
 *              lost, or a DShot output.
 *   LOST       bit k: input k has had no pulse for the lost time.
 *   WINDOW     the number of the last window closed, modulo 65536.
 *   FLAGS      that window: LINK_PA_F_*.
 *   GLITCH     bit k: input k had a high not taken as a pulse in it.
 *
 * PASS_STAT:
 *
 *   PERIOD(k)  input k's frame period, us; 0 when not known.
 *   MISSED(k)  frames missed since the last RESET, at most 65535.
 *   FRAMES(k)  pulses taken since the last RESET, modulo 65536.
 *   RESET      a write of 1 clears MISSED, FRAMES and GLITCHES; reads 0.
 *   GLITCHES   highs not taken on all inputs since the last RESET, at most
 *              65535.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LINK_PAGES_PASS_H
#define LINK_PAGES_PASS_H

/** @brief The channels of the pages: inputs and outputs 1 to 9. */
#define LINK_PA_CHANNELS 9u

/** @brief Registers of the PASS page. */
enum {
    LINK_PA_SOURCE0 = 0,  /**< SOURCE of channel 0; channel k at 0 + k  */
    LINK_PA_IN_US0  = 9,  /**< IN_US of input 0; input k at 9 + k        */
    LINK_PA_OUT_US0 = 18, /**< OUT_US of output 0; output k at 18 + k    */
    LINK_PA_LOST    = 27, /**< bit k: input k lost                       */
    LINK_PA_WINDOW  = 28, /**< the last window closed                    */
    LINK_PA_FLAGS   = 29, /**< that window's LINK_PA_F_*                 */
    LINK_PA_GLITCH  = 30, /**< bit k: a glitch on input k in that window */
    LINK_PA_COUNT   = 31,
};

/** @brief PASS register of channel @p k's SOURCE. */
#define LINK_PA_SOURCE(k) ((unsigned)LINK_PA_SOURCE0 + (unsigned)(k))
/** @brief PASS register of input @p k's last pulse. */
#define LINK_PA_IN_US(k)  ((unsigned)LINK_PA_IN_US0 + (unsigned)(k))
/** @brief PASS register of output @p k's last pulse. */
#define LINK_PA_OUT_US(k) ((unsigned)LINK_PA_OUT_US0 + (unsigned)(k))

/** @brief SOURCE values. */
enum {
    LINK_PA_SRC_OFF   = 0, /**< the output driven low: nothing          */
    LINK_PA_SRC_PASS  = 1, /**< the receiver's pulse, through MIRROR    */
    LINK_PA_SRC_BENCH = 2, /**< the bank's output, as on the bench      */
};

/** @brief FLAGS: the capture's ring lapped in the window: its figures are
 *         not known (a measurement gap, not an error). */
#define LINK_PA_F_LAPPED     0x0001u
/** @brief FLAGS: a programming session held a pin in the window: its
 *         line's edges flood the capture, the figures are not known. */
#define LINK_PA_F_SESSION    0x0002u
/** @brief FLAGS: the capture's FIFO was full and dropped a state. */
#define LINK_PA_F_STALL      0x0004u
/** @brief FLAGS: the capture does not run (no PIO0 or DMA room, or a
 *         chip before A4): every figure reads 0. */
#define LINK_PA_F_NO_CAPTURE 0x0008u

/** @brief A window's length, ms. */
#define LINK_PA_WINDOW_MS 50u

/** @brief Registers of the PASS_STAT page. */
enum {
    LINK_PT_PERIOD0  = 0,  /**< PERIOD of input 0; input k at 0 + k  */
    LINK_PT_MISSED0  = 9,  /**< MISSED of input 0; input k at 9 + k  */
    LINK_PT_FRAMES0  = 18, /**< FRAMES of input 0; input k at 18 + k */
    LINK_PT_RESET    = 27, /**< 1 clears MISSED, FRAMES, GLITCHES   */
    LINK_PT_GLITCHES = 28, /**< highs not taken on all inputs        */
    LINK_PT_COUNT    = 29,
};

/** @brief PASS_STAT register of input @p k's frame period. */
#define LINK_PT_PERIOD(k) ((unsigned)LINK_PT_PERIOD0 + (unsigned)(k))
/** @brief PASS_STAT register of input @p k's missed frames. */
#define LINK_PT_MISSED(k) ((unsigned)LINK_PT_MISSED0 + (unsigned)(k))
/** @brief PASS_STAT register of input @p k's frames. */
#define LINK_PT_FRAMES(k) ((unsigned)LINK_PT_FRAMES0 + (unsigned)(k))

#endif /* LINK_PAGES_PASS_H */
