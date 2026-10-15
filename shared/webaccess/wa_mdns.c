/*
 * The panel's mDNS answer. See wa_mdns.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_mdns.h"

#include <stdbool.h>
#include <string.h>

#define TYPE_A     1u
#define TYPE_ANY   255u
#define CLASS_IN   1u
#define CLASS_QU   0x8000u   /* in a question: answer by unicast       */
#define CLASS_FLUSH 0x8000u  /* in an answer: this replaces the cache  */
#define LEGACY_TTL 10u

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint8_t *put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return p + 2;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/*
 * Compare the name at @p at in the message with host.local, following
 * compression pointers; *next is set to the byte after the name where it
 * started.  -1 malformed, 0 another name, 1 this one.
 */
static int name_is(const uint8_t *m, size_t n, size_t at, const char *host,
                   size_t *next)
{
    const char *want[2] = { host, "local" };
    unsigned label = 0u;
    bool jumped = false;
    unsigned hops = 0u;
    int same = 1;
    for (;;) {
        if (at >= n) {
            return -1;
        }
        const uint8_t len = m[at];
        if ((len & 0xC0u) == 0xC0u) {
            if (at + 1u >= n || ++hops > 16u) {
                return -1;
            }
            if (!jumped) {
                *next = at + 2u;
                jumped = true;
            }
            at = ((size_t)(len & 0x3Fu) << 8) | m[at + 1u];
            continue;
        }
        if (len & 0xC0u) {
            return -1;
        }
        if (len == 0u) {
            if (!jumped) {
                *next = at + 1u;
            }
            return (same && label == 2u) ? 1 : 0;
        }
        if (at + 1u + len > n) {
            return -1;
        }
        if (label < 2u) {
            const char *w = want[label];
            if (strlen(w) != len) {
                same = 0;
            } else {
                for (unsigned i = 0; i < len; ++i) {
                    if (lower((char)m[at + 1u + i]) != lower(w[i])) {
                        same = 0;
                    }
                }
            }
        } else {
            same = 0;
        }
        ++label;
        at += 1u + len;
    }
}

static uint8_t *put_name(uint8_t *p, const char *host)
{
    const size_t h = strlen(host);
    *p++ = (uint8_t)h;
    memcpy(p, host, h);
    p += h;
    *p++ = 5u;
    memcpy(p, "local", 5u);
    p += 5u;
    *p++ = 0u;
    return p;
}

static size_t answer(uint16_t id, bool question, uint32_t ttl,
                     const char *host, const uint8_t ip[4], uint8_t *out,
                     size_t cap)
{
    const size_t h = strlen(host);
    const size_t name = h + 1u + 6u + 1u;
    const size_t need = 12u + (question ? name + 4u : 0u) + name + 10u + 4u;
    if (h == 0u || h > 63u || need > cap) {
        return 0u;
    }
    uint8_t *p = out;
    p = put16(p, id);
    p = put16(p, 0x8400u);            /* a response, authoritative */
    p = put16(p, question ? 1u : 0u);
    p = put16(p, 1u);
    p = put16(p, 0u);
    p = put16(p, 0u);
    if (question) {
        p = put_name(p, host);
        p = put16(p, TYPE_A);
        p = put16(p, CLASS_IN);
    }
    p = put_name(p, host);
    p = put16(p, TYPE_A);
    p = put16(p, question ? CLASS_IN : (CLASS_FLUSH | CLASS_IN));
    p = put16(p, ttl >> 16);
    p = put16(p, ttl & 0xFFFFu);
    p = put16(p, 4u);
    memcpy(p, ip, 4u);
    p += 4;
    return (size_t)(p - out);
}

size_t wa_mdns_reply(const uint8_t *q, size_t n, uint16_t src_port,
                     const char *host, const uint8_t ip[4], uint8_t *out,
                     size_t cap)
{
    if (q == NULL || n < 12u || host == NULL) {
        return 0u;
    }
    const uint16_t flags = get16(q + 2);
    if ((flags & 0x8000u) != 0u || (flags & 0x7800u) != 0u) {
        return 0u;                    /* a response, or not a QUERY */
    }
    const unsigned qd = get16(q + 4);
    size_t at = 12u;
    for (unsigned i = 0; i < qd; ++i) {
        size_t next = 0u;
        const int is = name_is(q, n, at, host, &next);
        if (is < 0 || next + 4u > n) {
            return 0u;
        }
        const uint16_t type = get16(q + next);
        const uint16_t cls  = get16(q + next + 2u) & (uint16_t)~CLASS_QU;
        at = next + 4u;
        if (is == 1 && (type == TYPE_A || type == TYPE_ANY)
            && cls == CLASS_IN) {
            const bool legacy = src_port != WA_MDNS_PORT;
            return answer(legacy ? get16(q) : 0u, legacy,
                          legacy ? LEGACY_TTL : WA_MDNS_TTL, host, ip, out,
                          cap);
        }
    }
    return 0u;
}

size_t wa_mdns_announce(const char *host, const uint8_t ip[4], uint8_t *out,
                        size_t cap)
{
    if (host == NULL) {
        return 0u;
    }
    return answer(0u, false, WA_MDNS_TTL, host, ip, out, cap);
}

/* ------------------------------------------------------- the starts */

void wa_mdns_tries_reset(wa_mdns_tries_t *t)
{
    t->starts = 0u;
    t->next_ms = 0u;
}

bool wa_mdns_tries_due(const wa_mdns_tries_t *t, uint32_t now_ms)
{
    if (t->starts == 0u) {
        return true;
    }
    return t->starts <= WA_MDNS_RETRIES
           && (int32_t)(now_ms - t->next_ms) >= 0;
}

void wa_mdns_tries_started(wa_mdns_tries_t *t, uint32_t now_ms)
{
    if (t->starts < 0xFFu) {
        ++t->starts;
    }
    t->next_ms = now_ms + WA_MDNS_RETRY_MS;
}
