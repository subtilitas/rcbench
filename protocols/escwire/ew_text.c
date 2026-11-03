/**
 * @file ew_text.c
 * @brief The text interface's lines and replies; see ew_text.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_text.h"

#include <string.h>

/** @brief The start of the first line of `info`. */
#define INFO_HEAD "ESCape32 rev"
/** @brief The separator of the second temperature in `info`. */
#define EXT_MARK ", ext "
/** @brief The most digits of a number on the wire. */
#define NUM_DIGITS_MAX 5u
/** @brief The bit of `music` in the record of the keys seen. */
#define MUSIC_BIT (1ull << EW_E32_KEYS)
/** @brief Every bit of that record. */
#define ALL_KEYS ((MUSIC_BIT << 1) - 1ull)

/**
 * @brief Characters into a line, which holds no NUL.
 * @param out takes them
 * @param s   the characters
 * @return their count
 */
static size_t put_chars(uint8_t *out, const char *s)
{
    size_t i = 0u;

    while (s[i] != '\0') {
        out[i] = (uint8_t)s[i];
        i++;
    }
    return i;
}

size_t ew_text_word(uint8_t *out, size_t size, const char *word)
{
    const size_t n = strlen(word);

    if (n + 1u > size) {
        return 0u;
    }
    (void)put_chars(out, word);
    out[n] = '\n';
    return n + 1u;
}

size_t ew_text_set(uint8_t *out, size_t size, const char *key, int16_t v)
{
    char num[8];
    size_t digits = 0u;
    uint32_t m = v < 0 ? (uint32_t)(-(int32_t)v) : (uint32_t)v;
    const size_t key_n = strlen(key);

    do {
        num[digits++] = (char)('0' + (m % 10u));
        m /= 10u;
    } while (m != 0u);
    if (v < 0) {
        num[digits++] = '-';
    }
    const size_t total = 4u + key_n + 1u + digits + 1u;

    if (total > size) {
        return 0u;
    }
    (void)put_chars(out, "set ");
    (void)put_chars(out + 4, key);
    out[4u + key_n] = ' ';
    for (size_t i = 0; i < digits; ++i) {
        out[4u + key_n + 1u + i] = (uint8_t)num[digits - 1u - i];
    }
    out[total - 1u] = '\n';
    return total;
}

bool ew_text_num(const uint8_t *s, size_t n, int16_t *v)
{
    const bool neg = n > 0u && s[0] == '-';
    const size_t from = neg ? 1u : 0u;
    const size_t digits = n - from;
    int32_t m = 0;

    if (digits == 0u || digits > NUM_DIGITS_MAX
        || (s[from] == '0' && (digits > 1u || neg))) {
        return false;
    }
    for (size_t i = from; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        m = m * 10 + (s[i] - '0');
    }
    if (m > INT16_MAX) {
        return false;
    }
    *v = (int16_t)(neg ? -m : m);
    return true;
}

/**
 * @brief Whether a reply ends with a line.
 * @param rx   the bytes
 * @param n    their count
 * @param word the line, without its LF
 * @param body takes the bytes before it when it does
 * @return true when it does
 */
static bool ends_with(const uint8_t *rx, size_t n, const char *word,
                      size_t *body)
{
    const size_t len = strlen(word);

    if (n < len + 1u || memcmp(rx + n - len - 1u, word, len) != 0
        || rx[n - 1u] != '\n'
        || (n > len + 1u && rx[n - len - 2u] != '\n')) {
        return false;
    }
    *body = n - len - 1u;
    return true;
}

ew_ans_t ew_text_reply(const ew_capture_t *c, const uint8_t *rx, size_t *body)
{
    *body = 0u;
    if (c->aborted || c->overflow) {
        return EW_ANS_ABORTED;
    }
    if (c->n == 0u) {
        return EW_ANS_SILENT;
    }
    if (c->bad_stop != 0u) {
        return EW_ANS_FRAMING;
    }
    for (size_t i = 0; i < c->n; ++i) {
        if (rx[i] != '\n' && (rx[i] < 0x20u || rx[i] > 0x7Eu)) {
            return EW_ANS_OTHER;
        }
    }
    if (ends_with(rx, c->n, "OK", body)) {
        return EW_ANS_OK;
    }
    if (ends_with(rx, c->n, "ERROR", body)) {
        return EW_ANS_TEXT_ERROR;
    }
    return EW_ANS_LENGTH;
}

