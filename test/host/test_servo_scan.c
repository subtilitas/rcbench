/**
 * @file
 * @brief servo_scan_decide(): a scanned line against the active servo
 *        (servo_active.h, PLAN-B 3.3).
 *
 * The failures this is about: a line that is almost an id taken as one
 * (lower case, a space, a missing dash, 4 or 6 characters after it); a
 * scan that changes the servo of a run under way; the scanner's repeat of
 * one label taken twice; and a repeat window that swallows a deliberate
 * rescan after the servo changed.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include <string.h>

#include "servo_active.h"

/** @brief A condition the rest of the case stands on: fail and return. */
#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)
/** @brief REQUIRE() of two integers equal. */
#define REQUIRE_EQ(a, b) REQUIRE((long)(a) == (long)(b))
/** @brief The active id, "" for none, so a string check never reads NULL. */
#define ACTIVE() (servo_active_id() != NULL ? servo_active_id() : "")

/** @brief No servo active, and a clock far from the last case's. */
static uint32_t fresh(void)
{
    static uint32_t t = 1000000u;
    CHECK(servo_active_clear(false));
    t += 100000u;
    return t;
}

TEST_CASE(an_id_sets_the_active_servo_from_a_scan)
{
    const uint32_t t = fresh();
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, false, t),
             SERVO_SCAN_SET);
    CHECK_STR_EQ(ACTIVE(), "KST_X10-0000A");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_SCAN);
    /* Another id replaces it. */
    CHECK_EQ(servo_scan_decide("KST_X10-0000B", false, false, t + 10u),
             SERVO_SCAN_SET);
    CHECK_STR_EQ(ACTIVE(), "KST_X10-0000B");
}

TEST_CASE(malformed_lines_change_nothing)
{
    static const char *const bad[] = {
        "",
        "https://example.com/KST_X10-0000A",
        "kst_x10-0000a",           /* lower case: no case is folded */
        "KST_X10-0000a",
        "KST_X100000A",            /* no dash */
        "KST_X10-000A",            /* 4 after the dash */
        "KST_X10-00000A",          /* 6 after the dash */
        "KST_X10-0000A ",          /* a trailing space */
        " KST_X10-0000A",
        "KST;X10-0000A",
        "KST/X10-0000A",
        "ABCDEFGHIJKLMNOPQR-00000",  /* a prefix of 18 */
        "-0000A",
    };
    const uint32_t t = fresh();
    REQUIRE_EQ(servo_scan_decide("SERVO-00001", false, false, t),
               SERVO_SCAN_SET);
    const uint32_t serial = servo_active_serial();
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK_EQ(servo_scan_decide(bad[i], false, false,
                                   t + 10u * (uint32_t)(i + 1u)),
                 SERVO_SCAN_MALFORMED);
        CHECK_STR_EQ(ACTIVE(), "SERVO-00001");
    }
    CHECK_EQ(servo_active_serial(), serial);
    CHECK_EQ(servo_scan_decide(NULL, false, false, t + 1000u),
             SERVO_SCAN_MALFORMED);
}

TEST_CASE(a_line_the_scanner_cut_is_too_long)
{
    char line[SERVO_SCAN_LINE_MAX + 1u];
    memset(line, 'A', sizeof(line) - 1u);
    line[SERVO_SCAN_LINE_MAX] = '\0';
    const uint32_t t = fresh();
    CHECK_EQ(servo_scan_decide(line, true, false, t), SERVO_SCAN_TOO_LONG);
    /* Cut even where the kept bytes are an id: the code was longer. */
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", true, false, t + 10u),
             SERVO_SCAN_TOO_LONG);
    CHECK(servo_active_id() == NULL);
}

TEST_CASE(during_a_run_another_id_is_refused_and_the_runs_own_dropped)
{
    const uint32_t t = fresh();
    REQUIRE_EQ(servo_scan_decide("KST_X10-0000A", false, false, t),
               SERVO_SCAN_SET);
    const uint32_t serial = servo_active_serial();
    CHECK_EQ(servo_scan_decide("KST_X10-0000B", false, true, t + 5000u),
             SERVO_SCAN_REFUSED_RUN);
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, true, t + 10000u),
             SERVO_SCAN_RUN_OWN);
    CHECK_STR_EQ(ACTIVE(), "KST_X10-0000A");
    CHECK_EQ(servo_active_serial(), serial);
    /* Nothing is kept for after the run. */
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, false, t + 20000u),
             SERVO_SCAN_SAME);
    CHECK_STR_EQ(ACTIVE(), "KST_X10-0000A");
}

