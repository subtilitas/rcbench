/**
 * @file openyge_file.c
 * @brief The parameter table of an OpenYGE ESC as a text file: the checksum,
 *        the text written, the text read and the load against the table read
 *        last; see openyge_file.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "openyge_file.h"

#include <string.h>

#include "openyge_map.h"

/** @brief The start value of the CRC-32 before its final inversion. */
#define CRC_START 0xFFFFFFFFu
/** @brief The header up to its version number. */
#define HEAD "YGE ESC settings, format "
/** @brief The first word of the checksum line. */
#define CK_WORD "checksum"
/** @brief Characters of a whole checksum line: the word, a space, 8 digits. */
#define CK_LINE 17u
/** @brief The key of an index the map has no field for. */
#define RAW_KEY "RAW"
/** @brief The last line of a file a person changed. */
#define EDITED "edited"
/** @brief The header lines before the first index line: header, type,
 *         firmware, serial, count. */
#define HEAD_LINES 5u

/** @brief Upper-case hexadecimal digits. */
static const char HEX[] = "0123456789ABCDEF";

/* --- checksum ------------------------------------------------------------- */

/**
 * @brief Add one byte to a running CRC-32, 4 bits a step.
 * @param crc the CRC so far, not inverted
 * @param b   the byte
 * @return the CRC with @p b
 */
static uint32_t crc_add(uint32_t crc, uint8_t b)
{
    static const uint32_t table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };

    crc ^= b;
    crc = (crc >> 4) ^ table[crc & 0x0Fu];
    crc = (crc >> 4) ^ table[crc & 0x0Fu];
    return crc;
}

/**
 * @brief Add a block to a running CRC-32.
 * @param crc the CRC so far, not inverted
 * @param p   the bytes
 * @param n   their count
 * @return the CRC with the block
 */
static uint32_t crc_block(uint32_t crc, const char *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        crc = crc_add(crc, (uint8_t)p[i]);
    }
    return crc;
}

uint32_t oy_file_crc32(const void *data, size_t n)
{
    if (data == NULL) {
        return 0u;
    }
    return ~crc_block(CRC_START, data, n);
}

/* --- writing -------------------------------------------------------------- */

/** @brief A text being written: bytes past @c cap are counted, not stored. */
typedef struct {
    char  *out; /**< the buffer           */
    size_t cap; /**< its bytes            */
    size_t n;   /**< bytes written so far */
} wbuf_t;

/**
 * @brief Add a character.
 * @param b the text
 * @param c the character
 */
static void put_char(wbuf_t *b, char c)
{
    if (b->n < b->cap) {
        b->out[b->n] = c;
    }
    b->n++;
}

/**
 * @brief Add a string.
 * @param b the text
 * @param s the string
 */
static void put_str(wbuf_t *b, const char *s)
{
    while (*s != '\0') {
        put_char(b, *s++);
    }
}

/**
 * @brief Add a number in decimal, at least @p width digits.
 * @param b     the text
 * @param v     the number
 * @param width the fewest digits; leading zeros fill
 */
