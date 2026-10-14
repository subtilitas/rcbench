/*
 * The bring-up diagnosis: each fault is constructed here rather than waited
 * for on a bench.
 *
 * The property that matters is the order.  A broken link shows several
 * symptoms at the same time (a silent one also times out on every poll), and
 * the module names the cause rather than the loudest consequence.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_bringup.h"
#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"

/* A link that is working: a hundred polls, a hundred replies, both ends
 * agreeing, and the protocol this tree speaks. */
static link_bringup_t healthy(void)
{
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    b.polls = 100;
    b.replies = 100;
    b.have_status = true;
    b.dev_frames = 100;
    b.have_identity = true;
    b.proto_major = LINK_PROTOCOL_MAJOR;
    b.proto_minor = LINK_PROTOCOL_MINOR;
    return b;
}

TEST_CASE(a_working_link_is_reported_as_working)
{
    const link_bringup_t b = healthy();
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_OK);
    CHECK_STR_EQ(link_diag_text(LINK_DIAG_OK), "link healthy");
}

/* No verdict before 4 polls have been made. */
TEST_CASE(nothing_is_concluded_before_enough_has_been_asked)
{
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    for (uint32_t n = 0; n < 4; ++n) {
        b.polls = n;
        b.replies = 0;
        if (link_bringup_diagnose(&b) != LINK_DIAG_OK) {
            T_FAIL("a verdict after only %u polls", n);
        }
    }
    b.polls = 4;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_SILENT);
}

/*
 * The headline case, and the one whose ordering matters most: a silent link
 * times out on every poll, so timeouts are the loudest number in the struct
 * and the least useful thing to report.
 */
TEST_CASE(silence_is_reported_as_silence_and_not_as_timeouts)
{
    link_bringup_t b = healthy();
    b.replies = 0;
    b.timeouts = 100;
    b.have_status = false;   /* a status read cannot have succeeded either */
    b.have_identity = false;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_SILENT);
    CHECK_STR_EQ(link_diag_text(LINK_DIAG_SILENT), "no reply to any poll");
    CHECK(link_diag_hint(LINK_DIAG_SILENT)[0] != '\0');
}

/*
 * The diagnosis only both ends together can make.  The coprocessor decoded
 * every request; the panel heard no answers.  Nothing in the panel's own
 * counters distinguishes this from a dead coprocessor, and the two want
 * completely different things done about them.
 */
TEST_CASE(requests_landing_without_answers_is_the_return_path)
{
    link_bringup_t b = healthy();
    b.replies = 2;          /* almost nothing came back ... */
    b.dev_frames = 100;     /* ... but every request was decoded */
    b.timeouts = 98;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_REPLIES_LOST);
    CHECK(link_diag_hint(LINK_DIAG_REPLIES_LOST)[0] != '\0');

    /* Without the coprocessor's status the same panel counters are an
     * intermittent link, not a return-path fault. */
    b.have_status = false;
    CHECK(link_bringup_diagnose(&b) != LINK_DIAG_REPLIES_LOST);
}

/* A coprocessor that decoded nothing either is not a return-path fault --
 * that is a link that is down in both directions. */
TEST_CASE(neither_end_hearing_anything_is_not_a_return_path_fault)
{
    link_bringup_t b = healthy();
    b.replies = 0;
    b.dev_frames = 0;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_SILENT);
}

/*
 * The partial case: when the coprocessor misses requests too, the outbound
 * direction is failing and the return path is not the fault.  Reported as
 * intermittent (both directions working sometimes).
 */
TEST_CASE(both_directions_falling_short_is_not_the_return_path)
{
    link_bringup_t b = healthy();
    b.replies = 60;
    b.dev_frames = 62;   /* the far end is not hearing the requests either */
    b.timeouts = 40;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_INTERMITTENT);

    /* The distinguishing fact is the far end's count and nothing else: with
     * the requests all landing, the identical panel-side numbers mean the
     * return path. */
    b.dev_frames = 100;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_REPLIES_LOST);
}

