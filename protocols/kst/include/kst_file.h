/*
 * KST servo programming protocol: a servo configuration as a text file.
 *
 * kst_file_format() writes the 32 registers of a servo as text a person
 * reads.  kst_file_parse() reads such a text back, and reads a text a
 * person wrote or changed.  kst_file_load() holds what was read against the
 * connected servo: its type, the access classes and the limits.  All three
 * work on buffers the caller holds.  There is no file access, no heap and
 * no stdio here.
 *
 * The text kst_file_format() writes, format version 1:
 *
 *   line 1         "KST servo configuration, format 1"
 *   line 2         "Type: V6" or "Type: V8", the servo type as kst_type()
 *                  gives it for the registers
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
 * 0x0A).
 *
 * The checksum is CRC-32 (cyclic redundancy check, polynomial 0x04C11DB7
 * reflected, start 0xFFFFFFFF, final inversion: the one of IEEE 802.3) over
 * every line before the checksum line, each with its line end counted as
 * one LF.
 *
 * A text is read as one of 3 kinds (kst_file_kind_t).  A line whose first
 * word is "checksum" decides between them.
 *
 * With a checksum line the file is VERIFIED or refused.  The 4 register
 * lines are the image.  The type line has to state the type of that image.
 * The field lines are not interpreted: the checksum covers them.  The file
 * has LF on every line or CR LF (carriage return, 0x0D, then LF) on every
 * line; because the checksum counts a line end as one LF, a transfer that
 * converts every line end keeps the file valid.  Both kinds in one file, a
 * CR at any other place, a lower-case hexadecimal digit and any byte after
 * the checksum line refuse it.
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
 *     ("registers 32" and its 4 lines) may each stay or go.  The block
 *     states the servo type and gives no value.  A file with a block or a
 *     type line is KST_FILE_EDITED_TYPED; a type line that contradicts the
 *     block refuses the file.  A file with neither states no type
 *     (KST_FILE_EDITED_UNTYPED).
 *   - The value of an editable field becomes a raw value through
 *     kst_field_raw_from_display(), with its rounding, as an entry on a
 *     keypad does.  A value that reads as the servo already shows it
 *     changes nothing.
 *
 * The last line of every file ends with a line end.  A text that stops
 * inside a line is refused as truncated, so a cut file never gives a
 * shortened number.  A file cut between two lines reads as an edited file
 * with fewer lines.
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

/** The format version kst_file_format() writes and kst_file_parse() reads. */
#define KST_FILE_FORMAT 1u

/** Characters kst_file_format() puts in a line at most, the line end not
 *  counted.  A line of an edited file may be longer. */
#define KST_FILE_LINE_MAX 64u

/** Lines kst_file_format() writes at most: one per field, the header, the
 *  type, the register count, 4 register lines and the checksum. */
#define KST_FILE_LINES_MAX ((unsigned)KST_F_COUNT + 8u)

/**
 * Bytes in a file at most: 3432.  kst_file_parse() refuses a longer text.
 * kst_file_format() writes fewer than 1024 bytes with this field table, so
 * a buffer of this size holds every written file with either line end and
 * leaves more than 2300 bytes for a person's comments.
 */
#define KST_FILE_MAX ((size_t)KST_FILE_LINES_MAX * (KST_FILE_LINE_MAX + 2u))

/** Why a text is refused.  The numbers are stable. */
typedef enum {
    KST_FILE_OK = 0,
    KST_FILE_ERR_ARG = 1,        /**< a NULL argument */
    KST_FILE_ERR_OVERSIZED = 2,  /**< more than KST_FILE_MAX bytes */
    KST_FILE_ERR_NOT_KST = 3,    /**< line 1 is not the header */
    KST_FILE_ERR_VERSION = 4,    /**< the version is not a number from 1 up */
    KST_FILE_ERR_NEWER = 5,      /**< a version above KST_FILE_FORMAT */
    KST_FILE_ERR_TRUNCATED = 6,  /**< the text ends inside a line, or
                                      inside the register block */
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
    KST_FILE_ERR_TRAILING = 11,  /**< bytes follow the checksum line */
    KST_FILE_ERR_NAME = 12,      /**< edited: no field has that name */
    KST_FILE_ERR_DUPLICATE = 13, /**< edited: the field has a line already */
    KST_FILE_ERR_VALUE = 14,     /**< edited: not a number in the field's
                                      unit, or a value the field does not
                                      encode; a type that is not V6 or V8 */
    KST_FILE_ERR_TYPE = 15,      /**< the type line contradicts the
                                      register block */
} kst_file_err_t;

