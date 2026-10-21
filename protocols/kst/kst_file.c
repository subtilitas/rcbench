/**
 * @file
 * @brief KST servo configuration as a text file: write and read.
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_file.h"

#include <string.h>

/** @brief Start of the first line, before the format number. */
#define HEADER      "KST servo configuration, format "
/** @brief Start of the servo type line. */
#define TYPE_LINE   "Type: "
/** @brief Keyword that starts the register count line. */
#define REGISTERS   "registers"
/** @brief Keyword that starts the checksum line. */
#define CHECKSUM    "checksum"
/** @brief Suffix on a field line that is not editable. */
#define NOT_LOADED  " [not loaded]"
/** @brief Length of the string literal @p s, without its terminator. */
#define STR_LEN(s)  (sizeof(s) - 1u)

/** @brief Registers on each line of the register block. */
#define REGS_PER_LINE  8u
/** @brief Lines of the register block. */
#define REG_LINES      (KST_REG_COUNT / REGS_PER_LINE)
/** @brief "00:" and 8 times " xx". */
#define REG_LINE_LEN   (3u + 3u * REGS_PER_LINE)
/** @brief "checksum", a space and 8 digits. */
#define CHECKSUM_LEN   (STR_LEN(CHECKSUM) + 9u)

/**
 * @brief A number in a file saturates here; no field encodes a value this high.
 */
#define NUMBER_MAX 9999999u

/**
 * @brief The byte order mark of UTF-8 (8-bit Unicode Transformation Format),
 *        which some editors put in front of a text.
 */
#define BOM "\xEF\xBB\xBF"

/** @brief Upper-case hex digits, indexed by a nibble value. */
static const char k_hex[] = "0123456789ABCDEF";

/* --- checksum ------------------------------------------------------------- */

/** @brief Initial value of the CRC-32 register. */
#define CRC_START 0xFFFFFFFFu

/** @brief CRC-32 of IEEE 802.3, 4 bits a step. */
static uint32_t crc_add(uint32_t crc, unsigned byte)
{
    static const uint32_t table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };

    crc ^= byte;
    crc = (crc >> 4) ^ table[crc & 0x0Fu];
    crc = (crc >> 4) ^ table[crc & 0x0Fu];
    return crc;
}

/* --- formatter ------------------------------------------------------------ */

/**
 * @brief The text as it grows.
 *
 * A byte past the buffer is counted and not written, so the length tells an
 * overflow at the end.
 */
typedef struct {
    char *out;
    size_t cap;
    size_t n;
} buf_t;

/**
 * @brief Append one character to a buffer.
 *
 * Characters past the capacity are counted but not stored.
 *
 */
static void put_char(buf_t *b, char c)
{
    if (b->n < b->cap) {
        b->out[b->n] = c;
    }
    b->n++;
}

/**
 * @brief Append a string to a buffer.
 *
 */
static void put_str(buf_t *b, const char *s)
{
    while (*s != '\0') {
        put_char(b, *s);
        s++;
    }
}

/**
 * @brief Append an unsigned decimal number to a buffer.
 *
 */
static void put_uint(buf_t *b, uint32_t v)
{
    char digits[10];
    unsigned n = 0;

    do {
        digits[n] = (char)('0' + (int)(v % 10u));
        n++;
        v /= 10u;
    } while (v != 0u);
    while (n > 0u) {
        n--;
        put_char(b, digits[n]);
    }
}

/**
 * @brief Append the low digits of a value as upper-case hexadecimal digits.
 *
 */
static void put_hex(buf_t *b, uint32_t v, unsigned digits)
{
    while (digits > 0u) {
        digits--;
        put_char(b, k_hex[(v >> (4u * digits)) & 0x0Fu]);
    }
}

/** @brief One line of the register block, without its line end. */
static void put_row(buf_t *b, const uint8_t *regs, unsigned row)
{
    put_hex(b, row * REGS_PER_LINE, 2);
    put_char(b, ':');
    for (unsigned i = 0; i < REGS_PER_LINE; ++i) {
        put_char(b, ' ');
        put_hex(b, regs[i], 2);
    }
}

/**
 * @brief Check whether a field's unit has a tenths digit.
 *
 */
static bool has_tenths(const kst_field_t *f)
{
    return f->unit == KST_UNIT_TENTH_US || f->unit == KST_UNIT_TENTH_PERCENT ||
           f->unit == KST_UNIT_TENTH_S;
}

