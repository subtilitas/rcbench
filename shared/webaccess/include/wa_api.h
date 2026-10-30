/**
 * @file
 * @brief The Wi-Fi page's file API: a page of a folder, the facts of one
 *        entry, a text file whole and the panel's state, each answered as
 *        JSON (JavaScript Object Notation) or as the file's bytes.
 *
 * Every card call runs through the file manager's runner (fm_req.h): the
 * same pages, rules, refusals and load as the FILES screen.  The API reads
 * only: nothing on the card is written, renamed or deleted through it.
 *
 * Requests are GET with a query string.  A path is relative to the card's
 * root and percent-encoded as wa_path_decode() reads a URL (uniform
 * resource locator) path: "/" stays a separator, "%2F" is refused, "+" is a
 * plus.  An empty or missing path is the root.  Parameters not named here
 * are ignored; a parameter given twice is read from its first place.
 *
 *   /api/status                              the panel and its card
 *   /api/list?path=P&go=G&key=N&key_dir=D    one page of folder P
 *   /api/info?path=P&dir=D&mount=M           the size and time of P
 *   /api/file?path=P&mount=M                 text file P, whole
 *
 * A page of a folder holds up to FM_PAGE_MAX entries, folders first, then
 * names with case folded (fm_tree.h).  G is first (the default), next,
 * prev, last or same.  next, prev and same name the page by the entry at
 * its edge: N, the name, and D, 1 for a folder.  next reads the page after
 * N, prev the page before it, same the page from N on.  Each answer carries
 * the mount number it was read from; a file or an INFO asked with
 * mount=M is refused with CARD_GONE when another card is in the slot since.
 *
 * A file answer is the file's bytes as the card holds them, its BOM (byte
 * order mark) included, with an ETag of their CRC-32 (cyclic redundancy
 * check), their line ends (X-Rcbench-Eol: lf, crlf, cr or mixed), their
 * count of lines (X-Rcbench-Lines) and whether a BOM leads
 * (X-Rcbench-Bom: 1 or 0).  Its limits are the viewer's (text_buf.h):
 * 1MiB, 65,536 line ends, no NUL in the first 4kB.
 *
 * A refusal is an HTTP (hypertext transfer protocol) status and
 * {"error":"WORD","text":"..."}: the word is wa_api_word()'s, the text in
 * the panel's language.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_API_H
#define RCBENCH_WA_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "card_tree.h"
#include "fm_io.h"
#include "fm_req.h"
#include "text_buf.h"
#include "wa_guard.h"
#include "wa_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The path of the panel's state. */
#define WA_API_URL_STATUS "/api/status"
/** @brief The path of a page of a folder. */
#define WA_API_URL_LIST   "/api/list"
/** @brief The path of one entry's facts. */
#define WA_API_URL_INFO   "/api/info"
/** @brief The path of a text file. */
#define WA_API_URL_FILE   "/api/file"

/** @brief Bytes of a file answer's header at most (wa_api_file_head()). */
#define WA_API_HEAD_MAX 320u

/**
 * @brief Why a request was refused.  One entry per fm_err_t from
 *        FM_ERR_NO_CARD on, then the API's own.
 */
typedef enum {
    WA_API_OK = 0,
    WA_API_NO_CARD,        /**< 503: nothing is mounted                  */
    WA_API_CARD_GONE,      /**< 503: the card is gone or swapped         */
    WA_API_NOT_FOUND,      /**< 404: no such file or folder              */
    WA_API_EXISTS,         /**< 409: the name is taken                   */
    WA_API_FULL,           /**< 507: the card or the folder is full      */
    WA_API_DENIED,         /**< 403: the card is write-protected         */
    WA_API_BUSY,           /**< 423: the logger writes the file          */
    WA_API_FIXED,          /**< 403: a folder the bench keeps            */
    WA_API_LOCKED,         /**< 403: the servo programmer's record       */
    WA_API_NAME,           /**< 400: a name or path the card cannot take */
    WA_API_ARG,            /**< 400: not possible for this entry         */
    WA_API_IO,             /**< 500: the card failed                     */
    WA_API_NOT_EMPTY,      /**< 409: the folder holds entries            */
    WA_API_SAME,           /**< 409: the target is the folder or in it   */
    WA_API_TOO_BIG,        /**< 413: over the viewer's 1MiB              */
    WA_API_ARMED,          /**< 409: the bench is ARMED                  */
    WA_API_LOGGING,        /**< 409: a run is being logged               */
    WA_API_CANCELLED,      /**< 503: the radio goes off                  */
    WA_API_NOT_TEXT,       /**< 415: a NUL in the first 4kB              */
    WA_API_TOO_MANY_LINES, /**< 422: more than 65,536 line ends          */
    WA_API_MEMORY,         /**< 503: no PSRAM for the request            */
    WA_API_CODE,           /**< 403: no access code, or a wrong one      */
    WA_API_REQUEST,        /**< 400: a parameter the API does not take   */
    WA_API_BODY,           /**< 400: a read that carries a body (guard)  */
    WA_API_HOST,           /**< 421: a Host that is not the panel (guard) */
    WA_API_COUNT
} wa_api_err_t;

