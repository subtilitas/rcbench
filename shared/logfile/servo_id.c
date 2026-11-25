/**
 * @file
 * @brief A servo's id: checked, built, taken apart, and counted off the
 *        card.  See servo_id.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_id.h"

#include <stdio.h>
#include <string.h>

#include "card_tree.h"

_Static_assert(SERVO_ID_PREFIX_MAX + 1u + SERVO_ID_SERIAL_LEN == SERVO_ID_MAX,
               "the longest id is the longest prefix, the dash and a serial");
_Static_assert(SERVO_ID_SIZE == SERVO_ID_MAX + 1u, "an id and its terminator");
_Static_assert(sizeof(CARD_SERVOS) + SERVO_ID_MAX + 1u <= CARD_RUN_DIR_MAX,
               "Servos/<id> fits a run's folder");

/** @brief The base of a serial. */
#define BASE 36u

/**
 * @brief The length of @p s, counted up to @p max + 1 at most.
 * @param s   The text.
 * @param max The longest length of interest.
 * @return The length; @p max + 1 for any longer text.
 */
static size_t len_to(const char *s, size_t max)
{
    size_t n = 0u;
    while (n <= max && s[n] != '\0') {
        ++n;
    }
    return n;
}

/**
 * @brief A character in upper case.
 * @param c The character.
 * @return @p c, a-z taken as A-Z.
 */
static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/**
 * @brief The value of one serial character.
 * @param c The character, any case.
 * @return 0 to 35; -1 outside 0-9, A-Z and a-z.
 */
static int digit_of(char c)
{
    const char u = upper(c);
    if (u >= '0' && u <= '9') {
        return u - '0';
    }
    if (u >= 'A' && u <= 'Z') {
        return u - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Whether a character may stand in a prefix.
 * @param c The upper-case character.
 * @return true for A-Z, 0-9, space, "-", "_" and ".".
 */
static bool prefix_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' '
           || c == '-' || c == '_' || c == '.';
}

/**
 * @brief The prefix rules on @p len characters of @p in.
 * @param in  The prefix; not terminated.
 * @param len Its length.
 * @return SERVO_ID_OK or the first rule broken.
 */
static servo_id_err_t prefix_rules(const char *in, size_t len)
{
    if (len == 0u) {
        return SERVO_ID_EMPTY;
    }
    if (len > SERVO_ID_PREFIX_MAX) {
        return SERVO_ID_LONG;
    }
    for (size_t i = 0u; i < len; ++i) {
        if (!prefix_char(upper(in[i]))) {
            return SERVO_ID_CHAR;
        }
    }
    if (in[0] == ' ' || in[0] == '.' || in[len - 1u] == ' ') {
        return SERVO_ID_EDGE;
    }
    return SERVO_ID_OK;
}

/**
 * @brief Copy @p len characters of @p in in upper case, and the terminator.
 * @param out Receives them; room for @p len + 1.
 * @param in  The text.
 * @param len Characters to copy.
 */
static void copy_upper(char *out, const char *in, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        out[i] = upper(in[i]);
    }
    out[len] = '\0';
}

servo_id_err_t servo_id_prefix_check(const char *in, char *out, size_t n)
{
    if (in == NULL) {
        return SERVO_ID_EMPTY;
    }
    const size_t len = len_to(in, SERVO_ID_PREFIX_MAX);
    const servo_id_err_t e = prefix_rules(in, len);
    if (e != SERVO_ID_OK) {
        return e;
    }
    if (out != NULL) {
        if (n <= len) {
            return SERVO_ID_LONG;
        }
        copy_upper(out, in, len);
    }
    return SERVO_ID_OK;
}

bool servo_id_serial_text(uint32_t serial, char out[6])
{
    if (out == NULL) {
        return false;
    }
    out[0] = '\0';
    if (serial >= SERVO_ID_SERIALS) {
        return false;
    }
    static const char k_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = SERVO_ID_SERIAL_LEN; i > 0u; --i) {
        out[i - 1u] = k_digits[serial % BASE];
        serial /= BASE;
    }
    out[SERVO_ID_SERIAL_LEN] = '\0';
    return true;
}

