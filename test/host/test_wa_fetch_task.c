/*
 * A release job's task that outlives the radio.
 *
 * The radio goes off without waiting for a task still in a name lookup:
 * lwIP gives up on one only after about 7s a DNS (Domain Name System)
 * server.  The failures under test: a new job that starts while that task
 * lives, a session that waits for it, and the task's late report landing on
 * the next job.  The HTTP (hypertext transfer protocol) client here is
 * scripted: its open() runs what the other tasks do while it waits; the
 * clock is the test's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "wa_fetch.h"

static const fw_version_t V20 = { 0u, 20u, 0u };

/* ------------------------------------------------- the scripted client */

static struct {
    uint32_t now;
    bool     stop;          /* the platform's fetch_stop                  */
    unsigned opens, closes;
    void   (*during_open)(void);
} g;

/**
 * @brief A request whose name lookup fails: what the other tasks do while
 *        it waits runs first.
 * @return false, no connection
 */
static bool f_open(void *ctx, const char *url, uint32_t timeout_ms,
                   bool api, wa_http_head_t *head, char *loc, size_t loc_cap)
{
    (void)ctx;
    (void)url;
    (void)timeout_ms;
    (void)api;
    (void)head;
    (void)loc;
    (void)loc_cap;
    ++g.opens;
    if (g.during_open != NULL) {
        g.during_open();
    }
    return false;
}

/** @brief Never reached: no request opens. @return -1 */
static int32_t f_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    (void)ctx;
    (void)dst;
    (void)n;
    (void)timeout_ms;
    return -1;
}

/** @brief Counts the closes. */
static void f_close(void *ctx)
{
    (void)ctx;
    ++g.closes;
}

/** @brief The test's clock. @return ms */
static uint32_t f_now(void *ctx)
{
    (void)ctx;
    return g.now;
}

/** @brief The platform's fetch_stop. @return whether it is set */
static bool f_stopped(void *ctx)
{
    (void)ctx;
    return g.stop;
}

static const wa_http_t k_http = { f_open, f_read, f_close, NULL };

static char    g_url[FW_REL_URL_MAX + 1u];
static char    g_loc[FW_REL_URL_MAX + 1u];
static uint8_t g_io[4096];

/** @brief A CHECK's runner from @p start. @return the runner */
static wa_run_t run_for(uint32_t start)
{
    wa_run_t r;
    memset(&r, 0, sizeof(r));
    r.http = &k_http;
    r.now = f_now;
    r.stopped = f_stopped;
    r.start_ms = start;
    r.budget_ms = WA_FETCH_CHECK_MS;
    r.running = (fw_version_t){ 0u, 22u, 0u };
    r.running_ok = true;
    r.url = g_url;
    r.loc = g_loc;
    r.io = g_io;
    r.io_cap = sizeof(g_io);
    return r;
}

/** @brief A fixed source of the session's secrets. @return a constant */
static uint32_t rand_k(void *ctx)
{
    (void)ctx;
    return 0x12345678u;
}

/** @brief The release check's session, joined. @return the session */
static wa_t joined(void)
{
    wa_t w;
    wa_init(&w);
    w.mode = WA_MODE_FETCH;
    w.state = WA_ST_JOINED;
    return w;
}

/** @brief The platform's "another session has the radio". @return it */
static bool other_session(const wa_t *w)
{
    return wa_radio(w) && w->mode != WA_MODE_FETCH;
}

/* ------------------------------------------------------------- the gate */

TEST_CASE(a_start_waits_for_the_last_jobs_task_and_names_the_gate_first)
{
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, false, false, false),
             WA_WHY_NONE);
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, true, false, false),
             WA_WHY_BUSY);
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, false, true, false),
             WA_WHY_BUSY);
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, false, false, true),
             WA_WHY_BUSY);
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, true, true, true),
             WA_WHY_BUSY);
    /* The bench's reason comes first, every other input at either value. */
    const wa_why_t gates[] = { WA_WHY_ARMED, WA_WHY_SUPPLY, WA_WHY_RUN,
                               WA_WHY_STOP, WA_WHY_INSTALL, WA_WHY_COPROC,
                               WA_WHY_DRAWING };
    for (size_t i = 0; i < sizeof(gates) / sizeof(gates[0]); ++i) {
        for (unsigned m = 0u; m < 8u; ++m) {
            CHECK_EQ(wa_fetch_start_gate(gates[i], (m & 1u) != 0u,
                                         (m & 2u) != 0u, (m & 4u) != 0u),
                     gates[i]);
        }
    }
}

/* ------------------------------------------- the lookup at radio off */

/*
 * The radio goes off while the runner waits in open() on a name lookup
 * (ARM during the start-up check): the job ends at once, the radio is not
 * held for the task, and while the task lives a new job is refused and
 * WI-FI's session starts.  The task's late end touches neither; the next
 * job starts once it has ended.
 */
static wa_fetch_t g_f;
static wa_t       g_w;
static wa_got_t   g_refused;
static wa_off_t   g_session;

