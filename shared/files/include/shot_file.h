/**
 * @file
 * @brief A screenshot on the card: the folder, the file's name, and the PNG
 *        (Portable Network Graphics) written from a picture in memory.
 *
 * The folder is SHOT_DIR in the card's root, made when missing
 * (card_make_dirs()).  It is no FIXED folder: nothing of the bench reads
 * it, so FILES and the Wi-Fi page rename and delete it as any other.
 *
 * A file is written as "<name>.NEW", then renamed: a card pulled or a write
 * stopped leaves no file of the real name.  A power cut while writing leaves
 * the ".NEW"; the next save of that name deletes it.  A name that is taken
 * is refused (FM_ERR_EXISTS): nothing is overwritten.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework):
 * the card is a table of functions (fm_io.h), so the host suite runs this
 * on a card in memory.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_io.h"
#include "png_enc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The folder in the card's root. */
#define SHOT_DIR "screenshots"
/** @brief What every file ends in; the keyboard does not type it. */
#define SHOT_EXT ".PNG"
/** @brief The bytes of SHOT_EXT. */
#define SHOT_EXT_LEN 4u
/** @brief The most bytes of a name in front of SHOT_EXT. */
#define SHOT_BASE_MAX (FM_NAME_MAX - SHOT_EXT_LEN)
/** @brief What a default name starts with: SHOT0001, SHOT0002 ... */
#define SHOT_PREFIX "SHOT"
/** @brief The highest number a default name takes. */
#define SHOT_NUMBER_MAX 9999u
/** @brief The answer for no memory: the state or the chunk did not fit. */
#define SHOT_ERR_MEMORY FM_ERR_TOO_BIG

/** @brief One save: what to write, how, and how to stop it. */
typedef struct {
    const fm_io_t *io;          /**< the card                             */
    const uint16_t *pixels;     /**< the picture, RGB565, top row first   */
    int      w;                 /**< its width, pixels                    */
    int      h;                 /**< its height, rows                     */
    const char *base;           /**< the name without SHOT_EXT            */
    png_enc_t *enc;             /**< the encoder's state                  */
    uint8_t *chunk;             /**< the write buffer                     */
    size_t   chunk_size;        /**< its bytes, FM_CHUNK on the panel     */
    /** @brief Whether to stop at the next row; may be NULL. */
    bool (*cancelled)(void *ctx);
    /** @brief The rows done, after each; may be NULL. */
    void (*progress)(int done, int total, void *ctx);
    void *ctx;                  /**< passed to both                       */
} shot_save_t;

/**
 * @brief Whether a name typed may be saved: fm_name_check() of the name with
 *        SHOT_EXT behind it.
 * @param base  the name without SHOT_EXT
 * @return FM_NAME_OK or why not (fm_name_t, as an int to keep fm_name.h out)
 */
int shot_name_check(const char *base);

/**
 * @brief The number the next default name takes: one above the highest
 *        SHOT<digits>.PNG in the folder, 1 for a folder missing or holding
 *        none, SHOT_NUMBER_MAX at most.
 * @param io      the card
 * @param number  takes the number; 1 on any failure
 * @return FM_OK, or why the folder could not be read; no folder is FM_OK
 */
fm_err_t shot_file_next(const fm_io_t *io, unsigned *number);

/**
 * @brief Write one screenshot.  Blocks for the whole write: the card's task
 *        calls it.
 * @param s     the save
 * @param path  takes the file's path from the card's root; may be NULL
 * @param n     bytes at @p path
 * @return FM_OK; FM_ERR_NO_CARD, FM_ERR_CARD_GONE, FM_ERR_EXISTS,
 *         FM_ERR_FULL, FM_ERR_DENIED, FM_ERR_NAME, FM_ERR_IO, or
 *         FM_ERR_CANCELLED; no file of the name is left on any but FM_OK
 */
fm_err_t shot_file_save(const shot_save_t *s, char *path, size_t n);

#ifdef __cplusplus
}
#endif
