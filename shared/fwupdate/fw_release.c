/*
 * fw_release.h says what is read and how strictly.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_release.h"

#include <stdio.h>
#include <string.h>

const char *const FW_REL_HOSTS[] = {
    "github.com",
    "release-assets.githubusercontent.com",
    "objects.githubusercontent.com",
    NULL,
};

const char *fw_rel_err_str(fw_rel_err_t e)
{
    static const char *const k[FW_REL_COUNT] = {
        [FW_REL_OK]         = "ok",
        [FW_REL_MISSING]    = "no Location",
        [FW_REL_LONG]       = "too long",
        [FW_REL_CHAR]       = "a byte outside printable ASCII",
        [FW_REL_PLACE]      = "not the expected place",
        [FW_REL_VERSION]    = "not a version tag",
        [FW_REL_STATUS]     = "unexpected HTTP status",
        [FW_REL_HOPS]       = "too many redirects",
        [FW_REL_HOST]       = "redirect to another host",
        [FW_REL_NO_LENGTH]  = "no Content-Length",
        [FW_REL_EMPTY]      = "empty body",
        [FW_REL_TOO_BIG]    = "larger than the cap",
        [FW_REL_SHORT]      = "body shorter than announced",
        [FW_REL_OVERRUN]    = "more bytes than asked for",
        [FW_REL_SUMS_BAD]   = "SHA256SUMS out of form",
        [FW_REL_SUMS_NONE]  = "no SHA256SUMS line for the file",
        [FW_REL_SUMS_TWICE] = "two SHA256SUMS lines for the file",
    };
    return ((unsigned)e < FW_REL_COUNT) ? k[e] : "?";
}

/* Printable ASCII, space excluded: what a URL in a header may hold. */
static bool url_bytes(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ch = (unsigned char)s[i];
        if (ch < 0x21u || ch > 0x7Eu) {
            return false;
        }
    }
    return true;
}

bool fw_release_parse_version(const char *s, size_t n, fw_version_t *out)
{
    if (s == NULL) {
        return false;
    }
    uint32_t part[3] = { 0u, 0u, 0u };
    unsigned which = 0u, digits = 0u;
    bool lead_zero = false;
    for (size_t i = 0; i < n; ++i) {
        const char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            if (lead_zero || digits == 5u) {
                return false;            /* "01", or a sixth digit */
            }
            lead_zero = (digits == 0u && ch == '0');
            part[which] = part[which] * 10u + (uint32_t)(ch - '0');
            ++digits;
        } else if (ch == '.' && digits > 0u && which < 2u) {
            ++which;
            digits = 0u;
            lead_zero = false;
        } else {
            return false;
        }
        if (part[which] > 0xFFFFu) {
            return false;
        }
    }
    if (which != 2u || digits == 0u) {
        return false;
    }
    if (out != NULL) {
        out->major = (uint16_t)part[0];
        out->minor = (uint16_t)part[1];
        out->patch = (uint16_t)part[2];
    }
    return true;
}

fw_rel_err_t fw_release_parse_tag(const char *loc, size_t n, bool cut,
                                  fw_version_t *out)
{
    static const char prefix[] = FW_REL_TAG_PREFIX;
    const size_t pn = sizeof(prefix) - 1u;
    if (loc == NULL || n == 0u) {
        return cut ? FW_REL_LONG : FW_REL_MISSING;
    }
    if (cut || n > pn + FW_REL_TAG_MAX) {
        return FW_REL_LONG;
    }
    if (!url_bytes(loc, n)) {
        return FW_REL_CHAR;
    }
    if (n <= pn + 1u || memcmp(loc, prefix, pn) != 0 || loc[pn] != 'v') {
        return FW_REL_PLACE;
    }
    fw_version_t v;
    if (!fw_release_parse_version(loc + pn + 1u, n - pn - 1u, &v)) {
        return FW_REL_VERSION;
    }
    if (out != NULL) {
        *out = v;
    }
    return FW_REL_OK;
}

fw_rel_status_t fw_release_status(int status)
{
    switch (status) {
    case 200:
        return FW_REL_BODY;
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
        return FW_REL_REDIRECT;
    default:
        return FW_REL_REFUSED;
    }
}

