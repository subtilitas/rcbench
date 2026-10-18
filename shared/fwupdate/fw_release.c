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
        [FW_REL_TAG_NONE]   = "no tag_name",
        [FW_REL_TAG_LONG]   = "tag_name too long",
        [FW_REL_TAG_FORM]   = "JSON out of form",
        [FW_REL_SCAN_LONG]  = "no tag_name within the scan",
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

/* ------------------------------------------------- the API's answer */

/*
 * Where the next byte falls.  Level 1 is the array, level 2 the first
 * release's object, whose keys are read; anything deeper is passed over
 * with its strings and brackets followed.
 */
enum {
    SC_TOP = 0,     /* before the array                                 */
    SC_FIRST,       /* after '[': the first release or ']'              */
    SC_KEY0,        /* after '{': a key or '}'                          */
    SC_KEY,         /* after ',': a key                                 */
    SC_KSTR,        /* in a key                                         */
    SC_KESC,        /* after '\' in a key                               */
    SC_COLON,       /* after a key                                      */
    SC_VALUE,       /* after ':'                                        */
    SC_TSTR,        /* in tag_name's value                              */
    SC_VSTR,        /* in another key's string value                    */
    SC_VESC,        /* after '\' in it                                  */
    SC_SCALAR,      /* in a number, true, false or null                 */
    SC_AFTER,       /* after a value: ',' or '}'                        */
    SC_DEEP,        /* inside a value nested deeper                     */
    SC_DSTR,        /* in a string there                                */
    SC_DESC,        /* after '\' in it                                  */
};

static const char k_tag_key[] = "tag_name";

