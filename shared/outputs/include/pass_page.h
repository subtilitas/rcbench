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

#include "esc_port.h"
#include "hitec_port.h"
#include "kst_port.h"
#include "link_pages.h"
#include "outputs.h"
#include "prog_port.h"
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
 * @brief The channels MIRROR drives: PASS-THROUGH channel k while the bank
 *        is armed for channel k's role (outputs_channel_driving()).  With
 *        surfaces armed alone a throttle channel's pad stays low, and the
 *        reverse; a role disarmed, or a channel's role changed to one not
 *        armed, takes its channels out of the mask at the next call.
 *
 * @param p  The pages.
 * @param o  The bank; NULL drives none.
 * @return   Bit k for channel k.
 */
uint16_t pass_page_mirror(const pass_page_t *p, const outputs_t *o);

/**
 * @brief The channels whose output pin the pages hold: OFF and
 *        PASS-THROUGH.
 *
 * @param p  The pages.
 * @return   Bit k for channel k.
 */
uint16_t pass_page_held(const pass_page_t *p);

/**
 * @brief The channels whose output a receiver drives: PASS-THROUGH.  A
 *        programming session on the common rail counts them as active
 *        channels beside the bound slots and ports (PLAN 2.6); BENCH
 *        counts through its slot, OFF drives nothing.
 *
 * @param p  The pages.
 * @return   Bit k for channel k.
 */
uint16_t pass_page_active(const pass_page_t *p);

/**
 * @brief Channels a programming session switched off go to OFF, whatever
 *        the bank's arm: PASS-THROUGH always, BENCH where no slot, port or
 *        page holds the pin (@p busy); a BENCH channel whose pin a slot
 *        holds stays BENCH, its slot being the session's to stop.
 *
 * @param p     The pages.
 * @param mask  Bit k: channel k switched off.
 * @param busy  Bit k: output k's pin is held by something else.
 * @return      The channels of @p mask now OFF.
 */
uint16_t pass_page_force_off(pass_page_t *p, uint16_t mask, uint16_t busy);

/**
 * @brief The channels whose output pin something other than the pages
 *        holds: an output slot, a programming port (prog_port_pins(),
 *        a prebound SERVO PWM port included), or a pin of @p other.  A
 *        SOURCE change on such a channel is refused with BUSY.
 *
 * @param o      The bank; NULL for none.
 * @param pp     The programming ports; NULL for none.
 * @param other  One bit per GPIO held elsewhere: the supply, the sensor
 *               bus, the phase tap, the ESC wire.
 * @return       Bit k for channel k, whose output is RSN_PIN_OUT(k).
 */
uint16_t pass_page_busy(const outputs_t *o, const prog_port_t *pp,
                        uint64_t other);

/** @brief Sets output k's pad: MIRROR's (PIO0) when @p mirror, else SIO
 *         driven low.  Must be idempotent: it is called every pass. */
typedef void (*pass_pad_fn)(void *ctx, unsigned k, bool mirror);

/**
 * @brief Every pad the pages hold, set as the pages say, on every call:
 *        MIRROR's for the channels pass_page_mirror() names, low for the
 *        other held ones.  No state is kept between calls, so a pad that
 *        another driver touched meanwhile is set again on the next call.
 *        Pads of BENCH channels are not touched.
 *
 * @param p    The pages.
 * @param o    The bank (pass_page_mirror()).
 * @param pad  Sets one pad.
 * @param ctx  @p pad's first argument.
 * @return     The channels routed to MIRROR.
 */
uint16_t pass_page_route(const pass_page_t *p, const outputs_t *o,
                         pass_pad_fn pad, void *ctx);

/**
 * @brief Whether a programming session is open, for FLAGS bit 1
 *        (LINK_PA_F_SESSION): a KST or HITEC session out of PWM or holding
 *        its port's pin, or an ESC_WIRE session on a programming port or on
 *        an output slot, from OPEN until its line is given back.
 *
 * @param kst    The KST port; NULL for none.
 * @param hitec  The HITEC port; NULL for none.
 * @param esc    The ESC_WIRE port; NULL for none.
 * @return       true while one is open.
 */
bool pass_page_session(const kst_port_t *kst, const hitec_port_t *hitec,
                       const esc_port_t *esc);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PASS_PAGE_H */
