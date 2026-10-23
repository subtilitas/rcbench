/**
 * @file
 * @brief Syntax highlighting of one line of text (text_hl.h): the rules of
 *        a bench text, JSON and CSV.
 *
 * SPDX-License-Identifier: MIT
 */

#include "text_hl.h"

#include <stdbool.h>
#include <string.h>

/** @brief Digits of the checksum in a KST file's "checksum" line. */
#define CHECKSUM_DIGITS 8u
/** @brief Characters a line has at least to be a heading. */
#define HEADING_MIN 3u

/** @brief Where the spans of a line are written. */
typedef struct {
    text_hl_span_t *out;  /**< the caller's spans */
    size_t cap;           /**< their count */
    size_t n;             /**< spans written */
    bool full;            /**< the last span is TEXT to the line's end */
} spans_t;

const text_hl_unit_t text_hl_units[TEXT_HL_UNIT_COUNT] = {
    {"V", TEXT_HL_VOLT},     {"mV", TEXT_HL_VOLT},  {"A", TEXT_HL_CURR},
    {"mA", TEXT_HL_CURR},    {"W", TEXT_HL_POWER},  {"rpm", TEXT_HL_RATE},
    {"Hz", TEXT_HL_RATE},    {"us", TEXT_HL_UNIT},  {"ms", TEXT_HL_UNIT},
    {"s", TEXT_HL_UNIT},     {"/s", TEXT_HL_UNIT},  {"deg", TEXT_HL_UNIT},
    {"Grad", TEXT_HL_UNIT},  {"%", TEXT_HL_UNIT},   {"mOhm", TEXT_HL_UNIT},
    {"B", TEXT_HL_UNIT},     {"kB", TEXT_HL_UNIT},  {"MB", TEXT_HL_UNIT},
};

/** @brief Expands a row of TEXT_HL_CLASS_ROWS to its style sheet name. */
#define CLASS_CSS(name, css, role) css,

/** @brief The style sheet's name of each class, by class. */
static const char *const k_css[TEXT_HL_CLASSES] = {
    TEXT_HL_CLASS_ROWS(CLASS_CSS)
};

/* ----------------------------------------------------------- characters */

/**
 * @brief Whether @p b belongs to a word: an ASCII letter or digit, "_", or
 *        any byte of a UTF-8 sequence.
 *
 * @param b The byte.
 * @return true for a word byte.
 */
static bool is_word(unsigned char b)
{
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') ||
           (b >= 'a' && b <= 'z') || b == '_' || b >= 0x80u;
}

/**
 * @brief Whether @p b is an ASCII digit.
 *
 * @param b The byte.
 * @return true for 0 to 9.
 */
static bool is_digit(unsigned char b)
{
    return b >= '0' && b <= '9';
}

/**
 * @brief Whether @p b is a hexadecimal digit.
 *
 * @param b The byte.
 * @return true for 0 to 9, A to F and a to f.
 */
static bool is_hex(unsigned char b)
{
    return is_digit(b) || (b >= 'A' && b <= 'F') || (b >= 'a' && b <= 'f');
}

/**
 * @brief Whether @p b is a space or a tab.
 *
 * @param b The byte.
 * @return true for 0x20 and 0x09.
 */
static bool is_blank(unsigned char b)
{
    return b == ' ' || b == '\t';
}

/** @brief The case of a letter. */
typedef enum {
    LETTER_NONE = 0,  /**< no letter */
    LETTER_UPPER,     /**< A to Z, Ä Ö Ü */
    LETTER_LOWER,     /**< a to z, ä ö ü ß */
} letter_t;

/**
 * @brief The case of the letter at @p s: ASCII and the 7 German letters.
 *
 * @param s   The bytes.
 * @param n   Their count from @p s, at least 1.
 * @param len Out: bytes of the character, 1 or 2.
 * @return The case; LETTER_NONE for any other character.
 */
