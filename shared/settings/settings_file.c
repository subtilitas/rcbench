/**
 * @file
 * @brief The settings as a text file a person reads and edits.  See
 *        settings_file.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_file.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief SF_HEADER without its last space. */
#define SF_HEADER_WORDS "rcbench settings, format"

/** @brief The column the comment of a setting's line starts at, when the
 *         key and the value leave room. */
#define COMMENT_COL 32

/** @brief The strings' comments, in English, by id. */
static const char *const k_text_notes[SETTING_TEXT_COUNT] = {
    [SET_TEXT_DUT_NAME]     = "Device under test, for the report",
    [SET_TEXT_LOG_AUTO_DIR] = "Auto log folder",
    [SET_TEXT_LOG_USER_DIR] = "User log folder",
    [SET_TEXT_IMU_ADDR]     = "Bound IMU: BLE address and its type",
    [SET_TEXT_IMU_NAME]     = "Bound IMU: name",
    [SET_TEXT_DIAL_ADDR]    = "Bound Dial: BLE address and its type",
    [SET_TEXT_DIAL_NAME]    = "Bound Dial: name",
    [SET_TEXT_PNODE_ADDR]   = "Bound power node: BLE address and its type",
    [SET_TEXT_PNODE_NAME]   = "Bound power node: name",
    [SET_TEXT_BNODE_ADDR]   = "Bound balance node: BLE address and its type",
    [SET_TEXT_BNODE_NAME]   = "Bound balance node: name",
};

/* ================================================================ writing */

/** @brief The text being written: where, its room, its length, and whether
 *         it ran out of room. */
typedef struct {
    char  *out;   /**< the buffer         */
    size_t cap;   /**< its bytes          */
    size_t len;   /**< bytes written      */
    bool   full;  /**< a line did not fit */
} writer_t;

/** @brief Append @p s and a LF; a line that does not fit marks the text
 *         full. */
static void put_line(writer_t *w, const char *s)
{
    const size_t n = strlen(s);
    if (w->full || w->len + n + 2u > w->cap) {
        w->full = true;
        return;
    }
    memcpy(w->out + w->len, s, n);
    w->len += n;
    w->out[w->len++] = '\n';
    w->out[w->len] = '\0';
}

/** @brief Append to @p line, at most to SF_LINE_MAX characters. */
static void cat(char *line, const char *fmt, ...)
{
    const size_t used = strlen(line);
    if (used >= SF_LINE_MAX) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(line + used, SF_LINE_MAX + 1u - used, fmt, ap);
    va_end(ap);
}

/** @brief Pad @p line with spaces to COMMENT_COL, or one space past it. */
static void pad(char *line)
{
    size_t n = strlen(line);
    while (n < SF_LINE_MAX) {
        line[n++] = ' ';
        if (n >= COMMENT_COL) {
            break;
        }
    }
    line[n] = '\0';
}

/** @brief A number of @p id as the file writes it. */
static void number_text(setting_id_t id, float v, char *out, size_t n)
{
    (void)snprintf(out, n, "%.*f", settings_decimals(id), (double)v);
}

/**
 * @brief The comment of a setting's line: its label, and the range and unit
 *        of a number or the options of a choice, as many as the line holds.
 */
static void comment_of(setting_id_t id, char *line)
{
    const setting_def_t *d = settings_def(id);
    cat(line, "# %s", settings_label(id));
    if (d->type == SET_TYPE_BOOL) {
        cat(line, ": ON, OFF");
    } else if (d->type == SET_TYPE_ENUM && d->options != NULL) {
        for (int i = 0; i < d->option_count; ++i) {
            const size_t room = SF_LINE_MAX - strlen(line);
            if (strlen(d->options[i]) + 6u > room) {
                cat(line, ", ...");
                break;
            }
            cat(line, "%s%s", (i == 0) ? ": " : ", ", d->options[i]);
        }
    } else if (d->type != SET_TYPE_ENUM) {
        char lo[16];
        char hi[16];
        number_text(id, d->min, lo, sizeof(lo));
        number_text(id, d->max, hi, sizeof(hi));
        cat(line, ", %s to %s%s", lo, hi, d->unit);
    }
}

