/**
 * @file ew_file.c
 * @brief The settings of an ESC as a text file: the checksum, a capture and the
 *        text written; see ew_file.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file_int.h"

#include <string.h>

/** @brief Upper-case hexadecimal digits. */
static const char k_hex[] = "0123456789ABCDEF";

/* --- checksum ------------------------------------------------------------- */

uint32_t ew_fi_crc_add(uint32_t crc, uint8_t byte)
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
        crc = ew_fi_crc_add(crc, p[i]);
    }
    return ~crc;
}

/* --- what a file says of itself ------------------------------------------- */

const char *ew_fi_fw_word(uint8_t fw)
{
    switch (fw) {
    case EW_FW_S:  return "BLHeli_S";
    case EW_FW_BJ: return "Bluejay";
    case EW_FW_AM: return "AM32";
    case EW_FW_E32: return "ESCape32";
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
    switch (fw) {
    case EW_FW_AM:  return (uint8_t)EW_CPU_ARM;
    case EW_FW_E32: return (uint8_t)EW_CPU_TEXT;
    default:        return (uint8_t)EW_CPU_EFM8;
    }
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
 * @brief Whether a text holds printable characters only.
 * @param s the text
 * @return true when it does
 */
static bool printable(const char *s)
{
    for (; *s != '\0'; ++s) {
        if (*s < 0x20 || *s > 0x7E) {
            return false;
        }
    }
    return true;
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

bool ew_fi_agrees(const ew_file_t *f)
{
    if (f->fw == (uint8_t)EW_FW_E32) {
        return f->cpu == (uint8_t)EW_CPU_TEXT;
    }
    if ((f->cpu != (uint8_t)EW_CPU_EFM8 && f->cpu != (uint8_t)EW_CPU_ARM)
        || f->layout != page_layout(f->fw, f->page)) {
        return false;
    }
    return f->fw == (uint8_t)EW_FW_NONE || f->cpu == fw_cpu(f->fw);
}

bool ew_file_capture(ew_file_t *f, const ew_contact_t *c, const ew_ident_t *id,
                     const uint8_t *page)
{
    if (f == NULL || c == NULL || id == NULL || page == NULL
        || id->len == 0u || id->len > EW_PAGE_MAX
        || c->cpu == (uint8_t)EW_CPU_NONE || c->cpu == (uint8_t)EW_CPU_TEXT
        || !name_fits(id->name)) {
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
    if (!ew_fi_agrees(&t)
        || (t.fw != (uint8_t)EW_FW_NONE && t.len != ew_map_len(t.fw))) {
        return false;
    }
    *f = t;
    return true;
}

bool ew_file_capture_text(ew_file_t *f, const ew_ident_t *id,
                          const uint8_t *page, const char *music)
{
    if (f == NULL || id == NULL || page == NULL || music == NULL
        || id->fw != (uint8_t)EW_FW_E32 || id->len != EW_MAP_LEN_E32
        || !name_fits(id->name) || strlen(music) > EW_E32_MUSIC_MAX
        || !printable(music)) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    f->kind = (uint8_t)EW_FILE_VERIFIED;
    f->fw = (uint8_t)EW_FW_E32;
    f->layout = id->layout;
    f->cpu = (uint8_t)EW_CPU_TEXT;
    f->len = EW_MAP_LEN_E32;
    memcpy(f->name, id->name, sizeof(f->name));
    memcpy(f->music, music, strlen(music) + 1u);
    memcpy(f->page, page, EW_MAP_LEN_E32);
    return true;
}

/* --- writing -------------------------------------------------------------- */

/**
 * @brief Add a character.
 * @param b the text
 * @param c the character
 */
static void put_char(ew_fi_buf_t *b, char c)
{
    if (b->n < b->cap) {
        b->out[b->n] = c;
    }
    b->n++;
}

void ew_fi_put_str(ew_fi_buf_t *b, const char *s)
{
    while (*s != '\0') {
        put_char(b, *s++);
    }
}

/**
 * @brief Add a number in decimal.
 * @param b the text
 * @param v the number
 */
static void put_dec(ew_fi_buf_t *b, unsigned v)
{
    if (v >= 10u) {
        put_dec(b, v / 10u);
    }
    put_char(b, (char)('0' + v % 10u));
}

void ew_fi_put_hex(ew_fi_buf_t *b, uint32_t v, unsigned digits)
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
static void put_key(ew_fi_buf_t *b, const char *key)
{
    ew_fi_put_str(b, key);
    ew_fi_put_str(b, ": ");
}

/**
 * @brief Add the identity lines of a processor.
 * @param b the text
 * @param f the file
 */
static void put_identity(ew_fi_buf_t *b, const ew_file_t *f)
{
    if (f->cpu == (uint8_t)EW_CPU_TEXT) {
        put_key(b, KEY_CPU);
        ew_fi_put_str(b, "TEXT\n");
        put_key(b, KEY_TARGET);
        ew_fi_put_str(b, f->name);
        put_char(b, '\n');
        return;
    }
    if (f->cpu == (uint8_t)EW_CPU_EFM8) {
        put_key(b, KEY_CPU);
        ew_fi_put_str(b, "EFM8\n");
        put_key(b, KEY_SIGNATURE);
        ew_fi_put_hex(b, f->signature, 4u);
        put_char(b, '\n');
        return;
    }
    put_key(b, KEY_CPU);
    ew_fi_put_str(b, "ARM\n");
    put_key(b, KEY_PIN);
    ew_fi_put_hex(b, f->pin_code, 2u);
    put_char(b, '\n');
    put_key(b, KEY_FLASH);
    ew_fi_put_hex(b, f->flash_code, 2u);
    put_char(b, '\n');
    put_key(b, KEY_PROTOCOL);
    put_dec(b, f->protocol);
    put_char(b, '\n');
    put_key(b, KEY_VERSION);
    put_dec(b, f->page[2]);
    put_char(b, '\n');
    put_key(b, KEY_NAME);
    ew_fi_put_str(b, f->name);
    put_char(b, '\n');
}

/**
 * @brief Add ESCape32's key lines and its `music`.
 * @param b the text
 * @param f the file
 */
static void put_keys(ew_fi_buf_t *b, const ew_file_t *f)
{
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        const int16_t v = ew_e32_get(f->page, i);

        put_key(b, ew_map_e32[i].key);
        if (v == EW_KEY_NONE) {
            ew_fi_put_str(b, KEY_NO_VALUE);
        } else {
            if (v < 0) {
                put_char(b, '-');
            }
            put_dec(b, (unsigned)(v < 0 ? -v : v));
        }
        put_char(b, '\n');
    }
    put_key(b, KEY_MUSIC);
    ew_fi_put_str(b, f->music);
    put_char(b, '\n');
}

/**
 * @brief Add the field lines and the bytes.
 * @param b the text
 * @param f the file
 */
static void put_fields(ew_fi_buf_t *b, const ew_file_t *f)
{
    for (size_t i = 0;; ++i) {
        const ew_field_t *fd = ew_map_nth(f->fw, f->layout, i);
        char text[VALUE_MAX];

        if (fd == NULL) {
            break;
        }
        (void)ew_field_text(fd, f->page[fd->off], text, sizeof(text));
        ew_fi_put_str(b, fd->name);
        ew_fi_put_str(b, " = ");
        ew_fi_put_str(b, text);
        if (fd->access != (uint8_t)EW_ACC_EDIT) {
            ew_fi_put_str(b, NOT_LOADED);
        }
        put_char(b, '\n');
    }
    ew_fi_put_str(b, BYTES " ");
    put_dec(b, f->len);
    put_char(b, '\n');
    for (size_t i = 0; i < f->len; ++i) {
        if (i % ROW_BYTES == 0u) {
            ew_fi_put_hex(b, (uint32_t)i, 2u);
            put_char(b, ':');
        }
        put_char(b, ' ');
        ew_fi_put_hex(b, f->page[i], 2u);
        if (i % ROW_BYTES == ROW_BYTES - 1u || i + 1u == f->len) {
            put_char(b, '\n');
        }
    }
}

size_t ew_file_format(const ew_file_t *f, char *out, size_t size)
{
    ew_fi_buf_t b = { out, size, 0u };

    if (f == NULL || out == NULL || f->kind != (uint8_t)EW_FILE_VERIFIED) {
        return 0u;
    }
    ew_fi_put_str(&b, HEADER);
    put_dec(&b, EW_FILE_FORMAT);
    put_char(&b, '\n');
    put_key(&b, KEY_FIRMWARE);
    ew_fi_put_str(&b, ew_fi_fw_word(f->fw));
    put_char(&b, '\n');
    put_key(&b, KEY_LAYOUT);
    put_dec(&b, f->layout);
    put_char(&b, '\n');
    put_identity(&b, f);
    if (f->cpu == (uint8_t)EW_CPU_TEXT) {
        put_keys(&b, f);
    } else {
        put_fields(&b, f);
    }
    if (b.n > size) {
        return 0u;
    }
    const uint32_t crc = ew_file_crc32(out, b.n);

    ew_fi_put_str(&b, CHECKSUM " ");
    ew_fi_put_hex(&b, crc, 8u);
    put_char(&b, '\n');
    return b.n > size ? 0u : b.n;
}
