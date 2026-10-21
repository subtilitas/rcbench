/**
 * @file
 * @brief The firmware update on this board.
 *
 * See update_hw.h and the update
 * section of link_pages.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "update_hw.h"

#include <stddef.h>
#include <string.h>

#include "pico/stdlib.h"

#include "boot/bootrom_constants.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "hardware/flash.h"
#include "pico/bootrom.h"

#include "link_update_dev.h"
#include "out_store.h"

/**
 * @brief Reads of both slots by their storage address: not translated to the
 * running slot, and not cached, so a sector just written reads back as the
 * flash holds it.
 */
#define FLASH_READ_BASE XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE

_Static_assert(IOMCU_SLOT_BYTES % FLASH_SECTOR_SIZE == 0u,
               "partitions.json: a slot is not whole sectors");
_Static_assert(LINK_UPD_SECTOR_BYTES == FLASH_SECTOR_SIZE,
               "the update's sector is not the flash's");

static link_update_dev_t s_upd;
static link_update_io_t  s_io;
static bool            (*s_busy)(void);
static uint32_t          s_target_off;   /* the slot written, from flash 0 */
static uint32_t          s_running_off;
static int               s_slot = -1;

/**
 * @brief The boot ROM's BUY needs 4 KiB to rewrite the sector holding the flag.
 */
static uint32_t s_buy_work[FLASH_SECTOR_SIZE / sizeof(uint32_t)];
static int      s_buy_rc;

/**
 * @brief Erases a sector of the slot being written.
 *
 * @param ctx  Unused.
 * @param off  Offset within the slot, in bytes. An offset of IOMCU_SLOT_BYTES
 *             or more is refused.
 * @return True when the erase ran.
 */
static bool io_erase(void *ctx, uint32_t off)
{
    (void)ctx;
    if (off >= IOMCU_SLOT_BYTES) {
        return false;
    }
    return out_store_flash_erase(s_target_off + off);
}

/**
 * @brief Programs one sector of the slot being written.
 *
 * @param ctx  Unused.
 * @param off  Offset within the slot, in bytes. An offset of IOMCU_SLOT_BYTES
 *             or more is refused.
 * @param data  The sector contents.
 * @return True when the program ran.
 */
static bool io_program(void *ctx, uint32_t off, const uint8_t *data)
{
    (void)ctx;
    if (off >= IOMCU_SLOT_BYTES) {
        return false;
    }
    return out_store_flash_program(s_target_off + off, data,
                                   FLASH_SECTOR_SIZE);
}

/**
 * @brief Tells whether a flash write may run now.
 *
 * @param ctx  Unused.
 * @return True when a flash window is open and the busy check allows it.
 */
static bool io_gate(void *ctx)
{
    (void)ctx;
    return out_store_window_open() && (s_busy == NULL || !s_busy());
}

/**
 * @brief Inside a flash window: the boot ROM clears the TBYB (try before you
 * buy) flag in flash, and for a version downgrade erases the other slot's
 * first sector.  link_update_dev erases that sector after every BUY, and
 * owes the erase until it holds.
 *
 * @param arg  Unused.
 */
static void buy_in_window(void *arg)
{
    (void)arg;
    rom_explicit_buy_fn buy =
        (rom_explicit_buy_fn)rom_func_lookup_inline(ROM_FUNC_EXPLICIT_BUY);
    s_buy_rc = buy((uint8_t *)s_buy_work, sizeof(s_buy_work));
}

/**
 * @brief Runs the boot ROM's BUY inside a flash window.
 *
 * @param ctx  Unused.
 * @return True when the window opened and BUY returned 0.
 */
static bool io_buy(void *ctx)
{
    (void)ctx;
    s_buy_rc = -1;
    return out_store_flash_call(buy_in_window, NULL) && s_buy_rc == 0;
}

/**
 * @brief Restarts into the slot just written, as a flash update boot. This
 * function does not return.
 *
 * @param ctx  Unused.
 */
static void io_restart(void *ctx)
{
    (void)ctx;
    /* A flash update boot of the slot just written: the boot ROM prefers
     * the partition that starts at p0, and starts its image on trial. */
    (void)rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 10u,
                     XIP_BASE + s_target_off, 0u);
    for (;;) {
        tight_loop_contents();
    }
}

