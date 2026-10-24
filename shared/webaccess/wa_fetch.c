/**
 * @file wa_fetch.c
 * @brief wa_fetch.h says what the jobs do; fw_release.h how strictly answers
 *        are read.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_fetch.h"

#include <string.h>

#include "fw_sha256.h"

/* ------------------------------------------------------------- the job */

void wa_fetch_init(wa_fetch_t *f, fw_version_t running)
{
    memset(f, 0, sizeof(*f));
    f->running = running;
}

wa_why_t wa_fetch_gate(bool armed, bool supply, bool run, bool coproc,
                       bool installing)
{
    if (armed) {
        return WA_WHY_ARMED;
    }
    if (supply) {
        return WA_WHY_SUPPLY;
    }
    if (run) {
        return WA_WHY_RUN;
    }
    if (installing) {
        return WA_WHY_INSTALL;
    }
    return coproc ? WA_WHY_COPROC : WA_WHY_NONE;
}

wa_why_t wa_fetch_start_gate(wa_why_t gate, bool taken, bool task_alive,
                             bool other)
{
    if (gate != WA_WHY_NONE) {
        return gate;
    }
    return (taken || task_alive || other) ? WA_WHY_BUSY : WA_WHY_NONE;
}

bool wa_fetch_busy(const wa_fetch_t *f)
{
    return f->phase != WA_PH_IDLE && f->phase != WA_PH_ENDED;
}

bool wa_fetch_stale(const wa_fetch_t *f, wa_channel_t now)
{
    return f->job == WA_JOB_CHECK && f->phase == WA_PH_ENDED
           && f->channel != now;
}

bool wa_fetch_offers(const wa_fetch_t *f, wa_channel_t now)
{
    return f->job == WA_JOB_CHECK && f->phase == WA_PH_ENDED
           && f->got == WA_GOT_NEWER && !wa_fetch_stale(f, now);
}

static uint32_t budget(const wa_fetch_t *f)
{
    if (f->job == WA_JOB_CHECK) {
        return WA_FETCH_CHECK_MS;
    }
    return (f->phase == WA_PH_JOINING) ? WA_FETCH_JOIN_MS
                                       : WA_FETCH_DOWNLOAD_MS;
}

static void end(wa_fetch_t *f, wa_got_t got, wa_why_t why)
{
    if (got == WA_GOT_TIMEOUT) {
        f->budget_ms = budget(f);    /* of the phase it ran out in */
    }
    f->phase = WA_PH_ENDED;
    f->got = got;
    f->why = why;
    f->owe_off = true;
    f->owe_run = false;
}

wa_got_t wa_fetch_start(wa_fetch_t *f, wa_job_t job, fw_version_t want,
                        wa_channel_t ch, bool have_net, wa_why_t gate,
                        uint32_t now_ms)
{
    if (wa_fetch_busy(f)) {
        /* The running job stays as it is; nothing about it changes. */
        return WA_GOT_BLOCKED;
    }
    const fw_version_t running = f->running;
    const uint32_t gen = f->gen + 1u;
    memset(f, 0, sizeof(*f));
    f->running = running;
    f->gen = gen;
    f->job = job;
    f->channel = (ch == WA_CH_TESTING) ? WA_CH_TESTING : WA_CH_STABLE;
    f->start_ms = now_ms;
    f->found = (job == WA_JOB_DOWNLOAD) ? want : (fw_version_t){ 0, 0, 0 };
    if (!have_net) {
        f->phase = WA_PH_ENDED;
        f->got = WA_GOT_NO_NET;
        return f->got;
    }
    if (gate != WA_WHY_NONE) {
        f->phase = WA_PH_ENDED;
        f->got = WA_GOT_BLOCKED;
        f->why = gate;
        return f->got;
    }
    f->phase = WA_PH_JOINING;
    return WA_GOT_NONE;
}

uint32_t wa_fetch_left(const wa_fetch_t *f, uint32_t now_ms)
{
    if (!wa_fetch_busy(f)) {
        return 0u;
    }
    const uint32_t spent = (uint32_t)(now_ms - f->start_ms);
    const uint32_t b = budget(f);
    return (spent >= b) ? 0u : b - spent;
}

void wa_fetch_abort(wa_fetch_t *f, wa_why_t why)
{
    if (wa_fetch_busy(f)) {
        end(f, WA_GOT_ABORTED, why);
    }
}

