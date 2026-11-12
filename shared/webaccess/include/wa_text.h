/**
 * @file
 * @brief The text the Wi-Fi page reads and writes: a card path out of a URL
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

/** @brief Why wa_path_decode() refused a path. */
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
 * @brief Decode @p n bytes of a URL path, the part after the download prefix
 *        and before any "?", into @p out of @p cap bytes: a path relative to
 *        the card, names separated by "/".
 *
 * The bytes are percent-decoded once.  @p out holds "" unless WA_PATH_OK.
 *
 * @param in The path as sent.
 * @param n The path's length, in bytes.
 * @param out Receives the decoded path.
 * @param cap The size of @p out, in bytes.
 * @return WA_PATH_OK, or why the path is refused.
 */
wa_path_err_t wa_path_decode(const char *in, size_t n, char *out, size_t cap);

/**
 * @brief UTF-16 units of @p n bytes of UTF-8.
 *
 * @param s The text.
 * @param n The text's length, in bytes.
 * @return The units, or -1 when the bytes are not UTF-8 (overlong forms,
 *         surrogates and values past U+10FFFF included).
 */
long wa_utf8_units(const char *s, size_t n);

/* ----------------------------------------------------------- output */

/** Where output goes: false when it could not be sent. */
typedef bool (*wa_sink_fn)(void *ctx, const void *data, size_t n);

/** @brief Bytes gathered before they go to the sink. */
#define WA_OUT_BUF 1024u

/** Output gathered into pieces of WA_OUT_BUF before it goes to the sink. */
typedef struct {
    wa_sink_fn sink;     /**< where a gathered piece goes */
    void      *ctx;      /**< passed to the sink */
    char       buf[WA_OUT_BUF];  /**< the gathered output */
    size_t     used;     /**< bytes in buf */
    bool       failed;   /**< the sink refused a piece; the rest is dropped */
    uint64_t   total;    /**< bytes handed to the sink                     */
} wa_out_t;

/**
 * @brief An output with no data, sending to @p sink.
 *
 * @param o The output.
 * @param sink Where a gathered piece goes.
 * @param ctx Passed to @p sink.
 */
void wa_out_init(wa_out_t *o, wa_sink_fn sink, void *ctx);
/**
 * @brief Append @p n bytes as they are.
 *
 * @param o The output.
 * @param data The bytes.
 * @param n The count.
 */
void wa_out_raw(wa_out_t *o, const void *data, size_t n);
/**
 * @brief Append the text @p s as it is.
 *
 * @param o The output.
 * @param s The text; NULL writes nothing.
 */
void wa_out_str(wa_out_t *o, const char *s);
/**
 * @brief @p s with & < > " and ' written as entities.
 *
 * @param o The output.
 * @param s The text.
 */
void wa_out_html(wa_out_t *o, const char *s);
/**
 * @brief @p s as the inside of a JavaScript string literal in single or
 *        double quotes.
 *
 * Backslash, ' and " get a backslash before them.  < is written as \\x3c,
 * so "\</script>" cannot end the script, and each control character
 * 0x00-0x1F and 0x7F as \\xHH.  U+2028 and U+2029 (UTF-8 E2 80 A8 and
 * E2 80 A9) are written as \\u2028 and \\u2029.  Other bytes from 0x80 up
 * pass unchanged.
 *
 * @param o The output.
 * @param s The text.
 */
void wa_out_js(wa_out_t *o, const char *s);
/**
 * @brief @p s as the inside of a JSON (JavaScript Object Notation) string,
 *        the quotes not written.
 *
 * Backslash and " get a backslash before them; each control character
 * 0x00-0x1F and 0x7F is written as \\u00HH.  A string that is UTF-8 passes
 * its other bytes unchanged.  In a string that is not, every byte from 0x80
 * up is written as \\ufffd, so the answer stays JSON a browser parses.
 *
 * @param o  the output
 * @param s  the text; NULL writes nothing
 */
void wa_out_json(wa_out_t *o, const char *s);
/**
 * @brief @p path with every byte but A-Z a-z 0-9 - . _ ~ and / percent-encoded.
 *
 * @param o The output.
 * @param path The path.
 */
