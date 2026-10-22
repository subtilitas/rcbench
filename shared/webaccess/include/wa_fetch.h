/**
 * @file wa_fetch.h
 * @brief The panel asking GitHub for its newest release, and fetching it.
 *
 * Two jobs, each on the home network the panel keeps (wa_session.h,
 * WA_MODE_FETCH: no page, no name), each with the radio on only for its
 * length:
 *
 *   CHECK     join, then one HTTPS (HTTP over TLS, transport layer
 *             security) GET for the newest release of the job's channel.
 *             STABLE: FW_REL_LATEST_URL, whose redirect's Location names
 *             it (fw_release_parse_tag()); when that release is past the
 *             panel's release line and newer than the running build, one
 *             request of FW_REL_API_URL more, whose highest release of the
 *             line is offered in its place when it is newer than the
 *             running build, and which leaves the result as it was when it
 *             fails.  TESTING: FW_REL_API_URL with
 *             GitHub's API headers, whose answer's 10 newest releases by
 *             date name it, the highest version among them that is no
 *             draft (fw_release_scan_feed()), read up to the 10th
 *             release's end and no further, with a Content-Length or in
 *             chunks; 403 and 429, GitHub's request limit, end as LIMIT.
 *             Where the highest is past the panel's release line, the
 *             highest of the line among them stands in for it when it is
 *             offered.  Either way a release is NEWER only when
 *             fw_release_offered() says so; one newer than the running
 *             build and past the line is PAST_LINE.  Join, address and request
 *             take WA_FETCH_CHECK_MS at most, together.
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
 * WI-FI, CANCEL -- stays ended: what the runner reports after it is
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

/** Which releases a CHECK looks at: SETUP > UPDATES' RELEASES. */
typedef enum {
    WA_CH_STABLE = 0,  /**< GitHub's latest release: never a pre-release */
    WA_CH_TESTING,     /**< the newest release, testing or stable        */
} wa_channel_t;

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
    WA_GOT_LIMIT,      /**< CHECK on TESTING: 403 or 429, GitHub's limit
                            of requests without an account; see status  */
    WA_GOT_UNVERSIONED,/**< CHECK: the running version did not parse, so
                            nothing is offered; wa_fetch_t.found        */
    WA_GOT_PAST_LINE,  /**< CHECK: the newest release is newer than the
                            running build and past the panel's release
                            line (fw_release_in_line()): not offered;
                            wa_fetch_t.found                            */
} wa_got_t;

/** Why a job did not start, or was ended from outside. */
typedef enum {
    WA_WHY_NONE = 0,
    WA_WHY_ARMED,      /**< ARMED, or an arm waiting                    */
    WA_WHY_SUPPLY,     /**< the supply's output on or coming on         */
    WA_WHY_RUN,        /**< a log run open                              */
    WA_WHY_STOP,
    WA_WHY_FILES,      /**< WI-FI opened: it takes the radio   */
    WA_WHY_COPROC,     /**< the IOmcu's update runs               */
    WA_WHY_INSTALL,    /**< a panel install runs                        */
    WA_WHY_CANCEL,     /**< the operator cancelled                      */
    WA_WHY_BUSY,       /**< a job runs already                          */
    WA_WHY_DRAWING,    /**< a board drawing is fetched or kept: no flash
                            write, so no download either                */
} wa_why_t;

typedef struct {
    wa_job_t      job;
    wa_channel_t  channel;  /**< CHECK: the releases it looked at         */
    wa_phase_t    phase;
    wa_got_t      got;
    wa_why_t      why;      /**< BLOCKED, ABORTED                         */
    wa_join_err_t join;     /**< JOIN                                     */
    fw_rel_err_t  rel;      /**< UNEXPECTED, SUMS, and IMAGE by length    */
    fw_err_t      img;      /**< IMAGE                                    */
    int           status;   /**< REFUSED, LIMIT: the HTTP status          */
    uint32_t      gen;      /**< jobs started                             */
    uint32_t      start_ms;
    fw_version_t  running;
    fw_version_t  found;    /**< NEWER, CURRENT, UNVERSIONED, PAST_LINE;
                                 the release
                                 a DOWNLOAD fetches                       */
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
 * CHECK) on channel @p ch (CHECK; kept by DOWNLOAD too).  WA_GOT_NONE and
 * JOINING: the platform starts the radio in WA_MODE_FETCH.  Otherwise the
 * job has ended at once: NO_NET without @p have_net, BLOCKED for @p gate or
 * for a job already running (why).
 */
