/**
 * @file ew_file.h
 * @brief The settings of an ESC (electronic speed controller) as a text file:
 *        write, read, hold against an ESC.
 *
 * All functions work on buffers the caller holds.  There is no file access
 * here; `shared/logfile/servo_store.h` with the stem EW_FILE_STEM reaches the
 * card.
 *
 * The text ew_file_format() writes, format 1:
 *
 *     ESC settings backup, format 1
 *     Firmware: AM32                    BLHeli_S, Bluejay, AM32 or none
 *     Layout: 4                         the layout or revision; 0 for none
 *     Processor: ARM                    EFM8 or ARM
 *     Pin code: 26                      ARM: 2 upper-case hexadecimal digits
 *     Flash code: 1F                    ARM: 2 digits
 *     Bootloader protocol: 3            ARM: byte 7 of the first contact
 *     Bootloader version: 12            ARM: byte 2 of the page
 *     Name: EXAMPLE_F421                ARM: at most 29 characters, maybe none
 *     Signature: E8B2                   EFM8 in place of the 5 lines above
 *     POWER RAMP = 5                    one line per field of the layout; a
 *     REVISION = 4 [not loaded]         field no load takes says so
 *     bytes 192                         the page, whole, as read
 *     00: 01 04 ...                     16 bytes a line, the offset in front
 *     checksum 1A2B3C4D                 8 upper-case hexadecimal digits
 *
 * The characters are ASCII (American Standard Code for Information
 * Interchange) 0x20 to 0x7E, a line holds at most EW_FILE_LINE_MAX of them and
 * every line ends with LF (line feed, 0x0A).
 *
 * The checksum is CRC-32 (cyclic redundancy check, polynomial 0x04C11DB7
 * reflected, start 0xFFFFFFFF, final inversion: the one of IEEE 802.3) over
 * every line before the checksum line, each with its line end counted as one
 * LF.  LF or CR LF (carriage return, 0x0D, then LF) on every line keeps a file
 * valid.  Both kinds in one file refuse it.
 *
 * The last line that is not blank says what the text is.
 *
 * - `checksum xxxxxxxx`: EW_FILE_VERIFIED, or refused.  The bytes are the
 *   page.  The field lines are not interpreted: the checksum covers them.
 * - `edited`: EW_FILE_EDITED, a person's input.  To change a file, change its
 *   field lines and put `edited` in place of the checksum line.
 * - Anything else: refused as EW_FILE_ERR_TRUNCATED.  A file cut at any byte
 *   has neither last line, so a cut file loads nothing.
 *
 * In an edited file:
 *
 * - `Firmware` and `Layout` are required and come before the fields.  The
 *   other identity lines are read and not used.
 * - A field line is `<name> = <value>`.  The name is the map's, without case
 *   and without spaces.  The value is the text this format writes for that
 *   value, without case and without spaces, its unit optional, or `raw <n>`
 *   for the byte.  Where several bytes read as one text the lowest is taken,
 *   and a line that reads as the ESC's value changes nothing.
 * - `#` starts a comment that runs to the end of the line.  Empty lines,
 *   spaces and tabs around a name and a value, LF and CR LF in one file are
 *   taken.  A trailing `[...]` is ignored.
 * - A field without a line keeps the ESC's value.  The bytes may stay or go;
 *   they have to be whole and they give no value.
 * - A name the map does not have, a name given twice and a text that is no
 *   value of the field refuse the file, with the line.
 *
 * Both kinds take the 3 bytes EF BB BF (the byte order mark of UTF-8, 8-bit
 * Unicode Transformation Format) in front of the header.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_FILE_H
#define EW_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_id.h"
#include "ew_map.h"
#include "ew_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The stem of the files on the card: ESCW001.TXT to ESCW999.TXT, the
 *         stem alone being the record of a write. */
#define EW_FILE_STEM "ESCW"

/** @brief The format version ew_file_format() writes and ew_file_parse()
 *         reads. */
#define EW_FILE_FORMAT 1u

/** @brief Characters in a line of a verified file at most, the line end not
 *         counted.  A line of an edited file may be longer. */
#define EW_FILE_LINE_MAX 64u

/** @brief Lines ew_file_format() writes at most. */
#define EW_FILE_LINES_MAX 80u

/** @brief Bytes in a file at most: 5280.  ew_file_parse() refuses a longer
 *         text.  A file of ew_file_format() fits with either line end and a
 *         byte order mark. */
#define EW_FILE_MAX ((size_t)EW_FILE_LINES_MAX * (EW_FILE_LINE_MAX + 2u))

