/*
 * The run note: the fact that an update run goes on after a restart.
 *
 * The failures under test: a note that is not one (another layout, a
 * truncated or corrupt entry) read as a run; a run that outlives a
 * rollback; a restart loop that opens UPDATES at every start (the
 * openings must count down and end, also on a store that refuses a write
 * or an erase); a run noted for an install that did not end, or lost for
 * one that did; and a version text that does not fit cut into another
 * one.  The store is memory, each call counted and refusable.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "fw_install.h"
#include "fw_run.h"

/* ------------------------------------------------------------ the store */

/* A store in memory: one entry, the calls counted, each refusable. */
typedef struct {
    uint8_t  buf[64];
    uint32_t len;          /* 0: no entry; UINT32_MAX: unreadable */
    unsigned loads, saves, erases;
    bool     save_fails, erase_fails;
} store_t;

static uint32_t s_load(void *ctx, uint8_t *buf, uint32_t cap)
{
    store_t *s = ctx;
    ++s->loads;
    if (s->len > 0u && s->len <= cap) {
        memcpy(buf, s->buf, s->len);
    }
    return s->len;
}

static bool s_save(void *ctx, const uint8_t *buf, uint32_t len)
{
    store_t *s = ctx;
    ++s->saves;
    if (s->save_fails || len > sizeof(s->buf)) {
        return false;
    }
    memcpy(s->buf, buf, len);
    s->len = len;
    return true;
}

static bool s_erase(void *ctx)
{
    store_t *s = ctx;
    ++s->erases;
    if (s->erase_fails) {
        return false;
    }
    s->len = 0u;
    return true;
}

static fw_run_ops_t ops_of(store_t *s)
{
    const fw_run_ops_t o = { s_load, s_save, s_erase, s };
    return o;
}

static unsigned calls(const store_t *s)
{
    return s->loads + s->saves + s->erases;
}

/* One start: a fresh view, settled against @p running. */
static fw_run_seen_t start(store_t *s, fw_run_t *run, const char *running)
{
    const fw_run_ops_t o = ops_of(s);
    memset(run, 0, sizeof(*run));
    return fw_run_settle(&o, run, running);
}

/* The note before a restart, from a view of the start before it. */
static bool begin(store_t *s, const char *expect)
{
    const fw_run_ops_t o = ops_of(s);
    fw_run_t run;
    memset(&run, 0, sizeof(run));
    return fw_run_begin(&o, &run, expect);
}

/* An entry built by hand, as the layout says. */
static void forge(store_t *s, uint8_t layout, const char *text,
                  uint8_t opens)
{
    memset(s->buf, 0, sizeof(s->buf));
    s->buf[0] = layout;
    memcpy(s->buf + 1u, text, strlen(text));
    s->buf[1u + FW_RUN_TEXT_MAX] = opens;
    s->len = FW_RUN_BYTES;
}

/* ------------------------------------------------------------ the cases */

TEST_CASE(no_note_is_no_run)
{
    store_t s = { 0 };
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
    CHECK(!run.active);
    CHECK(run.clear);
    CHECK_EQ(run.opens_left, 0u);
    CHECK_STR_EQ(run.expect, "");
    /* Read once, nothing written. */
    CHECK_EQ(s.loads, 1u);
    CHECK_EQ(s.saves + s.erases, 0u);
    /* An end with nothing stored writes nothing either. */
    const fw_run_ops_t o = ops_of(&s);
    CHECK(fw_run_end(&o, &run));
    CHECK_EQ(s.erases, 0u);
}

TEST_CASE(begin_then_settle_on_the_expected_version_is_a_run_with_one_opening_left)
{
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    CHECK_EQ(s.len, FW_RUN_BYTES);
    CHECK_EQ(s.buf[0], FW_RUN_LAYOUT);
    CHECK_EQ(s.buf[1u + FW_RUN_TEXT_MAX], FW_RUN_OPENS);
    CHECK_EQ(memcmp(s.buf + 1u, "0.24.1", 6u), 0);
    for (size_t i = 7u; i < 1u + FW_RUN_TEXT_MAX; ++i) {
        CHECK_EQ(s.buf[i], 0u);                  /* zero padding */
    }
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK(run.active);
    CHECK_EQ(run.opens_left, 1u);
    CHECK_STR_EQ(run.expect, "0.24.1");
    CHECK(!run.clear);
    /* The spend is stored before the run is used. */
    CHECK_EQ(s.len, FW_RUN_BYTES);
    CHECK_EQ(s.buf[1u + FW_RUN_TEXT_MAX], 1u);
}