/* The session went off under the job: why, in the job's words. */
static void session_off(wa_fetch_t *f, const wa_t *w)
{
    switch (w->off) {
    case WA_OFF_JOIN:
        f->join = w->join_err;
        end(f, WA_GOT_JOIN, WA_WHY_NONE);
        break;
    case WA_OFF_FAILED:  end(f, WA_GOT_RADIO, WA_WHY_NONE);      break;
    case WA_OFF_ARMED:   end(f, WA_GOT_ABORTED, WA_WHY_ARMED);   break;
    case WA_OFF_SUPPLY:  end(f, WA_GOT_ABORTED, WA_WHY_SUPPLY);  break;
    case WA_OFF_RUN:     end(f, WA_GOT_ABORTED, WA_WHY_RUN);     break;
    case WA_OFF_STOP:    end(f, WA_GOT_ABORTED, WA_WHY_STOP);    break;
    case WA_OFF_INSTALL: end(f, WA_GOT_ABORTED, WA_WHY_INSTALL); break;
    case WA_OFF_IDLE:    end(f, WA_GOT_TIMEOUT, WA_WHY_NONE);    break;
    case WA_OFF_NONE:
    case WA_OFF_GUESSES:
    case WA_OFF_ASKED:
    default:             end(f, WA_GOT_ABORTED, WA_WHY_CANCEL);  break;
    }
}

void wa_fetch_tick(wa_fetch_t *f, uint32_t now_ms, wa_why_t abort,
                   const wa_t *w)
{
    if (!wa_fetch_busy(f)) {
        return;
    }
    if (abort != WA_WHY_NONE) {
        end(f, WA_GOT_ABORTED, abort);
        return;
    }
    if (w->mode != WA_MODE_FETCH) {
        /* Another session took the radio: WI-FI. */
        end(f, WA_GOT_ABORTED, WA_WHY_FILES);
        return;
    }
    if (w->state == WA_ST_OFF) {
        session_off(f, w);
        return;
    }
    if (wa_fetch_left(f, now_ms) == 0u) {
        end(f, WA_GOT_TIMEOUT, WA_WHY_NONE);
        return;
    }
    if (f->phase == WA_PH_JOINING && w->state == WA_ST_JOINED) {
        f->phase = (f->job == WA_JOB_CHECK) ? WA_PH_ASKING : WA_PH_SUMS;
        f->owe_run = true;
        /* The download's own budget from here: WA_FETCH_DOWNLOAD_MS counts
         * from the start, the join included. */
    }
}

bool wa_fetch_take_off(wa_fetch_t *f)
{
    const bool owe = f->owe_off;
    f->owe_off = false;
    return owe;
}

bool wa_fetch_take_run(wa_fetch_t *f)
{
    const bool owe = f->owe_run;
    f->owe_run = false;
    return owe;
}

static bool mine(const wa_fetch_t *f, uint32_t gen)
{
    return gen == f->gen && wa_fetch_busy(f) && f->phase != WA_PH_JOINING;
}

void wa_fetch_on_phase(wa_fetch_t *f, uint32_t gen, wa_phase_t ph)
{
    if (mine(f, gen) && ph > WA_PH_JOINING && ph < WA_PH_ENDED) {
        f->phase = ph;
    }
}

void wa_fetch_on_bytes(wa_fetch_t *f, uint32_t gen, uint32_t done,
                       uint32_t total)
{
    if (mine(f, gen)) {
        f->done = done;
        f->total = total;
    }
}

/**
 * @brief Whether a CHECK's result names the release it found.
 * @param got the result
 * @return true for NEWER, CURRENT, UNVERSIONED and PAST_LINE
 */
static bool got_found(wa_got_t got)
{
    return got == WA_GOT_NEWER || got == WA_GOT_CURRENT
           || got == WA_GOT_UNVERSIONED || got == WA_GOT_PAST_LINE;
}

bool wa_fetch_has_found(const wa_fetch_t *f)
{
    return f != NULL && (f->job == WA_JOB_DOWNLOAD || got_found(f->got));
}

void wa_fetch_on_end(wa_fetch_t *f, uint32_t gen, const wa_fetch_res_t *r)
{
    if (!mine(f, gen)) {
        return;
    }
    f->rel = r->rel;
    f->img = r->img;
    f->status = r->status;
    if (got_found(r->got)) {
        f->found = r->found;
    }
    end(f, r->got, r->why);
}

/* ---------------------------------------------------------- the runner */

static uint32_t run_left(const wa_run_t *r)
{
    const uint32_t spent = (uint32_t)(r->now(r->ctx) - r->start_ms);
    return (spent >= r->budget_ms) ? 0u : r->budget_ms - spent;
}

