/*
 * The IOmcu's 3s console report, one line per pass (report_steps.h), over
 * a transport and a snapshot source this suite controls: a host that is
 * there or not, a room that is set per pass, and counts of every snapshot
 * and every line.
 *
 * At the limits: the first pass, 2999ms and 3000ms after a start, across
 * the wrap of the 32-bit millisecond clock, a room of 255 and 256 bytes, a
 * report of 10 lines and one of 5.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "report_steps.h"

typedef struct {
    bool     host;
    size_t   room;
    unsigned snaps;
    unsigned puts;
    char     text[4096];
    size_t   at;
    report_snap_t base;
} rig_t;

static rig_t R;

static bool r_host(void *ctx) { (void)ctx; return R.host; }
static size_t r_room(void *ctx) { (void)ctx; return R.room; }
static void r_snap(void *ctx, report_snap_t *s)
{
    (void)ctx;
    ++R.snaps;
    *s = R.base;
    s->requests = R.snaps;           /* each snapshot is told apart */
}
static void r_put(void *ctx, const char *line, size_t n)
{
    (void)ctx;
    ++R.puts;
    if (R.at + n < sizeof(R.text)) {
        memcpy(R.text + R.at, line, n);
        R.at += n;
        R.text[R.at] = '\0';
    }
}

static const report_io_t IO = { r_host, r_room, r_snap, r_put, NULL };

/* Every line applies but CAN_DOWN: 10 lines. */
static void rig_full(void)
{
    memset(&R, 0, sizeof(R));
    R.host = true;
    R.room = 2048u;
    R.base.store_off = true;
    R.base.slot = 0;
    R.base.reset = "r";
    R.base.boot = "b";
    R.base.can_up = true;
    R.base.overruns = 1u;
    R.base.sense_on = true;
}

/* Five lines: slot, reset, boot, CAN up, loop. */
static void rig_short(void)
{
    rig_full();
    R.base.store_off = false;
    R.base.overruns = 0u;
    R.base.sense_on = false;
}

/* Passes until the report has no lines left; returns the passes run. */
static unsigned run_report(report_steps_t *r, uint32_t now)
{
    unsigned passes = 0;
    do {
        (void)report_steps_pass(r, &IO, now);
        ++passes;
    } while (r->running && passes < 100u);
    return passes;
}

TEST_CASE(the_first_report_is_at_once_then_every_3000ms)
{
    CHECK_EQ(REPORT_PERIOD_MS, 3000);
    static const uint32_t starts[] = { 0u, 12345u, 0xFFFFFFFFu - 1000u };
    for (unsigned k = 0; k < 3u; ++k) {
        rig_short();
        report_steps_t r;
        report_steps_init(&r);
        const uint32_t t0 = starts[k];
        CHECK(report_steps_pass(&r, &IO, t0));      /* the first pass */
        CHECK_EQ(R.snaps, 1);
        (void)run_report(&r, t0 + 1u);
        CHECK_EQ(R.snaps, 1);
        for (uint32_t t = t0 + 2u; t != t0 + 3000u; t += 1u) {
            if (report_steps_pass(&r, &IO, t)) {
                T_FAIL("start %lu: a line at +%lu", (unsigned long)t0,
                       (unsigned long)(t - t0));
                break;
            }
        }
        CHECK_EQ(R.snaps, 1);                        /* not at 2999ms */
        CHECK(report_steps_pass(&r, &IO, t0 + 3000u));
        CHECK_EQ(R.snaps, 2);                        /* at 3000ms */
        (void)run_report(&r, t0 + 3001u);
        CHECK(!report_steps_pass(&r, &IO, t0 + 5999u));
        CHECK(report_steps_pass(&r, &IO, t0 + 6000u));
        CHECK_EQ(R.snaps, 3);
    }
}

TEST_CASE(one_line_per_pass)
{
    rig_full();
    report_steps_t r;
    report_steps_init(&r);
    unsigned lines = 0;
    for (uint32_t t = 0; t < 2000u; ++t) {
        const unsigned before = R.puts;
        if (report_steps_pass(&r, &IO, t)) {
            ++lines;
        }
        if (R.puts - before > 1u) {
            T_FAIL("pass %lu handed out %u lines", (unsigned long)t,
                   R.puts - before);
        }
    }
    CHECK_EQ(lines, 10);
    CHECK_EQ(R.puts, 10);
    /* The 10 lines took 10 passes in a row. */
    rig_full();
    report_steps_init(&r);
    CHECK_EQ(run_report(&r, 0u), 10);
    rig_short();
    report_steps_init(&r);
    CHECK_EQ(run_report(&r, 0u), 5);
    CHECK_EQ(R.puts, 5);
}

