/**
 * @file
 * @brief A servo's id: the text on its label, the name of its folder on the
 *        card, and the bench's count of ids handed out.
 *
 * An id is `<prefix>-<serial>`, as in `KST_X10-0000A`:
 *
 * | Part      | Rule |
 * | --------- | ---- |
 * | prefix    | 1 to SERVO_ID_PREFIX_MAX (17) characters of A-Z, 0-9, space, "-", "_" and "."; no space at either end; no "." at the start |
 * | separator | the last "-" of the id; a "-" inside the prefix is allowed |
 * | serial    | exactly 5 characters of 0-9 and A-Z, base 36: 00000, 00001, ... 00009, 0000A, ... ZZZZZ |
 * | length    | 7 to SERVO_ID_MAX (23) characters |
 *
 * The prefix is the operator's and is typed on the name keyboard.  The
 * serial is the bench's: one series for the card across every prefix,
 * 60,466,176 (36^5) ids.  Lower case read from a scan or typed on a computer
 * is taken as upper case, and an id is always given back in upper case.
 *
 * Every character of an id is valid in a FAT (File Allocation Table) long
 * name, so the id is also its folder's name, `Servos/<id>`.  A prefix that
 * starts with "." would name a folder the card's walk hides; a space at an
 * end is dropped by the keyboard and by Windows.
 *
 * servo_ids_t reads the card's count: a walk of `Servos` hands each entry to
 * servo_ids_visit(); the next id is one above the highest serial seen.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
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

/** @brief The longest prefix, characters. */
#define SERVO_ID_PREFIX_MAX 17u
/** @brief The serial's length, characters. */
#define SERVO_ID_SERIAL_LEN 5u
/** @brief The longest id: the prefix, "-" and the serial, characters. */
#define SERVO_ID_MAX        23u
/** @brief Bytes of an id and its terminator. */
#define SERVO_ID_SIZE       24u
/** @brief How many serials there are: 36^5. */
#define SERVO_ID_SERIALS    60466176u
/** @brief No serial: a malformed serial's value, and the highest of a card
 *         without an id folder. */
#define SERVO_ID_NO_SERIAL  0xFFFFFFFFu
/** @brief How many of the highest ids servo_ids_t keeps. */
#define SERVO_IDS_RECENT    6u

/** @brief The first rule a prefix or an id breaks. */
typedef enum {
    SERVO_ID_OK = 0,   /**< well formed                                    */
    SERVO_ID_EMPTY,    /**< NULL, "", or no prefix before the "-"          */
    SERVO_ID_LONG,     /**< a prefix over 17 or an id over 23 characters   */
    SERVO_ID_CHAR,     /**< a character outside A-Z, 0-9, space, "-", "_",
                            "." in the prefix                              */
    SERVO_ID_EDGE,     /**< a space at either end of the prefix, or a "."
                            at its start                                   */
    SERVO_ID_SERIAL,   /**< no "-", or not 5 characters of 0-9 and A-Z
                            after the last one                             */
} servo_id_err_t;

/** @brief An id taken apart. */
typedef struct {
    char     text[SERVO_ID_SIZE];             /**< the id, upper case     */
    char     prefix[SERVO_ID_PREFIX_MAX + 1u]; /**< the part before the last
                                                   "-", upper case        */
    uint32_t serial;                          /**< 0 to 60,466,175        */
} servo_id_t;

/**
 * @brief Check a prefix and give it back in upper case.
 *
 * The rules are checked in the order of servo_id_err_t: empty, long, a
 * character, an edge.
 *
 * @param in  The prefix.
 * @param out Receives it in upper case on SERVO_ID_OK; NULL for none.  Left
 *            as it was otherwise.
 * @param n   Size of @p out, bytes; SERVO_ID_PREFIX_MAX + 1 holds every
 *            prefix.  Too small is SERVO_ID_LONG.
 * @return SERVO_ID_OK or the first rule broken.
 */
servo_id_err_t servo_id_prefix_check(const char *in, char *out, size_t n);