/**
 * @brief The next line of a body.
 * @param body the body
 * @param n    its bytes
 * @param at   where the line starts; takes where the next one starts
 * @param len  takes the line's length, its LF left out
 * @return the line, NULL at the end
 */
static const uint8_t *next_line(const uint8_t *body, size_t n, size_t *at,
                                size_t *len)
{
    if (*at >= n) {
        return NULL;
    }
    const uint8_t *s = body + *at;
    const uint8_t *lf = memchr(s, '\n', n - *at);

    *len = lf != NULL ? (size_t)(lf - s) : n - *at;
    *at += *len + 1u;
    return s;
}

/**
 * @brief Characters into a string, cut to fit.
 * @param out  takes them and a NUL
 * @param size bytes at @p out
 * @param s    the characters
 * @param n    their count
 */
static void copy_text(char *out, size_t size, const uint8_t *s, size_t n)
{
    const size_t m = n < size - 1u ? n : size - 1u;

    memcpy(out, s, m);
    out[m] = '\0';
}

/**
 * @brief Digits at the start of some characters.
 * @param s   the characters
 * @param n   their count
 * @param max the most the number may be
 * @param v   takes the number
 * @return the digits read, 0 for none or a number over @p max
 */
static size_t read_digits(const uint8_t *s, size_t n, uint32_t max, uint32_t *v)
{
    size_t i = 0u;

    *v = 0u;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        *v = *v * 10u + (uint32_t)(s[i] - '0');
        if (*v > max) {
            return 0u;
        }
        i++;
    }
    return i;
}

/**
 * @brief The first line of `info` behind its head: `<N>.<M> [<target name>]`.
 *        A line of another form leaves the revision not read.
 * @param s   the characters
 * @param n   their count
 * @param out takes the result
 */
static void read_revision(const uint8_t *s, size_t n, ew_text_info_t *out)
{
    uint32_t major = 0u;
    uint32_t minor = 0u;
    const size_t a = read_digits(s, n, 9999u, &major);

    if (a == 0u || a >= n || s[a] != '.') {
        return;
    }
    const size_t b = read_digits(s + a + 1u, n - a - 1u, UINT16_MAX, &minor);
    const size_t end = a + 1u + b;

    if (b == 0u) {
        return;
    }
    if (end < n) {
        const uint8_t *close = memchr(s + end, ']', n - end);

        if (n < end + 3u || s[end] != ' ' || s[end + 1u] != '['
            || close == NULL) {
            return;
        }
        copy_text(out->target, sizeof(out->target), s + end + 2u,
                  (size_t)(close - (s + end + 2u)));
    }
    out->rev_read = true;
    out->rev = major <= UINT8_MAX ? (uint8_t)major : 0u;
    out->sub = (uint16_t)minor;
}

/**
 * @brief A line that starts with a word: its rest into a string.
 * @param s    the line
 * @param n    its length
 * @param head the word
 * @param out  takes the rest, EW_TEXT_LIVE_MAX + 1 bytes
 * @return true when the line starts with the word
 */
static bool live_value(const uint8_t *s, size_t n, const char *head, char *out)
{
    const size_t len = strlen(head);

    if (n < len || memcmp(s, head, len) != 0) {
        return false;
    }
    copy_text(out, EW_TEXT_LIVE_MAX + 1u, s + len, n - len);
    return true;
}

/**
 * @brief A `Temp:` line: the sensor, and the second one when it has one.
 * @param s   the line
 * @param n   its length
 * @param out takes the result
 */
static void read_temp(const uint8_t *s, size_t n, ew_text_info_t *out)
{
    const size_t mark = sizeof(EXT_MARK) - 1u;

    for (size_t i = 0; i + mark <= n; ++i) {
        if (memcmp(s + i, EXT_MARK, mark) == 0) {
            copy_text(out->temp, sizeof(out->temp), s, i);
            copy_text(out->temp_ext, sizeof(out->temp_ext), s + i + mark,
                      n - i - mark);
            return;
        }
    }
    copy_text(out->temp, sizeof(out->temp), s, n);
}