uint32_t servo_id_serial_parse(const char *five)
{
    if (five == NULL) {
        return SERVO_ID_NO_SERIAL;
    }
    uint32_t v = 0u;
    for (size_t i = 0u; i < SERVO_ID_SERIAL_LEN; ++i) {
        const int d = digit_of(five[i]);   /* the terminator is no digit */
        if (d < 0) {
            return SERVO_ID_NO_SERIAL;
        }
        v = v * BASE + (uint32_t)d;
    }
    return (five[SERVO_ID_SERIAL_LEN] == '\0') ? v : SERVO_ID_NO_SERIAL;
}

bool servo_id_build(char *out, size_t n, const char *prefix, uint32_t serial)
{
    if (out == NULL || n == 0u) {
        return false;
    }
    out[0] = '\0';
    char p[SERVO_ID_PREFIX_MAX + 1u];
    char s[SERVO_ID_SERIAL_LEN + 1u];
    if (servo_id_prefix_check(prefix, p, sizeof(p)) != SERVO_ID_OK
        || !servo_id_serial_text(serial, s)) {
        return false;
    }
    const size_t len = strlen(p) + 1u + SERVO_ID_SERIAL_LEN;
    if (n <= len) {
        return false;
    }
    (void)snprintf(out, n, "%s-%s", p, s);
    return true;
}

servo_id_err_t servo_id_why(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return SERVO_ID_EMPTY;
    }
    const size_t len = len_to(text, SERVO_ID_MAX);
    if (len > SERVO_ID_MAX) {
        return SERVO_ID_LONG;
    }
    const char *dash = strrchr(text, '-');
    if (dash == NULL || servo_id_serial_parse(dash + 1) == SERVO_ID_NO_SERIAL) {
        return SERVO_ID_SERIAL;
    }
    return prefix_rules(text, (size_t)(dash - text));
}

bool servo_id_parse(const char *text, servo_id_t *out)
{
    if (servo_id_why(text) != SERVO_ID_OK) {
        return false;
    }
    if (out != NULL) {
        const size_t len = strlen(text);
        const size_t plen = len - 1u - SERVO_ID_SERIAL_LEN;
        copy_upper(out->text, text, len);
        copy_upper(out->prefix, text, plen);
        out->serial = servo_id_serial_parse(text + plen + 1u);
    }
    return true;
}

bool servo_id_next(uint32_t highest, uint32_t *next)
{
    uint32_t v;
    if (highest == SERVO_ID_NO_SERIAL) {
        v = 0u;
    } else if (highest + 1u < SERVO_ID_SERIALS) {
        v = highest + 1u;
    } else {
        return false;
    }
    if (next != NULL) {
        *next = v;
    }
    return true;
}

bool servo_id_folder(char *out, size_t n, const char *id)
{
    if (out == NULL || n == 0u) {
        return false;
    }
    out[0] = '\0';
    servo_id_t p;
    if (!servo_id_parse(id, &p)) {
        return false;
    }
    const int k = snprintf(out, n, "%s/%s", CARD_SERVOS, p.text);
    if (k < 0 || (size_t)k >= n) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void servo_ids_begin(servo_ids_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->highest = SERVO_ID_NO_SERIAL;
    }
}

void servo_ids_visit(servo_ids_t *s, const char *name, bool is_dir)
{
    servo_id_t id;
    if (s == NULL || !is_dir || !servo_id_parse(name, &id)) {
        return;
    }
    if (s->count < UINT16_MAX) {
        ++s->count;
    }
    if (s->highest == SERVO_ID_NO_SERIAL || id.serial > s->highest) {
        s->highest = id.serial;
    }
    /* Where it goes among the highest: after every serial at or above it. */
    size_t at = 0u;
    while (at < s->recent_n
           && servo_id_serial_parse(strrchr(s->recent[at], '-') + 1)
                  >= id.serial) {
        ++at;
    }
    if (at >= SERVO_IDS_RECENT) {
        return;
    }
    const size_t keep = (s->recent_n < SERVO_IDS_RECENT) ? s->recent_n
                                                         : SERVO_IDS_RECENT - 1u;
    memmove(s->recent[at + 1u], s->recent[at],
            (keep - at) * sizeof(s->recent[0]));
    memcpy(s->recent[at], id.text, sizeof(id.text));
    if (s->recent_n < SERVO_IDS_RECENT) {
        ++s->recent_n;
    }
}
