/**
 * @file
 * @brief HC-05 answer lines and Bluetooth addresses: the line parser, the
 *        two address forms and the fields of an +INQ line.
 *
 * An answer line is OK, FAIL, ERROR:(hh) or a `+KEY:value` line.  Firmware
 * 2.0 prints the error code as `ERROR:(hh)`; `ERROR(hh)` and a bare `ERROR`
 * are taken as well, the bare one with code 0.  An address is NAP:UAP:LAP
 * (non-significant, upper and lower address part), 1 to 4, 1 to 2 and 1 to
 * 6 hex digits, with colons as +INQ prints it and commas as a set command
 * takes it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "hc05.h"
#include "hc05_int.h"

/** @brief The most hex digits of each address part: NAP, UAP, LAP. */
static const unsigned k_part_max[3] = { 4u, 2u, 6u };

/** @brief A `+KEY:` prefix and the kind of line it starts. */
typedef struct {
    const char *prefix;  /**< the text the line starts with */
    hc05_line_t kind;    /**< the kind it gives             */
} prefix_t;

/** @brief The `+KEY:` lines hc05_parse_line() knows. */
static const prefix_t k_prefixes[] = {
    { "+INQ:", HC05_LINE_INQ },
    { "+RNAME:", HC05_LINE_RNAME },
    { "+VERSION:", HC05_LINE_VERSION },
    { "+STATE:", HC05_LINE_STATE },
    { "+BIND:", HC05_LINE_BIND },
};

/**
 * @brief The value of a hex digit.
 * @param c The character.
 * @return 0 to 15, or -1 when @p c is no hex digit.
 */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/**
 * @brief Read 1 to @p max hex digits from *@p p, which is moved past them.
 * @param p   The text; on return the first byte that is no hex digit.
 * @param max The most digits taken, 1 to 8.
 * @param v   Receives the value.
 * @return false for no digit or more than @p max.
 */
static bool read_hex(const char **p, unsigned max, uint32_t *v)
{
    uint32_t acc = 0;
    unsigned n = 0;
    for (int d = hex_digit(**p); d >= 0; d = hex_digit(**p)) {
        if (++n > max) {
            return false;
        }
        acc = (acc << 4) | (uint32_t)d;
        ++*p;
    }
    *v = acc;
    return n > 0u;
}

/**
 * @brief Check an address in either form and write it with @p sep.
 * @param addr The address, its parts joined by colons or by commas, one
 *             kind in one address.
 * @param sep  The separator written.
 * @param out  Receives the address; "" on false when @p cap is not 0.
 * @param cap  The size of @p out.
 * @return false for another form, NULL, or @p out too small.
 */
static bool addr_convert(const char *addr, char sep, char *out, size_t cap)
{
    char buf[HC05_ADDR_MAX];
    size_t n = 0;
    unsigned part = 0;
    unsigned digits = 0;
    char seen = '\0';

    if (out != NULL && cap > 0u) {
        out[0] = '\0';
    }
    if (addr == NULL || out == NULL) {
        return false;
    }
    for (const char *p = addr;; ++p) {
        const char c = *p;
        if (c == '\0' || c == ':' || c == ',') {
            if (digits == 0u) {
                return false;
            }
            if (c == '\0') {
                break;
            }
            if (part == 2u || (seen != '\0' && c != seen)) {
                return false;
            }
            seen = c;
            buf[n++] = sep;
            ++part;
            digits = 0;
        } else if (hex_digit(c) >= 0 && digits < k_part_max[part]) {
            buf[n++] = c;
            ++digits;
        } else {
            return false;
        }
    }
    if (part != 2u || n + 1u > cap) {
        return false;
    }
    buf[n] = '\0';
    memcpy(out, buf, n + 1u);
    return true;
}

bool hc05_addr_cmd(const char *addr, char *out, size_t cap)
{
    return addr_convert(addr, ',', out, cap);
}

bool hc05_addr_show(const char *addr, char *out, size_t cap)
{
    return addr_convert(addr, ':', out, cap);
}

void hc05_copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    while (n + 1u < cap && src[n] != '\0') {
        const unsigned char c = (unsigned char)src[n];
        dst[n] = (c >= 0x20u && c <= 0x7Eu) ? (char)c : '?';
        ++n;
    }
    dst[n] = '\0';
}

bool hc05_inq_fields(const char *value, hc05_dev_t *dev)
{
    hc05_dev_t d;
    char addr[HC05_ADDR_MAX];
    const char *comma = strchr(value, ',');
    uint32_t cls = 0;
    uint32_t rssi = 0x7FFFu;

    if (comma == NULL || (size_t)(comma - value) >= sizeof addr) {
        return false;
    }
    memcpy(addr, value, (size_t)(comma - value));
    addr[comma - value] = '\0';
    memset(&d, 0, sizeof d);
    if (!hc05_addr_show(addr, d.addr, sizeof d.addr)) {
        return false;
    }
    const char *p = comma + 1;
    if (!read_hex(&p, 6u, &cls)) {
        return false;
    }
    if (*p == ',') {
        ++p;
        if (!read_hex(&p, 4u, &rssi)) {
            return false;
        }
    }
    if (*p != '\0' && *p != ',') {
        return false;
    }
    d.dev_class = (uint16_t)(cls & 0xFFFFu);
    d.rssi = (int16_t)(rssi >= 0x8000u ? (int32_t)rssi - 0x10000
                                       : (int32_t)rssi);
    *dev = d;
    return true;
}

/**
 * @brief Read what follows `ERROR`: nothing, `:(hh)` or `(hh)`.
 * @param rest The text after `ERROR`.
 * @param code Receives the code, 0 for a bare `ERROR`; may be NULL.
 * @return HC05_LINE_ERROR, or HC05_LINE_OTHER for another form.
 */
static hc05_line_t parse_error(const char *rest, uint8_t *code)
{
    uint32_t v = 0;
    if (*rest == ':') {
        ++rest;
    }
    if (*rest != '\0') {
        if (*rest != '(') {
            return HC05_LINE_OTHER;
        }
        ++rest;
        if (!read_hex(&rest, 2u, &v) || strcmp(rest, ")") != 0) {
            return HC05_LINE_OTHER;
        }
    } else if (rest[-1] == ':') {
        return HC05_LINE_OTHER;
    }
    if (code != NULL) {
        *code = (uint8_t)v;
    }
    return HC05_LINE_ERROR;
}

hc05_line_t hc05_parse_line(const char *line, char *value, size_t cap,
                            uint8_t *code)
{
    if (value != NULL && cap > 0u) {
        value[0] = '\0';
    }
    if (code != NULL) {
        *code = 0;
    }
    if (line == NULL) {
        return HC05_LINE_OTHER;
    }
    if (strcmp(line, "OK") == 0) {
        return HC05_LINE_OK;
    }
    if (strcmp(line, "FAIL") == 0) {
        return HC05_LINE_FAIL;
    }
    if (strncmp(line, "ERROR", 5) == 0) {
        return parse_error(line + 5, code);
    }
    for (size_t i = 0; i < sizeof k_prefixes / sizeof k_prefixes[0]; ++i) {
        const size_t n = strlen(k_prefixes[i].prefix);
        if (strncmp(line, k_prefixes[i].prefix, n) == 0) {
            if (value != NULL && cap > 0u) {
                const char *src = line + n;
                size_t k = 0;
                while (k + 1u < cap && src[k] != '\0') {
                    value[k] = src[k];
                    ++k;
                }
                value[k] = '\0';
            }
            return k_prefixes[i].kind;
        }
    }
    return HC05_LINE_OTHER;
}