/** What a text is. */
typedef enum {
    KST_FILE_VERIFIED = 0,       /**< the checksum fits: the registers are
                                      what kst_file_format() wrote */
    KST_FILE_EDITED_TYPED = 1,   /**< no checksum; a register block or a
                                      type line states the servo type */
    KST_FILE_EDITED_UNTYPED = 2, /**< no checksum, no register block and no
                                      type line: the file states no servo
                                      type */
} kst_file_kind_t;

/** A text as read. */
typedef struct {
    kst_file_kind_t kind;
    /** The servo type the file states; KST_TYPE_NONE for
     *  KST_FILE_EDITED_UNTYPED. */
    kst_type_t type;
    /** The register block; all 0 for a file without one. */
    kst_image_t image;
    /** Edited kinds: the fields a line gives a value for.  0 for
     *  KST_FILE_VERIFIED. */
    kst_field_set_t named;
    /** Named fields whose line reads "raw <n>". */
    kst_field_set_t as_raw;
    /** Per named field: the raw value for an editable field and for a
     *  field in @c as_raw, else the displayed value in the unit of
     *  kst_field_display(). */
    int32_t value[KST_F_COUNT];
    /** Per named field: the line of the file, counted from 1. */
    uint16_t line[KST_F_COUNT];
} kst_file_t;

/**
 * Write the text of @p img into @p out.
 *
 * Returns the number of bytes written.  No terminator is written.  Returns
 * 0, with @p out in an unspecified state, for a NULL argument and for an
 * @p out_size the text does not fit; KST_FILE_MAX always fits.
 *
 * Every image is written, also one that breaks the layout fingerprint or a
 * limit: the file states what a servo holds.
 */
size_t kst_file_format(const kst_image_t *img, char *out, size_t out_size);

/**
 * Read @p len bytes of @p text.
 *
 * @p out is written on KST_FILE_OK and on no other result.  @p line, when
 * not NULL, receives the number of the line the refusal is at, counted
 * from 1, and 0 for a result that belongs to no line (KST_FILE_OK,
 * KST_FILE_ERR_ARG, KST_FILE_ERR_OVERSIZED).
 *
 * Nothing is checked against the fingerprint, an access class or a limit
 * here; kst_file_load() does that against a servo.
 */
kst_file_err_t kst_file_parse(const char *text, size_t len, kst_file_t *out,
                              unsigned *line);

/* --- a file against a servo ----------------------------------------------- */

/** Whether a file loads.  The numbers are stable. */
typedef enum {
    KST_FILE_LOAD_OK = 0,            /**< @c target is the image to write */
    KST_FILE_LOAD_TYPE_MISMATCH = 1, /**< the file states another servo
                                          type: nothing of it is loaded */
    KST_FILE_LOAD_HARD_LIMIT = 2,    /**< the target breaks a hard rule:
                                          nothing of the file is loaded */
    KST_FILE_LOAD_ARG = 3,           /**< a NULL argument */
} kst_file_verdict_t;

/** What loading a file into a servo does. */
typedef struct {
    kst_file_verdict_t verdict;
    kst_file_kind_t kind;       /**< the file's kind, for the display */
    kst_type_t file_type;       /**< the type the file states */
    kst_type_t servo_type;      /**< the type of the servo's image */
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
 * Hold a file against the image read from the connected servo.
 *
 * @p servo is the servo's image as read last.  @p backup is the image the
 * limits compare with, as the caller passes it to kst_limits_edit() for an
 * edit made by hand; a caller without a separate backup passes @p servo.
 *
 * 1. Type.  A file that states a type loads into a servo of that type
 *    (kst_type()) and into no other.  A file that states no type passes:
 *    its values apply as input made by hand.
 * 2. Values.  Only fields of access class KST_ACCESS_EDITABLE are taken:
 *    all of them from a verified file's registers, the named ones from an
 *    edited file's lines.  A field of another class is never taken from a
 *    file; where the file holds another value than the servo it is in
 *    @c skipped, and the load goes on: the protection block, Servo / Gyro
 *    Servo, Lock and the pairing value never deny a load.  An edited
 *    file's register block gives no value and is not compared.
 * 3. Limits.  The target goes through kst_limits_edit(@p backup, target,
 *    no unlock set), the check an edit made by hand goes through.  A
 *    broken hard rule refuses the whole file.  Soft rules are returned.
 *
 * The caller passes the target of KST_FILE_LOAD_OK to kst_plan_edit() as
 * it does with an edit made by hand; nothing here replaces that check.
 *
 * Returns the verdict.  @p out is untouched for KST_FILE_LOAD_ARG.
 */
kst_file_verdict_t kst_file_load(const kst_file_t *file,
                                 const kst_image_t *servo,
                                 const kst_image_t *backup,
                                 kst_file_load_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KST_FILE_H */
