/**
 * @file node_scan.c
 * @brief The node SETUP's I2C SCAN (node_scan.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_scan.h"

#include <string.h>

#include "ina228.h"

/**
 * @brief Set an address's bit.
 * @param bits The 16 bytes.  @param a The address.
 */
static void set_bit(uint8_t bits[16], uint8_t a)
{
    bits[a >> 3] = (uint8_t)(bits[a >> 3] | (1u << (a & 7u)));
}

/**
 * @brief An address's bit.
 * @param bits The 16 bytes.  @param a The address.
 * @return true when set.
 */
static bool get_bit(const uint8_t bits[16], uint8_t a)
{
    return a < 128u && (bits[a >> 3] & (1u << (a & 7u))) != 0u;
}

/**
 * @brief The INA228 check of #985: MANUFACTURER_ID, then DEVICE_ID from a
 *        part that answers TI's 5449h.
 * @param io The bus.  @param a The address.
 * @return true for an INA228.
 */
static bool is_ina228(const nscan_io_t *io, uint8_t a)
{
    uint16_t maker = 0u;
    uint16_t device = 0u;
    return io->read16 != NULL
           && io->read16(io->ctx, a, INA228_MANUFACTURER, &maker)
           && maker == INA228_MAKER_ID
           && io->read16(io->ctx, a, INA228_DEVICE_ID, &device)
           && ina228_identity_ok(maker, device);
}

void nscan_run(const nscan_io_t *io, nscan_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->done = true;
    if (io == NULL || io->probe == NULL) {
        return;
    }
    for (uint8_t a = NSCAN_FIRST; a <= NSCAN_LAST; ++a) {
        if (!io->probe(io->ctx, a)) {
            continue;
        }
        set_bit(out->answered, a);
        ++out->n;
        if (a >= INA228_ADDR_MIN && a <= INA228_ADDR_MAX && is_ina228(io, a)) {
            set_bit(out->ina228, a);
            ++out->n_ina228;
        }
    }
}

bool nscan_answered(const nscan_t *r, uint8_t addr)
{
    return r != NULL && get_bit(r->answered, addr);
}

bool nscan_is_ina228(const nscan_t *r, uint8_t addr)
{
    return r != NULL && get_bit(r->ina228, addr);
}

uint8_t nscan_at(const nscan_t *r, unsigned i, bool only_ina228)
{
    if (r == NULL) {
        return 0u;
    }
    unsigned k = 0u;
    for (uint8_t a = NSCAN_FIRST; a <= NSCAN_LAST; ++a) {
        if (get_bit(only_ina228 ? r->ina228 : r->answered, a)) {
            if (k == i) {
                return a;
            }
            ++k;
        }
    }
    return 0u;
}
