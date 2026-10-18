/*
 * The programmer's journal file: the text of a write to the servo that
 * survives a restart of the panel, and the parse of it.  See kst_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "kst_prog.h"
#include "kst_prog_int.h"
#include "link_crc.h"

/* The text written, and the version before it, which has no pin line. */
static const char k_head[] = "RCBENCH KST 2\n";
static const char k_head_1[] = "RCBENCH KST 1\n";

/* ----------------------------------------------------- the journal's file */

static uint16_t crc16(const char *s, size_t n)
{
    return link_crc(LINK_CRC_INIT, s, n);
}

static char *put(char *at, const char *s)
{
    while (*s != '\0') {
        *at++ = *s++;
    }
    return at;
}

static char *put_hex(char *at, unsigned v, unsigned digits)
{
    static const char k_hex[] = "0123456789ABCDEF";

    for (unsigned i = digits; i > 0u; --i) {
        *at++ = k_hex[(v >> (4u * (i - 1u))) & 0xFu];
    }
    return at;
}

static char *put_image(char *at, const char *key, const kst_image_t *img)
{
    at = put(at, key);
    for (unsigned i = 0; i < sizeof(img->r); ++i) {
        at = put_hex(at, img->r[i], 2);
    }
    *at++ = '\n';
    return at;
}

size_t kst_prog_file_text(const kst_prog_t *p, char *out, size_t n)
{
    char *at = out;

    if (n < KST_PROG_FILE_MAX) {
        return 0;
    }
    at = put(at, k_head);
    if (p->journal) {
        at = put_image(at, "journal=", &p->journal_img);
        at = put_image(at, "target=", &p->journal_target);
        at = put(at, "port=");
        at = put_hex(at, p->journal_port, 1);
        *at++ = '\n';
        at = put(at, "pin=");
        at = put_hex(at, p->journal_pin, 2);
        *at++ = '\n';
    }
    {
        const uint16_t crc = crc16(out, (size_t)(at - out));

        at = put(at, "check=");
        at = put_hex(at, crc, 4);
        *at++ = '\n';
    }
    *at = '\0';
    return (size_t)(at - out);
}

size_t kst_prog_file_take(kst_prog_t *p, char *out, size_t n)
{
    size_t len;

    if (!p->file_wanted || p->file_busy || !p->fed || !p->f.card) {
        return 0;
    }
    len = kst_prog_file_text(p, out, n);
    if (len != 0u) {
        p->file_wanted = false;
        p->file_busy = true;
        p->file_stale = false;
        p->file_for = p->file_next;
        p->file_next = FILE_NONE;
    }
    return len;
}