/** @brief One setting's line, from the stored copy. */
static void setting_line(writer_t *w, setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    const float v = settings_stored(id);
    char line[SF_LINE_MAX + 1u] = "";
    cat(line, "%s = ", d->key);
    if (d->type == SET_TYPE_BOOL) {
        cat(line, "%s", (v != 0.0f) ? "ON" : "OFF");
    } else if (d->type == SET_TYPE_ENUM) {
        const long i = lrintf(v);
        if (d->options != NULL && i >= 0 && i < (long)d->option_count) {
            cat(line, "%s", d->options[i]);
        } else {
            cat(line, "%ld", i);
        }
    } else {
        char num[24];
        number_text(id, v, num, sizeof(num));
        cat(line, "%s", num);
    }
    pad(line);
    comment_of(id, line);
    put_line(w, line);
}

/** @brief One string's line, from the stored copy, quoted. */
static void text_line(writer_t *w, setting_text_id_t id)
{
    char line[SF_LINE_MAX + 1u] = "";
    cat(line, "%s = \"", settings_text_key(id));
    for (const char *p = settings_stored_text(id); *p != '\0'; ++p) {
        cat(line, (*p == '"' || *p == '\\') ? "\\%c" : "%c", *p);
    }
    cat(line, "\"");
    pad(line);
    cat(line, "# %s", k_text_notes[id]);
    put_line(w, line);
}

size_t sf_format(char *out, size_t cap, const char *version,
                 const char *written)
{
    if (out == NULL || cap == 0u) {
        return 0u;
    }
    writer_t w = { out, cap, 0u, false };
    out[0] = '\0';
    char line[SF_LINE_MAX + 1u];
    (void)snprintf(line, sizeof(line), "%s%u", SF_HEADER, SF_FORMAT);
    put_line(&w, line);
    (void)snprintf(line, sizeof(line), "Panel: %s",
                   (version != NULL && version[0] != '\0') ? version
                                                           : "not known");
    put_line(&w, line);
    (void)snprintf(line, sizeof(line), "Written: %s",
                   (written != NULL && written[0] != '\0') ? written
                                                           : "not known");
    put_line(&w, line);
    put_line(&w, "# Not in this file: the Wi-Fi home network and its "
                 "password,");
    put_line(&w, "# HOTSPOT PASSWORD and ACCESS CODE.");
    put_line(&w, "# One line a setting: <key> = <value>.  A setting without "
                 "a line loads");
    put_line(&w, "# its default.  Keep the last line, end.");
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (!settings_derived((setting_id_t)i)) {
            setting_line(&w, (setting_id_t)i);
        }
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        text_line(&w, (setting_text_id_t)i);
    }
    put_line(&w, "end");
    if (w.full) {
        out[0] = '\0';
        return 0u;
    }
    return w.len;
}

/* ================================================================ reading */

/** @brief One line of the text, without its line end. */
typedef struct {
    const char *p;    /**< its first byte              */
    size_t      n;    /**< its bytes                   */
} span_t;

/** @brief Whether @p c is a space or a tab. */
static bool blank(char c)
{
    return c == ' ' || c == '\t';
}

/** @brief @p s without the spaces and tabs at either end. */
static span_t trim(span_t s)
{
    while (s.n > 0u && blank(s.p[0])) {
        ++s.p;
        --s.n;
    }
    while (s.n > 0u && blank(s.p[s.n - 1u])) {
        --s.n;
    }
    return s;
}

/** @brief @p s up to a `#` outside double quotes, trimmed. */
static span_t uncomment(span_t s)
{
    bool quoted = false;
    for (size_t i = 0u; i < s.n; ++i) {
        const char c = s.p[i];
        if (quoted && c == '\\' && i + 1u < s.n) {
            ++i;
        } else if (c == '"') {
            quoted = !quoted;
        } else if (c == '#' && !quoted) {
            s.n = i;
            break;
        }
    }
    return trim(s);
}

