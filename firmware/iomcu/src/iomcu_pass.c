/**
 * @file
 * @brief The remote servo node's pass-through (IOMCU_BOARD=rsn): the PASS
 *        and PASS_STAT pages, the capture's ring drained into the decoder
 *        every main-loop pass, and each output pad routed.
 *
 * The rules are host-tested: pass_page.h for the pages and the routing,
 * protocols/passthru for the decoder and the ring, pass.pio in the host PIO
 * model.  This file reads the clock, the bank and the ports into them and
 * routes the pads they name.
 *
 * Order at boot: thru_boot() runs before any output slot is restored, so
 * MIRROR and CAPTURE take PIO0 at GPIO base 16 while it is empty.  On a
 * chip before A4 nothing starts and no pad is touched.
 *
 * Each pass, after the bank's arm: the ring drained (a lap, a full FIFO and
 * an open programming session flag the open window), the window closed
 * when due, and the pads routed.  A PASS-THROUGH pad is MIRROR's only while
 * the bank is armed for its channel's role; SAFE, the failsafe, a STOP and
 * every error leave it driven low from the pass that sees them.  OFF is
 * low always.  BENCH is not touched here.
 *
 * On the other boards (IOMCU_BOARD_RSN 0) the pages are not in the page
 * table and each function does nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pico/stdlib.h"

#include "iomcu.h"

#include "pass_page.h"
#include "rsn_pins.h"

#if IOMCU_BOARD_RSN

#include "iomcu_rsn_pio.h"
#include "pass_cap.h"
#include "pass_decode.h"
#include "pass_ring.h"

static pass_page_t s_thru;       /**< the PASS and PASS_STAT pages */
static pass_dec_t  s_thru_dec;   /**< the decoder */
static pass_ring_t s_thru_ring;  /**< the ring's reader */
static uint16_t    s_thru_mir;   /**< bit k: output k's pad is MIRROR's */
static uint16_t    s_thru_low;   /**< bit k: output k's pad is driven low */

void thru_boot(uint32_t now_ms)
{
    /* The fixed load's placement (rsn_pio.h): the fence claims PIO1's and
     * PIO2's free state machines, so MIRROR and CAPTURE go into PIO0, at
     * the GPIO base 16 that place_boot() set and pass_cap_start() sets
     * again. */
    bool cap = false;
    if (!rsn_chip_refused() && place_begin(RSN_PIO_FIXED, RSN_PIN_OUT(0))) {
        cap = pass_cap_start();
        place_end(cap);
    }
    pass_dec_init(&s_thru_dec, time_us_32());
    pass_ring_init(&s_thru_ring);
    pass_page_init(&s_thru, cap, now_ms);
}

/**
 * @brief Whether a pin is held by something other than the pages: an
 *        output slot, a programming port, the supply, the sensor bus, the
 *        phase tap or the ESC wire.
 *
 * @param pin  The GPIO.
 * @return     true when it is.
 */
static bool held_elsewhere(unsigned pin)
{
    const uint64_t taken = tone_taken() | tone_page_pins(&s_tone)
                           | esc_port_pins(&s_esc);
    if ((taken & ((uint64_t)1u << pin)) != 0u) {
        return true;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (s_outputs.slot[i].driver != OUT_DRIVER_NONE
            && s_outputs.slot[i].pin == pin) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The outputs a bidirectional DShot slot drives: their own receiver
 *        decodes the line, the capture's decoder skips it.
 *
 * @return State bits (pass_decode.h).
 */
static uint32_t bidir_bits(void)
{
    uint32_t m = 0u;
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        const out_slot_t *s = &s_outputs.slot[i];
        if (s->driver != OUT_DRIVER_DSHOT_BIDIR) {
            continue;
        }
        for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
            if (s->pin == RSN_PIN_OUT(k)) {
                m |= 1u << PASS_OUT_BIT(k);
            }
        }
    }
    return m;
}

