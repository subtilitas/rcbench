/*
 * Sending one card file to the browser a piece at a time.
 *
 * The failures under test: a download that holds the whole file, one that
 * sends a different number of bytes than its Content-Length announced, and
 * a read error or a pulled card that a browser would take as a complete
 * file.  And the connection's side: a receiver that closes, fails or stops
 * reading part way, and the radio going off, each end the transfer at once
 * with the file closed, so the server answers the next request.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "wa_fakefs.h"
#include "wa_send.h"

static uint8_t g_buf[WA_SEND_CHUNK];

/* Send node @p node announced as @p size; returns the bytes or -1. */
static int64_t send_all(fake_fs_t *fs, int node, uint64_t size, int *pieces)
{
    const wa_fs_t w = fake_wa(fs);
    wa_send_t s;
    if (!wa_send_begin(&s, &w, fs->n[node].path, size)) {
        return -2;
    }
    int64_t total = 0;
    *pieces = 0;
    for (;;) {
        const int32_t n = wa_send_next(&s, g_buf, sizeof(g_buf));
        if (n < 0) {
            CHECK(s.failed);
            CHECK_EQ(fs->files_open, 0);
            return -1;
        }
        if (n == 0) {
            CHECK_EQ(fs->files_open, 0);
            CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), 0);
            return total;
        }
        CHECK(n <= (int32_t)WA_SEND_CHUNK);
        for (int32_t k = 0; k < n; ++k) {
            if (g_buf[k] != fake_byte(node, (uint64_t)total + (uint64_t)k)) {
                T_FAIL("byte %ld differs", (long)(total + k));
                return -3;
            }
        }
        total += n;
        ++*pieces;
    }
}

TEST_CASE(an_empty_file_is_sent_as_no_bytes)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "empty.csv", false, 0u, 0);
    int pieces = -1;
    CHECK_EQ(send_all(&fs, f, 0u, &pieces), 0);
    CHECK_EQ(pieces, 0);
    fake_free(&fs);
}

TEST_CASE(a_file_goes_in_pieces_of_4096_bytes)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int a = fake_add(&fs, "a", false, 3u * WA_SEND_CHUNK, 0);
    const int b = fake_add(&fs, "b", false, 3u * WA_SEND_CHUNK + 1u, 0);
    const int c = fake_add(&fs, "c", false, 1u, 0);
    const int d = fake_add(&fs, "d", false, WA_SEND_CHUNK - 1u, 0);
    int pieces = 0;
    CHECK_EQ(send_all(&fs, a, 3u * WA_SEND_CHUNK, &pieces),
             3 * (int64_t)WA_SEND_CHUNK);
    CHECK_EQ(pieces, 3);
    CHECK_EQ(send_all(&fs, b, 3u * WA_SEND_CHUNK + 1u, &pieces),
             3 * (int64_t)WA_SEND_CHUNK + 1);
    CHECK_EQ(pieces, 4);
    CHECK_EQ(send_all(&fs, c, 1u, &pieces), 1);
    CHECK_EQ(send_all(&fs, d, WA_SEND_CHUNK - 1u, &pieces),
             (int64_t)WA_SEND_CHUNK - 1);
    CHECK_EQ(pieces, 1);
    fake_free(&fs);
}

TEST_CASE(a_read_error_part_way_fails_the_download)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 20000u, 0);
    /* At the start, inside a piece, at a piece's edge, at the last byte. */
    const int64_t at[] = { 0, 5000, 8192, 19999 };
    for (int i = 0; i < 4; ++i) {
        fs.n[f].fail_at = at[i];
        int pieces = 0;
        CHECK_EQ(send_all(&fs, f, 20000u, &pieces), -1);
    }
    fs.n[f].fail_at = -1;
    fake_free(&fs);
}

TEST_CASE(a_file_shorter_or_longer_than_announced_fails)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 10000u, 0);
    int pieces = 0;
    /* It ends early: a card pulled, or the file cut since the listing. */
    fs.n[f].end_at = 6000;
    CHECK_EQ(send_all(&fs, f, 10000u, &pieces), -1);
    fs.n[f].end_at = 8192;
    CHECK_EQ(send_all(&fs, f, 10000u, &pieces), -1);
    fs.n[f].end_at = -1;
    /* It runs long: grown since the listing. */
    CHECK_EQ(send_all(&fs, f, 8192u, &pieces), -1);
    CHECK_EQ(send_all(&fs, f, 0u, &pieces), -1);
    CHECK_EQ(send_all(&fs, f, 10000u, &pieces), 10000);
    fake_free(&fs);
}

