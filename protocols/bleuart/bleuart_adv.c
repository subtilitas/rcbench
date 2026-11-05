/**
 * @file
 * @brief The data side of the BLE central: the service UUIDs, the
 *        advertising data parser, the scan list and the address text.
 *
 * An AD structure is a length byte, a type byte and length - 1 data bytes.
 * The parser holds each structure to the report: a length 0 ends the data
 * (the rest is padding), a length past the end drops the rest.  A name is
 * cut to 23 bytes and every byte outside printable ASCII is shown as `?`.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "bleuart.h"

/** @brief The ISSC Transparent UART service,
 *         49535343-FE7D-4AE5-8FA9-9FAFD205E455. */
const uint8_t bleu_uuid_svc[16] = {
    0x55, 0xE4, 0x05, 0xD2, 0xAF, 0x9F, 0xA9, 0x8F,
    0xE5, 0x4A, 0x7D, 0xFE, 0x43, 0x53, 0x53, 0x49,
};
/** @brief Transfer TX, 49535343-1E4D-4BD9-BA61-23C647249616. */
const uint8_t bleu_uuid_tx[16] = {
    0x16, 0x96, 0x24, 0x47, 0xC6, 0x23, 0x61, 0xBA,
    0xD9, 0x4B, 0x4D, 0x1E, 0x43, 0x53, 0x53, 0x49,
};
/** @brief Transfer RX, 49535343-8841-43F4-A8D4-ECBE34729BB3. */
const uint8_t bleu_uuid_rx[16] = {
    0xB3, 0x9B, 0x72, 0x34, 0xBE, 0xEC, 0xD4, 0xA8,
    0xF4, 0x43, 0x41, 0x88, 0x43, 0x53, 0x53, 0x49,
};
/** @brief Air Patch, 49535343-ACA3-481C-91EC-D85E28A60318. */
const uint8_t bleu_uuid_air[16] = {
    0x18, 0x03, 0xA6, 0x28, 0x5E, 0xD8, 0xEC, 0x91,
    0x1C, 0x48, 0xA3, 0xAC, 0x43, 0x53, 0x53, 0x49,
};
/** @brief Connection Parameter, 49535343-6DAA-4D02-ABF6-19569ACA69FE. */
const uint8_t bleu_uuid_param[16] = {
    0xFE, 0x69, 0xCA, 0x9A, 0x56, 0x19, 0xF6, 0xAB,
    0x02, 0x4D, 0xAA, 0x6D, 0x43, 0x53, 0x53, 0x49,
};

/** @brief AD types the parser reads. @{ */
#define AD_FLAGS      0x01u /**< flags */
#define AD_UUID16_ANY 0x02u /**< incomplete 16-bit UUID list */
#define AD_UUID16_ALL 0x03u /**< complete 16-bit UUID list */
#define AD_UUID128_ANY 0x06u /**< incomplete 128-bit UUID list */
#define AD_UUID128_ALL 0x07u /**< complete 128-bit UUID list */
#define AD_NAME_SHORT 0x08u /**< shortened local name */
#define AD_NAME_FULL  0x09u /**< complete local name */
/** @} */

/** @brief The RSSI value HCI gives when it has none. */
#define RSSI_UNKNOWN 127

/**
 * @brief Copy a name, cut to BLEU_NAME_MAX - 1 bytes, each byte outside
 *        0x20 to 0x7E as `?`.
 * @param out Receives it and its NUL, BLEU_NAME_MAX bytes.
 * @param src The name's bytes.
 * @param n   Their count.
 */
static void copy_name(char *out, const uint8_t *src, size_t n)
{
    if (n > BLEU_NAME_MAX - 1u) {
        n = BLEU_NAME_MAX - 1u;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = (src[i] >= 0x20u && src[i] <= 0x7Eu) ? (char)src[i] : '?';
    }
    out[n] = '\0';
}

