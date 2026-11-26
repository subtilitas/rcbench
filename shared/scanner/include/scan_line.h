/**
 * @file
 * @brief The QR scanner's serial stream split into code lines and command
 *        answer frames.
 *
 * A GM861 scanner module sends two things on its TXD pin: the text of each
 * code it reads, ended by CR or CR LF, and the answer to a command frame
 * (gm861.h).  scan_line_feed() takes the bytes one at a time with a
 * millisecond clock and hands out whole lines and whole frames.
 *
 * - A line holds the bytes 0x20 to 0x7E only.  It ends at CR; a LF right
 *   after the CR is dropped; a LF alone ends a line too.  An empty line is
 *   dropped without a count.
 * - A byte outside 0x20 to 0x7E inside a line poisons it: the line is
 *   dropped at its end and counted as noise.  The UART glue feeds a byte
 *   with a framing or parity error as 0x00, which poisons the line the
 *   same way.
 * - Byte SCAN_LINE_MAX + 1 of a line makes it too long: the bytes up to
 *   its end are not kept, and the end hands out SCAN_OUT_LONG.
 * - A byte 0x02 or 0x03 at the start of a line opens an answer frame:
 *   0x00 0x00, a length of 1 to SCAN_DATA_MAX, the data and 2 CRC bytes
 *   over the bytes after the first two.  A frame with a wrong CRC or a
 *   wrong header is dropped and counted; after a wrong header the bytes up
 *   to the next CR or LF are dropped with it.
 * - SCAN_GAP_MS without a byte drops an open line or frame and counts it as
 *   partial.  The gap is measured with wrap-safe 32-bit arithmetic.
 *
 * Pure C, no SDK.  Host-tested in test_scan_line.
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

/** @brief Bytes of a code's text, without CR LF. */
#define SCAN_LINE_MAX 64u
/** @brief Silence in ms that ends a partial line or frame (96 byte times at 9600 baud). */
#define SCAN_GAP_MS 100u
/** @brief The most data bytes of an answer frame: zones 0x0000 to 0x0060. */
#define SCAN_DATA_MAX 97u
/** @brief Bytes of the longest answer frame: 02 00 00, length, data, 2 CRC bytes. */
#define SCAN_FRAME_MAX (4u + SCAN_DATA_MAX + 2u)

/** @brief What one byte, or one look at the clock, hands out. */
typedef enum {
    SCAN_OUT_NONE = 0,  /**< nothing complete */
    SCAN_OUT_LINE,      /**< a printable line ended: scan_line_text() */
    SCAN_OUT_FRAME,     /**< an answer frame with a good CRC: scan_line_frame() */
    SCAN_OUT_LONG       /**< a printable line past SCAN_LINE_MAX ended; its first SCAN_LINE_MAX bytes are the text */
} scan_out_t;

/** @brief Counts of what the framer handed out and dropped; each wraps at 2^32. */
typedef struct {
    uint32_t lines;     /**< lines handed out */
    uint32_t frames;    /**< frames handed out */
    uint32_t noise;     /**< lines dropped for a byte outside 0x20 to 0x7E */
    uint32_t partial;   /**< lines and frames dropped by SCAN_GAP_MS */
    uint32_t overlong;  /**< lines past SCAN_LINE_MAX */
    uint32_t bad_crc;   /**< frames with a wrong CRC, length or header */
} scan_counts_t;

/** @brief Where the framer is. */
typedef enum {
    SCAN_AT_START = 0,  /**< between lines */
    SCAN_AT_LINE,       /**< inside a line */
    SCAN_AT_FRAME,      /**< inside an answer frame */
    SCAN_AT_SKIP        /**< after a wrong frame header, up to CR or LF */
} scan_at_t;

/** @brief The framer's state.  Fields are private; read them through the functions. */
typedef struct {
    scan_at_t     at;                        /**< where the framer is */
    bool          poisoned;                  /**< the open line holds a byte outside 0x20 to 0x7E */
    bool          after_cr;                  /**< the last byte ended a line with CR */
    bool          seen;                      /**< a byte has arrived since the last end */
    uint32_t      last_ms;                   /**< when the last byte arrived */
    size_t        n;                         /**< bytes of the open line or frame */
    size_t        want;                      /**< bytes the open frame has in all, 0 until its length byte */
    uint8_t       buf[SCAN_FRAME_MAX];       /**< the open line or frame */
    char          text[SCAN_LINE_MAX + 1u];  /**< the last line handed out, NUL ended */
    uint8_t       frame[SCAN_FRAME_MAX];     /**< the last frame handed out */
    size_t        frame_len;                 /**< bytes in @c frame */
    scan_counts_t counts;                    /**< what was handed out and dropped */
} scan_line_t;

/**
 * @brief Start a framer between lines, with every count at 0.
 * @param s The framer; NULL does nothing.
 */
void scan_line_init(scan_line_t *s);

/**
 * @brief Take one byte from the scanner.
 *
 * A gap of SCAN_GAP_MS or more since the last byte drops the open line or
 * frame first, as scan_line_idle() does.
 *
 * @param s      The framer; NULL hands out nothing.
 * @param byte   The byte; 0x00 for a byte with a UART error.
 * @param now_ms The millisecond clock.
 * @return What ended with this byte.
 */
scan_out_t scan_line_feed(scan_line_t *s, uint8_t byte, uint32_t now_ms);

/**
 * @brief Drop an open line or frame after SCAN_GAP_MS of silence.
 * @param s      The framer; NULL does nothing.
 * @param now_ms The millisecond clock.
 * @return SCAN_OUT_NONE: a dropped part is only counted.
 */
scan_out_t scan_line_idle(scan_line_t *s, uint32_t now_ms);

/**
 * @brief The text of the last line handed out.
 * @param s The framer.
 * @return The text, NUL ended; "" before the first line or for a NULL framer.
 */
const char *scan_line_text(const scan_line_t *s);

/**
 * @brief The last answer frame handed out, from its 0x02 or 0x03 to its CRC.
 * @param s   The framer.
 * @param len Receives the frame's length in bytes; may be NULL.
 * @return The frame, or NULL for a NULL framer.
 */
const uint8_t *scan_line_frame(const scan_line_t *s, size_t *len);

/**
 * @brief The counts so far.
 * @param s The framer; must not be NULL.
 * @return The counts.
 */
scan_counts_t scan_line_counts(const scan_line_t *s);

#ifdef __cplusplus
}
#endif