TEST_CASE(a_card_pulled_between_pieces_fails_and_closes)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 10000u, 0);
    const wa_fs_t w = fake_wa(&fs);
    wa_send_t s;
    CHECK(wa_send_begin(&s, &w, "f", 10000u));
    CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), (int32_t)WA_SEND_CHUNK);
    fs.pulled = true;
    CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), -1);
    CHECK_EQ(wa_send_next(&s, g_buf, sizeof(g_buf)), -1);
    CHECK_EQ(fs.files_open, 0);
    /* Gone before it opened. */
    CHECK(!wa_send_begin(&s, &w, "f", 10000u));
    /* cppcheck-suppress redundantAssignment
     * the send reads through its own pointer to fs */
    fs.pulled = false;
    CHECK(!wa_send_begin(&s, &w, "missing", 1u));
    CHECK(!wa_send_begin(&s, NULL, "f", 1u));
    /* Given up part way by the server: closed. */
    CHECK(wa_send_begin(&s, &w, "f", 10000u));
    CHECK_EQ(wa_send_next(&s, g_buf, 100u), 100);
    CHECK_EQ(fs.files_open, 1);
    wa_send_end(&s);
    CHECK_EQ(fs.files_open, 0);
    (void)f;
    fake_free(&fs);
}

/* ------------------------------------------------------- the connection */

/*
 * A connection in memory.  put() takes up to the room it has; a wait moves
 * the clock by wait_ms and gives back refill bytes of room.  Every byte
 * taken is checked against the file's pattern.
 */
typedef struct {
    uint32_t now;
    uint32_t room;
    uint32_t refill;
    uint32_t wait_ms;
    int64_t  gone_at;        /* put() fails once this many are taken; -1 */
    int64_t  wait_gone_at;   /* wait() says GONE from this many on; -1   */
    int64_t  stop_at;        /* stop() is true from this many on; -1     */
    int      refill_on_wait; /* only that wait gives room back; 0: each  */
    bool     overclaim;      /* put() says it took more than offered     */
    int      node;           /* the file's node, for the pattern; -1     */
    uint64_t taken;
    uint32_t last_take_ms;
    uint32_t last_slice;
    int      puts;
    int      waits;
    bool     bytes_ok;
} conn_t;

static void conn_init(conn_t *c, uint32_t room, int node)
{
    memset(c, 0, sizeof(*c));
    c->room = room;
    c->refill = room;
    c->wait_ms = WA_SEND_SLICE_MS;
    c->gone_at = c->wait_gone_at = c->stop_at = -1;
    c->node = node;
    c->bytes_ok = true;
}

static int32_t conn_put(void *ctx, const uint8_t *p, uint32_t n)
{
    conn_t *c = ctx;
    ++c->puts;
    if (c->gone_at >= 0 && c->taken >= (uint64_t)c->gone_at) {
        return -1;
    }
    if (c->overclaim) {
        return (int32_t)n + 1;
    }
    uint32_t take = (n < c->room) ? n : c->room;
    if (c->gone_at >= 0 && c->taken + take > (uint64_t)c->gone_at) {
        take = (uint32_t)((uint64_t)c->gone_at - c->taken);
    }
    for (uint32_t k = 0; k < take && c->node >= 0; ++k) {
        if (p[k] != fake_byte(c->node, c->taken + k)) {
            c->bytes_ok = false;
        }
    }
    c->room -= take;
    c->taken += take;
    if (take > 0u) {
        c->last_take_ms = c->now;
    }
    return (int32_t)take;
}

static wa_wait_t conn_wait(void *ctx, uint32_t ms)
{
    conn_t *c = ctx;
    ++c->waits;
    c->last_slice = ms;
    c->now += c->wait_ms;
    if (c->wait_gone_at >= 0 && c->taken >= (uint64_t)c->wait_gone_at) {
        return WA_WAIT_GONE;
    }
    if (c->refill_on_wait == 0 || c->refill_on_wait == c->waits) {
        c->room += c->refill;
    }
    return (c->refill > 0u) ? WA_WAIT_READY : WA_WAIT_IDLE;
}