bool bleu_adv_parse(const uint8_t *ad, size_t len, bleu_adv_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (ad == NULL) {
        return false;
    }
    size_t pos = 0;
    while (pos < len) {
        const size_t n = ad[pos];
        if (n == 0u) {
            break;
        }
        if (n > len - pos - 1u) {
            out->cut = true;
            break;
        }
        const uint8_t type = ad[pos + 1u];
        const uint8_t *d = &ad[pos + 2u];
        const size_t dn = n - 1u;
        if (type == AD_FLAGS && dn >= 1u) {
            out->flags = d[0];
        } else if (type == AD_UUID16_ANY || type == AD_UUID16_ALL) {
            for (size_t i = 0; i + 2u <= dn; i += 2u) {
                if ((uint16_t)(d[i] | (d[i + 1u] << 8)) == BLEU_UUID16_ADV) {
                    out->has_18f0 = true;
                }
            }
        } else if (type == AD_UUID128_ANY || type == AD_UUID128_ALL) {
            for (size_t i = 0; i + 16u <= dn; i += 16u) {
                if (memcmp(&d[i], bleu_uuid_svc, 16u) == 0) {
                    out->has_issc = true;
                }
            }
        } else if (type == AD_NAME_FULL
                   || (type == AD_NAME_SHORT
                       && out->name_kind != AD_NAME_FULL)) {
            copy_name(out->name, d, dn);
            out->name_kind = type;
        }
        pos += n + 1u;
    }
    return out->has_18f0 || out->has_issc;
}

bool bleu_addr_eq(const bleu_addr_t *a, const bleu_addr_t *b)
{
    return a != NULL && b != NULL && a->type == b->type
           && memcmp(a->val, b->val, sizeof a->val) == 0;
}

void bleu_list_clear(bleu_list_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof *l);
    }
}

/**
 * @brief The RSSI a list sorts by: an unknown one below every measured.
 * @param rssi The report's RSSI.
 * @return The key, higher is stronger.
 */
static int rssi_key(int8_t rssi)
{
    return rssi == RSSI_UNKNOWN ? -129 : rssi;
}

/**
 * @brief Move found[i] up or down until the list is strongest first again;
 *        equal RSSI keeps the older row first.
 * @param l The list.
 * @param i The row whose RSSI changed.
 */
static void resort(bleu_list_t *l, size_t i)
{
    bleu_dev_t *const f = l->found;
    const size_t n = l->found_n;
    while (i > 0u && rssi_key(f[i].rssi) > rssi_key(f[i - 1u].rssi)) {
        const bleu_dev_t t = f[i];
        f[i] = f[i - 1u];
        f[i - 1u] = t;
        --i;
    }
    while (i + 1u < n && rssi_key(f[i].rssi) < rssi_key(f[i + 1u].rssi)) {
        const bleu_dev_t t = f[i];
        f[i] = f[i + 1u];
        f[i + 1u] = t;
        ++i;
    }
}

/**
 * @brief Merge one report into a listed row.
 * @param d    The row.
 * @param rssi The report's RSSI.
 * @param adv  What the report says.
 */
static void merge(bleu_dev_t *d, int8_t rssi, const bleu_adv_t *adv)
{
    if (rssi != RSSI_UNKNOWN) {
        d->rssi = rssi;
    }
    d->flags |= (uint8_t)((adv->has_18f0 ? BLEU_DEV_18F0 : 0u)
                          | (adv->has_issc ? BLEU_DEV_ISSC : 0u));
    if (adv->name_kind == AD_NAME_FULL
        || (adv->name_kind == AD_NAME_SHORT
            && (d->flags & BLEU_DEV_NAME_FULL) == 0u)) {
        memcpy(d->name, adv->name, sizeof d->name);
        if (adv->name_kind == AD_NAME_FULL) {
            d->flags |= BLEU_DEV_NAME_FULL;
        }
    }
}

/**
 * @brief Remember an address the list does not hold.
 * @param l       The list.
 * @param a       The address.
 * @param matched It matched and the list had no room.
 * @return false when the table is full.
 */
static bool remember(bleu_list_t *l, const bleu_addr_t *a, bool matched)
{
    if (l->seen_n >= BLEU_SEEN_MAX) {
        return false;
    }
    l->seen[l->seen_n].addr = *a;
    l->seen[l->seen_n].matched = matched;
    ++l->seen_n;
    return true;
}

