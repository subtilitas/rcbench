/**
 * @file test_prn_job.c
 * @brief The engine's job on what the T50M Pro file does not use, with the
 *        suites' printer files (fixtures/printers): writes without
 *        response, a step done on its write, a wait step, a delay, a poll
 *        on two conditions with a saved variable, a check, row frames, a
 *        notify joiner; a length joiner with a tail and a checksum, chunks
 *        without pad, a relink to a step and to the verdict, the verdicts
 *        "done" and "result"; the abort range; the start's refusals; a
 *        value too large for its encoding; the 120s limit.
 *
 * test_prn_job_vs_sv.c holds the job to sv_job with the T50M Pro file.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "prn_dual.h"
#include "prn_fixture.h"
#include "prn_job.h"

static prn_spec_t s_rows;
static prn_spec_t s_magic;
static prn_job_t s_j;
static uint32_t s_now;

/** @brief The last write handed out. */
static struct {
    uint8_t b[PRN_FRAME_MAX];
    uint16_t n;
    prn_write_t kind;
    uint32_t t;
} s_w;

/**
 * @brief Read a fixture printer file.
 * @param name its name under fixtures/printers
 * @param s    the spec
 * @return true when it parsed
 */
static bool load(const char *name, prn_spec_t *s)
{
    static char buf[PRN_FILE_MAX + 1u];
    char path[96];
    strcpy(path, "fixtures/printers/");
    strcat(path, name);
    const size_t n = prn_read(path, buf, sizeof buf);
    prn_perr_t e;
    return prn_parse(buf, n, name, s, &e);
}

/**
 * @brief Run the clock until the job hands out a write, at most @p ms.
 * @param ms the time
 * @return true when it did
 */
static bool next_write(uint32_t ms)
{
    for (uint32_t i = 0; i <= ms; ++i) {
        const uint8_t *p = NULL;
        uint16_t n = 0u;
        prn_write_t k = PRN_W_REQ;
        if (prn_job_tx(&s_j, s_now, &p, &n, &k)) {
            memcpy(s_w.b, p, n);
            s_w.n = n;
            s_w.kind = k;
            s_w.t = s_now;
            return true;
        }
        if (prn_job_ended(&s_j)) {
            return false;
        }
        s_now++;
    }
    return false;
}

/**
 * @brief Run the clock until the job ends, at most @p ms, writing
 *        nothing.
 * @param ms the time
 * @return true when it ended
 */
static bool run_out(uint32_t ms)
{
    for (uint32_t i = 0; i <= ms && !prn_job_ended(&s_j); ++i) {
        prn_job_tick(&s_j, s_now);
        if (!prn_job_ended(&s_j)) {
            s_now++;
        }
    }
    return prn_job_ended(&s_j);
}

/* -------------------------------------------------------- rows-test */

/** @brief 3 head lines of 12 bytes. */
static uint8_t s_lines[36];

/**
 * @brief Start a rows-test PRINT at @p t.
 * @param t the clock
 * @return prn_job_start()'s answer
 */
static bool rows_start(uint32_t t)
{
    for (unsigned i = 0; i < sizeof s_lines; ++i) {
        s_lines[i] = (uint8_t)(i + 1u);
    }
    const prn_stream_t z = { s_lines, sizeof s_lines, 0u, 3u, 12u, 30u };
    s_now = t;
    return prn_job_start(&s_j, &s_rows, PRN_JOB_PRINT, &z, t);
}

/**
 * @brief An answer of rows-test: 1A (3 bytes) or 1B (6 bytes).
 * @param key  the key
 * @param b1   byte 1
 * @param b2   byte 2
 * @param b3   byte 3
 */
static void rows_answer(uint8_t key, uint8_t b1, uint8_t b2, uint8_t b3)
{
    const uint8_t f[6] = { key, b1, b2, b3, 0u, 0u };
    prn_job_rx(&s_j, f, (key == 0x1Au) ? 3u : 6u, s_now);
}

/**
 * @brief Run rows-test from its start through the data step: go (done on
 *        its write), state (answered: battery 5), 3 row frames.
 * @return true when the job reached the wait step
 */
