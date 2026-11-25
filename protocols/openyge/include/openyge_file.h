/**
 * @file openyge_file.h
 * @brief The parameter table of an OpenYGE ESC (electronic speed controller)
 *        as a text file: write, read, and hold against the ESC on the wire.
 *
 * All functions work on buffers the caller holds.  There is no file access,
 * no heap and no float here.  The card path and the file name are the
 * caller's; oy_file_name() gives the name of the backup, `YGE001-5712.TXT`.
 *
 * The text oy_file_format() writes, format 1:
 *
 *     YGE ESC settings, format 1
 *     Type: 5712 165 HVT          index 11; the model name when the map has one
 *     Firmware: 1.03555           indices 12-13 as u32 / 100000, 5 decimals
 *     Serial: 4660                indices 14-15 as u32
 *     Count: 32                   index 0, OY_FILE_COUNT_MIN to 64
 *     00 PARAMETER COUNT = 32     one line per index from 0 to count - 1:
 *     01 ESC MODE = 2             2 decimal digits, the map's key (`RAW` for
 *     ...                         an index of 32 and above), the value 0 to
 *     31 RPM SETPOINT = 0         65535 in decimal without leading zeros
 *     checksum 1A2B3C4D           8 upper-case hexadecimal digits
 *
 * The characters are ASCII (American Standard Code for Information
 * Interchange) 0x20 to 0x7E, a line holds at most OY_FILE_LINE_MAX of them,
 * and every line ends with LF (line feed, 0x0A).
 *
 * The checksum is CRC-32 (cyclic redundancy check, polynomial 0x04C11DB7
 * reflected, start 0xFFFFFFFF, final inversion: the one of IEEE 802.3) over
 * every line before the checksum line, each with its line end counted as one
 * LF.  LF or CR LF (carriage return, 0x0D, then LF) on every line keeps a file
 * valid; both kinds in one file refuse it.  Behind the checksum line only
 * lines of spaces and tabs or of nothing pass.  The checksum line is taken
 * without its line end.  The 3 bytes EF BB BF (the byte order mark of UTF-8,
 * 8-bit Unicode Transformation Format) in front of the header are taken and
 * not counted.
 *
 * Only a file with its checksum loads.  A file whose last line that is not
 * blank reads `edited` is refused as OY_FILE_ERR_EDITED; one with neither line
 * as OY_FILE_ERR_TRUNCATED, so a file cut at any byte loads nothing.  A file
 * whose header lines say another thing than its index lines is refused as
 * OY_FILE_ERR_MISMATCH, even with its checksum.
 *
 * oy_file_load() takes the writable indices of a file into the table read last
 * and judges the result with oy_guard_approve() on a copy of the guard: a
 * load never gives a draft the guard refuses, and never approves one.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OPENYGE_FILE_H
#define OPENYGE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openyge.h"
#include "openyge_guard.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The stem of the backup files: YGE001-5712.TXT for type code
 *         5712, numbers 001 to 999. */
#define OY_FILE_STEM "YGE"

/** @brief The format version oy_file_format() writes and oy_file_parse()
 *         reads. */
#define OY_FILE_FORMAT 1u

/** @brief The fewest parameters a file holds: indices 0 to 15 carry the count
 *         and the identity. */
#define OY_FILE_COUNT_MIN 16u

/** @brief Characters in a line at most, the line end not counted. */
#define OY_FILE_LINE_MAX 64u

/** @brief Lines a file holds at most: the header, 4 identity lines, one line
 *         per index and the checksum. */
#define OY_FILE_LINES_MAX (6u + OPENYGE_MAX_PARAMS)

/** @brief Bytes in a file at most: 4620.  oy_file_parse() refuses a longer
 *         text.  A file of oy_file_format() fits with either line end and a
 *         byte order mark. */
#define OY_FILE_MAX ((size_t)OY_FILE_LINES_MAX * (OY_FILE_LINE_MAX + 2u))

/** @brief Bytes of the name oy_file_name() writes, its terminator counted:
 *         `YGE999-65535.TXT`. */
#define OY_FILE_NAME_MAX 17u

/** @brief Why a text is refused.  The numbers are stable. */
typedef enum {
    OY_FILE_OK = 0,
    OY_FILE_ERR_ARG = 1,       /**< a NULL argument                           */
    OY_FILE_ERR_OVERSIZED = 2, /**< more than OY_FILE_MAX bytes               */
    OY_FILE_ERR_NOT_YGE = 3,   /**< line 1 is not the header                  */
    OY_FILE_ERR_VERSION = 4,   /**< the version is not a number from 1 to 999 */
    OY_FILE_ERR_NEWER = 5,     /**< a version above OY_FILE_FORMAT            */
    OY_FILE_ERR_TRUNCATED = 6, /**< no checksum line and no `edited` last line,
                                    or a line before the checksum without its
                                    line end                                  */
    OY_FILE_ERR_LINE_END = 7,  /**< LF and CR LF in one file                  */
    OY_FILE_ERR_MALFORMED = 8, /**< a line is not what the format has there, a
                                    character outside 0x20 to 0x7E, or a line
                                    over OY_FILE_LINE_MAX characters          */
    OY_FILE_ERR_COUNT = 9,     /**< a count outside OY_FILE_COUNT_MIN to 64, or
                                    another number of index lines             */
    OY_FILE_ERR_CHECKSUM = 10, /**< the checksum differs or is not 8 upper-case
                                    digits: damaged, or changed               */
    OY_FILE_ERR_TRAILING = 11, /**< a line that is not blank behind the
                                    checksum line                             */
    OY_FILE_ERR_MISMATCH = 12, /**< the checksum fits, and a key is not the
                                    map's or a header line says another thing
                                    than the index lines                      */
    OY_FILE_ERR_EDITED = 13,   /**< an `edited` last line: a person's input,
                                    with no checksum to hold it               */
} oy_file_err_t;

