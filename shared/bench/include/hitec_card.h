/**
 * @file
 * @brief The card's side of the programmer for servos by Hitec: the text of
 *        the record of a write, which survives a restart of the panel, the
 *        text of a servo file, their parse, and the requests for the card.
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
 * A servo file holds the image of one servo as a full read found it, in
 * the card's folder CARD_HITEC_CONF under a name the operator chose or
 * HITEC001 to HITEC999 (servo_store.h).  Its text, at most
 * HITEC_CARD_FILE_MAX bytes with its terminator:
 *
 *   RCBENCH HITEC 1
 *   series=C or D                    the series
 *   model=...                        the model as hitec_card_model_text()
 *                                    names it, 1 to HITEC_CARD_MODEL_MAX
 *                                    printable characters
 *   image=XX...                      the addresses of a full read
 *                                    (hitec_card_file_addrs()): classic 100
 *                                    cells of 2 digits, D series 21
 *                                    registers of 4 digits
 *   version=XX                       classic, where the version was read
 *   check=XXXX                       link_crc() of the bytes before
 *
 * Every line ends with LF (line feed, 0x0A) alone.  A text with any other
 * line, a lower-case digit, a CR (carriage return) or a byte after the
 * check line is refused whole: a file edited by hand does not load.
 *
 * The card's side of the files: a save, a list and a load are asked for in
 * a hitec_card_req_t and answered in a hitec_card_done_t; hitec_card_run.h
 * carries them out on the card.
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

/* --- servo files ----------------------------------------------------------- */

/** @brief A servo file's text with its terminator: a classic file with the
 *         longest model line takes 281 bytes. */
#define HITEC_CARD_FILE_MAX 320u
/** @brief The characters of a model line's value at most. */
#define HITEC_CARD_MODEL_MAX 20u
/** @brief The addresses a file holds at most: a classic read, 100 cells. */
#define HITEC_CARD_FILE_ADDRS 100u
/** @brief The files a list holds: a page of the FILES list. */
#define HITEC_CARD_FILES 5u
/** @brief A file's base name with its terminator (SERVO_STORE_BASE_MAX). */
#define HITEC_CARD_NAME_MAX 33u
/** @brief The stem of the proposed names and the record's base name. */
#define HITEC_CARD_STEM HITEC_CARD_RECORD

/**
 * @brief The addresses a servo file holds for @p series: the full read.
 *
 * @param series hitec_family_t.
 * @param out    Receives up to HITEC_CARD_FILE_ADDRS addresses, ascending.
 * @return The count: 100 classic cells, 21 D series registers.
 */
unsigned hitec_card_file_addrs(uint8_t series,
                               uint8_t out[HITEC_CARD_FILE_ADDRS]);

/**
 * @brief The model of @p img as a file's model line names it: Hitec's name
 *        for a known model, "D485HW"; "D series" and the number of register
 *        0x00 for another D series servo; the name cells as read for
 *        another classic servo, "?" for none.
 *
 * @param img The image.
 * @param out Receives the name, terminated.
 */
void hitec_card_model_text(const hitec_image_t *img,
                           char out[HITEC_CARD_MODEL_MAX + 1u]);

/**
 * @brief The servo file text of @p img.
 *
 * @param img The image, of a full read; a byte not known is written as its
 *            value all the same.
 * @param out Receives the text, terminated.
 * @param n   Bytes at @p out, HITEC_CARD_FILE_MAX or more.
 * @return The length without the terminator; 0 when @p n is too small or
 *         @p img is of neither series.
 */
size_t hitec_card_file_text(const hitec_image_t *img, char *out, size_t n);

/**
 * @brief Parse a servo file text.
 *
 * @param text  The text; it need not be terminated.
 * @param len   Its length in bytes.
 * @param out   Receives the image on HITEC_CARD_OK, the addresses of
 *              hitec_card_file_addrs() known; untouched otherwise.
 * @param model Receives the model line's value on HITEC_CARD_OK; may be
 *              NULL.
 * @param line  Receives the line, from 1, that HITEC_CARD_ERR_FIELD names;
 *              0 otherwise; may be NULL.
 * @return HITEC_CARD_OK, or the first fault: HITEC_CARD_ERR_HEADER for a
 *         text that is no servo file, HITEC_CARD_ERR_TRUNCATED,
 *         HITEC_CARD_ERR_CHECK, HITEC_CARD_ERR_FIELD.  A record's text is
 *         HITEC_CARD_ERR_FIELD at line 3.
 */
hitec_card_result_t hitec_card_file_parse(const char *text, size_t len,
                                          hitec_image_t *out,
                                          char *model, unsigned *line);

/** @brief What the card's side is asked for. */
typedef enum {
    HITEC_CARD_IDLE = 0,
    HITEC_CARD_SAVE,   /**< the image into a new file                      */
    HITEC_CARD_LIST,   /**< a page of the files and the next proposed name */
    HITEC_CARD_LOAD,   /**< the file with the name                         */
} hitec_card_op_t;

/** @brief How the card's side answered. */
typedef enum {
    HITEC_CARD_DONE_OK = 0,
    HITEC_CARD_DONE_NO_CARD,
    HITEC_CARD_DONE_FULL,    /**< no space, or no proposed name is left    */
    HITEC_CARD_DONE_WRITE,   /**< the file was not written whole           */
    HITEC_CARD_DONE_READ,    /**< the directory or the file does not read  */
    HITEC_CARD_DONE_NAME,    /**< the name is not one a file may have      */
    HITEC_CARD_DONE_EXISTS,  /**< a file has the name; nothing written     */
    HITEC_CARD_DONE_DENIED,  /**< the card takes no file: write protected,
                                  or a file has the name of a folder       */
    HITEC_CARD_DONE_BAD,     /**< the file does not parse: parse and line
                                  say why                                  */
} hitec_card_done_result_t;

/** @brief A request to the card's side. */
typedef struct {
    uint8_t  op;        /**< hitec_card_op_t                               */
    bool     preset;    /**< SAVE: the card's side names the file          */
    uint16_t first;     /**< LIST: the first file wanted, from 0           */
    uint32_t id;        /**< comes back in the answer                      */
    char     name[HITEC_CARD_NAME_MAX]; /**< SAVE, LOAD: the base name     */
    hitec_image_t image;                /**< SAVE: the servo               */
} hitec_card_req_t;

/** @brief The card's side's answer. */
typedef struct {
    uint8_t  op;        /**< the request's hitec_card_op_t                 */
    uint8_t  result;    /**< hitec_card_done_result_t                      */
    uint8_t  parse;     /**< LOAD: hitec_card_result_t                     */
    uint8_t  count;     /**< LIST: the files in names                      */
    uint16_t line;      /**< LOAD, HITEC_CARD_ERR_FIELD: the line          */
    uint16_t total;     /**< LIST: the servo files on the card, 65535 for
                             that many or more                            */
    uint32_t id;        /**< the request's                                 */
    /** @brief SAVE: the file written; LIST: the next proposed name. */
    char     name[HITEC_CARD_NAME_MAX];
    char     model[HITEC_CARD_MODEL_MAX + 1u]; /**< LOAD: the model line   */
    hitec_image_t image;                       /**< LOAD: the file's image */
    char     names[HITEC_CARD_FILES][HITEC_CARD_NAME_MAX]; /**< LIST       */
    /** @brief LIST: each file's model line; "" for a file that does not
     *         parse. */
    char     models[HITEC_CARD_FILES][HITEC_CARD_MODEL_MAX + 1u];
} hitec_card_done_t;

#ifdef __cplusplus
}
#endif
