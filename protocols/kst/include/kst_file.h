/**
 * @file
 * @brief KST servo programming protocol: a servo configuration as a text file.
 *
 * Implements the programming protocol of KST and Chaservo servos.  KST and
 * Chaservo are names or trademarks of their respective owners; they are used
 * here only to say which servos this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * kst_file_format() writes the 32 registers of a servo as text a person
 * reads.  kst_file_parse() reads such a text back, and reads a text a
 * person wrote or changed.  kst_file_load() holds what was read against the
 * connected servo: its type, the access classes and the limits.  All three
 * work on buffers the caller holds.  There is no file access, no heap and
 * no stdio here.
 *
 * The text kst_file_format() writes, format version 2:
 *
 *   line 1         "KST servo configuration, format 2"
 *   line 2         "Type: V6" or "Type: V8", the servo type the caller
 *                  states; no line where it states none
 *   27 lines       one per named field of the field table, in table order:
 *                  "<name> = <value>", the value as kst_field_display()
 *                  gives it with its unit; a field no load writes carries
 *                  " [not loaded]"
 *   1 line         "registers 32"
 *   4 lines        "00: xx xx xx xx xx xx xx xx", 8 registers each, the
 *                  address of the first one in front, 2 upper-case
 *                  hexadecimal digits per register
 *   last line      "checksum xxxxxxxx", 8 upper-case hexadecimal digits
 *
 * The characters are ASCII (American Standard Code for Information
 * Interchange) 0x20 to 0x7E, a line holds at most KST_FILE_LINE_MAX of
 * them, and every line, the last one included, ends with LF (line feed,
 * 0x0A).  kst_file_format() writes exactly this.
 *
 * The checksum is CRC-32 (cyclic redundancy check, polynomial 0x04C11DB7
 * reflected, start 0xFFFFFFFF, final inversion: the one of IEEE 802.3) over
 * every line before the checksum line, each with its line end counted as
 * one LF.
 *
 * A text is read as one of 3 kinds (kst_file_kind_t).  A line whose first
 * word is "checksum" decides between them.
 *
 * The servo type is the caller's statement, not a reading of the
 * registers: 20k_sel, the bit kst_type() reads, is a drive setting, and
 * V8 servos with it set are known (kst_reg.h).  A text of format 1 names
 * the type kst_type() gave for its registers; it is read and its type is
 * not kept: kst_file_parse() gives KST_TYPE_NONE for it, and an edited
 * one is KST_FILE_EDITED_UNTYPED.
 *
 * With a checksum line the file is VERIFIED or refused.  The 4 register
 * lines are the image.  In format 2 the type line may be left out and
 * states the type on its own; in format 1 it is there and has to state the
 * type kst_type() gives for that image.  The field lines are not
 * interpreted: the checksum covers them.  The file
 * has LF on every line or CR LF (carriage return, 0x0D, then LF) on every
 * line; because the checksum counts a line end as one LF, a transfer that
 * converts every line end keeps the file valid.  Both kinds in one file, a
 * CR at any other place and a lower-case hexadecimal digit refuse it.
 * Behind the checksum line, lines of spaces and tabs or of nothing pass and
 * any other byte refuses the file.  The checksum line is taken without its
 * line end when it holds its 17 characters.
 *
 * Without a checksum line the file is EDITED: a person's input.
 *
 *   - Line 1 is the header, unchanged.
 *   - A field line is "<name> = <value>".  The name is the field table's,
 *     compared without case and without spaces.  The value is a number in
 *     the field's unit, with a full stop or a comma before at most 1
 *     decimal place where the unit has tenths.  The unit may follow and
 *     may be left out.  "raw <n>" gives the raw value.  A trailing
 *     "[...]" is ignored.
 *   - "#" starts a comment that runs to the end of the line.  Empty lines,
 *     spaces and tabs around a name and a value, and LF and CR LF in one
 *     file are taken.
 *   - A name the table does not have, a name given twice and a value the
 *     field does not encode are refused with the line number.
 *   - A field without a line keeps the servo's value.
 *   - The type line ("Type: V6", also "type = v6") and the register block
 *     ("registers 32" and its 4 lines) may each stay or go.  In format 2
 *     a file with a type line is KST_FILE_EDITED_TYPED and one without is
 *     KST_FILE_EDITED_UNTYPED; the block states no type.  In format 1 a
 *     type line that contradicts the type kst_type() gives for the block
 *     refuses the file, and the file is KST_FILE_EDITED_UNTYPED.
 *   - The value of an editable field becomes a raw value through
 *     kst_field_raw_from_display(), with its rounding, as an entry on a
 *     keypad does.  A value that reads as the servo already shows it
 *     changes nothing.
 *   - Where the register block shows as the line of an editable field
 *     reads, the block gives the raw value: several raw values of
 *     Max. Duty show as one percentage, and a file whose line is unchanged loads
 *     the raw value it was written from.  A line that reads otherwise
 *     wins over the block.  The block gives no value for a field without
 *     a line.
 *
 * Both kinds take the 3 bytes EF BB BF (the byte order mark of UTF-8,
 * 8-bit Unicode Transformation Format) in front of the header; they are
 * not part of the checksum.
 *
 * The last line of an edited file may lack its line end when the line
 * shows its own end: a unit, a "[...]", a comment, a type line, a
 * register line.  A text that ends in the digits of a number, in a name
 * or inside the register block is refused as truncated, so a cut file
 * never gives a shortened number.  A file cut between two lines reads as
 * an edited file with fewer lines; kst_file_load() states how many of the
 * 27 fields it gives (@c named of @c fields).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_FILE_H
#define KST_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kst_limits.h"
#include "kst_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The format version kst_file_format() writes and kst_file_parse()
 *        reads.
 */