static bool json_space(uint8_t ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static bool json_scalar(uint8_t ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z')
           || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '+'
           || ch == '.';
}

void fw_release_scan_init(fw_rel_scan_t *s)
{
    memset(s, 0, sizeof(*s));
    s->st = SC_TOP;
}

static void scan_end(fw_rel_scan_t *s, fw_rel_err_t e)
{
    s->ended = true;
    s->err = e;
}

/* A level opens: false when it would pass FW_REL_SCAN_DEPTH. */
static bool scan_open(fw_rel_scan_t *s, bool array)
{
    if (s->depth >= FW_REL_SCAN_DEPTH) {
        return false;
    }
    if (array) {
        s->arrays |= (uint32_t)1u << s->depth;
    } else {
        s->arrays &= ~((uint32_t)1u << s->depth);
    }
    ++s->depth;
    return true;
}

/* A level below the object closes with @p array's bracket: false when it
 * is the other's.  Only SC_DEEP calls it, at level 3 or deeper. */
static bool scan_close(fw_rel_scan_t *s, bool array)
{
    const bool was = (s->arrays >> (s->depth - 1u)) & 1u;
    if (was != array) {
        return false;
    }
    --s->depth;
    return true;
}

/* tag_name's value whole: "v" and a version. */
static void scan_tag(fw_rel_scan_t *s)
{
    fw_version_t v;
    if (s->tn < 2u || s->tag[0] != 'v'
        || !fw_release_parse_version(s->tag + 1, s->tn - 1u, &v)) {
        scan_end(s, FW_REL_VERSION);
        return;
    }
    s->found = v;
    scan_end(s, FW_REL_OK);
}

/* One byte at level 2, the first release's object. */
static void scan_object(fw_rel_scan_t *s, uint8_t ch)
{
    switch (s->st) {
    case SC_KEY0:
    case SC_KEY:
        if (json_space(ch)) {
            return;
        }
        if (ch == '"') {
            s->st = SC_KSTR;
            s->kn = 0u;
            s->kmatch = true;
        } else if (ch == '}' && s->st == SC_KEY0) {
            scan_end(s, FW_REL_TAG_NONE);
        } else {
            scan_end(s, FW_REL_TAG_FORM);
        }
        return;
    case SC_KSTR:
        if (ch == '"') {
            s->want = s->kmatch && s->kn == sizeof(k_tag_key) - 1u;
            s->st = SC_COLON;
        } else if (ch < 0x20u) {
            scan_end(s, FW_REL_TAG_FORM);
        } else if (ch == '\\') {
            s->kmatch = false;      /* an escaped key is not tag_name */
            s->st = SC_KESC;
        } else if (s->kmatch && s->kn < sizeof(k_tag_key) - 1u
                   && ch == (uint8_t)k_tag_key[s->kn]) {
            ++s->kn;
        } else {
            s->kmatch = false;
        }
        return;
    case SC_KESC:
        if (ch < 0x20u) {
            scan_end(s, FW_REL_TAG_FORM);
        } else {
            s->st = SC_KSTR;
        }
        return;
    case SC_COLON:
        if (json_space(ch)) {
            return;
        }
        if (ch == ':') {
            s->st = SC_VALUE;
        } else {
            scan_end(s, FW_REL_TAG_FORM);
        }
        return;
    case SC_VALUE:
        if (json_space(ch)) {
            return;
        }
        if (s->want) {
            if (ch == '"') {
                s->st = SC_TSTR;
                s->tn = 0u;
            } else {
                scan_end(s, FW_REL_TAG_FORM);    /* not a string */
            }
        } else if (ch == '"') {
            s->st = SC_VSTR;
        } else if (ch == '{' || ch == '[') {
            (void)scan_open(s, ch == '[');   /* level 3: always room */
            s->st = SC_DEEP;
        } else if (json_scalar(ch)) {
            s->st = SC_SCALAR;
        } else {
            scan_end(s, FW_REL_TAG_FORM);
        }
        return;
    case SC_TSTR:
        if (ch == '"') {
            scan_tag(s);
        } else if (ch < 0x20u || ch == '\\') {
            scan_end(s, FW_REL_TAG_FORM);
        } else if (s->tn >= FW_REL_TAG_MAX) {
            scan_end(s, FW_REL_TAG_LONG);
        } else {
            s->tag[s->tn++] = (char)ch;
        }
        return;
    case SC_VSTR:
        if (ch == '"') {
            s->st = SC_AFTER;
        } else if (ch < 0x20u) {
            scan_end(s, FW_REL_TAG_FORM);
        } else if (ch == '\\') {
            s->st = SC_VESC;
        }
        return;
    case SC_VESC:
        if (ch < 0x20u) {
            scan_end(s, FW_REL_TAG_FORM);
        } else {
            s->st = SC_VSTR;
        }
        return;
    case SC_SCALAR:
        if (json_scalar(ch)) {
            return;
        }
        s->st = SC_AFTER;
        scan_object(s, ch);          /* the byte that ended it */
        return;
    case SC_AFTER:
    default:
        if (json_space(ch)) {
            return;
        }
        if (ch == ',') {
            s->st = SC_KEY;
        } else if (ch == '}') {
            scan_end(s, FW_REL_TAG_NONE);    /* the object closed */
        } else {
            scan_end(s, FW_REL_TAG_FORM);
        }
        return;
    }
}

/* One byte inside a value below level 2. */
static void scan_deep(fw_rel_scan_t *s, uint8_t ch)
{
    switch (s->st) {
    case SC_DSTR:
        if (ch == '"') {
            s->st = SC_DEEP;
        } else if (ch < 0x20u) {
            scan_end(s, FW_REL_TAG_FORM);
        } else if (ch == '\\') {
            s->st = SC_DESC;
        }
        return;
    case SC_DESC:
        if (ch < 0x20u) {
            scan_end(s, FW_REL_TAG_FORM);
        } else {
            s->st = SC_DSTR;
        }
        return;
    case SC_DEEP:
    default:
        if (ch == '"') {
            s->st = SC_DSTR;
        } else if (ch == '{' || ch == '[') {
            if (!scan_open(s, ch == '[')) {
                scan_end(s, FW_REL_TAG_FORM);
            }
        } else if (ch == '}' || ch == ']') {
            if (!scan_close(s, ch == ']')) {
                scan_end(s, FW_REL_TAG_FORM);
            } else if (s->depth == 2u) {
                s->st = SC_AFTER;
            }
        }
        return;
    }
}

static void scan_byte(fw_rel_scan_t *s, uint8_t ch)
{
    switch (s->st) {
    case SC_TOP:
        if (json_space(ch)) {
            return;
        }
        if (ch == '[') {
            (void)scan_open(s, true);
            s->st = SC_FIRST;
        } else {
            scan_end(s, FW_REL_TAG_FORM);
        }
        return;
    case SC_FIRST:
        if (json_space(ch)) {
            return;
        }
        if (ch == '{') {
            (void)scan_open(s, false);
            s->st = SC_KEY0;
        } else if (ch == ']') {
            scan_end(s, FW_REL_TAG_NONE);     /* no release at all */
        } else {
            scan_end(s, FW_REL_TAG_FORM);
        }
        return;
    case SC_DEEP:
    case SC_DSTR:
    case SC_DESC:
        scan_deep(s, ch);
        return;
    default:
        scan_object(s, ch);
        return;
    }
}

bool fw_release_scan_feed(fw_rel_scan_t *s, const uint8_t *d, size_t n)
{
    if (s == NULL) {
        return true;
    }
    if (d == NULL) {
        n = 0u;
    }
    for (size_t i = 0; i < n && !s->ended; ++i) {
        if (s->seen >= FW_REL_SCAN_MAX) {
            scan_end(s, FW_REL_SCAN_LONG);
            break;
        }
        ++s->seen;
        scan_byte(s, d[i]);
    }
    return s->ended;
}

fw_rel_err_t fw_release_scan_end(const fw_rel_scan_t *s, fw_version_t *out)
{
    if (s == NULL) {
        return FW_REL_TAG_NONE;
    }
    if (!s->ended) {
        return FW_REL_TAG_NONE;     /* the answer ended before the tag */
    }
    if (s->err == FW_REL_OK && out != NULL) {
        *out = s->found;
    }
    return s->err;
}

bool fw_release_offered(fw_version_t found, fw_version_t running,
                        bool running_ok)
{
    return running_ok && fw_version_cmp(found, running) > 0;
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
