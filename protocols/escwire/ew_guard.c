/**
 * @file ew_guard.c
 * @brief The gate; see ew_guard.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_guard.h"

#include <string.h>

#include "ew_a.h"
#include "ew_text.h"

void ew_guard_init(ew_guard_t *g)
{
    memset(g, 0, sizeof(*g));
}

void ew_guard_target(ew_guard_t *g, const ew_contact_t *c)
{
    ew_guard_init(g);
    g->cpu = c->cpu;
    g->base = c->base;
    g->page = c->page;
    g->boot = c->boot;
}

void ew_guard_text_target(ew_guard_t *g)
{
    ew_guard_init(g);
    g->cpu = EW_CPU_TEXT;
}

/**
 * @brief The rules of AM32 on any page that is written.
 * @param page the page
 * @param n    its length
 * @return EW_REF_NONE when they hold
 */
static ew_refuse_t am_page_rules(const uint8_t *page, size_t n)
{
    if (n != EW_MAP_LEN_AM) {
        return EW_REF_AM_LENGTH;
    }
    if (page[0] != 0x01u) {
        return EW_REF_AM_BYTE0;
    }
    if (!ew_map_writable(EW_FW_AM, page[1])) {
        return EW_REF_AM_REVISION;
    }
    return EW_REF_NONE;
}

ew_refuse_t ew_guard_approve(ew_guard_t *g, const ew_ident_t *id,
                             const uint8_t *orig, const uint8_t *draft,
                             size_t n, uint16_t *off)
{
    if (g == NULL || id == NULL || orig == NULL || draft == NULL) {
        return EW_REF_ARG;
    }
    if (id->fw == (uint8_t)EW_FW_AM) {
        const ew_refuse_t r = am_page_rules(draft, n);

        if (r != EW_REF_NONE) {
            return r;
        }
    }
    if (!id->writable || !ew_map_writable(id->fw, id->layout)) {
        return EW_REF_NOT_WRITABLE;
    }
    if (n != ew_map_len(id->fw)) {
        return EW_REF_COUNT;
    }
    switch (ew_image_check(id->fw, id->layout, orig, draft, n, off)) {
    case EW_IMG_NOT_EDITABLE:
        return EW_REF_CHANGED_BYTE;
    case EW_IMG_VALUE:
        return EW_REF_VALUE;
    default:
        break;
    }
    g->approved = draft;
    g->len = (uint16_t)n;
    return EW_REF_NONE;
}

ew_refuse_t ew_guard_approve_restore(ew_guard_t *g, const ew_ident_t *id,
                                     const uint8_t *orig, const uint8_t *page,
                                     size_t n)
{
    if (g == NULL || id == NULL || orig == NULL || page == NULL) {
        return EW_REF_ARG;
    }
    if (id->fw != (uint8_t)EW_FW_AM || !id->erased || id->can_build
        || id->name[0] == '\0') {
        return EW_REF_RESTORE;
    }
    const ew_refuse_t r = am_page_rules(page, n);

    if (r != EW_REF_NONE) {
        return r;
    }
    for (size_t i = 0; i < n; ++i) {
        if (orig[i] != 0xFFu) {
            return EW_REF_RESTORE;
        }
    }
    g->approved = page;
    g->len = (uint16_t)n;
    return EW_REF_NONE;
}

void ew_guard_done(ew_guard_t *g)
{
    g->approved = NULL;
    g->len = 0u;
    g->announced = 0u;
    g->data_sent = false;
}

/**
 * @brief The address rules of an erase and of a program.
 * @param g the guard
 * @return EW_REF_NONE when the standing address is the settings start
 */
static ew_refuse_t write_address(const ew_guard_t *g)
{
    if (!g->addr_set) {
        return EW_REF_NO_ADDRESS;
    }
    if (g->cpu == (uint8_t)EW_CPU_EFM8) {
        if (g->addr < g->page) {
            return EW_REF_FIRST_PAGE;
        }
        if (g->addr >= g->boot) {
            return EW_REF_BOOTLOADER;
        }
    }
    return g->addr == g->base ? EW_REF_NONE : EW_REF_NOT_SETTINGS;
}

/**
 * @brief An address frame.
 * @param g the guard
 * @param f the frame's bytes, the form checked
 * @return EW_REF_NONE when it may go
 */