#define KST_FILE_FORMAT 2u

/**
 * @brief Characters kst_file_format() puts in a line at most, the line end not
 *        counted.
 *
 * A line of an edited file may be longer.
 */
#define KST_FILE_LINE_MAX 64u

/**
 * @brief Lines kst_file_format() writes at most: one per field, the header, the
 *        type, the register count, 4 register lines and the checksum.
 */
#define KST_FILE_LINES_MAX ((unsigned)KST_F_COUNT + 8u)

/**
 * @brief Bytes in a file at most: 3432.  kst_file_parse() refuses a longer
 *        text. kst_file_format() writes fewer than 1024 bytes with this field
 *        table, so a buffer of this size holds every written file with either
 *        line end and leaves more than 2300 bytes for a person's comments.
 */
#define KST_FILE_MAX ((size_t)KST_FILE_LINES_MAX * (KST_FILE_LINE_MAX + 2u))

/**
 * @brief Why a text is refused.
 *
 * The numbers are stable.
 */
typedef enum {
    KST_FILE_OK = 0,
    KST_FILE_ERR_ARG = 1,        /**< a NULL argument */
    KST_FILE_ERR_OVERSIZED = 2,  /**< more than KST_FILE_MAX bytes */
    KST_FILE_ERR_NOT_KST = 3,    /**< line 1 is not the header */
    KST_FILE_ERR_VERSION = 4,    /**< the version is not a number from 1 up */
    KST_FILE_ERR_NEWER = 5,      /**< a version above KST_FILE_FORMAT */
    KST_FILE_ERR_TRUNCATED = 6,  /**< the text ends inside a line that
                                      does not show its own end, or inside
                                      the register block */
    KST_FILE_ERR_LINE_END = 7,   /**< checksum line present: LF and CR LF
                                      in one file */
    KST_FILE_ERR_MALFORMED = 8,  /**< a line is not what the format has at
                                      that place */
    KST_FILE_ERR_REG_COUNT = 9,  /**< the register count is not 32 */
    KST_FILE_ERR_CHECKSUM = 10,  /**< a checksum line is present and its
                                      value differs or is not 8 upper-case
                                      hexadecimal digits: the file is
                                      damaged or was changed.  Without the
                                      line the file reads as an edited one */
    KST_FILE_ERR_TRAILING = 11,  /**< a line that is not blank follows
                                      the checksum line */
    KST_FILE_ERR_NAME = 12,      /**< edited: no field has that name */
    KST_FILE_ERR_DUPLICATE = 13, /**< edited: the field has a line already */
    KST_FILE_ERR_VALUE = 14,     /**< edited: not a number in the field's
                                      unit, or a value the field does not
                                      encode; a type that is not V6 or V8 */
    KST_FILE_ERR_TYPE = 15,      /**< format 1: the type line
                                      contradicts the type kst_type()
                                      gives for the register block */
} kst_file_err_t;