TEST_CASE(a_protocol_skew_outranks_the_symptoms_it_causes)
{
    link_bringup_t b = healthy();
    b.proto_major = (uint16_t)(LINK_PROTOCOL_MAJOR + 1);
    /* A skew produces corruption and staleness as side effects; the skew is
     * still what wants fixing. */
    b.rx_crc_errors = 40;
    b.mismatches = 12;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_PROTOCOL_MISMATCH);

    /* A minor difference is compatible by construction and is not a fault. */
    b.proto_major = LINK_PROTOCOL_MAJOR;
    b.proto_minor = (uint16_t)(LINK_PROTOCOL_MINOR + 7);
    b.rx_crc_errors = 0;
    b.mismatches = 0;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_OK);
}

TEST_CASE(corruption_is_seen_from_whichever_end_sees_it)
{
    link_bringup_t b = healthy();
    b.rx_crc_errors = 3;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_CORRUPT);

    /* And the other direction: the requests are arriving corrupt, which the
     * panel can only learn by asking. */
    b = healthy();
    b.dev_crc_errors = 3;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_CORRUPT);
}

TEST_CASE(late_answers_are_reported_as_late_rather_than_as_loss)
{
    link_bringup_t b = healthy();
    b.mismatches = 5;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_STALE);
    CHECK(link_diag_hint(LINK_DIAG_STALE)[0] != '\0');
}

/* A link that mostly works is a different report from one that does not, and
 * the boundary is where the difference stops being one dropped frame. */
TEST_CASE(an_occasional_drop_is_intermittent_and_not_a_fault)
{
    link_bringup_t b = healthy();
    b.replies = 96;              /* 4% lost */
    b.dev_frames = 100;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_OK);

    b.replies = 80;              /* 20% lost, with the requests landing */
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_REPLIES_LOST);

    /* The same loss with no word from the far end is only intermittent. */
    b.have_status = false;
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_INTERMITTENT);
}

/* -------------------------------------------------------- round-trip stats */

TEST_CASE(the_round_trip_keeps_its_extremes_and_its_mean)
{
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    const uint32_t us[] = { 400, 380, 900, 410, 390 };
    for (size_t i = 0; i < sizeof(us) / sizeof(us[0]); ++i) {
        link_bringup_add_rtt(&b, us[i]);
    }
    CHECK_EQ(b.rt_samples, 5);
    CHECK_EQ(b.rt_min_us, 380);
    CHECK_EQ(b.rt_max_us, 900);
    /* The max is what matters for a turnaround: the worst round trip is what
     * a poll period has to clear, not the typical one. */
    CHECK(b.rt_mean_us >= 490 && b.rt_mean_us <= 500);
}

TEST_CASE(the_first_sample_is_the_min_the_max_and_the_mean)
{
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    link_bringup_add_rtt(&b, 742);
    CHECK_EQ(b.rt_samples, 1);
    CHECK_EQ(b.rt_min_us, 742);
    CHECK_EQ(b.rt_max_us, 742);
    CHECK_EQ(b.rt_mean_us, 742);
}

/*
 * A bench runs for days.  A 32-bit microsecond sum overflows in about an hour
 * of polling, so the mean is kept incrementally.
 */
TEST_CASE(the_mean_survives_a_session_longer_than_a_sum_would)
{
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    for (uint32_t i = 0; i < 200000u; ++i) {
        link_bringup_add_rtt(&b, 4000);
    }
    CHECK_EQ(b.rt_samples, 200000);
    /* 200,000 x 4000 us is 8e8 us, which a 32-bit sum would have wrapped. */
    CHECK(b.rt_mean_us >= 3990 && b.rt_mean_us <= 4000);
    CHECK_EQ(b.rt_max_us, 4000);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    link_bringup_add_rtt(NULL, 100);
    CHECK_EQ(link_bringup_diagnose(NULL), LINK_DIAG_OK);
    CHECK(link_diag_text(LINK_DIAG_OK)[0] != '\0');
    CHECK(link_diag_hint((link_diag_t)99)[0] == '\0');
}

