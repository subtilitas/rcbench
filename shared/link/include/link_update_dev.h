/**
 * @file
 * @brief The IOmcu's half of the firmware update: the UPDATE_CTL and
 * UPDATE_DATA pages (link_pages.h) and what they write.
 *
 * Pure C.  The flash, the store's gate, the boot ROM's BUY and the restart
 * are the caller's, handed in as callbacks, and both slots are read through
 * plain pointers, so the host suite runs every path against a slot in RAM.
 *
 * RAM: two 4KiB sector buffers -- the slot's first sector, held until
 * ACTIVATE writes it last, and the sector being filled.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_UPDATE_DEV_H
#define RCBENCH_LINK_UPDATE_DEV_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "link_sha256.h"
#include "link_update_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bytes a link_update_dev_poll() hashes, at most: 1KiB, about 0.4ms of
 * SHA-256 at 150MHz (not measured).
 */
#define LINK_UPD_HASH_STEP 1024u

/**
 * @brief Time without a request to either update page that ends a session:
 * longer than the panel's own 1000ms wait for an answer, so one lost frame
 * costs a retry and not the session, and longer than the panel's
 * LINK_UPH_LOST_MS, so the panel gives up first.
 *
 * Requests to other pages do not count: a panel that polls and sends no update
 * has left it.
 */
#define LINK_UPD_SILENCE_MS 5000u

/**
 * @brief Time between two tries of an owed erase of the other slot's first
 * sector: each one parks the loop for about 19ms.
 */
#define LINK_UPD_SWEEP_MS 1000u

/**
 * @brief Time from the ACTIVATE that wrote the first sector to the restart, so
 * its acknowledgement is on the wire first.
 */
#define LINK_UPD_RESTART_MS 20u

/**
 * @brief The flash, gate, boot ROM and restart callbacks of the update.
 */
typedef struct {
    /**
     * @brief Erase the 4KiB sector @p off bytes into the slot being written.
     */
    bool (*erase)(void *ctx, uint32_t off);
    /** @brief Program the 4KiB sector at @p off from @p data. */
    bool (*program)(void *ctx, uint32_t off, const uint8_t *data);
    /**
     * @brief True while flash may be written: the bank not driving, the
     * supply's output off, no servo programming session, no store save pending.
     */
    bool (*gate)(void *ctx);
    /** @brief Buy the image on trial; true when the boot ROM took it. */
    bool (*buy)(void *ctx);
    /** @brief Restart into the slot written, as a flash update boot. */
    void (*restart)(void *ctx);
    void *ctx;
} link_update_io_t;

/**
 * @brief The state of the IOmcu's side of one update session.
 */
typedef struct {
    const link_update_io_t *io;
    const uint8_t *running;     /**< the running slot's bytes               */
    const uint8_t *target;      /**< the other slot's bytes                 */
    uint32_t       slot_bytes;  /**< bytes in one slot; 0 when there are none */
    uint16_t       slot;        /**< LINK_UPD_SLOT                           */

    uint8_t  state;             /**< a link_upd_state_t                      */
    uint8_t  error;             /**< a link_upd_err_t                        */
    uint16_t regs_cmd[4];       /**< CMD and ARG0..2 as last written         */
    uint32_t size;              /**< the session's image, or bytes hashed    */
    uint16_t next;              /**< the block COMMIT takes next             */
    uint16_t blocks;            /**< blocks in the session's image           */

    uint16_t data[LINK_UPD_DATA_COUNT];

    uint8_t  digest_of;         /**< a link_upd_digest_t                     */
    uint8_t  digest[LINK_SHA256_BYTES];
    uint8_t  expect[LINK_SHA256_BYTES];
    link_image_hash_t hash;

    bool     restart_due;
    bool     restart_timed;
    uint32_t restart_at_ms;

    bool     heard;             /**< an update page took a request since the
                                 *   last poll                              */
    uint32_t heard_ms;          /**< the poll that saw the last one         */
    bool     sweep;             /**< the other slot's first sector is owed
                                 *   its erase (LINK_UPD_SLOT_OTHER)        */
    bool     sweep_tried;
    uint32_t sweep_ms;          /**< the last try                           */

    uint8_t  first[LINK_UPD_SECTOR_BYTES];  /**< the slot's first sector    */
    uint8_t  sector[LINK_UPD_SECTOR_BYTES]; /**< the sector being filled    */
} link_update_dev_t;