static bool conn_stop(void *ctx)
{
    const conn_t *c = ctx;
    return c->stop_at >= 0 && c->taken >= (uint64_t)c->stop_at;
}

// cppcheck-suppress constParameterCallback
static uint32_t conn_now(void *ctx)
{
    return ((const conn_t *)ctx)->now;
}

static wa_sink_t conn_sink(conn_t *c)
{
    const wa_sink_t k = { conn_put, conn_wait, conn_stop, conn_now, c };
    return k;
}

/* Run node @p f, announced as its size, into @p c. */
static wa_send_res_t run_file(fake_fs_t *fs, int f, conn_t *c)
{
    const wa_fs_t w = fake_wa(fs);
    const wa_sink_t k = conn_sink(c);
    wa_send_t s;
    if (!wa_send_begin(&s, &w, fs->n[f].path, fs->n[f].size)) {
        return (wa_send_res_t)-1;
    }
    return wa_send_run(&s, g_buf, sizeof(g_buf), &k);
}

TEST_CASE(a_file_goes_whole_through_a_connection_that_takes_part)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "big.bin", false, 5u * WA_SEND_CHUNK + 7u, 0);
    /* Room for 1000 bytes, refilled by each wait: every piece is taken in
     * parts, and parts that cross a piece's edge. */
    conn_t c;
    conn_init(&c, 1000u, f);
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_DONE);
    CHECK_EQ((long)c.taken, (long)(5u * WA_SEND_CHUNK + 7u));
    CHECK(c.bytes_ok);
    CHECK_EQ(fs.files_open, 0);
    CHECK_EQ(c.last_slice, WA_SEND_SLICE_MS);
    /* An empty file: nothing offered, done. */
    const int e = fake_add(&fs, "empty", false, 0u, 0);
    conn_init(&c, 1000u, e);
    CHECK_EQ(run_file(&fs, e, &c), WA_SEND_DONE);
    CHECK_EQ(c.puts, 0);
    CHECK_EQ(fs.files_open, 0);
    /* Nothing to put is done without a call. */
    CHECK_EQ(wa_sink_all(&(wa_sink_t){ conn_put, conn_wait, conn_stop,
                                       conn_now, &c }, g_buf, 0u),
             WA_SEND_DONE);
    CHECK_EQ(c.puts, 0);
    fake_free(&fs);
}

TEST_CASE(a_receiver_gone_part_way_ends_the_send_at_once)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 20000u, 0);
    /* The connection fails at the first byte, inside a piece, at a piece's
     * edge and at the last byte. */
    const int64_t at[] = { 0, 5000, 8192, 19999 };
    for (int i = 0; i < 4; ++i) {
        conn_t c;
        conn_init(&c, 3000u, f);
        c.gone_at = at[i];
        CHECK_EQ(run_file(&fs, f, &c), WA_SEND_GONE);
        CHECK_EQ((long)c.taken, (long)at[i]);
        CHECK_EQ(fs.files_open, 0);
        CHECK(c.bytes_ok);
    }
    /* It closes while the panel waits for room: no further offer. */
    conn_t c;
    conn_init(&c, 3000u, f);
    c.wait_gone_at = 6000;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_GONE);
    CHECK_EQ((long)c.taken, 6000L);
    CHECK_EQ(c.waits, 2);
    CHECK_EQ(fs.files_open, 0);
    /* A connection that claims more than it was offered is a failed one. */
    conn_init(&c, 3000u, f);
    c.overclaim = true;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_GONE);
    CHECK_EQ(c.puts, 1);
    CHECK_EQ(fs.files_open, 0);
    fake_free(&fs);
}

