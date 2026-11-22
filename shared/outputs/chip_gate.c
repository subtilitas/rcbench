/**
 * @file
 * @brief The remote servo node's chip check.
 *
 * See chip_gate.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "chip_gate.h"

#include "link_pages.h"

bool chip_gate_ok(uint8_t chip_rev, uint8_t rom_ver)
{
    return chip_rev >= CHIP_GATE_REV_MIN && rom_ver >= CHIP_GATE_ROM_MIN;
}

uint16_t chip_gate_fault(uint8_t chip_rev, uint8_t rom_ver)
{
    return chip_gate_ok(chip_rev, rom_ver) ? 0u
                                           : (uint16_t)LINK_FAULT_CHIP_STEPPING;
}