static letter_t letter(const unsigned char *s, size_t n, size_t *len)
{
    *len = 1u;
    if (s[0] >= 'A' && s[0] <= 'Z') {
        return LETTER_UPPER;
    }
    if (s[0] >= 'a' && s[0] <= 'z') {
        return LETTER_LOWER;
    }
    if (s[0] == 0xC3u && n >= 2u) {
        *len = 2u;
        switch (s[1]) {
        case 0x84u: case 0x96u: case 0x9Cu:   /* Ä Ö Ü */
            return LETTER_UPPER;
        case 0xA4u: case 0xB6u: case 0xBCu:   /* ä ö ü */
        case 0x9Fu:                           /* ß */
            return LETTER_LOWER;
        default:
            break;
        }
    }
    *len = 1u;
    return LETTER_NONE;
}

/* ---------------------------------------------------------------- spans */

/**
 * @brief Add bytes @p start to @p start + @p len as class @p cls, joined to
 *        the last span when it has that class.
 *
 * @param sp    The spans.
 * @param start First byte; the end of the last span.
 * @param len   Bytes; 0 adds nothing.
 * @param cls   The class.
 */
static void emit(spans_t *sp, size_t start, size_t len, text_hl_class_t cls)
{
    if (len == 0u) {
        return;
    }
    if (sp->n > 0u &&
        (sp->full || sp->out[sp->n - 1u].cls == (uint8_t)cls)) {
        sp->out[sp->n - 1u].len += (uint32_t)len;
        return;
    }
    if (sp->n + 1u == sp->cap) {
        /* The last slot: the rest of the line is one TEXT span. */
        cls = TEXT_HL_TEXT;
        sp->full = true;
    }
    sp->out[sp->n].start = (uint32_t)start;
    sp->out[sp->n].len = (uint32_t)len;
    sp->out[sp->n].cls = (uint8_t)cls;
    sp->n++;
}

/* ---------------------------------------------------------------- words */

/**
 * @brief Narrow [*a, *b) of @p s past the blanks at both ends.
 *
 * @param s The string.
 * @param a In and out: the first byte.
 * @param b In and out: the byte after the last.
 */
static void trim(const char *s, size_t *a, size_t *b)
{
    while (*a < *b && is_blank((unsigned char)s[*a])) {
        (*a)++;
    }
    while (*b > *a && is_blank((unsigned char)s[*b - 1u])) {
        (*b)--;
    }
}

const char *text_hl_word_core(const char *s, size_t *len)
{
    size_t a = 0u;
    size_t b;

    if (s == NULL) {
        *len = 0u;
        return "";
    }
    b = strlen(s);
    trim(s, &a, &b);
    for (size_t i = a; i + 1u < b; ++i) {
        if (s[i] == ':' && is_blank((unsigned char)s[i + 1u])) {
            a = i + 1u;
            break;
        }
    }
    trim(s, &a, &b);
    if (b > a + 1u && s[a] == '(' && s[b - 1u] == ')') {
        a++;
        b--;
    }
    if (b > a && s[b - 1u] == '.') {
        b--;
    }
    trim(s, &a, &b);
    *len = b - a;
    return s + a;
}

/**
 * @brief Whether the edge byte @p e of a word stands alone against its
 *        neighbour @p nb: a word byte against no word byte, a mark against
 *        anything but the same mark.
 *
 * @param e  The word's first or last byte.
 * @param nb The byte beside it in the line.
 * @return true when the word ends there.
 */
static bool apart(unsigned char e, unsigned char nb)
{
    return is_word(e) ? !is_word(nb) : nb != e;
}

/**
 * @brief The longest verdict word of @p list at byte @p i of the line.
 *
 * @param line The line.
 * @param len  Its bytes.
 * @param i    Where the word would start.
 * @param list The words.
 * @param n    Their count.
 * @return Bytes of the longest word that matches; 0 for none.
 */