/**
 * @brief Start idle.  @p slot is LINK_UPD_SLOT as the boot ROM reports it: the
 * running slot, LINK_UPD_SLOT_NONE without a partition table this build writes,
 * and LINK_UPD_SLOT_TRIAL for an image that waits for its BUY. @p running and
 * @p target may be NULL with LINK_UPD_SLOT_NONE.
 *
 * A bought image that finds the other slot's first sector written owes its
 * erase.
 *
 * @param u The update state to set up.
 * @param io The callbacks; kept, not copied.
 * @param running The running slot's bytes.
 * @param target The other slot's bytes.
 * @param slot_bytes Bytes in one slot, a multiple of the 4KiB sector.
 * @param slot LINK_UPD_SLOT as the boot ROM reports it.
 */
void link_update_dev_init(link_update_dev_t *u, const link_update_io_t *io,
                          const uint8_t *running, const uint8_t *target,
                          uint32_t slot_bytes, uint16_t slot);

/**
 * @brief The two pages' handlers, for the caller's link_page_t table.
 *
 * @param u The update state.
 * @param offset First register, 0 to LINK_UPD_COUNT - 1.
 * @param count Number of registers to read.
 * @param out Receives the registers.
 */
void    link_update_dev_read_ctl(link_update_dev_t *u, uint8_t offset,
                                 uint8_t count, uint16_t *out);
/**
 * @brief Write registers of the UPDATE_CTL page: one command and its arguments.
 *
 * @param u The update state.
 * @param offset First register; must be LINK_UPD_CMD.
 * @param count Number of registers, 1 to 4.
 * @param in The registers written.
 * @return 0 when the command is taken, else the NACK reason.
 */
uint8_t link_update_dev_write_ctl(link_update_dev_t *u, uint8_t offset,
                                  uint8_t count, const uint16_t *in);
/**
 * @brief Read registers of the UPDATE_DATA page.
 *
 * @param u The update state.
 * @param offset First register of the staged block.
 * @param count Number of registers to read.
 * @param out Receives the registers.
 */
void    link_update_dev_read_data(link_update_dev_t *u, uint8_t offset,
                                  uint8_t count, uint16_t *out);
/**
 * @brief Write registers of the UPDATE_DATA page: stage part of a block.
 *
 * @param u The update state.
 * @param offset First register of the block, 0 to LINK_UPD_DATA_COUNT - 1.
 * @param count Number of registers written.
 * @param in The registers written.
 * @return 0 when stored, LINK_NACK_BAD_RANGE when the window does not fit the
 *       block, or when the arguments are NULL.
 */
uint8_t link_update_dev_write_data(link_update_dev_t *u, uint8_t offset,
                                   uint8_t count, const uint16_t *in);

/**
 * @brief The work that does not fit a reply: LINK_UPD_HASH_STEP bytes of a
 * hash, the restart LINK_UPD_RESTART_MS after ACTIVATE, an owed erase of the
 * other slot's first sector, and the end of a session that has had no request
 * to either update page for LINK_UPD_SILENCE_MS.
 *
 * Once per pass of the caller's loop.
 *
 * @param u The update state.
 * @param now_ms The current time, in ms.
 */
void link_update_dev_poll(link_update_dev_t *u, uint32_t now_ms);

/**
 * @brief True while ARM is refused: a session runs or an image waits for BUY.
 *
 * @param u The update state; NULL gives false.
 * @return true while a session runs, an ACTIVATE is pending, or an image waits
 *       for BUY.
 */
bool link_update_dev_holds_arm(const link_update_dev_t *u);

/**
 * @brief Whether a write of @p n CONTROL registers from @p off is refused for
 * the update: it asks for a non-zero ARM while link_update_dev_holds_arm().
 *
 * The caller answers it with LINK_NACK_NOT_ARMED before the page's own rules
 * run, so nothing of the frame is stored.  A disarm is always taken.
 *
 * @param u The update state.
 * @param off First CONTROL register of the write.
 * @param n Number of registers in the write.
 * @param in The registers written.
 * @return true when the write must be refused.
 */
bool link_update_dev_refuses_arm(const link_update_dev_t *u, uint8_t off,
                                 uint8_t n, const uint16_t *in);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_UPDATE_DEV_H */
