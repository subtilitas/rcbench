/**
 * @file
 * @brief The Wi-Fi page's text. See wa_text.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_text.h"

#include <string.h>

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

long wa_utf8_units(const char *s, size_t n)
{
    long units = 0;
    size_t i = 0;
    while (i < n) {
        const uint8_t b = (uint8_t)s[i];
        uint32_t cp;
        size_t len;
        if (b < 0x80u) {
            cp = b;
            len = 1u;
        } else if (b >= 0xC2u && b <= 0xDFu) {
            cp = b & 0x1Fu;
            len = 2u;
        } else if (b >= 0xE0u && b <= 0xEFu) {
            cp = b & 0x0Fu;
            len = 3u;
        } else if (b >= 0xF0u && b <= 0xF4u) {
            cp = b & 0x07u;
            len = 4u;
        } else {
            return -1;   /* a continuation byte first, or C0, C1, F5..FF */
        }
        if (i + len > n) {
            return -1;
        }
        for (size_t k = 1; k < len; ++k) {
            const uint8_t c = (uint8_t)s[i + k];
            if ((c & 0xC0u) != 0x80u) {
                return -1;
            }
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if ((len == 3u && cp < 0x800u) || (len == 4u && cp < 0x10000u)
            || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
            return -1;   /* overlong, a surrogate, or past Unicode */
        }
        units += (cp >= 0x10000u) ? 2 : 1;
        i += len;
    }
    return units;
}

/* One name: not empty, not "." or "..", UTF-8, at most WA_NAME_UNITS. */
static wa_path_err_t check_name(const char *s, size_t n)
{
    if (n == 0u) {
        return WA_PATH_EMPTY_NAME;
    }
    if ((n == 1u && s[0] == '.') || (n == 2u && s[0] == '.' && s[1] == '.')) {
        return WA_PATH_DOTS;
    }
    const long units = wa_utf8_units(s, n);
    if (units < 0) {
        return WA_PATH_UTF8;
    }
    if ((unsigned long)units > WA_NAME_UNITS) {
        return WA_PATH_NAME_LONG;
    }
    return WA_PATH_OK;
}

wa_path_err_t wa_path_decode(const char *in, size_t n, char *out, size_t cap)
{
    if (out == NULL || cap == 0u) {
        return WA_PATH_LONG;
    }
    out[0] = '\0';
    if (in == NULL || n == 0u) {
        return WA_PATH_EMPTY;
    }
    if (in[0] == '/') {
        return WA_PATH_ABSOLUTE;
    }
    size_t len = 0u;
    size_t name_at = 0u;
    for (size_t i = 0; i < n; ++i) {
        char ch = in[i];
        bool sep = false;
        if (ch == '%') {
            if (i + 2u >= n + 0u || hexval(in[i + 1u]) < 0
                || hexval(in[i + 2u]) < 0) {
                out[0] = '\0';
                return WA_PATH_ESCAPE;
            }
            ch = (char)((hexval(in[i + 1u]) << 4) | hexval(in[i + 2u]));
            i += 2u;
            if (ch == '/') {
                out[0] = '\0';
                return WA_PATH_ENCODED_SLASH;
            }
        } else if (ch == '/') {
            sep = true;
        }
        if (ch == '\\') {
            out[0] = '\0';
            return WA_PATH_BACKSLASH;
        }
        if (ch == '\0') {
            out[0] = '\0';
            return WA_PATH_NUL;
        }
        if ((uint8_t)ch < 0x20u || ch == 0x7F) {
            out[0] = '\0';
            return WA_PATH_CONTROL;
        }
        if (len + 1u >= cap || len >= WA_PATH_MAX) {
            out[0] = '\0';
            return WA_PATH_LONG;
        }
        if (sep) {
            const wa_path_err_t e = check_name(out + name_at, len - name_at);
            if (e != WA_PATH_OK) {
                out[0] = '\0';
                return e;
            }
            out[len++] = '/';
            name_at = len;
        } else {
            out[len++] = ch;
        }
    }
    const wa_path_err_t e = check_name(out + name_at, len - name_at);
    if (e != WA_PATH_OK) {
        out[0] = '\0';
        return e;
    }
    out[len] = '\0';
    return WA_PATH_OK;
}

