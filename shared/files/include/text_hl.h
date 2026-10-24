/**
 * @file
 * @brief Syntax highlighting of one line of text: the spans of a line and
 *        the class of each, for the viewer to colour.
 *
 * A line is classed when it is drawn; nothing is stored.  The spans cover
 * the line from its first byte to its last, in order, without a gap; two
 * neighbours never have the same class.  A span starts and ends at an
 * ASCII byte or at the line's ends, so it never splits a UTF-8 sequence.
 *
 * Kinds, by the file's extension (text_hl_kind_of()):
 *
 *   TEXT   every .TXT: the rules below, in this order:
 *          - hex: the whole line, for "NN: XX XX ..." (a KST register
 *            line), "checksum XXXXXXXX", and "key=hexdigits" (the KST
 *            programmer's journal: journal=, target=, port=, pin=, check=);
 *          - heading: the whole line, for line 1, and for a line without
 *            a label whose first character is an upper-case letter and
 *            whose text before the first "(" holds no lower-case letter:
 *            "SETTINGS IN FORCE", "ENDS (the search before programming)",
 *            "AKTIVE EINSTELLUNGEN"; at least 3 characters;
 *          - label: from column 0 up to and with the first ":" or "=" in
 *            the first TEXT_HL_LABEL_CELLS cells, when the line starts with
 *            no space and a space, a tab or the line's end follows it;
 *          - then, along the rest: a comment from a "#" at the start or
 *            after a space or tab to the line's end; a verdict word; a
 *            number with a unit of the unit table; anything else is TEXT.
 *   JSON   .json: keys, strings, numbers, true / false / null, and the
 *          marks { } [ ] , : as punctuation.
 *   CSV    .csv: line 1 is the header; the delimiters are punctuation:
 *          ";" on a line that holds one, "," on a line that does not.
 *   PLAIN  any other file: one TEXT span.
 *
 * Verdict words are the caller's: three lists of strings, one per class,
 * which the screen builds from the report writers' text IDs in every
 * language, so a report written in German on a panel showing English is
 * classed the same, and a changed word in ui_text.def changes the classes
 * with it.  A word matches whole and in its case: "PASS" is no verdict in
 * "PASSED" or as "pass".  Where words overlap, the longest one at a place
 * wins ("NICHT BESTANDEN" over "BESTANDEN").  Each string is matched by its
 * core (text_hl_word_core()), so a writer's string with a label in front,
 * parentheses around or a full stop behind gives the word itself.
 *
 * Numbers: an optional sign, digits, and a "." with digits, at a place no
 * letter, digit or "." is in front of, and ending at a place no letter,
 * digit or "." with a digit follows.  With a unit of the table behind, after
 * at most one space, number and unit are one span of the unit's class; a
 * number without one is TEXT.  A unit ends at a space, a punctuation mark
 * or the line's end.
 *
 * Pure C: no ui, no gfx, no text table.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Cells from column 0 a label's ":" or "=" stands in at most. */
#define TEXT_HL_LABEL_CELLS 24u

/**
 * @brief Every class of a span, one row each: X(name, css, role).
 *
 * name is the class's enum name after TEXT_HL_; css a short name for the
 * Wi-Fi page's style sheet; role the theme's colour role (ui_theme.h's
 * ui_color_id_t) the screen and the page draw the class in.  The rows'
 * order gives the enum's numbers, which are stable: a new class goes at
 * the end.  This file and text_hl.c expand the first two columns only, so
 * shared/files includes nothing of ui; a reader that expands role
 * includes ui_theme.h.  Light and dark palettes both hold every role.
 *
 * @param X The row macro.
 */
#define TEXT_HL_CLASS_ROWS(X)                                              \
    X(TEXT,     "tx", UI_C_TEXT)       /* plain text */                    \
    X(HEADING,  "hd", UI_C_ACCENT)     /* a title or section heading */    \
    X(LABEL,    "lb", UI_C_TEXT_DIM)   /* "Label:" or "name =" */          \
    X(OK,       "ok", UI_C_OK)         /* a word of the OK list */         \
    X(FAIL,     "fl", UI_C_DANGER)     /* a word of the FAIL list */       \
    X(WARN,     "wn", UI_C_WARN)       /* a word of the WARN list */       \
    X(VOLT,     "vo", UI_C_VOLT)       /* a number in V or mV */           \
    X(CURR,     "cu", UI_C_CURR)       /* a number in A or mA */           \
    X(POWER,    "pw", UI_C_POWER)      /* a number in W */                 \
    X(RATE,     "rt", UI_C_RPM)        /* a number in rpm or Hz */         \
    X(UNIT,     "un", UI_C_TEAL)       /* a number in another unit */      \
    X(COMMENT,  "cm", UI_C_TEXT_FAINT) /* "#" to the line's end */         \
    X(HEX,      "hx", UI_C_TEXT_DIM)   /* register, checksum, journal */   \
    X(KEY,      "ky", UI_C_TEXT_DIM)   /* a JSON key */                    \
    X(NUMBER,   "nm", UI_C_TEAL)       /* a JSON number */                 \
    X(LITERAL,  "lt", UI_C_LILAC)      /* JSON true, false, null */        \
    X(PUNCT,    "pu", UI_C_TEXT_FAINT) /* JSON marks, CSV delimiters */    \
    X(CSV_HEAD, "ch", UI_C_ACCENT)     /* a CSV header cell */

