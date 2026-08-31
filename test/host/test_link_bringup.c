/*
 * The bring-up diagnosis: each fault is constructed here rather than waited
 * for on a bench.
 *
 * The property that matters is the *order*. A broken link shows several
 * symptoms at once -- a silent one also times out on every poll -- and the
 * value of this module is naming the cause rather than the loudest
 * consequence.
 */
#include <string.h>

#include "greatest.h"

#include "link_bringup.h"
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

/* Before anything has been asked there is nothing to conclude, and a bring-up
 * report that cries fault on its first frame is one nobody reads. */
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

    /* And without the coprocessor's side of the story it must NOT claim this:
     * the same panel counters with no status read are just an intermittent
     * link, and guessing further would be inventing the evidence. */
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
 * And the partial version of the same thing, which is what makes the
 * dev_frames half of the test load-bearing: if the coprocessor is missing
 * requests too, the outbound direction is failing and the return path is
 * innocent. Reported as intermittent -- both directions working sometimes --
 * rather than as a direction line that is not releasing, which would send
 * somebody to the wrong end of the cable with a scope.
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
 * A bench runs for days.  A microsecond sum overflows 32 bits in about an
 * hour of polling, so the mean is kept incrementally -- and this is the test
 * that would have caught a sum.
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
        CHECK(text != NULL && text[0] != '\0');   /* every code says something */
        CHECK(hint != NULL);                        /* healthy has no hint, "" */
        if (all[i] != LINK_DIAG_OK) {
            CHECK(hint[0] != '\0');                /* every fault points somewhere */
        }
    }
    /* An out-of-range value falls to the healthy default rather than reading
     * off the end of the table. */
    CHECK(link_diag_text((link_diag_t)99)[0] != '\0');
    CHECK(link_diag_hint((link_diag_t)99) != NULL);
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
    return test_summary("link_bringup");
}