TEST_CASE(the_second_settle_spends_the_last_opening_and_the_third_finds_none)
{
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK_EQ(run.opens_left, 1u);
    /* Power cut, start two: the last opening, and the entry goes. */
    const unsigned erases = s.erases;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK(run.active);
    CHECK_EQ(run.opens_left, 0u);
    CHECK_EQ(s.erases, erases + 1u);
    CHECK_EQ(s.len, 0u);
    CHECK(run.clear);
    /* Start three: no run, whatever the loop. */
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
        CHECK(!run.active);
    }
}

TEST_CASE(a_second_settle_in_one_start_calls_nothing)
{
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    const fw_run_ops_t o = ops_of(&s);
    const unsigned before = calls(&s);
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(fw_run_settle(&o, &run, "0.24.1"), FW_RUN_STANDS);
    }
    CHECK_EQ(calls(&s), before);
    CHECK_EQ(run.opens_left, 1u);
    /* Ended in between: settling again does not bring it back. */
    CHECK(fw_run_end(&o, &run));
    CHECK_EQ(fw_run_settle(&o, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK(!run.active);
}

TEST_CASE(another_running_version_ends_the_run_and_erases)
{
    /* The install wrote 0.24.1; the trial went back and 0.24.0 runs. */
    static const char *const k_running[] = {
        "0.24.0", "0.24.10", "0.24.", "0.24.1 ", "", NULL,
    };
    for (size_t i = 0; i < sizeof(k_running) / sizeof(k_running[0]); ++i) {
        store_t s = { 0 };
        CHECK(begin(&s, "0.24.1"));
        fw_run_t run;
        CHECK_EQ(start(&s, &run, k_running[i]), FW_RUN_MISMATCH);
        CHECK(!run.active);
        CHECK_EQ(run.opens_left, 0u);
        CHECK_STR_EQ(run.expect, "0.24.1");   /* for the console line */
        CHECK_EQ(s.erases, 1u);
        CHECK_EQ(s.len, 0u);
        CHECK(run.clear);
        /* The next start of the same build finds nothing. */
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
    }
    /* An erase the store refuses: still no run, at this start and the
     * next one, which reads the same mismatch. */
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    s.erase_fails = true;
    fw_run_t run;
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(start(&s, &run, "0.24.0"), FW_RUN_MISMATCH);
        CHECK(!run.active);
        CHECK(!run.clear);
    }
}

TEST_CASE(a_foreign_layout_byte_reads_as_none_and_is_erased)
{
    for (unsigned b = 0u; b < 256u; ++b) {
        if (b == FW_RUN_LAYOUT) {
            continue;
        }
        store_t s = { 0 };
        forge(&s, (uint8_t)b, "0.24.1", 2u);
        fw_run_t run;
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_FOREIGN);
        CHECK(!run.active);
        CHECK_STR_EQ(run.expect, "");
        CHECK_EQ(s.erases, 1u);
        CHECK_EQ(s.saves, 0u);
        CHECK_EQ(s.len, 0u);
    }
    /* The restart note's entry, 40 bytes, under this key: not a run. */
    store_t s = { 0 };
    memset(s.buf, 0x31, sizeof(s.buf));
    s.len = FW_NOTE_BYTES;
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_FOREIGN);
    CHECK(!run.active);
}

TEST_CASE(a_truncated_blob_reads_as_none)
{
    store_t full = { 0 };
    CHECK(begin(&full, "0.24.1"));
    /* Every length short of the entry, one past it, and an entry the
     * store cannot read (UINT32_MAX): none is a run. */
    for (uint32_t len = 1u; len <= FW_RUN_BYTES + 1u; ++len) {
        if (len == FW_RUN_BYTES) {
            continue;
        }
        store_t s = full;
        s.len = len;
        s.loads = s.saves = s.erases = 0u;
        fw_run_t run;
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_FOREIGN);
        CHECK(!run.active);
        CHECK_EQ(s.erases, 1u);
    }
    store_t s = full;
    s.len = UINT32_MAX;
    s.erase_fails = true;      /* and it cannot be erased either */
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_FOREIGN);
    CHECK(!run.active);
    CHECK(!run.clear);
}