/** @brief Expands a row of TEXT_HL_CLASS_ROWS to its enum entry. */
#define TEXT_HL_CLASS_ENUM(name, css, role) TEXT_HL_##name,

/** @brief The class of a span, in the order of TEXT_HL_CLASS_ROWS. */
typedef enum {
    TEXT_HL_CLASS_ROWS(TEXT_HL_CLASS_ENUM)
    TEXT_HL_CLASSES  /**< the count of classes */
} text_hl_class_t;

/** @brief How a file's lines are classed.  The numbers are stable. */
typedef enum {
    TEXT_HL_KIND_TEXT = 0,   /**< the rules of a bench text */
    TEXT_HL_KIND_JSON = 1,   /**< JSON */
    TEXT_HL_KIND_CSV = 2,    /**< CSV */
    TEXT_HL_KIND_PLAIN = 3,  /**< no classes */
} text_hl_kind_t;

/** @brief The verdict words, one list per class.  Any list may be empty. */
typedef struct {
    const char *const *ok;    /**< OK words; NULL entries are skipped */
    size_t ok_n;              /**< their count */
    const char *const *fail;  /**< FAIL words */
    size_t fail_n;            /**< their count */
    const char *const *warn;  /**< WARN words */
    size_t warn_n;            /**< their count */
} text_hl_words_t;

/** @brief A unit of the table and the class of a number in it. */
typedef struct {
    const char *unit;     /**< the unit as the bench's writers put it */
    text_hl_class_t cls;  /**< VOLT, CURR, POWER, RATE or UNIT */
} text_hl_unit_t;

/** @brief Rows of text_hl_units. */
#define TEXT_HL_UNIT_COUNT 20u

/**
 * @brief The units a number may carry, case as written: V mV A mA W rpm Hz
 *        us ms s /s deg Grad % mOhm B kB MB mΩ Ω; mOhm for the reports of
 *        0.22 and older.  test_text_hl holds it to
 *        every unit the report formats of ui_text.def put behind a number.
 */
extern const text_hl_unit_t text_hl_units[TEXT_HL_UNIT_COUNT];

/** @brief A span of a line. */
typedef struct {
    uint32_t start;  /**< first byte, from the line's start */
    uint32_t len;    /**< bytes */
    uint8_t cls;     /**< a text_hl_class_t */
} text_hl_span_t;

/**
 * @brief The spans of one line.
 *
 * With room for fewer spans than the line has, the last span the room
 * holds is TEXT and reaches to the line's end.
 *
 * @param line    The line's bytes, without its line end.
 * @param len     Their count.
 * @param line_no The line's index in the file, from 0.
 * @param kind    How the file is classed.
 * @param words   The verdict words; NULL for none.
 * @param out     Out: the spans.
 * @param cap     Room in @p out.
 * @return Spans written; 0 for an empty line, a NULL or @p cap 0.
 */
size_t text_hl_line(const char *line, size_t len, size_t line_no,
                    text_hl_kind_t kind, const text_hl_words_t *words,
                    text_hl_span_t *out, size_t cap);

/**
 * @brief How a file is classed, by its name: ".json" JSON, ".csv" CSV,
 *        ".txt" TEXT, in any case, a ".NEW" behind any of them taken off
 *        first; PLAIN for any other name.
 *
 * @param name The file name; NULL is PLAIN.
 * @return The kind.
 */
text_hl_kind_t text_hl_kind_of(const char *name);

/**
 * @brief The word a writer's string gives: spaces taken off both ends, a
 *        "Label:" and the spaces after it taken off the front, then one pair
 *        of parentheses around it and a full stop behind it.
 *
 * "Brown-out:      not run" gives "not run", " (cut short)" "cut short",
 * "Not run." "Not run".
 *
 * @param s   The string; NULL gives an empty word.
 * @param len Out: bytes of the word.
 * @return Its first byte, inside @p s.
 */
const char *text_hl_word_core(const char *s, size_t *len);

/**
 * @brief The style sheet's name of a class (TEXT_HL_CLASS_ROWS).
 *
 * @param cls The class.
 * @return Its short name; "tx" for a value out of range.
 */
const char *text_hl_class_css(text_hl_class_t cls);

/**
 * @brief The class of a unit of the table.
 *
 * @param unit The unit's bytes.
 * @param len  Their count.
 * @return TEXT_HL_VOLT, TEXT_HL_CURR, TEXT_HL_POWER, TEXT_HL_RATE or
 *         TEXT_HL_UNIT; TEXT_HL_TEXT for a word that is no unit.
 */
text_hl_class_t text_hl_unit_class(const char *unit, size_t len);

#ifdef __cplusplus
}
#endif