/** @brief Why a text is refused.  The numbers are stable. */
typedef enum {
    EW_FILE_OK = 0,
    EW_FILE_ERR_ARG = 1,        /**< a NULL argument                            */
    EW_FILE_ERR_OVERSIZED = 2,  /**< more than EW_FILE_MAX bytes                */
    EW_FILE_ERR_NOT_ESC = 3,    /**< line 1 is not the header                   */
    EW_FILE_ERR_VERSION = 4,    /**< the version is not a number from 1 up      */
    EW_FILE_ERR_NEWER = 5,      /**< a version above EW_FILE_FORMAT             */
    EW_FILE_ERR_TRUNCATED = 6,  /**< no `checksum` and no `edited` last line,
                                     or the bytes of an edited file are cut     */
    EW_FILE_ERR_LINE_END = 7,   /**< verified: LF and CR LF in one file         */
    EW_FILE_ERR_MALFORMED = 8,  /**< a line is not what the format has there    */
    EW_FILE_ERR_FIRMWARE = 9,   /**< a firmware this format does not name; for
                                     an edited file also `none`                 */
    EW_FILE_ERR_LENGTH = 10,    /**< the count of bytes does not fit the
                                     firmware, or is not 1 to EW_PAGE_MAX       */
    EW_FILE_ERR_CHECKSUM = 11,  /**< the checksum differs or is not 8 upper-case
                                     digits: damaged, or changed                */
    EW_FILE_ERR_TRAILING = 12,  /**< a line that is not blank behind a checksum
                                     line                                       */
    EW_FILE_ERR_MISMATCH = 13,  /**< the checksum fits and the header lines say
                                     another thing than the page or each other  */
    EW_FILE_ERR_NAME_LONG = 14, /**< a name over EW_ID_NAME_MAX characters      */
    EW_FILE_ERR_FIELD = 15,     /**< edited: no field or line has that name     */
    EW_FILE_ERR_DUPLICATE = 16, /**< edited: that line is there already         */
    EW_FILE_ERR_VALUE = 17,     /**< edited: no value of the field reads so     */
    EW_FILE_ERR_NO_TARGET = 18, /**< edited: a field before, or no, `Firmware`
                                     and `Layout`                               */
} ew_file_err_t;

/** @brief What a text is. */
typedef enum {
    EW_FILE_VERIFIED = 0, /**< the checksum fits: @c page is what was read     */
    EW_FILE_EDITED = 1,   /**< a person's input: @c page holds named fields    */
} ew_file_kind_t;

/** @brief A file as read, or a page as captured. */
typedef struct {
    uint8_t  kind;       /**< an ew_file_kind_t                                */
    uint8_t  fw;         /**< an ew_fw_t; EW_FW_NONE for an ESC refused        */
    uint8_t  layout;     /**< the layout or revision; 0 for none or erased     */
    uint8_t  cpu;        /**< an ew_cpu_t; verified only                       */
    uint8_t  pin_code;   /**< ARM: byte 3 of the first contact                 */
    uint8_t  flash_code; /**< ARM: byte 4                                      */
    uint8_t  protocol;   /**< ARM: byte 7, the bootloader protocol             */
    uint8_t  named;      /**< edited: the lines that give a value              */
    uint16_t signature;  /**< EFM8: bytes 4 and 5                              */
    uint16_t len;        /**< bytes of @c page                                 */
    char     name[EW_ID_NAME_MAX + 1u]; /**< ARM: the name, "" for none        */
    uint8_t  has[EW_PAGE_MAX / 8u];     /**< edited: bit n, a line for byte n  */
    uint8_t  page[EW_PAGE_MAX];         /**< the page, or the named fields     */
} ew_file_t;

/** @brief What ew_file_load() found.  The numbers are stable. */
typedef enum {
    EW_LOAD_OK = 0,
    EW_LOAD_ARG = 1,       /**< a NULL argument                               */
    EW_LOAD_FIRMWARE = 2,  /**< a file of another firmware                    */
    EW_LOAD_LAYOUT = 3,    /**< a file of another layout or revision          */
    EW_LOAD_ERASED = 4,    /**< AM32 settings erased: only a restore writes   */
    EW_LOAD_READ_ONLY = 5, /**< a page that is not written                    */
    EW_LOAD_VALUE = 6,     /**< an editable field would take no value of its
                                own: @c bad_off is its byte                   */
} ew_load_t;

/** @brief What loading a file into an ESC does. */
typedef struct {
    uint8_t  changed;  /**< editable fields the load changes                  */
    uint8_t  named;    /**< editable fields the file gives a value for        */
    uint8_t  fields;   /**< editable fields of the layout                     */
    uint16_t skipped;  /**< bytes the file holds otherwise and the load keeps:
                            tags, markers, melody, tune, fields not editable  */
    uint16_t bad_off;  /**< EW_LOAD_VALUE: the byte                           */
    uint16_t len;      /**< bytes of @c target                                */
    uint8_t  target[EW_PAGE_MAX]; /**< EW_LOAD_OK: the draft for
                                       ew_session_write(); else the ESC's page */
} ew_file_load_t;