bool ew_text_info(const uint8_t *body, size_t n, ew_text_info_t *out)
{
    static const char k_head[] = INFO_HEAD;
    static const char k_temp[] = "Temp: ";
    const size_t head = sizeof(k_head) - 1u;
    const size_t temp = sizeof(k_temp) - 1u;
    size_t at = 0u;
    size_t len = 0u;

    memset(out, 0, sizeof(*out));
    const uint8_t *s = next_line(body, n, &at, &len);

    if (s == NULL || len < head || memcmp(s, k_head, head) != 0) {
        return false;
    }
    read_revision(s + head, len - head, out);
    while ((s = next_line(body, n, &at, &len)) != NULL) {
        if (len >= temp && memcmp(s, k_temp, temp) == 0) {
            read_temp(s + temp, len - temp, out);
        } else if (!live_value(s, len, "Volt: ", out->volt)
                   && !live_value(s, len, "Curr: ", out->curr)
                   && !live_value(s, len, "Csum: ", out->csum)) {
            (void)live_value(s, len, "ERPM: ", out->erpm);
        }
    }
    return true;
}

bool ew_text_rev_known(const ew_text_info_t *info)
{
    return info->rev_read && ew_map_writable(EW_FW_E32, info->rev);
}

/**
 * @brief Split a line `<key>: <value>`; an empty value may lack its blank.
 * @param s     the line
 * @param n     its length
 * @param key_n takes the key's length
 * @param val   takes where the value starts
 * @param val_n takes the value's length
 * @return true when the line is one
 */
static bool split_line(const uint8_t *s, size_t n, size_t *key_n,
                       const uint8_t **val, size_t *val_n)
{
    const uint8_t *colon = memchr(s, ':', n);

    if (colon == NULL || colon == s) {
        return false;
    }
    *key_n = (size_t)(colon - s);
    const size_t rest = n - *key_n - 1u;

    if (rest == 0u) {
        *val = colon + 1;
        *val_n = 0u;
        return true;
    }
    if (colon[1] != ' ') {
        return false;
    }
    *val = colon + 2;
    *val_n = rest - 1u;
    return true;
}

/**
 * @brief The line of `music`, once.
 * @param val   its value
 * @param val_n the value's length
 * @param seen  the record of keys seen
 * @param music takes the value, may be NULL
 * @return true when it is good
 */
static bool show_music(const uint8_t *val, size_t val_n, uint64_t *seen,
                       char *music)
{
    if ((*seen & MUSIC_BIT) != 0u || val_n > EW_E32_MUSIC_MAX) {
        return false;
    }
    *seen |= MUSIC_BIT;
    if (music != NULL) {
        copy_text(music, EW_E32_MUSIC_MAX + 1u, val, val_n);
    }
    return true;
}

/**
 * @brief One line of `show`.
 * @param s     the line
 * @param n     its length
 * @param page  takes a numeric key's value
 * @param seen  the record of keys seen
 * @param music takes the value of `music`, may be NULL
 * @return true when the line is a key of the map, new, with a good value
 */
static bool show_line(const uint8_t *s, size_t n, uint8_t *page, uint64_t *seen,
                      char *music)
{
    size_t key_n = 0u;
    size_t val_n = 0u;
    size_t slot = 0u;
    const uint8_t *val = NULL;
    int16_t v = 0;

    if (!split_line(s, n, &key_n, &val, &val_n)) {
        return false;
    }
    if (key_n == 5u && memcmp(s, "music", 5u) == 0) {
        return show_music(val, val_n, seen, music);
    }
    if (ew_key_find((const char *)s, key_n, &slot) == NULL
        || (*seen & (1ull << slot)) != 0u || !ew_text_num(val, val_n, &v)) {
        return false;
    }
    *seen |= 1ull << slot;
    ew_e32_put(page, slot, v);
    return true;
}

bool ew_text_show(const uint8_t *body, size_t n, uint8_t *page, char *music)
{
    uint64_t seen = 0u;
    bool whole = true;
    size_t at = 0u;
    size_t len = 0u;
    const uint8_t *s;

    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        ew_e32_put(page, i, EW_KEY_NONE);
    }
    if (music != NULL) {
        music[0] = '\0';
    }
    while ((s = next_line(body, n, &at, &len)) != NULL) {
        if (!show_line(s, len, page, &seen, music)) {
            whole = false;
        }
    }
    return whole && seen == ALL_KEYS;
}

bool ew_text_set_reply(const uint8_t *body, size_t n, const char *key,
                       int16_t *v)
{
    size_t at = 0u;
    size_t len = 0u;
    size_t key_n = 0u;
    size_t val_n = 0u;
    const uint8_t *val = NULL;
    const uint8_t *s = next_line(body, n, &at, &len);

    return s != NULL && at >= n && split_line(s, len, &key_n, &val, &val_n)
           && key_n == strlen(key) && memcmp(s, key, key_n) == 0
           && ew_text_num(val, val_n, v);
}