/**
 * @brief What follows the number of a field's value, terminated: "us", "%",
 *        "s", "of 12" or nothing.
 *
 * @p out holds 8 characters.
 */
static void unit_text(const kst_field_t *f, char *out)
{
    buf_t b = {out, 7, 0};

    switch (f->unit) {
    case KST_UNIT_TENTH_US:
        put_str(&b, "us");
        break;
    case KST_UNIT_PERCENT:
    case KST_UNIT_TENTH_PERCENT:
        put_char(&b, '%');
        break;
    case KST_UNIT_TENTH_S:
        put_char(&b, 's');
        break;
    case KST_UNIT_COUNT_OF:
        put_str(&b, "of ");
        put_uint(&b, (uint32_t)f->lo_bits + f->hi_bits);
        break;
    default:
        break;
    }
    out[b.n] = '\0';
}

/**
 * @brief Append the displayed value of a field, with its unit.
 *
 */
static void put_value(buf_t *b, kst_field_id_t id, const kst_field_t *f,
                      uint16_t raw)
{
    int32_t shown = 0;
    uint32_t v;
    char unit[8];

    if (!kst_field_display(id, raw, &shown)) {
        put_str(b, "raw ");
        put_uint(b, raw);
        put_str(b, ", no displayed value");
        return;
    }
    /* No conversion gives a negative value. */
    v = (uint32_t)shown;
    if (f->unit == KST_UNIT_FACTOR) {
        put_char(b, 'x');
    }
    if (has_tenths(f)) {
        put_uint(b, v / 10u);
        put_char(b, '.');
        put_uint(b, v % 10u);
    } else {
        put_uint(b, v);
    }
    unit_text(f, unit);
    if (unit[0] != '\0') {
        put_char(b, ' ');
        put_str(b, unit);
    }
}

size_t kst_file_format(const kst_image_t *img, char *out, size_t out_size)
{
    buf_t b;
    uint32_t crc = CRC_START;

    if (img == NULL || out == NULL) {
        return 0;
    }
    b.out = out;
    b.cap = out_size;
    b.n = 0;

    put_str(&b, HEADER);
    put_uint(&b, KST_FILE_FORMAT);
    put_char(&b, '\n');
    put_str(&b, TYPE_LINE);
    put_str(&b, kst_type_name(kst_type(img)));
    put_char(&b, '\n');
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);

        if (f->name[0] == '\0') {
            continue;
        }
        put_str(&b, f->name);
        put_str(&b, " = ");
        put_value(&b, id, f, kst_field_get(img, id));
        if (f->access != KST_ACCESS_EDITABLE) {
            put_str(&b, NOT_LOADED);
        }
        put_char(&b, '\n');
    }
    put_str(&b, REGISTERS " ");
    put_uint(&b, KST_REG_COUNT);
    put_char(&b, '\n');
    for (unsigned row = 0; row < REG_LINES; ++row) {
        put_row(&b, &img->r[(size_t)row * REGS_PER_LINE], row);
        put_char(&b, '\n');
    }
    if (b.n > b.cap) {
        return 0;
    }
    for (size_t i = 0; i < b.n; ++i) {
        crc = crc_add(crc, (unsigned char)out[i]);
    }
    put_str(&b, CHECKSUM " ");
    put_hex(&b, ~crc, 8);
    put_char(&b, '\n');
    return b.n > b.cap ? 0 : b.n;
}

/* --- reading: characters and lines ---------------------------------------- */

/**
 * @brief Check whether a character is a space or a tab.
 */
static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

/**
 * @brief Convert an ASCII upper-case letter to lower case.
 *
 * Other characters are returned unchanged.
 *
 */
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/**
 * @brief Whether @p s is @p ref, compared without case and without the spaces
 *        and tabs in either.
 */
static bool loose_eq(const char *s, size_t n, const char *ref)
{
    size_t i = 0;

    for (;;) {
        while (i < n && is_blank(s[i])) {
            i++;
        }
        while (*ref == ' ') {
            ref++;
        }
        if (i == n || *ref == '\0') {
            return i == n && *ref == '\0';
        }
        if (lower(s[i]) != lower(*ref)) {
            return false;
        }
        i++;
        ref++;
    }
}

/**
 * @brief Skip leading spaces and tabs of a string.
 *
 */