/*
 * Every diagnosis has words and a hint, so the panel never shows a fault it
 * cannot describe.  The logic tests above prove which code comes out; this
 * proves each code can be turned into something a person reads.
 */
TEST_CASE(every_diagnosis_has_text_and_a_hint)
{
    const link_diag_t all[] = {
        LINK_DIAG_OK, LINK_DIAG_SILENT, LINK_DIAG_PROTOCOL_MISMATCH,
        LINK_DIAG_REPLIES_LOST, LINK_DIAG_CORRUPT, LINK_DIAG_STALE,
        LINK_DIAG_INTERMITTENT,
    };
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        const char *text = link_diag_text(all[i]);
        const char *hint = link_diag_hint(all[i]);
        CHECK(text != NULL && text[0] != '\0');   /* every code has text */
        CHECK(hint != NULL);                        /* OK has the hint "" */
        if (all[i] != LINK_DIAG_OK) {
            CHECK(hint[0] != '\0');                /* every fault has a hint */
        }
    }
    /* An out-of-range value falls to the healthy default rather than reading
     * off the end of the table. */
    CHECK(link_diag_text((link_diag_t)99)[0] != '\0');
    CHECK(link_diag_hint((link_diag_t)99) != NULL);
}

/* ------------------------------------------------------------ status page */

/*
 * A coprocessor's STATUS page, as wide as its minor makes it, and the refusal
 * count it keeps the way the firmware keeps it: in the registers before each
 * read.
 */
typedef struct {
    uint16_t regs[LINK_ST_COUNT];
    uint32_t refused;
} copro_t;

static copro_t     g_co;
static link_dev_t  g_dev;
static link_host_t g_host;

static void st_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    copro_t *c = (copro_t *)ctx;
    c->regs[LINK_ST_REFUSED_LO] = (uint16_t)(c->refused & 0xFFFFu);
    c->regs[LINK_ST_REFUSED_HI] = (uint16_t)(c->refused >> 16);
    memcpy(out, c->regs + off, (size_t)n * 2u);
}

static const link_page_t k_status_new[] = {
    { LINK_PAGE_STATUS, LINK_ST_COUNT, st_read, NULL },
};
static const link_page_t k_status_old[] = {
    { LINK_PAGE_STATUS, LINK_ST_COUNT_V420, st_read, NULL },
};

static void copro(bool old)
{
    memset(&g_co, 0, sizeof(g_co));
    g_co.regs[LINK_ST_FAULTS]     = 0x0011u;
    g_co.regs[LINK_ST_FRAMES_LO]  = 0x5678u;
    g_co.regs[LINK_ST_FRAMES_HI]  = 0x0001u;
    g_co.regs[LINK_ST_CRC_ERRORS] = 3u;
    g_co.regs[LINK_ST_RESYNCS]    = 4u;
    link_dev_init(&g_dev, old ? k_status_old : k_status_new, 1u, &g_co, 0u);
    link_host_init(&g_host, 0u);
}

/*
 * A read of @p count STATUS registers, over CAN frames both ways.  Each
 * request frame goes through the coprocessor's counted decode, so a frame
 * it refuses is in the count the reply carries.
 */
static bool status_over_can(uint8_t count, link_msg_t *got)
{
    link_msg_t req;
    if (!link_host_read(&g_host, LINK_PAGE_STATUS, 0u, count, 1u, &req)) {
        return false;
    }
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(&req, out, LINK_CAN_MAX_FRAMES);
    link_msg_t part, reply;
    bool have = false;
    for (size_t i = 0; i < n; ++i) {
        if (link_can_decode_counted(&out[i], &part, &g_co.refused)) {
            have = link_dev_dispatch(&g_dev, &part, &reply, 1u);
        }
    }
    if (!have) {
        return false;
    }
    link_can_frame_t back[LINK_CAN_MAX_FRAMES];
    const size_t m = link_can_encode(&reply, back, LINK_CAN_MAX_FRAMES);
    bool answered = false;
    for (size_t k = 0; k < m; ++k) {
        if (link_can_decode(&back[k], &part)
            && link_host_accept(&g_host, &part, 1u, got)) {
            answered = true;
        }
    }
    return answered;
}