fw_rel_err_t fw_release_redirect(const char *loc, size_t n, bool cut,
                                 unsigned hops)
{
    static const char scheme[] = "https://";
    const size_t sn = sizeof(scheme) - 1u;
    if (hops >= FW_REL_HOPS_MAX) {
        return FW_REL_HOPS;
    }
    if (loc == NULL || n == 0u) {
        return cut ? FW_REL_LONG : FW_REL_MISSING;
    }
    if (cut || n > FW_REL_URL_MAX) {
        return FW_REL_LONG;
    }
    if (!url_bytes(loc, n)) {
        return FW_REL_CHAR;
    }
    if (n <= sn || memcmp(loc, scheme, sn) != 0) {
        return FW_REL_PLACE;
    }
    const char *host = loc + sn;
    const char *slash = memchr(host, '/', n - sn);
    if (slash == NULL) {
        return FW_REL_PLACE;
    }
    /* The host exactly: no user, no port, no other case.  "github.com.x"
     * and "x.github.com" are other hosts. */
    const size_t hn = (size_t)(slash - host);
    for (const char *const *h = FW_REL_HOSTS; *h != NULL; ++h) {
        if (strlen(*h) == hn && memcmp(*h, host, hn) == 0) {
            return FW_REL_OK;
        }
    }
    return FW_REL_HOST;
}

fw_rel_err_t fw_release_length(int64_t length, bool chunked, uint32_t cap)
{
    if (chunked || length < 0) {
        return FW_REL_NO_LENGTH;
    }
    if (length == 0) {
        return FW_REL_EMPTY;
    }
    return (length > (int64_t)cap) ? FW_REL_TOO_BIG : FW_REL_OK;
}

static int hex(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

static bool name_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
           || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
}

fw_rel_err_t fw_release_parse_sums(const char *text, size_t n,
                                   const char *name,
                                   uint8_t out[FW_SHA256_BYTES])
{
    if (text == NULL || name == NULL) {
        return FW_REL_SUMS_BAD;
    }
    if (n > FW_REL_SUMS_MAX) {
        return FW_REL_LONG;
    }
    if (n == 0u || text[n - 1u] != '\n') {
        return FW_REL_SUMS_BAD;
    }
    const size_t want = strlen(name);
    unsigned found = 0u;
    size_t at = 0u;
    while (at < n) {
        const char *line = text + at;
        const char *nl = memchr(line, '\n', n - at);   /* never NULL */
        const size_t len = (size_t)(nl - line);
        at += len + 1u;
        /* 64 hex digits, a space, a space or '*', a name of 1 or more. */
        if (len < 2u * FW_SHA256_BYTES + 3u || line[64] != ' '
            || (line[65] != ' ' && line[65] != '*')) {
            return FW_REL_SUMS_BAD;
        }
        uint8_t h[FW_SHA256_BYTES];
        for (unsigned i = 0; i < FW_SHA256_BYTES; ++i) {
            const int hi = hex(line[(size_t)2u * i]);
            const int lo = hex(line[(size_t)2u * i + 1u]);
            if (hi < 0 || lo < 0) {
                return FW_REL_SUMS_BAD;
            }
            h[i] = (uint8_t)((hi << 4) | lo);
        }
        const char *file = line + 66;
        size_t fl = len - 66u;
        if (fl >= 2u && file[0] == '.' && file[1] == '/') {
            file += 2;
            fl -= 2u;
        }
        if (fl == 0u) {
            return FW_REL_SUMS_BAD;
        }
        for (size_t i = 0; i < fl; ++i) {
            if (!name_char(file[i])) {
                return FW_REL_SUMS_BAD;
            }
        }
        if (fl == want && memcmp(file, name, want) == 0) {
            if (++found > 1u) {
                return FW_REL_SUMS_TWICE;
            }
            if (out != NULL) {
                memcpy(out, h, sizeof(h));
            }
        }
    }
    return (found == 1u) ? FW_REL_OK : FW_REL_SUMS_NONE;
}

void fw_release_version_text(fw_version_t v, char out[FW_REL_VER_TEXT])
{
    snprintf(out, FW_REL_VER_TEXT, "%u.%u.%u", (unsigned)v.major,
             (unsigned)v.minor, (unsigned)v.patch);
}

void fw_release_image_name(fw_version_t v, char out[FW_REL_NAME_MAX])
{
    snprintf(out, FW_REL_NAME_MAX, "rcbench-panel-v%u.%u.%u.bin",
             (unsigned)v.major, (unsigned)v.minor, (unsigned)v.patch);
}

void fw_release_sig_name(fw_version_t v, char out[FW_REL_SIG_NAME_MAX])
{
    snprintf(out, FW_REL_SIG_NAME_MAX, "rcbench-panel-v%u.%u.%u.bin.sig",
             (unsigned)v.major, (unsigned)v.minor, (unsigned)v.patch);
}

bool fw_release_url(fw_version_t v, const char *file, char *out, size_t cap)
{
    if (file == NULL || out == NULL || cap == 0u) {
        return false;
    }
    const int k = snprintf(out, cap, FW_REL_DL_PREFIX "v%u.%u.%u/%s",
                           (unsigned)v.major, (unsigned)v.minor,
                           (unsigned)v.patch, file);
    return k > 0 && (size_t)k < cap;
}
