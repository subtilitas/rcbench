/*
 * The node SETUP's I2C SCAN (firmware/node/main/node_scan.c), over fake
 * parts.
 *
 * The failures this is about: an address of 0x08 to 0x77 skipped (the
 * edges, the display board's codec at 0x18 and touch at 0x38), a reserved
 * address asked, a part written to, the INA228 check run outside 0x40 to
 * 0x4F, and a part of another maker or another TI die marked as an
 * INA228.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ina228.h"
#include "node_scan.h"

/** @brief A fake part: its address and its two ID registers. */
typedef struct {
    uint8_t  addr;
    uint16_t maker;
    uint16_t device;
} part_t;

/** @brief The fake bus: its parts and what was asked of it. */
typedef struct {
    const part_t *parts;
    size_t        n;
    unsigned      probes[128];   /**< per address */
    unsigned      reads[128];    /**< per address */
    uint8_t       regs_seen[8];  /**< the registers read, in order */
    unsigned      reg_n;
    bool          fail_reads;
} bus_t;

/** @brief The part at an address. */
static const part_t *at(const bus_t *b, uint8_t a)
{
    for (size_t i = 0; i < b->n; ++i) {
        if (b->parts[i].addr == a) {
            return &b->parts[i];
        }
    }
    return NULL;
}

/** @brief nscan_io_t's probe. */
static bool f_probe(void *ctx, uint8_t addr)
{
    bus_t *b = ctx;
    ++b->probes[addr & 0x7Fu];
    return at(b, addr) != NULL;
}

/** @brief nscan_io_t's register read. */
static bool f_read16(void *ctx, uint8_t addr, uint8_t reg, uint16_t *v)
{
    bus_t *b = ctx;
    ++b->reads[addr & 0x7Fu];
    if (b->reg_n < sizeof b->regs_seen) {
        b->regs_seen[b->reg_n++] = reg;
    }
    const part_t *p = at(b, addr);
    if (p == NULL || b->fail_reads) {
        return false;
    }
    *v = reg == INA228_MANUFACTURER ? p->maker
       : reg == INA228_DEVICE_ID    ? p->device : 0u;
    return true;
}

/** @brief The display node's bus with an INA228, an ADS1115, an INA238
 *         and parts at both edges. */
static const part_t k_parts[] = {
    { 0x08u, 0u, 0u },              /* the lowest address asked */
    { 0x18u, 0u, 0u },              /* ES8311 codec */
    { 0x38u, 0u, 0u },              /* FT6336G touch */
    { 0x40u, 0x5449u, 0x2281u },    /* INA228 */
    { 0x45u, 0x5449u, 0x2280u },    /* INA228, the MATEK's address */
    { 0x48u, 0x0000u, 0x8583u },    /* ADS1115: not TI's ID registers */
    { 0x4Au, 0x5449u, 0x2381u },    /* INA238: TI, another die */
    { 0x77u, 0u, 0u },              /* the highest address asked */
};

TEST_CASE(every_address_of_0x08_to_0x77_is_probed_once_and_no_other)
{
    bus_t b;
    memset(&b, 0, sizeof b);
    b.parts = k_parts;
    b.n = sizeof k_parts / sizeof k_parts[0];
    const nscan_io_t io = { f_probe, f_read16, &b };
    nscan_t r;
    nscan_run(&io, &r);
    for (unsigned a = 0u; a < 128u; ++a) {
        const unsigned want = (a >= 0x08u && a <= 0x77u) ? 1u : 0u;
        if (b.probes[a] != want) {
            T_FAIL("address 0x%02X probed %u times", a, b.probes[a]);
        }
    }
    CHECK(r.done);
    CHECK_EQ(r.n, 8u);
    CHECK(nscan_answered(&r, 0x08u));
    CHECK(nscan_answered(&r, 0x18u));
    CHECK(nscan_answered(&r, 0x38u));
    CHECK(nscan_answered(&r, 0x77u));
    CHECK(!nscan_answered(&r, 0x41u));
}

TEST_CASE(only_ina228s_are_marked_by_the_maker_and_die_check)
{
    bus_t b;
    memset(&b, 0, sizeof b);
    b.parts = k_parts;
    b.n = sizeof k_parts / sizeof k_parts[0];
    const nscan_io_t io = { f_probe, f_read16, &b };
    nscan_t r;
    nscan_run(&io, &r);
    CHECK_EQ(r.n_ina228, 2u);
    CHECK(nscan_is_ina228(&r, 0x40u));
    CHECK(nscan_is_ina228(&r, 0x45u));
    CHECK(!nscan_is_ina228(&r, 0x48u));
    CHECK(!nscan_is_ina228(&r, 0x4Au));
    CHECK_EQ(nscan_at(&r, 0u, true), 0x40u);
    CHECK_EQ(nscan_at(&r, 1u, true), 0x45u);
    CHECK_EQ(nscan_at(&r, 2u, true), 0u);
    CHECK_EQ(nscan_at(&r, 0u, false), 0x08u);
    CHECK_EQ(nscan_at(&r, 7u, false), 0x77u);
    CHECK_EQ(nscan_at(&r, 8u, false), 0u);
}

TEST_CASE(nothing_is_written_and_ids_are_read_only_in_0x40_to_0x4f)
{
    bus_t b;
    memset(&b, 0, sizeof b);
    b.parts = k_parts;
    b.n = sizeof k_parts / sizeof k_parts[0];
    /* The bus calls hold no write: the probe carries no data byte, and
     * the reads are MANUFACTURER_ID and DEVICE_ID alone. */
    const nscan_io_t io = { f_probe, f_read16, &b };
    nscan_t r;
    nscan_run(&io, &r);
    for (unsigned a = 0u; a < 128u; ++a) {
        const bool range = a >= INA228_ADDR_MIN && a <= INA228_ADDR_MAX;
        if (b.reads[a] != 0u && !(range && at(&b, (uint8_t)a) != NULL)) {
            T_FAIL("address 0x%02X read %u times", a, b.reads[a]);
        }
    }
    CHECK_EQ(b.reads[0x18], 0u);
    CHECK_EQ(b.reads[0x38], 0u);
    CHECK_EQ(b.reads[0x48], 1u);   /* MANUFACTURER_ID answers 0000h: done */
    for (unsigned i = 0u; i < b.reg_n; ++i) {
        CHECK(b.regs_seen[i] == INA228_MANUFACTURER
              || b.regs_seen[i] == INA228_DEVICE_ID);
    }
}

TEST_CASE(an_empty_bus_and_failed_reads_mark_nothing)
{
    bus_t b;
    memset(&b, 0, sizeof b);
    const nscan_io_t io = { f_probe, f_read16, &b };
    nscan_t r;
    nscan_run(&io, &r);
    CHECK(r.done);
    CHECK_EQ(r.n, 0u);
    b.parts = k_parts;
    b.n = sizeof k_parts / sizeof k_parts[0];
    b.fail_reads = true;
    nscan_run(&io, &r);
    CHECK_EQ(r.n, 8u);
    CHECK_EQ(r.n_ina228, 0u);
    nscan_run(NULL, &r);
    CHECK(r.done);
    CHECK_EQ(r.n, 0u);
    CHECK(!nscan_answered(NULL, 0x40u));
}

int main(void)
{
    RUN(every_address_of_0x08_to_0x77_is_probed_once_and_no_other);
    RUN(only_ina228s_are_marked_by_the_maker_and_die_check);
    RUN(nothing_is_written_and_ids_are_read_only_in_0x40_to_0x4f);
    RUN(an_empty_bus_and_failed_reads_mark_nothing);
    return test_summary("node_scan");
}
