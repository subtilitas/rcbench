/*
 * The status byte, which carries two unrelated things in one octet and
 * overloads one of them.
 *
 * The two rules worth holding: 0xC0 is setpoint noise rather than a BEC
 * over-current that cannot happen, and a warning bit is a caution until the
 * state says otherwise.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "openyge.h"

static openyge_status_t decode(uint8_t b)
{
    openyge_status_t s;
    openyge_status_decode(b, &s);
    return s;
}

TEST_CASE(the_low_nibble_is_the_motor_state)
{
    CHECK_EQ(decode(0x0E).state, OPENYGE_ST_RUNNING_NORM);
    CHECK_STR_EQ(openyge_state_name(0x0E), "RUNNING");
    CHECK_STR_EQ(openyge_state_name(0x00), "DISARMED");
    CHECK_STR_EQ(openyge_state_name(0x0C), "WINDMILLING");
    /* The warning bits must not leak into the state. */
    CHECK_EQ(decode(0xB0 | 0x0C).state, OPENYGE_ST_WINDMILLING);
}

/* Reserved values are shown as themselves, not folded onto a neighbour: an
 * unknown state that displays as "RUNNING" is worse than one that displays as
 * a number. */
TEST_CASE(reserved_states_are_reported_as_unknown)
{
    const uint8_t reserved[] = { 0x03, 0x05, 0x0B, 0x0D, 0x0F };
    for (size_t i = 0; i < sizeof(reserved); ++i) {
        const openyge_status_t s = decode(reserved[i]);
        CHECK_EQ(s.state, reserved[i]);
        CHECK_EQ(s.state_known, false);
        CHECK_STR_EQ(openyge_state_name(reserved[i]), "?");
    }
    CHECK(decode(0x0E).state_known);
}

/*
 * The trap.  0x80|0x40 would read as "BEC over-current", which cannot happen,
 * so it means the ESC's *input signal* is dirty.  Decoding it as a BEC fault
 * sends somebody after a short that is not there.
 */
TEST_CASE(the_overloaded_warning_nibble_is_setpoint_noise)
{
    const openyge_status_t s = decode(0xC0 | OPENYGE_ST_RUNNING_NORM);
    CHECK(s.setpoint_noise);
    CHECK_EQ(s.state, OPENYGE_ST_RUNNING_NORM);
    /* And it is not reported as any kind of over-current or BEC problem. */
    CHECK_EQ(s.warn_overcurrent, false);
    CHECK_EQ(s.fault_overcurrent, false);
    CHECK_EQ(s.any_fault, false);
}

TEST_CASE(bit_seven_selects_whether_the_warning_is_about_the_bec)
{
    const openyge_status_t esc = decode(0x20 | OPENYGE_ST_RUNNING_NORM);
    CHECK_EQ(esc.subject, OPENYGE_SUBJECT_ESC);
    CHECK(esc.warn_overtemp);
    CHECK_EQ(esc.setpoint_noise, false);

    const openyge_status_t bec = decode(0xA0 | OPENYGE_ST_RUNNING_NORM);
    CHECK_EQ(bec.subject, OPENYGE_SUBJECT_BEC);
    CHECK(bec.warn_overtemp);

    /* 0x80 alone: the subject is the BEC and there is nothing wrong with it. */
    const openyge_status_t none = decode(0x80 | OPENYGE_ST_RUNNING_NORM);
    CHECK_EQ(none.subject, OPENYGE_SUBJECT_BEC);
    CHECK_EQ(none.warn_overtemp, false);
    CHECK_EQ(none.any_fault, false);
}

/*
 * Over-voltage is the condition with no flag of its own: it is the absence of
 * warnings while the power is cut.  A decoder that only reports bits misses
 * it entirely.
 */
TEST_CASE(power_cut_with_no_warnings_is_an_overvoltage_fault)
{
    const openyge_status_t s = decode(OPENYGE_ST_POWER_CUT);
    CHECK(s.fault_overvoltage);
    CHECK(s.any_fault);

    /* Power cut *with* a warning is that warning's fault, not over-voltage. */
    const openyge_status_t hot = decode(0x20 | OPENYGE_ST_POWER_CUT);
    CHECK_EQ(hot.fault_overvoltage, false);
    CHECK(hot.fault_overtemp);
}

