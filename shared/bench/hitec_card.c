/**
 * @file
 * @brief The text of the Hitec programmer's record of a write, and its
 *        parse.  See hitec_card.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_card.h"

#include <string.h>

#include "link_crc.h"

/** @brief The first line. */
static const char k_head[] = "RCBENCH HITEC 1\n";
/** @brief The check line's length: "check=" 4 digits and the newline. */
#define CHECK_LEN 11u
/** @brief The highest GPIO of the IOmcu. */
#define PIN_LAST 47u

unsigned hitec_card_addrs(uint8_t series, uint8_t out[HITEC_C_BLOCK_LEN])
{
    if (series == (uint8_t)HITEC_D) {
        size_t n = 0;
        const uint8_t *list = hitec_d_read_list(&n);

        memcpy(out, list, n);
        return (unsigned)n;
    }
    for (unsigned i = 0; i < HITEC_C_BLOCK_LEN; ++i) {
        out[i] = (uint8_t)i;
    }
    return HITEC_C_BLOCK_LEN;
}

/* ------------------------------------------------------------------ text */

/**
 * @brief Append @p s without its terminator.
 *
 * @param at Where it goes.
 * @param s  The text.
 * @return The position after it.
 */
static char *put(char *at, const char *s)
{
    while (*s != '\0') {
        *at++ = *s++;
    }
    return at;
}

/**
 * @brief Append @p v as @p digits upper-case hexadecimal digits.
 *
 * @param at     Where they go.
 * @param v      The value.
 * @param digits The count.
 * @return The position after them.
 */
static char *put_hex(char *at, unsigned v, unsigned digits)
{
    static const char k_hex[] = "0123456789ABCDEF";

    for (unsigned i = digits; i > 0u; --i) {
        *at++ = k_hex[(v >> (4u * (i - 1u))) & 0xFu];
    }
    return at;
}

/**
 * @brief Append the line @p key with the record's addresses of @p img.
 *
 * @param at     Where it goes.
 * @param key    "before=" or "target=".
 * @param series hitec_family_t.
 * @param img    The image.
 * @return The position after the line.
 */
static char *put_image(char *at, const char *key, uint8_t series,
                       const hitec_image_t *img)
{
    uint8_t addrs[HITEC_C_BLOCK_LEN];
    const unsigned n = hitec_card_addrs(series, addrs);

    at = put(at, key);
    for (unsigned i = 0; i < n; ++i) {
        at = series == (uint8_t)HITEC_D
                 ? put_hex(at, hitec_image_reg(img, addrs[i]), 4u)
                 : put_hex(at, hitec_image_cell(img, addrs[i]), 2u);
    }
    *at++ = '\n';
    return at;
}

size_t hitec_card_text(const hitec_card_record_t *r, char *out, size_t n)
{
    char *at = out;

    if (out == NULL || n < HITEC_CARD_TEXT_MAX
        || (r != NULL && (r->series > (uint8_t)HITEC_D || r->port > 15u))) {
        return 0;
    }
    at = put(at, k_head);
    if (r != NULL) {
        at = put(at, r->series == (uint8_t)HITEC_D ? "series=D\n"
                                                  : "series=C\n");
        at = put(at, "port=");
        at = put_hex(at, r->port, 1u);
        at = put(at, "\npin=");
        at = put_hex(at, r->pin, 2u);
        *at++ = '\n';
        at = put_image(at, "before=", r->series, &r->before);
        at = put_image(at, "target=", r->series, &r->target);
    }
    {
        const uint16_t crc = link_crc(LINK_CRC_INIT, out, (size_t)(at - out));

        at = put(at, "check=");
        at = put_hex(at, crc, 4u);
        *at++ = '\n';
    }
    *at = '\0';
    return (size_t)(at - out);
}

/* ----------------------------------------------------------------- parse */

/**
 * @brief The value of @p digits upper-case hexadecimal digits at @p s.
 *
 * @param s      The digits.
 * @param digits Their count, at most 4.
 * @return The value; -1 for a character that is no such digit.
 */
