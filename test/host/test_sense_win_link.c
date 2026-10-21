/*
 * The panel's half of SENSE_WIN_A and SENSE_WIN_B (sense_win_link.h): the
 * pages read whole while a part they carry is enabled, every
 * SENSE_WIN_LINK_READ_MS and in turn; nothing sent to an IOmcu older than
 * 4.25 or after BAD_PAGE; and a window's channels handed out only from a
 * page whose last read holds that window.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "sense_win_link.h"

static sense_win_link_t s;

/* A page as the IOmcu would answer it: window @p window, part a online
 * for bit a of @p online, entry k with current @p ma + k and @p flags. */
static void page_regs(uint16_t *r, uint16_t window, uint16_t flags,
                      int16_t ma)
{
    memset(r, 0, LINK_SX_COUNT * sizeof(uint16_t));
    r[LINK_SX_WINDOW] = window;
    r[LINK_SX_FLAGS] = (uint16_t)(LINK_SX_HAVE | 0x03u);
    for (unsigned k = 0u; k < LINK_SX_PER_PAGE; ++k) {
        r[LINK_SX_E(k, LINK_SX_E_MEAN_MA)] = (uint16_t)(int16_t)(ma + (int)k);
        r[LINK_SX_E(k, LINK_SX_E_MAX_MA)]  = (uint16_t)(int16_t)(ma + 100);
        r[LINK_SX_E(k, LINK_SX_E_MEAN_MV)] = 5000u;
        r[LINK_SX_E(k, LINK_SX_E_MIN_MV)]  = (uint16_t)(4900u + k);
        r[LINK_SX_E(k, LINK_SX_E_FLAGS)]   = flags;
    }
}

/* One read at @p now, answered with @p regs (NULL: no answer). */
static bool read_one(uint32_t now, const uint16_t *regs, uint8_t *page)
{
    sense_win_link_op_t op;
    if (!sense_win_link_next(&s, now, &op)) {
        return false;
    }
    CHECK_EQ(op.n, LINK_SX_COUNT);
    if (page != NULL) {
        *page = op.page;
    }
    sense_win_link_done(&s, (regs != NULL) ? SENSE_WIN_LINK_ACK
                                           : SENSE_WIN_LINK_NO_ANSWER, regs);
    return true;
}

TEST_CASE(nothing_goes_to_an_older_iomcu_or_with_no_part)
{
    sense_win_link_init(&s);
    sense_win_link_want(&s, 0x0Fu);
    sense_win_link_op_t op;
    CHECK(!sense_win_link_next(&s, 0u, &op));        /* no link */
    sense_win_link_came_up(&s, 24u);
    CHECK(!sense_win_link_next(&s, 0u, &op));
    sense_win_link_came_up(&s, 25u);
    sense_win_link_want(&s, 0u);
    CHECK(!sense_win_link_next(&s, 0u, &op));
    CHECK(!sense_win_link_next(NULL, 0u, &op));
    CHECK(!sense_win_link_next(&s, 0u, NULL));
    bench_chw_t w;
    CHECK(!sense_win_link_window(&s, 1u, &w));
    CHECK_EQ(w.ok, 0u);
    CHECK_EQ(w.window, 1u);
    CHECK(!sense_win_link_window(NULL, 1u, &w));
    CHECK(!sense_win_link_window(&s, 1u, NULL));
}

/* Parts 0x40 and 0x41 read page A alone, 0x42 or 0x43 page B; both in
 * turn, each every SENSE_WIN_LINK_READ_MS. */
TEST_CASE(the_pages_a_map_needs_are_read_in_turn)
{
    sense_win_link_init(&s);
    sense_win_link_came_up(&s, 25u);
    sense_win_link_want(&s, 0x02u);
    uint16_t r[LINK_SX_COUNT];
    page_regs(r, 1u, LINK_SX_E_CURRENT, 0);
    uint8_t page = 0u;
    CHECK(read_one(1000u, r, &page));
    CHECK_EQ(page, LINK_PAGE_SENSE_WIN_A);
    CHECK(!read_one(1000u + SENSE_WIN_LINK_READ_MS - 1u, r, &page));
    CHECK(read_one(1000u + SENSE_WIN_LINK_READ_MS, r, &page));
    CHECK_EQ(page, LINK_PAGE_SENSE_WIN_A);
    sense_win_link_want(&s, 0x05u);
    CHECK(read_one(2000u, r, &page));
    CHECK_EQ(page, LINK_PAGE_SENSE_WIN_B);       /* never asked: first */
    CHECK(read_one(2000u, r, &page));
    CHECK_EQ(page, LINK_PAGE_SENSE_WIN_A);
    CHECK(!read_one(2000u, r, &page));
    /* Both due at once: A, then B. */
    CHECK(read_one(2000u + SENSE_WIN_LINK_READ_MS, r, &page));
    CHECK_EQ(page, LINK_PAGE_SENSE_WIN_A);
    CHECK(read_one(2000u + SENSE_WIN_LINK_READ_MS, r, &page));
    CHECK_EQ(page, LINK_PAGE_SENSE_WIN_B);
    /* An exchange in flight takes no second one; a done with none is
     * nothing. */
    sense_win_link_op_t op;
    CHECK(sense_win_link_next(&s, 3000u, &op));
    CHECK(!sense_win_link_next(&s, 3000u, &op));
    sense_win_link_done(&s, SENSE_WIN_LINK_ACK, r);
    sense_win_link_done(&s, SENSE_WIN_LINK_ACK, r);
    sense_win_link_done(NULL, SENSE_WIN_LINK_ACK, r);
}

