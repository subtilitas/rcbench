/*
 * The panel asking GitHub for its newest release, and fetching it.
 *
 * Two jobs, each on the home network the panel keeps (wa_session.h,
 * WA_MODE_FETCH: no page, no name), each with the radio on only for its
 * length:
 *
 *   CHECK     join, then one HTTPS (HTTP over TLS, transport layer
 *             security) GET of FW_REL_LATEST_URL; the redirect's Location
 *             names the newest release (fw_release_parse_tag()).  Join,
 *             address and request take WA_FETCH_CHECK_MS at most, together.
 *   DOWNLOAD  join within WA_FETCH_JOIN_MS, then SHA256SUMS and the panel
 *             image of one release, each through at most FW_REL_HOPS_MAX
 *             redirects to the hosts fw_release.h allows, the image into
 *             memory; its SHA-256 against its SHA256SUMS line, then every
 *             fw_image_check(); WA_FETCH_DOWNLOAD_MS at most from the start.
 *             What it leaves is an image staged as an upload would leave
 *             it, for the existing installer.  No flash is written here.
 *
 * Two halves.  The job (wa_fetch_t) lives with the session under the
 * platform's lock: started and ticked by the UI loop, which holds the
 * budget, the gate and every abort, and told by the radio and by the
 * runner what happened.  The runner (wa_run_check(), wa_run_download())
 * runs on a task of its own against an HTTP client the platform passes in
 * (wa_http_t), so the host suite runs it against a scripted one.  A job
 * ended from outside -- the budget, STOP, ARMED, the supply, a log run,
 * FILES & UPDATE, CANCEL -- stays ended: what the runner reports after it is
 * dropped (the job's generation).
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_FETCH_H
#define RCBENCH_WA_FETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_image.h"
#include "fw_release.h"
#include "fw_upload.h"
#include "wa_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** CHECK: join, address and the request, together. */
#define WA_FETCH_CHECK_MS     30000u
/** DOWNLOAD: join and address. */
#define WA_FETCH_JOIN_MS      30000u
/** DOWNLOAD: from the start to a checked image. */
#define WA_FETCH_DOWNLOAD_MS  180000u
/** One connection or one read waits this long at most, and never past the
 *  job's budget. */
#define WA_FETCH_OP_MS        10000u

typedef enum {
    WA_JOB_CHECK = 0,
    WA_JOB_DOWNLOAD,
} wa_job_t;

typedef enum {
    WA_PH_IDLE = 0,    /**< no job since the start                      */
    WA_PH_JOINING,     /**< the radio joins and takes an address        */
    WA_PH_ASKING,      /**< CHECK: the request to GitHub                */
    WA_PH_SUMS,        /**< DOWNLOAD: SHA256SUMS                         */
    WA_PH_IMAGE,       /**< DOWNLOAD: the image                         */
    WA_PH_CHECKING,    /**< DOWNLOAD: SHA-256 and the image checks       */
    WA_PH_ENDED,       /**< see wa_fetch_t.got                           */
} wa_phase_t;

/** How a job ended. */
typedef enum {
    WA_GOT_NONE = 0,   /**< under way, or none ran                      */
    WA_GOT_NEWER,      /**< CHECK: a newer release, wa_fetch_t.found    */
    WA_GOT_CURRENT,    /**< CHECK: none newer than the running build    */
    WA_GOT_STAGED,     /**< DOWNLOAD: the image waits, checked          */
    WA_GOT_NO_NET,     /**< no home network kept: nothing started       */
    WA_GOT_BLOCKED,    /**< the gate: nothing started; see why          */
    WA_GOT_ABORTED,    /**< ended from outside; see why                 */
    WA_GOT_JOIN,       /**< the join failed; see join                   */
    WA_GOT_RADIO,      /**< the radio did not come up                   */
    WA_GOT_TIMEOUT,    /**< the budget ran out                          */
    WA_GOT_CONNECT,    /**< no connection: name, TCP or TLS failed      */
    WA_GOT_REFUSED,    /**< an HTTP status the step does not take       */
    WA_GOT_UNEXPECTED, /**< an answer out of form; see rel              */
    WA_GOT_SUMS,       /**< SHA256SUMS missing, out of form or without
                            the image's line; see rel                   */
    WA_GOT_HASH,       /**< the image's SHA-256 is not SHA256SUMS'      */
    WA_GOT_IMAGE,      /**< the image checks refused it; see img        */
    WA_GOT_MEMORY,     /**< no memory for the image                     */
} wa_got_t;

