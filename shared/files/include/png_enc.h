/**
 * @file
 * @brief A streaming PNG (Portable Network Graphics) encoder for an RGB565
 *        picture: 8-bit RGB, no interlace, one zlib stream of fixed-code
 *        deflate blocks over the IDAT chunks.
 *
 * Project-free: no ESP-IDF (Espressif Internet-of-Things Development
 * Framework), no heap, no file calls.  The caller gives it the state, in
 * any RAM, one row at a time, and a sink that takes the file's bytes in
 * order, so a task can stop between two rows.
 *
 * Each row gets the PNG filter that scores lowest of None, Sub and Up: the
 * sum of the bytes taken as signed magnitudes, the PNG specification's
 * heuristic.  The match search looks back over the current and the previous
 * filtered row only, at most (2 * width * 3 + 1) bytes, through one hash
 * table of PNG_ENC_HASH entries with no chains.
 *
 * Limits: a picture up to PNG_ENC_MAX_W pixels wide.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The widest picture: the panel's 1024 pixels. */
#define PNG_ENC_MAX_W 1024
/** @brief The bytes of a row in the zlib stream: filter byte and RGB. */
#define PNG_ENC_ROW (1 + PNG_ENC_MAX_W * 3)
/** @brief The match table's entries, a power of two. */
#define PNG_ENC_HASH 4096
/** @brief The most bytes of one IDAT chunk's data. */
#define PNG_ENC_IDAT 8192

/**
 * @brief Takes the next bytes of the file.
 * @param data  the bytes
 * @param n     how many
 * @param ctx   the context given to png_enc_start()
 * @return false to stop: the next call answers false as well
 */
typedef bool (*png_enc_sink_t)(const uint8_t *data, size_t n, void *ctx);

/** @brief The encoder's state: 36,928B on a 32-bit processor. */
typedef struct {
    png_enc_sink_t sink;            /**< where the file goes              */
    void    *ctx;                   /**< the sink's context               */
    int      w;                     /**< pixels a row                     */
    int      h;                     /**< rows in the picture              */
    int      rows;                  /**< rows taken so far                */
    bool     ok;                    /**< no sink has refused              */
    uint32_t adler_a;               /**< Adler-32, low sum                */
    uint32_t adler_b;               /**< Adler-32, high sum               */
    uint32_t bits;                  /**< bits waiting for a byte          */
    int      nbits;                 /**< how many                         */
    size_t   idat;                  /**< bytes in @c out                  */
    uint32_t pos;                   /**< stream bytes so far              */
    int32_t  head[PNG_ENC_HASH];    /**< newest stream position per hash,
                                         -1 for none                      */
    uint8_t  raw_prev[PNG_ENC_MAX_W * 3]; /**< the previous row's RGB     */
    uint8_t  raw[PNG_ENC_MAX_W * 3];      /**< this row's RGB             */
    uint8_t  win[2 * PNG_ENC_ROW];  /**< previous and this filtered row   */
    uint8_t  out[PNG_ENC_IDAT];     /**< the IDAT chunk being filled      */
} png_enc_t;

/**
 * @brief Begin a picture: writes the signature, IHDR and the zlib header.
 * @param e     the state
 * @param w     pixels a row, 1 to PNG_ENC_MAX_W
 * @param h     rows, at least 1
 * @param sink  takes the file's bytes
 * @param ctx   passed to @p sink
 * @return false for a size out of range or a refused write
 */
bool png_enc_start(png_enc_t *e, int w, int h, png_enc_sink_t sink, void *ctx);

/**
 * @brief The next row, top first.
 * @param e    the state
 * @param row  @c w pixels, RGB565 in the processor's byte order
 * @return false for a refused write or a row past the last
 */
bool png_enc_row(png_enc_t *e, const uint16_t *row);

/**
 * @brief End the picture: the final deflate block, the Adler-32, the last
 *        IDAT and IEND.
 * @param e  the state, after every row
 * @return false for a refused write or rows missing
 */
bool png_enc_end(png_enc_t *e);

#ifdef __cplusplus
}
#endif