/* ----------------------------------------------------------- output */

void wa_out_init(wa_out_t *o, wa_sink_fn sink, void *ctx)
{
    memset(o, 0, sizeof(*o));
    o->sink = sink;
    o->ctx  = ctx;
}

bool wa_out_flush(wa_out_t *o)
{
    if (o->failed) {
        return false;
    }
    if (o->used > 0u) {
        if (o->sink == NULL || !o->sink(o->ctx, o->buf, o->used)) {
            o->failed = true;
            o->used = 0u;
            return false;
        }
        o->total += o->used;
        o->used = 0u;
    }
    return true;
}

void wa_out_raw(wa_out_t *o, const void *data, size_t n)
{
    const char *p = data;
    while (n > 0u && !o->failed) {
        size_t room = WA_OUT_BUF - o->used;
        if (room == 0u) {
            (void)wa_out_flush(o);
            continue;
        }
        const size_t k = (n < room) ? n : room;
        memcpy(o->buf + o->used, p, k);
        o->used += k;
        p += k;
        n -= k;
    }
}

void wa_out_str(wa_out_t *o, const char *s)
{
    if (s != NULL) {
        wa_out_raw(o, s, strlen(s));
    }
}

void wa_out_html(wa_out_t *o, const char *s)
{
    for (; s != NULL && *s != '\0'; ++s) {
        switch (*s) {
        case '&':  wa_out_str(o, "&amp;");  break;
        case '<':  wa_out_str(o, "&lt;");   break;
        case '>':  wa_out_str(o, "&gt;");   break;
        case '"':  wa_out_str(o, "&quot;"); break;
        case '\'': wa_out_str(o, "&#39;");  break;
        default:   wa_out_raw(o, s, 1u);    break;
        }
    }
}

void wa_out_js(wa_out_t *o, const char *s)
{
    static const char k_hex[] = "0123456789abcdef";
    for (; s != NULL && *s != '\0'; ++s) {
        const uint8_t b = (uint8_t)*s;
        if (b == '\\' || b == '\'' || b == '"') {
            const char esc[2] = { '\\', *s };
            wa_out_raw(o, esc, 2u);
        } else if (b == '<' || b < 0x20u || b == 0x7Fu) {
            const char esc[4] = { '\\', 'x', k_hex[b >> 4], k_hex[b & 0x0Fu] };
            wa_out_raw(o, esc, 4u);
        } else if (b == 0xE2u && (uint8_t)s[1] == 0x80u
                   && ((uint8_t)s[2] == 0xA8u || (uint8_t)s[2] == 0xA9u)) {
            /* U+2028 and U+2029 end a line in a script of older engines. */
            wa_out_str(o, (uint8_t)s[2] == 0xA8u ? "\\u2028" : "\\u2029");
            s += 2;
        } else {
            wa_out_raw(o, s, 1u);
        }
    }
}

void wa_out_json(wa_out_t *o, const char *s)
{
    static const char k_hex[] = "0123456789abcdef";
    if (s == NULL) {
        return;
    }
    const bool utf8 = wa_utf8_units(s, strlen(s)) >= 0;
    for (; *s != '\0'; ++s) {
        const uint8_t b = (uint8_t)*s;
        if (b == '\\' || b == '"') {
            const char esc[2] = { '\\', *s };
            wa_out_raw(o, esc, 2u);
        } else if (b < 0x20u || b == 0x7Fu) {
            const char esc[6] = { '\\', 'u', '0', '0', k_hex[b >> 4],
                                  k_hex[b & 0x0Fu] };
            wa_out_raw(o, esc, 6u);
        } else if (b >= 0x80u && !utf8) {
            wa_out_str(o, "\\ufffd");
        } else {
            wa_out_raw(o, s, 1u);
        }
    }
}

