/**
 * @file test_servo_active.c
 * @brief The active servo (servo_active.h): set and cleared with no run
 *        active and refused with one, each source, a malformed id, the
 *        printed flag, and the serial counting each change and nothing
 *        else.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "servo_active.h"
#include "servo_id.h"

/** @brief No servo active, as after power-on. */
static void none(void)
{
    CHECK(servo_active_clear(false));
    CHECK(servo_active_id() == NULL);
}

TEST_CASE(power_on_has_no_active_servo)
{
    CHECK(servo_active_id() == NULL);
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_NONE);
    CHECK(!servo_active_printed());
    CHECK_EQ(servo_active_serial(), 0u);
}

TEST_CASE(a_servo_is_set_and_cleared_with_no_run_active)
{
    none();
    const uint32_t s0 = servo_active_serial();
    CHECK(servo_active_set("kst_x10-0000a", SERVO_ACTIVE_SCAN, false));
    CHECK_STR_EQ(servo_active_id(), "KST_X10-0000A");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_SCAN);
    CHECK_EQ(servo_active_serial(), s0 + 1u);

    CHECK(servo_active_set("KST_X10-0000B", SERVO_ACTIVE_NEW, false));
    CHECK_STR_EQ(servo_active_id(), "KST_X10-0000B");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_NEW);
    CHECK_EQ(servo_active_serial(), s0 + 2u);

    CHECK(servo_active_set("KST X10 LEFT WING-ZZZZZ", SERVO_ACTIVE_PICK,
                           false));
    CHECK_STR_EQ(servo_active_id(), "KST X10 LEFT WING-ZZZZZ");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_PICK);
    CHECK_EQ(servo_active_serial(), s0 + 3u);

    CHECK(servo_active_clear(false));
    CHECK(servo_active_id() == NULL);
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_NONE);
    CHECK_EQ(servo_active_serial(), s0 + 4u);
    /* A clear with none active changes nothing. */
    CHECK(servo_active_clear(false));
    CHECK_EQ(servo_active_serial(), s0 + 4u);
}

TEST_CASE(a_run_active_refuses_every_change)
{
    none();
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_SCAN, false));
    const uint32_t s0 = servo_active_serial();
    CHECK(!servo_active_set("KST-00002", SERVO_ACTIVE_SCAN, true));
    CHECK(!servo_active_set("KST-00002", SERVO_ACTIVE_NEW, true));
    CHECK(!servo_active_set("KST-00002", SERVO_ACTIVE_PICK, true));
    CHECK(!servo_active_set("KST-00001", SERVO_ACTIVE_NEW, true));
    CHECK(!servo_active_clear(true));
    CHECK_STR_EQ(servo_active_id(), "KST-00001");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_SCAN);
    CHECK_EQ(servo_active_serial(), s0);

    /* A run with no servo active keeps none. */
    none();
    const uint32_t s1 = servo_active_serial();
    CHECK(!servo_active_set("KST-00002", SERVO_ACTIVE_SCAN, true));
    CHECK(!servo_active_clear(true));
    CHECK(servo_active_id() == NULL);
    CHECK_EQ(servo_active_serial(), s1);
}

TEST_CASE(a_malformed_id_or_source_is_refused)
{
    none();
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_SCAN, false));
    const uint32_t s0 = servo_active_serial();
    CHECK(!servo_active_set("KST-0001", SERVO_ACTIVE_SCAN, false));
    CHECK(!servo_active_set("https://example.com", SERVO_ACTIVE_SCAN, false));
    CHECK(!servo_active_set("", SERVO_ACTIVE_SCAN, false));
    CHECK(!servo_active_set(NULL, SERVO_ACTIVE_SCAN, false));
    CHECK(!servo_active_set("KST-00002", SERVO_ACTIVE_NONE, false));
    CHECK(!servo_active_set("KST-00002", (servo_active_from_t)5, false));
    CHECK(!servo_active_set("KST-00002", (servo_active_from_t)-1, false));
    CHECK_STR_EQ(servo_active_id(), "KST-00001");
    CHECK_EQ(servo_active_serial(), s0);
}

TEST_CASE(the_same_id_again_keeps_its_print_and_takes_the_new_source)
{
    none();
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_NEW, false));
    CHECK(!servo_active_printed());
    uint32_t s0 = servo_active_serial();
    servo_active_set_printed();
    CHECK(servo_active_printed());
    CHECK_EQ(servo_active_serial(), s0 + 1u);
    servo_active_set_printed();                    /* printed twice */
    CHECK_EQ(servo_active_serial(), s0 + 1u);

    /* The same id from the same source: nothing changes. */
    s0 = servo_active_serial();
    CHECK(servo_active_set("kst-00001", SERVO_ACTIVE_NEW, false));
    CHECK_EQ(servo_active_serial(), s0);
    /* Scanned: the source changes, the print stays. */
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_SCAN, false));
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_SCAN);
    CHECK(servo_active_printed());
    CHECK_EQ(servo_active_serial(), s0 + 1u);
    /* Another servo starts unprinted. */
    CHECK(servo_active_set("KST-00002", SERVO_ACTIVE_SCAN, false));
    CHECK(!servo_active_printed());
    /* And back: a print is of the servo active since it became active. */
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_PICK, false));
    CHECK(!servo_active_printed());
    /* With none active there is nothing to print. */
    none();
    s0 = servo_active_serial();
    servo_active_set_printed();
    CHECK(!servo_active_printed());
    CHECK_EQ(servo_active_serial(), s0);
}

TEST_CASE(the_id_is_a_copy_not_the_callers_text)
{
    none();
    char text[SERVO_ID_SIZE] = "KST-00001";
    CHECK(servo_active_set(text, SERVO_ACTIVE_SCAN, false));
    (void)memcpy(text, "KST-9", 5u);
    CHECK_STR_EQ(servo_active_id(), "KST-00001");
}

int main(void)
{
    RUN(power_on_has_no_active_servo);
    RUN(a_servo_is_set_and_cleared_with_no_run_active);
    RUN(a_run_active_refuses_every_change);
    RUN(a_malformed_id_or_source_is_refused);
    RUN(the_same_id_again_keeps_its_print_and_takes_the_new_source);
    RUN(the_id_is_a_copy_not_the_callers_text);
    return test_summary("servo_active");
}
