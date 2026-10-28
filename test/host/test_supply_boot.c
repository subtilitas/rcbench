/*
 * The IOmcu's start-up switches a PD mini left on off before anything else.
 *
 * Under test: the start-up's passes (supply_boot_off() in
 * firmware/iomcu/src/iomcu_sense.c, modelled here pass for pass: the page's
 * step with the output asked off, the bytes in, the driver's step and
 * AUTO's next rate between transactions) against a modelled module that
 * answers only at its own rate and holds its output until it is written.
 * A module left on is switched off within 100ms at a set rate; under AUTO
 * at each of the seven rates, the slowest 9600 last, within 3500ms
 * (SUPPLY_PAGE_BOOT_OFF_MS), which needs pdmini_hurry(): at the driver's
 * own 1000ms between WHO_AM_I the last rate is not reached in time.  A
 * module that reads off ends the wait at its first state read; one that
 * never answers ends it at the bound, never before or after.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_page.h"

/* ------------------------------------------------------------- the module */

static struct {
    bool     powered;
    uint8_t  rate;            /* the setting it answers at, 0 to 6        */
    bool     output;
    bool     attached;
    uint8_t  out[48];         /* a reply on its way, all of it at out_at  */
    size_t   out_n;
    uint32_t out_at;
    unsigned whoami;          /* WHO_AM_I heard at its own rate           */
    uint32_t off_at;          /* when the output went off, 0 never        */
} m;

static uint32_t      now;
static uint8_t       bus_rate;   /* the rate the UART runs at */
static pdmini_t      d;
static supply_page_t pg;
static outputs_t     o;

static void m_attach(void *ctx)
{
    (void)ctx;
    m.attached = true;
}

static void m_detach(void *ctx)
{
    (void)ctx;
    m.attached = false;
    m.out_n = 0u;
}

static void m_reply(const uint8_t *p, size_t n)
{
    memcpy(m.out, p, n);
    m.out[n] = pdmini_crc8(p, n);
    m.out_n  = n + 1u;
    m.out_at = now + 2u;
}

static void m_send(void *ctx, const uint8_t *p, size_t n)
{
    (void)ctx;
    if (!m.powered || !m.attached || bus_rate != m.rate || n < 1u
        || pdmini_crc8(p, n - 1u) != p[n - 1u]) {
        return;               /* nothing heard, or garbage at its rate */
    }
    switch (p[0]) {
    case PDMINI_WHO_AM_I: {
        static const char who[] = "WeAct Studio PD Power Mini V1 BUCK";
        uint8_t r[40];
        r[0] = PDMINI_WHO_AM_I;
        r[1] = (uint8_t)(sizeof(who) - 1u);
        memcpy(&r[2], who, sizeof(who) - 1u);
        ++m.whoami;
        m_reply(r, 2u + sizeof(who) - 1u);
        break;
    }
    case PDMINI_READ_STATE: {
        const uint8_t r[2] = { PDMINI_READ_STATE, m.output ? 1u : 0u };
        m_reply(r, 2u);
        break;
    }
    case PDMINI_READ_ID: {
        const uint8_t r[2] = { PDMINI_READ_ID, 0u };
        m_reply(r, 2u);
        break;
    }
    case PDMINI_READ_DATA: {
        const uint8_t r[6] = { PDMINI_READ_DATA, 0u, 0x88u, 0x13u, 0xE8u,
                               0x03u };          /* 5000mV, 1000mA */
        m_reply(r, 6u);
        break;
    }
    case PDMINI_READ_DISPLAY: {
        const uint8_t r[5] = { PDMINI_READ_DISPLAY, 0u, 0u, 0u, 0u };
        m_reply(r, 5u);
        break;
    }
    case PDMINI_READ_INPUT: {
        const uint8_t r[6] = { PDMINI_READ_INPUT, 5u, 0xC0u, 0x5Du, 0u,
                               0u };             /* PD, 24000mV */
        m_reply(r, 6u);
        break;
    }
    case PDMINI_OUTPUT_EN:
        if (n == 3u) {
            const bool was = m.output;
            m.output = p[1] == 1u;                /* 1 is on */
            if (was && !m.output) {
                m.off_at = now;
            }
        }
        break;
    default:
        break;
    }
}

static const pdmini_io_t k_io = { m_attach, m_detach, m_send, NULL };

/* --------------------------------------------------------- the start-up */

/*
 * The wiring restored at start-up with @p baud (0 to 6, or AUTO), and the
 * driver attached as supply_rewire() attaches it: restored, maybe on.
 */
static void start(uint16_t baud, uint32_t t0)
{
    now = t0;
    outputs_init(&o, now);
    supply_page_init(&pg);
    const uint16_t r[4] = { 1u, 8u, 9u, baud };
    CHECK_EQ(supply_page_write(&pg, LINK_SP_ENABLE, 4u, r, &o, false), 0u);
    pdmini_init(&d, &k_io, now);
    pdmini_restored(&d);
    bus_rate = supply_page_rate(&pg, NULL);
}

/* One pass of supply_pass() with the output asked off, at @p now. */
static void pass(void)
{
    supply_page_port_frame(&pg, false, now);
    supply_page_step(&pg, false, &d);
    if (m.out_n > 0u && (int32_t)(now - m.out_at) >= 0) {
        for (size_t i = 0; i < m.out_n; ++i) {
            pdmini_rx(&d, m.out[i], now);
        }
        m.out_n = 0u;
    }
    pdmini_step(&d, now);
    const uint8_t rate = supply_page_rate(&pg, &d);
    if (rate != bus_rate && (d.phase == PD_IDLE || d.phase == PD_GAP)) {
        bus_rate = rate;
    }
}