/* A window's channels from the page that holds it; flags say which
 * quantity each has; a channel with neither is no reading. */
TEST_CASE(a_window_takes_the_channels_of_its_own_number)
{
    sense_win_link_init(&s);
    sense_win_link_came_up(&s, 25u);
    sense_win_link_want(&s, 0x0Fu);
    uint16_t a[LINK_SX_COUNT];
    uint16_t b[LINK_SX_COUNT];
    page_regs(a, 40u, (uint16_t)(LINK_SX_E_CURRENT | LINK_SX_E_VOLTAGE),
              -10);
    page_regs(b, 41u, LINK_SX_E_VOLTAGE, 300);
    b[LINK_SX_E(5, LINK_SX_E_FLAGS)] = 0u;          /* channel 11 off */
    CHECK(read_one(0u, a, NULL));                    /* A first */
    CHECK(read_one(0u, b, NULL));
    bench_chw_t w;
    CHECK(sense_win_link_window(&s, 40u, &w));
    CHECK_EQ(w.ok, 0x003Fu);
    CHECK_EQ(w.no_current, 0u);
    CHECK_EQ(w.no_voltage, 0u);
    CHECK_EQ(w.mean_ma[0], -10);
    CHECK_EQ(w.mean_ma[5], -5);
    CHECK_EQ(w.max_ma[3], 90);
    CHECK_EQ(w.min_mv[2], 4902u);
    CHECK(sense_win_link_window(&s, 41u, &w));
    CHECK_EQ(w.ok, 0x07C0u);
    CHECK_EQ(w.no_current, 0x07C0u);
    CHECK_EQ(w.mean_ma[0], 0);
    CHECK(!sense_win_link_window(&s, 42u, &w));
    /* A page without a closed window is no reading. */
    a[LINK_SX_FLAGS] = 0x03u;
    CHECK(read_one(SENSE_WIN_LINK_READ_MS, a, NULL));
    CHECK(!sense_win_link_window(&s, 40u, &w));
    /* No answer keeps the last read. */
    CHECK(read_one(SENSE_WIN_LINK_READ_MS, NULL, NULL));
    CHECK(sense_win_link_window(&s, 41u, &w));
}

/* A page no longer wanted, BAD_PAGE, and a lost link forget what was
 * read. */
TEST_CASE(a_page_gone_forgets_its_reads)
{
    sense_win_link_init(&s);
    sense_win_link_came_up(&s, 25u);
    sense_win_link_want(&s, 0x01u);
    uint16_t a[LINK_SX_COUNT];
    page_regs(a, 7u, LINK_SX_E_CURRENT, 1);
    CHECK(read_one(0u, a, NULL));
    bench_chw_t w;
    CHECK(sense_win_link_window(&s, 7u, &w));
    sense_win_link_want(&s, 0x04u);
    CHECK(!sense_win_link_window(&s, 7u, &w));
    sense_win_link_want(&s, 0x01u);
    CHECK(read_one(SENSE_WIN_LINK_READ_MS, a, NULL));
    CHECK(sense_win_link_window(&s, 7u, &w));
    sense_win_link_lost(&s);
    CHECK(!sense_win_link_window(&s, 7u, &w));
    CHECK_EQ(s.want, 0x01u);                     /* the map stays */
    sense_win_link_came_up(&s, 25u);
    sense_win_link_op_t op;
    CHECK(sense_win_link_next(&s, 0u, &op));
    sense_win_link_done(&s, (int)LINK_NACK_BAD_PAGE, NULL);
    CHECK(!sense_win_link_next(&s, SENSE_WIN_LINK_READ_MS, &op));
    CHECK(!sense_win_link_window(&s, 7u, &w));
    sense_win_link_init(NULL);
    sense_win_link_lost(NULL);
    sense_win_link_came_up(NULL, 25u);
    sense_win_link_want(NULL, 1u);
}