/**
 * @brief What the API answers with.  The caller places every buffer; the
 *        fields are the API's, set by wa_api_init() and wa_api_set_*().
 */
typedef struct {
    const fm_runner_t *run;           /**< the card, the bench, the chunk  */
    fm_req_t *req;                    /**< room for one request            */
    uint8_t *text;                    /**< a file's room, or NULL          */
    size_t text_cap;                  /**< bytes at @p text                */
    uint32_t *ends;                   /**< the line index, or NULL         */
    size_t ends_cap;                  /**< entries at @p ends              */
    text_buf_t tb;                    /**< the last file loaded            */
    uint32_t etag;                    /**< its bytes' CRC-32               */
    char user_logs[CARD_DIR_MAX];     /**< USER LOG FOLDER; "" default     */
    char auto_logs[CARD_DIR_MAX];     /**< AUTO LOG FOLDER; "" default     */
} wa_api_t;

/** @brief What /api/status tells: the caller fills it each time. */
typedef struct {
    const char *name;     /**< the panel's name, "rcbench-1A2B"            */
    const char *version;  /**< the panel firmware running                  */
    bool de;              /**< the panel shows German                      */
    bool mounted;         /**< a volume is mounted                         */
    unsigned mount;       /**< its mount number                            */
    const char *card;     /**< its name; may be NULL                       */
    uint64_t total;       /**< its bytes; 0 when not known                 */
    uint64_t free;        /**< its free bytes; 0 when not known            */
    fm_err_t gate;        /**< fm_gate() of a write now                    */
    const char *user_logs; /**< USER LOG FOLDER; NULL or "" the default    */
} wa_api_status_t;

/**
 * @brief Start the API on a card's runner and the room of one request.
 * @param api  the API
 * @param run  the card, the bench and a chunk; borrowed for the API's life
 * @param req  room for one request; borrowed likewise
 */
void wa_api_init(wa_api_t *api, const fm_runner_t *run, fm_req_t *req);

/**
 * @brief Give the API room for a file: up to @p cap bytes and @p ends_cap
 *        line ends, TEXT_BUF_FILE_MAX and TEXT_BUF_ENDS_MAX on the panel.
 *        Without it /api/file answers WA_API_MEMORY.
 * @param api       the API
 * @param text      the bytes, or NULL for none
 * @param cap       count of @p text
 * @param ends      the line index, or NULL for none
 * @param ends_cap  count of @p ends
 */
void wa_api_set_text(wa_api_t *api, uint8_t *text, size_t cap,
                     uint32_t *ends, size_t ends_cap);

/**
 * @brief The log folders SETUP > SD CARD names, for the rules (fm_rules.h).
 * @param api        the API
 * @param user_logs  USER LOG FOLDER; NULL or "" is CARD_USER_LOGS
 * @param auto_logs  AUTO LOG FOLDER; NULL or "" is CARD_AUTO_LOGS
 */
void wa_api_set_logs(wa_api_t *api, const char *user_logs,
                     const char *auto_logs);

/**
 * @brief The answer of a core call as the API's.
 * @param e  the core's answer
 * @return WA_API_OK for FM_OK; the entry of the same name otherwise;
 *         WA_API_IO for a value outside fm_err_t
 */
wa_api_err_t wa_api_of(fm_err_t e);