/* supply_boot_off(): passes every 1ms until the page owes nothing.  The
 * time it took, ms. */
static uint32_t boot_off(bool hurry)
{
    const uint32_t t0 = now;
    pdmini_hurry(&d, hurry);
    while (supply_page_boot_owed(&pg, &d, now - t0)) {
        pass();
        ++now;
    }
    pdmini_hurry(&d, false);
    return now - t0;
}

static void module_on_at(uint8_t rate)
{
    memset(&m, 0, sizeof(m));
    m.powered = true;
    m.rate    = rate;
    m.output  = true;
}

/* A module left on at the set rate 19200: off within 100ms, and the wait
 * ends once a read shows it off. */
TEST_CASE(a_module_left_on_is_off_within_100_ms_at_a_set_rate)
{
    module_on_at(1u);
    start(1u, 5000u);
    const uint32_t took = boot_off(true);
    CHECK(!m.output);
    CHECK(m.off_at - 5000u <= 100u);
    CHECK(took < 500u);
    CHECK(!pdmini_may_be_on(&d));
}

/* Under AUTO, a module left on at every rate the scan can find: off within
 * the bound, the slowest the last rate tried, 9600. */
TEST_CASE(a_module_left_on_is_off_within_the_bound_at_every_rate)
{
    for (uint8_t rate = 0u; rate < SUPPLY_BAUD_COUNT; ++rate) {
        module_on_at(rate);
        start(SUPPLY_BAUD_AUTO, 5000u);
        const uint32_t took = boot_off(true);
        CHECK(!m.output);
        CHECK(m.off_at - 5000u < SUPPLY_PAGE_BOOT_OFF_MS);
        CHECK(took < SUPPLY_PAGE_BOOT_OFF_MS);
        CHECK_EQ(m.whoami, 1u);
    }
}

/* Without the hurry, WHO_AM_I comes once a second: the scan does not reach
 * 9600 inside the bound, and the module is still on when the wait ends. */
TEST_CASE(without_the_hurry_the_last_rate_is_not_reached)
{
    module_on_at(0u);
    start(SUPPLY_BAUD_AUTO, 5000u);
    CHECK_EQ(boot_off(false), SUPPLY_PAGE_BOOT_OFF_MS);
    CHECK(m.output);
}

/* A module that reads off: the wait ends at its first state read, and no
 * OUTPUT_EN is written to it. */
TEST_CASE(a_module_off_ends_the_wait_at_its_state_read)
{
    module_on_at(1u);
    m.output = false;
    start(1u, 5000u);
    const uint32_t took = boot_off(true);
    CHECK(!m.output);
    CHECK_EQ(m.off_at, 0u);
    CHECK(took < 100u);
}

/* A module that does not answer: the wait ends at the bound, 3500ms, not
 * a millisecond before and not after; across the 2^32ms wrap as at 5s. */
TEST_CASE(a_silent_module_ends_the_wait_at_the_bound)
{
    static const uint32_t k_t0[] = { 5000u, 0u - 1000u };
    for (size_t i = 0; i < sizeof(k_t0) / sizeof(k_t0[0]); ++i) {
        module_on_at(1u);
        m.powered = false;
        start(SUPPLY_BAUD_AUTO, k_t0[i]);
        CHECK_EQ(boot_off(true), SUPPLY_PAGE_BOOT_OFF_MS);
        CHECK(pdmini_may_be_on(&d));        /* the main loop goes on */
        CHECK(!supply_page_boot_owed(&pg, &d, SUPPLY_PAGE_BOOT_OFF_MS));
        CHECK(supply_page_boot_owed(&pg, &d, SUPPLY_PAGE_BOOT_OFF_MS - 1u));
    }
}

/* Nothing wired, or no driver: nothing to wait for. */
TEST_CASE(nothing_wired_owes_no_wait)
{
    supply_page_init(&pg);
    pdmini_init(&d, &k_io, 0u);
    pdmini_restored(&d);
    CHECK(!supply_page_boot_owed(&pg, &d, 0u));
    start(1u, 0u);
    CHECK(supply_page_boot_owed(&pg, &d, 0u));
    CHECK(!supply_page_boot_owed(&pg, NULL, 0u));
    CHECK(!supply_page_boot_owed(NULL, &d, 0u));
}

/* The hurry ends with the wait: WHO_AM_I goes back to one a second. */
TEST_CASE(after_the_wait_who_am_i_is_once_a_second_again)
{
    module_on_at(1u);
    m.powered = false;
    start(1u, 5000u);
    (void)boot_off(true);
    const unsigned fails = d.who_failed;
    for (uint32_t k = 0; k < 2000u; ++k) {
        pass();
        ++now;
    }
    /* The one on the wire as the wait ended, and one a second after it;
     * hurried, 2000ms would hold 4 or 5. */
    CHECK(d.who_failed - fails <= 3u);
}

int main(void)
{
    RUN(a_module_left_on_is_off_within_100_ms_at_a_set_rate);
    RUN(a_module_left_on_is_off_within_the_bound_at_every_rate);
    RUN(without_the_hurry_the_last_rate_is_not_reached);
    RUN(a_module_off_ends_the_wait_at_its_state_read);
    RUN(a_silent_module_ends_the_wait_at_the_bound);
    RUN(nothing_wired_owes_no_wait);
    RUN(after_the_wait_who_am_i_is_once_a_second_again);
    return test_summary("supply_boot");
}