TEST_CASE(a_corrupt_entry_reads_as_none_and_is_erased)
{
    /* The openings outside 1 to FW_RUN_OPENS. */
    static const uint8_t k_opens[] = { 0u, FW_RUN_OPENS + 1u, 0x7Fu, 0xFFu };
    for (size_t i = 0; i < sizeof(k_opens); ++i) {
        store_t s = { 0 };
        forge(&s, FW_RUN_LAYOUT, "0.24.1", k_opens[i]);
        fw_run_t run;
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_FOREIGN);
        CHECK(!run.active);
        CHECK_EQ(s.len, 0u);
    }
    /* An empty text. */
    {
        store_t s = { 0 };
        forge(&s, FW_RUN_LAYOUT, "", 2u);
        fw_run_t run;
        CHECK_EQ(start(&s, &run, ""), FW_RUN_FOREIGN);
        CHECK(!run.active);
    }
    /* A byte outside 0x21 to 0x7E, at the first and the last place. */
    static const uint8_t k_bad[] = { 0x01u, 0x20u, 0x7Fu, 0x80u, 0xFFu };
    for (size_t i = 0; i < sizeof(k_bad); ++i) {
        for (size_t at = 0u; at < FW_RUN_TEXT_MAX; at += FW_RUN_TEXT_MAX - 1u) {
            store_t s = { 0 };
            forge(&s, FW_RUN_LAYOUT, "0123456789abcdef", 2u);
            s.buf[1u + at] = k_bad[i];
            fw_run_t run;
            CHECK_EQ(start(&s, &run, "0123456789abcdef"), FW_RUN_FOREIGN);
            CHECK(!run.active);
        }
    }
    /* A byte behind the padding's start: "0.24.1\0X". */
    {
        store_t s = { 0 };
        forge(&s, FW_RUN_LAYOUT, "0.24.1", 2u);
        s.buf[1u + 7u] = 'X';
        fw_run_t run;
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_FOREIGN);
        CHECK(!run.active);
    }
    /* The same entries, right: a run. */
    {
        store_t s = { 0 };
        forge(&s, FW_RUN_LAYOUT, "0.24.1", 1u);
        fw_run_t run;
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
        CHECK_EQ(run.opens_left, 0u);
    }
}

TEST_CASE(a_version_text_of_16_bytes_fits_and_17_is_refused)
{
    static const char k16[] = "65535.65535.6553";
    static const char k17[] = "65535.65535.65535";
    CHECK_EQ(strlen(k16), FW_RUN_TEXT_MAX);
    CHECK_EQ(strlen(k17), FW_RUN_TEXT_MAX + 1u);
    store_t s = { 0 };
    CHECK(begin(&s, k16));
    fw_run_t run;
    CHECK_EQ(start(&s, &run, k16), FW_RUN_STANDS);
    CHECK_STR_EQ(run.expect, k16);
    /* 17 bytes: not written, and never cut to the 16 that would match. */
    store_t t = { 0 };
    CHECK(!begin(&t, k17));
    CHECK_EQ(t.saves, 0u);
    CHECK_EQ(start(&t, &run, k16), FW_RUN_NONE);
    CHECK_EQ(start(&t, &run, k17), FW_RUN_NONE);
    /* A running text of 17 bytes against a 16-byte note: no match. */
    store_t u = { 0 };
    CHECK(begin(&u, k16));
    CHECK_EQ(start(&u, &run, k17), FW_RUN_MISMATCH);
    /* Empty, NULL and a space are refused the same way. */
    static const char *const k_refused[] = { "", NULL, "0.24 1", "0.24.1\n" };
    for (size_t i = 0; i < sizeof(k_refused) / sizeof(k_refused[0]); ++i) {
        store_t v = { 0 };
        CHECK(!begin(&v, k_refused[i]));
        CHECK_EQ(v.saves, 0u);
    }
}

TEST_CASE(a_refused_text_erases_a_note_that_stands)
{
    /* A run of 0.24.1 stands; the next install names a text that does not
     * fit.  The restart must not find the old run. */
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    CHECK(!begin(&s, "0.24.2-a-long-build"));
    CHECK_EQ(s.len, 0u);
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
}