wa_got_t wa_fetch_start(wa_fetch_t *f, wa_job_t job, fw_version_t want,
                        wa_channel_t ch, bool have_net, wa_why_t gate,
                        uint32_t now_ms);

/**
 * Whether the last job is a CHECK that found a release to offer on channel
 * @p now: ended NEWER, and run on @p now.  A check on the other channel
 * offers nothing: RELEASES changed after it.
 */
bool wa_fetch_offers(const wa_fetch_t *f, wa_channel_t now);

/**
 * Whether the last job is a CHECK that ended on another channel than
 * @p now: its result is not shown, as if no check had run.
 */
bool wa_fetch_stale(const wa_fetch_t *f, wa_channel_t now);

/** Whether a job runs: joining to checking. */
bool wa_fetch_busy(const wa_fetch_t *f);

/**
 * @brief Whether the last job names a release in wa_fetch_t.found.
 * @param f the job; NULL reads as none
 * @return true for a DOWNLOAD, and for a CHECK that ended NEWER, CURRENT,
 *         UNVERSIONED or PAST_LINE; false otherwise
 */
bool wa_fetch_has_found(const wa_fetch_t *f);

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

/**
 * A response's head, as the client read it.  @c chunked is the header
 * Transfer-Encoding: chunked, not a guess from a missing length: GitHub's
 * API answers a request its cache does not hold in chunks, without a
 * Content-Length (its Cache-Control is max-age=60), and the same request
 * from its cache with one.
 */
typedef struct {
    int     status;     /**< HTTP status; 0 none                        */
    int64_t length;     /**< Content-Length; -1 none                    */
    bool    chunked;    /**< Transfer-Encoding: chunked                 */
    size_t  loc_n;      /**< Location bytes kept; 0 none                */
    bool    loc_cut;    /**< the Location was longer than the buffer    */
} wa_http_head_t;

/** The HTTP client: one request at a time, the platform's. */
typedef struct {
    /**
     * GET @p url, waiting @p timeout_ms at most for each step; with @p api
     * also the headers Accept: FW_REL_API_ACCEPT and X-GitHub-Api-Version:
     * FW_REL_API_VERSION.  The head in @p head, the Location's bytes (not
     * terminated) in @p loc of @p loc_cap.  No redirect is followed.
     * False: no connection, or no head in time.
     */
    bool    (*open)(void *ctx, const char *url, uint32_t timeout_ms,
                    bool api, wa_http_head_t *head, char *loc,
                    size_t loc_cap);
    /** Up to @p n body bytes: more than 0 read, 0 the end, below 0 broke
     *  off or nothing in @p timeout_ms.  On a chunked body 0 is its last
     *  chunk only: a body closed before it is below 0. */
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
    bool      running_ok;    /**< running parsed from the build's version */
    wa_channel_t channel;    /**< CHECK                                   */

    /* Scratch the platform owns, in PSRAM on the panel. */
    char     *url;           /**< FW_REL_URL_MAX + 1 bytes                */
    char     *loc;           /**< FW_REL_URL_MAX + 1 bytes                */
    uint8_t  *io;            /**< a read's piece                          */
    uint32_t  io_cap;
    char     *sums;          /**< FW_REL_SUMS_MAX bytes                   */
    fw_upload_t *up;         /**< DOWNLOAD: the image, staged on success  */
} wa_run_t;

/** CHECK, from the join on.  NEWER, CURRENT, UNVERSIONED, or why not. */
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