static void put_dec(wbuf_t *b, uint32_t v, unsigned width)
{
    char d[10];
    unsigned k = 0;

    do {
        d[k++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    while (k < width && k < sizeof(d)) {
        d[k++] = '0';
    }
    while (k > 0u) {
        put_char(b, d[--k]);
    }
}

size_t oy_file_format(const uint16_t *table, uint16_t count, char *out,
                      size_t size)
{
    if (table == NULL || out == NULL || count < OY_FILE_COUNT_MIN
        || count > OPENYGE_MAX_PARAMS || table[0] != count) {
        return 0u;
    }
    wbuf_t b = { out, size, 0u };
    const uint16_t type = table[OY_I_TYPE];
    const char *name = oy_map_type_name(type);
    const uint32_t fw = oy_map_u32(table[OY_I_FW_LO], table[OY_I_FW_HI]);

    put_str(&b, HEAD);
    put_dec(&b, OY_FILE_FORMAT, 1u);
    put_str(&b, "\nType: ");
    put_dec(&b, type, 1u);
    if (name != NULL) {
        put_char(&b, ' ');
        put_str(&b, name);
    }
    put_str(&b, "\nFirmware: ");
    put_dec(&b, fw / 100000u, 1u);
    put_char(&b, '.');
    put_dec(&b, fw % 100000u, 5u);
    put_str(&b, "\nSerial: ");
    put_dec(&b, oy_map_u32(table[OY_I_SERIAL_LO], table[OY_I_SERIAL_HI]), 1u);
    put_str(&b, "\nCount: ");
    put_dec(&b, count, 1u);
    put_char(&b, '\n');
    for (uint16_t i = 0; i < count; ++i) {
        const oy_field_t *f = oy_map_field(i);

        put_dec(&b, i, 2u);
        put_char(&b, ' ');
        put_str(&b, f != NULL ? f->key : RAW_KEY);
        put_str(&b, " = ");
        put_dec(&b, table[i], 1u);
        put_char(&b, '\n');
    }
    if (b.n > size) {
        return 0u;
    }
    const uint32_t crc = oy_file_crc32(out, b.n);

    put_str(&b, CK_WORD " ");
    for (int sh = 28; sh >= 0; sh -= 4) {
        put_char(&b, HEX[(crc >> (unsigned)sh) & 0x0Fu]);
    }
    put_char(&b, '\n');
    return b.n <= size ? b.n : 0u;
}

bool oy_file_name(char *out, size_t size, unsigned n, uint16_t type)
{
    if (out == NULL || n == 0u || n > 999u) {
        return false;
    }
    wbuf_t b = { out, size, 0u };

    put_str(&b, OY_FILE_STEM);
    put_dec(&b, n, 3u);
    put_char(&b, '-');
    put_dec(&b, type, 1u);
    put_str(&b, ".TXT");
    put_char(&b, '\0');
    return b.n <= size;
}

/* --- reading -------------------------------------------------------------- */

/** @brief One line of a text, its line end taken off. */
typedef struct {
    const char *s;  /**< its first character       */
    size_t      n;  /**< its characters            */
    bool        lf; /**< it ends with LF           */
    bool        cr; /**< a CR stood before that LF */
} line_t;

/** @brief Walks a text line by line. */
typedef struct {
    const char *p;   /**< the text          */
    size_t      len; /**< its bytes         */
    size_t      pos; /**< the next line's offset */
} lines_t;

/**
 * @brief The next line.
 * @param it the walk
 * @param l  takes the line
 * @return false at the end of the text
 */
static bool next_line(lines_t *it, line_t *l)
{
    if (it->pos >= it->len) {
        return false;
    }
    const char *s = it->p + it->pos;
    const size_t rest = it->len - it->pos;
    const char *nl = memchr(s, '\n', rest);
    size_t n = nl != NULL ? (size_t)(nl - s) : rest;

    it->pos += nl != NULL ? n + 1u : n;
    l->lf = nl != NULL;
    l->cr = l->lf && n > 0u && s[n - 1u] == '\r';
    if (l->cr) {
        --n;
    }
    l->s = s;
    l->n = n;
    return true;
}

/**
 * @brief Whether a line holds spaces and tabs only, or nothing.
 * @param l the line
 * @return true when it does
 */
static bool blank(const line_t *l)
{
    for (size_t i = 0; i < l->n; ++i) {
        if (l->s[i] != ' ' && l->s[i] != '\t') {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a line holds characters 0x20 to 0x7E only.
 * @param l the line
 * @return true when it does
 */
static bool printable(const line_t *l)
{
    for (size_t i = 0; i < l->n; ++i) {
        if (l->s[i] < 0x20 || l->s[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a line is exactly @p lit.
 * @param s   the line's characters
 * @param n   their count
 * @param lit the text
 * @return true when it is
 */
static bool same(const char *s, size_t n, const char *lit)
{
    return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

/**
 * @brief Take @p lit off the front of a line.
 * @param l   the line; advanced past @p lit when it starts with it
 * @param lit the text
 * @return true when the line started with it
 */
static bool take(line_t *l, const char *lit)
{
    const size_t k = strlen(lit);

    if (l->n < k || memcmp(l->s, lit, k) != 0) {
        return false;
    }
    l->s += k;
    l->n -= k;
    return true;
}

/**
 * @brief A decimal number of at most 10 digits, no leading zero.
 * @param s   the digits
 * @param n   their count
 * @param max the largest value taken
 * @param v   takes the value
 * @return false for no digits, another character, a leading zero, or a value
 *         over @p max
 */
static bool number(const char *s, size_t n, uint32_t max, uint32_t *v)
{
    if (n == 0u || n > 10u || (n > 1u && s[0] == '0')) {
        return false;
    }
    uint64_t x = 0u;

    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        x = x * 10u + (uint64_t)(s[i] - '0');
    }
    if (x > max) {
        return false;
    }
    *v = (uint32_t)x;
    return true;
}

/**
 * @brief Whether a line's first word is `checksum`.
 * @param l the line
 * @return true when it is
 */
static bool checksum_line(const line_t *l)
{
    const size_t k = sizeof(CK_WORD) - 1u;

    return l->n >= k && memcmp(l->s, CK_WORD, k) == 0
           && (l->n == k || l->s[k] == ' ');
}

/**
 * @brief Whether a checksum line holds @p crc in 8 upper-case digits.
 * @param l   the line
 * @param crc the CRC-32 of the lines before it
 * @return true when it does
 */
static bool checksum_fits(const line_t *l, uint32_t crc)
{
    if (l->n != CK_LINE) {
        return false;
    }
    uint32_t v = 0u;

    for (size_t i = CK_LINE - 8u; i < CK_LINE; ++i) {
        const char c = l->s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A') + 10u;
        } else {
            return false;
        }
        v = (v << 4) | d;
    }
    return v == crc;
}

/**
 * @brief A refusal with its line.
 * @param line takes @p no, may be NULL
 * @param no   the line, 0 for none
 * @param e    the refusal
 * @return @p e
 */
static oy_file_err_t fail(unsigned *line, unsigned no, oy_file_err_t e)
{
    if (line != NULL) {
        *line = no;
    }
    return e;
}

/**
 * @brief Line 1: the header and its version.
 * @param l the line
 * @return OY_FILE_OK, OY_FILE_ERR_NOT_YGE, _VERSION or _NEWER
 */
static oy_file_err_t header(line_t l)
{
    uint32_t v = 0u;

    if (!take(&l, HEAD)) {
        return OY_FILE_ERR_NOT_YGE;
    }
    if (!number(l.s, l.n, 999u, &v) || v == 0u) {
        return OY_FILE_ERR_VERSION;
    }
    return v > OY_FILE_FORMAT ? OY_FILE_ERR_NEWER : OY_FILE_OK;
}

/**
 * @brief The walk over the lines: line ends, characters, lengths, the
 *        checksum, and what follows it.
 * @param it   the walk, standing at line 1
 * @param body takes the lines before the checksum line
 * @param line takes the line of a refusal
 * @return OY_FILE_OK when the checksum fits
 */
static oy_file_err_t frame(lines_t it, unsigned *body, unsigned *line)
{
    line_t l;
    unsigned no = 0u;
    int crlf = -1;
    bool found = false;
    bool edited = false;
    uint32_t crc = CRC_START;

    while (next_line(&it, &l)) {
        ++no;
        if (found) {
            if (!blank(&l)) {
                return fail(line, no, OY_FILE_ERR_TRAILING);
            }
            continue;
        }
        if (l.lf) {
            if (crlf < 0) {
                crlf = l.cr ? 1 : 0;
            } else if (crlf != (l.cr ? 1 : 0)) {
                return fail(line, no, OY_FILE_ERR_LINE_END);
            }
        }
        if (!printable(&l)) {
            return fail(line, no, OY_FILE_ERR_MALFORMED);
        }
        if (checksum_line(&l)) {
            if (!checksum_fits(&l, ~crc)) {
                return fail(line, no, OY_FILE_ERR_CHECKSUM);
            }
            found = true;
            *body = no - 1u;
            continue;
        }
        if (!blank(&l)) {
            edited = same(l.s, l.n, EDITED);
        }
        if (!l.lf) {
            break;
        }
        if (l.n > OY_FILE_LINE_MAX) {
            return fail(line, no, OY_FILE_ERR_MALFORMED);
        }
        crc = crc_block(crc, l.s, l.n);
        crc = crc_add(crc, (uint8_t)'\n');
    }
    if (!found) {
        return fail(line, 0u,
                    edited ? OY_FILE_ERR_EDITED : OY_FILE_ERR_TRUNCATED);
    }
    return OY_FILE_OK;
}

/**
 * @brief Line 2: `Type: <code>`, then the model name when the map has one.
 * @param l    the line
 * @param type takes the code
 * @return OY_FILE_OK, OY_FILE_ERR_MALFORMED or OY_FILE_ERR_MISMATCH
 */
static oy_file_err_t type_line(line_t l, uint16_t *type)
{
    uint32_t v = 0u;

    if (!take(&l, "Type: ")) {
        return OY_FILE_ERR_MALFORMED;
    }
    const char *sp = memchr(l.s, ' ', l.n);
    const size_t k = sp != NULL ? (size_t)(sp - l.s) : l.n;

    if (!number(l.s, k, 0xFFFFu, &v)) {
        return OY_FILE_ERR_MALFORMED;
    }
    *type = (uint16_t)v;
    const char *name = oy_map_type_name(*type);

    if (name == NULL) {
        return k == l.n ? OY_FILE_OK : OY_FILE_ERR_MISMATCH;
    }
    if (k == l.n || !same(l.s + k + 1u, l.n - k - 1u, name)) {
        return OY_FILE_ERR_MISMATCH;
    }
    return OY_FILE_OK;
}

/**
 * @brief Line 3: `Firmware: <a>.<5 digits>`, the value a × 100000 + b.
 * @param l  the line
 * @param fw takes the value
 * @return true for a value that fits 32 bits
 */
static bool firmware_line(line_t l, uint32_t *fw)
{
    uint32_t a = 0u;
    uint32_t b = 0u;

    if (!take(&l, "Firmware: ") || l.n < 7u || l.s[l.n - 6u] != '.') {
        return false;
    }
    if (!number(l.s, l.n - 6u, 42949u, &a)) {
        return false;
    }
    for (size_t i = l.n - 5u; i < l.n; ++i) {
        if (l.s[i] < '0' || l.s[i] > '9') {
            return false;
        }
        b = b * 10u + (uint32_t)(l.s[i] - '0');
    }
    const uint64_t v = (uint64_t)a * 100000u + b;

    if (v > 0xFFFFFFFFu) {
        return false;
    }
    *fw = (uint32_t)v;
    return true;
}

/**
 * @brief An index line: `<ii> <key> = <value>`.
 * @param l     the line
 * @param index the index this line has to carry
 * @param value takes the value
 * @return OY_FILE_OK, OY_FILE_ERR_MALFORMED or OY_FILE_ERR_MISMATCH (a key
 *         that is not the map's)
 */
static oy_file_err_t index_line(line_t l, uint16_t index, uint16_t *value)
{
    const char want[3] = { (char)('0' + index / 10u),
                           (char)('0' + index % 10u), ' ' };
    uint32_t v = 0u;

    if (l.n < 3u || memcmp(l.s, want, sizeof(want)) != 0) {
        return OY_FILE_ERR_MALFORMED;
    }
    l.s += 3;
    l.n -= 3u;
    size_t k = 0u;

    while (k + 3u <= l.n && memcmp(l.s + k, " = ", 3u) != 0) {
        ++k;
    }
    if (k + 3u > l.n
        || !number(l.s + k + 3u, l.n - k - 3u, 0xFFFFu, &v)) {
        return OY_FILE_ERR_MALFORMED;
    }
    const oy_field_t *f = oy_map_field(index);

    if (!same(l.s, k, f != NULL ? f->key : RAW_KEY)) {
        return OY_FILE_ERR_MISMATCH;
    }
    *value = (uint16_t)v;
    return OY_FILE_OK;
}

/**
 * @brief Lines 2 to @p body of a text whose checksum fits.
 * @param it   the walk, standing at line 2
 * @param body the lines before the checksum line
 * @param t    takes the file
 * @param line takes the line of a refusal
 * @return OY_FILE_OK, or why the text is refused
 */
static oy_file_err_t fields(lines_t it, unsigned body, oy_file_t *t,
                            unsigned *line)
{
    line_t l;
    uint32_t v = 0u;
    oy_file_err_t e = OY_FILE_OK;

    for (unsigned no = 2u; no <= body && next_line(&it, &l); ++no) {
        if (no == 2u) {
            e = type_line(l, &t->type);
        } else if (no == 3u) {
            e = firmware_line(l, &t->firmware) ? OY_FILE_OK
                                               : OY_FILE_ERR_MALFORMED;
        } else if (no == 4u) {
            e = (take(&l, "Serial: ")
                 && number(l.s, l.n, 0xFFFFFFFFu, &t->serial))
                    ? OY_FILE_OK : OY_FILE_ERR_MALFORMED;
        } else if (no == HEAD_LINES) {
            if (!take(&l, "Count: ") || !number(l.s, l.n, 0xFFFFu, &v)) {
                e = OY_FILE_ERR_MALFORMED;
            } else if (v < OY_FILE_COUNT_MIN || v > OPENYGE_MAX_PARAMS) {
                e = OY_FILE_ERR_COUNT;
            }
            t->count = (uint16_t)v;
        } else if (no - HEAD_LINES - 1u >= t->count) {
            e = OY_FILE_ERR_COUNT;
        } else {
            const uint16_t i = (uint16_t)(no - HEAD_LINES - 1u);
            e = index_line(l, i, &t->value[i]);
        }
        if (e != OY_FILE_OK) {
            return fail(line, no, e);
        }
    }
    if (body < HEAD_LINES) {
        return fail(line, body + 1u, OY_FILE_ERR_MALFORMED);
    }
    if (body - HEAD_LINES != t->count) {
        return fail(line, body + 1u, OY_FILE_ERR_COUNT);
    }
    if (t->value[0] != t->count) {
        return fail(line, HEAD_LINES + 1u, OY_FILE_ERR_MISMATCH);
    }
    if (t->value[OY_I_TYPE] != t->type) {
        return fail(line, 2u, OY_FILE_ERR_MISMATCH);
    }
    if (oy_map_u32(t->value[OY_I_FW_LO], t->value[OY_I_FW_HI])
        != t->firmware) {
        return fail(line, 3u, OY_FILE_ERR_MISMATCH);
    }
    if (oy_map_u32(t->value[OY_I_SERIAL_LO], t->value[OY_I_SERIAL_HI])
        != t->serial) {
        return fail(line, 4u, OY_FILE_ERR_MISMATCH);
    }
    return OY_FILE_OK;
}

oy_file_err_t oy_file_parse(const char *text, size_t len, oy_file_t *out,
                            unsigned *line)
{
    if (text == NULL || out == NULL) {
        return fail(line, 0u, OY_FILE_ERR_ARG);
    }
    if (len > OY_FILE_MAX) {
        return fail(line, 0u, OY_FILE_ERR_OVERSIZED);
    }
    if (len >= 3u && memcmp(text, "\xEF\xBB\xBF", 3u) == 0) {
        text += 3;
        len -= 3u;
    }
    lines_t it = { text, len, 0u };
    line_t l;

    if (!next_line(&it, &l)) {
        return fail(line, 0u, OY_FILE_ERR_NOT_YGE);
    }
    oy_file_err_t e = header(l);

    if (e != OY_FILE_OK) {
        return fail(line, 1u, e);
    }
    unsigned body = 0u;
    const lines_t top = { text, len, 0u };

    e = frame(top, &body, line);
    if (e != OY_FILE_OK) {
        return e;
    }
    oy_file_t t;

    memset(&t, 0, sizeof(t));
    e = fields(it, body, &t, line);
    if (e != OY_FILE_OK) {
        return e;
    }
    *out = t;
    return fail(line, 0u, OY_FILE_OK);
}

/* --- load ----------------------------------------------------------------- */

oy_load_t oy_file_load(const oy_file_t *f, const oy_guard_t *g,
                       oy_file_load_t *out)
{
    if (f == NULL || g == NULL || out == NULL) {
        return OY_LOAD_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->count = g->count;
    memcpy(out->draft, g->orig, sizeof(out->draft));
    if (!g->table) {
        out->ref = (uint8_t)OY_REF_NO_TABLE;
        return OY_LOAD_GUARD;
    }
    if (f->count != g->count) {
        return OY_LOAD_COUNT;
    }
    if (f->type != g->orig[OY_I_TYPE]) {
        return OY_LOAD_TYPE;
    }
    out->other_fw = f->firmware
                    != oy_map_u32(g->orig[OY_I_FW_LO], g->orig[OY_I_FW_HI]);
    out->other_serial = f->serial
                        != oy_map_u32(g->orig[OY_I_SERIAL_LO],
                                      g->orig[OY_I_SERIAL_HI]);
    uint16_t d[OPENYGE_MAX_PARAMS];

    memcpy(d, g->orig, sizeof(d));
    for (uint16_t i = 0; i < g->count; ++i) {
        if (!oy_map_writable(i)) {
            continue;
        }
        const oy_field_t *fl = oy_map_field(i);

        if (fl->kind == OY_KIND_FLAGS) {
            d[i] = (uint16_t)((g->orig[i] & (uint16_t)~fl->edit_bits)
                              | (f->value[i] & fl->edit_bits));
        } else {
            d[i] = f->value[i];
        }
    }
    oy_guard_t probe = *g;
    uint16_t bad = 0u;
    const oy_ref_t r = oy_guard_approve(&probe, d, g->count, g->read_no,
                                        g->read_crc, &bad);

    if (r != OY_REF_NONE) {
        out->ref = (uint8_t)r;
        out->bad = bad;
        return OY_LOAD_GUARD;
    }
    for (uint16_t i = 0; i < g->count; ++i) {
        if (d[i] != g->orig[i]) {
            ++out->changed;
        }
    }
    memcpy(out->draft, d, sizeof(out->draft));
    return OY_LOAD_OK;
}