static uint32_t op_ms(uint32_t left)
{
    return (left < WA_FETCH_OP_MS) ? left : WA_FETCH_OP_MS;
}

static void report(const wa_run_t *r, wa_phase_t ph, uint32_t done,
                   uint32_t total)
{
    if (r->report != NULL) {
        r->report(r->ctx, ph, done, total);
    }
}

static void res(wa_fetch_res_t *out, wa_got_t got, fw_rel_err_t rel)
{
    out->got = got;
    out->rel = rel;
}

/* What a step that cannot go on says: the budget, an abort, or @p got. */
static void failed(const wa_run_t *r, wa_fetch_res_t *out, wa_got_t got,
                   fw_rel_err_t rel)
{
    if (r->stopped(r->ctx)) {
        res(out, WA_GOT_ABORTED, FW_REL_OK);
    } else if (run_left(r) == 0u) {
        res(out, WA_GOT_TIMEOUT, FW_REL_OK);
    } else {
        res(out, got, rel);
    }
}

/* Where a fetched body goes. */
typedef struct {
    /* The announced size, already within the cap: false refuses it, with
     * the result in @p out. */
    bool (*begin)(const wa_run_t *r, void *ctx, uint32_t bytes,
                  wa_fetch_res_t *out);
    void (*add)(const wa_run_t *r, void *ctx, const uint8_t *d, uint32_t n,
                uint32_t done, uint32_t total);
    void *ctx;
    uint32_t cap;
    wa_got_t bad;   /* a body out of form, or refused: UNEXPECTED or SUMS */
} sink_t;

/*
 * GET r->url, following at most FW_REL_HOPS_MAX redirects to the allowed
 * hosts, into @p k.  True with the whole body taken; false with @p out set.
 */
static bool fetch(const wa_run_t *r, const sink_t *k, wa_fetch_res_t *out)
{
    const wa_http_t *h = r->http;
    for (unsigned hops = 0u;; ++hops) {
        if (r->stopped(r->ctx)) {
            res(out, WA_GOT_ABORTED, FW_REL_OK);
            return false;
        }
        const uint32_t left = run_left(r);
        if (left == 0u) {
            res(out, WA_GOT_TIMEOUT, FW_REL_OK);
            return false;
        }
        wa_http_head_t head = { .status = 0, .length = -1 };
        if (!h->open(h->ctx, r->url, op_ms(left), false, &head, r->loc,
                     FW_REL_URL_MAX)) {
            h->close(h->ctx);
            failed(r, out, WA_GOT_CONNECT, FW_REL_OK);
            return false;
        }
        const size_t loc_n = (head.loc_n > FW_REL_URL_MAX) ? FW_REL_URL_MAX
                                                           : head.loc_n;
        switch (fw_release_status(head.status)) {
        case FW_REL_REDIRECT: {
            h->close(h->ctx);
            const fw_rel_err_t e = fw_release_redirect(r->loc, loc_n,
                                                       head.loc_cut, hops);
            if (e != FW_REL_OK) {
                res(out, WA_GOT_UNEXPECTED, e);
                return false;
            }
            memcpy(r->url, r->loc, loc_n);
            r->url[loc_n] = '\0';
            continue;
        }
        case FW_REL_REFUSED:
            h->close(h->ctx);
            out->status = head.status;
            res(out, (k->bad == WA_GOT_SUMS) ? WA_GOT_SUMS : WA_GOT_REFUSED,
                FW_REL_STATUS);
            return false;
        case FW_REL_BODY:
        default:
            break;
        }
        const fw_rel_err_t e = fw_release_length(head.length, head.chunked,
                                                 k->cap);
        if (e != FW_REL_OK) {
            h->close(h->ctx);
            res(out, k->bad, e);
            return false;
        }
        const uint32_t total = (uint32_t)head.length;
        if (!k->begin(r, k->ctx, total, out)) {
            h->close(h->ctx);
            return false;
        }
        uint32_t done = 0u;
        while (done < total) {
            if (r->stopped(r->ctx)) {
                h->close(h->ctx);
                res(out, WA_GOT_ABORTED, FW_REL_OK);
                return false;
            }
            const uint32_t now_left = run_left(r);
            if (now_left == 0u) {
                h->close(h->ctx);
                res(out, WA_GOT_TIMEOUT, FW_REL_OK);
                return false;
            }
            const uint32_t want = (total - done < r->io_cap) ? total - done
                                                             : r->io_cap;
            const int32_t got = h->read(h->ctx, r->io, want, op_ms(now_left));
            if (got < 0) {
                h->close(h->ctx);
                failed(r, out, WA_GOT_CONNECT, FW_REL_OK);
                return false;
            }
            if (got == 0) {
                h->close(h->ctx);
                res(out, k->bad, FW_REL_SHORT);
                return false;
            }
            if ((uint32_t)got > want) {
                h->close(h->ctx);
                res(out, k->bad, FW_REL_OVERRUN);
                return false;
            }
            done += (uint32_t)got;
            k->add(r, k->ctx, r->io, (uint32_t)got, done, total);
        }
        h->close(h->ctx);
        return true;
    }
}