/** @brief The other tasks while the lookup waits, as on the station. */
static void arm_during_lookup(void)
{
    /* The UI loop: ARMED ends the job, and the radio goes off for it. */
    wa_fetch_tick(&g_f, g.now, WA_WHY_ARMED, &g_w);
    CHECK_EQ(g_f.got, WA_GOT_ABORTED);
    CHECK(wa_fetch_take_off(&g_f));
    g.stop = true;
    g.now += 1550u;                        /* the station dropped */
    /* The bench back SAFE, the task still in its lookup: no job starts. */
    g_refused = wa_fetch_start(&g_f, WA_JOB_CHECK, V20, WA_CH_STABLE, true,
                               wa_fetch_start_gate(WA_WHY_NONE, false, true,
                                                   other_session(&g_w)),
                               g.now);
    /* WI-FI's JOIN starts all the same. */
    wa_init(&g_w);
    g_session = wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, g.now, rand_k,
                         NULL);
    g.now += 10300u;                       /* lwIP gives up on the lookup */
}

TEST_CASE(a_lookup_pending_at_radio_off_holds_jobs_not_the_radio)
{
    memset(&g, 0, sizeof(g));
    g.now = 5000u;
    wa_fetch_init(&g_f, (fw_version_t){ 0u, 22u, 0u });
    CHECK_EQ(wa_fetch_start(&g_f, WA_JOB_CHECK, V20, WA_CH_STABLE, true,
                            WA_WHY_NONE, g.now), WA_GOT_NONE);
    g_w = joined();
    wa_fetch_tick(&g_f, g.now, WA_WHY_NONE, &g_w);
    CHECK(wa_fetch_take_run(&g_f));
    const uint32_t first = g_f.gen;
    g.during_open = arm_during_lookup;
    const wa_run_t r = run_for(5000u);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    g.during_open = NULL;
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.closes, 1u);
    /* The task's own end: stopped, and its report goes nowhere. */
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    wa_fetch_on_end(&g_f, first, &out);
    CHECK_EQ(g_refused, WA_GOT_BLOCKED);
    CHECK_EQ(g_f.got, WA_GOT_BLOCKED);
    CHECK_EQ(g_f.why, WA_WHY_BUSY);
    CHECK(g_f.gen != first);
    CHECK(!wa_fetch_take_off(&g_f));       /* the refusal owes no radio */
    CHECK_EQ(g_session, WA_OFF_NONE);
    CHECK(wa_radio(&g_w));
    CHECK_EQ(g_w.mode, WA_MODE_JOIN);
    /* The task has ended; WI-FI's session holds the radio: still none. */
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, false, false,
                                 other_session(&g_w)), WA_WHY_BUSY);
    /* The session closed: the next job starts. */
    wa_close(&g_w, WA_OFF_ASKED);
    CHECK_EQ(wa_fetch_start(&g_f, WA_JOB_CHECK, V20, WA_CH_STABLE, true,
                            wa_fetch_start_gate(WA_WHY_NONE, false, false,
                                                other_session(&g_w)),
                            g.now), WA_GOT_NONE);
    CHECK_EQ(g_f.phase, WA_PH_JOINING);
    /* A report of the first job's task after all of it is dropped. */
    const wa_fetch_res_t late = { .got = WA_GOT_NEWER, .found = V20 };
    wa_fetch_on_end(&g_f, first, &late);
    wa_fetch_on_phase(&g_f, first, WA_PH_ASKING);
    CHECK_EQ(g_f.phase, WA_PH_JOINING);
    CHECK_EQ(g_f.got, WA_GOT_NONE);
}

/*
 * The same lookup when nothing else happens until it ends: the radio was
 * off from the abort on, the job stays ended as ARMED, and a start right
 * after the task's end is the first that is not refused.
 */
TEST_CASE(the_first_start_after_the_tasks_end_is_taken)
{
    memset(&g, 0, sizeof(g));
    g.now = 0xFFFFF000u;                   /* across the wrap */
    wa_fetch_init(&g_f, (fw_version_t){ 0u, 22u, 0u });
    (void)wa_fetch_start(&g_f, WA_JOB_CHECK, V20, WA_CH_STABLE, true,
                         WA_WHY_NONE, g.now);
    g_w = joined();
    wa_fetch_tick(&g_f, g.now, WA_WHY_NONE, &g_w);
    CHECK(wa_fetch_take_run(&g_f));
    const uint32_t first = g_f.gen;
    wa_fetch_abort(&g_f, WA_WHY_ARMED);
    g.stop = true;
    g.now += 7000u;
    const wa_run_t r = run_for(g.now - 7000u);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    wa_fetch_on_end(&g_f, first, &out);
    CHECK_EQ(g_f.got, WA_GOT_ABORTED);
    CHECK_EQ(g_f.why, WA_WHY_ARMED);
    CHECK_EQ(g_f.gen, first);
    /* One frame before the end it is still refused; at the end, taken. */
    wa_init(&g_w);
    CHECK_EQ(wa_fetch_start_gate(WA_WHY_NONE, false, true,
                                 other_session(&g_w)), WA_WHY_BUSY);
    CHECK_EQ(wa_fetch_start(&g_f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true,
                            wa_fetch_start_gate(WA_WHY_NONE, false, false,
                                                other_session(&g_w)),
                            g.now), WA_GOT_NONE);
    CHECK_EQ(g_f.gen, first + 1u);
    CHECK(wa_fetch_busy(&g_f));
}

int main(void)
{
    RUN(a_start_waits_for_the_last_jobs_task_and_names_the_gate_first);
    RUN(a_lookup_pending_at_radio_off_holds_jobs_not_the_radio);
    RUN(the_first_start_after_the_tasks_end_is_taken);
    return test_summary("wa_fetch_task");
}
