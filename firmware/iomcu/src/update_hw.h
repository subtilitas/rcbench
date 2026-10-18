/*
 * The IOmcu's firmware update over the link: link_update_dev on this
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
 * Read the boot ROM's word on this boot -- which slot runs, whether it is on
 * trial -- and check the partition table against the layout this build has.
 * @p busy says whether flash may not be written now: the bank driving, the
 * supply on, a servo programming session.
 */
void update_hw_init(bool (*busy)(void));

/* The two pages' handlers, for the link_page_t table. */
void    update_hw_ctl_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t update_hw_ctl_write(void *ctx, uint8_t off, uint8_t n,
                            const uint16_t *in);
void    update_hw_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t update_hw_data_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in);

/** Once per pass: a slice of a hash, the restart after ACTIVATE, an owed
 *  erase of the other slot's first sector, the end of a session that has
 *  had no update request for 5 s. */
void update_hw_poll(uint32_t now_ms);

/** True while ARM is refused: an update session or an image on trial. */
bool update_hw_holds_arm(void);

/** Whether a CONTROL write is refused for the update (link_update_dev.h). */
bool update_hw_refuses_arm(uint8_t off, uint8_t n, const uint16_t *in);

/** For the console: the slot running, 0 or 1, or -1 without a table. */
int update_hw_slot(void);

/** For the console: whether the running image waits for its BUY. */
bool update_hw_on_trial(void);

#endif /* RCBENCH_UPDATE_HW_H */