static long take_hex(const char *s, unsigned digits)
{
    long v = 0;

    for (unsigned i = 0; i < digits; ++i) {
        const char c = s[i];
        int d;

        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

/** @brief The text being read: where it is and where its body ends. */
typedef struct {
    const char *s;   /**< the text */
    size_t      at;  /**< the next byte */
    size_t      end; /**< the first byte of the check line */
} reader_t;

/**
 * @brief Take the line "@p key + @p digits digits + newline".
 *
 * @param r      The reader.
 * @param key    The key.
 * @param digits The digits.
 * @param v      Receives their value, for at most 4 digits; NULL to keep
 *               the place only.
 * @return The line's first digit; NULL when it is not there.
 */
static const char *take_line(reader_t *r, const char *key, unsigned digits,
                             long *v)
{
    const size_t k = strlen(key);
    const char *d = r->s + r->at + k;

    if (r->end - r->at < k + digits + 1u || memcmp(r->s + r->at, key, k) != 0
        || d[digits] != '\n') {
        return NULL;
    }
    for (unsigned i = 0; i < digits; ++i) {
        if (take_hex(d + i, 1u) < 0) {
            return NULL;
        }
    }
    if (v != NULL) {
        *v = take_hex(d, digits);
    }
    r->at += k + digits + 1u;
    return d;
}

/**
 * @brief Take an image line into @p img.
 *
 * @param r      The reader.
 * @param key    "before=" or "target=".
 * @param series hitec_family_t.
 * @param img    Receives the addresses, known; emptied first.
 * @return false when the line is not there.
 */
static bool take_image(reader_t *r, const char *key, uint8_t series,
                       hitec_image_t *img)
{
    uint8_t addrs[HITEC_C_BLOCK_LEN];
    const unsigned n = hitec_card_addrs(series, addrs);
    const unsigned w = series == (uint8_t)HITEC_D ? 4u : 2u;
    const char *d = take_line(r, key, n * w, NULL);

    if (d == NULL) {
        return false;
    }
    hitec_image_init(img, series);
    for (unsigned i = 0; i < n; ++i) {
        const long v = take_hex(d + (size_t)i * w, w);

        if (series == (uint8_t)HITEC_D) {
            (void)hitec_image_put_reg(img, addrs[i], (uint16_t)v);
        } else {
            (void)hitec_image_put_cell(img, addrs[i], (uint8_t)v);
        }
    }
    return true;
}

hitec_card_result_t hitec_card_parse(const char *text, size_t len,
                                     hitec_card_record_t *out)
{
    const size_t head = sizeof(k_head) - 1u;
    hitec_card_record_t got;
    reader_t r;
    long v = 0;

    if (text == NULL || len < head || memcmp(text, k_head, head) != 0) {
        return HITEC_CARD_ERR_HEADER;
    }
    if (len < head + CHECK_LEN || text[len - 1u] != '\n'
        || memcmp(text + len - CHECK_LEN, "check=", 6u) != 0) {
        return HITEC_CARD_ERR_TRUNCATED;
    }
    v = take_hex(text + len - 5u, 4u);
    if (v < 0 || (uint16_t)v != link_crc(LINK_CRC_INIT, text, len - CHECK_LEN)) {
        return HITEC_CARD_ERR_CHECK;
    }
    r.s = text;
    r.at = head;
    r.end = len - CHECK_LEN;
    if (r.at == r.end) {
        return HITEC_CARD_NONE;
    }
    memset(&got, 0, sizeof(got));
    if (r.end - r.at < 9u || memcmp(text + r.at, "series=", 7u) != 0
        || (text[r.at + 7u] != 'C' && text[r.at + 7u] != 'D')
        || text[r.at + 8u] != '\n') {
        return HITEC_CARD_ERR_FIELD;
    }
    got.series = text[r.at + 7u] == 'D' ? (uint8_t)HITEC_D
                                        : (uint8_t)HITEC_CLASSIC;
    r.at += 9u;
    if (take_line(&r, "port=", 1u, &v) == NULL) {
        return HITEC_CARD_ERR_FIELD;
    }
    got.port = (uint8_t)v;
    if (take_line(&r, "pin=", 2u, &v) == NULL) {
        return HITEC_CARD_ERR_FIELD;
    }
    got.pin = v <= (long)PIN_LAST ? (uint8_t)v : (uint8_t)HITEC_CARD_NO_PIN;
    if (!take_image(&r, "before=", got.series, &got.before)
        || !take_image(&r, "target=", got.series, &got.target)
        || r.at != r.end) {
        return HITEC_CARD_ERR_FIELD;
    }
    *out = got;
    return HITEC_CARD_OK;
}