static void trim(const char **s, size_t *n)
{
    while (*n > 0u && is_blank((*s)[0])) {
        (*s)++;
        (*n)--;
    }
    while (*n > 0u && is_blank((*s)[*n - 1u])) {
        (*n)--;
    }
}

/**
 * @brief Decimal digits from @p s[*i] on.
 *
 * False without a digit.
 */
static bool number(const char *s, size_t n, size_t *i, uint32_t *v)
{
    const size_t start = *i;

    *v = 0;
    while (*i < n && s[*i] >= '0' && s[*i] <= '9') {
        if (*v <= NUMBER_MAX / 10u) {
            *v = *v * 10u + (uint32_t)(s[*i] - '0');
        } else {
            *v = NUMBER_MAX;
        }
        (*i)++;
    }
    return *i > start;
}

/** @brief 2 hexadecimal digits of either case from @p s[*i] on. */
static bool hex2(const char *s, size_t n, size_t *i, unsigned *v)
{
    unsigned value = 0;

    if (n - *i < 2u) {
        return false;
    }
    for (unsigned k = 0; k < 2u; ++k) {
        const char c = lower(s[*i + k]);

        if (c >= '0' && c <= '9') {
            value = value * 16u + (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value = value * 16u + (unsigned)(c - 'a') + 10u;
        } else {
            return false;
        }
    }
    *i += 2u;
    *v = value;
    return true;
}

/** @brief Reading position in the file text. */
typedef struct {
    const char *text;   /**< the file text */
    size_t len;         /**< bytes in text */
    size_t pos;         /**< start of the next line */
    unsigned line;      /**< number of the line asked for last, from 1 */
} cur_t;

/** @brief One line of the file text, without its line end. */
typedef struct {
    const char *s;      /**< first byte of the line */
    size_t n;           /**< bytes without the line end */
    bool cr;            /**< the line ends with CR LF */
    bool open;          /**< the text ends in this line, without a line end */
} line_t;

/**
 * @brief The next line: a complete one, or with @c open the bytes behind the
 *        last line end.
 *
 * False at the end of the text.
 */
static bool next_line(cur_t *c, line_t *l)
{
    const char *start = c->text + c->pos;
    const size_t left = c->len - c->pos;
    const char *lf;

    if (left == 0u) {
        return false;
    }
    lf = memchr(start, '\n', left);
    c->line++;
    l->s = start;
    l->open = lf == NULL;
    l->n = l->open ? left : (size_t)(lf - start);
    c->pos += l->open ? left : l->n + 1u;
    l->cr = !l->open && l->n > 0u && start[l->n - 1u] == '\r';
    if (l->cr) {
        l->n--;
    }
    return true;
}

/** @brief Whether a line holds spaces and tabs only, or nothing. */
static bool blank_line(const line_t *l)
{
    for (size_t i = 0; i < l->n; ++i) {
        if (!is_blank(l->s[i])) {
            return false;
        }
    }
    return true;
}

/** @brief Whether the first word of a line is "checksum", in any case. */
static bool is_checksum_line(const char *s, size_t n)
{
    size_t w = 0;

    while (n > 0u && is_blank(s[0])) {
        s++;
        n--;
    }
    while (w < n && !is_blank(s[w]) && s[w] != '=' && s[w] != ':') {
        w++;
    }
    return w == STR_LEN(CHECKSUM) && loose_eq(s, w, CHECKSUM);
}

/**
 * @brief Check whether a text has a line whose first word is "checksum".
 *
 */
static bool has_checksum_line(const char *text, size_t len)
{
    cur_t c = {text, len, 0, 0};
    line_t l;

    while (next_line(&c, &l)) {
        if (is_checksum_line(l.s, l.n)) {
            return true;
        }
    }
    return false;
}

/** @brief Line 1: the header and a version this code reads. */
static kst_file_err_t read_head(cur_t *c)
{
    const size_t have = c->len < STR_LEN(HEADER) ? c->len : STR_LEN(HEADER);
    line_t l;
    size_t i = STR_LEN(HEADER);
    uint32_t version = 0;

    if (memcmp(c->text, HEADER, have) != 0) {
        c->line = 1;
        return KST_FILE_ERR_NOT_KST;
    }
    if (!next_line(c, &l) || l.open) {
        c->line = 1;
        return KST_FILE_ERR_TRUNCATED;
    }
    if (!number(l.s, l.n, &i, &version) || i != l.n || version == 0u) {
        return KST_FILE_ERR_VERSION;
    }
    if (version > KST_FILE_FORMAT) {
        return KST_FILE_ERR_NEWER;
    }
    return KST_FILE_OK;
}

/** @brief "V6" or "V8" in either case. */
static kst_type_t type_value(const char *s, size_t n)
{
    if (loose_eq(s, n, "V6")) {
        return KST_TYPE_V6;
    }
    if (loose_eq(s, n, "V8")) {
        return KST_TYPE_V8;
    }
    return KST_TYPE_NONE;
}

/** @brief The number after "registers". */
static kst_file_err_t register_count(const char *s, size_t n)
{
    size_t i = 0;
    uint32_t count = 0;

    if (!number(s, n, &i, &count) || i != n) {
        return KST_FILE_ERR_MALFORMED;
    }
    return count == KST_REG_COUNT ? KST_FILE_OK : KST_FILE_ERR_REG_COUNT;
}

/**
 * @brief One line of the register block into its 8 registers.
 *
 * @p strict takes the line as kst_file_format() writes it and no other.
 */
static bool register_row(const char *s, size_t n, unsigned row, bool strict,
                         kst_image_t *img)
{
    uint8_t regs[REGS_PER_LINE];
    char written[REG_LINE_LEN];
    buf_t b = {written, sizeof(written), 0};
    size_t i = 0;
    unsigned v = 0;

    if (!hex2(s, n, &i, &v) || v != row * REGS_PER_LINE || i == n ||
        s[i] != ':') {
        return false;
    }
    i++;
    for (unsigned k = 0; k < REGS_PER_LINE; ++k) {
        while (i < n && is_blank(s[i])) {
            i++;
        }
        if (!hex2(s, n, &i, &v)) {
            return false;
        }
        regs[k] = (uint8_t)v;
    }
    if (i != n) {
        return false;
    }
    put_row(&b, regs, row);
    if (strict && (n != REG_LINE_LEN || memcmp(s, written, n) != 0)) {
        return false;
    }
    memcpy(&img->r[(size_t)row * REGS_PER_LINE], regs, sizeof(regs));
    return true;
}

/* --- reading: a file with a checksum line --------------------------------- */

/**
 * @brief Check whether a line equals the checksum line for a CRC value.
 */
static bool checksum_fits(const line_t *l, uint32_t crc)
{
    char written[CHECKSUM_LEN];
    buf_t b = {written, sizeof(written), 0};

    put_str(&b, CHECKSUM " ");
    put_hex(&b, crc, 8);
    return l->n == CHECKSUM_LEN && memcmp(l->s, written, CHECKSUM_LEN) == 0;
}

/**
 * @brief Add a line and its end as one LF.
 *
 * False for a line longer than the format has and for a character outside 0x20
 * to 0x7E.
 */
static bool crc_line(const line_t *l, uint32_t *crc)
{
    if (l->n > KST_FILE_LINE_MAX) {
        return false;
    }
    for (size_t i = 0; i < l->n; ++i) {
        const unsigned ch = (unsigned char)l->s[i];

        if (ch < 0x20u || ch > 0x7Eu) {
            return false;
        }
        *crc = crc_add(*crc, ch);
    }
    *crc = crc_add(*crc, '\n');
    return true;
}

/**
 * @brief The caller found a line whose first word is "checksum".
 *
 * Every state below ends at that line or refuses before it, so the loop leaves
 * with the checksum compared.
 *
 * Behind the checksum line only blank lines pass.  The checksum line is
 * the one line taken without its line end: its 17 characters show it
 * whole.
 */
static kst_file_err_t read_verified(cur_t *c, kst_file_t *f)
{
    enum { S_HEAD, S_TYPE, S_FIELDS, S_ROWS, S_CHECK, S_DONE } state = S_HEAD;
    line_t l;
    uint32_t crc = CRC_START;
    unsigned row = 0;
    bool crlf = false;
    kst_type_t stated = KST_TYPE_NONE;
    kst_file_err_t err;

    c->pos = 0;
    c->line = 0;
    while (next_line(c, &l)) {
        if (state == S_DONE) {
            if (!blank_line(&l)) {
                return KST_FILE_ERR_TRAILING;
            }
            continue;
        }
        if (l.open) {
            if (state != S_CHECK || l.n != CHECKSUM_LEN) {
                return KST_FILE_ERR_TRUNCATED;
            }
        } else {
            if (state == S_HEAD) {
                crlf = l.cr;
            }
            if (l.cr != crlf) {
                return KST_FILE_ERR_LINE_END;
            }
        }
        if (state == S_CHECK) {
            if (!checksum_fits(&l, ~crc)) {
                return KST_FILE_ERR_CHECKSUM;
            }
            state = S_DONE;
            continue;
        }
        if (!crc_line(&l, &crc)) {
            return KST_FILE_ERR_MALFORMED;
        }
        switch (state) {
        case S_HEAD:
            state = S_TYPE;
            break;
        case S_TYPE:
            if (l.n != STR_LEN(TYPE_LINE) + 2u ||
                memcmp(l.s, TYPE_LINE "V", STR_LEN(TYPE_LINE) + 1u) != 0) {
                return KST_FILE_ERR_MALFORMED;
            }
            stated = type_value(l.s + STR_LEN(TYPE_LINE), 2);
            if (stated == KST_TYPE_NONE) {
                return KST_FILE_ERR_MALFORMED;
            }
            state = S_FIELDS;
            break;
        case S_FIELDS:
            if (is_checksum_line(l.s, l.n)) {
                return KST_FILE_ERR_MALFORMED;
            }
            if (l.n > STR_LEN(REGISTERS) &&
                memcmp(l.s, REGISTERS " ", STR_LEN(REGISTERS) + 1u) == 0) {
                err = register_count(l.s + STR_LEN(REGISTERS) + 1u,
                                     l.n - STR_LEN(REGISTERS) - 1u);
                if (err != KST_FILE_OK) {
                    return err;
                }
                state = S_ROWS;
            }
            break;
        default: /* S_ROWS */
            if (!register_row(l.s, l.n, row, true, &f->image)) {
                return KST_FILE_ERR_MALFORMED;
            }
            row++;
            if (row == REG_LINES) {
                state = S_CHECK;
            }
            break;
        }
    }
    if (stated != kst_type(&f->image)) {
        c->line = 2;
        return KST_FILE_ERR_TYPE;
    }
    f->kind = KST_FILE_VERIFIED;
    f->type = stated;
    return KST_FILE_OK;
}

/* --- reading: a file without a checksum line ------------------------------ */

/** @brief State of a write that applies edits to a file's lines. */
typedef struct {
    kst_file_t *f;
    unsigned rows_left;   /**< lines of the register block still to come */
    bool has_block;
    kst_type_t stated;    /**< by the type line */
    unsigned type_line;   /**< 0: the file has none */
    /**
     * @brief Per named field without "raw": the number as written, in the unit
     *        of kst_field_display().
     */
    int32_t typed[KST_F_COUNT];
} edit_t;

/** @brief Whether a raw value of the field displays as @p display. */
static bool shows(kst_field_id_t id, int32_t display)
{
    const uint32_t max = kst_field_raw_max(id);
    int32_t shown = 0;

    for (uint32_t raw = 0; raw <= max; ++raw) {
        if (kst_field_display(id, (uint16_t)raw, &shown) && shown == display) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The value of field @p id as a person writes it.
 *
 * @p typed receives the number of a line without "raw".  @p closed is set when
 * the line shows its own end: a unit, a "[...]" or a remark behind the number.
 * A line that ends in the digits of its number does not: a text cut inside that
 * number reads the same.
 */
static bool field_value(kst_field_id_t id, const char *s, size_t n,
                        kst_file_t *f, int32_t *typed, bool *closed)
{
    const kst_field_t *fd = kst_field(id);
    const bool editable = fd->access == KST_ACCESS_EDITABLE;
    char unit[8];
    size_t i = 0;
    uint32_t v = 0;
    uint32_t tenth = 0;
    uint16_t raw = 0;

    if (n > 0u && s[n - 1u] == ']') {
        /* A trailing "[...]", as kst_file_format() writes behind a field
         * no load takes. */
        while (n > 0u && s[n - 1u] != '[') {
            n--;
        }
        if (n == 0u) {
            return false;
        }
        n--;
        trim(&s, &n);
        *closed = true;
    }
    if (n > 3u && loose_eq(s, 3, "raw") && is_blank(s[3])) {
        i = 3;
        while (i < n && is_blank(s[i])) {
            i++;
        }
        /* What follows a comma is a remark. */
        if (!number(s, n, &i, &v) || (i != n && s[i] != ',') ||
            v > kst_field_raw_max(id)) {
            return false;
        }
        f->as_raw |= KST_FIELD_BIT(id);
        f->value[id] = (int32_t)v;
        *closed = *closed || i != n;
        return true;
    }
    if (fd->unit == KST_UNIT_FACTOR && n > 0u && lower(s[0]) == 'x') {
        i = 1;
        while (i < n && is_blank(s[i])) {
            i++;
        }
    }
    if (!number(s, n, &i, &v)) {
        return false;
    }
    if (has_tenths(fd)) {
        v *= 10u;
        if (i < n && (s[i] == '.' || s[i] == ',')) {
            i++;
            if (i == n || s[i] < '0' || s[i] > '9') {
                return false;
            }
            tenth = (uint32_t)(s[i] - '0');
            i++;
        }
    }
    unit_text(fd, unit);
    if (i != n) {
        if (!loose_eq(s + i, n - i, unit)) {
            return false;
        }
        *closed = true;
    }
    *typed = (int32_t)(v + tenth);
    if (!editable) {
        /* A field no load takes keeps its displayed value: prot1_time and
         * prot1_pwm have no conversion back.  The value is one the field
         * shows for some raw value, or the line is refused. */
        if (!shows(id, (int32_t)(v + tenth))) {
            return false;
        }
        f->value[id] = (int32_t)(v + tenth);
        return true;
    }
    if (!kst_field_raw_from_display(id, (int32_t)(v + tenth), &raw)) {
        return false;
    }
    f->value[id] = raw;
    return true;
}

/**
 * @brief A line that is not empty, not a comment and not in the register block.
 *
 * @p closed: see field_value(); the other lines show their end by their form.
 */
static kst_file_err_t edited_line(edit_t *e, const char *s, size_t n,
                                  unsigned line, bool *closed)
{
    const char *value;
    size_t name_len = 0;
    size_t value_len;

    *closed = true;
    while (name_len < n && s[name_len] != '=' && s[name_len] != ':') {
        name_len++;
    }
    if (name_len == n) {
        if (n <= STR_LEN(REGISTERS) || !is_blank(s[STR_LEN(REGISTERS)]) ||
            !loose_eq(s, STR_LEN(REGISTERS), REGISTERS)) {
            return KST_FILE_ERR_MALFORMED;
        }
        if (e->has_block) {
            return KST_FILE_ERR_DUPLICATE;
        }
        s += STR_LEN(REGISTERS);
        n -= STR_LEN(REGISTERS);
        trim(&s, &n);
        if (register_count(s, n) != KST_FILE_OK) {
            return register_count(s, n);
        }
        e->has_block = true;
        e->rows_left = REG_LINES;
        return KST_FILE_OK;
    }
    value = s + name_len + 1u;
    value_len = n - name_len - 1u;
    trim(&value, &value_len);
    if (loose_eq(s, name_len, "type")) {
        if (e->type_line != 0u) {
            return KST_FILE_ERR_DUPLICATE;
        }
        e->stated = type_value(value, value_len);
        e->type_line = line;
        return e->stated == KST_TYPE_NONE ? KST_FILE_ERR_VALUE : KST_FILE_OK;
    }
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *fd = kst_field(id);

        if (fd->name[0] == '\0' || !loose_eq(s, name_len, fd->name)) {
            continue;
        }
        if ((e->f->named & KST_FIELD_BIT(id)) != 0u) {
            return KST_FILE_ERR_DUPLICATE;
        }
        *closed = false;
        if (!field_value(id, value, value_len, e->f, &e->typed[id],
                         closed)) {
            return KST_FILE_ERR_VALUE;
        }
        e->f->named |= KST_FIELD_BIT(id);
        e->f->line[id] = (uint16_t)line;
        return KST_FILE_OK;
    }
    return KST_FILE_ERR_NAME;
}

/**
 * @brief An editable field whose line shows what the register block holds takes
 *        the block's raw value: several raw values of Max.
 *
 * Duty show as one percentage, and the line alone gives the one its number
 * converts to.
 */
static void take_block_values(const edit_t *e, kst_file_t *f)
{
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_set_t bit = KST_FIELD_BIT(id);
        const uint16_t raw = kst_field_get(&f->image, id);
        int32_t shown = 0;

        if (kst_field(id)->access == KST_ACCESS_EDITABLE &&
            (f->named & bit) != 0u && (f->as_raw & bit) == 0u &&
            kst_field_display(id, raw, &shown) && shown == e->typed[id]) {
            f->value[id] = raw;
            f->as_raw |= bit;
        }
    }
}

/**
 * @brief A text may end without a line end, as an editor leaves it.
 *
 * The last line is then taken when it shows its own end, and the text is
 * refused as truncated when it does not or when the line is not read: a text
 * cut inside a line never gives a shortened number.
 */
static kst_file_err_t read_edited(cur_t *c, kst_file_t *f)
{
    edit_t e;
    line_t l;
    kst_file_err_t err;

    memset(&e, 0, sizeof(e));
    e.f = f;
    e.stated = KST_TYPE_NONE;
    while (next_line(c, &l)) {
        const char *s = l.s;
        size_t n = 0;
        bool closed = false;

        /* A comment may hold any byte; the rest of a line is ASCII. */
        while (n < l.n && s[n] != '#') {
            const unsigned ch = (unsigned char)s[n];

            if ((ch < 0x20u && ch != '\t') || ch > 0x7Eu) {
                return l.open ? KST_FILE_ERR_TRUNCATED
                              : KST_FILE_ERR_MALFORMED;
            }
            n++;
        }
        /* A comment is a person's and ends where the text does. */
        closed = n < l.n;
        trim(&s, &n);
        if (n == 0u) {
            continue;
        }
        if (e.rows_left > 0u) {
            if (!register_row(s, n, REG_LINES - e.rows_left, false,
                              &f->image)) {
                return l.open ? KST_FILE_ERR_TRUNCATED
                              : KST_FILE_ERR_MALFORMED;
            }
            e.rows_left--;
            continue;
        }
        {
            bool by_form = false;

            err = edited_line(&e, s, n, c->line, &by_form);
            closed = closed || by_form;
        }
        if (l.open && (err != KST_FILE_OK || !closed)) {
            return KST_FILE_ERR_TRUNCATED;
        }
        if (err != KST_FILE_OK) {
            return err;
        }
    }
    if (e.rows_left > 0u) {
        /* The line the block's next row belongs in. */
        if (c->pos == 0u || c->text[c->pos - 1u] == '\n') {
            c->line++;
        }
        return KST_FILE_ERR_TRUNCATED;
    }
    if (e.has_block) {
        const kst_type_t block = kst_type(&f->image);

        if (e.type_line != 0u && e.stated != block) {
            c->line = e.type_line;
            return KST_FILE_ERR_TYPE;
        }
        e.stated = block;
        take_block_values(&e, f);
    }
    f->type = e.stated;
    f->kind = e.stated == KST_TYPE_NONE ? KST_FILE_EDITED_UNTYPED
                                        : KST_FILE_EDITED_TYPED;
    return KST_FILE_OK;
}

kst_file_err_t kst_file_parse(const char *text, size_t len, kst_file_t *out,
                              unsigned *line)
{
    cur_t c = {text, len, 0, 0};
    kst_file_t f;
    kst_file_err_t err;

    if (line != NULL) {
        *line = 0;
    }
    if (text == NULL || out == NULL) {
        return KST_FILE_ERR_ARG;
    }
    if (len > KST_FILE_MAX) {
        return KST_FILE_ERR_OVERSIZED;
    }
    if (len >= STR_LEN(BOM) && memcmp(text, BOM, STR_LEN(BOM)) == 0) {
        c.text += STR_LEN(BOM);
        c.len -= STR_LEN(BOM);
    }
    memset(&f, 0, sizeof(f));
    f.type = KST_TYPE_NONE;
    err = read_head(&c);
    if (err == KST_FILE_OK) {
        err = has_checksum_line(c.text, c.len) ? read_verified(&c, &f)
                                               : read_edited(&c, &f);
    }
    if (err != KST_FILE_OK) {
        if (line != NULL) {
            *line = c.line;
        }
        return err;
    }
    *out = f;
    return KST_FILE_OK;
}

/* --- a file against a servo ----------------------------------------------- */

/** @brief Whether the file gives a value for the field. */
static bool gives(const kst_file_t *file, kst_field_id_t id)
{
    return file->kind == KST_FILE_VERIFIED ||
           (file->named & KST_FIELD_BIT(id)) != 0u;
}

/** @brief Whether the file's value of a field is the value @p img holds. */
static bool holds(const kst_file_t *file, kst_field_id_t id,
                  const kst_image_t *img)
{
    const uint16_t have = kst_field_get(img, id);
    const bool editable = kst_field(id)->access == KST_ACCESS_EDITABLE;
    int32_t shown = 0;
    int32_t wanted = 0;

    if (file->kind == KST_FILE_VERIFIED) {
        return kst_field_get(&file->image, id) == have;
    }
    /* The value is raw for a field written "raw <n>" and for an editable
     * field, and displayed for a locked one: 7 of 12 is not raw 7. */
    if ((file->as_raw & KST_FIELD_BIT(id)) != 0u) {
        return file->value[id] == (int32_t)have;
    }
    (void)kst_field_display(id, have, &shown);
    if (!editable) {
        return file->value[id] == shown;
    }
    if (file->value[id] == (int32_t)have) {
        return true;
    }
    /* A number a person typed that the servo's value already shows changes
     * nothing: several raw values of Max. Duty show as one percentage. */
    return file->value[id] >= 0 && file->value[id] <= 0xFFFF &&
           kst_field_display(id, (uint16_t)file->value[id], &wanted) &&
           wanted == shown;
}

/** @brief The raw value a load sets an editable field to. */
static bool wanted_raw(const kst_file_t *file, kst_field_id_t id,
                       uint16_t *raw)
{
    if (file->kind == KST_FILE_VERIFIED) {
        *raw = kst_field_get(&file->image, id);
        return true;
    }
    if (file->value[id] < 0 || file->value[id] > 0xFFFF) {
        return false;
    }
    *raw = (uint16_t)file->value[id];
    return true;
}

/**
 * @brief The first field of the file, by line, whose value a broken hard rule
 *        needs: putting the servo's value back clears at least one of them.
 */
static kst_field_id_t blame(const kst_file_t *file, const kst_image_t *servo,
                            const kst_image_t *backup,
                            const kst_file_load_t *load, kst_rules_t hard)
{
    kst_field_id_t found = KST_F_COUNT;

    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        kst_image_t without = load->target;

        if ((load->editable & KST_FIELD_BIT(id)) == 0u) {
            continue;
        }
        (void)kst_field_edit(&without, id, kst_field_get(servo, id));
        if ((hard & ~kst_limits_edit(backup, &without, 0, NULL)) == 0u) {
            continue;
        }
        if (found == KST_F_COUNT || file->line[id] < file->line[found]) {
            found = id;
        }
    }
    return found;
}

