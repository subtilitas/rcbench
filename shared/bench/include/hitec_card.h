/**
 * @file
 * @brief The card's side of the programmer for servos by Hitec: the text of
 *        the record of a write, which survives a restart of the panel, and
 *        its parse.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * Before the first step of a write, hitec_prog.h records the servo's
 * values, the values the write is to leave, the series, the port and the
 * GPIO that port drives.  With a card mounted the record goes to
 * BENCH/HITEC/CONF/HITEC.TXT (CARD_HITEC_CONF) and is read again at the
 * panel's next start.  A write that ends verified clears it: the text then
 * holds no record.
 *
 * The record holds the addresses a write can change: classic cells 0x00 to
 * 0x23 (hitec_writable() and the balance 0x1C), and the 21 D series
 * registers of a full read (hitec_d_read_list()).  The text, at most
 * HITEC_CARD_TEXT_MAX bytes with its terminator, X a hexadecimal digit:
 *
 *   RCBENCH HITEC 1
 *   series=C or D                    the series of the write
 *   port=X                           the programming port, from 0
 *   pin=XX                           its GPIO, FF not known
 *   before=XX...                     classic: 36 cells of 2 digits; D
 *   target=XX...                     series: 21 registers of 4 digits,
 *                                    high digit first
 *   check=XXXX                       link_crc() of the bytes before
 *
 * A text with no record is the first line and the check line.  The digits
 * are upper case.  A text that does not end with its check line, whose
 * check does not match, or whose lines are not these, is refused whole.
 *
 * Pure C, no heap, no card access: the caller writes and reads the bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hitec_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The record's base name in CARD_HITEC_CONF: HITEC.TXT, and
 *         HITEC.NEW while it is written. */
#define HITEC_CARD_RECORD "HITEC"
/** @brief The text's size, with its terminator: a D series record takes
 *         234 bytes. */
#define HITEC_CARD_TEXT_MAX 256u
/** @brief A pin that is not known. */
#define HITEC_CARD_NO_PIN 0xFFu

/** @brief A record of a write. */
typedef struct {
    uint8_t       series;  /**< hitec_family_t */
    uint8_t       port;    /**< the programming port, from 0 */
    uint8_t       pin;     /**< its GPIO; HITEC_CARD_NO_PIN not known */
    hitec_image_t before;  /**< the servo before the write */
    hitec_image_t target;  /**< what the write was to leave */
} hitec_card_record_t;

/** @brief What a parse found. */
typedef enum {
    HITEC_CARD_OK = 0,        /**< a record */
    HITEC_CARD_NONE,          /**< a whole text that holds no record */
    HITEC_CARD_ERR_HEADER,    /**< not "RCBENCH HITEC 1" */
    HITEC_CARD_ERR_TRUNCATED, /**< no check line at its end */
    HITEC_CARD_ERR_CHECK,     /**< the check does not match */
    HITEC_CARD_ERR_FIELD,     /**< a line that is not the one due */
} hitec_card_result_t;

/**
 * @brief The addresses a record holds for @p series.
 *
 * @param series hitec_family_t.
 * @param out    Receives up to 36 addresses, ascending.
 * @return The count: 36 classic cells, 21 D series registers.
 */
unsigned hitec_card_addrs(uint8_t series, uint8_t out[HITEC_C_BLOCK_LEN]);

/**
 * @brief The text of @p r, or of no record for NULL.
 *
 * Only the addresses hitec_card_addrs() names are written; a byte that is
 * not known is written as its value all the same.
 *
 * @param r   The record; NULL for a text without one.
 * @param out Receives the text, terminated.
 * @param n   Bytes at @p out, HITEC_CARD_TEXT_MAX or more.
 * @return The length without the terminator; 0 when @p n is too small, @p r
 *         names a series other than classic and D, or a port above 15.
 */
size_t hitec_card_text(const hitec_card_record_t *r, char *out, size_t n);

/**
 * @brief Parse a text.
 *
 * A record's images know the addresses hitec_card_addrs() names, nothing
 * else.  A pin above 47 reads as HITEC_CARD_NO_PIN.
 *
 * @param text The text; it need not be terminated.
 * @param len  Its length in bytes.
 * @param out  Receives the record on HITEC_CARD_OK; untouched otherwise.
 * @return A hitec_card_result_t.
 */
hitec_card_result_t hitec_card_parse(const char *text, size_t len,
                                     hitec_card_record_t *out);

#ifdef __cplusplus
}
#endif
