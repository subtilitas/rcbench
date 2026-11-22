/**
 * @file
 * @brief The remote servo node's chip check (board 3): the RP2350 A4
 *        stepping at boot, and what a chip before it refuses.
 *
 * The node's outputs carry no external pull-down and a bidirectional DShot
 * line idles high on the pad's pull-up: only an A4 pad holds low on its own
 * pull-down (erratum E9 fixed).  The IOmcu of board 3 therefore reads the
 * stepping before anything else and, on an earlier chip, drives nothing:
 * RAIL_EN and every PORT_EN stay low, no output is bound, every output pad
 * stays in its reset state, and the link refuses whatever could drive a pin
 * (link_dev_refuse_drive(), link_gate.h).  Boards 1 and 2 do not check.
 *
 * The stepping is read twice over: SYSINFO CHIP_ID's REVISION (bits 31 to
 * 28, rp2350_chip_version()) is 2 on A2 and 3 on A3 and A4; the boot ROM's
 * version byte at 0x13 (rp2350_rom_version()) is 2 on A2, 3 on A3 and 4 on
 * A4.  Only the ROM byte tells A3 from A4.
 *
 * Pure logic, host-tested in test_chip_gate.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_CHIP_GATE_H
#define RCBENCH_CHIP_GATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The lowest chip revision taken: A3 and A4 read 3. */
#define CHIP_GATE_REV_MIN 3u
/** @brief The lowest boot ROM version taken: A4 reads 4. */
#define CHIP_GATE_ROM_MIN 4u

/**
 * @brief Whether the chip is of the A4 stepping or later.
 *
 * @param chip_rev  rp2350_chip_version().
 * @param rom_ver   rp2350_rom_version().
 * @return true for a revision of CHIP_GATE_REV_MIN or more with a ROM of
 *         CHIP_GATE_ROM_MIN or more; false otherwise.
 */
bool chip_gate_ok(uint8_t chip_rev, uint8_t rom_ver);

/**
 * @brief STATUS's fault bit for the chip: LINK_FAULT_CHIP_STEPPING on a
 *        chip chip_gate_ok() refuses.
 *
 * @param chip_rev  rp2350_chip_version().
 * @param rom_ver   rp2350_rom_version().
 * @return LINK_FAULT_CHIP_STEPPING or 0.
 */
uint16_t chip_gate_fault(uint8_t chip_rev, uint8_t rom_ver);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CHIP_GATE_H */