TEST_CASE(a_run_without_a_servo_refuses_every_id)
{
    const uint32_t t = fresh();
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, true, t),
             SERVO_SCAN_REFUSED_RUN);
    CHECK(servo_active_id() == NULL);
}

TEST_CASE(the_same_line_at_2999ms_is_a_repeat_and_at_3000ms_already_active)
{
    const uint32_t t = fresh();
    REQUIRE_EQ(servo_scan_decide("KST_X10-0000A", false, false, t),
               SERVO_SCAN_SET);
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, false, t + 2999u),
             SERVO_SCAN_REPEAT);
    /* The window runs from the line taken, not from the repeat. */
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, false, t + 3000u),
             SERVO_SCAN_SAME);
    /* A malformed line repeats the same way. */
    CHECK_EQ(servo_scan_decide("nope", false, false, t + 4000u),
             SERVO_SCAN_MALFORMED);
    CHECK_EQ(servo_scan_decide("nope", false, false, t + 4100u),
             SERVO_SCAN_REPEAT);
}

TEST_CASE(the_repeat_window_holds_across_the_clock_wrap)
{
    (void)fresh();
    REQUIRE_EQ(servo_scan_decide("KST_X10-0000C", false, false, 0xFFFFFF00u),
               SERVO_SCAN_SET);
    CHECK_EQ(servo_scan_decide("KST_X10-0000C", false, false, 0x00000A00u),
             SERVO_SCAN_REPEAT);
    CHECK_EQ(servo_scan_decide("KST_X10-0000C", false, false, 0x00000AB8u),
             SERVO_SCAN_SAME);
}

TEST_CASE(a_change_of_the_servo_between_ends_the_repeat_window)
{
    const uint32_t t = fresh();
    REQUIRE_EQ(servo_scan_decide("KST_X10-0000A", false, false, t),
               SERVO_SCAN_SET);
    /* A clear, then the same label at once: set again, not dropped. */
    REQUIRE(servo_active_clear(false));
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, false, t + 500u),
             SERVO_SCAN_SET);
    /* NEW ID makes another servo; the label of the first at once. */
    REQUIRE(servo_active_set("KST_X10-0000Z", SERVO_ACTIVE_NEW, false));
    CHECK_EQ(servo_scan_decide("KST_X10-0000A", false, false, t + 900u),
             SERVO_SCAN_SET);
    CHECK_STR_EQ(ACTIVE(), "KST_X10-0000A");
}

TEST_CASE(a_scan_of_the_new_id_servo_makes_its_source_the_scan)
{
    const uint32_t t = fresh();
    REQUIRE(servo_active_set("KST_X10-0000N", SERVO_ACTIVE_NEW, false));
    CHECK_EQ(servo_scan_decide("KST_X10-0000N", false, false, t),
             SERVO_SCAN_SAME);
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_SCAN);
}

TEST_CASE(the_longest_ids_are_taken_whole)
{
    const uint32_t t = fresh();
    CHECK_EQ(servo_scan_decide("ABCDEFGHIJKLMNOPQ-ZZZZZ", false, false, t),
             SERVO_SCAN_SET);
    CHECK_STR_EQ(ACTIVE(), "ABCDEFGHIJKLMNOPQ-ZZZZZ");
    CHECK_EQ(servo_scan_decide("A-00000", false, false, t + 10u),
             SERVO_SCAN_SET);
}

int main(void)
{
    RUN(an_id_sets_the_active_servo_from_a_scan);
    RUN(malformed_lines_change_nothing);
    RUN(a_line_the_scanner_cut_is_too_long);
    RUN(during_a_run_another_id_is_refused_and_the_runs_own_dropped);
    RUN(a_run_without_a_servo_refuses_every_id);
    RUN(the_same_line_at_2999ms_is_a_repeat_and_at_3000ms_already_active);
    RUN(the_repeat_window_holds_across_the_clock_wrap);
    RUN(a_change_of_the_servo_between_ends_the_repeat_window);
    RUN(a_scan_of_the_new_id_servo_makes_its_source_the_scan);
    RUN(the_longest_ids_are_taken_whole);
    return test_summary("servo_scan");
}