/* The page length a panel asks for follows the minor that answered. */
TEST_CASE(the_status_read_is_as_long_as_the_coprocessors_page)
{
    CHECK_EQ(link_bringup_status_count(0u), LINK_ST_COUNT_V420);
    CHECK_EQ(link_bringup_status_count(LINK_MINOR_REFUSED - 1u),
             LINK_ST_COUNT_V420);
    CHECK_EQ(link_bringup_status_count(LINK_MINOR_REFUSED), LINK_ST_COUNT);
    CHECK_EQ(link_bringup_status_count(LINK_PROTOCOL_MINOR), LINK_ST_COUNT);
    CHECK_EQ(link_bringup_status_count(0xFFFFu), LINK_ST_COUNT);
}

/*
 * The count crosses the link whole: both halves, low first, from 0x0001FFFF
 * where a half read alone would be wrong, and on to the 2^32 wrap.  A frame
 * the coprocessor refuses on the way is in the next answer.
 */
TEST_CASE(the_refusal_count_crosses_the_link_and_wraps)
{
    copro(false);
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    uint16_t faults = 0;
    link_msg_t st;
    memset(&st, 0, sizeof(st));

    CHECK(status_over_can(link_bringup_status_count(LINK_PROTOCOL_MINOR),
                          &st));
    CHECK_EQ(st.count, LINK_ST_COUNT);
    CHECK(link_bringup_take_status(&b, &st, &faults));
    CHECK(b.have_status);
    CHECK(b.have_refused);
    CHECK_EQ(b.dev_refused, 0u);
    CHECK_EQ(b.dev_frames, 0x00015678u);
    CHECK_EQ(b.dev_crc_errors, 3u);
    CHECK_EQ(b.dev_resyncs, 4u);
    CHECK_EQ(faults, 0x0011u);

    /* A READ that arrives as DLC 8, as from an ESP-IDF v5.5.0 panel. */
    link_can_frame_t bad;
    memset(&bad, 0, sizeof(bad));
    bad.id  = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_READ,
                          LINK_PAGE_STATUS, 0, LINK_ST_COUNT);
    bad.dlc = 8;
    link_msg_t part;
    CHECK(!link_can_decode_counted(&bad, &part, &g_co.refused));
    CHECK(status_over_can(LINK_ST_COUNT, &st));
    CHECK(link_bringup_take_status(&b, &st, NULL));
    CHECK_EQ(b.dev_refused, 1u);

    g_co.refused = 0x0001FFFFu;
    CHECK(status_over_can(LINK_ST_COUNT, &st));
    CHECK(link_bringup_take_status(&b, &st, NULL));
    CHECK_EQ(b.dev_refused, 0x0001FFFFu);

    g_co.refused = 0xFFFFFFFFu;
    CHECK(status_over_can(LINK_ST_COUNT, &st));
    CHECK(link_bringup_take_status(&b, &st, NULL));
    CHECK_EQ(b.dev_refused, 0xFFFFFFFFu);
    CHECK(!link_can_decode_counted(&bad, &part, &g_co.refused));
    CHECK(status_over_can(LINK_ST_COUNT, &st));
    CHECK(link_bringup_take_status(&b, &st, NULL));
    CHECK_EQ(b.dev_refused, 0u);
    CHECK(b.have_refused);
}

/*
 * A 4.20 coprocessor has 8 STATUS registers.  The 10-register read is
 * refused whole, which is why the panel asks by minor; the 8 it has carry
 * every counter but the refusals, and no count from an earlier coprocessor
 * survives.
 */