/**
 * @brief Where partition @p n of the table the boot ROM holds starts and how
 * many bytes it spans; false without one.
 *
 * @param n  Partition number, 0 or 1.
 * @param start  Receives the start offset in bytes from flash 0.
 * @param bytes  Receives the length of the partition, in bytes.
 * @return True when the boot ROM holds the partition.
 */
static bool partition_at(unsigned n, uint32_t *start, uint32_t *bytes)
{
    uint32_t buf[3];
    const int rc = rom_get_partition_table_info(
        buf, 3u, PT_INFO_PARTITION_LOCATION_AND_FLAGS
                 | PT_INFO_SINGLE_PARTITION | (n << 24));
    if (rc != 3
        || buf[0] != (PT_INFO_PARTITION_LOCATION_AND_FLAGS
                      | PT_INFO_SINGLE_PARTITION)) {
        return false;
    }
    const uint32_t first = (buf[1] & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                           >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    const uint32_t last = (buf[1] & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)
                          >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    if (last < first) {
        return false;
    }
    *start = first * FLASH_SECTOR_SIZE;
    *bytes = (last - first + 1u) * FLASH_SECTOR_SIZE;
    return true;
}

void update_hw_init(bool (*busy)(void))
{
    s_busy = busy;
    s_io = (link_update_io_t){ io_erase, io_program, io_gate, io_buy,
                               io_restart, NULL };

    uint16_t slot = LINK_UPD_SLOT_NONE;
    boot_info_t info;
    memset(&info, 0, sizeof(info));
    const bool have_info = rom_get_boot_info(&info);
    uint32_t a = 0u, a_bytes = 0u, b = 0u, b_bytes = 0u;
    /* The table this build was made for, or none: an image cannot tell a
     * table written for another layout from damage, and writes into neither. */
    const bool table = partition_at(0u, &a, &a_bytes)
                       && partition_at(1u, &b, &b_bytes)
                       && a == IOMCU_SLOT_A_OFFSET && b == IOMCU_SLOT_B_OFFSET
                       && a_bytes == IOMCU_SLOT_BYTES
                       && b_bytes == IOMCU_SLOT_BYTES;
    if (have_info && table && (info.partition == 0 || info.partition == 1)) {
        s_slot = info.partition;
        slot = (uint16_t)info.partition;
        if ((info.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING)
            != 0u) {
            slot |= LINK_UPD_SLOT_TRIAL;
        }
    }
    s_running_off = (s_slot == 1) ? IOMCU_SLOT_B_OFFSET : IOMCU_SLOT_A_OFFSET;
    s_target_off  = (s_slot == 1) ? IOMCU_SLOT_A_OFFSET : IOMCU_SLOT_B_OFFSET;
    link_update_dev_init(&s_upd, &s_io,
                         (const uint8_t *)(FLASH_READ_BASE + s_running_off),
                         (const uint8_t *)(FLASH_READ_BASE + s_target_off),
                         IOMCU_SLOT_BYTES, slot);
}

void update_hw_ctl_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    link_update_dev_read_ctl(&s_upd, off, n, out);
}

uint8_t update_hw_ctl_write(void *ctx, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    (void)ctx;
    return link_update_dev_write_ctl(&s_upd, off, n, in);
}

void update_hw_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    link_update_dev_read_data(&s_upd, off, n, out);
}

uint8_t update_hw_data_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    (void)ctx;
    return link_update_dev_write_data(&s_upd, off, n, in);
}

void update_hw_poll(uint32_t now_ms)
{
    link_update_dev_poll(&s_upd, now_ms);
}

bool update_hw_holds_arm(void)
{
    return link_update_dev_holds_arm(&s_upd);
}

bool update_hw_refuses_arm(uint8_t off, uint8_t n, const uint16_t *in)
{
    return link_update_dev_refuses_arm(&s_upd, off, n, in);
}

int update_hw_slot(void)
{
    return s_slot;
}

bool update_hw_on_trial(void)
{
    return (s_upd.slot & LINK_UPD_SLOT_TRIAL) != 0u;
}