void kst_prog_file_done(kst_prog_t *p, bool ok)
{
    const uint8_t what = p->file_for;
    const bool stale = p->file_stale;

    if (!p->file_busy) {
        return;
    }
    p->file_busy = false;
    p->file_stale = false;
    p->file_for = FILE_NONE;
    if (stale) {
        /* The answer to a text of before the write that waits: that
         * write's text is wanted still and goes next. */
        return;
    }
    /* The card holds the text of before: a record cleared here would be
     * found again at the next start.  The text is written again. */
    p->file_retry = !ok;
    p->file_retry_ms = p->f.now_ms;
    if (p->wait == W_FILE && what == FILE_JOURNAL) {
        if (ok) {
            kst_prog_send(p, &p->held);
        } else {
            kst_prog_not_started(p, KST_PROG_MSG_STORE_FAILED, 0);
        }
    }
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* @p digits hexadecimal digits at @p s; -1 for anything else. */
static long take_hex(const char *s, unsigned digits)
{
    long v = 0;

    for (unsigned i = 0; i < digits; ++i) {
        const int d = hex(s[i]);

        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

/* "key" + 64 digits + newline at @p s, @p left bytes there. */
static bool take_image(const char *s, size_t left, const char *key,
                       kst_image_t *img)
{
    const size_t k = strlen(key);

    if (left < k + 65u || memcmp(s, key, k) != 0 || s[k + 64u] != '\n') {
        return false;
    }
    for (size_t i = 0; i < sizeof(img->r); ++i) {
        const long v = take_hex(s + k + 2u * i, 2);

        if (v < 0) {
            return false;
        }
        img->r[i] = (uint8_t)v;
    }
    return true;
}

typedef struct {
    bool journal;
    uint8_t journal_port;
    uint8_t journal_pin;
    kst_image_t journal_img;
    kst_image_t journal_target;
} loaded_t;

static bool parse(loaded_t *into, const char *text, size_t len)
{
    const size_t head = sizeof(k_head) - 1u;
    size_t at = head;
    bool v1;
    long v;

    if (len < head + 11u || len >= KST_PROG_FILE_MAX) {
        return false;
    }
    v1 = memcmp(text, k_head_1, head) == 0;
    if (!v1 && memcmp(text, k_head, head) != 0) {
        return false;
    }
    /* The last line is the check of all before it. */
    if (memcmp(text + len - 11u, "check=", 6) != 0 || text[len - 1u] != '\n') {
        return false;
    }
    v = take_hex(text + len - 5u, 4);
    if (v < 0 || (uint16_t)v != crc16(text, len - 11u)) {
        return false;
    }
    len -= 11u;

    into->journal = false;
    if (at != len) {
        if (!take_image(text + at, len - at, "journal=", &into->journal_img)) {
            return false;
        }
        at += 8u + 65u;
        if (!take_image(text + at, len - at, "target=",
                        &into->journal_target)) {
            return false;
        }
        at += 7u + 65u;
        if (len - at != (v1 ? 7u : 14u)
            || memcmp(text + at, "port=", 5) != 0
            || text[at + 6u] != '\n') {
            return false;
        }
        v = take_hex(text + at + 5u, 1);
        if (v < 0 || v >= (long)LINK_PG_PORTS_MAX) {
            /* No port of any build: the record names none. */
            return false;
        }
        into->journal_port = (uint8_t)v;
        into->journal_pin = KST_PROG_NO_PIN;
        at += 7u;
        if (!v1) {
            if (memcmp(text + at, "pin=", 4) != 0 || text[at + 6u] != '\n') {
                return false;
            }
            v = take_hex(text + at + 4u, 2);
            if (v < 0) {
                return false;
            }
            /* A pin of no IOmcu: the record stands, by its port. */
            if (v <= (long)KST_PROG_PIN_LAST) {
                into->journal_pin = (uint8_t)v;
            }
        }
        into->journal = true;
    }
    return true;
}

bool kst_prog_file_whole(const char *text, size_t len)
{
    loaded_t got;

    memset(&got, 0, sizeof(got));
    return parse(&got, text, len);
}

bool kst_prog_file_load(kst_prog_t *p, const char *text, size_t len)
{
    /* Parsed aside: a text that is not whole changes nothing. */
    loaded_t got;

    memset(&got, 0, sizeof(got));
    if (!parse(&got, text, len)) {
        (void)kst_prog_say(p, KST_PROG_MSG_FILE_BAD, 0);
        return false;
    }
    p->journal = got.journal;
    p->journal_open = got.journal;
    p->journal_port = got.journal_port;
    p->journal_pin = got.journal ? got.journal_pin : KST_PROG_NO_PIN;
    p->journal_img = got.journal_img;
    p->journal_target = got.journal_target;
    /* With a servo read, a card mounted under a session: the record is
     * held against that servo here, and not first at the next read. */
    if (kst_prog_journal_classify(p)) {
        (void)kst_prog_say(p, KST_PROG_MSG_JOURNAL_OTHER,
                           kst_prog_journal_arg(p));
    } else if (p->journal) {
        (void)kst_prog_say(p, KST_PROG_MSG_JOURNAL, kst_prog_journal_arg(p));
    }
    return true;
}