static ew_refuse_t on_address(ew_guard_t *g, const uint8_t *f)
{
    const uint16_t a = (uint16_t)(((uint16_t)f[2] << 8) | f[3]);

    if (f[1] != 0u) {
        return EW_REF_FORM;
    }
    if (g->cpu == (uint8_t)EW_CPU_ARM && a < EW_AM_ADDR_FLASH
        && a != EW_AM_ADDR_SETTINGS && a != EW_AM_ADDR_NAME
        && a != EW_AM_ADDR_NEXT && a != EW_AM_ADDR_RECORD) {
        return EW_REF_AM_ADDRESS;
    }
    g->addr = a;
    g->addr_set = true;
    return EW_REF_NONE;
}

/**
 * @brief An announcement.
 * @param g the guard
 * @param f the frame's bytes, the form checked
 * @return EW_REF_NONE when it may go
 */
static ew_refuse_t on_announce(ew_guard_t *g, const uint8_t *f)
{
    const uint16_t count = (uint16_t)(((uint16_t)f[2] << 8) | f[3]);

    if (f[1] != 0u) {
        return EW_REF_FORM;
    }
    if (g->cpu == (uint8_t)EW_CPU_ARM && count != EW_MAP_LEN_AM) {
        return EW_REF_AM_LENGTH;
    }
    if (g->approved == NULL) {
        return EW_REF_NOT_APPROVED;
    }
    if (count != g->len) {
        return EW_REF_COUNT;
    }
    const ew_refuse_t r = write_address(g);

    if (r != EW_REF_NONE) {
        return r;
    }
    g->announced = count;
    g->data_sent = false;
    return EW_REF_NONE;
}

/**
 * @brief A frame of a command and its argument.
 * @param g the guard
 * @param f the frame's bytes, the form checked
 * @return EW_REF_NONE when it may go
 */
static ew_refuse_t on_short(ew_guard_t *g, const uint8_t *f)
{
    ew_refuse_t r;

    switch (f[0]) {
    case EW_A_CMD_READ:
        if (!g->addr_set) {
            return EW_REF_NO_ADDRESS;
        }
        g->addr_set = false;
        return EW_REF_NONE;
    case EW_A_CMD_ERASE:
        if (g->cpu == (uint8_t)EW_CPU_ARM) {
            return EW_REF_ERASE_AM;
        }
        if (f[1] != 0x01u) {
            return EW_REF_FORM;
        }
        r = write_address(g);
        if (r == EW_REF_NONE && g->approved == NULL) {
            r = EW_REF_NOT_APPROVED;
        }
        if (r == EW_REF_NONE) {
            g->addr_set = false;
        }
        return r;
    case EW_A_CMD_PROGRAM:
        if (f[1] != 0x01u) {
            return EW_REF_FORM;
        }
        r = write_address(g);
        if (r == EW_REF_NONE && !g->data_sent) {
            r = EW_REF_NO_DATA;
        }
        if (r == EW_REF_NONE) {
            g->addr_set = false;
            g->data_sent = false;
        }
        return r;
    case EW_A_CMD_KEEP:
        return f[1] == 0u ? EW_REF_NONE : EW_REF_FORM;
    default: /* EW_A_CMD_LEAVE */
        if (g->approved != NULL) {
            return EW_REF_HALF_WRITTEN;
        }
        if ((g->cpu == (uint8_t)EW_CPU_ARM && f[1] != 0u)
            || (g->cpu == (uint8_t)EW_CPU_EFM8 && f[1] != 1u)) {
            return EW_REF_LEAVE_ARG;
        }
        return EW_REF_NONE;
    }
}

ew_refuse_t ew_guard_frame(ew_guard_t *g, const uint8_t *f, size_t n)
{
    if (g == NULL || f == NULL || n == 0u) {
        return EW_REF_ARG;
    }
    if (g->cpu == (uint8_t)EW_CPU_TEXT) {
        return EW_REF_INTERFACE;
    }
    if (g->cpu == (uint8_t)EW_CPU_NONE) {
        uint8_t contact[EW_A_CONTACT_LEN];

        (void)ew_a_contact(contact);
        return (n == sizeof(contact) && memcmp(f, contact, n) == 0)
                   ? EW_REF_NONE
                   : EW_REF_NO_CONTACT;
    }
    if (g->announced != 0u) {
        if (n != (size_t)g->announced + 2u
            || memcmp(f, g->approved, g->announced) != 0) {
            return EW_REF_DATA;
        }
        if (ew_crc16(f, n) != 0u) {
            return EW_REF_CRC;
        }
        g->announced = 0u;
        g->data_sent = true;
        return EW_REF_NONE;
    }
    switch (f[0]) {
    case EW_A_CMD_ADDRESS:
    case EW_A_CMD_ANNOUNCE:
        if (n != EW_A_LONG_FRAME) {
            return EW_REF_FORM;
        }
        if (ew_crc16(f, n) != 0u) {
            return EW_REF_CRC;
        }
        return f[0] == EW_A_CMD_ADDRESS ? on_address(g, f) : on_announce(g, f);
    case EW_A_CMD_READ:
    case EW_A_CMD_ERASE:
    case EW_A_CMD_PROGRAM:
    case EW_A_CMD_KEEP:
    case EW_A_CMD_LEAVE:
        if (n != EW_A_SHORT_FRAME) {
            return EW_REF_FORM;
        }
        if (ew_crc16(f, n) != 0u) {
            return EW_REF_CRC;
        }
        return on_short(g, f);
    default:
        return EW_REF_COMMAND;
    }
}