void bleu_list_report(bleu_list_t *l, const bleu_addr_t *addr, int8_t rssi,
                      const uint8_t *ad, size_t len)
{
    if (l == NULL || addr == NULL) {
        return;
    }
    ++l->reports;
    bleu_adv_t adv;
    const bool match = bleu_adv_parse(ad, len, &adv);
    for (size_t i = 0; i < l->found_n; ++i) {
        if (bleu_addr_eq(&l->found[i].addr, addr)) {
            merge(&l->found[i], rssi, &adv);
            resort(l, i);
            return;
        }
    }
    size_t seen = BLEU_SEEN_MAX;
    for (size_t i = 0; i < l->seen_n; ++i) {
        if (bleu_addr_eq(&l->seen[i].addr, addr)) {
            seen = i;
        }
    }
    if (!match) {
        if (seen == BLEU_SEEN_MAX) {
            (void)remember(l, addr, false);
            ++l->hidden;
        }
        return;
    }
    if (seen != BLEU_SEEN_MAX) {
        if (l->seen[seen].matched) {
            return;
        }
        /* An other device that now shows the service: listed, no longer
         * hidden. */
        l->seen[seen] = l->seen[l->seen_n - 1u];
        --l->seen_n;
        --l->hidden;
    }
    bleu_dev_t d;
    memset(&d, 0, sizeof d);
    d.addr = *addr;
    d.rssi = RSSI_UNKNOWN;
    merge(&d, rssi, &adv);
    if (l->found_n == BLEU_FOUND_MAX) {
        bleu_dev_t *last = &l->found[BLEU_FOUND_MAX - 1u];
        if (rssi_key(d.rssi) <= rssi_key(last->rssi)) {
            (void)remember(l, addr, true);
            return;
        }
        (void)remember(l, &last->addr, true);
        --l->found_n;
    }
    l->found[l->found_n] = d;
    ++l->found_n;
    resort(l, l->found_n - 1u);
}

/** @brief The hex digits addresses are written with. */
static const char k_hex[] = "0123456789ABCDEF";

size_t bleu_addr_format(const bleu_addr_t *a, bool with_type, char *out,
                        size_t cap)
{
    const size_t n = with_type ? 19u : 17u;
    if (a == NULL || out == NULL || cap < n + 1u
        || (with_type && a->type > BLEU_ADDR_RANDOM)) {
        return 0;
    }
    for (size_t i = 0; i < 6u; ++i) {
        const uint8_t v = a->val[5u - i];
        out[i * 3u] = k_hex[v >> 4];
        out[i * 3u + 1u] = k_hex[v & 0x0Fu];
        if (i < 5u) {
            out[i * 3u + 2u] = ':';
        }
    }
    if (with_type) {
        out[17] = ' ';
        out[18] = a->type == BLEU_ADDR_RANDOM ? 'R' : 'P';
    }
    out[n] = '\0';
    return n;
}

/**
 * @brief The value of one hex digit.
 * @param c The character.
 * @return 0 to 15, or -1 for a character that is not a hex digit.
 */
static int hex_val(char c)
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

bool bleu_addr_parse(const char *s, bleu_addr_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    bleu_addr_t a;
    for (size_t i = 0; i < 6u; ++i) {
        const int hi = hex_val(s[i * 3u]);
        const int lo = hi < 0 ? -1 : hex_val(s[i * 3u + 1u]);
        const char sep = (i < 5u) ? ':' : ' ';
        if (lo < 0 || s[i * 3u + 2u] != sep) {
            return false;
        }
        a.val[5u - i] = (uint8_t)(hi << 4 | lo);
    }
    if (s[18] == 'P') {
        a.type = BLEU_ADDR_PUBLIC;
    } else if (s[18] == 'R') {
        a.type = BLEU_ADDR_RANDOM;
    } else {
        return false;
    }
    if (s[19] != '\0') {
        return false;
    }
    *out = a;
    return true;
}
