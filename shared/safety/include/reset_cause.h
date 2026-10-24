/**
 * @file
 * @brief Why the IOmcu last started, named from the registers its boot
 *        leaves behind.
 *
 * Three records survive a restart, and each is cleared by a different reset
 * (RP2350 datasheet, sections 7.3, 7.4, 12.9.4 and 12.9.5):
 *
 * - POWMAN CHIP_RESET latches the source of the last chip-level reset: POR,
 *   BOR, the RUN pin, a debugger, a glitch, a switched-core power-down, or a
 *   watchdog that POWMAN WDSEL lets reset the chip.  Its HAD_* bits are RO
 *   (table 489), so software cannot clear them.  A watchdog reset through
 *   PSM WDSEL alone is a system reset, not a chip-level one, and leaves
 *   CHIP_RESET as it was: after the boot ROM's reboot(), which sets
 *   PSM WDSEL = ~PROC_COLD and leaves POWMAN WDSEL at 0, CHIP_RESET still
 *   reads HAD_POR from the power-up before.
 * - WATCHDOG REASON names the last watchdog trigger, TIMER or FORCE.  Every
 *   chip-level reset resets the watchdog, so REASON reads 0 after one.  The
 *   boot ROM's reboot() runs the watchdog in timer mode, so a reboot it
 *   performs reads TIMER.
 * - WATCHDOG SCRATCH2 and SCRATCH3 hold p0 and p1 of the last reboot() of
 *   any type but PC_SP.  A chip-level reset by the watchdog and the RUN pin
 *   or DVDD (digital core supply) cycling reset them.
 *
 * The boot ROM's boot info (rom_get_boot_info()) gives the type of this
 * boot; a type other than NORMAL comes only from a reboot() vector in the
 * watchdog scratch registers.  This file turns these raw values into one
 * fixed word.
 *
 * Pure C with no pico-sdk: the firmware reads the registers once at boot
 * (firmware/iomcu/src/iomcu_report.c) and holds the bit values below equal
 * to the SDK's with _Static_assert.
 *
 * Bit names and positions are those of pico-sdk 2.3.0,
 * src/rp2350/hardware_regs/include/hardware/regs/powman.h and watchdog.h,
 * and the boot types of boot/bootrom_constants.h.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_RESET_CAUSE_H
#define RCBENCH_RESET_CAUSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* POWMAN CHIP_RESET: the source of the last chip-level reset. */
#define RESET_CHIP_HAD_POR                  0x00010000u /**< bit 16 */
#define RESET_CHIP_HAD_BOR                  0x00020000u /**< bit 17 */
#define RESET_CHIP_HAD_RUN_LOW              0x00040000u /**< bit 18 */
#define RESET_CHIP_HAD_DP_RESET_REQ         0x00080000u /**< bit 19 */
#define RESET_CHIP_HAD_RESCUE               0x00200000u /**< bit 21 */
#define RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN_ASYNC 0x00400000u /**< bit 22 */
#define RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN 0x00800000u /**< bit 23 */
#define RESET_CHIP_HAD_WATCHDOG_RESET_SWCORE 0x01000000u /**< bit 24 */
#define RESET_CHIP_HAD_SWCORE_PD            0x02000000u /**< bit 25 */
#define RESET_CHIP_HAD_GLITCH_DETECT        0x04000000u /**< bit 26 */
#define RESET_CHIP_HAD_HZD_SYS_RESET_REQ    0x08000000u /**< bit 27 */
#define RESET_CHIP_HAD_WATCHDOG_RESET_PSM   0x10000000u /**< bit 28 */

/** @brief Every CHIP_RESET bit that names a watchdog reset. */
#define RESET_CHIP_HAD_WATCHDOG_ANY                                         \
    (RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN_ASYNC                             \
     | RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN                                 \
     | RESET_CHIP_HAD_WATCHDOG_RESET_SWCORE                                 \
     | RESET_CHIP_HAD_WATCHDOG_RESET_PSM)

/* WATCHDOG REASON: both bits zero after a reset that was not the watchdog. */
#define RESET_WD_REASON_TIMER 0x1u /**< the watchdog counted down to zero */
#define RESET_WD_REASON_FORCE 0x2u /**< CTRL.TRIGGER was written */

/* The boot type, byte 1 of the boot ROM's boot word (0xttppbbdd). */
#define RESET_BOOT_TYPE_NORMAL       0x00u /**< no one-shot boot type */
#define RESET_BOOT_TYPE_BOOTSEL      0x02u /**< USB or UART boot */
#define RESET_BOOT_TYPE_RAM_IMAGE    0x03u /**< an image in SRAM */
#define RESET_BOOT_TYPE_FLASH_UPDATE 0x04u /**< after a flash write */
#define RESET_BOOT_TYPE_PC_SP        0x0Du /**< a vector reboot */
#define RESET_BOOT_TYPE_CHAINED_FLAG 0x80u /**< a bootloader chained in */

/**
 * @brief SCRATCH2 and SCRATCH3 after picotool's `reboot` from BOOTSEL into
 *        the application: type NORMAL, p0 = BOOT_PARTITION_NONE (-1), p1 = 0.
 *
 * picotool 2.x sends this after every command run with `-f`, and for
 * `picotool reboot`.  This firmware calls reboot() with type FLASH_UPDATE
 * only.
 */
#define RESET_PICOTOOL_P0 0xFFFFFFFFu
/** @brief SCRATCH3 after that reboot: p1 = 0. */
#define RESET_PICOTOOL_P1 0x00000000u

/**
 * @brief The raw values, as read at boot.
 */