/** @brief What a text is. */
typedef enum {
    KST_FILE_VERIFIED = 0,       /**< the checksum fits: the registers are
                                      what kst_file_format() wrote */
    KST_FILE_EDITED_TYPED = 1,   /**< no checksum; format 2 with a type
                                      line: the file states the servo
                                      type */
    KST_FILE_EDITED_UNTYPED = 2, /**< no checksum, and no type line or
                                      format 1: the file states no servo
                                      type */
} kst_file_kind_t;

/** @brief A text as read. */
typedef struct {
    kst_file_kind_t kind;
    /** @brief The format version of line 1, 1 to KST_FILE_FORMAT. */
    uint32_t version;
    /**
     * @brief The servo type the file states: its type line in format 2;
     *        KST_TYPE_NONE without one and for every text of format 1.
     */
    kst_type_t type;
    /** @brief The register block; all 0 for a file without one. */
    kst_image_t image;
    /**
     * @brief Edited kinds: the fields a line gives a value for.
     *
     * 0 for KST_FILE_VERIFIED.
     */
    kst_field_set_t named;
    /**
     * @brief Named fields whose @c value is raw: the line reads "raw <n>", or
     *        the line of an editable field reads as the register block shows.
     */
    kst_field_set_t as_raw;
    /**
     * @brief Per named field: the raw value for an editable field and for a
     *        field in @c as_raw, else the displayed value in the unit of
     *        kst_field_display().
     */
    int32_t value[KST_F_COUNT];
    /** @brief Per named field: the line of the file, counted from 1. */
    uint16_t line[KST_F_COUNT];
} kst_file_t;

/**
 * @brief Write the text of @p img into @p out.
 *
 * Returns the number of bytes written.  No terminator is written.  Returns
 * 0, with @p out in an unspecified state, for a NULL argument and for an
 * @p out_size the text does not fit; KST_FILE_MAX always fits.
 *
 * Every image is written, also one that breaks the layout fingerprint or a
 * limit: the file states what a servo holds.  @p type is the servo type the
 * caller states, the operator's: KST_TYPE_V6 or KST_TYPE_V8 writes the type
 * line, any other value writes none.  kst_type() of @p img is no such
 * statement.
 *
 * @param img       Image to write.
 * @param type      Servo type the file states, or KST_TYPE_NONE for none.
 * @param out       Buffer that receives the text.
 * @param out_size  Size of out, in bytes.
 * @return Bytes written; 0 for a NULL argument or a buffer that is too small.
 */
size_t kst_file_format(const kst_image_t *img, kst_type_t type, char *out,
                       size_t out_size);

/**
 * @brief Read @p len bytes of @p text.
 *
 * @p out is written on KST_FILE_OK and on no other result.  @p line, when
 * not NULL, receives the number of the line the refusal is at, counted
 * from 1, and 0 for a result that belongs to no line (KST_FILE_OK,
 * KST_FILE_ERR_ARG, KST_FILE_ERR_OVERSIZED).
 *
 * Nothing is checked against the fingerprint, an access class or a limit
 * here; kst_file_load() does that against a servo.
 *
 * @param text  Text to read.
 * @param len   Length of text, in bytes.
 * @param out   Receives the file on KST_FILE_OK.
 * @param line  Receives the line of a refusal; may be NULL.
 * @return KST_FILE_OK, or the reason the text is refused.
 */
kst_file_err_t kst_file_parse(const char *text, size_t len, kst_file_t *out,
                              unsigned *line);

/* --- a file against a servo ----------------------------------------------- */

/**
 * @brief Whether a file loads.
 *
 * The numbers are stable.
 */
typedef enum {
    KST_FILE_LOAD_OK = 0,            /**< @c target is the image to write */
    KST_FILE_LOAD_TYPE_MISMATCH = 1, /**< the file states another servo
                                          type: nothing of it is loaded */
    KST_FILE_LOAD_HARD_LIMIT = 2,    /**< the target breaks a hard rule:
                                          nothing of the file is loaded */
    KST_FILE_LOAD_ARG = 3,           /**< a NULL argument */
} kst_file_verdict_t;