/* The read before the last answers for its window until a third window
 * replaces it; the same window read twice keeps the older one. */
TEST_CASE(the_read_before_answers_for_its_window)
{
    sense_win_link_init(&s);
    sense_win_link_came_up(&s, 25u);
    sense_win_link_want(&s, 0x01u);
    uint16_t a[LINK_SX_COUNT];
    page_regs(a, 20u, LINK_SX_E_CURRENT, 5);
    CHECK(read_one(0u, a, NULL));
    page_regs(a, 21u, LINK_SX_E_CURRENT, 6);
    CHECK(read_one(SENSE_WIN_LINK_READ_MS, a, NULL));
    bench_chw_t w;
    CHECK(sense_win_link_window(&s, 20u, &w));
    CHECK_EQ(w.mean_ma[0], 5);
    CHECK(sense_win_link_window(&s, 21u, &w));
    CHECK_EQ(w.mean_ma[0], 6);
    /* Window 21 read again: 20 stays the one before. */
    CHECK(read_one(2u * SENSE_WIN_LINK_READ_MS, a, NULL));
    CHECK(sense_win_link_window(&s, 20u, &w));
    page_regs(a, 22u, LINK_SX_E_CURRENT, 7);
    CHECK(read_one(3u * SENSE_WIN_LINK_READ_MS, a, NULL));
    CHECK(!sense_win_link_window(&s, 20u, &w));
    CHECK(sense_win_link_window(&s, 21u, &w));
    CHECK_EQ(w.mean_ma[0], 6);
    /* Window numbers wrap at 65536. */
    page_regs(a, 0xFFFFu, LINK_SX_E_CURRENT, 8);
    CHECK(read_one(4u * SENSE_WIN_LINK_READ_MS, a, NULL));
    page_regs(a, 0u, LINK_SX_E_CURRENT, 9);
    CHECK(read_one(5u * SENSE_WIN_LINK_READ_MS, a, NULL));
    CHECK(sense_win_link_window(&s, 0xFFFFu, &w));
    CHECK_EQ(w.mean_ma[0], 8);
    CHECK(sense_win_link_window(&s, 0u, &w));
    CHECK_EQ(w.mean_ma[0], 9);
    /* A page dropped from the map forgets both reads. */
    sense_win_link_want(&s, 0x04u);
    sense_win_link_want(&s, 0x01u);
    CHECK(!sense_win_link_window(&s, 0xFFFFu, &w));
    CHECK(!sense_win_link_window(&s, 0u, &w));
}

/* The read clock across the 32-bit millisecond wrap: still every
 * SENSE_WIN_LINK_READ_MS. */
TEST_CASE(the_reads_keep_their_rate_across_the_clock_wrap)
{
    sense_win_link_init(&s);
    sense_win_link_came_up(&s, 26u);
    sense_win_link_want(&s, 0x01u);
    uint16_t a[LINK_SX_COUNT];
    page_regs(a, 1u, LINK_SX_E_CURRENT, 0);
    const uint32_t t0 = UINT32_MAX - 20u;
    CHECK(read_one(t0, a, NULL));
    CHECK(!read_one(t0 + SENSE_WIN_LINK_READ_MS - 1u, a, NULL));
    CHECK(read_one(t0 + SENSE_WIN_LINK_READ_MS, a, NULL));
    /* An unanswered read is asked again a period on, not at once. */
    CHECK(read_one(t0 + 2u * SENSE_WIN_LINK_READ_MS, NULL, NULL));
    CHECK(!read_one(t0 + 2u * SENSE_WIN_LINK_READ_MS + 1u, a, NULL));
    bench_chw_t w;
    CHECK(sense_win_link_window(&s, 1u, &w));
}

/* FAST's channel from the ring's window over what the pages hold; a ring
 * window without readings clears it; no FAST, no new window, nothing. */