/**
 * @brief A serial as its 5 characters: 0 is "00000", 10 is "0000A", 36 is
 *        "00010".
 *
 * @param serial The serial.
 * @param out    Receives the text and its terminator; "" when false.
 * @return false for a serial of SERVO_ID_SERIALS or more.
 */
bool servo_id_serial_text(uint32_t serial, char out[6]);

/**
 * @brief The serial of exactly 5 characters of 0-9 and A-Z (a-z taken as
 *        A-Z) and the terminator.
 *
 * @param five The text.
 * @return The serial; SERVO_ID_NO_SERIAL for NULL, another length or
 *         another character.
 */
uint32_t servo_id_serial_parse(const char *five);

/**
 * @brief Build the id of @p prefix and @p serial: "KST_X10" and 10 give
 *        "KST_X10-0000A".
 *
 * @param out    Receives the id in upper case; "" when false.
 * @param n      Size of @p out, bytes; SERVO_ID_SIZE holds every id.
 * @param prefix The prefix; servo_id_prefix_check() must take it.
 * @param serial The serial, below SERVO_ID_SERIALS.
 * @return false for a prefix refused, a serial out of range or an @p out
 *         too small.
 */
bool servo_id_build(char *out, size_t n, const char *prefix, uint32_t serial);

/**
 * @brief Whether @p text is a well-formed id; the one check of a scan, a
 *        folder on the card and a pick from a list.
 *
 * The id splits at its last "-".  Lower case is taken as upper case.
 *
 * @param text The text.
 * @param out  Receives the id taken apart, in upper case, when true; NULL
 *             for none.  Left as it was otherwise.
 * @return true when servo_id_why() is SERVO_ID_OK.
 */
bool servo_id_parse(const char *text, servo_id_t *out);

/**
 * @brief The first rule @p text breaks as an id, in this order: empty,
 *        long (over 23 characters), the serial, then the prefix's rules.
 *
 * @param text The text.
 * @return SERVO_ID_OK for a well-formed id.
 */
servo_id_err_t servo_id_why(const char *text);

/**
 * @brief The serial after @p highest.
 *
 * @param highest The highest serial the card holds; SERVO_ID_NO_SERIAL for
 *                none.
 * @param next    Receives it: 0 after none, @p highest + 1 otherwise.
 * @return false at 60,466,175 (ZZZZZ) and above: there is no wrap, a
 *         wrapped id would meet a label in use.
 */
bool servo_id_next(uint32_t highest, uint32_t *next);

/**
 * @brief The folder of servo @p id from the card's root: "Servos/<id>", the
 *        id in upper case.
 *
 * @param out Receives the path; "" when false.
 * @param n   Size of @p out, bytes; CARD_RUN_DIR_MAX holds every folder.
 * @param id  The id.
 * @return false for an id servo_id_parse() refuses or an @p out too small.
 */
bool servo_id_folder(char *out, size_t n, const char *id);

/** @brief The card's ids, read off one walk of `Servos`. */
typedef struct {
    uint32_t highest;   /**< the highest serial; SERVO_ID_NO_SERIAL for no
                             id folder                                    */
    uint16_t count;     /**< id folders seen, held at 65,535              */
    /** @brief The ids of the SERVO_IDS_RECENT highest serials, upper case,
     *         highest first; of one serial, the one seen first first. */
    char     recent[SERVO_IDS_RECENT][SERVO_ID_SIZE];
    uint8_t  recent_n;  /**< entries of recent in use                     */
} servo_ids_t;

/**
 * @brief Start a walk: no id seen.
 * @param s The count.
 */
void servo_ids_begin(servo_ids_t *s);

/**
 * @brief Count one entry of `Servos`: a folder whose name servo_id_parse()
 *        takes, under any prefix.  A file and any other folder are left out.
 *
 * @param s      The count.
 * @param name   The entry's name.
 * @param is_dir Whether it is a folder.
 */
void servo_ids_visit(servo_ids_t *s, const char *name, bool is_dir);

#ifdef __cplusplus
}
#endif