TEST_CASE(a_receiver_that_takes_nothing_for_30_s_ends_the_send)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 20000u, 0);
    /* 5000 bytes, then nothing ever again: the send ends 30s after the
     * last byte, at the first look at or past it. */
    conn_t c;
    conn_init(&c, 5000u, f);
    c.refill = 0u;
    c.now = 1000u;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_QUIET);
    CHECK_EQ((long)c.taken, 5000L);
    CHECK_EQ((long)(c.now - c.last_take_ms), (long)WA_SEND_QUIET_MS);
    CHECK_EQ(c.waits, (int)(WA_SEND_QUIET_MS / WA_SEND_SLICE_MS));
    CHECK_EQ(fs.files_open, 0);
    /* Room again one slice before the limit: the send goes on, and the
     * quiet time counts from the new byte. */
    conn_init(&c, 5000u, f);
    c.refill_on_wait = (int)(WA_SEND_QUIET_MS / WA_SEND_SLICE_MS) - 1;
    c.refill = 20000u;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_DONE);
    CHECK_EQ((long)c.taken, 20000L);
    CHECK(c.bytes_ok);
    /* The clock wraps at 2^32 during the wait: still 30s, not at once and
     * not never. */
    conn_init(&c, 5000u, f);
    c.refill = 0u;
    c.now = 0xFFFFF000u;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_QUIET);
    CHECK_EQ((long)(uint32_t)(c.now - c.last_take_ms),
             (long)WA_SEND_QUIET_MS);
    /* A wait that returns early (room said, none there) still counts the
     * clock, not the waits. */
    conn_init(&c, 5000u, f);
    c.refill = 0u;
    c.wait_ms = 1u;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_QUIET);
    CHECK_EQ(c.waits, (int)WA_SEND_QUIET_MS);
    CHECK_EQ(fs.files_open, 0);
    fake_free(&fs);
}

TEST_CASE(the_radio_going_off_ends_the_send_within_a_slice)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 20000u, 0);
    /* Before the first byte: nothing offered. */
    conn_t c;
    conn_init(&c, 3000u, f);
    c.stop_at = 0;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_STOPPED);
    CHECK_EQ(c.puts, 0);
    CHECK_EQ(fs.files_open, 0);
    /* Part way, while the receiver takes nothing: at the next slice. */
    conn_init(&c, 3000u, f);
    c.refill = 0u;
    c.stop_at = 3000;
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_STOPPED);
    CHECK_EQ((long)c.taken, 3000L);
    CHECK(c.waits <= 1);
    CHECK_EQ(fs.files_open, 0);
    fake_free(&fs);
}

TEST_CASE(a_card_read_error_part_way_ends_the_send)
{
    fake_fs_t fs;
    fake_init(&fs);
    const int f = fake_add(&fs, "f", false, 20000u, 0);
    fs.n[f].fail_at = 9000;
    conn_t c;
    conn_init(&c, 100000u, f);
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_READ_FAILED);
    CHECK_EQ((long)c.taken, 9000L);
    CHECK_EQ(fs.files_open, 0);
    /* A file shorter than announced. */
    fs.n[f].fail_at = -1;
    fs.n[f].end_at = 12288;
    conn_init(&c, 100000u, f);
    CHECK_EQ(run_file(&fs, f, &c), WA_SEND_READ_FAILED);
    CHECK_EQ((long)c.taken, 12288L);
    CHECK_EQ(fs.files_open, 0);
    fake_free(&fs);
}

TEST_CASE(every_outcome_has_a_name)
{
    const wa_send_res_t all[] = { WA_SEND_DONE, WA_SEND_GONE, WA_SEND_QUIET,
                                  WA_SEND_STOPPED, WA_SEND_READ_FAILED };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        CHECK(strcmp(wa_send_res_name(all[i]), "?") != 0);
        for (size_t j = 0; j < i; ++j) {
            CHECK(strcmp(wa_send_res_name(all[i]),
                         wa_send_res_name(all[j])) != 0);
        }
    }
    CHECK_STR_EQ(wa_send_res_name((wa_send_res_t)99), "?");
}

/* ------------------------------------------------- the refused body */

/** @brief A client that is scripted a read at a time. */
typedef struct {
    const int32_t *script;  /**< each get()'s answer                    */
    size_t n;               /**< answers; after them the last again     */
    size_t at;              /**< the next                               */
    uint32_t now;           /**< the clock                              */
    uint32_t step_ms;       /**< what a get() takes                     */
    int stop_at;            /**< stop() from this get() on; -1 never    */
    uint32_t max_ask;       /**< the largest read asked                 */
    uint64_t got;           /**< bytes handed over                      */
    int gets;               /**< get() calls                            */
} client_t;

/**
 * @brief wa_source_t's get(): the script's next answer, the clock moved.
 * @param ctx  the client
 * @param p    unused
 * @param n    bytes asked at most
 * @return as wa_source_t says
 */
