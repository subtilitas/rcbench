/**
 * @file
 * @brief What wa_api.c lends wa_api_write.c: the query's parameters, the
 *        JSON (JavaScript Object Notation) pieces, a fresh request and the
 *        loaded text's CRC-32 (cyclic redundancy check).  Private to
 *        shared/webaccess.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_API_INT_H
#define RCBENCH_WA_API_INT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_req.h"
#include "text_buf.h"
#include "wa_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A JSON string: @p s between quotes.
 * @param o  the output
 * @param s  the text
 */
void wa_api_json_str(wa_out_t *o, const char *s);

/**
 * @brief A JSON member: ",\"key\":" with no comma before the first.
 * @param o      the output
 * @param key    the member's name, plain ASCII
 * @param first  the object's first member
 */
void wa_api_json_key(wa_out_t *o, const char *key, bool first);

/**
 * @brief A JSON boolean.
 * @param o  the output
 * @param b  the value
 */
void wa_api_json_bool(wa_out_t *o, bool b);

/**
 * @brief The raw value of parameter @p key: the bytes after "key=" up to
 *        the next "&", at the query's start or after an "&".
 * @param query  the query string
 * @param key    the parameter's name
 * @param n      takes the value's bytes
 * @return the value, inside @p query; NULL when not given
 */
const char *wa_api_q_raw(const char *query, const char *key, size_t *n);

/**
 * @brief Parameter @p key as a path from the card's root.
 * @param query  the query string
 * @param key    the parameter's name
 * @param out    takes the path; FM_PATH_SIZE bytes
 * @return WA_API_OK, the root for a value not given or empty; WA_API_NAME
 *         for one wa_path_decode() or fm_path_check() refuses
 */
wa_api_err_t wa_api_q_path(const char *query, const char *key, char *out);

/**
 * @brief Parameter @p key as 0 or 1.
 * @param query  the query string
 * @param key    the parameter's name
 * @param out    takes the value; false when not given
 * @return false for a value other than "0" and "1"
 */
bool wa_api_q_flag(const char *query, const char *key, bool *out);

/**
 * @brief Parameter "mount", a mount number of 1 to 10 digits.
 * @param query  the query string
 * @param io     the card, for the mount now when not given
 * @param out    takes the number
 * @return false for a value that is not a number up to UINT32_MAX
 */
bool wa_api_q_mount(const char *query, const fm_io_t *io, unsigned *out);

/**
 * @brief The folder and the name of @p path, which is not the root.
 * @param path  a path fm_path_check() takes
 * @param dir   takes the folder; FM_PATH_SIZE bytes
 * @param name  takes the name; FM_NAME_BUF bytes
 * @return WA_API_REQUEST for the root, WA_API_NAME for a name the walk
 *         does not list
 */
wa_api_err_t wa_api_split(const char *path, char *dir, char *name);

/**
 * @brief An empty request with the API's log folders.
 * @param api   the API
 * @param kind  what it asks
 * @return the request
 */
fm_req_t *wa_api_req(wa_api_t *api, fm_req_kind_t kind);

/**
 * @brief The API's reason for a refused text.
 * @param e  text_buf_load_done()'s answer
 * @return the reason
 */
wa_api_err_t wa_api_text_why(text_buf_err_t e);

/**
 * @brief The CRC-32 of the loaded file's bytes, read a chunk at a time.
 * @param api  the API, a file loaded
 * @return the CRC
 */
uint32_t wa_api_crc(const wa_api_t *api);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_API_INT_H */
