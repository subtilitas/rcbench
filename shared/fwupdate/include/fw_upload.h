/*
 * Receiving a panel image into memory, as a source hands it over.
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

typedef enum {
    FW_UP_IDLE = 0,    /**< nothing received or waiting                 */
    FW_UP_RECEIVING,   /**< bytes are arriving                          */
    FW_UP_STAGED,      /**< a whole image passed fw_image_check()        */
    FW_UP_FAILED,      /**< the last one was refused: see @c err        */
} fw_up_state_t;

typedef struct {
    uint8_t        *buf;
    uint32_t        cap;
    uint32_t        slot;
    uint32_t        bytes;    /**< announced                    */
    uint32_t        done;     /**< received                     */
    fw_up_state_t   state;
    fw_err_t        err;
    fw_image_info_t info;     /**< of a staged image            */
} fw_upload_t;

/** Into @p buf of @p cap bytes, for a slot of @p slot bytes. */
void fw_upload_init(fw_upload_t *u, uint8_t *buf, uint32_t cap,
                    uint32_t slot);

/**
 * A source announces @p bytes.  FW_OK and RECEIVING, or why not, with the
 * state unchanged: FW_ERR_BUSY while RECEIVING or STAGED, FW_ERR_EMPTY for
 * 0, FW_ERR_TOO_BIG over the slot, FW_ERR_MEMORY for a buffer too small.
 */
fw_err_t fw_upload_begin(fw_upload_t *u, uint32_t bytes);

/**
 * @p len more bytes.  FW_ERR_LENGTH, and FAILED, when they run past the
 * announced size.
 */
fw_err_t fw_upload_add(fw_upload_t *u, const uint8_t *data, uint32_t len);

/**
 * The source is done: @p whole false when it broke off.  FW_OK and STAGED,
 * or FAILED with the reason: FW_ERR_READ for a source that broke off or
 * sent less than announced, anything fw_image_check() refuses.
 */
fw_err_t fw_upload_end(fw_upload_t *u, bool whole);

/** Drop what is received or staged: IDLE. */
void fw_upload_discard(fw_upload_t *u);

/** Received of announced, in percent, 0 to 100. */
unsigned fw_upload_percent(const fw_upload_t *u);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_UPLOAD_H */