static size_t word_at(const unsigned char *line, size_t len, size_t i,
                      const char *const *list, size_t n)
{
    size_t best = 0u;

    for (size_t k = 0u; list != NULL && k < n; ++k) {
        size_t wl;
        const char *w = text_hl_word_core(list[k], &wl);

        if (wl == 0u || wl <= best || wl > len - i ||
            memcmp(line + i, w, wl) != 0) {
            continue;
        }
        if ((i > 0u && !apart((unsigned char)w[0], line[i - 1u])) ||
            (i + wl < len && !apart((unsigned char)w[wl - 1u], line[i + wl]))) {
            continue;
        }
        best = wl;
    }
    return best;
}

/* -------------------------------------------------------------- numbers */

text_hl_class_t text_hl_unit_class(const char *unit, size_t len)
{
    if (unit == NULL) {
        return TEXT_HL_TEXT;
    }
    for (size_t k = 0u; k < TEXT_HL_UNIT_COUNT; ++k) {
        if (strlen(text_hl_units[k].unit) == len &&
            memcmp(text_hl_units[k].unit, unit, len) == 0) {
            return text_hl_units[k].cls;
        }
    }
    return TEXT_HL_TEXT;
}

const char *text_hl_class_css(text_hl_class_t cls)
{
    return ((unsigned)cls < (unsigned)TEXT_HL_CLASSES) ? k_css[cls]
                                                       : k_css[TEXT_HL_TEXT];
}

/**
 * @brief A number at byte @p i, with the unit behind it.
 *
 * @param line The line.
 * @param len  Its bytes.
 * @param i    Where the number would start.
 * @param cls  Out: the unit's class; TEXT_HL_TEXT for a bare number.
 * @return Bytes of the number and its unit; 0 when no number starts here.
 */
static size_t number_at(const unsigned char *line, size_t len, size_t i,
                        text_hl_class_t *cls)
{
    size_t j = i;

    *cls = TEXT_HL_TEXT;
    if (i > 0u && (is_word(line[i - 1u]) || line[i - 1u] == '.')) {
        return 0u;
    }
    if ((line[j] == '+' || line[j] == '-') && j + 1u < len) {
        j++;
    }
    if (!is_digit(line[j])) {
        return 0u;
    }
    while (j < len && is_digit(line[j])) {
        j++;
    }
    if (j + 1u < len && line[j] == '.' && is_digit(line[j + 1u])) {
        j++;
        while (j < len && is_digit(line[j])) {
            j++;
        }
    }
    if (j < len && (is_word(line[j]) ||
                    (line[j] == '.' && j + 1u < len &&
                     is_digit(line[j + 1u])))) {
        return 0u;
    }
    /* The unit: after at most one space, "%", "/" and letters, or
     * letters, ending at no word byte. */
    size_t k = (j < len && line[j] == ' ') ? j + 1u : j;
    size_t u = k;
    if (u < len && line[u] == '%') {
        u++;
    } else {
        if (u < len && line[u] == '/') {
            u++;
        }
        while (u < len && ((line[u] >= 'A' && line[u] <= 'Z') ||
                           (line[u] >= 'a' && line[u] <= 'z'))) {
            u++;
        }
    }
    if (u > k && (u == len || !is_word(line[u]))) {
        *cls = text_hl_unit_class((const char *)line + k, u - k);
        if (*cls != TEXT_HL_TEXT) {
            return u - i;
        }
    }
    return j - i;
}

/* ------------------------------------------------------------ the rules */

/**
 * @brief Whether a line is a register line "NN: XX XX ...", a checksum line
 *        "checksum XXXXXXXX" or a journal line "key=hexdigits", trailing
 *        spaces allowed.
 *
 * @param s   The line.
 * @param len Its bytes.
 * @return true for a hex line.
 */