/** @brief A file as read. */
typedef struct {
    uint16_t count;    /**< parameters, index 0                              */
    uint16_t type;     /**< the ESC type code, index 11                      */
    uint32_t firmware; /**< indices 12-13; 103555 for V1.03555               */
    uint32_t serial;   /**< indices 14-15                                    */
    uint16_t value[OPENYGE_MAX_PARAMS]; /**< value per index; 0 past @c count */
} oy_file_t;

/** @brief What oy_file_load() found.  The numbers are stable. */
typedef enum {
    OY_LOAD_OK = 0,
    OY_LOAD_ARG = 1,   /**< a NULL argument                                  */
    OY_LOAD_TYPE = 2,  /**< a file of another ESC type code                  */
    OY_LOAD_COUNT = 3, /**< a file of another count than the table read last */
    OY_LOAD_GUARD = 4, /**< the guard refuses the draft: @c ref and @c bad say
                            why; also no table read, or a read-only table    */
} oy_load_t;

/** @brief What loading a file into the ESC does. */
typedef struct {
    uint16_t count;      /**< entries of @c draft                            */
    uint16_t changed;    /**< indices whose value the load changes           */
    uint16_t bad;        /**< OY_LOAD_GUARD: the first refused index         */
    uint8_t  ref;        /**< OY_LOAD_GUARD: an oy_ref_t                     */
    bool     other_fw;   /**< the file names another firmware than the ESC   */
    bool     other_serial; /**< the file names another serial than the ESC  */
    uint16_t draft[OPENYGE_MAX_PARAMS]; /**< OY_LOAD_OK: the draft for
                                             oy_session_write(); else the
                                             table read last                 */
} oy_file_load_t;

/**
 * @brief CRC-32 of a block.
 * @param data the bytes, may be NULL with @p n 0
 * @param n    their count
 * @return the checksum; 0xCBF43926 over `"123456789"`
 */
uint32_t oy_file_crc32(const void *data, size_t n);

/**
 * @brief Write the text of a table as read.
 * @param table the values, index 0 first; index 0 is @p count
 * @param count entries at @p table, OY_FILE_COUNT_MIN to OPENYGE_MAX_PARAMS
 * @param out   takes the text, without a terminator
 * @param size  bytes at @p out; OY_FILE_MAX always fits
 * @return bytes written; 0 for a NULL, a count out of range or not at index
 *         0, or a text that does not fit, with @p out in an unspecified state
 */
size_t oy_file_format(const uint16_t *table, uint16_t count, char *out,
                      size_t size);

/**
 * @brief Read a text.
 * @param text the text
 * @param len  its bytes
 * @param out  takes the file on OY_FILE_OK and on no other result
 * @param line takes the line of a refusal, from 1; 0 when it has none; may be
 *             NULL
 * @return OY_FILE_OK, or why the text is refused
 */
oy_file_err_t oy_file_parse(const char *text, size_t len, oy_file_t *out,
                            unsigned *line);

/**
 * @brief Hold a file against the table read last.  The writable indices
 *        (oy_map_writable()) take the file's value, bits 0 and 1 of index 26
 *        only; every other index and bit stays the ESC's.  The draft is then
 *        judged by oy_guard_approve() on a copy of @p g, which stays as it is.
 * @param f   the file
 * @param g   the guard of the session, holding the table read last
 * @param out takes the result; untouched for OY_LOAD_ARG
 * @return OY_LOAD_OK, or why nothing is loaded
 */
oy_load_t oy_file_load(const oy_file_t *f, const oy_guard_t *g,
                       oy_file_load_t *out);

/**
 * @brief The name of a backup file: `YGE<nnn>-<type>.TXT`.
 * @param out  takes the name with its terminator
 * @param size bytes at @p out; OY_FILE_NAME_MAX always fits
 * @param n    the file's number, 1 to 999
 * @param type the ESC type code, index 11
 * @return true when written; false for a NULL, @p n out of range or too
 *         little room
 */
bool oy_file_name(char *out, size_t size, unsigned n, uint16_t type);

#ifdef __cplusplus
}
#endif

#endif /* OPENYGE_FILE_H */
