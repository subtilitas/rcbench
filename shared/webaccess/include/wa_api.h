/**
 * @file
 * @brief The Wi-Fi page's file API: a page of a folder, the facts of one
 *        entry, a text file whole and the panel's state, each answered as
 *        JSON (JavaScript Object Notation) or as the file's bytes.
 *
 * Every card call runs through the file manager's runner (fm_req.h): the
 * same pages, rules, refusals, load and jobs as the FILES screen.
 *
 * Reads are GET with a query string.  A path is relative to the card's
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
 * Writes (wa_api_write.c) change the card through FILES' jobs (fm_ops.h),
 * under the same write gate, places and names:
 *
 *   PUT  /api/file?path=P&mount=M   body: the file's bytes, 1MiB at most
 *   POST /api/mkdir                 body: path=P&mount=M
 *   POST /api/rename                body: from=P&to=Q&dir=D&mount=M
 *   POST /api/delete                body: path=P&dir=D&mount=M
 *
 * A POST's body is a form of WA_API_FORM_MAX bytes at most, read as a
 * query string is: each path percent-encoded as above.  A PUT's body is
 * the file as it goes on the card, held to the viewer's limits (a NUL in
 * the first 4kB, more than 65,536 line ends and more than 1MiB refused)
 * before a byte is written, and saved through "<name>.NEW" (fm_job_save()).
 * Its If-Match: "<ETag>" replaces the file only while its CRC-32 is the
 * ETag a read of it gave (412 CHANGED otherwise, with the ETag of the file
 * it found, none for a file that is gone); If-None-Match: * makes
 * the file only where no entry has the name (409 EXISTS); neither header
 * makes or replaces it.  Both are asked before the body is read
 * (wa_api_put_check()) and again directly before the write.  A new file's name, every name of MKDIR and the
 * new name of RENAME follow the panel keyboard's rule (fm_name_check()).
 * RENAME with Q in P's folder renames the entry; with Q of the same name
 * in another folder it moves it there, a folder only when empty; any
 * other Q is a REQUEST.  DELETE takes a run's report along and a folder
 * only when empty.
 *
 * Whether a browser may write at all is wa_api_may_write()'s: SETUP's
 * Browser writes, the access code, the hold on the panel.  The request
 * guard (wa_guard.h) refuses a write from another site before it.
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
/** @brief The path that makes a folder. */
#define WA_API_URL_MKDIR  "/api/mkdir"
/** @brief The path that renames or moves an entry. */
#define WA_API_URL_RENAME "/api/rename"
/** @brief The path that deletes an entry. */
#define WA_API_URL_DELETE "/api/delete"

/** @brief Bytes of a write's form at most (wa_guard.h WA_FORM_BODY_MAX). */
#define WA_API_FORM_MAX   1024u

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
    WA_API_CHANGED,        /**< 412: the file is not the one read        */
    WA_API_NOT_TEXT,       /**< 415: a NUL in the first 4kB              */
    WA_API_TOO_MANY_LINES, /**< 422: more than 65,536 line ends          */
    WA_API_MEMORY,         /**< 503: no PSRAM for the request            */
    WA_API_CODE,           /**< 403: no access code, or a wrong one      */
    WA_API_REQUEST,        /**< 400: a parameter the API does not take   */
    WA_API_BODY,           /**< 400: a read that carries a body (guard)  */
    WA_API_HOST,           /**< 421: a Host that is not the panel (guard) */
    WA_API_TOO_LARGE,      /**< 413: a body over its limit (guard)       */
    WA_API_ORIGIN,         /**< 403: a write from another page (guard)   */
    WA_API_WRITES_OFF,     /**< 403: SETUP allows no browser writes      */
    WA_API_WRITES_HOLD,    /**< 403: no hold on the panel this session   */
    WA_API_WRITES_CODE,    /**< 403: a write without the access code     */
    WA_API_PANEL_BUSY,     /**< 503: FILES holds the card: Retry-After 2 */
    WA_API_FORM_LARGE,     /**< 413: a POST's form over WA_API_FORM_MAX  */
    WA_API_COUNT
} wa_api_err_t;