/**
 * @brief The result of a check that found release @p v, offered or not.
 *
 * One of the panel's release line newer than the running build is NEWER; a
 * newer one past the line is PAST_LINE; an older or equal one is CURRENT;
 * any with a running version that did not parse is UNVERSIONED.
 *
 * @param r   the run: the running version
 * @param out the result, with @p v as its found
 * @param v   the release the check found
 */
static void found(const wa_run_t *r, wa_fetch_res_t *out, fw_version_t v)
{
    out->found = v;
    if (!r->running_ok) {
        res(out, WA_GOT_UNVERSIONED, FW_REL_OK);
    } else if (fw_release_offered(v, r->running, true)) {
        res(out, WA_GOT_NEWER, FW_REL_OK);
    } else if (fw_version_cmp(v, r->running) > 0) {
        res(out, WA_GOT_PAST_LINE, FW_REL_OK);
    } else {
        res(out, WA_GOT_CURRENT, FW_REL_OK);
    }
}

/**
 * @brief One request of FW_REL_API_URL, its answer read a piece at a time
 *        into the scan until it ends.
 *
 * The bytes after the 10th release are not read: the connection closes.  A
 * chunked answer has no length to hold it to: it is read until the scan
 * ends, its last chunk, or FW_REL_SCAN_MAX bytes.
 *
 * @param r         the run
 * @param left      ms of the budget left
 * @param out       why it failed, on false
 * @param v         the highest no-draft release, on true
 * @param have_line set on true: a no-draft release of the line was read
 * @param line      the highest of them, when @p have_line
 * @return true when the answer was read and its scan ended with FW_REL_OK
 */
static bool api_scan(const wa_run_t *r, uint32_t left, wa_fetch_res_t *out,
                     fw_version_t *v, bool *have_line, fw_version_t *line)
{
    const wa_http_t *h = r->http;
    wa_http_head_t head = { .status = 0, .length = -1 };
    if (!h->open(h->ctx, FW_REL_API_URL, op_ms(left), true, &head, r->loc,
                 FW_REL_URL_MAX)) {
        h->close(h->ctx);
        failed(r, out, WA_GOT_CONNECT, FW_REL_OK);
        return false;
    }
    if (head.status == 403 || head.status == 429) {
        h->close(h->ctx);
        out->status = head.status;
        res(out, WA_GOT_LIMIT, FW_REL_STATUS);
        return false;
    }
    if (head.status != 200) {
        h->close(h->ctx);
        out->status = head.status;
        res(out, WA_GOT_REFUSED, FW_REL_STATUS);
        return false;
    }
    const bool chunked = head.chunked;
    uint32_t total = FW_REL_SCAN_MAX;    /* chunked: the most read */
    if (!chunked) {
        const fw_rel_err_t le = fw_release_length(head.length, false,
                                                  FW_REL_API_MAX);
        if (le != FW_REL_OK) {
            h->close(h->ctx);
            res(out, WA_GOT_UNEXPECTED, le);
            return false;
        }
        total = (uint32_t)head.length;
    }
    fw_rel_scan_t scan;
    fw_release_scan_init(&scan);
    uint32_t done = 0u;
    bool ended = false;
    while (!ended && done < total) {
        if (r->stopped(r->ctx)) {
            h->close(h->ctx);
            res(out, WA_GOT_ABORTED, FW_REL_OK);
            return false;
        }
        const uint32_t now_left = run_left(r);
        if (now_left == 0u) {
            h->close(h->ctx);
            res(out, WA_GOT_TIMEOUT, FW_REL_OK);
            return false;
        }
        const uint32_t want = (total - done < r->io_cap) ? total - done
                                                         : r->io_cap;
        const int32_t got = h->read(h->ctx, r->io, want, op_ms(now_left));
        if (got < 0) {
            h->close(h->ctx);
            failed(r, out, WA_GOT_CONNECT, FW_REL_OK);
            return false;
        }
        if (got == 0 && chunked && done > 0u) {
            break;                       /* its last chunk */
        }
        if (got == 0) {
            h->close(h->ctx);
            res(out, WA_GOT_UNEXPECTED, chunked ? FW_REL_EMPTY
                                                : FW_REL_SHORT);
            return false;
        }
        if ((uint32_t)got > want) {
            h->close(h->ctx);
            res(out, WA_GOT_UNEXPECTED, FW_REL_OVERRUN);
            return false;
        }
        done += (uint32_t)got;
        ended = fw_release_scan_feed(&scan, r->io, (size_t)got);
    }
    h->close(h->ctx);
    if (!ended && chunked && done >= total) {
        /* FW_REL_SCAN_MAX bytes in chunks without the releases' end. */
        res(out, WA_GOT_UNEXPECTED, FW_REL_SCAN_LONG);
        return false;
    }
    const fw_rel_err_t e = fw_release_scan_end(&scan, v);
    if (e != FW_REL_OK) {
        res(out, WA_GOT_UNEXPECTED, e);
        return false;
    }
    *have_line = fw_release_scan_line(&scan, line);
    return true;
}