/** @brief Whether @p s is @p word, without case. */
static bool is_word(span_t s, const char *word)
{
    const size_t n = strlen(word);
    if (s.n != n) {
        return false;
    }
    for (size_t i = 0u; i < n; ++i) {
        char a = s.p[i];
        char b = word[i];
        a = (a >= 'a' && a <= 'z') ? (char)(a - 'a' + 'A') : a;
        b = (b >= 'a' && b <= 'z') ? (char)(b - 'a' + 'A') : b;
        if (a != b) {
            return false;
        }
    }
    return true;
}

/** @brief Whether @p s starts with @p word, without case; @p rest takes
 *         what follows, trimmed. */
static bool starts(span_t s, const char *word, span_t *rest)
{
    const size_t n = strlen(word);
    if (s.n < n || !is_word((span_t){ s.p, n }, word)) {
        return false;
    }
    *rest = trim((span_t){ s.p + n, s.n - n });
    return true;
}

/** @brief Copy @p s into @p out, cut to its room. */
static void copy_span(span_t s, char *out, size_t cap)
{
    const size_t n = (s.n < cap - 1u) ? s.n : cap - 1u;
    memcpy(out, s.p, n);
    out[n] = '\0';
}

/** @brief The text from @p pos to the next LF, its CR dropped; the position
 *         after the LF into @p next. */
static span_t line_at(const char *text, size_t len, size_t pos, size_t *next)
{
    size_t e = pos;
    while (e < len && text[e] != '\n') {
        ++e;
    }
    *next = (e < len) ? e + 1u : e;
    span_t s = { text + pos, e - pos };
    if (s.n > 0u && s.p[s.n - 1u] == '\r') {
        --s.n;
    }
    return s;
}

/** @brief Where the text starts: past a UTF-8 byte order mark. */
static size_t text_start(const char *text, size_t len)
{
    return (len >= 3u && (unsigned char)text[0] == 0xEFu
            && (unsigned char)text[1] == 0xBBu
            && (unsigned char)text[2] == 0xBFu) ? 3u : 0u;
}

/** @brief Read the header line into @p head. */
static sf_err_t header(span_t s, sf_head_t *head)
{
    /* The header without its last space: "format" and no number is a
     * version that is missing, not another file. */
    span_t rest;
    if (!starts(trim(s), SF_HEADER_WORDS, &rest)) {
        return SF_ERR_NOT_SETTINGS;
    }
    unsigned v = 0u;
    if (rest.n == 0u || rest.n > 3u) {
        return SF_ERR_VERSION;
    }
    for (size_t i = 0u; i < rest.n; ++i) {
        if (rest.p[i] < '0' || rest.p[i] > '9') {
            return SF_ERR_VERSION;
        }
        v = v * 10u + (unsigned)(rest.p[i] - '0');
    }
    if (v == 0u) {
        return SF_ERR_VERSION;
    }
    head->format = v;
    return (v > SF_FORMAT) ? SF_ERR_NEWER : SF_OK;
}

/** @brief A `Panel:` or `Written:` line into @p head; false for another. */
static bool head_line(span_t c, sf_head_t *head)
{
    span_t rest;
    if (starts(c, "Panel:", &rest)) {
        copy_span(is_word(rest, "not known") ? (span_t){ "", 0u } : rest,
                  head->version, sizeof(head->version));
        return true;
    }
    if (starts(c, "Written:", &rest)) {
        copy_span(is_word(rest, "not known") ? (span_t){ "", 0u } : rest,
                  head->written, sizeof(head->written));
        return true;
    }
    return false;
}