static int32_t client_get(void *ctx, uint8_t *p, uint32_t n)
{
    client_t *c = ctx;
    (void)p;
    c->gets++;
    c->now += c->step_ms;
    if (n > c->max_ask) {
        c->max_ask = n;
    }
    int32_t a = c->script[(c->at < c->n) ? c->at : c->n - 1u];
    c->at++;
    if (a > (int32_t)n) {
        a = (int32_t)n;
    }
    if (a > 0) {
        c->got += (uint64_t)a;
    }
    return a;
}

/**
 * @brief wa_source_t's stop().
 * @param ctx  the client
 * @return true from its stop_at on
 */
static bool client_stop(void *ctx)
{
    const client_t *c = ctx;
    return c->stop_at >= 0 && c->gets >= c->stop_at;
}

/**
 * @brief wa_source_t's now_ms().
 * @param ctx  the client
 * @return its clock
 */
static uint32_t client_now(void *ctx)
{
    return ((const client_t *)ctx)->now;
}

/**
 * @brief wa_linger() over @p c with the 4,096B buffer.
 * @param c     the client
 * @param left  the body's bytes
 * @return how it ended
 */
static wa_linger_t linger(client_t *c, uint64_t left)
{
    const wa_source_t s = { client_get, client_stop, client_now, c };
    return wa_linger(&s, left, g_buf, sizeof(g_buf));
}

TEST_CASE(a_refused_body_is_read_to_its_end_and_no_further)
{
    static const int32_t all[] = { 4096 };
    client_t c = { .script = all, .n = 1u, .step_ms = 1u, .stop_at = -1 };
    /* No body: no read. */
    CHECK_EQ(linger(&c, 0u), WA_LINGER_DONE);
    CHECK_EQ(c.gets, 0);
    /* 1MiB: every byte, never a read past the end or the buffer. */
    CHECK_EQ(linger(&c, 1048576u + 1u), WA_LINGER_DONE);
    CHECK_EQ((long long)c.got, 1048577LL);
    CHECK_EQ(c.max_ask, 4096u);
    /* Shorter reads add up; the last asks only what is left. */
    static const int32_t some[] = { 1, 2, 3 };
    client_t d = { .script = some, .n = 3u, .step_ms = 1u, .stop_at = -1 };
    CHECK_EQ(linger(&d, 6u), WA_LINGER_DONE);
    CHECK_EQ(d.gets, 3);
    CHECK_EQ(d.max_ask, 6u);
}

TEST_CASE(a_client_that_closes_or_fails_ends_it_at_once)
{
    static const int32_t closes[] = { 100, 0 };
    client_t c = { .script = closes, .n = 2u, .step_ms = 1u, .stop_at = -1 };
    CHECK_EQ(linger(&c, 4096u), WA_LINGER_GONE);
    CHECK_EQ(c.gets, 2);
    static const int32_t fails[] = { WA_GET_FAILED };
    client_t d = { .script = fails, .n = 1u, .step_ms = 1u, .stop_at = -1 };
    CHECK_EQ(linger(&d, 4096u), WA_LINGER_GONE);
    CHECK_EQ(d.gets, 1);
}

TEST_CASE(no_byte_for_2_s_ends_it_and_a_byte_starts_the_2_s_again)
{
    static const int32_t idle[] = { WA_GET_IDLE };
    client_t c = { .script = idle, .n = 1u, .step_ms = WA_SEND_SLICE_MS,
                   .stop_at = -1 };
    CHECK_EQ(linger(&c, 4096u), WA_LINGER_QUIET);
    CHECK_EQ(c.now, WA_LINGER_QUIET_MS);
    CHECK_EQ(c.gets, (int)(WA_LINGER_QUIET_MS / WA_SEND_SLICE_MS));
    /* One millisecond short of the limit is still waiting, and the byte
     * starts it again: the second idle read still waits, the third does not
     * come. */
    static const int32_t late[] = { WA_GET_IDLE, 1, WA_GET_IDLE };
    client_t d = { .script = late, .n = 3u,
                   .step_ms = WA_LINGER_QUIET_MS - 1u, .stop_at = -1 };
    CHECK_EQ(linger(&d, 4096u), WA_LINGER_QUIET);
    CHECK_EQ(d.gets, 4);
}