/**
 * @brief What a browser write needs, SETUP > SD CARD > Browser writes, in
 *        the option order of the setting.
 */
typedef enum {
    WA_WRITES_OPEN = 0,  /**< nothing beyond reaching the page: the default */
    WA_WRITES_CODE,      /**< this session's access code, with the code
                              switch off too                             */
    WA_WRITES_HOLD,      /**< a 2s hold on WI-FI, once a radio session    */
    WA_WRITES_OFF,       /**< no write at all                            */
    WA_WRITES_COUNT
} wa_writes_t;

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
    uint32_t etag;                    /**< its bytes' CRC-32, or a saved
                                           file's                        */
    uint32_t found_tag;               /**< a PUT refused CHANGED: the CRC-32
                                           of the file it found           */
    bool found;                       /**< found_tag holds one            */
    bool info_asked;                  /**< the last INFO asked etag=1 of a
                                           file                           */
    bool info_summed;                 /**< ... and etag holds its CRC-32  */
    bool created;                     /**< the last write made its entry   */
    char done[FM_PATH_SIZE];          /**< the path the last write left    */
    bool restart;                     /**< the last PUT saved a file the
                                           panel reads at its next start */
    bool put;                         /**< the last write was a PUT        */
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
    wa_writes_t writes;   /**< SETUP's Browser writes                      */
    bool window;          /**< WA_WRITES_HOLD: the hold opened writes      */
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
 * @param g     the guard's answer
 * @param kind  the request: a body over its limit names that limit
 * @return WA_API_OK for WA_GUARD_OK; WA_API_BODY and WA_API_HOST for the two
 *         a read can meet; WA_API_TOO_LARGE, or WA_API_FORM_LARGE for a
 *         POST's form, for a body over its limit; WA_API_ORIGIN for another
 *         page; WA_API_REQUEST for a value outside the enum
 */
wa_api_err_t wa_api_of_guard(wa_guard_t g, wa_req_kind_t kind);

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
 *        FILES opens its right pane.  "writes" is "open" or "closed",
 *        "why" the first reason a write is refused now (the gate's word,
 *        WRITES_OFF, WRITES_HOLD) or "", "why_text" its sentence, and
 *        "mode" the setting: OPEN, CODE, HOLD or OFF.  With CODE the
 *        answer says "open": the code is checked per write.
 * @param o   the output
 * @param st  the panel's state
 */
void wa_api_status_json(wa_out_t *o, const wa_api_status_t *st);

/**
 * @brief Whether a browser write may go on to the card's rules.
 * @param mode    SETUP's Browser writes
 * @param code    the request carries this session's access code
 * @param window  the hold on the panel opened writes this session
 * @return WA_API_OK; WA_API_WRITES_OFF, WA_API_WRITES_HOLD or
 *         WA_API_WRITES_CODE; WA_API_WRITES_OFF for a mode outside the enum
 */
wa_api_err_t wa_api_may_write(wa_writes_t mode, bool code, bool window);

/**
 * @brief The setting's word for @p mode: "OPEN", "CODE", "HOLD", "OFF".
 * @param mode  the mode
 * @return a constant string; "OFF" outside the enum
 */
const char *wa_api_writes_word(wa_writes_t mode);

/**
 * @brief Where a PUT's body goes: the API's text room, emptied.
 * @param api  the API
 * @param cap  takes the bytes it holds, TEXT_BUF_FILE_MAX on the panel
 * @return the room; NULL without one (wa_api_set_text())
 */
uint8_t *wa_api_body(wa_api_t *api, size_t *cap);

/**
 * @brief What PUT /api/file asks before its body: the query and the headers,
 *        the rules, and the file there against If-Match or If-None-Match.
 *        Nothing is written.  wa_api_put() asks all of it again, at the
 *        write: a file changed while the body arrived is refused there.
 * @param api       the API
 * @param query     the query string, without "?"; NULL is ""
 * @param if_match  the If-Match header, or NULL
 * @param if_none   the If-None-Match header, or NULL
 * @param len       the body's bytes, Content-Length
 * @return WA_API_OK, or the refusal wa_api_put() would give, with the ETag
 *         of the file a CHANGED found
 */