/* The same bit is a caution while running and a fault once power is cut. */
TEST_CASE(a_warning_becomes_a_fault_only_with_the_state)
{
    /* Both of the bits qualified by POWER_CUT, because they are qualified
     * separately and an earlier version of this case checked only one. */
    const uint8_t bits[] = { 0x20, 0x40 };
    for (size_t i = 0; i < sizeof(bits); ++i) {
        const openyge_status_t running =
            decode(bits[i] | OPENYGE_ST_RUNNING_NORM);
        if (running.any_fault) {
            T_FAIL("0x%02X while running was reported as a fault", bits[i]);
        }
        const openyge_status_t cut = decode(bits[i] | OPENYGE_ST_POWER_CUT);
        if (!cut.any_fault) {
            T_FAIL("0x%02X with power cut was not reported as a fault",
                   bits[i]);
        }
    }

    CHECK(decode(0x40 | OPENYGE_ST_RUNNING_NORM).warn_overcurrent);
    CHECK_EQ(decode(0x40 | OPENYGE_ST_RUNNING_NORM).fault_overcurrent, false);
    CHECK(decode(0x40 | OPENYGE_ST_POWER_CUT).fault_overcurrent);
    CHECK(decode(0x20 | OPENYGE_ST_RUNNING_NORM).warn_overtemp);
    CHECK_EQ(decode(0x20 | OPENYGE_ST_RUNNING_NORM).fault_overtemp, false);
    CHECK(decode(0x20 | OPENYGE_ST_POWER_CUT).fault_overtemp);
}

/* Undervoltage is the one qualified by a threshold rather than by a single
 * state: it is a fault below STARTING, which is where a pack too flat to spin
 * the motor shows up. */
TEST_CASE(undervoltage_is_a_fault_only_below_starting)
{
    for (uint8_t st = 0x0; st <= 0x0F; ++st) {
        const openyge_status_t s = decode(0x10 | st);
        CHECK(s.warn_undervoltage);
        if (st < OPENYGE_ST_STARTING) {
            if (!s.fault_undervoltage) {
                T_FAIL("state 0x%X: undervoltage should be a fault", st);
            }
        } else if (s.fault_undervoltage) {
            T_FAIL("state 0x%X: undervoltage should be a caution only", st);
        }
    }
}

TEST_CASE(a_clean_running_esc_reports_nothing_at_all)
{
    const openyge_status_t s = decode(OPENYGE_ST_RUNNING_NORM);
    CHECK_EQ(s.any_fault, false);
    CHECK_EQ(s.setpoint_noise, false);
    CHECK_EQ(s.warn_undervoltage, false);
    CHECK_EQ(s.warn_overtemp, false);
    CHECK_EQ(s.warn_overcurrent, false);
    CHECK_EQ(s.subject, OPENYGE_SUBJECT_ESC);
}

/* Nothing in the byte may crash the decoder or leave it half-filled. */
TEST_CASE(every_one_of_the_256_values_decodes)
{
    for (int b = 0; b <= 0xFF; ++b) {
        const openyge_status_t s = decode((uint8_t)b);
        CHECK_EQ(s.state, (uint8_t)(b & 0x0F));
        if (s.setpoint_noise && (uint8_t)(b & 0xF0) != 0xC0) {
            T_FAIL("0x%02X reported setpoint noise", b);
        }
        const bool derived = s.fault_overvoltage || s.fault_undervoltage
                             || s.fault_overtemp || s.fault_overcurrent;
        if (derived != s.any_fault) {
            T_FAIL("0x%02X: any_fault disagrees with the individual faults", b);
        }
    }
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    openyge_status_decode(0xFF, NULL);
    CHECK_STR_EQ(openyge_state_name(0xFF), "?");
}

int main(void)
{
    RUN(the_low_nibble_is_the_motor_state);
    RUN(reserved_states_are_reported_as_unknown);
    RUN(the_overloaded_warning_nibble_is_setpoint_noise);
    RUN(bit_seven_selects_whether_the_warning_is_about_the_bec);
    RUN(power_cut_with_no_warnings_is_an_overvoltage_fault);
    RUN(a_warning_becomes_a_fault_only_with_the_state);
    RUN(undervoltage_is_a_fault_only_below_starting);
    RUN(a_clean_running_esc_reports_nothing_at_all);
    RUN(every_one_of_the_256_values_decodes);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("openyge_status");
}