TEST_CASE(a_body_still_arriving_after_10_s_is_late)
{
    /* A byte a second: no quiet, and the server held 10s at most. */
    static const int32_t drip[] = { 1 };
    client_t c = { .script = drip, .n = 1u, .step_ms = 1000u, .stop_at = -1 };
    CHECK_EQ(linger(&c, 1048576u), WA_LINGER_LATE);
    CHECK_EQ(c.now, WA_LINGER_TOTAL_MS);
    CHECK_EQ((long)c.got, 10L);
    /* Just inside the limit it ends well. */
    client_t d = { .script = drip, .n = 1u, .step_ms = 1000u, .stop_at = -1 };
    CHECK_EQ(linger(&d, 10u), WA_LINGER_DONE);
    /* The clock's wrap at 2^32 moves neither limit. */
    client_t e = { .script = drip, .n = 1u, .step_ms = 1000u, .stop_at = -1,
                   .now = 0xFFFFF000u };
    CHECK_EQ(linger(&e, 1048576u), WA_LINGER_LATE);
    CHECK_EQ((long)e.got, 10L);
    static const int32_t idle[] = { WA_GET_IDLE };
    client_t f = { .script = idle, .n = 1u, .step_ms = WA_SEND_SLICE_MS,
                   .stop_at = -1, .now = 0xFFFFFF00u };
    CHECK_EQ(linger(&f, 4096u), WA_LINGER_QUIET);
}

TEST_CASE(the_radio_going_off_ends_it_within_a_slice)
{
    static const int32_t idle[] = { WA_GET_IDLE };
    /* Before the first read, then after one slice. */
    client_t c = { .script = idle, .n = 1u, .step_ms = WA_SEND_SLICE_MS,
                   .stop_at = 0 };
    CHECK_EQ(linger(&c, 4096u), WA_LINGER_STOPPED);
    CHECK_EQ(c.gets, 0);
    client_t d = { .script = idle, .n = 1u, .step_ms = WA_SEND_SLICE_MS,
                   .stop_at = 1 };
    CHECK_EQ(linger(&d, 4096u), WA_LINGER_STOPPED);
    CHECK_EQ(d.gets, 1);
    /* No stop() is never. */
    const wa_source_t none = { client_get, NULL, client_now, &c };
    c.gets = 0;
    c.now = 0u;
    CHECK_EQ(wa_linger(&none, 4096u, g_buf, sizeof(g_buf)), WA_LINGER_QUIET);
}

TEST_CASE(every_way_a_refused_body_ends_has_a_name)
{
    const wa_linger_t all[] = { WA_LINGER_DONE, WA_LINGER_GONE, WA_LINGER_QUIET,
                                WA_LINGER_LATE, WA_LINGER_STOPPED };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        CHECK(strcmp(wa_linger_name(all[i]), "?") != 0);
        for (size_t j = 0; j < i; ++j) {
            CHECK(strcmp(wa_linger_name(all[i]), wa_linger_name(all[j])) != 0);
        }
    }
    CHECK_STR_EQ(wa_linger_name((wa_linger_t)99), "?");
}

int main(void)
{
    RUN(an_empty_file_is_sent_as_no_bytes);
    RUN(a_file_goes_in_pieces_of_4096_bytes);
    RUN(a_read_error_part_way_fails_the_download);
    RUN(a_file_shorter_or_longer_than_announced_fails);
    RUN(a_card_pulled_between_pieces_fails_and_closes);
    RUN(a_file_goes_whole_through_a_connection_that_takes_part);
    RUN(a_receiver_gone_part_way_ends_the_send_at_once);
    RUN(a_receiver_that_takes_nothing_for_30_s_ends_the_send);
    RUN(the_radio_going_off_ends_the_send_within_a_slice);
    RUN(a_card_read_error_part_way_ends_the_send);
    RUN(every_outcome_has_a_name);
    RUN(a_refused_body_is_read_to_its_end_and_no_further);
    RUN(a_client_that_closes_or_fails_ends_it_at_once);
    RUN(no_byte_for_2_s_ends_it_and_a_byte_starts_the_2_s_again);
    RUN(a_body_still_arriving_after_10_s_is_late);
    RUN(the_radio_going_off_ends_it_within_a_slice);
    RUN(every_way_a_refused_body_ends_has_a_name);
    return test_summary("wa_send");
}