typedef struct {
    uint32_t chip_reset;       /**< POWMAN CHIP_RESET */
    uint32_t wd_reason;        /**< WATCHDOG REASON */
    uint32_t wd_scratch2;      /**< WATCHDOG SCRATCH2: last reboot() p0 */
    uint32_t wd_scratch3;      /**< WATCHDOG SCRATCH3: last reboot() p1 */
    bool     boot_ok;          /**< rom_get_boot_info() returned true */
    uint32_t boot_word;        /**< boot_info_t.boot_word, 0xttppbbdd */
    uint32_t reboot_params[2]; /**< boot_info_t.reboot_params */
} reset_regs_t;

/**
 * @brief The causes reset_cause_decode() names.
 */
typedef enum {
    RESET_CAUSE_UNKNOWN = 0,   /**< no cause recorded */
    RESET_CAUSE_POR,           /**< power-on reset */
    RESET_CAUSE_BOR,           /**< brown-out detector */
    RESET_CAUSE_RUN_PIN,       /**< RUN pin held low */
    RESET_CAUSE_GLITCH,        /**< supply glitch detector */
    RESET_CAUSE_DEBUGGER,      /**< Arm debugger, rescue or hazard debugger */
    RESET_CAUSE_POWER_DOWN,    /**< switched-core power-down */
    RESET_CAUSE_FLASH_UPDATE,  /**< a reboot() into a FLASH_UPDATE boot */
    RESET_CAUSE_RAM_IMAGE,     /**< a reboot() into a RAM image boot */
    RESET_CAUSE_BOOTSEL_REBOOT, /**< back from BOOTSEL by picotool */
    RESET_CAUSE_SW_RESET,      /**< watchdog triggered at once */
    RESET_CAUSE_WATCHDOG,      /**< watchdog counted down, NORMAL boot */
    RESET_CAUSE_COUNT
} reset_cause_t;

/**
 * @brief Names the cause of the last start.
 *
 * First match wins:
 *
 * 1. The boot type: FLASH_UPDATE (this firmware's update over CAN, a UF2
 *    copied to the BOOTSEL drive, `picotool load -x`), RAM_IMAGE, BOOTSEL
 *    (the boot ROM reports this type only while it stays in BOOTSEL;
 *    decoded for completeness).
 * 2. REASON TIMER or FORCE: a watchdog system reset, and CHIP_RESET is the
 *    record of an older reset.  SCRATCH2/SCRATCH3 at RESET_PICOTOOL_P0/P1
 *    with a NORMAL boot: BOOTSEL_REBOOT.  The 1200 baud request and
 *    picotool's `-f` enter BOOTSEL through reboot() with p0 = interface
 *    flags and p1 = the activity LED pin (0xFFFFFFFF for none); the reboot
 *    out of BOOTSEL overwrites both.  The firmware zeroes both registers
 *    after it reads them.  Else FORCE: SW_RESET (the SDK's
 *    watchdog_reboot() with no delay).  Else WATCHDOG: the boot ROM's 16.7s
 *    TBYB (try before you buy) timer after an update that was not bought, a
 *    reboot() with type NORMAL and other parameters, or the SDK's
 *    watchdog_reboot() with a delay.
 * 3. REASON 0, so CHIP_RESET holds this start's chip-level reset:
 *    HAD_POR, HAD_BOR, HAD_RUN_LOW, HAD_GLITCH_DETECT, in that order; then
 *    HAD_DP_RESET_REQ, HAD_RESCUE or HAD_HZD_SYS_RESET_REQ as DEBUGGER;
 *    HAD_SWCORE_PD as POWER_DOWN; a HAD_WATCHDOG_* bit as WATCHDOG.
 * 4. Nothing set: UNKNOWN.
 *
 * A debugger's warm reset of a core clears REASON (datasheet, WATCHDOG
 * REASON) without a chip-level reset; the start after one reads as the
 * chip-level reset before it.
 *
 * @param r  The raw values; NULL reads as UNKNOWN.
 * @return The cause.
 */
reset_cause_t reset_cause_decode(const reset_regs_t *r);

/**
 * @brief The fixed word for a cause: POR, BOR, RUN_PIN, GLITCH, DEBUGGER,
 *        POWER_DOWN, FLASH_UPDATE, RAM_IMAGE, BOOTSEL_REBOOT, SW_RESET,
 *        WATCHDOG or UNKNOWN.
 *
 * @param c  The cause; a value out of range reads as UNKNOWN.
 * @return The word, a static string of at most 14 characters.
 */
const char *reset_cause_word(reset_cause_t c);

/**
 * @brief The boot type in the boot word, without the chained flag.
 *
 * @param r  The raw values.
 * @return The boot type, or -1 when rom_get_boot_info() failed.
 */
int reset_cause_boot_type(const reset_regs_t *r);

/**
 * @brief The text of the start-up line and the 3s line, every raw value in
 *        hex:
 *        `POR (chip_reset 0x00010000 reason 0x0 boot 0x00000000
 *        params 0x00000000 0x00000000 scratch 0x00000000 0x00000000)`,
 *        with `boot none` in place of the boot word and the params when
 *        rom_get_boot_info() failed.
 *
 * @param r    The raw values.
 * @param out  Receives the text, NUL-terminated and cut to fit.
 * @param n    Size of @p out in bytes; RESET_CAUSE_FULL_MAX holds it.
 * @return The length the whole text needs, without the NUL, as snprintf().
 */
size_t reset_cause_full(const reset_regs_t *r, char *out, size_t n);

/** @brief Bytes that hold the longest full form with its NUL. */
#define RESET_CAUSE_FULL_MAX 144u

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_RESET_CAUSE_H */
