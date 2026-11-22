/**
 * @file
 * @brief The panel's settings as a text file: write, read the header, read
 *        the whole, and put a read file in force.
 *
 * The text sf_format() writes, format 1:
 *
 *     rcbench settings, format 1
 *     Panel: 0.23.15                    the build that wrote it
 *     Written: 2026-11-21 14:05:33 UTC  or "not known" without a clock
 *     # Not in this file: the Wi-Fi home network, ...
 *     pack_cells = 4                    # Pack cells, 1 to 14S
 *     theme = DARK                      # Theme: DARK, LIGHT
 *     srv_dut = "SERVO"                 # DUT name
 *     end
 *
 * One line a setting, `<key> = <value>`, the key the NVS (non-volatile
 * storage) key of settings.h's schema, in schema order, then the strings in
 * id order.  A number is written with settings_decimals() digits after the
 * point, a switch ON or OFF, a choice the English text of its option, a
 * string in double quotes with `\"` and `\\` for a quote and a backslash.
 * Each line carries a comment with the setting's English label and its
 * range or its options.  Sensor SCL is not written: it follows Sensor SDA.
 *
 * The file holds the stored copy (settings_stored(), settings_stored_text()):
 * the values the panel's store holds, not edits that are not saved.  The
 * Wi-Fi home network, its password, HOTSPOT PASSWORD, ACCESS CODE and the
 * code switch are in another store (wa_session.h) and never in this file:
 * a card is read by every computer it goes into, and a file that is loaded
 * must not switch the access code off or name a network the panel joins.
 *
 * Reading, sf_parse():
 *
 * - Line 1 is the header; a format above SF_FORMAT refuses the file.
 * - The last line that is not blank and not a comment is `end`.  A file
 *   without it is cut short and is refused whole, so a cut file never loads
 *   half the settings and defaults for the rest.
 * - `Panel:` and `Written:` lines are read where they stand; a missing one
 *   reads as not known.
 * - `#` starts a comment outside quotes.  Spaces and tabs around a key and a
 *   value, empty lines, LF and CR LF, and a UTF-8 byte order mark in front
 *   of the header are taken.
 * - A key is compared without case; a value of a choice or a switch too.
 * - A line that is refused is skipped and listed with its line number and a
 *   code (sf_line_err_t); the other lines load.
 *
 * sf_apply() sets every setting: the file's value where a line gave one,
 * the schema's default where none did.  A setting held now (settings_held())
 * keeps its value and is listed as SF_LINE_HELD.
 *
 * Pure C: no file access and no heap; the caller holds every buffer.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The format version sf_format() writes and sf_parse() reads. */
#define SF_FORMAT 1u

/** @brief Line 1 without the version. */
#define SF_HEADER "rcbench settings, format "

/** @brief Bytes of a file at most: sf_parse() refuses a longer text, and a
 *         file of sf_format() fits with CR LF line ends. */
#define SF_FILE_MAX 24576u

/** @brief Characters of a line sf_format() writes at most, the line end not
 *         counted. */
#define SF_LINE_MAX 120u

/** @brief Room for the panel's version, with the terminator. */
#define SF_VERSION_MAX 24u

/** @brief Room for the written date, with the terminator. */
#define SF_WRITTEN_MAX 32u

/** @brief Refused lines an sf_load_t lists; @c refused_n counts them all. */
#define SF_REFUSED_MAX 16u

/** @brief Room for a key in a refusal, with the terminator: 15 characters. */
#define SF_KEY_MAX 16u

/** @brief Why a file is refused whole.  The numbers are stable. */
typedef enum {
    SF_OK = 0,
    SF_ERR_ARG = 1,          /**< a NULL argument                           */
    SF_ERR_OVERSIZED = 2,    /**< more than SF_FILE_MAX bytes               */
    SF_ERR_NOT_SETTINGS = 3, /**< line 1 is not the header                  */
    SF_ERR_VERSION = 4,      /**< the format is not a number from 1 to 999  */
    SF_ERR_NEWER = 5,        /**< a format above SF_FORMAT                  */
    SF_ERR_TRUNCATED = 6,    /**< no `end` as the last line                 */
} sf_err_t;