/** @brief What ew_file_restore_check() found.  The numbers are stable. */
typedef enum {
    EW_RESTORE_OK = 0,
    EW_RESTORE_ARG = 1,         /**< a NULL argument                          */
    EW_RESTORE_NOT_VERIFIED = 2, /**< an edited file: no CRC-32 holds         */
    EW_RESTORE_FIRMWARE = 3,    /**< the file or the ESC is not AM32          */
    EW_RESTORE_NOT_ERASED = 4,  /**< a byte of the ESC's page is not 0xFF     */
    EW_RESTORE_CAN_BUILD = 5,   /**< an ESC built for CAN (Controller Area
                                     Network) boards                          */
    EW_RESTORE_BAD_PAGE = 6,    /**< the file's byte 0 is not 0x01, or its
                                     revision is not 3 or 4                   */
    EW_RESTORE_PIN_CODE = 7,    /**< another pin code                         */
    EW_RESTORE_FLASH_CODE = 8,  /**< another flash code                       */
    EW_RESTORE_PROTOCOL = 9,    /**< another bootloader protocol              */
    EW_RESTORE_NAME = 10,       /**< another name                             */
} ew_restore_t;

/**
 * @brief CRC-32 of a block.
 * @param data the bytes, may be NULL with @p n 0
 * @param n    their count
 * @return the checksum
 */
uint32_t ew_file_crc32(const void *data, size_t n);

/**
 * @brief Take a page as read into a file, whole.
 * @param f    takes the file
 * @param c    the first contact's answer
 * @param id   what identification found
 * @param page the page, @c id->len bytes
 * @return false for a NULL, a length out of range, an ESC no processor was
 *         found for, or an identity that does not fit the page; @p f is then
 *         unchanged
 */
bool ew_file_capture(ew_file_t *f, const ew_contact_t *c, const ew_ident_t *id,
                     const uint8_t *page);

/**
 * @brief Write the text of a captured file.
 * @param f    the file, of kind EW_FILE_VERIFIED
 * @param out  takes the text, without a terminator
 * @param size bytes at @p out; EW_FILE_MAX always fits
 * @return bytes written; 0 for a NULL, an edited file, or a text that does not
 *         fit, with @p out in an unspecified state
 */
size_t ew_file_format(const ew_file_t *f, char *out, size_t size);

/**
 * @brief Read a text.
 * @param text the text
 * @param len  its bytes
 * @param out  takes the file on EW_FILE_OK and on no other result
 * @param line takes the line of a refusal, from 1; 0 when it has none; may be
 *             NULL
 * @return EW_FILE_OK, or why the text is refused
 */
ew_file_err_t ew_file_parse(const char *text, size_t len, ew_file_t *out,
                            unsigned *line);

/**
 * @brief Hold a file against the ESC on the wire.  Editable fields are taken
 *        and every other byte stays the ESC's.  Only the same firmware and the
 *        same layout load, and only into a page that is written.
 * @param f   the file
 * @param id  what identification found
 * @param esc the page as read, @c id->len bytes
 * @param out takes the result; untouched for EW_LOAD_ARG
 * @return EW_LOAD_OK, or why nothing is loaded
 */
ew_load_t ew_file_load(const ew_file_t *f, const ew_ident_t *id,
                       const uint8_t *esc, ew_file_load_t *out);

/**
 * @brief Whether a file may restore an AM32 whose settings read erased: the
 *        checks before ew_session_restore(), which repeats the identity and
 *        page rules.  @c f->page and @p from are its arguments, with
 *        EW_MAP_LEN_AM bytes.
 *
 * The bootloader version, byte 2 of a page, reads 0xFF on an erased ESC, so
 * the bootloader protocol stands for it.
 *
 * @param f    the file
 * @param id   what identification found
 * @param here the ESC's identity, from ew_session_backup_id()
 * @param esc  the page as read, EW_MAP_LEN_AM bytes
 * @param from takes the file's identity on EW_RESTORE_OK
 * @return EW_RESTORE_OK, or why the file does not restore this ESC
 */
ew_restore_t ew_file_restore_check(const ew_file_t *f, const ew_ident_t *id,
                                   const ew_backup_id_t *here,
                                   const uint8_t *esc, ew_backup_id_t *from);

#ifdef __cplusplus
}
#endif

#endif /* EW_FILE_H */
