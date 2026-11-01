/**
 * @file ew_file.c
 * @brief The settings of an ESC as a text file; see ew_file.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file.h"

#include <string.h>

/** @brief The start of the first line, before the format number. */
#define HEADER "ESC settings backup, format "
/** @brief The keyword of the line in front of the bytes. */
#define BYTES "bytes"
/** @brief The keyword of the last line of a verified file. */
#define CHECKSUM "checksum"
/** @brief The last line of an edited file. */
#define EDITED "edited"
/** @brief Behind a field no load takes. */
#define NOT_LOADED " [not loaded]"
/** @brief The byte order mark of UTF-8. */
#define BOM "\xEF\xBB\xBF"
/** @brief The length of the string literal @p s. */
#define STR_LEN(s) (sizeof(s) - 1u)

/** @brief Bytes on a line of the block. */
#define ROW_BYTES 16u
/** @brief "00:" and 16 times " xx". */
#define ROW_LEN (3u + 3u * ROW_BYTES)
/** @brief "checksum", a space and 8 digits. */
#define CHECKSUM_LEN (STR_LEN(CHECKSUM) + 9u)
/** @brief Room for a value's text. */
#define VALUE_MAX 32u

/** @brief Upper-case hexadecimal digits. */
static const char k_hex[] = "0123456789ABCDEF";

/** @brief The keys of the identity lines. */
#define KEY_FIRMWARE  "Firmware"
#define KEY_LAYOUT    "Layout"
#define KEY_CPU       "Processor"
#define KEY_PIN       "Pin code"
#define KEY_FLASH     "Flash code"
#define KEY_PROTOCOL  "Bootloader protocol"
#define KEY_VERSION   "Bootloader version"
#define KEY_NAME      "Name"
#define KEY_SIGNATURE "Signature"

/* --- checksum ------------------------------------------------------------- */

/** @brief The start of the CRC-32 register. */
#define CRC_START 0xFFFFFFFFu

/**
 * @brief Add one byte to the CRC-32 register, 4 bits a step.
 * @param crc  the register
 * @param byte the byte
 * @return the register
 */
static uint32_t crc_add(uint32_t crc, uint8_t byte)
{
    static const uint32_t k_table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };

    crc ^= byte;
    crc = (crc >> 4) ^ k_table[crc & 0x0Fu];
    crc = (crc >> 4) ^ k_table[crc & 0x0Fu];
    return crc;
}

uint32_t ew_file_crc32(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t crc = CRC_START;

    for (size_t i = 0; i < n; ++i) {
        crc = crc_add(crc, p[i]);
    }
    return ~crc;
}

/* --- what a file says of itself ------------------------------------------- */

/**
 * @brief The word a file uses for a firmware.
 * @param fw an ew_fw_t
 * @return the word; "none" for EW_FW_NONE
 */
static const char *fw_word(uint8_t fw)
{
    switch (fw) {
    case EW_FW_S:  return "BLHeli_S";
    case EW_FW_BJ: return "Bluejay";
    case EW_FW_AM: return "AM32";
    default:       return "none";
    }
}

/**
 * @brief The processor a firmware runs on.
 * @param fw a firmware that is not EW_FW_NONE
 * @return its processor
 */
static uint8_t fw_cpu(uint8_t fw)
{
    return fw == (uint8_t)EW_FW_AM ? (uint8_t)EW_CPU_ARM : (uint8_t)EW_CPU_EFM8;
}

/**
 * @brief The layout a page holds, as identification reads it.
 * @param fw   the firmware
 * @param page the page
 * @return the layout; 0 for no firmware and for erased settings
 */
static uint8_t page_layout(uint8_t fw, const uint8_t *page)
{
    switch (fw) {
    case EW_FW_S:
    case EW_FW_BJ:
        return page[2];
    case EW_FW_AM:
        return page[0] == 0xFFu ? 0u : page[1];
    default:
        return 0u;
    }
}

/**
 * @brief Whether a name fits the format: printable, at most EW_ID_NAME_MAX.
 * @param name the name
 * @return true when it does
 */