sf_err_t sf_head(const char *text, size_t len, sf_head_t *out)
{
    if (text == NULL || out == NULL) {
        return SF_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    size_t pos = text_start(text, len);
    size_t next;
    const sf_err_t e = header(line_at(text, len, pos, &next), out);
    if (e != SF_OK) {
        return e;
    }
    bool panel = false;
    bool written = false;
    for (pos = next; pos < len && !(panel && written); pos = next) {
        const span_t c = uncomment(line_at(text, len, pos, &next));
        span_t rest;
        if (c.n == 0u) {
            continue;
        }
        panel = panel || starts(c, "Panel:", &rest);
        written = written || starts(c, "Written:", &rest);
        if (!head_line(c, out)) {
            break;                      /* the settings begin */
        }
    }
    return SF_OK;
}

/** @brief Record a refused line in @p l. */
static void refuse(sf_load_t *l, unsigned line, sf_line_err_t code, span_t key)
{
    if (l->refused_n < SF_REFUSED_MAX) {
        sf_refusal_t *r = &l->refused[l->refused_n];
        r->line = (uint16_t)((line > 0xFFFFu) ? 0xFFFFu : line);
        r->code = (uint8_t)code;
        copy_span(key, r->key, sizeof(r->key));
    }
    if (l->refused_n < 0xFFFFu) {
        ++l->refused_n;
    }
}

/** @brief A decimal number: a sign, digits, a point and digits; nothing
 *         else.  False for anything else. */
static bool number(span_t s, float *out)
{
    char buf[24];
    if (s.n == 0u || s.n >= sizeof(buf)) {
        return false;
    }
    bool digit = false;
    bool point = false;
    for (size_t i = 0u; i < s.n; ++i) {
        const char c = s.p[i];
        if (c >= '0' && c <= '9') {
            digit = true;
        } else if (c == '.' && !point) {
            point = true;
        } else if (!((c == '-' || c == '+') && i == 0u)) {
            return false;
        }
    }
    if (!digit) {
        return false;
    }
    memcpy(buf, s.p, s.n);
    buf[s.n] = '\0';
    *out = strtof(buf, NULL);
    return true;
}

/** @brief A setting's value as the line spells it; false for one that reads
 *         as no value of its type. */
static bool value_of(setting_id_t id, span_t v, float *out)
{
    const setting_def_t *d = settings_def(id);
    if (d->type == SET_TYPE_BOOL) {
        if (is_word(v, "ON") || is_word(v, "OFF")) {
            *out = is_word(v, "ON") ? 1.0f : 0.0f;
            return true;
        }
        return false;
    }
    if (d->type == SET_TYPE_ENUM) {
        for (int i = 0; d->options != NULL && i < d->option_count; ++i) {
            if (is_word(v, d->options[i])) {
                *out = (float)i;
                return true;
            }
        }
        return false;
    }
    return number(v, out);
}

/** @brief A quoted string into @p out; SF_LINE_OK, SF_LINE_VALUE for one not
 *         in quotes or with a control byte, SF_LINE_LONG for one too long. */
static sf_line_err_t quoted(span_t v, char out[SETTINGS_TEXT_MAX])
{
    if (v.n < 2u || v.p[0] != '"' || v.p[v.n - 1u] != '"') {
        return SF_LINE_VALUE;
    }
    size_t n = 0u;
    for (size_t i = 1u; i + 1u < v.n; ++i) {
        char c = v.p[i];
        if (c == '\\' && i + 2u < v.n) {
            c = v.p[++i];
        } else if (c == '"' || c == '\\') {
            return SF_LINE_VALUE;
        }
        if ((unsigned char)c < 0x20u || c == 0x7F) {
            return SF_LINE_VALUE;
        }
        if (n >= SETTINGS_TEXT_MAX - 1u) {
            return SF_LINE_LONG;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return SF_LINE_OK;
}

/** @brief One `<key> = <value>` line into @p l. */
static void setting_in(sf_load_t *l, unsigned ln, span_t c)
{
    size_t eq = 0u;
    while (eq < c.n && c.p[eq] != '=') {
        ++eq;
    }
    const span_t key = trim((span_t){ c.p, eq });
    if (eq == c.n || key.n == 0u) {
        refuse(l, ln, SF_LINE_MALFORMED, key);
        return;
    }
    const span_t v = trim((span_t){ c.p + eq + 1u, c.n - eq - 1u });
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (!is_word(key, settings_def(id)->key)) {
            continue;
        }
        float f = 0.0f;
        if (settings_derived(id)) {
            refuse(l, ln, SF_LINE_DERIVED, key);
        } else if (l->value_line[i] != 0u) {
            refuse(l, ln, SF_LINE_TWICE, key);
        } else if (!value_of(id, v, &f)) {
            refuse(l, ln, SF_LINE_VALUE, key);
        } else if (!settings_takes(id, f)) {
            refuse(l, ln, SF_LINE_RANGE, key);
        } else {
            l->values[i] = f;
            l->value_line[i] = (uint16_t)((ln > 0xFFFFu) ? 0xFFFFu : ln);
            ++l->taken;
        }
        return;
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (!is_word(key, settings_text_key((setting_text_id_t)i))) {
            continue;
        }
        char t[SETTINGS_TEXT_MAX];
        const sf_line_err_t e = (l->text_line[i] != 0u) ? SF_LINE_TWICE
                                                        : quoted(v, t);
        if (e != SF_LINE_OK) {
            refuse(l, ln, e, key);
        } else {
            memcpy(l->texts[i], t, sizeof(t));
            l->text_line[i] = (uint16_t)((ln > 0xFFFFu) ? 0xFFFFu : ln);
            ++l->taken;
        }
        return;
    }
    refuse(l, ln, SF_LINE_KEY, key);
}

sf_err_t sf_parse(const char *text, size_t len, sf_load_t *out,
                  unsigned *line)
{
    if (line != NULL) {
        *line = 0u;
    }
    if (text == NULL || out == NULL) {
        return SF_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < SETTING_COUNT; ++i) {
        out->values[i] = settings_def((setting_id_t)i)->def;
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        (void)snprintf(out->texts[i], SETTINGS_TEXT_MAX, "%s",
                       settings_text_default((setting_text_id_t)i));
    }
    if (len > SF_FILE_MAX) {
        return SF_ERR_OVERSIZED;
    }
    size_t pos = text_start(text, len);
    size_t next;
    const sf_err_t e = header(line_at(text, len, pos, &next), &out->head);
    if (e != SF_OK) {
        if (line != NULL) {
            *line = 1u;
        }
        return e;
    }
    unsigned ln = 1u;
    unsigned last = 1u;
    bool ended = false;
    for (pos = next; pos < len; pos = next) {
        ++ln;
        const span_t c = uncomment(line_at(text, len, pos, &next));
        if (c.n == 0u) {
            continue;
        }
        last = ln;
        ended = is_word(c, "end");
        if (ended || head_line(c, &out->head)) {
            continue;
        }
        setting_in(out, ln, c);
    }
    if (!ended) {
        if (line != NULL) {
            *line = last;
        }
        return SF_ERR_TRUNCATED;
    }
    return SF_OK;
}

/** @brief Whether value @p v of @p id is off @p ref by half a step or more. */
static bool apart(setting_id_t id, float v, float ref)
{
    const float step = settings_def(id)->step;
    return (step > 0.0f) ? fabsf(v - ref) >= 0.5f * step : v != ref;
}

bool sf_differs(const sf_load_t *l)
{
    if (l == NULL) {
        return false;
    }
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (!settings_derived(id)
            && apart(id, l->values[i], settings_stored(id))) {
            return true;
        }
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (strcmp(l->texts[i],
                   settings_stored_text((setting_text_id_t)i)) != 0) {
            return true;
        }
    }
    return false;
}

int sf_apply(sf_load_t *l)
{
    if (l == NULL) {
        return 0;
    }
    int changed = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (settings_derived(id)) {
            continue;
        }
        const float before = settings_get(id);
        if (settings_held(id)) {
            if (apart(id, l->values[i], before)) {
                const char *key = settings_def(id)->key;
                refuse(l, l->value_line[i], SF_LINE_HELD,
                       (span_t){ key, strlen(key) });
            }
            continue;
        }
        settings_set(id, l->values[i]);
        changed += (settings_get(id) != before) ? 1 : 0;
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        const setting_text_id_t id = (setting_text_id_t)i;
        if (strcmp(settings_text(id), l->texts[i]) != 0) {
            settings_set_text(id, l->texts[i]);
            ++changed;
        }
    }
    return changed;
}