static bool hex_line(const unsigned char *s, size_t len)
{
    size_t i = 0u;

    while (len > 0u && is_blank(s[len - 1u])) {
        len--;
    }
    if (len >= 6u && is_hex(s[0]) && is_hex(s[1]) && s[2] == ':') {
        for (i = 3u; i + 3u <= len; i += 3u) {
            if (s[i] != ' ' || !is_hex(s[i + 1u]) || !is_hex(s[i + 2u])) {
                return false;
            }
        }
        return i == len;
    }
    if (len > 8u && memcmp(s, "checksum", 8u) == 0) {
        i = 8u;
        while (i < len && is_blank(s[i])) {
            i++;
        }
        if (i == 8u || len - i != CHECKSUM_DIGITS) {
            return false;
        }
        while (i < len && is_hex(s[i])) {
            i++;
        }
        return i == len;
    }
    while (i < len && s[i] >= 'a' && s[i] <= 'z') {
        i++;
    }
    if (i == 0u || i + 1u >= len || s[i] != '=') {
        return false;
    }
    for (i++; i < len; ++i) {
        if (!is_hex(s[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a line is a heading: its first character an upper-case
 *        letter, at least HEADING_MIN characters, and no lower-case letter
 *        before the first "(".
 *
 * @param s   The line.
 * @param len Its bytes.
 * @return true for a heading.
 */
static bool heading(const unsigned char *s, size_t len)
{
    size_t chars = 0u;
    size_t n = 1u;
    bool open = false;

    if (len == 0u || letter(s, len, &n) != LETTER_UPPER) {
        return false;
    }
    for (size_t i = 0u; i < len; i += n) {
        n = 1u;
        if (s[i] == '(') {
            open = true;
        }
        if (!open && letter(s + i, len - i, &n) == LETTER_LOWER) {
            return false;
        }
        if ((s[i] & 0xC0u) != 0x80u) {
            chars++;
        }
    }
    return chars >= HEADING_MIN;
}

/**
 * @brief Where a label ends: after the first ":" or "=" in the first
 *        TEXT_HL_LABEL_CELLS characters, when a blank or the line's end
 *        follows it and the line starts with no blank.
 *
 * @param s   The line.
 * @param len Its bytes.
 * @return Bytes of the label with its mark; 0 for none.
 */
static size_t label_end(const unsigned char *s, size_t len)
{
    size_t cells = 0u;

    if (len == 0u || is_blank(s[0])) {
        return 0u;
    }
    for (size_t i = 0u; i < len && cells < TEXT_HL_LABEL_CELLS; ++i) {
        if ((s[i] & 0xC0u) == 0x80u) {
            continue;
        }
        if (s[i] == ':' || s[i] == '=') {
            return (i + 1u == len || is_blank(s[i + 1u])) ? i + 1u : 0u;
        }
        cells++;
    }
    return 0u;
}

/**
 * @brief The spans of a bench text's line from byte @p i on: comments,
 *        verdict words and numbers with units.
 *
 * @param sp    The spans.
 * @param s     The line.
 * @param len   Its bytes.
 * @param i     Where to start.
 * @param words The verdict words; NULL for none.
 */
static void rest(spans_t *sp, const unsigned char *s, size_t len, size_t i,
                 const text_hl_words_t *words)
{
    static const text_hl_class_t k_word_cls[3] = {TEXT_HL_OK, TEXT_HL_FAIL,
                                                  TEXT_HL_WARN};
    size_t text = i;

    while (i < len) {
        if (s[i] == '#' && (i == 0u || is_blank(s[i - 1u]))) {
            emit(sp, text, i - text, TEXT_HL_TEXT);
            emit(sp, i, len - i, TEXT_HL_COMMENT);
            return;
        }
        size_t best = 0u;
        text_hl_class_t cls = TEXT_HL_TEXT;
        if (words != NULL) {
            const char *const *lists[3] = {words->ok, words->fail,
                                           words->warn};
            const size_t counts[3] = {words->ok_n, words->fail_n,
                                      words->warn_n};
            for (size_t k = 0u; k < 3u; ++k) {
                const size_t wl = word_at(s, len, i, lists[k], counts[k]);
                if (wl > best) {
                    best = wl;
                    cls = k_word_cls[k];
                }
            }
        }
        if (best == 0u) {
            best = number_at(s, len, i, &cls);
        }
        if (best > 0u && cls != TEXT_HL_TEXT) {
            emit(sp, text, i - text, TEXT_HL_TEXT);
            emit(sp, i, best, cls);
            i += best;
            text = i;
        } else {
            i += (best > 0u) ? best : 1u;
        }
    }
    emit(sp, text, len - text, TEXT_HL_TEXT);
}

/**
 * @brief The spans of a bench text's line.
 *
 * @param sp      The spans.
 * @param s       The line.
 * @param len     Its bytes.
 * @param line_no Its index in the file.
 * @param words   The verdict words; NULL for none.
 */
static void text_line(spans_t *sp, const unsigned char *s, size_t len,
                      size_t line_no, const text_hl_words_t *words)
{
    size_t lead = 0u;

    if (hex_line(s, len)) {
        emit(sp, 0u, len, TEXT_HL_HEX);
        return;
    }
    while (lead < len && is_blank(s[lead])) {
        lead++;
    }
    if (lead < len && s[lead] == '#') {
        emit(sp, 0u, lead, TEXT_HL_TEXT);
        emit(sp, lead, len - lead, TEXT_HL_COMMENT);
        return;
    }
    const size_t label = label_end(s, len);
    if (line_no == 0u || (label == 0u && heading(s, len))) {
        emit(sp, 0u, len, TEXT_HL_HEADING);
        return;
    }
    emit(sp, 0u, label, TEXT_HL_LABEL);
    rest(sp, s, len, label, words);
}

/**
 * @brief Bytes of a JSON number at @p i.
 *
 * @param s   The line.
 * @param len Its bytes.
 * @param i   Where it would start.
 * @return Bytes; 0 when no number starts here.
 */
static size_t json_number(const unsigned char *s, size_t len, size_t i)
{
    size_t j = i;

    if (s[j] == '-') {
        j++;
    }
    if (j >= len || !is_digit(s[j])) {
        return 0u;
    }
    while (j < len && is_digit(s[j])) {
        j++;
    }
    if (j + 1u < len && s[j] == '.' && is_digit(s[j + 1u])) {
        j++;
        while (j < len && is_digit(s[j])) {
            j++;
        }
    }
    if (j < len && (s[j] == 'e' || s[j] == 'E')) {
        size_t k = j + 1u;
        if (k < len && (s[k] == '+' || s[k] == '-')) {
            k++;
        }
        if (k < len && is_digit(s[k])) {
            j = k;
            while (j < len && is_digit(s[j])) {
                j++;
            }
        }
    }
    return j - i;
}

/**
 * @brief The spans of a JSON line.
 *
 * @param sp  The spans.
 * @param s   The line.
 * @param len Its bytes.
 */
static void json_line(spans_t *sp, const unsigned char *s, size_t len)
{
    static const char *const k_lit[3] = {"true", "false", "null"};
    size_t i = 0u;

    while (i < len) {
        const unsigned char b = s[i];
        size_t n = 1u;
        text_hl_class_t cls = TEXT_HL_TEXT;

        if (b != '\0' && strchr("{}[],:", b) != NULL) {
            cls = TEXT_HL_PUNCT;
        } else if (b == '"') {
            size_t j = i + 1u;
            while (j < len && s[j] != '"') {
                j += (s[j] == '\\') ? 2u : 1u;
            }
            j = (j < len) ? j + 1u : len;
            n = j - i;
            while (j < len && is_blank(s[j])) {
                j++;
            }
            cls = (j < len && s[j] == ':') ? TEXT_HL_KEY : TEXT_HL_TEXT;
        } else if (json_number(s, len, i) > 0u) {
            n = json_number(s, len, i);
            cls = TEXT_HL_NUMBER;
        } else {
            n = 1u;
            for (size_t k = 0u; k < 3u; ++k) {
                const size_t wl = strlen(k_lit[k]);
                if (wl <= len - i && memcmp(s + i, k_lit[k], wl) == 0 &&
                    (i == 0u || !is_word(s[i - 1u])) &&
                    (i + wl == len || !is_word(s[i + wl]))) {
                    n = wl;
                    cls = TEXT_HL_LITERAL;
                }
            }
        }
        emit(sp, i, n, cls);
        i += n;
    }
}

/**
 * @brief The spans of a CSV line.
 *
 * @param sp      The spans.
 * @param s       The line.
 * @param len     Its bytes.
 * @param line_no Its index in the file: 0 is the header.
 */
static void csv_line(spans_t *sp, const unsigned char *s, size_t len,
                     size_t line_no)
{
    const unsigned char delim = (memchr(s, ';', len) != NULL) ? ';' : ',';
    const text_hl_class_t cell = (line_no == 0u) ? TEXT_HL_CSV_HEAD
                                                 : TEXT_HL_TEXT;

    for (size_t i = 0u; i < len; ++i) {
        emit(sp, i, 1u, (s[i] == delim) ? TEXT_HL_PUNCT : cell);
    }
}

size_t text_hl_line(const char *line, size_t len, size_t line_no,
                    text_hl_kind_t kind, const text_hl_words_t *words,
                    text_hl_span_t *out, size_t cap)
{
    spans_t sp = {out, cap, 0u, false};
    const unsigned char *s = (const unsigned char *)line;

    if (line == NULL || out == NULL || cap == 0u || len == 0u) {
        return 0u;
    }
    switch (kind) {
    case TEXT_HL_KIND_TEXT:
        text_line(&sp, s, len, line_no, words);
        break;
    case TEXT_HL_KIND_JSON:
        json_line(&sp, s, len);
        break;
    case TEXT_HL_KIND_CSV:
        csv_line(&sp, s, len, line_no);
        break;
    default:
        emit(&sp, 0u, len, TEXT_HL_TEXT);
        break;
    }
    return sp.n;
}

/**
 * @brief Whether the @p n bytes at @p s are @p ext without regard to case.
 *
 * @param s   The bytes.
 * @param n   Their count.
 * @param ext An extension in lower case, with its dot.
 * @return true when they are the same.
 */
static bool same_ext(const char *s, size_t n, const char *ext)
{
    if (n != strlen(ext)) {
        return false;
    }
    for (size_t i = 0u; i < n; ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != ext[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Where the extension of the first @p n bytes of @p name starts.
 *
 * @param name The name.
 * @param n    Bytes to look at.
 * @return The offset of its last ".", or @p n without one.
 */
static size_t ext_at(const char *name, size_t n)
{
    size_t i = n;

    while (i > 0u && name[i - 1u] != '.' && name[i - 1u] != '/') {
        i--;
    }
    return (i > 0u && name[i - 1u] == '.') ? i - 1u : n;
}

text_hl_kind_t text_hl_kind_of(const char *name)
{
    if (name == NULL) {
        return TEXT_HL_KIND_PLAIN;
    }
    size_t n = strlen(name);
    size_t e = ext_at(name, n);
    if (same_ext(name + e, n - e, ".new")) {
        n = e;
        e = ext_at(name, n);
    }
    if (same_ext(name + e, n - e, ".txt")) {
        return TEXT_HL_KIND_TEXT;
    }
    if (same_ext(name + e, n - e, ".json")) {
        return TEXT_HL_KIND_JSON;
    }
    if (same_ext(name + e, n - e, ".csv")) {
        return TEXT_HL_KIND_CSV;
    }
    return TEXT_HL_KIND_PLAIN;
}
