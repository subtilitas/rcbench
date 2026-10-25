/**
 * @file fw_image.h
 * @brief The panel's application image, read from memory: what it is, whether it is
 *        whole, and whether it is one this panel should run.
 *
 * An ESP-IDF (Espressif Internet-of-Things Development Framework) app image
 * starts with a 24-byte image header (first byte 0xE9, the chip ID at offset
 * 12, the hash flag at 23), then the first segment's 8-byte header, then the
 * 256-byte application descriptor (esp_app_desc_t) at offset 32: magic word
 * 0xABCD5432, version at 48, project name at 80, the ELF (Executable and
 * Linkable Format) file's SHA-256 at 176.  The segments follow, then padding
 * and a checksum byte to the next 16-byte boundary, then the SHA-256 of every
 * byte before it.
 *
 * Two depths of check.  fw_image_head() reads the first FW_HEAD_BYTES, which
 * is what an image says about itself.  fw_image_check() reads the whole
 * image and is the gate before the first erase: header, chip,
 * descriptor, project name, version, length, size against the slot and the
 * appended SHA-256.  Neither knows where the bytes came from.
 *
 * Pure C, no ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_IMAGE_H
#define RCBENCH_FW_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Image header, first segment header and the application descriptor. */
#define FW_HEAD_BYTES     288u
#define FW_IMAGE_MAGIC    0xE9u
/** @brief esp_chip_id_t of the ESP32-P4: ESP_CHIP_ID_ESP32P4, 0x0012, in
 *         ESP-IDF's esp_app_format.h. */
#define FW_CHIP_ESP32P4   0x0012u
#define FW_DESC_MAGIC     0xABCD5432uL
/** The CMake project name, which the descriptor carries. */
#define FW_PROJECT        "rcbench-panel"
/** esp_app_desc_t.version and .project_name, terminator included. */
#define FW_TEXT_MAX       32u
/** ESP_IMAGE_MAX_SEGMENTS. */
#define FW_MAX_SEGMENTS   16u

/** A release number, MAJOR.MINOR.PATCH. */
typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} fw_version_t;

/** Why an image is refused; FW_OK is none of them. */
typedef enum {
    FW_OK = 0,
    FW_ERR_EMPTY,      /**< 0 bytes                                         */
    FW_ERR_SHORT,      /**< shorter than its own header or segments say     */
    FW_ERR_TOO_BIG,    /**< larger than the slot it would go into           */
    FW_ERR_NOT_IMAGE,  /**< first byte is not 0xE9                          */
    FW_ERR_CHIP,       /**< built for another chip                          */
    FW_ERR_NO_DESC,    /**< no application descriptor at offset 32          */
    FW_ERR_PROJECT,    /**< another project's application                   */
    FW_ERR_VERSION,    /**< version is not MAJOR.MINOR.PATCH                */
    FW_ERR_NO_HASH,    /**< built without the appended SHA-256              */
    FW_ERR_LENGTH,     /**< longer than its segments, or a segment overruns */
    FW_ERR_HASH,       /**< the appended SHA-256 does not match             */
    FW_ERR_READ,       /**< the source broke off before the last byte       */
    FW_ERR_RUNNING,    /**< the image is the build that is running          */
    FW_ERR_MEMORY,     /**< no buffer to stage the image in                 */
    FW_ERR_FLASH,      /**< an erase, a write or the final check failed     */
    FW_ERR_BUSY,       /**< another image is being received or waits        */
    FW_ERR_COUNT
} fw_err_t;

/** What an image says about itself. */
typedef struct {
    uint32_t     bytes;
    fw_version_t version;
    char         version_text[FW_TEXT_MAX];
    uint8_t      elf_sha[FW_SHA256_BYTES];
} fw_image_info_t;

/** The English name of @p e, for the console. */
const char *fw_err_str(fw_err_t e);

/**
 * Parse "MAJOR.MINOR.PATCH" from at most @p n bytes of @p text: digits only,
 * each number 0 to 65535, terminated by '\0' or by the end of the field.
 * Anything else ("4184795", "0.19.0-dirty", "1..2") is false.
 */
bool fw_version_parse(const char *text, size_t n, fw_version_t *out);

/** Negative, zero or positive as @p a is older, the same or newer. */
int fw_version_cmp(fw_version_t a, fw_version_t b);

/**
 * Check the first @p have bytes of an image of @p bytes in all, for a slot
 * of @p slot bytes: size, magic, chip, descriptor, project and version.
 * The length and the hash need the whole image and are not checked.
 * @p have below FW_HEAD_BYTES with @p bytes at or above it reads as the
 * source failing (FW_ERR_READ).  @p out is filled on FW_OK.
 */
fw_err_t fw_image_head(const uint8_t *head, uint32_t have, uint32_t bytes,
                       uint32_t slot, fw_image_info_t *out);

/**
 * Check a whole image of @p bytes at @p img for a slot of @p slot bytes:
 * everything fw_image_head() checks, then that the segments add up to the
 * image's length and that the last 32 bytes are the SHA-256 of all bytes
 * before them.  @p out is filled on FW_OK.
 */
fw_err_t fw_image_check(const uint8_t *img, uint32_t bytes, uint32_t slot,
                        fw_image_info_t *out);

/* ------------------------------------------------------ against the panel */

/** What a checked image is to the panel running now. */
typedef enum {
    FW_OFFER_NEWER = 0,  /**< a higher version                               */
    FW_OFFER_OLDER,      /**< a lower version: allowed, and the question says */
    FW_OFFER_REBUILD,    /**< the same version, another build (ELF SHA-256)   */
    FW_OFFER_INSTALLED,  /**< the build that is running: not offered          */
} fw_offer_t;

fw_offer_t fw_offer(const fw_image_info_t *running,
                    const fw_image_info_t *image);

/** Whether an offer is one INSTALL may act on. */
bool fw_offer_installable(fw_offer_t o);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_IMAGE_H */
