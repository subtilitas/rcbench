/**
 * @file
 * @brief The IOmcu's firmware update over the link: link_update_dev on this
 * board's flash and boot ROM.
 *
 * The flash holds the partition table of partitions.json: slots A and B of
 * 2036 KiB each, and the store.  The image runs from one slot; the update
 * writes the other through the store's flash windows (out_store.h), restarts
 * into it as a flash update boot, and the boot ROM keeps it once the panel
 * writes BUY.  Without a partition table -- an image loaded before the table
 * was installed -- UPDATE_CTL reports no slots and every command that writes
 * is refused.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_UPDATE_HW_H
#define RCBENCH_UPDATE_HW_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Read the boot ROM's word on this boot -- which slot runs, whether
 * it is on trial -- and check the partition table against the layout this
 * build has.
 *
 * @p busy says whether flash may not be written now: the bank driving, the
 * supply on, a servo programming session.
 *
 * @param busy  Returns true while flash may not be written now.
 */
void update_hw_init(bool (*busy)(void));

/**
 * @brief The two pages' handlers, for the link_page_t table.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the register values.
 */
void    update_hw_ctl_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the UPDATE_CTL page, for the link_page_t table.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t update_hw_ctl_write(void *ctx, uint8_t off, uint8_t n,
                            const uint16_t *in);
/**
 * @brief Reads the UPDATE_DATA page, for the link_page_t table.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the register values.
 */
void    update_hw_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the UPDATE_DATA page, for the link_page_t table.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t update_hw_data_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in);

/**
 * @brief Once per pass: a slice of a hash, the restart after ACTIVATE, an
 * owed erase of the other slot's first sector, the end of a session that has
 * had no update request for 5 s.
 *
 * @param now_ms  The pass's clock in ms.
 */
void update_hw_poll(uint32_t now_ms);

/**
 * @brief True while ARM is refused: an update session or an image on trial.
 *
 * @return True when ARM is refused.
 */
bool update_hw_holds_arm(void);

/**
 * @brief Whether a CONTROL write is refused for the update
 * (link_update_dev.h).
 *
 * @param off  First register of the write.
 * @param n  Number of registers in the write.
 * @param in  The register values of the write.
 * @return True when the write is refused.
 */
bool update_hw_refuses_arm(uint8_t off, uint8_t n, const uint16_t *in);

/**
 * @brief For the console: the slot running, 0 or 1, or -1 without a table.
 *
 * @return The slot running, 0 or 1, or -1 without a partition table.
 */
int update_hw_slot(void);

/**
 * @brief For the console: whether the running image waits for its BUY.
 *
 * @return True when the running image is on trial.
 */
bool update_hw_on_trial(void);

#endif /* RCBENCH_UPDATE_HW_H */
