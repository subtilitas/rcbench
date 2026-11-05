/**
 * @file
 * @brief Which flash ranges the IOmcu's firmware update may erase or
 * program: whole sectors and pages between the end of the boot ROM's two
 * boot slots and the start of the output store.
 *
 * The first 8KiB of flash are the RP2350 boot ROM's boot slots 0 and 1
 * (RP2350 datasheet, section 5.1.15). Slot 0 holds the partition table
 * (firmware/iomcu/partitions.json); with it gone the boot ROM finds no slot
 * to boot and stays in BOOTSEL. Only the install UF2 (USB Flashing Format)
 * writes there, over USB. The application refuses every erase and program
 * that touches a byte below OUT_STORE_RANGE_FLOOR.
 *
 * Pure C, no flash API, so the host suite holds the arithmetic.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_STORE_RANGE_H
#define RCBENCH_OUT_STORE_RANGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The boot ROM's boot slots 0 and 1: 2 x 4KiB from offset 0. */
#define OUT_STORE_RANGE_FLOOR 0x2000u
/** @brief A flash sector, the unit of an erase: 4KiB. */
#define OUT_STORE_RANGE_SECTOR 4096u
/** @brief A flash page, the unit of a program: 256 bytes. */
#define OUT_STORE_RANGE_PAGE 256u

/**
 * @brief Whether a sector erase at @p off may run: @p off a whole sector,
 * at or above OUT_STORE_RANGE_FLOOR, and the sector ending at or before
 * @p ceiling.
 *
 * @param off      Offset from the start of flash, in bytes.
 * @param ceiling  First byte the update may not touch: the output store's
 *                 offset.
 * @return True when the erase may run.
 */
bool out_store_range_erase_ok(uint32_t off, uint32_t ceiling);

/**
 * @brief Whether a program of @p len bytes at @p off may run: @p off a
 * whole sector at or above OUT_STORE_RANGE_FLOOR, @p len whole pages and at
 * most one sector, and the range ending at or before @p ceiling.
 *
 * @param off      Offset from the start of flash, in bytes.
 * @param len      Bytes to program.
 * @param ceiling  First byte the update may not touch.
 * @return True when the program may run.
 */
bool out_store_range_program_ok(uint32_t off, uint32_t len, uint32_t ceiling);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_STORE_RANGE_H */