/**
 * @brief TESTING: the 10 newest releases by date, their highest offered,
 *        or in its place the highest of the panel's release line when that
 *        one is offered.
 * @param r    the run
 * @param left ms of the budget left
 * @param out  the result
 */
static void check_api(const wa_run_t *r, uint32_t left, wa_fetch_res_t *out)
{
    fw_version_t v, line;
    bool have_line = false;
    if (!api_scan(r, left, out, &v, &have_line, &line)) {
        return;
    }
    /* A patch release of the panel's line, beside a later release for
     * another panel, is the one offered. */
    if (have_line && fw_release_offered(line, r->running, r->running_ok)) {
        v = line;
    }
    found(r, out, v);
}

void wa_run_check(const wa_run_t *r, wa_fetch_res_t *out)
{
    memset(out, 0, sizeof(*out));
    report(r, WA_PH_ASKING, 0u, 0u);
    const wa_http_t *h = r->http;
    if (r->stopped(r->ctx)) {
        res(out, WA_GOT_ABORTED, FW_REL_OK);
        return;
    }
    const uint32_t left = run_left(r);
    if (left == 0u) {
        res(out, WA_GOT_TIMEOUT, FW_REL_OK);
        return;
    }
    if (r->channel == WA_CH_TESTING) {
        check_api(r, left, out);
        return;
    }
    wa_http_head_t head = { .status = 0, .length = -1 };
    const bool open = h->open(h->ctx, FW_REL_LATEST_URL, op_ms(left), false,
                              &head, r->loc, FW_REL_URL_MAX);
    /* Only the head is read: the body of a redirect is not wanted. */
    h->close(h->ctx);
    if (!open) {
        failed(r, out, WA_GOT_CONNECT, FW_REL_OK);
        return;
    }
    if (fw_release_status(head.status) != FW_REL_REDIRECT) {
        out->status = head.status;
        res(out, WA_GOT_REFUSED, FW_REL_STATUS);
        return;
    }
    const size_t loc_n = (head.loc_n > FW_REL_URL_MAX) ? FW_REL_URL_MAX
                                                       : head.loc_n;
    fw_version_t v;
    const fw_rel_err_t e = fw_release_parse_tag(r->loc, loc_n, head.loc_cut,
                                                &v);
    if (e != FW_REL_OK) {
        res(out, WA_GOT_UNEXPECTED, e);
        return;
    }
    /* GitHub's latest release is past the panel's line and newer than the
     * running build: one request of the release list for the line's
     * newest, offered when it is newer.  A list that fails or holds none
     * leaves the latest release's result. */
    if (r->running_ok && !fw_release_in_line(v)
        && fw_version_cmp(v, r->running) > 0) {
        const uint32_t now_left = run_left(r);
        wa_fetch_res_t scan_out;
        memset(&scan_out, 0, sizeof(scan_out));
        fw_version_t top, line;
        bool have_line = false;
        if (now_left > 0u
            && api_scan(r, now_left, &scan_out, &top, &have_line, &line)
            && have_line && fw_release_offered(line, r->running, true)) {
            v = line;
        }
    }
    found(r, out, v);
}

/* SHA256SUMS into r->sums. */
typedef struct {
    uint32_t n;
} sums_t;

