/**
 * @file
 * @brief The boot ROM's record of the IOmcu's last boot as one console
 *        line: boot type, partition booted, diagnostic source and the
 *        diagnostic word.
 *
 * rom_get_boot_info() (pico-sdk 2.3.0, pico/bootrom.h) returns the boot
 * word 0xttppbbdd -- tt TBYB (try before you buy) and update flags, pp the
 * partition booted, bb the boot type, dd the diagnostic source -- and the
 * diagnostic word. The source says what the word describes: after a
 * power-on reset boot slot 0 (the partition table's search, slot 0 in bits
 * 0-15 and slot 1 in bits 16-31); after `picotool reboot -a -g <n>`
 * partition n and its B partition (A in bits 0-15, B in bits 16-31). The
 * bits are the BOOT_DIAGNOSTIC_* values of boot/bootrom_constants.h.
 *
 * Pure C with no pico-sdk; the firmware fills reset_regs_t once at boot
 * (firmware/iomcu/src/iomcu_report.c).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_BOOT_DIAG_H
#define RCBENCH_BOOT_DIAG_H

#include <stddef.h>

#include "reset_cause.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The partition codes of the boot word (bootrom_constants.h). */
#define BOOT_DIAG_PARTITION_NONE   (-1) /**< no partition */
#define BOOT_DIAG_PARTITION_SLOT0  (-2) /**< boot slot 0 */
#define BOOT_DIAG_PARTITION_SLOT1  (-3) /**< boot slot 1 */
#define BOOT_DIAG_PARTITION_WINDOW (-4) /**< a RAM or flash window */

/** @brief Bytes that hold the longest line text with its NUL. */
#define BOOT_DIAG_TEXT_MAX 112u

/**
 * @brief The line's text:
 *        `boot type NORMAL partition 1 source slot 0 diagnostic 0x00010355`,
 *        or `boot info none` when rom_get_boot_info() failed.
 *
 * The type is NORMAL, BOOTSEL, RAM_IMAGE, FLASH_UPDATE, PC_SP or its value
 * in hex, with `+CHAINED` for a chained boot. A partition or source is
 * `none`, `slot 0`, `slot 1`, `window`, its number 0 to 15, or its value
 * in hex.
 *
 * @param r    The raw values; NULL reads as no boot info.
 * @param out  Receives the text, NUL-terminated and cut to fit.
 * @param n    Size of @p out in bytes; BOOT_DIAG_TEXT_MAX holds it.
 * @return The length the whole text needs, without the NUL, as snprintf().
 */
size_t boot_diag_text(const reset_regs_t *r, char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_BOOT_DIAG_H */
