/**
 * @file ew_map.c
 * @brief The lookups over the field tables, and the text of a value; see
 *        ew_map.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_map.h"

#include <string.h>

const ew_field_t *ew_map_table(uint8_t fw, size_t *count)
{
    switch (fw) {
    case EW_FW_S:
        *count = ew_map_s_count;
        return ew_map_s;
    case EW_FW_BJ:
        *count = ew_map_bj_count;
        return ew_map_bj;
    case EW_FW_AM:
        *count = ew_map_am_count;
        return ew_map_am;
    default:
        *count = 0u;
        return NULL;
    }
}

const ew_field_t *ew_map_find(uint8_t fw, uint8_t layout, uint8_t off)
{
    size_t n = 0u;
    const ew_field_t *t = ew_map_table(fw, &n);

    for (size_t i = 0; i < n; ++i) {
        if (t[i].off == off && layout >= t[i].from && layout <= t[i].to) {
            return &t[i];
        }
    }
    return NULL;
}

const ew_field_t *ew_map_nth(uint8_t fw, uint8_t layout, size_t i)
{
    size_t n = 0u;
    size_t seen = 0u;
    const ew_field_t *t = ew_map_table(fw, &n);

    for (size_t k = 0; k < n; ++k) {
        if (layout >= t[k].from && layout <= t[k].to) {
            if (seen == i) {
                return &t[k];
            }
            seen++;
        }
    }
    return NULL;
}

bool ew_map_writable(uint8_t fw, uint8_t layout)
{
    switch (fw) {
    case EW_FW_S:  return layout == 33u;
    case EW_FW_BJ: return layout == 208u;
    case EW_FW_AM: return layout == 3u || layout == 4u;
    default:       return false;
    }
}

uint16_t ew_map_len(uint8_t fw)
{
    switch (fw) {
    case EW_FW_S:  return EW_MAP_LEN_S;
    case EW_FW_BJ: return EW_MAP_LEN_BJ;
    case EW_FW_AM: return EW_MAP_LEN_AM;
    default:       return 0u;
    }
}

bool ew_field_ok(const ew_field_t *f, uint8_t raw)
{
    if (f == NULL) {
        return false;
    }
    return (raw >= f->lo && raw <= f->hi) || (f->has_alt && raw == f->alt);
}

/**
 * @brief Text into a buffer, cut at its end.
 * @param out  the buffer
 * @param size its bytes
 * @param at   characters already there
 * @param s    the text to add
 * @return the characters there now
 */
static size_t add_text(char *out, size_t size, size_t at, const char *s)
{
    while (*s != '\0' && at + 1u < size) {
        out[at++] = *s++;
    }
    out[at] = '\0';
    return at;
}

/**
 * @brief A value in decimal, with @p dec decimal places and no trailing
 *        zeros behind the point.
 * @param out  the buffer
 * @param size its bytes
 * @param at   characters already there
 * @param v    the value
 * @param dec  the decimal places
 * @return the characters there now
 */
static size_t add_number(char *out, size_t size, size_t at, int32_t v,
                         uint8_t dec)
{
    char tmp[16];
    size_t n = 0u;
    uint32_t m = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    uint8_t d = dec;

    /* Trailing zeros of the fraction go first, and the point with them. */
    while (d > 0u && m % 10u == 0u) {
        m /= 10u;
        d--;
    }
    for (uint8_t i = 0u; i < d; ++i) {
        tmp[n++] = (char)('0' + (m % 10u));
        m /= 10u;
    }
    if (d > 0u) {
        tmp[n++] = '.';
    }
    do {
        tmp[n++] = (char)('0' + (m % 10u));
        m /= 10u;
    } while (m != 0u);
    if (v < 0) {
        tmp[n++] = '-';
    }
    while (n > 0u && at + 1u < size) {
        out[at++] = tmp[--n];
    }
    out[at] = '\0';
    return at;
}

size_t ew_field_text(const ew_field_t *f, uint8_t raw, char *out, size_t size)
{
    size_t at = 0u;

    if (size == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (f->has_alt && raw == f->alt) {
        return add_text(out, size, 0u, f->alt_label);
    }
    if (raw < f->lo || raw > f->hi) {
        at = add_text(out, size, 0u, "raw ");
        return add_number(out, size, at, raw, 0u);
    }
    if (f->labels != NULL) {
        return add_text(out, size, 0u, f->labels[raw - f->lo]);
    }
    {
        const int64_t mul = f->mul != 0 ? f->mul : 1;
        const int64_t div = f->div != 0u ? f->div : 1;
        const int64_t v = ((int64_t)raw * mul + f->add) / div;

        at = add_number(out, size, 0u, (int32_t)v, f->dec);
    }
    return add_text(out, size, at, f->unit);
}

ew_img_t ew_image_check(uint8_t fw, uint8_t layout, const uint8_t *orig,
                        const uint8_t *draft, size_t n, uint16_t *off)
{
    for (size_t i = 0; i < n; ++i) {
        if (orig[i] == draft[i]) {
            continue;
        }
        const ew_field_t *f = ew_map_find(fw, layout, (uint8_t)i);
        const ew_img_t r = (f == NULL || f->access != (uint8_t)EW_ACC_EDIT)
                               ? EW_IMG_NOT_EDITABLE
                               : (ew_field_ok(f, draft[i]) ? EW_IMG_OK
                                                           : EW_IMG_VALUE);
        if (r != EW_IMG_OK) {
            if (off != NULL) {
                *off = (uint16_t)i;
            }
            return r;
        }
    }
    return EW_IMG_OK;
}