/**
 * @brief Whether the bank is ARMED for any role, as this pass left it: a
 *        SOURCE change is refused while any role is armed.  Which pads
 *        MIRROR drives is judged per channel by its own role
 *        (pass_page_mirror()).
 *
 * @return true while ARMED.
 */
static bool armed(void)
{
    return outputs_armed_roles(&s_outputs) != 0u;
}

/**
 * @brief Each held pad as the pages say: MIRROR's for PASS-THROUGH while
 *        the bank is armed for the channel's role, low otherwise; only what
 *        changed is touched.
 */
static void route(void)
{
    const uint16_t held = pass_page_held(&s_thru);
    const uint16_t mir  = pass_page_mirror(&s_thru, &s_outputs);
    const uint16_t low  = (uint16_t)(held & ~mir);
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        const uint16_t b = (uint16_t)(1u << k);
        if ((mir & b) != 0u && (s_thru_mir & b) == 0u) {
            pass_cap_route(k, true);
        } else if ((low & b) != 0u && (s_thru_low & b) == 0u) {
            pass_cap_route(k, false);
        }
    }
    s_thru_mir = mir;
    s_thru_low = low;
}

void thru_service(uint32_t now_ms)
{
    if (s_thru.capture) {
        pass_dec_skip(&s_thru_dec, bidir_bits(), time_us_32());
        /* The head before the clock: every stamp read is at or before it. */
        const uint32_t head = pass_cap_head();
        const uint32_t now_us = time_us_32();
        const pass_drain_t r = pass_ring_drain(&s_thru_ring, pass_cap_states(),
                                               pass_cap_stamps(), head, now_us,
                                               &s_thru_dec);
        uint16_t f = 0u;
        if (r.lapped) {
            f |= LINK_PA_F_LAPPED;
        }
        if (pass_cap_stalled()) {
            f |= LINK_PA_F_STALL;
        }
        if (pass_page_session(&s_kst, &s_hitec, &s_esc)) {
            f |= LINK_PA_F_SESSION;
        }
        pass_page_flag(&s_thru, f);
    }
    (void)pass_page_tick(&s_thru, &s_thru_dec, now_ms);
    route();
}

uint64_t thru_pins(void)
{
    const uint16_t held = pass_page_held(&s_thru);
    uint64_t m = 0u;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        if ((held & (1u << k)) != 0u) {
            m |= (uint64_t)1u << RSN_PIN_OUT(k);
        }
    }
    return m;
}

uint16_t thru_active(void)
{
    return pass_page_active(&s_thru);
}

void thru_drop(uint16_t mask)
{
    uint16_t busy = 0u;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        if (held_elsewhere(RSN_PIN_OUT(k))) {
            busy |= (uint16_t)(1u << k);
        }
    }
    if (pass_page_force_off(&s_thru, mask, busy) != 0u) {
        reserve_held();
        route();
    }
}

void thru_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    pass_page_read(&s_thru, off, n, out);
}

uint8_t thru_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    pass_ctx_t c = { .armed = armed(), .busy = 0u };
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        if (held_elsewhere(RSN_PIN_OUT(k))) {
            c.busy |= (uint16_t)(1u << k);
        }
    }
    const uint8_t nack = pass_page_write(&s_thru, off, n, in, &c);
    if (nack == 0u) {
        /* The pins the pages now hold are no slot's, and their pads go
         * low at once. */
        reserve_held();
        route();
    }
    return nack;
}

void thru_stat_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    pass_stat_read(&s_thru, off, n, out);
}

uint8_t thru_stat_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return pass_stat_write(&s_thru, off, n, in, &s_thru_dec);
}

#else /* the other boards: no pass-through */

void thru_boot(uint32_t now_ms) { (void)now_ms; }

void thru_service(uint32_t now_ms) { (void)now_ms; }

uint64_t thru_pins(void) { return 0u; }

uint16_t thru_active(void) { return 0u; }

void thru_drop(uint16_t mask) { (void)mask; }

#endif /* IOMCU_BOARD_RSN */