/** @brief Why one line is refused and skipped.  The numbers are stable. */
typedef enum {
    SF_LINE_OK = 0,
    SF_LINE_MALFORMED = 1, /**< no `=`, no key, or another `<word>:` line   */
    SF_LINE_KEY = 2,       /**< no setting has that key                     */
    SF_LINE_TWICE = 3,     /**< the key had a line already                  */
    SF_LINE_VALUE = 4,     /**< not a number, not an option, not ON or OFF,
                                or a string not in quotes                   */
    SF_LINE_RANGE = 5,     /**< a value the schema does not take
                                (settings_takes())                          */
    SF_LINE_LONG = 6,      /**< a string over SETTINGS_TEXT_MAX - 1 bytes   */
    SF_LINE_DERIVED = 7,   /**< a setting that follows another: sense_scl   */
    SF_LINE_HELD = 8,      /**< sf_apply(): held now, left as it is         */
} sf_line_err_t;

/** @brief What the first lines say. */
typedef struct {
    unsigned format;                 /**< the header's format, 1 to 999     */
    char version[SF_VERSION_MAX];    /**< `Panel:`; "" not known            */
    char written[SF_WRITTEN_MAX];    /**< `Written:`; "" not known          */
} sf_head_t;

/** @brief One refused line. */
typedef struct {
    uint16_t line;           /**< from 1; 0 for a setting without a line   */
    uint8_t  code;           /**< an sf_line_err_t                          */
    char     key[SF_KEY_MAX]; /**< the key as the line spells it, cut       */
} sf_refusal_t;

/** @brief A file read by sf_parse(). */
typedef struct {
    sf_head_t head;                       /**< its first lines              */
    float     values[SETTING_COUNT];      /**< the values its lines gave    */
    uint16_t  value_line[SETTING_COUNT];  /**< each one's line; 0 for none  */
    /** @brief The strings its lines gave. */
    char      texts[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];
    uint16_t  text_line[SETTING_TEXT_COUNT]; /**< each one's line; 0 none  */
    sf_refusal_t refused[SF_REFUSED_MAX]; /**< the first refused lines      */
    uint16_t  refused_n;                  /**< every refused line           */
    uint16_t  taken;                      /**< lines that gave a value      */
} sf_load_t;

/**
 * @brief Write the stored settings as the text of format SF_FORMAT.
 * @param out     Takes the text, terminated.
 * @param cap     Bytes at @p out; SF_FILE_MAX always fits.
 * @param version The panel's version, "0.23.15"; NULL or "" writes
 *                "not known".
 * @param written The date, "2026-11-21 14:05:33 UTC"; NULL or "" writes
 *                "not known".
 * @return The bytes written, the terminator not counted; 0 when @p out is
 *         NULL or the text does not fit.
 */
size_t sf_format(char *out, size_t cap, const char *version,
                 const char *written);

/**
 * @brief Read the header, `Panel:` and `Written:` from the first lines of a
 *        file: what a list shows before the file is read whole.
 *
 * Reads until both lines are found, the text ends or a setting's line
 * comes; a text cut inside a line is taken up to the cut.
 *
 * @param text The text, or its first bytes.
 * @param len  Its bytes.
 * @param out  Takes the header; on a refusal it holds what was read.
 * @return SF_OK, SF_ERR_ARG, SF_ERR_NOT_SETTINGS, SF_ERR_VERSION or
 *         SF_ERR_NEWER.
 */
sf_err_t sf_head(const char *text, size_t len, sf_head_t *out);

/**
 * @brief Read a whole file.
 * @param text The text.
 * @param len  Its bytes.
 * @param out  Takes what it gives; every value it does not give is the
 *             schema's default.
 * @param line Takes the line of a refusal, from 1, 0 for one without; may be
 *             NULL.
 * @return SF_OK, or why the file is refused whole.
 */
sf_err_t sf_parse(const char *text, size_t len, sf_load_t *out,
                  unsigned *line);

/**
 * @brief Whether putting @p l in force would change the stored copy.
 * @param l The file read.
 * @return true when a value or a string differs.
 */
bool sf_differs(const sf_load_t *l);

/**
 * @brief Put a file in force: settings_set() and settings_set_text() for
 *        every setting and string, the file's value or the default.  A held
 *        setting keeps its value and is listed in @p l as SF_LINE_HELD.  The
 *        caller asks for the save (settings_request_save()).
 * @param l The file read; takes the held ones.
 * @return The values and strings in force that changed.
 */
int sf_apply(sf_load_t *l);

#ifdef __cplusplus
}
#endif
