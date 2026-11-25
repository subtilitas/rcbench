/**
 * @file
 * @brief The writes that could drive a pin, refused as one.
 *
 * See link_gate.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_gate.h"

#include <stddef.h>

#include "link_msg.h"
#include "link_pages.h"

/**
 * @brief Whether the write puts a value other than 0 into register @p reg.
 *
 * @param off  First register written.
 * @param n    Registers written.
 * @param in   The values.
 * @param reg  The register asked about.
 * @return     true when @p reg is among the written ones and not 0.
 */
static bool sets(uint8_t off, uint8_t n, const uint16_t *in, unsigned reg)
{
    return reg >= (unsigned)off && reg < (unsigned)off + n
           && in[reg - off] != 0u;
}

/**
 * @brief Whether an OUTPUTS-shaped write gives any slot a driver.
 *
 * @param off  First register written.
 * @param n    Registers written.
 * @param in   The values.
 * @return     true when a DRIVER register is among them and not
 *             LINK_DRIVER_NONE.
 */
static bool binds(uint8_t off, uint8_t n, const uint16_t *in)
{
    for (unsigned i = 0; i < n; ++i) {
        const unsigned reg = (unsigned)off + i;
        if (reg % LINK_OS_STRIDE == (unsigned)LINK_OS_DRIVER
            && in[i] != (uint16_t)LINK_DRIVER_NONE) {
            return true;
        }
    }
    return false;
}

uint8_t link_gate_drive(uint8_t page, uint8_t off, uint8_t n,
                        const uint16_t *in)
{
    if (n == 0u) {
        return 0u;
    }
    if (in == NULL) {
        return (uint8_t)LINK_NACK_NOT_ARMED;
    }
    bool refuse = false;
    switch (page) {
    case LINK_PAGE_CONTROL:
        refuse = sets(off, n, in, LINK_CT_ARM);
        break;
    case LINK_PAGE_SUPPLY:
        refuse = sets(off, n, in, LINK_SP_OUTPUT);
        break;
    case LINK_PAGE_POWER:
        refuse = sets(off, n, in, LINK_PW_RAIL)
                 || sets(off, n, in, LINK_PW_PORT_WANT);
        break;
    case LINK_PAGE_PASS:
        /* OFF drives the pad low and PASS-THROUGH gives it to MIRROR:
         * only BENCH, which leaves the pad to an output slot, is taken. */
        for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
            const unsigned reg = LINK_PA_SOURCE(k);
            if (reg >= (unsigned)off && reg < (unsigned)off + n
                && in[reg - off] != (uint16_t)LINK_PA_SRC_BENCH) {
                refuse = true;
            }
        }
        break;
    case LINK_PAGE_KST:
    case LINK_PAGE_HITEC:
    case LINK_PAGE_ESC_WIRE:
    case LINK_PAGE_YGE_PROG:
        refuse = true;          /* every command, CONNECT among them */
        break;
    case LINK_PAGE_PROG:
        for (unsigned p = 0; p < LINK_PG_PORTS_MAX; ++p) {
            const unsigned reg = LINK_PG_FUNC(p);
            if (reg >= (unsigned)off && reg < (unsigned)off + n
                && in[reg - off] != (uint16_t)LINK_PG_FN_NONE) {
                refuse = true;
            }
        }
        break;
    case LINK_PAGE_OUTPUTS:
    case LINK_PAGE_OUTPUTS_HI:
    case LINK_PAGE_BIND_OUT:
    case LINK_PAGE_BIND_OUT_HI:
        refuse = binds(off, n, in);
        break;
    default:
        break;
    }
    return refuse ? (uint8_t)LINK_NACK_NOT_ARMED : 0u;
}