TEST_CASE(no_line_without_256_bytes_of_room)
{
    rig_short();
    report_steps_t r;
    report_steps_init(&r);
    R.room = 255u;
    for (uint32_t t = 0; t < 50u; ++t) {
        CHECK(!report_steps_pass(&r, &IO, t));
    }
    CHECK_EQ(R.puts, 0);
    CHECK_EQ(R.snaps, 1);        /* taken at the due pass, kept */
    CHECK(r.running);
    R.room = 256u;
    CHECK(report_steps_pass(&r, &IO, 50u));
    CHECK_EQ(R.puts, 1);
    CHECK(R.at > 0u);
    CHECK(strncmp(R.text, "rcbench-iomcu: running from slot A\n",
                  R.at) == 0);
}

TEST_CASE(no_host_no_snapshot_no_line)
{
    rig_full();
    R.host = false;
    report_steps_t r;
    report_steps_init(&r);
    for (uint32_t t = 0; t < 7000u; t += 10u) {
        CHECK(!report_steps_pass(&r, &IO, t));
    }
    CHECK_EQ(R.snaps, 0);
    CHECK_EQ(R.puts, 0);
    CHECK_EQ(r.skipped, 0);
    /* A host that comes takes the next report whole. */
    R.host = true;
    CHECK_EQ(run_report(&r, 9000u), 10);
    CHECK_EQ(R.snaps, 1);
}

TEST_CASE(an_unfinished_report_is_abandoned_at_the_next_due_time)
{
    rig_full();
    report_steps_t r;
    report_steps_init(&r);
    CHECK(report_steps_pass(&r, &IO, 0u));
    CHECK(report_steps_pass(&r, &IO, 1u));
    R.room = 0u;                             /* the host stops reading */
    for (uint32_t t = 2u; t < 3000u; ++t) {
        CHECK(!report_steps_pass(&r, &IO, t));
    }
    CHECK_EQ(R.puts, 2);
    CHECK_EQ(r.skipped, 0);
    R.room = 2048u;
    CHECK(report_steps_pass(&r, &IO, 3000u));
    CHECK_EQ(r.skipped, 1);
    CHECK_EQ(R.snaps, 2);
    /* The new report starts at its first line, from the new snapshot. */
    CHECK_EQ(r.next, 1);
    CHECK_EQ(r.snap.requests, 2);
    /* And an abandoned report with no host after it is counted too. */
    R.room = 0u;
    R.host = false;
    (void)report_steps_pass(&r, &IO, 6000u);
    CHECK_EQ(r.skipped, 2);
    CHECK(!r.running);
}

TEST_CASE(lines_that_do_not_apply_cost_no_pass)
{
    rig_short();
    report_steps_t r;
    report_steps_init(&r);
    /* Slot, reset, boot, CAN up, then the loop line in the fifth pass:
     * STORE_OFF before them and CAN_DOWN, OVERRUN and the three sense
     * lines between them are passed over. */
    for (unsigned p = 0; p < 5u; ++p) {
        if (!report_steps_pass(&r, &IO, p)) {
            T_FAIL("pass %u handed out no line", p);
        }
    }
    CHECK(!r.running);
    CHECK_EQ(R.puts, 5);
    CHECK(strstr(R.text, "loop max") != NULL);
}

TEST_CASE(the_snapshot_is_taken_once_per_report)
{
    rig_full();
    report_steps_t r;
    report_steps_init(&r);
    for (uint32_t t = 0; t < 10u; ++t) {
        (void)report_steps_pass(&r, &IO, t);
        CHECK_EQ(R.snaps, 1);
    }
    /* Every line came from the first snapshot: the CAN line says 1. */
    CHECK(strstr(R.text, ", 1 requests served") != NULL);
    for (uint32_t t = 10u; t < 3000u; ++t) {
        (void)report_steps_pass(&r, &IO, t);
    }
    CHECK_EQ(R.snaps, 1);
}

int main(void)
{
    RUN(the_first_report_is_at_once_then_every_3000ms);
    RUN(one_line_per_pass);
    RUN(no_line_without_256_bytes_of_room);
    RUN(no_host_no_snapshot_no_line);
    RUN(an_unfinished_report_is_abandoned_at_the_next_due_time);
    RUN(lines_that_do_not_apply_cost_no_pass);
    RUN(the_snapshot_is_taken_once_per_report);
    return test_summary("report_steps");
}
