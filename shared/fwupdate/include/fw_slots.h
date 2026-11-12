/**
 * @file fw_slots.h
 * @brief The IOmcu's two update slots on the RP2350 boot ROM: which slot
 *        runs, which one an update writes, whether the running image is on
 *        trial, where a write lands, and whether a BUY was taken.
 *
 * The flash holds two slots, A and B, from the partition table of
 * firmware/iomcu/partitions.json.  The image runs from one; an update
 * writes the other and restarts into it as a flash update boot, which the
 * boot ROM starts on trial (TBYB, try before you buy) until a BUY keeps
 * it.  This module holds the rules of that; firmware/iomcu/src/update_hw.c
 * makes the boot ROM's calls and hands their answers in, so the host
 * suite runs every rule without the chip.
 *
 * The rules:
 * - a partition is read from the boot ROM's answer to a one-partition
 *   location query: three words, the first naming exactly that query;
 * - the slots are used only when both partitions read and match the layout
 *   the image was built for, and the boot ROM names partition 0 or 1 as the
 *   one it started.  Otherwise no slot: an image cannot tell a table
 *   written for another layout from damage, and writes into neither;
 * - the slot written is the one not running; without a slot it is slot B,
 *   and link_update_dev refuses every write anyway;
 * - an image with the boot ROM's BUY_PENDING flag set is on trial;
 * - a write at an offset of a slot's size or more is refused;
 * - a BUY counts only when its flash window ran and the boot ROM answered
 *   0.  The answer is cleared before each try, so a window that does not
 *   run cannot report an earlier BUY's 0.
 *
 * Pure C, no pico-sdk.  The boot ROM's constants this module reads are its
 * own FW_SLOTS_* copies; update_hw.c holds each equal to the pico-sdk's
 * with a _Static_assert.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_SLOTS_H
#define RCBENCH_FW_SLOTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name The boot ROM's words this module reads
 * Copies of the pico-sdk's constants (bootrom_constants.h, picobin.h).
 * @{
 */
/** @brief Words of a one-partition location query: header, location, flags. */
#define FW_SLOTS_PT_INFO_WORDS          3
/**
 * @brief The header word of that answer: PT_INFO_PARTITION_LOCATION_AND_FLAGS
 *        (0x0010) with PT_INFO_SINGLE_PARTITION (0x8000).
 */
#define FW_SLOTS_PT_INFO_HEADER         0x8010u
/** @brief PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS. */
#define FW_SLOTS_LOC_FIRST_SECTOR_BITS  0x00001FFFu
/** @brief PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB. */
#define FW_SLOTS_LOC_FIRST_SECTOR_LSB   0u
/** @brief PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS. */
#define FW_SLOTS_LOC_LAST_SECTOR_BITS   0x03FFE000u
/** @brief PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB. */
#define FW_SLOTS_LOC_LAST_SECTOR_LSB    13u
/** @brief BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING: the image waits for BUY. */
#define FW_SLOTS_BUY_PENDING            0x01u
/** @} */

/**
 * @brief The BUY answer before the boot ROM gave one: not 0, so it never
 *        reads as a BUY taken.
 */
#define FW_SLOTS_BUY_NOT_RUN            (-1)

/** @brief The layout the image was built for (partitions.json). */
typedef struct {
    uint32_t a_off;        /**< slot A's start, bytes from flash 0          */
    uint32_t b_off;        /**< slot B's start, bytes from flash 0          */
    uint32_t slot_bytes;   /**< bytes in one slot, A and B alike            */
    uint32_t sector_bytes; /**< bytes in one flash sector: 4096             */
} fw_slots_layout_t;

/** @brief One partition as the boot ROM's table holds it. */
typedef struct {
    bool     found; /**< the answer named the partition                     */
    uint32_t start; /**< first byte, from flash 0                           */
    uint32_t bytes; /**< length in bytes                                    */
} fw_slots_part_t;

/** @brief What the boot ROM says about this boot. */
typedef struct {
    bool            have_info; /**< the boot info query answered            */
    int             partition; /**< the partition started; -1 for none      */
    uint8_t         tbyb;      /**< its TBYB and update flags               */
    fw_slots_part_t p0;        /**< partition 0 of the table                */
    fw_slots_part_t p1;        /**< partition 1 of the table                */
} fw_slots_boot_t;

/** @brief The slots this boot runs with. */
typedef struct {
    int      slot;        /**< slot running: 0 (A), 1 (B), or -1 for none   */
    uint16_t link_slot;   /**< LINK_UPD_SLOT as link_update_dev_init() takes
                           *   it: the slot, LINK_UPD_SLOT_NONE, and
                           *   LINK_UPD_SLOT_TRIAL                          */
    uint32_t running_off; /**< the running slot's start, from flash 0       */
    uint32_t target_off;  /**< the slot an update writes, from flash 0      */
    uint32_t slot_bytes;  /**< bytes in one slot                            */
} fw_slots_t;

/** @brief One BUY: the boot ROM's answer to the last try. */
typedef struct {
    int rc; /**< the answer, or FW_SLOTS_BUY_NOT_RUN                        */
} fw_slots_buy_t;

/**
 * @brief Read one partition out of the boot ROM's answer to a one-partition
 *        location query.
 * @param rc           Words the query returned, or a negative error.
 * @param words        The answer, FW_SLOTS_PT_INFO_WORDS words; NULL reads
 *                     as no answer.
 * @param sector_bytes Bytes in one flash sector.
 * @return The partition; found is false unless @p rc is
 *         FW_SLOTS_PT_INFO_WORDS, the header is FW_SLOTS_PT_INFO_HEADER and
 *         the last sector is not before the first.
 */
fw_slots_part_t fw_slots_partition(int rc, const uint32_t *words,
                                   uint32_t sector_bytes);

/**
 * @brief Decide the slots of this boot.
 * @param s    Receives the slots.
 * @param l    The layout the image was built for.
 * @param boot What the boot ROM says.
 */
void fw_slots_choose(fw_slots_t *s, const fw_slots_layout_t *l,
                     const fw_slots_boot_t *boot);

/**
 * @brief Where a write @p off bytes into the slot being written lands.
 * @param s    The slots.
 * @param off  Offset within the slot, in bytes.
 * @param addr Receives the offset from flash 0.
 * @return False, with @p addr untouched, when @p off is the slot's size or
 *         more.
 */
bool fw_slots_target(const fw_slots_t *s, uint32_t off, uint32_t *addr);

/**
 * @brief The address a flash update boot names: the start of the slot
 *        just written, in the XIP (execute in place) window.
 * @param s        The slots.
 * @param xip_base Start of the XIP window.
 * @return @p xip_base plus the written slot's offset.
 */
uint32_t fw_slots_update_base(const fw_slots_t *s, uint32_t xip_base);

/**
 * @brief Start a BUY: forget the last answer.
 * @param b The BUY.
 */
void fw_slots_buy_begin(fw_slots_buy_t *b);

/**
 * @brief Record the boot ROM's answer, from inside the flash window.
 * @param b  The BUY.
 * @param rc The boot ROM's answer; 0 is success.
 */
void fw_slots_buy_answer(fw_slots_buy_t *b, int rc);

/**
 * @brief Whether the BUY was taken.
 * @param b          The BUY.
 * @param window_ran Whether the flash window opened and ran the call.
 * @return True when the window ran and the boot ROM answered 0.
 */
bool fw_slots_buy_taken(const fw_slots_buy_t *b, bool window_ran);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_SLOTS_H */