static bool rows_through_data(void)
{
    if (!next_write(10u)) {
        return false;
    }
    prn_job_written(&s_j, true, s_now);
    if (!next_write(10u)) {
        return false;
    }
    prn_job_written(&s_j, true, s_now);
    rows_answer(0x1Au, 0x00u, 0x05u, 0u);
    for (unsigned i = 0; i < 3u; ++i) {
        if (!next_write(20u) || s_w.n != 19u) {
            return false;
        }
        prn_job_written(&s_j, true, s_now);
    }
    return s_j.phase == PRN_PH_STEP && s_j.pos == 3u;
}

TEST_CASE(a_rows_print_runs_every_step_without_response)
{
    CHECK(rows_start(100u));
    /* go: 55 55 01, density 3, speed (stream_len 36: else 3) as be16, the
     * CRC-8 of bytes 0 to the one before it; done on its write */
    CHECK(next_write(0u));
    CHECK_EQ(s_w.kind, PRN_W_NR);
    CHECK_EQ(s_w.n, 7u);
    const uint8_t go[6] = { 0x55u, 0x55u, 0x01u, 0x03u, 0x00u, 0x03u };
    CHECK(memcmp(s_w.b, go, 6u) == 0);
    CHECK_EQ(s_w.b[6], prn_ck(PRN_CK_CRC8, go, 6u));
    prn_job_written(&s_j, true, s_now);
    CHECK_EQ(s_j.pos, 1u);
    /* state: answered with battery 5, saved as v0 */
    CHECK(next_write(0u));
    CHECK_EQ(s_w.b[2], 0x02u);
    prn_job_written(&s_j, true, s_now);
    rows_answer(0x1Au, 0x00u, 0x05u, 0u);
    CHECK_EQ(s_j.pos, 2u);
    uint64_t v = 0u;
    CHECK(prn_job_field(&s_j, PRN_F_BATTERY, &v));
    CHECK_EQ(v, 5u);
    CHECK(!prn_job_field(&s_j, PRN_F_LEFT, &v));
    CHECK(!prn_job_field(&s_j, 200u, &v));
    /* 3 row frames, one write each, 5ms apart after each is taken */
    uint32_t last = 0u;
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK(next_write(20u));
        CHECK_EQ(s_w.n, 19u);
        CHECK_EQ(s_w.kind, PRN_W_NR);
        CHECK_EQ(s_w.b[4], i);
        CHECK(memcmp(s_w.b + 6, s_lines + 12u * i, 12u) == 0);
        if (i > 0u) {
            CHECK_EQ(s_w.t - last, 5u);
        }
        last = s_w.t;
        prn_job_written(&s_j, true, s_now);
    }
    /* settle 50ms, then end after its 10ms delay */
    CHECK(next_write(100u));
    CHECK_EQ(s_w.t - last, 60u);
    CHECK_EQ(s_w.b[2], 0xF3u);
    CHECK_EQ(s_w.b[3], 3u); /* lines, le32 */
    prn_job_written(&s_j, true, s_now);
    /* page 0, result 5: until fails (result = v0); polled again 50ms on */
    rows_answer(0x1Bu, 0x00u, 0x00u, 0x05u);
    CHECK(!prn_job_ended(&s_j));
    const uint32_t t0 = s_now;
    CHECK(next_write(100u));
    CHECK_EQ(s_w.t - t0, 50u);
    prn_job_written(&s_j, true, s_now);
    rows_answer(0x1Bu, 0x00u, 0x01u, 0x07u);
    CHECK(prn_job_ended(&s_j));
    CHECK_EQ(s_j.err, PRN_OK);
}

