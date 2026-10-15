/*
 * The text the Wi-Fi page reads and writes: a card path out of a URL
 * (uniform resource locator), names into HTML (hypertext markup language)
 * and back into URLs, a form field, a cookie, and a download's file name.
 *
 * The path is the part that decides what leaves the card, so it is read by
 * one rule and everything else is refused (wa_path_decode()).  The page only
 * ever links paths that rule takes.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_TEXT_H
#define RCBENCH_WA_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes of a path relative to the card, terminator not counted. */
#define WA_PATH_MAX       384u
/** UTF-16 units of one name: the FAT (file allocation table) long name. */
#define WA_NAME_UNITS     255u
/** Bytes of one name in UTF-8: 255 units of 3 bytes, and a terminator. */
#define WA_NAME_BYTES     (WA_NAME_UNITS * 3u + 1u)

typedef enum {
    WA_PATH_OK = 0,
    WA_PATH_EMPTY,          /**< nothing after the prefix                  */
    WA_PATH_ABSOLUTE,       /**< starts with "/"                           */
    WA_PATH_DOTS,           /**< a "." or ".." name, written or encoded    */
    WA_PATH_BACKSLASH,      /**< "\" written or encoded                    */
    WA_PATH_ENCODED_SLASH,  /**< "%2F": a separator must be written as one */
    WA_PATH_NUL,            /**< "%00"                                     */
    WA_PATH_CONTROL,        /**< a byte below 0x20, or 0x7F                */
    WA_PATH_ESCAPE,         /**< "%" not followed by two hex digits        */
    WA_PATH_UTF8,           /**< not UTF-8                                  */
    WA_PATH_EMPTY_NAME,     /**< "//", or a "/" at the end                 */
    WA_PATH_NAME_LONG,      /**< a name over WA_NAME_UNITS                 */
    WA_PATH_LONG,           /**< over WA_PATH_MAX bytes, or over @p cap    */
} wa_path_err_t;

/**
 * Decode @p n bytes of a URL path, the part after the download prefix and
 * before any "?", into @p out of @p cap bytes: a path relative to the
 * card, names separated by "/".  The bytes are percent-decoded once.
 * Refused: see wa_path_err_t.  @p out holds "" unless WA_PATH_OK.
 */
wa_path_err_t wa_path_decode(const char *in, size_t n, char *out, size_t cap);

/**
 * UTF-16 units of @p n bytes of UTF-8, or -1 when they are not UTF-8
 * (overlong forms, surrogates and values past U+10FFFF included).
 */
long wa_utf8_units(const char *s, size_t n);

/* ----------------------------------------------------------- output */

/** Where output goes: false when it could not be sent. */
typedef bool (*wa_sink_fn)(void *ctx, const void *data, size_t n);

#define WA_OUT_BUF 1024u

/** Output gathered into pieces of WA_OUT_BUF before it goes to the sink. */
typedef struct {
    wa_sink_fn sink;
    void      *ctx;
    char       buf[WA_OUT_BUF];
    size_t     used;
    bool       failed;   /**< the sink refused a piece; the rest is dropped */
    uint64_t   total;    /**< bytes handed to the sink                     */
} wa_out_t;

void wa_out_init(wa_out_t *o, wa_sink_fn sink, void *ctx);
void wa_out_raw(wa_out_t *o, const void *data, size_t n);
void wa_out_str(wa_out_t *o, const char *s);
/** @p s with & < > " and ' written as entities. */
void wa_out_html(wa_out_t *o, const char *s);
/**
 * @p s as the inside of a JavaScript string literal in single or double
 * quotes.  Backslash, ' and " get a backslash before them.  < is written
 * as \x3c, so "</script>" cannot end the script, and each control
 * character 0x00-0x1F and 0x7F as \xHH.  U+2028 and U+2029 (UTF-8
 * E2 80 A8 and E2 80 A9) are written as \u2028 and \u2029.  Other bytes
 * from 0x80 up pass unchanged.
 */
void wa_out_js(wa_out_t *o, const char *s);
/** @p path with every byte but A-Z a-z 0-9 - . _ ~ and / percent-encoded. */
void wa_out_url(wa_out_t *o, const char *path);
void wa_out_u64(wa_out_t *o, uint64_t v);
/** Hand over what is gathered.  False once the sink has refused. */
bool wa_out_flush(wa_out_t *o);

/* ------------------------------------------------------ what comes in */

/**
 * Field @p key of a form body (application/x-www-form-urlencoded) of @p n
 * bytes, decoded ("+" a space, "%xx" a byte) into @p out of @p cap.  False
 * when the field is missing, badly encoded or longer than @p cap - 1.
 */
bool wa_form_get(const char *body, size_t n, const char *key, char *out,
                 size_t cap);

/**
 * Cookie @p name of a Cookie header, as sent, into @p out of @p cap.  False
 * when missing or longer than @p cap - 1.
 */
bool wa_cookie_get(const char *header, const char *name, char *out,
                   size_t cap);

/**
 * The Content-Disposition value for a download of @p name, the last part of
 * a path: attachment, an ASCII (American Standard Code for Information
 * Interchange) filename with every other byte, '"' and '\' as '_', and a
 * filename* in UTF-8 percent-encoded (RFC 6266).  Returns the length, or 0
 * when it does not fit @p cap.
 */
size_t wa_disposition(char *out, size_t cap, const char *name);

/** A calendar time, from seconds since 1970. */
typedef struct {
    int year, month, day, hour, min, sec;
} wa_civil_t;

/** @p t seconds since 1970 as a calendar date and time. */
wa_civil_t wa_civil(int64_t t);

/** @p t as "2026-10-14 09:05". */
void wa_out_time(wa_out_t *o, int64_t t);

/** The last name of @p path. */
const char *wa_basename(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_TEXT_H */
