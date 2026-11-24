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
 * @brief The channels whose output pin something other than the pages
 *        holds: an output slot, a programming port, the supply, the
 *        sensor bus, the phase tap or the ESC wire (pass_page_busy()).
 *
 * @return Bit k for channel k.
 */
static uint16_t busy(void)
{
    return pass_page_busy(&s_outputs, &s_prog,
                          tone_taken() | tone_page_pins(&s_tone)
                          | esc_port_pins(&s_esc));
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
 * @brief pass_cap_route() as a pass_pad_fn.
 *
 * @param ctx     Not used.
 * @param k       The channel.
 * @param mirror  MIRROR's, else low.
 */
static void pad(void *ctx, unsigned k, bool mirror)
{
    (void)ctx;
    pass_cap_route(k, mirror);
}

/**
 * @brief Every held pad set as the pages say, every pass: MIRROR's for
 *        PASS-THROUGH while the bank is armed for the channel's role, low
 *        otherwise.  Nothing is cached, so a pad another driver touched
 *        (a programming port's take and give, a bind) is set again here.
 */
static void route(void)
{
    (void)pass_page_route(&s_thru, &s_outputs, pad, NULL);
}

void thru_service(uint32_t now_ms)
{
    if (s_thru.capture) {
        pass_dec_skip(&s_thru_dec, bidir_bits(), time_us_32());
        /* The head before the clock: every stamp read is at or before it. */
        const uint32_t head = pass_cap_head();
        /* The stall flag after the head: a state dropped before it is in
         * this drain, which is then undone; one dropped after it restarts
         * the decoder at this drain's newest state, before the gap. */
        const bool stalled = pass_cap_stalled();
        const uint32_t now_us = time_us_32();
        const pass_drain_t r = pass_ring_drain(&s_thru_ring, pass_cap_states(),
                                               pass_cap_stamps(), head, now_us,
                                               stalled, &s_thru_dec);
        uint16_t f = 0u;
        if (r.lapped) {
            f |= LINK_PA_F_LAPPED;
        }
        if (stalled) {
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
    if (pass_page_force_off(&s_thru, mask, busy()) != 0u) {
        reserve_held();
        route();
        outputs_set_passing(&s_outputs, pass_page_active(&s_thru));
    }
}

void thru_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    pass_page_read(&s_thru, off, n, out);
}

/**
 * @brief After the sources changed: the bank told which outputs carry a
 *        receiver's channel, and a port whose output became a throttle
 *        switched off in the same pass (rsn_throttle_hold()).
 */
static void passing_follow(void)
{
    outputs_set_passing(&s_outputs, pass_page_active(&s_thru));
    rsn_throttle_hold();
}

uint8_t thru_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    /* The sessions as they stand at this write: without the port
     * switches no channel goes to PASS-THROUGH while one is open. */
    const pass_ctx_t c = { .armed = armed(), .busy = busy(),
                           .locked = rsn_lock() };
    const uint8_t nack = pass_page_write(&s_thru, off, n, in, &c);
    if (nack == 0u) {
        /* The pins the pages now hold are no slot's, and their pads go
         * low at once. */
        reserve_held();
        route();
        passing_follow();
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