/**
 * @brief The API's reason for a refusal of the request guard (wa_guard.h),
 *        so a read of the API refused there is answered in JSON as its
 *        other refusals are.
 * @param g  the guard's answer
 * @return WA_API_OK for WA_GUARD_OK, WA_API_BODY and WA_API_HOST for the
 *         two a read can meet, WA_API_REQUEST for the others, which only
 *         a write meets
 */
wa_api_err_t wa_api_of_guard(wa_guard_t g);

/**
 * @brief The reason word of @p e: "NOT_FOUND" and the like.
 * @param e  the reason
 * @return a constant string; "" for WA_API_OK, "IO" outside the enum
 */
const char *wa_api_word(wa_api_err_t e);

/**
 * @brief The HTTP status of @p e.
 * @param e  the reason
 * @return 200 for WA_API_OK; the status listed at the enum otherwise
 */
unsigned wa_api_http(wa_api_err_t e);

/**
 * @brief The HTTP status line of @p e, "404 Not Found" and the like.
 * @param e  the reason
 * @return a constant string
 */
const char *wa_api_status_line(wa_api_err_t e);

/**
 * @brief The sentence a refusal carries.
 * @param e   the reason
 * @param de  German
 * @return a constant string; "" for WA_API_OK
 */
const char *wa_api_text(wa_api_err_t e, bool de);

/**
 * @brief A refusal's body: {"error":"WORD","text":"..."}.
 * @param o   the output
 * @param e   the reason
 * @param de  German
 */
void wa_api_error_json(wa_out_t *o, wa_api_err_t e, bool de);

/**
 * @brief /api/status's body.  "user_logs" is the User log folder, where
 *        FILES opens its right pane.
 * @param o   the output
 * @param st  the panel's state
 */
void wa_api_status_json(wa_out_t *o, const wa_api_status_t *st);

/**
 * @brief Read the page /api/list asks for, into the API's request.
 * @param api    the API
 * @param query  the query string, without "?"; NULL is ""
 * @return WA_API_OK, or why not
 */
wa_api_err_t wa_api_list(wa_api_t *api, const char *query);

/**
 * @brief The page wa_api_list() read, as JSON.
 * @param api  the API, after wa_api_list() answered WA_API_OK
 * @param o    the output
 */
void wa_api_list_json(const wa_api_t *api, wa_out_t *o);

/**
 * @brief Read what /api/info asks for, into the API's request.
 * @param api    the API
 * @param query  the query string, without "?"; NULL is ""
 * @return WA_API_OK, or why not
 */
wa_api_err_t wa_api_info(wa_api_t *api, const char *query);

/**
 * @brief The facts wa_api_info() read, as JSON.  "t" is the stamp the card
 *        holds, -1 for none; "time" is it as INFO shows it,
 *        "YYYY-MM-DD HH:MM" in the zone in force (wall_local_s()), and ""
 *        for a stamp that says no date (wall_stamp_known()).
 * @param api  the API, after wa_api_info() answered WA_API_OK
 * @param o    the output
 */
void wa_api_info_json(const wa_api_t *api, wa_out_t *o);

/**
 * @brief Load the file /api/file asks for into the API's text room and
 *        hold it to the viewer's limits.
 * @param api    the API
 * @param query  the query string, without "?"; NULL is ""
 * @return WA_API_OK, or why not
 */
wa_api_err_t wa_api_file(wa_api_t *api, const char *query);

/**
 * @brief The status line and headers of the file wa_api_file() loaded,
 *        ending with the empty line.
 * @param api  the API, after wa_api_file() answered WA_API_OK
 * @param out  takes the text
 * @param cap  bytes at @p out; WA_API_HEAD_MAX is enough
 * @return its length; 0 when it does not fit
 */
size_t wa_api_file_head(const wa_api_t *api, char *out, size_t cap);

/**
 * @brief Bytes of the file wa_api_file() loaded, its BOM included.
 * @param api  the API
 * @return the count; 0 for NULL
 */
size_t wa_api_file_size(const wa_api_t *api);

/**
 * @brief Bytes of the loaded file from offset @p at on, as the card holds
 *        them.
 * @param api  the API
 * @param at   the offset
 * @param out  takes the bytes
 * @param n    room at @p out
 * @return bytes written; 0 at the end
 */
size_t wa_api_file_read(const wa_api_t *api, size_t at, void *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_API_H */