TEST_CASE(a_refused_save_is_no_run_and_reports_false)
{
    store_t s = { 0 };
    s.save_fails = true;
    CHECK(!begin(&s, "0.24.1"));
    CHECK_EQ(s.saves, 1u);
    s.save_fails = false;
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
    CHECK(!run.active);
}

TEST_CASE(a_store_that_refuses_the_spend_opens_nothing)
{
    /* At 2 openings the spend is a save; refused: no run, and the entry
     * keeps its count, so each start tries again and opens nothing. */
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    s.save_fails = true;
    fw_run_t run;
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_UNSPENT);
        CHECK(!run.active);
        CHECK_EQ(run.opens_left, 0u);
    }
    CHECK_EQ(s.buf[1u + FW_RUN_TEXT_MAX], FW_RUN_OPENS);
    /* At 1 opening the spend is an erase; refused: no run either. */
    store_t t = { 0 };
    forge(&t, FW_RUN_LAYOUT, "0.24.1", 1u);
    t.erase_fails = true;
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(start(&t, &run, "0.24.1"), FW_RUN_UNSPENT);
        CHECK(!run.active);
        CHECK(!run.clear);
    }
}

TEST_CASE(a_refused_erase_still_ends_the_run_in_ram)
{
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    s.erase_fails = true;
    const fw_run_ops_t o = ops_of(&s);
    CHECK(!fw_run_end(&o, &run));
    CHECK(!run.active);
    CHECK_EQ(run.opens_left, 0u);
    CHECK_EQ(s.erases, 1u);
    /* The entry left opens nothing while the store refuses to spend it;
     * once the store erases, it spends its last opening and is gone: two
     * openings in all, no loop. */
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_UNSPENT);
    s.erase_fails = false;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK_EQ(run.opens_left, 0u);
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
}

TEST_CASE(end_twice_is_harmless)
{
    store_t s = { 0 };
    CHECK(begin(&s, "0.24.1"));
    fw_run_t run;
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    const fw_run_ops_t o = ops_of(&s);
    CHECK(fw_run_end(&o, &run));
    CHECK(fw_run_end(&o, &run));
    CHECK_EQ(s.erases, 1u);
    CHECK(!run.active);
    CHECK_EQ(s.len, 0u);
    /* An end before any settle in a start erases what may stand. */
    store_t t = { 0 };
    CHECK(begin(&t, "0.24.1"));
    fw_run_t fresh_run;
    memset(&fresh_run, 0, sizeof(fresh_run));
    const fw_run_ops_t p = ops_of(&t);
    CHECK(fw_run_end(&p, &fresh_run));
    CHECK(fw_run_end(&p, &fresh_run));
    CHECK_EQ(t.erases, 1u);
    CHECK_EQ(start(&t, &fresh_run, "0.24.1"), FW_RUN_NONE);
    /* A refused erase is tried again by the next end. */
    store_t u = { 0 };
    CHECK(begin(&u, "0.24.1"));
    CHECK_EQ(start(&u, &run, "0.24.1"), FW_RUN_STANDS);
    u.erase_fails = true;
    const fw_run_ops_t q = ops_of(&u);
    CHECK(!fw_run_end(&q, &run));
    u.erase_fails = false;
    CHECK(fw_run_end(&q, &run));
    CHECK(fw_run_end(&q, &run));
    CHECK_EQ(u.erases, 2u);
}

TEST_CASE(the_run_is_noted_only_after_a_complete_install)
{
    static const fw_install_status_t k_st[] = {
        FW_INSTALL_IDLE, FW_INSTALL_WAITING, FW_INSTALL_RUNNING,
        FW_INSTALL_DONE, FW_INSTALL_FAILED,
    };
    for (size_t i = 0; i < sizeof(k_st) / sizeof(k_st[0]); ++i) {
        store_t s = { 0 };
        const fw_run_ops_t o = ops_of(&s);
        fw_run_t run;
        memset(&run, 0, sizeof(run));
        const bool done = (k_st[i] == FW_INSTALL_DONE);
        CHECK_EQ(fw_run_after_install(&o, &run, k_st[i], "0.24.1"), done);
        CHECK_EQ(s.saves, done ? 1u : 0u);
        CHECK_EQ(s.erases, 0u);
        CHECK_EQ(start(&s, &run, "0.24.1"),
                 done ? FW_RUN_STANDS : FW_RUN_NONE);
    }
}