wa_api_err_t wa_api_put_check(wa_api_t *api, const char *query,
                              const char *if_match, const char *if_none,
                              size_t len);

/**
 * @brief PUT /api/file: the @p len bytes at wa_api_body() as file P.
 * @param api       the API
 * @param query     the query string, without "?"; NULL is ""
 * @param if_match  the If-Match header, or NULL
 * @param if_none   the If-None-Match header, or NULL
 * @param len       bytes of the body received
 * @return WA_API_OK, with created, etag and done set; or why not
 */
wa_api_err_t wa_api_put(wa_api_t *api, const char *query,
                        const char *if_match, const char *if_none,
                        size_t len);

/**
 * @brief POST /api/mkdir.
 * @param api   the API
 * @param form  the body, NUL-terminated; NULL is ""
 * @return WA_API_OK, with done set; or why not
 */
wa_api_err_t wa_api_mkdir(wa_api_t *api, const char *form);

/**
 * @brief POST /api/rename: a rename in P's folder, or a move.
 * @param api   the API
 * @param form  the body, NUL-terminated; NULL is ""
 * @return WA_API_OK, with done set; or why not
 */
wa_api_err_t wa_api_rename(wa_api_t *api, const char *form);

/**
 * @brief POST /api/delete.
 * @param api   the API
 * @param form  the body, NUL-terminated; NULL is ""
 * @return WA_API_OK, with done set; or why not
 */
wa_api_err_t wa_api_delete(wa_api_t *api, const char *form);

/**
 * @brief The status of a write's answer: 201 for an entry made, 200 else.
 * @param api  the API, after a write answered WA_API_OK
 * @return "201 Created" or "200 OK"
 */
const char *wa_api_write_status(const wa_api_t *api);

/**
 * @brief A write's body: {"path":"...","created":B,"etag":"xxxxxxxx",
 *        "note":"ESC_NEXT_START" or ""}; "etag" only after a PUT.
 * @param api  the API, after a write answered WA_API_OK
 * @param o    the output
 */
void wa_api_write_json(const wa_api_t *api, wa_out_t *o);

/**
 * @brief A refused write's body: wa_api_error_json(), and for a PUT refused
 *        CHANGED over a file that is there, "etag":"xxxxxxxx" of that file
 *        as the compare read it.
 * @param api  the API; may be NULL for a refusal before the core
 * @param o    the output
 * @param e    why it is refused
 * @param de   German texts
 */
void wa_api_refused_json(const wa_api_t *api, wa_out_t *o, wa_api_err_t e,
                         bool de);

/** @brief Room for an ETag header's value: '"', 8 hex digits, '"', NUL. */
#define WA_API_ETAG_SIZE 11u

/**
 * @brief The ETag header a write's answer carries: the saved bytes'
 *        CRC-32 as a GET of the file gives it, after a PUT only.
 * @param api  the API, after a write answered WA_API_OK
 * @param out  takes "\"xxxxxxxx\"", WA_API_ETAG_SIZE bytes
 * @return true after a PUT; false, and @p out empty, otherwise
 */
bool wa_api_write_etag(const wa_api_t *api, char *out);

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
 * @brief Read what /api/info asks for, into the API's request.  With etag=1
 *        a file of TEXT_BUF_FILE_MAX bytes at most is read for the CRC-32
 *        GET /api/file would give as its ETag, a chunk at a time and in no
 *        room of its own: the bytes do not cross the network.  A larger
 *        file has no ETag; what the rules refuse a read of is refused.
 * @param api    the API
 * @param query  the query string, without "?"; NULL is ""
 * @return WA_API_OK, or why not
 */
wa_api_err_t wa_api_info(wa_api_t *api, const char *query);

/**
 * @brief The facts wa_api_info() read, as JSON.  "t" is the stamp the card
 *        holds, -1 for none; "time" is it as INFO shows it,
 *        "YYYY-MM-DD HH:MM" in the zone in force (wall_local_s()), and ""
 *        for a stamp that says no date (wall_stamp_known()).  "etag" only
 *        when a file was asked with etag=1: its CRC-32, 8 hex digits, or ""
 *        for a file over TEXT_BUF_FILE_MAX.
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