void wa_out_url(wa_out_t *o, const char *path);
/**
 * @brief @p v in decimal.
 *
 * @param o The output.
 * @param v The number.
 */
void wa_out_u64(wa_out_t *o, uint64_t v);
/**
 * @brief Hand over what is gathered.
 *
 * @param o The output.
 * @return false once the sink has refused.
 */
bool wa_out_flush(wa_out_t *o);

/* ------------------------------------------------------ what comes in */

/**
 * @brief Field @p key of a form body (application/x-www-form-urlencoded) of
 *        @p n bytes, decoded ("+" a space, "%xx" a byte) into @p out of
 *        @p cap.
 *
 * @param body The body.
 * @param n The body's length, in bytes.
 * @param key The field's name.
 * @param out Receives the value.
 * @param cap The size of @p out, in bytes.
 * @return false when the field is missing, badly encoded or longer than
 *         @p cap - 1.
 */
bool wa_form_get(const char *body, size_t n, const char *key, char *out,
                 size_t cap);

/**
 * @brief Cookie @p name of a Cookie header, as sent, into @p out of @p cap.
 *
 * @param header The header's value.
 * @param name The cookie's name.
 * @param out Receives the value.
 * @param cap The size of @p out, in bytes.
 * @return false when missing or longer than @p cap - 1.
 */
bool wa_cookie_get(const char *header, const char *name, char *out,
                   size_t cap);

/** @brief Bytes of the longest code as a cookie's value, terminator apart:
 *         63 characters of 3 bytes each. */
#define WA_CODE_COOKIE_MAX 189u

/**
 * @brief An access code as a cookie's value: letters, digits and "-" as they
 *        are, every other byte as %XX, since a kept code may hold a space, a
 *        ";" or a quote.  A RANDOM code is its own cookie value.
 * @param code The code.
 * @param out  Takes the value.
 * @param cap  Bytes of @p out.
 * @return false, @p out empty, when it does not fit.
 */
bool wa_code_cookie(const char *code, char *out, size_t cap);

/**
 * @brief The access code a Cookie header carries, wa_code_cookie() undone.
 * @param header The header's value.
 * @param name   The cookie's name.
 * @param out    Takes the code; empty when false.
 * @param cap    Bytes of @p out.
 * @return false for no such cookie, one too long for @p out, and a broken %XX.
 */
bool wa_code_of_cookie(const char *header, const char *name, char *out,
                       size_t cap);

/**
 * @brief The Content-Disposition value for a download of @p name, the last
 *        part of a path: attachment, an ASCII (American Standard Code for
 *        Information Interchange) filename with every other byte, '"' and
 *        a backslash as '_', and a filename* in UTF-8 percent-encoded
 *        (RFC 6266).
 *
 * @param out Receives the value.
 * @param cap The size of @p out, in bytes.
 * @param name The file's name.
 * @return The length, or 0 when it does not fit @p cap.
 */
size_t wa_disposition(char *out, size_t cap, const char *name);

/** @brief A calendar time, from seconds since 1970. */
typedef struct {
    int year,    /**< the year, 1970 for the epoch */
        month,   /**< 1 to 12 */
        day,     /**< 1 to 31 */
        hour,    /**< 0 to 23 */
        min,     /**< 0 to 59 */
        sec;     /**< 0 to 59 */
} wa_civil_t;

/**
 * @brief @p t seconds since 1970 as a calendar date and time.
 *
 * @param t Seconds since 1970-01-01 00:00:00.
 * @return The date and time.
 */
wa_civil_t wa_civil(int64_t t);

/**
 * @brief @p t as "2026-10-14 09:05".
 *
 * @param o The output.
 * @param t Seconds since 1970-01-01 00:00:00.
 */
void wa_out_time(wa_out_t *o, int64_t t);

/**
 * @brief The last name of @p path.
 *
 * @param path The path.
 * @return The part after the last "/", or @p path when it has none.
 */
const char *wa_basename(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_TEXT_H */