/* The panel's flash in memory, enough for one install. */
#define SLOT 0x40000u
static uint8_t g_slot[SLOT];
static bool    g_switched;

static bool m_begin(void *ctx, uint32_t bytes)
{
    (void)ctx;
    return bytes <= SLOT;
}

static bool m_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t len)
{
    (void)ctx;
    memcpy(g_slot + off, src, len);
    return true;
}

static bool m_finish(void *ctx)
{
    (void)ctx;
    g_switched = true;
    return true;
}

static void m_abort(void *ctx) { (void)ctx; }

static const fw_flash_t k_mem = { m_begin, m_write, m_finish, m_abort, NULL };

TEST_CASE(a_power_cut_before_the_write_is_no_run)
{
    static uint8_t img[SLOT];
    fw_fix_t f = fw_fix_default();
    f.version = "0.24.1";
    f.body = 200000u;                      /* 4 writes of 64kB */
    const uint32_t len = fw_fix_make(&f, img);
    const fw_image_info_t r = fw_fix_running("0.24.0", 0x11);
    /* The power goes after 0 to 5 steps: every step short of DONE leaves
     * the running build and no run; the whole install notes it. */
    for (unsigned cut = 0u; cut < 8u; ++cut) {
        store_t s = { 0 };
        const fw_run_ops_t o = ops_of(&s);
        fw_run_t run;
        memset(&run, 0, sizeof(run));
        g_switched = false;
        fw_install_t in;
        CHECK_EQ(fw_install_begin(&in, &k_mem, img, len, SLOT, &r), FW_OK);
        fw_install_status_t st = FW_INSTALL_IDLE;
        unsigned steps = 0u;
        do {
            st = fw_install_step(&in, true);
            ++steps;
        } while (st == FW_INSTALL_RUNNING && steps < cut);
        if (st == FW_INSTALL_RUNNING) {
            /* Cut here: the call after the loop never runs. */
            CHECK(!g_switched);
            CHECK_EQ(start(&s, &run, "0.24.0"), FW_RUN_NONE);
            continue;
        }
        CHECK_EQ(st, FW_INSTALL_DONE);
        CHECK(g_switched);
        CHECK(fw_run_after_install(&o, &run, st, f.version));
        CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    }
}

TEST_CASE(a_run_goes_on_across_the_install_and_the_c6_restart)
{
    /* The install of 0.24.1 notes the run; its first start spends one
     * opening; the C6 update notes it again with the running version, so
     * the start after the C6 restart has its own openings. */
    store_t s = { 0 };
    const fw_run_ops_t o = ops_of(&s);
    fw_run_t run;
    memset(&run, 0, sizeof(run));
    CHECK(fw_run_after_install(&o, &run, FW_INSTALL_DONE, "0.24.1"));
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK_EQ(run.opens_left, 1u);
    CHECK(fw_run_begin(&o, &run, "0.24.1"));        /* the C6 update */
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_STANDS);
    CHECK_EQ(run.opens_left, 1u);
    /* The run's last step done: ended, and the next start finds none. */
    CHECK(fw_run_end(&o, &run));
    CHECK_EQ(start(&s, &run, "0.24.1"), FW_RUN_NONE);
}

int main(void)
{
    RUN(no_note_is_no_run);
    RUN(begin_then_settle_on_the_expected_version_is_a_run_with_one_opening_left);
    RUN(the_second_settle_spends_the_last_opening_and_the_third_finds_none);
    RUN(a_second_settle_in_one_start_calls_nothing);
    RUN(another_running_version_ends_the_run_and_erases);
    RUN(a_foreign_layout_byte_reads_as_none_and_is_erased);
    RUN(a_truncated_blob_reads_as_none);
    RUN(a_corrupt_entry_reads_as_none_and_is_erased);
    RUN(a_version_text_of_16_bytes_fits_and_17_is_refused);
    RUN(a_refused_text_erases_a_note_that_stands);
    RUN(a_refused_save_is_no_run_and_reports_false);
    RUN(a_store_that_refuses_the_spend_opens_nothing);
    RUN(a_refused_erase_still_ends_the_run_in_ram);
    RUN(end_twice_is_harmless);
    RUN(the_run_is_noted_only_after_a_complete_install);
    RUN(a_power_cut_before_the_write_is_no_run);
    RUN(a_run_goes_on_across_the_install_and_the_c6_restart);
    return test_summary("fw_run");
}
