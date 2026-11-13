/**
 * @file
 * @brief The firmware update on this board.
 *
 * See update_hw.h and the update
 * section of link_pages.h.  The rules -- which slot runs, which one is
 * written, the trial, where a write lands, when a BUY counts -- are
 * shared/fwupdate/fw_slots.c, host-tested; this file makes the boot ROM's
 * and the flash's calls.
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

#include "fw_slots.h"
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
/* fw_slots.h reads the boot ROM's words with its own copies of these. */
_Static_assert(FW_SLOTS_PT_INFO_HEADER
                   == (PT_INFO_PARTITION_LOCATION_AND_FLAGS
                       | PT_INFO_SINGLE_PARTITION),
               "fw_slots.h: the location query's header differs");
_Static_assert(FW_SLOTS_LOC_FIRST_SECTOR_BITS
                   == PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS
               && FW_SLOTS_LOC_FIRST_SECTOR_LSB
                      == PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB
               && FW_SLOTS_LOC_LAST_SECTOR_BITS
                      == PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS
               && FW_SLOTS_LOC_LAST_SECTOR_LSB
                      == PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB,
               "fw_slots.h: the partition location fields differ");
_Static_assert(FW_SLOTS_BUY_PENDING == BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING,
               "fw_slots.h: the BUY_PENDING flag differs");

/** @brief The update's state: the pages, the session, the owed erase. */
static link_update_dev_t s_upd;
/** @brief The flash and boot ROM callbacks s_upd calls. */
static link_update_io_t  s_io;
/** @brief Returns true while flash may not be written; NULL for never. */
static bool            (*s_busy)(void);
/** @brief Sends the queued CAN frames before the restart; NULL for none. */
static void            (*s_drain)(void);
/** @brief The slots of this boot, from fw_slots_choose(). */
static fw_slots_t        s_slots = { .slot = -1 };

/**
 * @brief The boot ROM's BUY needs 4KiB to rewrite the sector holding the flag.
 */
static uint32_t s_buy_work[FLASH_SECTOR_SIZE / sizeof(uint32_t)];
/** @brief The last BUY's answer, written inside the flash window. */
static fw_slots_buy_t s_buy;

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
    uint32_t addr;
    if (!fw_slots_target(&s_slots, off, &addr)) {
        return false;
    }
    return out_store_flash_erase(addr);
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
    uint32_t addr;
    if (!fw_slots_target(&s_slots, off, &addr)) {
        return false;
    }
    return out_store_flash_program(addr, data, FLASH_SECTOR_SIZE);
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
    fw_slots_buy_answer(&s_buy,
                        buy((uint8_t *)s_buy_work, sizeof(s_buy_work)));
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
    fw_slots_buy_begin(&s_buy);
    const bool ran = out_store_flash_call(buy_in_window, NULL);
    return fw_slots_buy_taken(&s_buy, ran);
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
    /* The ACTIVATE's ACK is queued (can_txq.h): out first. */
    if (s_drain != NULL) {
        s_drain();
    }
    /* A flash update boot of the slot just written: the boot ROM prefers
     * the partition that starts at p0, and starts its image on trial. */
    (void)rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 10u,
                     fw_slots_update_base(&s_slots, XIP_BASE), 0u);
    for (;;) {
        tight_loop_contents();
    }
}

/**
 * @brief Partition @p n of the table the boot ROM holds.
 *
 * @param n  Partition number, 0 or 1.
 * @return The partition; found is false without one.
 */
static fw_slots_part_t partition_at(unsigned n)
{
    uint32_t buf[FW_SLOTS_PT_INFO_WORDS];
    const int rc = rom_get_partition_table_info(
        buf, FW_SLOTS_PT_INFO_WORDS,
        PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION
            | (n << 24));
    return fw_slots_partition(rc, buf, FLASH_SECTOR_SIZE);
}

void update_hw_init(bool (*busy)(void), void (*drain)(void))
{
    s_busy = busy;
    s_drain = drain;
    s_io = (link_update_io_t){ io_erase, io_program, io_gate, io_buy,
                               io_restart, NULL };

    boot_info_t info;
    memset(&info, 0, sizeof(info));
    fw_slots_boot_t boot;
    boot.have_info = rom_get_boot_info(&info);
    boot.partition = info.partition;
    boot.tbyb = info.tbyb_and_update_info;
    boot.p0 = partition_at(0u);
    boot.p1 = partition_at(1u);
    const fw_slots_layout_t layout = {
        IOMCU_SLOT_A_OFFSET, IOMCU_SLOT_B_OFFSET, IOMCU_SLOT_BYTES,
    };
    fw_slots_choose(&s_slots, &layout, &boot);
    link_update_dev_init(&s_upd, &s_io,
                         (const uint8_t *)(FLASH_READ_BASE
                                           + s_slots.running_off),
                         (const uint8_t *)(FLASH_READ_BASE
                                           + s_slots.target_off),
                         IOMCU_SLOT_BYTES, s_slots.link_slot);
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
    return s_slots.slot;
}

bool update_hw_on_trial(void)
{
    return (s_upd.slot & LINK_UPD_SLOT_TRIAL) != 0u;
}