static bool unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
           || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'
           || c == '~';
}

void wa_out_url(wa_out_t *o, const char *path)
{
    static const char k_hex[] = "0123456789ABCDEF";
    for (; path != NULL && *path != '\0'; ++path) {
        if (unreserved(*path) || *path == '/') {
            wa_out_raw(o, path, 1u);
        } else {
            const uint8_t b = (uint8_t)*path;
            const char esc[3] = { '%', k_hex[b >> 4], k_hex[b & 0x0Fu] };
            wa_out_raw(o, esc, 3u);
        }
    }
}

void wa_out_u64(wa_out_t *o, uint64_t v)
{
    char d[21];
    int i = 20;
    d[i] = '\0';
    do {
        d[--i] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    wa_out_str(o, d + i);
}

/* ------------------------------------------------------ what comes in */

bool wa_form_get(const char *body, size_t n, const char *key, char *out,
                 size_t cap)
{
    if (body == NULL || key == NULL || out == NULL || cap == 0u) {
        return false;
    }
    const size_t klen = strlen(key);
    size_t i = 0u;
    while (i < n) {
        size_t end = i;
        while (end < n && body[end] != '&') {
            ++end;
        }
        if (end - i > klen && memcmp(body + i, key, klen) == 0
            && body[i + klen] == '=') {
            size_t len = 0u;
            for (size_t k = i + klen + 1u; k < end; ++k) {
                char ch = body[k];
                if (ch == '+') {
                    ch = ' ';
                } else if (ch == '%') {
                    if (k + 2u >= end + 0u || hexval(body[k + 1u]) < 0
                        || hexval(body[k + 2u]) < 0) {
                        return false;
                    }
                    ch = (char)((hexval(body[k + 1u]) << 4)
                                | hexval(body[k + 2u]));
                    k += 2u;
                }
                if (len + 1u >= cap) {
                    return false;
                }
                out[len++] = ch;
            }
            out[len] = '\0';
            return true;
        }
        i = end + 1u;
    }
    return false;
}

bool wa_cookie_get(const char *header, const char *name, char *out,
                   size_t cap)
{
    if (header == NULL || name == NULL || out == NULL || cap == 0u) {
        return false;
    }
    const size_t nlen = strlen(name);
    const char *p = header;
    while (*p != '\0') {
        while (*p == ' ' || *p == ';') {
            ++p;
        }
        const char *end = strchr(p, ';');
        if (end == NULL) {
            end = p + strlen(p);
        }
        if ((size_t)(end - p) > nlen && memcmp(p, name, nlen) == 0
            && p[nlen] == '=') {
            const char *v = p + nlen + 1u;
            const size_t len = (size_t)(end - v);
            if (len + 1u > cap) {
                return false;
            }
            memcpy(out, v, len);
            out[len] = '\0';
            return true;
        }
        p = end;
    }
    return false;
}

bool wa_code_cookie(const char *code, char *out, size_t cap)
{
    static const char k_hex[] = "0123456789ABCDEF";
    if (code == NULL || out == NULL || cap == 0u) {
        return false;
    }
    size_t n = 0u;
    for (const char *p = code; *p != '\0'; ++p) {
        const unsigned char ch = (unsigned char)*p;
        const bool plain = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z')
                           || (ch >= 'A' && ch <= 'Z') || ch == '-';
        const size_t need = plain ? 1u : 3u;
        if (n + need + 1u > cap) {
            out[0] = '\0';
            return false;
        }
        if (plain) {
            out[n++] = (char)ch;
        } else {
            out[n++] = '%';
            out[n++] = k_hex[ch >> 4];
            out[n++] = k_hex[ch & 0x0Fu];
        }
    }
    out[n] = '\0';
    return true;
}

bool wa_code_of_cookie(const char *header, const char *name, char *out,
                       size_t cap)
{
    char raw[WA_CODE_COOKIE_MAX + 1u];
    if (out == NULL || cap == 0u) {
        return false;
    }
    out[0] = '\0';
    if (!wa_cookie_get(header, name, raw, sizeof(raw))) {
        return false;
    }
    size_t n = 0u;
    for (size_t i = 0u; raw[i] != '\0'; ++i) {
        char ch = raw[i];
        if (ch == '%') {
            /* The first test stops at a terminator: the second byte is read
             * only behind a first that is a digit. */
            if (hexval(raw[i + 1u]) < 0 || hexval(raw[i + 2u]) < 0) {
                out[0] = '\0';
                return false;
            }
            ch = (char)((hexval(raw[i + 1u]) << 4) | hexval(raw[i + 2u]));
            i += 2u;
        }
        if (n + 2u > cap) {
            out[0] = '\0';
            return false;
        }
        out[n++] = ch;
    }
    out[n] = '\0';
    return true;
}

const char *wa_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

size_t wa_disposition(char *out, size_t cap, const char *name)
{
    static const char k_hex[] = "0123456789ABCDEF";
    size_t len = 0u;
#define PUT(ch)                                                               \
    do {                                                                      \
        if (len + 1u >= cap) {                                                \
            if (cap > 0u) {                                                   \
                out[0] = '\0';                                                \
            }                                                                 \
            return 0u;                                                        \
        }                                                                     \
        out[len++] = (ch);                                                    \
    } while (0)
    static const char k_head[] = "attachment; filename=\"";
    for (const char *p = k_head; *p != '\0'; ++p) {
        PUT(*p);
    }
    for (const char *p = name; *p != '\0'; ++p) {
        const uint8_t b = (uint8_t)*p;
        PUT((b < 0x20u || b > 0x7Eu || b == '"' || b == '\\') ? '_' : *p);
    }
    static const char k_mid[] = "\"; filename*=UTF-8''";
    for (const char *p = k_mid; *p != '\0'; ++p) {
        PUT(*p);
    }
    for (const char *p = name; *p != '\0'; ++p) {
        if (unreserved(*p)) {
            PUT(*p);
        } else {
            const uint8_t b = (uint8_t)*p;
            PUT('%');
            PUT(k_hex[b >> 4]);
            PUT(k_hex[b & 0x0Fu]);
        }
    }
#undef PUT
    out[len] = '\0';
    return len;
}

wa_civil_t wa_civil(int64_t t)
{
    /* Days and the seconds into the day, floored for times before 1970. */
    int64_t days = t / 86400;
    int64_t secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        --days;
    }
    /* Civil date from days since 1970-01-01 (Howard Hinnant's method). */
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    wa_civil_t c;
    c.day   = (int)(doy - (153 * mp + 2) / 5 + 1);
    c.month = (int)((mp < 10) ? mp + 3 : mp - 9);
    c.year  = (int)(yoe + era * 400 + ((c.month <= 2) ? 1 : 0));
    c.hour  = (int)(secs / 3600);
    c.min   = (int)((secs / 60) % 60);
    c.sec   = (int)(secs % 60);
    return c;
}

static void out_2(wa_out_t *o, int v)
{
    const char d[2] = { (char)('0' + (v / 10) % 10), (char)('0' + v % 10) };
    wa_out_raw(o, d, 2u);
}

void wa_out_time(wa_out_t *o, int64_t t)
{
    const wa_civil_t c = wa_civil(t);
    wa_out_u64(o, (uint64_t)((c.year < 0) ? 0 : c.year));
    wa_out_str(o, "-");
    out_2(o, c.month);
    wa_out_str(o, "-");
    out_2(o, c.day);
    wa_out_str(o, " ");
    out_2(o, c.hour);
    wa_out_str(o, ":");
    out_2(o, c.min);
}