TEST_CASE(fast_comes_from_the_ring)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.servo_new = true;
    b.servo_window = 9u;
    b.servo_ok = 0x01u;
    b.servo_no_voltage = 0x01u;
    b.servo_mean_ma[0] = 321;
    b.servo_max_ma[0] = 400;
    b.servo_min_mv[0] = 4800u;
    bench_chw_t w;
    memset(&w, 0, sizeof(w));
    w.window = 9u;
    w.ok = 0x0011u;                     /* channel 4 from a page */
    w.mean_ma[4] = 5;
    sense_win_link_ring(&w, 4u, &b);
    CHECK_EQ(w.ok, 0x0011u);
    CHECK_EQ(w.no_voltage, 0x0010u);
    CHECK_EQ(w.no_current, 0u);
    CHECK_EQ(w.mean_ma[4], 321);
    CHECK_EQ(w.max_ma[4], 400);
    CHECK_EQ(w.min_mv[4], 4800u);
    b.servo_no_voltage = 0u;
    b.servo_no_current = 0x01u;
    sense_win_link_ring(&w, 4u, &b);
    CHECK_EQ(w.no_current, 0x0010u);
    CHECK_EQ(w.no_voltage, 0u);
    b.servo_ok = 0u;
    sense_win_link_ring(&w, 4u, &b);
    CHECK_EQ(w.ok, 0x0001u);
    CHECK_EQ(w.no_current, 0u);
    CHECK_EQ(w.no_voltage, 0u);
    CHECK_EQ(w.mean_ma[4], 0);
    b.servo_ok = 0x01u;
    sense_win_link_ring(&w, LINK_SM_FAST_NONE, &b);
    CHECK_EQ(w.ok, 0x0001u);
    b.servo_new = false;
    sense_win_link_ring(&w, 4u, &b);
    CHECK_EQ(w.ok, 0x0001u);
    sense_win_link_ring(NULL, 4u, &b);
    sense_win_link_ring(&w, 4u, NULL);
}

/*
 * The control task's poll, every @p period_ms: SERVO_WIN's windows taken,
 * a row each, then the pages read, as the panel orders them, at an offset
 * in the pass of 0 to 6 ms that shrinks from poll to poll; each row's
 * groups attached as the panel does.  The IOmcu closes window n at 50 n
 * ms.  Counts the rows in 20 s and those without FAST's channel 0 and
 * without page A's channel 3.
 */
static void poll_rows(uint32_t period_ms, unsigned *rows, unsigned *no_fast,
                      unsigned *no_other)
{
    sense_win_link_init(&s);
    sense_win_link_came_up(&s, 25u);
    sense_win_link_want(&s, 0x03u);
    *rows = *no_fast = *no_other = 0u;
    uint16_t taken = 20u;
    unsigned k = 0u;
    for (uint32_t p = 1000u; p < 21000u; p += period_ms, ++k) {
        bench_state_t row[4];
        unsigned n = 0u;
        for (uint16_t w = (uint16_t)(taken + 1u); w <= p / 50u && n < 4u;
             ++w) {
            memset(&row[n], 0, sizeof(row[n]));
            row[n].servo_new = true;
            row[n].servo_window = w;
            row[n].servo_ok = 0x01u;
            ++n;
            taken = w;
        }
        const uint32_t tp = p + 6u - 2u * (k % 4u);
        uint16_t r[LINK_SX_COUNT];
        page_regs(r, (uint16_t)(tp / 50u), LINK_SX_E_CURRENT, 0);
        while (read_one(tp, r, NULL)) {
        }
        for (unsigned i = 0u; i < n; ++i) {
            (void)sense_win_link_window(&s, row[i].servo_window,
                                        &row[i].chw);
            sense_win_link_ring(&row[i].chw, 0u, &row[i]);
            ++*rows;
            *no_fast += ((row[i].chw.ok & 0x0001u) == 0u) ? 1u : 0u;
            *no_other += ((row[i].chw.ok & 0x0008u) == 0u) ? 1u : 0u;
        }
    }
}

/* FAST is on every row.  The other channels miss only the windows no read
 * saw: at a 53 ms poll about 1 in 17, at a 50 ms poll none, the offset in
 * the pass shrinking or not. */
TEST_CASE(every_poll_reads_the_pages_after_the_ring)
{
    unsigned rows, no_fast, no_other;
    poll_rows(53u, &rows, &no_fast, &no_other);
    CHECK(rows >= 398u);
    CHECK_EQ(no_fast, 0u);
    CHECK(no_other <= rows / 17u + 1u);
    /* A gate of 50 ms would skip every read whose offset shrank: half the
     * rows. */
    poll_rows(50u, &rows, &no_fast, &no_other);
    CHECK(rows >= 398u);
    CHECK_EQ(no_fast, 0u);
    CHECK_EQ(no_other, 0u);
}

int main(void)
{
    RUN(nothing_goes_to_an_older_iomcu_or_with_no_part);
    RUN(the_pages_a_map_needs_are_read_in_turn);
    RUN(a_window_takes_the_channels_of_its_own_number);
    RUN(a_page_gone_forgets_its_reads);
    RUN(the_read_before_answers_for_its_window);
    RUN(the_reads_keep_their_rate_across_the_clock_wrap);
    RUN(fast_comes_from_the_ring);
    RUN(every_poll_reads_the_pages_after_the_ring);
    return test_summary("sense_win_link");
}