/** @brief What loading a file into a servo does. */
typedef struct {
    kst_file_verdict_t verdict;
    kst_file_kind_t kind;       /**< the file's kind, for the display */
    kst_type_t file_type;       /**< the type the file states */
    kst_type_t servo_type;      /**< the servo's type as the caller
                                     states it */
    uint8_t named;              /**< fields the file gives a value for:
                                     @c fields for a verified file, the
                                     lines read for an edited one */
    uint8_t fields;             /**< named fields of the field table: 27 */
    kst_field_set_t editable;   /**< the editable fields in which the file
                                     differs from the servo: what the load
                                     changes */
    kst_field_set_t skipped;    /**< fields of the other access classes in
                                     which the file differs from the servo
                                     after the load: they keep the servo's
                                     value */
    kst_rules_t rules;          /**< every rule the load breaks, hard and
                                     soft, as kst_limits_edit() gives them.
                                     KST_FILE_LOAD_OK leaves soft rules
                                     only: the caller asks for their
                                     confirmation.  0 for a type mismatch */
    kst_field_id_t hard_field;  /**< KST_FILE_LOAD_HARD_LIMIT: the first
                                     field of the file whose value the
                                     broken hard rules need, by line; else
                                     and when no single field is at fault
                                     KST_F_COUNT */
    unsigned hard_line;         /**< the line of @c hard_field in an edited
                                     file; 0 for a verified file and
                                     without a field */
    kst_image_t target;         /**< KST_FILE_LOAD_OK: the servo's image
                                     with the fields of @c editable set by
                                     kst_field_edit(), so register 01
                                     follows a changed Max. Duty.  Any other
                                     verdict: the servo's image */
} kst_file_load_t;

/**
 * @brief Hold a file against the image read from the connected servo.
 *
 * @p servo is the servo's image as read last.  @p backup is the image the
 * limits compare with, as the caller passes it to kst_limits_edit() for an
 * edit made by hand; a caller without a separate backup passes @p servo.
 *
 * 1. Type.  A file that states a type loads into a servo whose type the
 *    caller states as the same, @p servo_type, and into no other: a
 *    @p servo_type of KST_TYPE_NONE takes no file that states one.  A file
 *    that states no type passes: its values apply as input made by hand.
 *    kst_type() of @p servo is not consulted.
 * 2. Values.  Only fields of access class KST_ACCESS_EDITABLE are taken:
 *    all of them from a verified file's registers, the named ones from an
 *    edited file's lines.  A field of another class is never taken from a
 *    file; where the file holds another value than the servo it is in
 *    @c skipped, and the load goes on: the protection block, Servo / Gyro
 *    Servo, Lock and the pairing value never deny a load.  An edited
 *    file's register block gives the raw value of an editable field whose
 *    line reads as the block shows, and no other value.
 * 3. Limits.  The target goes through kst_limits_edit(@p backup, target,
 *    no unlock set), the check an edit made by hand goes through.  A
 *    broken hard rule refuses the whole file.  Soft rules are returned.
 *
 * The caller passes the target of KST_FILE_LOAD_OK to kst_plan_edit() as
 * it does with an edit made by hand; nothing here replaces that check.
 *
 * Returns the verdict.  @p out is untouched for KST_FILE_LOAD_ARG.
 *
 * @param file    The file as read.
 * @param servo   The servo image as read last.
 * @param backup      The image the limits compare with.
 * @param servo_type  The servo's type as the caller states it, or
 *                    KST_TYPE_NONE while it states none.
 * @param out         Receives the load result.
 * @return The verdict.
 */
kst_file_verdict_t kst_file_load(const kst_file_t *file,
                                 const kst_image_t *servo,
                                 const kst_image_t *backup,
                                 kst_type_t servo_type,
                                 kst_file_load_t *out);

/**
 * @brief Whether a line is a checksum line, the test kst_file_parse() makes:
 *        its first word, after spaces and tabs, is "checksum" in any case,
 *        ended by a space, a tab, '=', ':' or the line's end.
 *
 * A file with such a line is read as VERIFIED or refused; a text editor
 * that keeps a file EDITED takes out every line this test names.
 *
 * @param s  The line, without its line end; NULL only with @p n 0.
 * @param n  Its bytes.
 * @return true for a checksum line.
 */
bool kst_file_checksum_line(const char *s, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* KST_FILE_H */