/** Why a job did not start, or was ended from outside. */
typedef enum {
    WA_WHY_NONE = 0,
    WA_WHY_ARMED,      /**< ARMED, or an arm waiting                    */
    WA_WHY_SUPPLY,     /**< the supply's output on or coming on         */
    WA_WHY_RUN,        /**< a log run open                              */
    WA_WHY_STOP,
    WA_WHY_FILES,      /**< FILES & UPDATE opened: it takes the radio   */
    WA_WHY_COPROC,     /**< the coprocessor's update runs               */
    WA_WHY_INSTALL,    /**< a panel install runs                        */
    WA_WHY_CANCEL,     /**< the operator cancelled                      */
    WA_WHY_BUSY,       /**< a job runs already                          */
    WA_WHY_DRAWING,    /**< a board drawing is fetched or kept: no flash
                            write, so no download either                */
} wa_why_t;

typedef struct {
    wa_job_t      job;
    wa_phase_t    phase;
    wa_got_t      got;
    wa_why_t      why;      /**< BLOCKED, ABORTED                         */
    wa_join_err_t join;     /**< JOIN                                     */
    fw_rel_err_t  rel;      /**< UNEXPECTED, SUMS, and IMAGE by length    */
    fw_err_t      img;      /**< IMAGE                                    */
    int           status;   /**< REFUSED: the HTTP status                 */
    uint32_t      gen;      /**< jobs started                             */
    uint32_t      start_ms;
    fw_version_t  running;
    fw_version_t  found;    /**< NEWER, CURRENT; the release a DOWNLOAD
                                 fetches                                  */
    uint32_t      done;     /**< IMAGE: bytes received                    */
    uint32_t      total;    /**< IMAGE: bytes announced                   */
    uint32_t      budget_ms;/**< TIMEOUT: the budget that ran out         */
    bool          owe_off;  /**< ended: the radio goes off                */
    bool          owe_run;  /**< joined: the runner starts                */
} wa_fetch_t;

/** No job; @p running is the build that runs. */
void wa_fetch_init(wa_fetch_t *f, fw_version_t running);

/** The gate as wa_gate() takes it, plus the panel's own two. */
wa_why_t wa_fetch_gate(bool armed, bool supply, bool run, bool coproc,
                       bool installing);

/**
 * Start @p job at @p now_ms for release @p want (DOWNLOAD; ignored by
 * CHECK).  WA_GOT_NONE and JOINING: the platform starts the radio in
 * WA_MODE_FETCH.  Otherwise the job has ended at once: NO_NET without
 * @p have_net, BLOCKED for @p gate or for a job already running (why).
 */
wa_got_t wa_fetch_start(wa_fetch_t *f, wa_job_t job, fw_version_t want,
                        bool have_net, wa_why_t gate, uint32_t now_ms);

/** Whether a job runs: joining to checking. */
bool wa_fetch_busy(const wa_fetch_t *f);

/**
 * Once a frame, after wa_tick(): @p abort as the UI loop sees it now
 * (WA_WHY_NONE for none), @p w the session.  Ends the job for an abort, for
 * the session going off under it and for the budget; moves JOINING on once
 * the session has its address.  The platform reads owe_off and owe_run
 * after it.
 */
void wa_fetch_tick(wa_fetch_t *f, uint32_t now_ms, wa_why_t abort,
                   const wa_t *w);

/** End the job now for @p why; nothing when none runs. */
void wa_fetch_abort(wa_fetch_t *f, wa_why_t why);

/**
 * ms of the budget left at @p now_ms for the phase under way; 0 when it is
 * spent or no job runs.
 */