/**
 * @brief Whether a line is exactly a word.
 * @param s    the line, without its LF
 * @param n    its length
 * @param word the word
 * @return true when it is
 */
static bool is_word(const uint8_t *s, size_t n, const char *word)
{
    return n == strlen(word) && memcmp(s, word, n) == 0;
}

/**
 * @brief Split `set <key> <value>`, one blank between words: a key of the map
 *        and one of its values.
 * @param s    the line, without its LF
 * @param n    its length
 * @param slot takes the key's place
 * @param v    takes the value
 * @return true when the line is one
 */
static bool parse_set(const uint8_t *s, size_t n, size_t *slot, int16_t *v)
{
    static const char k_set[] = "set ";
    const size_t head = sizeof(k_set) - 1u;

    if (n <= head || memcmp(s, k_set, head) != 0) {
        return false;
    }
    const uint8_t *blank = memchr(s + head, ' ', n - head);

    if (blank == NULL) {
        return false;
    }
    const size_t key_n = (size_t)(blank - (s + head));
    const ew_key_t *k = ew_key_find((const char *)(s + head), key_n, slot);

    return ew_text_num(blank + 1, n - head - key_n - 1u, v) && ew_key_ok(k, *v);
}

ew_refuse_t ew_guard_line(const uint8_t *line, size_t n)
{
    if (line == NULL || n == 0u) {
        return EW_REF_ARG;
    }
    if (n - 1u > EW_LINE_MAX) {
        return EW_REF_LINE_LONG;
    }
    if (line[n - 1u] != '\n') {
        return EW_REF_LINE;
    }
    const size_t len = n - 1u;
    size_t slot = 0u;
    int16_t v = 0;

    for (size_t i = 0; i < len; ++i) {
        if (line[i] < 0x20u || line[i] > 0x7Eu) {
            return EW_REF_LINE;
        }
    }
    if (is_word(line, len, "info") || is_word(line, len, "show")
        || is_word(line, len, "save") || is_word(line, len, "help")
        || is_word(line, len, "get music")
        || parse_set(line, len, &slot, &v)) {
        return EW_REF_NONE;
    }
    if (len > 4u && memcmp(line, "get ", 4u) == 0
        && ew_key_find((const char *)(line + 4), len - 4u, NULL) != NULL) {
        return EW_REF_NONE;
    }
    return EW_REF_LINE;
}

ew_refuse_t ew_guard_text(const ew_guard_t *g, const uint8_t *line, size_t n)
{
    if (g == NULL) {
        return EW_REF_ARG;
    }
    const ew_refuse_t r = ew_guard_line(line, n);

    if (r != EW_REF_NONE) {
        return r;
    }
    const size_t len = n - 1u;

    if (g->cpu == (uint8_t)EW_CPU_EFM8 || g->cpu == (uint8_t)EW_CPU_ARM) {
        return EW_REF_INTERFACE;
    }
    if (g->cpu == (uint8_t)EW_CPU_NONE) {
        return is_word(line, len, "info") ? EW_REF_NONE : EW_REF_NO_CONTACT;
    }
    if (is_word(line, len, "save")) {
        return g->approved != NULL ? EW_REF_NONE : EW_REF_NOT_APPROVED;
    }
    size_t slot = 0u;
    int16_t v = 0;

    if (parse_set(line, len, &slot, &v)) {
        if (g->approved == NULL) {
            return EW_REF_NOT_APPROVED;
        }
        return ew_e32_get(g->approved, slot) == v ? EW_REF_NONE : EW_REF_DATA;
    }
    return EW_REF_NONE;
}