TEST_CASE(an_older_coprocessor_is_read_without_the_count)
{
    copro(true);
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    b.have_refused = true;      /* the coprocessor that answered before */
    b.dev_refused  = 9u;
    link_msg_t st;
    memset(&st, 0, sizeof(st));

    CHECK(status_over_can(LINK_ST_COUNT, &st));
    CHECK_EQ(st.op, LINK_OP_NACK);
    CHECK_EQ(st.regs[0], LINK_NACK_BAD_RANGE);
    CHECK(!link_bringup_take_status(&b, &st, NULL));
    CHECK_EQ(b.dev_refused, 9u);    /* nothing taken from a refusal */

    CHECK(status_over_can(link_bringup_status_count(LINK_MINOR_REFUSED - 1u),
                          &st));
    CHECK_EQ(st.op, LINK_OP_DATA);
    CHECK_EQ(st.count, LINK_ST_COUNT_V420);
    uint16_t faults = 0;
    CHECK(link_bringup_take_status(&b, &st, &faults));
    CHECK(b.have_status);
    CHECK(!b.have_refused);
    CHECK_EQ(b.dev_refused, 0u);
    CHECK_EQ(b.dev_frames, 0x00015678u);
    CHECK_EQ(faults, 0x0011u);
}

/* Only a DATA reply from register 0 with at least the 4.20 page is taken. */
TEST_CASE(a_status_reply_that_is_not_the_page_is_not_taken)
{
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    link_msg_t st;
    memset(&st, 0, sizeof(st));
    st.op    = LINK_OP_DATA;
    st.page  = LINK_PAGE_STATUS;
    st.count = LINK_ST_COUNT_V420 - 1u;
    CHECK(!link_bringup_take_status(&b, &st, NULL));
    st.count  = LINK_ST_COUNT;
    st.offset = 1u;
    CHECK(!link_bringup_take_status(&b, &st, NULL));
    st.offset = 0u;
    st.op     = LINK_OP_ACK;
    CHECK(!link_bringup_take_status(&b, &st, NULL));
    st.op     = LINK_OP_DATA;
    st.count  = LINK_MAX_REGS + 1u;
    CHECK(!link_bringup_take_status(&b, &st, NULL));
    CHECK(!b.have_status);
    CHECK(!link_bringup_take_status(&b, NULL, NULL));
    /* Nine registers: the low half without the high one is no count. */
    st.count = LINK_ST_COUNT - 1u;
    st.regs[LINK_ST_REFUSED_LO] = 5u;
    CHECK(link_bringup_take_status(&b, &st, NULL));
    CHECK(!b.have_refused);
    CHECK_EQ(b.dev_refused, 0u);
    /* A record is optional: the faults alone. */
    uint16_t faults = 0;
    st.regs[LINK_ST_FAULTS] = 0x0020u;
    CHECK(link_bringup_take_status(NULL, &st, &faults));
    CHECK_EQ(faults, 0x0020u);
}

int main(void)
{
    RUN(a_working_link_is_reported_as_working);
    RUN(nothing_is_concluded_before_enough_has_been_asked);
    RUN(silence_is_reported_as_silence_and_not_as_timeouts);
    RUN(requests_landing_without_answers_is_the_return_path);
    RUN(neither_end_hearing_anything_is_not_a_return_path_fault);
    RUN(both_directions_falling_short_is_not_the_return_path);
    RUN(a_protocol_skew_outranks_the_symptoms_it_causes);
    RUN(corruption_is_seen_from_whichever_end_sees_it);
    RUN(late_answers_are_reported_as_late_rather_than_as_loss);
    RUN(an_occasional_drop_is_intermittent_and_not_a_fault);
    RUN(the_round_trip_keeps_its_extremes_and_its_mean);
    RUN(the_first_sample_is_the_min_the_max_and_the_mean);
    RUN(the_mean_survives_a_session_longer_than_a_sum_would);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    RUN(every_diagnosis_has_text_and_a_hint);
    RUN(the_status_read_is_as_long_as_the_coprocessors_page);
    RUN(the_refusal_count_crosses_the_link_and_wraps);
    RUN(an_older_coprocessor_is_read_without_the_count);
    RUN(a_status_reply_that_is_not_the_page_is_not_taken);
    return test_summary("link_bringup");
}