kst_file_verdict_t kst_file_load(const kst_file_t *file,
                                 const kst_image_t *servo,
                                 const kst_image_t *backup,
                                 kst_file_load_t *out)
{
    kst_rules_t hard;

    if (file == NULL || servo == NULL || backup == NULL || out == NULL) {
        return KST_FILE_LOAD_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->kind = file->kind;
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;

        if (kst_field(id)->name[0] != '\0') {
            out->fields++;
            if (gives(file, id)) {
                out->named++;
            }
        }
    }
    out->file_type = file->type;
    out->servo_type = kst_type(servo);
    out->hard_field = KST_F_COUNT;
    out->target = *servo;
    if (file->type != KST_TYPE_NONE && file->type != out->servo_type) {
        out->verdict = KST_FILE_LOAD_TYPE_MISMATCH;
        return out->verdict;
    }
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        uint16_t raw = 0;

        if (kst_field(id)->access == KST_ACCESS_EDITABLE && gives(file, id) &&
            !holds(file, id, servo) && wanted_raw(file, id, &raw) &&
            kst_field_edit(&out->target, id, raw)) {
            out->editable |= KST_FIELD_BIT(id);
        }
    }
    /* After the edits: register 01 follows a loaded Max. Duty. */
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;

        if (kst_field(id)->access != KST_ACCESS_EDITABLE && gives(file, id) &&
            !holds(file, id, &out->target)) {
            out->skipped |= KST_FIELD_BIT(id);
        }
    }
    out->rules = kst_limits_edit(backup, &out->target, 0, NULL);
    hard = out->rules & KST_RULES_HARD;
    if (hard != 0u) {
        out->hard_field = blame(file, servo, backup, out, hard);
        if (out->hard_field != KST_F_COUNT) {
            out->hard_line = file->line[out->hard_field];
        }
        out->target = *servo;
        out->verdict = KST_FILE_LOAD_HARD_LIMIT;
    }
    return out->verdict;
}