TEST_CASE(rows_errors_end_the_job_before_and_inside_the_abort_range)
{
    /* the cover flag before the range: the job ends, nothing more is sent */
    CHECK(rows_start(0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    rows_answer(0x1Au, 0x01u, 0x05u, 0u);
    CHECK(prn_job_ended(&s_j));
    CHECK_EQ(s_j.err, PRN_COVER);
    CHECK(!next_write(1000u));
    /* a battery of 0: the check's code */
    CHECK(rows_start(0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    rows_answer(0x1Au, 0x00u, 0x00u, 0u);
    CHECK_EQ(s_j.err, PRN_BATTERY);
    /* no answer to state: 3 tries 300ms apart, then SILENT */
    CHECK(rows_start(0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    unsigned tries = 0u;
    while (next_write(1000u)) {
        prn_job_written(&s_j, true, s_now);
        tries++;
    }
    CHECK_EQ(tries, 3u);
    CHECK_EQ(s_j.err, PRN_SILENT);
    /* a poll that never holds: 2s, then NOT_PRINTED through the abort
     * template once (the range runs from rows to end) */
    CHECK(rows_start(0u));
    CHECK(rows_through_data());
    unsigned aborts = 0u;
    while (next_write(3000u)) {
        prn_job_written(&s_j, true, s_now);
        if (s_w.b[2] == 0x09u) {
            aborts++;
        } else {
            rows_answer(0x1Bu, 0x00u, 0x00u, 0x05u);
        }
    }
    CHECK_EQ(aborts, 1u);
    CHECK_EQ(s_j.err, PRN_NOT_PRINTED);
}

TEST_CASE(rows_abort_link_and_refused_writes)
{
    /* an abort during the frames sends the abort template, after the
     * write in flight is taken */
    CHECK(rows_start(0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    rows_answer(0x1Au, 0x00u, 0x05u, 0u);
    CHECK(next_write(5u));
    prn_job_abort(&s_j, s_now);
    CHECK(!next_write(5u)); /* the frame is still out */
    prn_job_written(&s_j, true, s_now);
    CHECK(next_write(5u));
    CHECK_EQ(s_w.b[2], 0x09u);
    prn_job_abort(&s_j, s_now); /* a second abort changes nothing */
    prn_job_written(&s_j, true, s_now);
    CHECK(prn_job_ended(&s_j));
    CHECK_EQ(s_j.err, PRN_ABORTED);
    /* no relink line: a drop ends the job with LINK */
    CHECK(rows_start(0u));
    CHECK(rows_through_data());
    prn_job_link(&s_j, false, s_now);
    CHECK_EQ(s_j.err, PRN_LINK);
    /* a refused write */
    CHECK(rows_start(0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, false, s_now);
    CHECK_EQ(s_j.err, PRN_WRITE);
}

/* ------------------------------------------------------- magic-test */

static uint8_t s_stream[250];

/**
 * @brief Start a magic-test job at @p t.
 * @param kind QUERY or PRINT
 * @param t    the clock
 * @return prn_job_start()'s answer
 */
static bool magic_start(prn_job_kind_t kind, uint32_t t)
{
    for (unsigned i = 0; i < sizeof s_stream; ++i) {
        s_stream[i] = (uint8_t)(i * 3u);
    }
    const prn_stream_t z = { s_stream, sizeof s_stream, 2u, 30u, 20u, 0u };
    s_now = t;
    return prn_job_start(&s_j, &s_magic, kind,
                         (kind == PRN_JOB_PRINT) ? &z : NULL, t);
}

/**
 * @brief A magic-test answer: A5 5A, the length less 5 as be16, the key,
 *        the body, the sum of bytes 0 to the one before it, 0D.
 * @param key  the key
 * @param body the body
 * @param n    its length
 * @param bad  0 good, 1 a wrong checksum, 2 a wrong tail
 * @param out  the frame
 * @return its length
 */
static size_t magic_frame(uint8_t key, const uint8_t *body, size_t n,
                          unsigned bad, uint8_t *out)
{
    const size_t total = 5u + n + 2u;
    out[0] = 0xA5u;
    out[1] = 0x5Au;
    out[2] = 0u;
    out[3] = (uint8_t)(total - 5u);
    out[4] = key;
    memcpy(out + 5, body, n);
    out[total - 2u] = (uint8_t)(prn_ck(PRN_CK_SUM8, out, total - 2u)
                                + ((bad == 1u) ? 1u : 0u));
    out[total - 1u] = (bad == 2u) ? 0x0Au : 0x0Du;
    return total;
}

/**
 * @brief Answer hello: the roll, the counter and the UID.
 * @param w  width
 * @param h  height
 */
static void magic_hello(uint8_t w, uint8_t h)
{
    const uint8_t body[6] = { w, h, 0x2Cu, 0x00u, 0x12u, 0x34u };
    uint8_t f[32];
    const size_t n = magic_frame(0x01u, body, sizeof body, 0u, f);
    /* split, with a byte before the magic */
    const uint8_t junk = 0x00u;
    prn_job_rx(&s_j, &junk, 1u, s_now);
    prn_job_rx(&s_j, f, 3u, s_now);
    prn_job_rx(&s_j, f + 3, n - 3u, s_now);
}

/**
 * @brief Answer finish.
 * @param result the result byte (bit 7 busy)
 * @param bad    as magic_frame()
 */
static void magic_finish(uint8_t result, unsigned bad)
{
    uint8_t f[16];
    const size_t n = magic_frame(0x02u, &result, 1u, bad, f);
    prn_job_rx(&s_j, f, n, s_now);
}

/**
 * @brief Run a magic-test PRINT through its data: hello answered, every
 *        fragment taken.
 * @param frags set to the fragments written
 * @return true when the job is in finish
 */
static bool magic_through_data(unsigned *frags)
{
    if (!next_write(5u)) {
        return false;
    }
    prn_job_written(&s_j, true, s_now);
    magic_hello(20u, 30u);
    *frags = 0u;
    while (s_j.phase == PRN_PH_STEP && s_j.pos == 1u && next_write(50u)) {
        prn_job_written(&s_j, true, s_now);
        (*frags)++;
    }
    return s_j.phase == PRN_PH_STEP && s_j.pos == 2u;
}

TEST_CASE(a_magic_print_joins_checks_and_reads_the_result)
{
    CHECK(magic_start(PRN_JOB_PRINT, 1000u));
    CHECK(next_write(0u));
    /* hello: A5 01, stream_len le32 = 250, frames = 3, c0 not yet set */
    const uint8_t hello[7] = { 0xA5u, 0x01u, 0xFAu, 0x00u, 0x00u, 0x00u,
                               0x03u };
    CHECK_EQ(s_w.n, 8u);
    CHECK_EQ(s_w.kind, PRN_W_REQ);
    CHECK(memcmp(s_w.b, hello, 7u) == 0);
    CHECK_EQ(s_w.b[7], 0u);
    prn_job_written(&s_j, true, s_now);
    magic_hello(20u, 30u);
    CHECK_EQ(s_j.pos, 1u);
    /* chunk frames of 100, 100 and 50 bytes and 9 more, in writes of 64 */
    const uint16_t want[5] = { 64u, 45u, 64u, 45u, 59u };
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK(next_write(5u));
        CHECK_EQ(s_w.n, want[i]);
        if (i == 4u) {
            CHECK_EQ(s_w.b[1], 2u);   /* idx */
            CHECK_EQ(s_w.b[2], 3u);   /* total */
            CHECK_EQ(s_w.b[4], 59u);  /* len be16 */
            const uint32_t crc = prn_ck(PRN_CK_CRC32LE, s_w.b, 55u);
            CHECK_EQ(s_w.b[55], (uint8_t)crc);
            CHECK_EQ(s_w.b[58], (uint8_t)(crc >> 24));
        }
        prn_job_written(&s_j, true, s_now);
    }
    /* finish: a=2, p1 = speed (mean 125 a page: else 9), c0 = 0x2C */
    CHECK(next_write(5u));
    CHECK_EQ(s_w.b[1], 2u);
    CHECK_EQ(s_w.b[6], 9u);
    CHECK_EQ(s_w.b[7], 0x2Cu);
    prn_job_written(&s_j, true, s_now);
    /* a frame with a wrong sum and one with a wrong tail are dropped */
    magic_finish(0x05u, 1u);
    magic_finish(0x05u, 2u);
    CHECK_EQ(s_j.rx_bad, 2u);
    CHECK(!prn_job_ended(&s_j));
    /* busy, polled 30ms on; then idle with result 5: PRINTED */
    magic_finish(0x85u, 0u);
    CHECK(next_write(100u));
    prn_job_written(&s_j, true, s_now);
    magic_finish(0x05u, 0u);
    CHECK(prn_job_ended(&s_j));
    CHECK_EQ(s_j.err, PRN_OK);
    uint64_t uid = 0u;
    CHECK(prn_job_field(&s_j, PRN_F_UID, &uid));
    CHECK_EQ(uid, 0x1234u);
    /* result 3 is not one of 0 and 5 */
    unsigned frags = 0u;
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(magic_through_data(&frags));
    CHECK_EQ(frags, 5u);
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    magic_finish(0x03u, 0u);
    CHECK_EQ(s_j.err, PRN_NOT_PRINTED);
}

TEST_CASE(magic_rolls_against_the_limits_and_the_layout)
{
    const struct {
        prn_job_kind_t kind;
        uint8_t w, h;
        uint8_t err;
    } k[] = {
        { PRN_JOB_PRINT, 0u, 20u, PRN_NO_LABEL },
        { PRN_JOB_QUERY, 20u, 0u, PRN_NO_LABEL },
        { PRN_JOB_PRINT, 9u, 20u, PRN_SMALL },
        { PRN_JOB_PRINT, 26u, 20u, PRN_LARGE },
        { PRN_JOB_PRINT, 20u, 101u, PRN_LARGE },
        { PRN_JOB_PRINT, 25u, 20u, PRN_ROLL },
        { PRN_JOB_QUERY, 9u, 9u, PRN_OK },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        CHECK(magic_start(k[i].kind, 0u));
        CHECK(next_write(5u));
        prn_job_written(&s_j, true, s_now);
        magic_hello(k[i].w, k[i].h);
        CHECK(prn_job_ended(&s_j));
        CHECK_EQ(s_j.err, k[i].err);
    }
}

TEST_CASE(magic_relinks_to_its_step_and_to_the_verdict)
{
    /* a drop during the frames: 2s for the link, then the verdict, with
     * no result read: NOT_PRINTED, relinked */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    magic_hello(20u, 30u);
    CHECK(next_write(5u));
    prn_job_link(&s_j, false, s_now);
    CHECK_EQ(s_j.phase, PRN_PH_RELINK);
    prn_job_written(&s_j, true, s_now); /* an answer for a lost write */
    s_now += 1999u;
    prn_job_tick(&s_j, s_now);
    CHECK_EQ(s_j.phase, PRN_PH_RELINK);
    prn_job_link(&s_j, true, s_now);
    CHECK(prn_job_ended(&s_j));
    CHECK(s_j.relinked);
    CHECK_EQ(s_j.err, PRN_NOT_PRINTED);
    /* a drop in finish: back inside 2s, finish sends again at once */
    unsigned frags = 0u;
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(magic_through_data(&frags));
    CHECK(next_write(5u));
    prn_job_link(&s_j, false, s_now);
    s_now += 100u;
    prn_job_link(&s_j, true, s_now);
    CHECK_EQ(s_j.pos, 2u);
    CHECK(next_write(0u));
    prn_job_written(&s_j, true, s_now);
    magic_finish(0x00u, 0u);
    CHECK_EQ(s_j.err, PRN_OK);
    /* a second drop after the relink ends the job */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(magic_through_data(&frags));
    prn_job_link(&s_j, false, s_now);
    prn_job_link(&s_j, true, s_now);
    prn_job_link(&s_j, false, s_now);
    CHECK_EQ(s_j.err, PRN_LINK);
    /* the link never back: LINK after 2s */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(magic_through_data(&frags));
    prn_job_link(&s_j, false, s_now);
    const uint32_t t0 = s_now;
    CHECK(run_out(5000u));
    CHECK_EQ(s_j.err, PRN_LINK);
    CHECK_EQ(s_now - t0, 2000u);
    /* hello has a relink line but nothing was written yet: LINK at once */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    prn_job_link(&s_j, false, s_now);
    CHECK_EQ(s_j.err, PRN_LINK);
}

TEST_CASE(magic_answers_lost_and_the_query)
{
    /* hello's answer lost: the step's 400ms, then SILENT */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    const uint32_t t0 = s_now;
    CHECK(run_out(1000u));
    CHECK_EQ(s_j.err, PRN_SILENT);
    CHECK_EQ(s_now - t0, 400u);
    /* a write never taken: answer_ms 250 */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(next_write(5u));
    const uint32_t t1 = s_now;
    CHECK(run_out(1000u));
    CHECK_EQ(s_j.err, PRN_SILENT);
    CHECK_EQ(s_now - t1, 250u);
    /* a fragment never taken: the data step's 800ms */
    unsigned frags = 0u;
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    magic_hello(20u, 30u);
    CHECK(next_write(5u));
    const uint32_t t2 = s_now;
    CHECK(next_write(2000u)); /* the abort template, after 800ms */
    CHECK_EQ(s_now - t2, 800u);
    CHECK_EQ(s_w.b[1], 3u);
    prn_job_written(&s_j, true, s_now);
    CHECK_EQ(s_j.err, PRN_SILENT);
    /* a poll whose answer is lost: answer_ms, SILENT, the abort template */
    CHECK(magic_start(PRN_JOB_PRINT, 0u));
    CHECK(magic_through_data(&frags));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    CHECK(next_write(1000u));
    CHECK_EQ(s_w.b[1], 3u);
    prn_job_written(&s_j, true, s_now);
    CHECK_EQ(s_j.err, PRN_SILENT);
    /* the query: hello only */
    CHECK(magic_start(PRN_JOB_QUERY, 0u));
    CHECK(next_write(5u));
    prn_job_written(&s_j, true, s_now);
    magic_hello(20u, 30u);
    CHECK_EQ(s_j.err, PRN_OK);
}

/* ---------------------------------------------------------- general */

TEST_CASE(a_job_that_cannot_run_does_not_start)
{
    prn_spec_t t50;
    CHECK(prn_load_t50m(&t50, NULL, NULL));
    static uint8_t big[300u * 500u];
    prn_stream_t z = { big, 1000u, 2u, 0u, 0u, 0u };
    memset(&s_j, 0, sizeof s_j);
    CHECK(!prn_job_start(&s_j, NULL, PRN_JOB_QUERY, NULL, 0u));
    CHECK(!prn_job_start(&s_j, &t50, PRN_JOB_PRINT, NULL, 0u));
    z.data = NULL;
    CHECK(!prn_job_start(&s_j, &t50, PRN_JOB_PRINT, &z, 0u));
    z.data = big;
    z.len = 0u;
    CHECK(!prn_job_start(&s_j, &t50, PRN_JOB_PRINT, &z, 0u));
    z.len = 255u * 500u + 1u; /* 256 chunks */
    CHECK(!prn_job_start(&s_j, &t50, PRN_JOB_PRINT, &z, 0u));
    z.len = 1000u;
    z.pages = 0u; /* the speed table needs pages */
    CHECK(!prn_job_start(&s_j, &t50, PRN_JOB_PRINT, &z, 0u));
    CHECK_EQ(s_j.phase, PRN_PH_IDLE);
    prn_spec_t bad = t50;
    bad.nmatch = 0u;
    CHECK(!prn_job_start(&s_j, &bad, PRN_JOB_QUERY, NULL, 0u));
    /* rows not whole */
    const prn_stream_t r = { big, 35u, 0u, 3u, 0u, 0u };
    CHECK(!prn_job_start(&s_j, &s_rows, PRN_JOB_PRINT, &r, 0u));
    /* an idle job takes nothing and hands out nothing */
    memset(&s_j, 0, sizeof s_j);
    const uint8_t *p = NULL;
    uint16_t n = 0u;
    prn_write_t k;
    CHECK(!prn_job_tx(&s_j, 0u, &p, &n, &k));
    prn_job_tick(&s_j, 5000u);
    prn_job_abort(&s_j, 0u);
    prn_job_link(&s_j, false, 0u);
    prn_job_written(&s_j, true, 0u);
    prn_job_rx(&s_j, big, 4u, 0u);
    CHECK_EQ(s_j.phase, PRN_PH_IDLE);
    CHECK(!prn_job_ended(&s_j));
}

/** @brief The rig for the T50M Pro cases below, off the stack. */
static dual_t s_d;
/** @brief A stream over 65,535 bytes, 132 chunks and more. */
static uint8_t s_long[70000];

TEST_CASE(a_value_too_large_for_its_encoding_ends_the_job_with_engine)
{
    static prn_spec_t t50;
    CHECK(prn_load_t50m(&t50, NULL, NULL));
    /* BUF_FULL carries the stream's length in 16 bits: 70,000 bytes do
     * not fit, and STOP_PRINT goes out once START was sent */
    memset(&s_d, 0, sizeof s_d);
    s_d.prn = true;
    s_d.spec = &t50;
    dual_start(&s_d, SV_JOB_PRINT, 0u);
    s_d.z.data = s_long;
    s_d.z.len = sizeof s_long;
    dual_restart(&s_d, SV_JOB_PRINT, 0u);
    dual_run(&s_d, 60000u);
    CHECK(prn_job_ended(&s_d.pj));
    CHECK_EQ(s_d.pj.err, PRN_ENGINE);
    CHECK_EQ(sim_count(&s_d.sim, SV_C_STOP), 1u);
    CHECK_EQ(sim_count(&s_d.sim, SV_C_BUF_FULL), 0u);
}

/** @brief A printer with three polls of up to 50s each. */
static const char k_slow[] =
    "rcbench printer, format 1\n"
    "id slow\nname SLOW\nversion 1\nmatch name S\n"
    "service FF00\nchr cmd FF01 write\nmtu_min 23\nconn 6 6 0 10\n"
    "head_dots 80\ndots_mm 8\nline 10 lsb 1 no\n"
    "label feed top\nlabel dot0 left\nlabel align center\n"
    "label margin 0 0\nroll source fixed\nroll fixed 10 10 0\n"
    "roll limits 10 10 10 10\n"
    "chunk size 1\nchunk pad no\nchunk max 1\nchunk frame {data}\n"
    "write cmd req\nwrite data req 16 0\n"
    "tmpl t 01\nrx join notify 2 2\nrx key 0\nanswer 01 2\n"
    "field 01 busy 1 bit 0\n"
    "step a\n  send t\n  expect 01\n  poll 1000\n  until busy clear\n"
    "  within 50000\n"
    "step b\n  send t\n  expect 01\n  poll 1000\n  until busy clear\n"
    "  within 50000\n"
    "step c\n  send t\n  expect 01\n  poll 1000\n  until busy clear\n"
    "  within 50000\n"
    "step d\n  data\n  within 1000\n"
    "job query a\njob print a b c d\nverdict none\nend\n";

TEST_CASE(a_job_ends_within_120s)
{
    static prn_spec_t slow;
    prn_perr_t e;
    CHECK(prn_parse(k_slow, sizeof k_slow - 1u, "slow.prn", &slow, &e));
    const uint8_t one = 0xFFu;
    const prn_stream_t z = { &one, 1u, 0u, 1u, 0u, 0u };
    s_now = 0xFFFFF000u; /* across the clock's wrap */
    CHECK(prn_job_start(&s_j, &slow, PRN_JOB_PRINT, &z, s_now));
    const uint8_t busy[2] = { 0x01u, 0x01u };
    const uint8_t idle[2] = { 0x01u, 0x00u };
    unsigned writes = 0u;
    while (next_write(2000u)) {
        prn_job_written(&s_j, true, s_now);
        writes++;
        /* steps a and b end 48s in, step c would end 148s into the job */
        const bool done = s_j.pos < 2u && s_now - s_j.t_step >= 48000u;
        prn_job_rx(&s_j, done ? idle : busy, 2u, s_now);
    }
    CHECK(prn_job_ended(&s_j));
    CHECK_EQ(s_j.err, PRN_ENGINE);
    CHECK_EQ(s_now - 0xFFFFF000u, PRN_JOB_MS);
    CHECK(writes > 100u);
    /* a lone notification of the wrong length is no answer */
    CHECK(prn_job_start(&s_j, &slow, PRN_JOB_QUERY, NULL, 0u));
    s_now = 0u;
    CHECK(next_write(0u));
    prn_job_written(&s_j, true, s_now);
    const uint8_t three[3] = { 0x01u, 0x00u, 0x00u };
    prn_job_rx(&s_j, three, 3u, s_now);
    CHECK(!prn_job_ended(&s_j));
    prn_job_rx(&s_j, idle, 2u, s_now);
    CHECK_EQ(s_j.err, PRN_OK);
}

/**
 * @brief Start a T50M Pro PRINT and put it at position 5 (printing), armed,
 *        with the job's 120s passed.
 * @param s         the spec
 * @param in_flight a write of the step is out
 * @return true when the job started
 */
static bool t50m_armed_at_cap(const prn_spec_t *s, bool in_flight)
{
    static uint8_t stream[64];
    const prn_stream_t z = { stream, sizeof stream, 1u, 8u, 40u, 30u };
    s_now = 0xFFFFF000u;
    if (!prn_job_start(&s_j, s, PRN_JOB_PRINT, &z, s_now)) {
        return false;
    }
    s_j.phase = PRN_PH_STEP;
    s_j.pos = 5u;
    s_j.armed = true;
    s_j.want = false;
    s_j.await = false;
    s_j.in_flight = in_flight;
    s_j.out_kind = in_flight ? 1u : 0u; /* a command */
    s_j.t_sent = s_now;
    s_now += PRN_JOB_MS;
    return true;
}

/**
 * @brief Hand out the next write and hold it to the abort command.
 * @return true when the write is `cmd a=0x14`
 */
static bool abort_goes_out(void)
{
    const uint8_t *p = NULL;
    uint16_t n = 0u;
    prn_write_t k = PRN_W_REQ;
    if (!prn_job_tx(&s_j, s_now, &p, &n, &k) || s_j.phase != PRN_PH_STOP) {
        return false;
    }
    return p == s_j.cmd && memchr(p, 0x14, n) != NULL;
}

TEST_CASE(the_cap_in_the_abort_range_still_sends_the_abort)
{
    static prn_spec_t t50m;
    static char txt[PRN_FILE_MAX + 1u];
    size_t n = 0u;
    CHECK(prn_load_t50m(&t50m, txt, &n));
    /* the tick first, then the write */
    CHECK(t50m_armed_at_cap(&t50m, false));
    prn_job_tick(&s_j, s_now);
    CHECK(!prn_job_ended(&s_j));
    CHECK(abort_goes_out());
    prn_job_tick(&s_j, s_now);
    CHECK(!prn_job_ended(&s_j));
    prn_job_written(&s_j, true, s_now + 20u);
    CHECK(prn_job_ended(&s_j));
    CHECK_EQ(s_j.err, PRN_ENGINE);
    /* the write first, then the tick */
    CHECK(t50m_armed_at_cap(&t50m, false));
    CHECK(abort_goes_out()); /* prn_job_tx() ticks first */
    prn_job_tick(&s_j, s_now + 500u);
    CHECK(!prn_job_ended(&s_j));
    prn_job_written(&s_j, true, s_now);
    CHECK_EQ(s_j.err, PRN_ENGINE);
    /* a write in flight: the abort follows its answer */
    CHECK(t50m_armed_at_cap(&t50m, true));
    prn_job_tick(&s_j, s_now);
    CHECK_EQ(s_j.phase, PRN_PH_STOP);
    s_now += 999u;
    prn_job_tick(&s_j, s_now);
    prn_job_written(&s_j, true, s_now);
    CHECK(abort_goes_out());
    prn_job_written(&s_j, true, s_now);
    CHECK_EQ(s_j.err, PRN_ENGINE);
    /* a caller that never writes: STOP ends at 2 x answer_ms + the gap */
    CHECK(t50m_armed_at_cap(&t50m, false));
    prn_job_tick(&s_j, s_now);
    const uint32_t t0 = s_now;
    CHECK(run_out(10000u));
    CHECK_EQ(s_now - t0, 2u * t50m.answer_ms + t50m.gap_ms);
    CHECK_EQ(s_j.err, PRN_ENGINE);
}

int main(void)
{
    if (!load("rows-test.prn", &s_rows) || !load("magic-test.prn", &s_magic)) {
        printf("the fixtures in fixtures/printers do not parse\n");
        return 1;
    }
    RUN(a_rows_print_runs_every_step_without_response);
    RUN(rows_errors_end_the_job_before_and_inside_the_abort_range);
    RUN(rows_abort_link_and_refused_writes);
    RUN(a_magic_print_joins_checks_and_reads_the_result);
    RUN(magic_rolls_against_the_limits_and_the_layout);
    RUN(magic_relinks_to_its_step_and_to_the_verdict);
    RUN(magic_answers_lost_and_the_query);
    RUN(a_job_that_cannot_run_does_not_start);
    RUN(a_value_too_large_for_its_encoding_ends_the_job_with_engine);
    RUN(a_job_ends_within_120s);
    RUN(the_cap_in_the_abort_range_still_sends_the_abort);
    return test_summary("prn_job");
}