uint32_t wa_fetch_left(const wa_fetch_t *f, uint32_t now_ms);

/** True once after the job ended: the radio goes off. */
bool wa_fetch_take_off(wa_fetch_t *f);
/** True once after the join: the runner starts. */
bool wa_fetch_take_run(wa_fetch_t *f);

/** What the runner ends with. */
typedef struct {
    wa_got_t     got;
    wa_why_t     why;
    fw_rel_err_t rel;
    fw_err_t     img;
    int          status;
    fw_version_t found;
} wa_fetch_res_t;

/* What the runner reports, each for job @p gen; a report for another job,
 * or for one that has ended, is dropped. */
void wa_fetch_on_phase(wa_fetch_t *f, uint32_t gen, wa_phase_t ph);
void wa_fetch_on_bytes(wa_fetch_t *f, uint32_t gen, uint32_t done,
                       uint32_t total);
void wa_fetch_on_end(wa_fetch_t *f, uint32_t gen, const wa_fetch_res_t *r);

/* ---------------------------------------------------------- the runner */

/** A response's head, as the client read it. */
typedef struct {
    int     status;     /**< HTTP status; 0 none                        */
    int64_t length;     /**< Content-Length; -1 none                    */
    bool    chunked;
    size_t  loc_n;      /**< Location bytes kept; 0 none                */
    bool    loc_cut;    /**< the Location was longer than the buffer    */
} wa_http_head_t;

/** The HTTP client: one request at a time, the platform's. */
typedef struct {
    /**
     * GET @p url, waiting @p timeout_ms at most for each step; the head in
     * @p head, the Location's bytes (not terminated) in @p loc of
     * @p loc_cap.  No redirect is followed.  False: no connection, or no
     * head in time.
     */
    bool    (*open)(void *ctx, const char *url, uint32_t timeout_ms,
                    wa_http_head_t *head, char *loc, size_t loc_cap);
    /** Up to @p n body bytes: more than 0 read, 0 the end, below 0 broke
     *  off or nothing in @p timeout_ms. */
    int32_t (*read)(void *ctx, uint8_t *dst, uint32_t n,
                    uint32_t timeout_ms);
    void    (*close)(void *ctx);
    void    *ctx;
} wa_http_t;

typedef struct {
    const wa_http_t *http;
    uint32_t (*now)(void *ctx);
    /** Whether the job has been ended from outside; asked between steps. */
    bool     (*stopped)(void *ctx);
    /** A phase begun, and the image's bytes so far; may be NULL. */
    void     (*report)(void *ctx, wa_phase_t ph, uint32_t done,
                       uint32_t total);
    /** A buffer of @p bytes for the image, in PSRAM on the panel; NULL for
     *  none.  The runner keeps it in @p up; the platform frees it. */
    uint8_t *(*alloc)(void *ctx, uint32_t bytes);
    void     *ctx;

    uint32_t  start_ms;      /**< the job's                               */
    uint32_t  budget_ms;     /**< from start_ms                           */
    uint32_t  slot;          /**< the largest image taken, in bytes       */
    fw_version_t running;

    /* Scratch the platform owns, in PSRAM on the panel. */
    char     *url;           /**< FW_REL_URL_MAX + 1 bytes                */
    char     *loc;           /**< FW_REL_URL_MAX + 1 bytes                */
    uint8_t  *io;            /**< a read's piece                          */
    uint32_t  io_cap;
    char     *sums;          /**< FW_REL_SUMS_MAX bytes                   */
    fw_upload_t *up;         /**< DOWNLOAD: the image, staged on success  */
} wa_run_t;

/** CHECK, from the join on.  NEWER, CURRENT, or why not. */
void wa_run_check(const wa_run_t *r, wa_fetch_res_t *out);

/**
 * DOWNLOAD of release @p v, from the join on.  STAGED with the image in
 * r->up, or why not with r->up not staged.
 */
void wa_run_download(const wa_run_t *r, fw_version_t v, wa_fetch_res_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_FETCH_H */