static bool name_fits(const char *name)
{
    for (size_t i = 0; name[i] != '\0'; ++i) {
        if (i >= EW_ID_NAME_MAX || name[i] < 0x20 || name[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether the lines of a file agree with its page and each other.
 * @param f the file, its bytes read
 * @return true when they do
 */
static bool agrees(const ew_file_t *f)
{
    if (f->layout != page_layout(f->fw, f->page)) {
        return false;
    }
    return f->fw == (uint8_t)EW_FW_NONE || f->cpu == fw_cpu(f->fw);
}

bool ew_file_capture(ew_file_t *f, const ew_contact_t *c, const ew_ident_t *id,
                     const uint8_t *page)
{
    if (f == NULL || c == NULL || id == NULL || page == NULL
        || id->len == 0u || id->len > EW_PAGE_MAX
        || c->cpu == (uint8_t)EW_CPU_NONE || !name_fits(id->name)) {
        return false;
    }
    ew_file_t t;

    memset(&t, 0, sizeof(t));
    t.kind = (uint8_t)EW_FILE_VERIFIED;
    t.fw = id->fw;
    t.layout = id->layout;
    t.cpu = c->cpu;
    t.len = id->len;
    memcpy(t.page, page, id->len);
    if (c->cpu == (uint8_t)EW_CPU_ARM) {
        t.pin_code = c->pin_code;
        t.flash_code = c->flash_code;
        t.protocol = c->protocol;
        memcpy(t.name, id->name, sizeof(t.name));
    } else {
        t.signature = c->signature;
    }
    if (!agrees(&t)
        || (t.fw != (uint8_t)EW_FW_NONE && t.len != ew_map_len(t.fw))) {
        return false;
    }
    *f = t;
    return true;
}

/* --- writing -------------------------------------------------------------- */

/** @brief The text as it grows.  A byte past the end is counted, not stored. */
typedef struct {
    char  *out; /**< the buffer             */
    size_t cap; /**< its bytes              */
    size_t n;   /**< the bytes of the text  */
} buf_t;

/**
 * @brief Add a character.
 * @param b the text
 * @param c the character
 */
static void put_char(buf_t *b, char c)
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
static void put_str(buf_t *b, const char *s)
{
    while (*s != '\0') {
        put_char(b, *s++);
    }
}

/**
 * @brief Add a byte in decimal.
 * @param b the text
 * @param v the byte
 */
static void put_dec(buf_t *b, unsigned v)
{
    if (v >= 100u) {
        put_char(b, (char)('0' + v / 100u));
    }
    if (v >= 10u) {
        put_char(b, (char)('0' + v / 10u % 10u));
    }
    put_char(b, (char)('0' + v % 10u));
}

/**
 * @brief Add the low digits of a value in upper-case hexadecimal.
 * @param b      the text
 * @param v      the value
 * @param digits how many
 */
static void put_hex(buf_t *b, uint32_t v, unsigned digits)
{
    while (digits > 0u) {
        digits--;
        put_char(b, k_hex[(v >> (4u * digits)) & 0x0Fu]);
    }
}

/**
 * @brief Add a line: its key, a colon and a space.
 * @param b   the text
 * @param key the key
 */
static void put_key(buf_t *b, const char *key)
{
    put_str(b, key);
    put_str(b, ": ");
}

/**
 * @brief Add the identity lines of a processor.
 * @param b the text
 * @param f the file
 */
static void put_identity(buf_t *b, const ew_file_t *f)
{
    if (f->cpu == (uint8_t)EW_CPU_EFM8) {
        put_key(b, KEY_CPU);
        put_str(b, "EFM8\n");
        put_key(b, KEY_SIGNATURE);
        put_hex(b, f->signature, 4u);
        put_char(b, '\n');
        return;
    }
    put_key(b, KEY_CPU);
    put_str(b, "ARM\n");
    put_key(b, KEY_PIN);
    put_hex(b, f->pin_code, 2u);
    put_char(b, '\n');
    put_key(b, KEY_FLASH);
    put_hex(b, f->flash_code, 2u);
    put_char(b, '\n');
    put_key(b, KEY_PROTOCOL);
    put_dec(b, f->protocol);
    put_char(b, '\n');
    put_key(b, KEY_VERSION);
    put_dec(b, f->page[2]);
    put_char(b, '\n');
    put_key(b, KEY_NAME);
    put_str(b, f->name);
    put_char(b, '\n');
}

size_t ew_file_format(const ew_file_t *f, char *out, size_t size)
{
    buf_t b = { out, size, 0u };

    if (f == NULL || out == NULL || f->kind != (uint8_t)EW_FILE_VERIFIED) {
        return 0u;
    }
    put_str(&b, HEADER);
    put_dec(&b, EW_FILE_FORMAT);
    put_char(&b, '\n');
    put_key(&b, KEY_FIRMWARE);
    put_str(&b, fw_word(f->fw));
    put_char(&b, '\n');
    put_key(&b, KEY_LAYOUT);
    put_dec(&b, f->layout);
    put_char(&b, '\n');
    put_identity(&b, f);
    for (size_t i = 0;; ++i) {
        const ew_field_t *fd = ew_map_nth(f->fw, f->layout, i);
        char text[VALUE_MAX];

        if (fd == NULL) {
            break;
        }
        (void)ew_field_text(fd, f->page[fd->off], text, sizeof(text));
        put_str(&b, fd->name);
        put_str(&b, " = ");
        put_str(&b, text);
        if (fd->access != (uint8_t)EW_ACC_EDIT) {
            put_str(&b, NOT_LOADED);
        }
        put_char(&b, '\n');
    }
    put_str(&b, BYTES " ");
    put_dec(&b, f->len);
    put_char(&b, '\n');
    for (size_t i = 0; i < f->len; ++i) {
        if (i % ROW_BYTES == 0u) {
            put_hex(&b, (uint32_t)i, 2u);
            put_char(&b, ':');
        }
        put_char(&b, ' ');
        put_hex(&b, f->page[i], 2u);
        if (i % ROW_BYTES == ROW_BYTES - 1u || i + 1u == f->len) {
            put_char(&b, '\n');
        }
    }
    if (b.n > size) {
        return 0u;
    }
    const uint32_t crc = ew_file_crc32(out, b.n);

    put_str(&b, CHECKSUM " ");
    put_hex(&b, crc, 8u);
    put_char(&b, '\n');
    return b.n > size ? 0u : b.n;
}

/* --- reading: characters and lines ---------------------------------------- */

/** @brief The reading position. */
typedef struct {
    const char *text; /**< the text                            */
    size_t      len;  /**< its bytes                           */
    size_t      pos;  /**< the start of the next line          */
    unsigned    line; /**< the line asked for last, from 1     */
} cur_t;

/** @brief One line, without its line end. */
typedef struct {
    const char *s;    /**< its first byte                      */
    size_t      n;    /**< its bytes                           */
    bool        cr;   /**< it ends with CR LF                  */
    bool        open; /**< the text ends in it, with no line end */
} line_t;

/**
 * @brief The next line.  At the end of the text a line of no bytes.
 * @param c the position
 * @param l takes the line
 * @return false at the end of the text
 */
static bool next_line(cur_t *c, line_t *l)
{
    const size_t left = c->len - c->pos;
    const char *start = c->text + c->pos;
    const char *lf = left > 0u ? memchr(start, '\n', left) : NULL;

    l->s = start;
    l->open = lf == NULL;
    l->n = l->open ? left : (size_t)(lf - start);
    l->cr = !l->open && l->n > 0u && start[l->n - 1u] == '\r';
    if (left == 0u) {
        return false;
    }
    c->line++;
    c->pos += l->open ? left : l->n + 1u;
    if (l->cr) {
        l->n--;
    }
    return true;
}

/**
 * @brief Whether a character is a space or a tab.
 * @param c the character
 * @return true when it is
 */
static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

/**
 * @brief Fold a capital letter.
 * @param c the character
 * @return its lower case; any other character unchanged
 */
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/**
 * @brief Whether a text is a word, without case and without spaces in either.
 * @param s   the text
 * @param n   its bytes
 * @param ref the word
 * @return true when they read alike
 */
static bool loose_eq(const char *s, size_t n, const char *ref)
{
    size_t i = 0u;

    for (;;) {
        while (i < n && is_blank(s[i])) {
            i++;
        }
        while (is_blank(*ref)) {
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
 * @brief Take spaces and tabs off both ends.
 * @param s the text
 * @param n its bytes
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
 * @brief Whether a line holds spaces and tabs or nothing.
 * @param l the line
 * @return true when it does
 */
static bool blank_line(const line_t *l)
{
    const char *s = l->s;
    size_t n = l->n;

    trim(&s, &n);
    return n == 0u;
}

/**
 * @brief Whether the first word of a line is the checksum's, in any case.
 * @param l the line
 * @return true when it is
 */
static bool checksum_word(const line_t *l)
{
    const char *s = l->s;
    size_t n = l->n;
    size_t w = 0u;

    trim(&s, &n);
    while (w < n && !is_blank(s[w]) && s[w] != '=' && s[w] != ':') {
        w++;
    }
    return loose_eq(s, w, CHECKSUM);
}

/**
 * @brief A byte in decimal, the whole text.
 * @param s the text
 * @param n its bytes
 * @param max the highest value
 * @param v takes the value
 * @return false for no digit, another character or a value over @p max
 */
static bool whole_dec(const char *s, size_t n, unsigned max, unsigned *v)
{
    unsigned value = 0u;

    if (n == 0u || n > 3u) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        value = value * 10u + (unsigned)(s[i] - '0');
    }
    *v = value;
    return value <= max;
}

/**
 * @brief Upper-case hexadecimal digits, the whole text.
 * @param s      the text
 * @param n      its bytes
 * @param digits how many it has to hold
 * @param v      takes the value
 * @return false for another count or another character
 */
static bool whole_hex(const char *s, size_t n, size_t digits, uint32_t *v)
{
    uint32_t value = 0u;

    if (n != digits) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char *at = s[i] != '\0' ? strchr(k_hex, s[i]) : NULL;

        if (at == NULL) {
            return false;
        }
        value = (value << 4) | (uint32_t)(at - k_hex);
    }
    *v = value;
    return true;
}

/**
 * @brief One line of the block.
 * @param s    the text of the line
 * @param n    its bytes
 * @param row  its number, from 0
 * @param len  the bytes of the whole block
 * @param page takes the bytes
 * @return false for a line that is not this one, as ew_file_format() writes it
 */
static bool block_row(const char *s, size_t n, size_t row, size_t len,
                      uint8_t *page)
{
    const size_t at = row * ROW_BYTES;
    const size_t left = len - at;
    const size_t count = left < ROW_BYTES ? left : ROW_BYTES;
    uint32_t v = 0u;

    if (n != 3u + 3u * count || s[2] != ':' || !whole_hex(s, 2u, 2u, &v)
        || v != at) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *cell = s + 3u + 3u * i;

        if (cell[0] != ' ' || !whole_hex(cell + 1, 2u, 2u, &v)) {
            return false;
        }
        page[at + i] = (uint8_t)v;
    }
    return true;
}

/**
 * @brief The count of a block's lines.
 * @param len the bytes of the block
 * @return its lines
 */
static size_t block_rows(size_t len)
{
    return (len + ROW_BYTES - 1u) / ROW_BYTES;
}

/**
 * @brief Line 1: the header and a version this code reads.
 * @param c the position, at the start of the text
 * @return EW_FILE_OK, or why line 1 is refused
 */
static ew_file_err_t read_head(cur_t *c)
{
    const size_t have = c->len < STR_LEN(HEADER) ? c->len : STR_LEN(HEADER);
    line_t l;
    unsigned version = 0u;

    if (memcmp(c->text, HEADER, have) != 0) {
        c->line = 1u;
        return EW_FILE_ERR_NOT_ESC;
    }
    if (!next_line(c, &l) || l.open) {
        c->line = 1u;
        return EW_FILE_ERR_TRUNCATED;
    }
    if (!whole_dec(l.s + STR_LEN(HEADER), l.n - STR_LEN(HEADER), 999u, &version)
        || version == 0u) {
        return EW_FILE_ERR_VERSION;
    }
    return version > EW_FILE_FORMAT ? EW_FILE_ERR_NEWER : EW_FILE_OK;
}

/* --- reading: a verified file --------------------------------------------- */

/** @brief The state of one verified read. */
typedef struct {
    cur_t   *c;    /**< the position                            */
    uint32_t crc;  /**< the register over the lines so far      */
    bool     crlf; /**< the file's line end                     */
    line_t   l;    /**< the line just taken                     */
} ver_t;

/**
 * @brief Take a line with its check and its share of the checksum.
 * @param v the read
 * @return EW_FILE_OK, EW_FILE_ERR_LINE_END or EW_FILE_ERR_MALFORMED
 */
static ew_file_err_t ver_line(ver_t *v)
{
    (void)next_line(v->c, &v->l);
    if (!v->l.open && v->l.cr != v->crlf) {
        return EW_FILE_ERR_LINE_END;
    }
    if (v->l.n > EW_FILE_LINE_MAX) {
        return EW_FILE_ERR_MALFORMED;
    }
    for (size_t i = 0; i < v->l.n; ++i) {
        if (v->l.s[i] < 0x20 || v->l.s[i] > 0x7E) {
            return EW_FILE_ERR_MALFORMED;
        }
        v->crc = crc_add(v->crc, (uint8_t)v->l.s[i]);
    }
    v->crc = crc_add(v->crc, '\n');
    return EW_FILE_OK;
}

/**
 * @brief Take a line that holds a key, a colon, a space and a value.
 * @param v   the read
 * @param key the key
 * @param val takes the value
 * @param n   takes its bytes
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t ver_key(ver_t *v, const char *key, const char **val,
                             size_t *n)
{
    const ew_file_err_t err = ver_line(v);
    const size_t k = strlen(key);

    if (err != EW_FILE_OK) {
        return err;
    }
    if (v->l.n < k + 2u || memcmp(v->l.s, key, k) != 0 || v->l.s[k] != ':'
        || v->l.s[k + 1u] != ' ') {
        return EW_FILE_ERR_MALFORMED;
    }
    *val = v->l.s + k + 2u;
    *n = v->l.n - k - 2u;
    return EW_FILE_OK;
}

/**
 * @brief Take a line with a key and hexadecimal digits.
 * @param v      the read
 * @param key    the key
 * @param digits how many
 * @param out    takes the value
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t ver_hex(ver_t *v, const char *key, size_t digits,
                             uint32_t *out)
{
    const char *s = NULL;
    size_t n = 0u;
    const ew_file_err_t err = ver_key(v, key, &s, &n);

    if (err != EW_FILE_OK) {
        return err;
    }
    return whole_hex(s, n, digits, out) ? EW_FILE_OK : EW_FILE_ERR_MALFORMED;
}

/**
 * @brief Take a line with a key and a byte in decimal.
 * @param v   the read
 * @param key the key
 * @param max the highest value
 * @param out takes the value
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t ver_dec(ver_t *v, const char *key, unsigned max,
                             unsigned *out)
{
    const char *s = NULL;
    size_t n = 0u;
    const ew_file_err_t err = ver_key(v, key, &s, &n);

    if (err != EW_FILE_OK) {
        return err;
    }
    return whole_dec(s, n, max, out) ? EW_FILE_OK : EW_FILE_ERR_MALFORMED;
}

/**
 * @brief The firmware a word names.
 * @param s   the word
 * @param n   its bytes
 * @param out takes the firmware
 * @return false for no firmware of this format
 */
static bool fw_from_word(const char *s, size_t n, uint8_t *out)
{
    for (uint8_t fw = (uint8_t)EW_FW_NONE; fw < (uint8_t)EW_FWS; ++fw) {
        if (loose_eq(s, n, fw_word(fw))) {
            *out = fw;
            return true;
        }
    }
    return false;
}

/**
 * @brief The lines of an ARM part's identity, behind the processor's.
 * @param v       the read
 * @param f       takes them
 * @param version takes the bootloader version the file states
 * @return EW_FILE_OK, or why they are refused
 */
static ew_file_err_t ver_arm(ver_t *v, ew_file_t *f, unsigned *version)
{
    uint32_t h = 0u;
    unsigned d = 0u;
    const char *s = NULL;
    size_t n = 0u;
    ew_file_err_t err = ver_hex(v, KEY_PIN, 2u, &h);

    if (err != EW_FILE_OK) {
        return err;
    }
    f->pin_code = (uint8_t)h;
    err = ver_hex(v, KEY_FLASH, 2u, &h);
    if (err != EW_FILE_OK) {
        return err;
    }
    f->flash_code = (uint8_t)h;
    err = ver_dec(v, KEY_PROTOCOL, 255u, &d);
    if (err != EW_FILE_OK) {
        return err;
    }
    f->protocol = (uint8_t)d;
    err = ver_dec(v, KEY_VERSION, 255u, version);
    if (err != EW_FILE_OK) {
        return err;
    }
    err = ver_key(v, KEY_NAME, &s, &n);
    if (err != EW_FILE_OK) {
        return err;
    }
    if (n > EW_ID_NAME_MAX) {
        return EW_FILE_ERR_NAME_LONG;
    }
    memcpy(f->name, s, n);
    return EW_FILE_OK;
}

/**
 * @brief The lines from the firmware's to the identity's end.
 * @param v       the read
 * @param f       takes them
 * @param version takes the bootloader version an ARM part's file states, or
 *                is left alone
 * @return EW_FILE_OK, or why they are refused
 */
static ew_file_err_t ver_identity(ver_t *v, ew_file_t *f, unsigned *version)
{
    const char *s = NULL;
    size_t n = 0u;
    unsigned d = 0u;
    ew_file_err_t err = ver_key(v, KEY_FIRMWARE, &s, &n);

    if (err != EW_FILE_OK) {
        return err;
    }
    if (!fw_from_word(s, n, &f->fw)) {
        return EW_FILE_ERR_FIRMWARE;
    }
    err = ver_dec(v, KEY_LAYOUT, 255u, &d);
    if (err != EW_FILE_OK) {
        return err;
    }
    f->layout = (uint8_t)d;
    err = ver_key(v, KEY_CPU, &s, &n);
    if (err != EW_FILE_OK) {
        return err;
    }
    if (n == 4u && memcmp(s, "EFM8", 4u) == 0) {
        uint32_t sig = 0u;

        f->cpu = (uint8_t)EW_CPU_EFM8;
        err = ver_hex(v, KEY_SIGNATURE, 4u, &sig);
        f->signature = (uint16_t)sig;
        return err;
    }
    if (n != 3u || memcmp(s, "ARM", 3u) != 0) {
        return EW_FILE_ERR_MALFORMED;
    }
    f->cpu = (uint8_t)EW_CPU_ARM;
    return ver_arm(v, f, version);
}

/**
 * @brief The field lines and the count of bytes behind them.
 * @param v the read
 * @param f takes the count
 * @return EW_FILE_OK, or why they are refused
 */
static ew_file_err_t ver_count(ver_t *v, ew_file_t *f)
{
    unsigned count = 0u;

    for (;;) {
        const ew_file_err_t err = ver_line(v);

        if (err != EW_FILE_OK) {
            return err;
        }
        if (checksum_word(&v->l)) {
            return EW_FILE_ERR_MALFORMED;
        }
        if (v->l.n > STR_LEN(BYTES)
            && memcmp(v->l.s, BYTES " ", STR_LEN(BYTES) + 1u) == 0) {
            break;
        }
    }
    if (!whole_dec(v->l.s + STR_LEN(BYTES) + 1u, v->l.n - STR_LEN(BYTES) - 1u,
                   EW_PAGE_MAX, &count)
        || count == 0u) {
        return EW_FILE_ERR_LENGTH;
    }
    if (f->fw != (uint8_t)EW_FW_NONE && count != ew_map_len(f->fw)) {
        return EW_FILE_ERR_LENGTH;
    }
    f->len = (uint16_t)count;
    return EW_FILE_OK;
}

/**
 * @brief A text whose last line is a checksum line: the whole file, as
 *        ew_file_format() writes it.
 * @param c the position, behind the header
 * @param f takes the file
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t read_verified(cur_t *c, ew_file_t *f)
{
    ver_t v;
    cur_t first = *c;
    char written[CHECKSUM_LEN + 1u];
    buf_t want = { written, sizeof(written), 0u };
    unsigned version = 0u;
    ew_file_err_t err;

    /* The header comes again, for its share of the checksum. */
    c->pos = 0u;
    c->line = 0u;
    first.pos = 0u;
    (void)next_line(&first, &v.l);
    v.c = c;
    v.crc = CRC_START;
    v.crlf = v.l.cr;
    err = ver_line(&v);
    if (err == EW_FILE_OK) {
        err = ver_identity(&v, f, &version);
    }
    if (err == EW_FILE_OK) {
        err = ver_count(&v, f);
    }
    if (err != EW_FILE_OK) {
        return err;
    }
    for (size_t row = 0; row < block_rows(f->len); ++row) {
        err = ver_line(&v);
        if (err != EW_FILE_OK) {
            return err;
        }
        if (!block_row(v.l.s, v.l.n, row, f->len, f->page)) {
            return EW_FILE_ERR_MALFORMED;
        }
    }
    (void)next_line(c, &v.l);
    if (!v.l.open && v.l.cr != v.crlf) {
        return EW_FILE_ERR_LINE_END;
    }
    put_str(&want, CHECKSUM " ");
    put_hex(&want, ~v.crc, 8u);
    if (v.l.n != CHECKSUM_LEN || memcmp(v.l.s, written, CHECKSUM_LEN) != 0) {
        return EW_FILE_ERR_CHECKSUM;
    }
    while (next_line(c, &v.l)) {
        if (!blank_line(&v.l)) {
            return EW_FILE_ERR_TRAILING;
        }
    }
    if (!agrees(f)
        || (f->cpu == (uint8_t)EW_CPU_ARM && version != f->page[2])) {
        c->line = 2u;
        return EW_FILE_ERR_MISMATCH;
    }
    f->kind = (uint8_t)EW_FILE_VERIFIED;
    return EW_FILE_OK;
}

/* --- reading: an edited file ---------------------------------------------- */

/**
 * @brief The raw value a text gives: `raw <n>`, or the lowest byte that reads
 *        as the text.
 * @param fd the field
 * @param s  the text
 * @param n  its bytes
 * @param v  takes the value
 * @return false when no byte reads so
 */
static bool value_from_text(const ew_field_t *fd, const char *s, size_t n,
                            uint8_t *v)
{
    const size_t unit = strlen(fd->unit);
    unsigned given = 0u;

    if (n > 4u && lower(s[0]) == 'r' && lower(s[1]) == 'a' && lower(s[2]) == 'w'
        && is_blank(s[3])) {
        const char *num = s + 4;
        size_t nn = n - 4u;

        trim(&num, &nn);
        if (whole_dec(num, nn, 255u, &given)) {
            *v = (uint8_t)given;
            return true;
        }
        return false;
    }
    for (unsigned raw = 0u; raw <= 255u; ++raw) {
        char text[VALUE_MAX];
        const size_t len = ew_field_text(fd, (uint8_t)raw, text, sizeof(text));
        bool same = loose_eq(s, n, text);

        /* A person may leave the unit out. */
        if (!same && unit > 0u && len > unit
            && strcmp(text + len - unit, fd->unit) == 0) {
            text[len - unit] = '\0';
            same = loose_eq(s, n, text);
        }
        if (same) {
            *v = (uint8_t)raw;
            return true;
        }
    }
    return false;
}

/** @brief The state of one edited read. */
typedef struct {
    ew_file_t *f;         /**< the file                       */
    size_t     rows_left; /**< lines of the block still to come */
    size_t     row;       /**< the next line of the block     */
    bool       has_fw;    /**< a firmware line came           */
    bool       has_layout; /**< a layout line came            */
    bool       has_block; /**< a block came                   */
} edit_t;

/**
 * @brief A line with a colon: one of the identity lines.
 * @param e the read
 * @param k the key
 * @param kn its bytes
 * @param s  the value
 * @param n  its bytes
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t edit_identity(edit_t *e, const char *k, size_t kn,
                                   const char *s, size_t n)
{
    static const char *const k_unused[] = {
        KEY_CPU, KEY_PIN, KEY_FLASH, KEY_PROTOCOL, KEY_VERSION, KEY_NAME,
        KEY_SIGNATURE,
    };
    unsigned d = 0u;

    if (loose_eq(k, kn, KEY_FIRMWARE)) {
        if (e->has_fw) {
            return EW_FILE_ERR_DUPLICATE;
        }
        e->has_fw = true;
        if (!fw_from_word(s, n, &e->f->fw) || e->f->fw == (uint8_t)EW_FW_NONE) {
            return EW_FILE_ERR_FIRMWARE;
        }
        e->f->len = ew_map_len(e->f->fw);
        return EW_FILE_OK;
    }
    if (loose_eq(k, kn, KEY_LAYOUT)) {
        if (e->has_layout) {
            return EW_FILE_ERR_DUPLICATE;
        }
        e->has_layout = true;
        if (!whole_dec(s, n, 255u, &d)) {
            return EW_FILE_ERR_VALUE;
        }
        e->f->layout = (uint8_t)d;
        return EW_FILE_OK;
    }
    for (size_t i = 0; i < sizeof(k_unused) / sizeof(k_unused[0]); ++i) {
        if (loose_eq(k, kn, k_unused[i])) {
            return EW_FILE_OK;
        }
    }
    return EW_FILE_ERR_FIELD;
}

/**
 * @brief A line with an equals sign: a field.
 * @param e  the read
 * @param k  the name
 * @param kn its bytes
 * @param s  the value
 * @param n  its bytes
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t edit_field(edit_t *e, const char *k, size_t kn,
                                const char *s, size_t n)
{
    ew_file_t *f = e->f;

    if (!e->has_fw || !e->has_layout) {
        return EW_FILE_ERR_NO_TARGET;
    }
    if (n > 0u && s[n - 1u] == ']') {
        while (n > 0u && s[n - 1u] != '[') {
            n--;
        }
        n = n > 0u ? n - 1u : 0u;
        trim(&s, &n);
    }
    for (size_t i = 0;; ++i) {
        const ew_field_t *fd = ew_map_nth(f->fw, f->layout, i);

        if (fd == NULL) {
            return EW_FILE_ERR_FIELD;
        }
        if (!loose_eq(k, kn, fd->name)) {
            continue;
        }
        if ((f->has[fd->off / 8u] & (1u << (fd->off % 8u))) != 0u) {
            return EW_FILE_ERR_DUPLICATE;
        }
        if (!value_from_text(fd, s, n, &f->page[fd->off])) {
            return EW_FILE_ERR_VALUE;
        }
        f->has[fd->off / 8u] |= (uint8_t)(1u << (fd->off % 8u));
        f->named++;
        return EW_FILE_OK;
    }
}

/**
 * @brief A line that is no comment and not in the block.
 * @param e the read
 * @param s the line, trimmed, not empty
 * @param n its bytes
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t edit_line(edit_t *e, const char *s, size_t n)
{
    size_t k = 0u;
    size_t vn;
    const char *val;
    unsigned count = 0u;

    while (k < n && s[k] != '=' && s[k] != ':') {
        k++;
    }
    if (k == n) {
        if (n <= STR_LEN(BYTES) || !is_blank(s[STR_LEN(BYTES)])
            || !loose_eq(s, STR_LEN(BYTES), BYTES)) {
            return EW_FILE_ERR_MALFORMED;
        }
        const char *rest = s + STR_LEN(BYTES);
        size_t rn = n - STR_LEN(BYTES);

        if (e->has_block) {
            return EW_FILE_ERR_DUPLICATE;
        }
        trim(&rest, &rn);
        if (!whole_dec(rest, rn, EW_PAGE_MAX, &count) || count == 0u
            || !e->has_fw || count != e->f->len) {
            return EW_FILE_ERR_LENGTH;
        }
        e->has_block = true;
        e->rows_left = block_rows(count);
        return EW_FILE_OK;
    }
    val = s + k + 1u;
    vn = n - k - 1u;
    trim(&val, &vn);
    return s[k] == ':' ? edit_identity(e, s, k, val, vn)
                       : edit_field(e, s, k, val, vn);
}

/**
 * @brief A text whose last line is the word for an edited file.
 * @param c    the position, behind the header
 * @param f    takes the file
 * @param last the last line
 * @return EW_FILE_OK, or why it is refused
 */
static ew_file_err_t read_edited(cur_t *c, ew_file_t *f, const char *last)
{
    edit_t e;
    line_t l;
    uint8_t scratch[EW_PAGE_MAX];

    memset(&e, 0, sizeof(e));
    e.f = f;
    while (next_line(c, &l) && l.s != last) {
        const char *s = l.s;
        size_t n = 0u;

        /* A comment may hold any byte; the rest of a line is ASCII. */
        while (n < l.n && s[n] != '#') {
            if ((s[n] < 0x20 && s[n] != '\t') || s[n] > 0x7E) {
                return EW_FILE_ERR_MALFORMED;
            }
            n++;
        }
        trim(&s, &n);
        if (n == 0u) {
            continue;
        }
        if (e.rows_left > 0u) {
            if (!block_row(s, n, e.row, f->len, scratch)) {
                return EW_FILE_ERR_MALFORMED;
            }
            e.row++;
            e.rows_left--;
            continue;
        }
        const ew_file_err_t err = edit_line(&e, s, n);

        if (err != EW_FILE_OK) {
            return err;
        }
    }
    if (e.rows_left > 0u) {
        return EW_FILE_ERR_TRUNCATED;
    }
    if (!e.has_fw || !e.has_layout) {
        return EW_FILE_ERR_NO_TARGET;
    }
    f->kind = (uint8_t)EW_FILE_EDITED;
    return EW_FILE_OK;
}

/* --- reading: the text ---------------------------------------------------- */

ew_file_err_t ew_file_parse(const char *text, size_t len, ew_file_t *out,
                            unsigned *line)
{
    cur_t c = { text, len, 0u, 0u };
    ew_file_err_t err;

    if (line != NULL) {
        *line = 0u;
    }
    if (text == NULL || out == NULL) {
        return EW_FILE_ERR_ARG;
    }
    if (len > EW_FILE_MAX) {
        return EW_FILE_ERR_OVERSIZED;
    }
    if (len >= STR_LEN(BOM) && memcmp(text, BOM, STR_LEN(BOM)) == 0) {
        c.text += STR_LEN(BOM);
        c.len -= STR_LEN(BOM);
    }
    err = read_head(&c);
    if (err == EW_FILE_OK) {
        /* The last line that is not blank says what the text is. */
        cur_t scan = c;
        line_t l;
        line_t last = { NULL, 0u, false, false };
        unsigned last_at = 0u;
        bool any_checksum = false;

        while (next_line(&scan, &l)) {
            if (!blank_line(&l)) {
                last = l;
                last_at = scan.line;
            }
            any_checksum = any_checksum || checksum_word(&l);
        }
        ew_file_t f;

        memset(&f, 0, sizeof(f));
        if (last.s != NULL && checksum_word(&last)) {
            err = read_verified(&c, &f);
        } else if (any_checksum) {
            c.line = last_at;
            err = EW_FILE_ERR_TRAILING;
        } else if (last.s != NULL && loose_eq(last.s, last.n, EDITED)) {
            err = read_edited(&c, &f, last.s);
        } else {
            c.line = scan.line;
            err = EW_FILE_ERR_TRUNCATED;
        }
        if (err == EW_FILE_OK) {
            *out = f;
            return EW_FILE_OK;
        }
    }
    if (line != NULL) {
        *line = c.line;
    }
    return err;
}

/* --- a file against an ESC ------------------------------------------------ */

/**
 * @brief Whether a line of an edited file reads as the ESC's value already
 *        does: several bytes of a percentage read as one text, and the line
 *        gave the lowest of them.
 * @param f    the file
 * @param fd   the field of the byte, may be NULL
 * @param have the ESC's byte
 * @param want the file's byte
 * @return true when the load leaves the byte
 */
static bool edited_same(const ew_file_t *f, const ew_field_t *fd, uint8_t have,
                        uint8_t want)
{
    char a[VALUE_MAX];
    char b[VALUE_MAX];

    if (f->kind != (uint8_t)EW_FILE_EDITED || fd == NULL) {
        return false;
    }
    (void)ew_field_text(fd, have, a, sizeof(a));
    (void)ew_field_text(fd, want, b, sizeof(b));
    return strcmp(a, b) == 0;
}

ew_load_t ew_file_load(const ew_file_t *f, const ew_ident_t *id,
                       const uint8_t *esc, ew_file_load_t *out)
{
    if (f == NULL || id == NULL || esc == NULL || out == NULL
        || id->len > EW_PAGE_MAX) {
        return EW_LOAD_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->len = id->len;
    memcpy(out->target, esc, id->len);
    if (f->fw != id->fw) {
        return EW_LOAD_FIRMWARE;
    }
    if (id->erased) {
        return EW_LOAD_ERASED;
    }
    if (f->layout != id->layout) {
        return EW_LOAD_LAYOUT;
    }
    if (!id->writable) {
        return EW_LOAD_READ_ONLY;
    }
    for (size_t i = 0; i < id->len; ++i) {
        const ew_field_t *fd = ew_map_find(f->fw, f->layout, (uint8_t)i);
        const bool editable = fd != NULL && fd->access == (uint8_t)EW_ACC_EDIT;
        const bool gives = f->kind == (uint8_t)EW_FILE_VERIFIED
                           || (f->has[i / 8u] & (1u << (i % 8u))) != 0u;

        if (editable) {
            out->fields++;
        }
        if (!gives) {
            continue;
        }
        if (editable) {
            out->named++;
        }
        if (f->page[i] == esc[i] || edited_same(f, fd, esc[i], f->page[i])) {
            continue;
        }
        if (editable) {
            out->target[i] = f->page[i];
            out->changed++;
        } else {
            out->skipped++;
        }
    }
    uint16_t off = 0u;

    if (ew_image_check(f->fw, f->layout, esc, out->target, id->len, &off)
        != EW_IMG_OK) {
        out->bad_off = off;
        memcpy(out->target, esc, id->len);
        return EW_LOAD_VALUE;
    }
    return EW_LOAD_OK;
}

ew_restore_t ew_file_restore_check(const ew_file_t *f, const ew_ident_t *id,
                                   const ew_backup_id_t *here,
                                   const uint8_t *esc, ew_backup_id_t *from)
{
    if (f == NULL || id == NULL || here == NULL || esc == NULL
        || from == NULL) {
        return EW_RESTORE_ARG;
    }
    if (f->kind != (uint8_t)EW_FILE_VERIFIED) {
        return EW_RESTORE_NOT_VERIFIED;
    }
    if (f->fw != (uint8_t)EW_FW_AM || id->fw != (uint8_t)EW_FW_AM) {
        return EW_RESTORE_FIRMWARE;
    }
    if (!id->erased) {
        return EW_RESTORE_NOT_ERASED;
    }
    for (size_t i = 0; i < EW_MAP_LEN_AM; ++i) {
        if (esc[i] != 0xFFu) {
            return EW_RESTORE_NOT_ERASED;
        }
    }
    if (id->can_build) {
        return EW_RESTORE_CAN_BUILD;
    }
    if (f->page[0] != 0x01u || !ew_map_writable(EW_FW_AM, f->page[1])) {
        return EW_RESTORE_BAD_PAGE;
    }
    if (f->pin_code != here->pin_code) {
        return EW_RESTORE_PIN_CODE;
    }
    if (f->flash_code != here->flash_code) {
        return EW_RESTORE_FLASH_CODE;
    }
    if (f->protocol != here->protocol) {
        return EW_RESTORE_PROTOCOL;
    }
    if (strncmp(f->name, here->name, sizeof(f->name)) != 0) {
        return EW_RESTORE_NAME;
    }
    memset(from, 0, sizeof(*from));
    from->pin_code = f->pin_code;
    from->flash_code = f->flash_code;
    from->protocol = f->protocol;
    memcpy(from->name, f->name, sizeof(from->name));
    return EW_RESTORE_OK;
}
