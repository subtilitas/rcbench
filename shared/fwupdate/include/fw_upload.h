/**
 * @file
 * @brief Receiving a panel image into memory, as a source hands it over.
 *
 * The installer (fw_install.h) takes an image already in memory and knows
 * nothing about where it came from.  This is the step before it: a source
 * announces how many bytes it will send, then hands them over in pieces of
 * any size, into a buffer the caller owns (PSRAM, pseudo-static
 * random-access memory, on the panel).  The Wi-Fi page's upload is one
 * source; nothing here assumes a network or a file system.
 *
 * Refused before a byte is stored: an announced size of 0 or larger than the
 * slot, a buffer too small, and a second image while one is being received
 * or waits for the operator.  A source that breaks off, or sends more than
 * it announced, leaves nothing staged.  A whole image is checked at once
 * (fw_image_check()), so what waits for the operator is an image that can be
 * installed or one that says why not.
 *
 * Not thread-safe: the platform serialises the calls.  No flash is touched.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_UPLOAD_H
#define RCBENCH_FW_UPLOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Where an upload is. */
typedef enum {
    FW_UP_IDLE = 0,    /**< nothing received or waiting                 */
    FW_UP_RECEIVING,   /**< bytes are arriving                          */
    FW_UP_STAGED,      /**< a whole image passed fw_image_check()        */
    FW_UP_FAILED,      /**< the last one was refused: see @c err        */
} fw_up_state_t;

/** @brief An upload into a buffer the caller owns. */
typedef struct {
    uint8_t        *buf;      /**< the caller's buffer          */
    uint32_t        cap;      /**< its bytes                    */
    uint32_t        slot;     /**< the largest image taken      */
    uint32_t        bytes;    /**< announced                    */
    uint32_t        done;     /**< received                     */
    fw_up_state_t   state;    /**< where it is                  */
    fw_err_t        err;      /**< why FAILED                   */
    fw_image_info_t info;     /**< of a staged image            */
    bool            checking; /**< between fw_upload_seal() and
                                   fw_upload_checked(): the buffer is
                                   the check's                  */
} fw_upload_t;

/**
 * @brief An upload into @p buf of @p cap bytes, for a slot of @p slot
 *        bytes: IDLE.
 * @param u     the upload
 * @param buf   the buffer, or NULL
 * @param cap   its bytes
 * @param slot  the slot's bytes
 */
void fw_upload_init(fw_upload_t *u, uint8_t *buf, uint32_t cap,
                    uint32_t slot);

/**
 * @brief A source announces @p bytes.
 * @param u      the upload
 * @param bytes  the image's bytes
 * @return FW_OK and RECEIVING, or why not, with the state unchanged:
 *         FW_ERR_BUSY while RECEIVING, sealed or STAGED, FW_ERR_EMPTY for
 *         0, FW_ERR_TOO_BIG over the slot, FW_ERR_MEMORY for a buffer too
 *         small
 */
fw_err_t fw_upload_begin(fw_upload_t *u, uint32_t bytes);

/**
 * @brief @p len more bytes.
 * @param u     the upload
 * @param data  the bytes
 * @param len   how many
 * @return FW_OK; FW_ERR_LENGTH, and FAILED, when they run past the
 *         announced size; FW_ERR_BUSY when not RECEIVING or sealed
 */
fw_err_t fw_upload_add(fw_upload_t *u, const uint8_t *data, uint32_t len);

/**
 * @brief The source is done; the image is checked at once.
 * @param u      the upload
 * @param whole  false when the source broke off
 * @return FW_OK and STAGED, or FAILED with the reason: FW_ERR_READ for a
 *         source that broke off or sent less than announced, anything
 *         fw_image_check() refuses
 */
fw_err_t fw_upload_end(fw_upload_t *u, bool whole);

/**
 * @brief The first half of fw_upload_end(), for a check run without the
 *        caller's lock: the bytes are whole and wait for fw_image_check()
 *        over @c buf and @c bytes.
 *
 * Between this and fw_upload_checked() the state stays RECEIVING and
 * fw_upload_busy() is true, a fw_upload_discard() included: the buffer is
 * the check's, and no caller frees it.
 *
 * @param u      the upload
 * @param whole  false when the source broke off
 * @return FW_OK and @c checking set; otherwise what fw_upload_end() returns
 *         for the same call, with the same state
 */
fw_err_t fw_upload_seal(fw_upload_t *u, bool whole);

/**
 * @brief The second half: the answer of the check over a sealed upload.
 * @param u     the upload
 * @param e     what fw_image_check() returned
 * @param info  what it filled in
 * @return FW_OK and STAGED, or FAILED with @p e; FW_ERR_BUSY and IDLE when
 *         the upload was discarded during the check, or was not sealed
 */
fw_err_t fw_upload_checked(fw_upload_t *u, fw_err_t e,
                           const fw_image_info_t *info);

/**
 * @brief Whether the buffer is in use: bytes arriving, or a check under
 *        way.  A caller frees the buffer of an upload that is neither this
 *        nor STAGED.
 * @param u  the upload
 * @return true while RECEIVING or sealed
 */
bool fw_upload_busy(const fw_upload_t *u);

/**
 * @brief Drop what is received or staged: IDLE.  During a check, IDLE once
 *        fw_upload_checked() has its answer.
 * @param u  the upload
 */
void fw_upload_discard(fw_upload_t *u);

/**
 * @brief Received of announced.
 * @param u  the upload
 * @return percent, 0 to 100
 */
unsigned fw_upload_percent(const fw_upload_t *u);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_UPLOAD_H */