static bool sums_begin(const wa_run_t *r, void *ctx, uint32_t bytes,
                       wa_fetch_res_t *out)
{
    (void)r;
    (void)bytes;
    (void)out;
    ((sums_t *)ctx)->n = 0u;
    return true;
}

static void sums_add(const wa_run_t *r, void *ctx, const uint8_t *d,
                     uint32_t n, uint32_t done, uint32_t total)
{
    (void)total;
    sums_t *s = ctx;
    /* fetch() never hands over more than the cap of FW_REL_SUMS_MAX. */
    memcpy(r->sums + s->n, d, n);
    s->n = done;
}

/* The image into r->up. */
static bool image_begin(const wa_run_t *r, void *ctx, uint32_t bytes,
                        wa_fetch_res_t *out)
{
    (void)ctx;
    uint8_t *buf = r->alloc(r->ctx, bytes);
    if (buf == NULL) {
        res(out, WA_GOT_MEMORY, FW_REL_OK);
        return false;
    }
    fw_upload_init(r->up, buf, bytes, r->slot);
    const fw_err_t e = fw_upload_begin(r->up, bytes);
    if (e != FW_OK) {
        out->img = e;
        res(out, WA_GOT_IMAGE, FW_REL_OK);
        return false;
    }
    report(r, WA_PH_IMAGE, 0u, bytes);
    return true;
}

static void image_add(const wa_run_t *r, void *ctx, const uint8_t *d,
                      uint32_t n, uint32_t done, uint32_t total)
{
    (void)ctx;
    /* Within the announced size: fetch() reads no more than it. */
    (void)fw_upload_add(r->up, d, n);
    report(r, WA_PH_IMAGE, done, total);
}

void wa_run_download(const wa_run_t *r, fw_version_t v, wa_fetch_res_t *out)
{
    memset(out, 0, sizeof(*out));
    out->found = v;
    report(r, WA_PH_SUMS, 0u, 0u);

    char name[FW_REL_NAME_MAX];
    fw_release_image_name(v, name);
    if (!fw_release_url(v, FW_REL_SUMS_NAME, r->url, FW_REL_URL_MAX + 1u)) {
        res(out, WA_GOT_UNEXPECTED, FW_REL_LONG);
        return;
    }
    sums_t sums = { 0u };
    const sink_t ks = { sums_begin, sums_add, &sums, FW_REL_SUMS_MAX,
                        WA_GOT_SUMS };
    if (!fetch(r, &ks, out)) {
        return;
    }
    uint8_t want[FW_SHA256_BYTES];
    const fw_rel_err_t se = fw_release_parse_sums(r->sums, sums.n, name,
                                                  want);
    if (se != FW_REL_OK) {
        res(out, WA_GOT_SUMS, se);
        return;
    }

    report(r, WA_PH_IMAGE, 0u, 0u);
    if (!fw_release_url(v, name, r->url, FW_REL_URL_MAX + 1u)) {
        res(out, WA_GOT_UNEXPECTED, FW_REL_LONG);
        return;
    }
    const sink_t ki = { image_begin, image_add, NULL, r->slot,
                        WA_GOT_UNEXPECTED };
    if (!fetch(r, &ki, out)) {
        if (r->up->state == FW_UP_RECEIVING) {
            (void)fw_upload_end(r->up, false);
        }
        return;
    }

    report(r, WA_PH_CHECKING, r->up->done, r->up->bytes);
    uint8_t got[FW_SHA256_BYTES];
    fw_sha256(r->up->buf, r->up->done, got);
    if (memcmp(got, want, sizeof(got)) != 0) {
        (void)fw_upload_end(r->up, false);
        res(out, WA_GOT_HASH, FW_REL_OK);
        return;
    }
    const fw_err_t e = fw_upload_end(r->up, true);
    if (e != FW_OK) {
        out->img = e;
        res(out, WA_GOT_IMAGE, FW_REL_OK);
        return;
    }
    if (fw_version_cmp(r->up->info.version, v) != 0) {
        /* A file under this release's name that says it is another. */
        fw_upload_discard(r->up);
        res(out, WA_GOT_UNEXPECTED, FW_REL_VERSION);
        return;
    }
    if (r->stopped(r->ctx) || run_left(r) == 0u) {
        /* Ended, or past the budget, while the last bytes came in or the
         * checks ran: the job is over, the image is not offered. */
        fw_upload_discard(r->up);
        failed(r, out, WA_GOT_TIMEOUT, FW_REL_OK);
        return;
    }
    res(out, WA_GOT_STAGED, FW_REL_OK);
}
