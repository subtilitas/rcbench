/*
 * Host unit tests for the number parsing of the log reader: splitting a value
 * from its unit, the evidence a value gives for the German or English decimal
 * convention, parsing under a known convention, units and time units.
 *
 * The cases follow logwiju's test/verify-numbers.mjs.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "log_fields.h"
#include "log_numbers.h"

#include "logfile_rig.h"

TEST_CASE(split_value_separates_number_and_unit)
{
    check_split("10.23A", "10.23", 1, 0, "A");
    check_split("22,34V", "22,34", 1, 0, "V");
    check_split("-3,5 \xC2\xB0" "C", "3,5", -1, 0, "\xC2\xB0" "C");
    check_split("12 %", "12", 1, 0, "%");
    check_split("1.5e3 m/s", "1.5", 1, 3, "m/s");

    log_value_t v;
    CHECK(!log_split_value("   ", &v));
    CHECK(!log_split_value("n/a", &v));
    CHECK(!log_split_value("", &v));
}

TEST_CASE(evidence_reports_what_a_value_proves)
{
    CHECK_EQ(log_evidence_of("1.234,56"), LOG_EV_DE);
    CHECK_EQ(log_evidence_of("1,234.56"), LOG_EV_EN);
    CHECK_EQ(log_evidence_of("10,23"), LOG_EV_DE);
    CHECK_EQ(log_evidence_of("1.5"), LOG_EV_EN);
    CHECK_EQ(log_evidence_of("1.234.567"), LOG_EV_DE);
    CHECK_EQ(log_evidence_of("1,234,567"), LOG_EV_EN);
    CHECK_EQ(log_evidence_of("1,234"), LOG_EV_AMBIGUOUS);
    CHECK_EQ(log_evidence_of("1.234"), LOG_EV_AMBIGUOUS);
    CHECK_EQ(log_evidence_of("42"), LOG_EV_NONE);
}

static log_conv_t infer(const char *const *vals, int n, log_ambig_t fallback,
                        bool *confident, bool *conflict)
{
    log_votes_t v;
    log_votes_reset(&v);
    for (int i = 0; i < n; ++i) {
        log_votes_add(&v, vals[i]);
    }
    return log_votes_result(&v, fallback, confident, conflict);
}

static double parsed(const char *raw, log_conv_t conv)
{
    double out = 0.0;
    if (!log_parse_with(raw, conv, &out, NULL, 0)) {
        T_FAIL("parse(\"%s\") rejected", raw);
        return 0.0;
    }
    return out;
}

TEST_CASE(one_value_settles_the_whole_column)
{
    /* "10,23" proves comma-decimal, so "1,234" must read 1.234 not 1234. */
    static const char *const de_col[] = { "1,234", "10,23", "5,678" };
    bool confident = false;
    log_conv_t c = infer(de_col, 3, LOG_AMBIG_THOUSANDS, &confident, NULL);
    CHECK_EQ(c, LOG_CONV_DE);
    CHECK(confident);
    CHECK_NEAR(parsed("1,234", c), 1.234, 1e-9);
    CHECK_NEAR(parsed("10,23", c), 10.23, 1e-9);
    CHECK_NEAR(parsed("5,678", c), 5.678, 1e-9);

    /* Same digits, but English evidence present. */
    static const char *const en_col[] = { "1,234", "10.23", "5,678" };
    c = infer(en_col, 3, LOG_AMBIG_THOUSANDS, NULL, NULL);
    CHECK_EQ(c, LOG_CONV_EN);
    CHECK_NEAR(parsed("1,234", c), 1234.0, 1e-9);
    CHECK_NEAR(parsed("5,678", c), 5678.0, 1e-9);
}

TEST_CASE(a_wholly_ambiguous_column_uses_the_fallback)
{
    static const char *const col[] = { "1,234", "5,678" };
    bool confident = true;
    CHECK_EQ(infer(col, 2, LOG_AMBIG_THOUSANDS, &confident, NULL), LOG_CONV_EN);
    CHECK(!confident);
    CHECK_EQ(infer(col, 2, LOG_AMBIG_DECIMAL, NULL, NULL), LOG_CONV_DE);
    CHECK_NEAR(parsed("1,234", LOG_CONV_EN), 1234.0, 1e-9);
    CHECK_NEAR(parsed("1,234", LOG_CONV_DE), 1.234, 1e-9);
}

TEST_CASE(conflicting_evidence_is_reported)
{
    static const char *const col[] = { "10,23", "10.23" };
    bool conflict = false;
    (void)infer(col, 2, LOG_AMBIG_THOUSANDS, NULL, &conflict);
    CHECK(conflict);
}

TEST_CASE(parse_with_a_known_convention)
{
    CHECK_NEAR(parsed("1.234,56", LOG_CONV_DE), 1234.56, 1e-9);
    CHECK_NEAR(parsed("1,234.56", LOG_CONV_EN), 1234.56, 1e-9);
    CHECK_NEAR(parsed("-1.234,5", LOG_CONV_DE), -1234.5, 1e-9);
    CHECK_NEAR(parsed("1 234,56", LOG_CONV_DE), 1234.56, 1e-9);
    CHECK_NEAR(parsed("12,345,678", LOG_CONV_EN), 12345678.0, 1e-9);
    CHECK_NEAR(parsed("12.345.678", LOG_CONV_DE), 12345678.0, 1e-9);

    char unit[8];
    double v = 0.0;
    CHECK(log_parse_with("22,34V", LOG_CONV_DE, &v, unit, sizeof(unit)));
    CHECK_NEAR(v, 22.34, 1e-9);
    CHECK_STR_EQ(unit, "V");

    CHECK(log_parse_loose("10,23", LOG_AMBIG_THOUSANDS, &v));
    CHECK_NEAR(v, 10.23, 1e-9);
}

TEST_CASE(the_wrong_convention_is_rejected_not_guessed)
{
    /* Stripping "." out of the German 1.234,56 and reading it as English gives
     * 1.23456, a wrong number.  The cell is rejected instead. */
    double v = 0.0;
    CHECK(!log_parse_with("1.234,56", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("1,234.56", LOG_CONV_DE, &v, NULL, 0));
    CHECK(!log_parse_with("1,23,456", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("1.2.3", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("10,23", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("10.23", LOG_CONV_DE, &v, NULL, 0));

    /* A plain integer reads the same either way. */
    CHECK_NEAR(parsed("42", LOG_CONV_DE), 42.0, 1e-9);
    CHECK_NEAR(parsed("42", LOG_CONV_EN), 42.0, 1e-9);
}

TEST_CASE(units_from_values_and_headers)
{
    log_unit_tally_t t;
    char unit[LOG_UNIT_MAX];
    bool mixed = true;

    log_unit_tally_reset(&t);
    log_unit_tally_add(&t, "A");
    log_unit_tally_add(&t, "A");
    log_unit_tally_add(&t, "");
    log_unit_dominant(&t, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "A");
    CHECK(!mixed);

    log_unit_tally_reset(&t);
    log_unit_tally_add(&t, "A");
    log_unit_tally_add(&t, "V");
    log_unit_tally_add(&t, "A");
    log_unit_dominant(&t, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "A");
    CHECK(mixed);

    char name[LOG_NAME_MAX];
    log_unit_from_header("voltage (V)", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "voltage");
    CHECK_STR_EQ(unit, "V");

    log_unit_from_header("current [A]", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "current");
    CHECK_STR_EQ(unit, "A");

    log_unit_from_header("altitude m", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "altitude");
    CHECK_STR_EQ(unit, "m");

    /* An axis index is part of the name, not a unit. */
    log_unit_from_header("gyroADC[0]", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "gyroADC[0]");
    CHECK_STR_EQ(unit, "");

    log_unit_from_header("motor", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "motor");
    CHECK_STR_EQ(unit, "");

    CHECK_NEAR(log_time_unit_scale("ms"), 1e-3, 1e-15);
    CHECK_NEAR(log_time_unit_scale("us"), 1e-6, 1e-18);
    CHECK_NEAR(log_time_unit_scale("min"), 60.0, 1e-9);
    CHECK_NEAR(log_time_unit_scale("nonsense"), 0.0, 1e-15);
}

TEST_CASE(blackbox_field_names_keep_their_grouping)
{
    CHECK_STR_EQ(log_field_meta("motor[0]").group, "Motor");
    CHECK_STR_EQ(log_field_meta("eRPM[3]").group, "RPM");
    CHECK_STR_EQ(log_field_meta("vbatLatest").group, "Power");
    CHECK_STR_EQ(log_field_meta("amperageLatest").unit, "A");
    CHECK_STR_EQ(log_field_meta("escTemperature[1]").group, "ESC");
    CHECK_STR_EQ(log_field_meta("stateFlags").group, "Flags");
    CHECK_STR_EQ(log_field_meta("GPS_numSat").group, "GPS");
    CHECK_STR_EQ(log_field_meta("axisP[0]").group, "PID");
    CHECK_STR_EQ(log_field_meta("something").group, "Other");
    /* An exact rule must not swallow a longer name. */
    CHECK_STR_EQ(log_field_meta("timeSinceArm").group, "Other");
}

TEST_CASE(exotic_grouping_characters_are_not_decimal_points)
{
    /* Swiss exporters group with an apostrophe, German ones with a no-break
     * or narrow no-break space.  None of those is ever a decimal point, so
     * they come out of the digits before anything else looks at them. */
    check_split("1\xC2\xA0" "234,56", "1234,56", 1, 0, "");
    check_split("1\xE2\x80\xAF" "234,56", "1234,56", 1, 0, "");
    check_split("1'234'567", "1234567", 1, 0, "");
    check_split("12\xC2\xA0%", "12", 1, 0, "%");

    CHECK_NEAR(parsed("1\xC2\xA0" "234,56", LOG_CONV_DE), 1234.56, 1e-9);
    CHECK_NEAR(parsed("1'234'567", LOG_CONV_EN), 1234567.0, 1e-9);

    /* A trailing no-break space is whitespace, not a unit. */
    check_split("22,34\xC2\xA0", "22,34", 1, 0, "");
}

TEST_CASE(exponents_and_degenerate_input)
{
    check_split("1.5e-3", "1.5", 1, -3, "");
    check_split("+42", "42", 1, 0, "");
    CHECK_NEAR(parsed("1.5e-3", LOG_CONV_EN), 0.0015, 1e-12);

    /* An 'e' with no exponent behind it is part of the unit, not a number. */
    check_split("5e", "5", 1, 0, "e");

    log_value_t v;
    CHECK(!log_split_value(".5", &v));  /* must start on a digit */
    CHECK(!log_split_value(NULL, &v));
    CHECK(!log_split_value("1", NULL));
    /* Longer than any real measurement, and longer than the digit buffer. */
    CHECK(!log_split_value("111111111111111111111111111111111111111111111", &v));

    CHECK_EQ(log_evidence_of(NULL), LOG_EV_NONE);
    CHECK(!log_is_well_formed(NULL, LOG_CONV_EN));
    CHECK(!log_is_well_formed("1.2.3", LOG_CONV_EN));
    CHECK(!log_is_well_formed("1,2345", LOG_CONV_EN));
    CHECK(!log_is_well_formed("1,23a", LOG_CONV_DE));
}

TEST_CASE(a_dot_grouped_ambiguous_file_follows_the_fallback)
{
    /* The mirror of the comma case: when only "1.234" is ever seen, reading
     * the dot as thousands means German. */
    static const char *const col[] = { "1.234", "5.678" };
    bool confident = true;
    CHECK_EQ(infer(col, 2, LOG_AMBIG_THOUSANDS, &confident, NULL), LOG_CONV_DE);
    CHECK(!confident);
    CHECK_EQ(infer(col, 2, LOG_AMBIG_DECIMAL, NULL, NULL), LOG_CONV_EN);
}

/*
 * A repeated separator is only grouping when the groups are groups.  A
 * version, an IP (Internet Protocol) address and a dotted date all have the
 * shape and none of them is a number; treating one as grouping evidence hands
 * the whole file to the wrong convention.
 */
TEST_CASE(a_version_or_an_address_casts_no_vote)
{
    CHECK(log_evidence_of("1.234.567") == LOG_EV_DE);
    CHECK(log_evidence_of("1.2.3") == LOG_EV_NONE);
    CHECK(log_evidence_of("192.168.0.1") == LOG_EV_NONE);
    CHECK(log_evidence_of("15.01.2024") == LOG_EV_NONE);
    CHECK(log_evidence_of("1.0234.567") == LOG_EV_NONE);
}

int main(void)
{
    RUN(split_value_separates_number_and_unit);
    RUN(evidence_reports_what_a_value_proves);
    RUN(one_value_settles_the_whole_column);
    RUN(a_wholly_ambiguous_column_uses_the_fallback);
    RUN(conflicting_evidence_is_reported);
    RUN(parse_with_a_known_convention);
    RUN(the_wrong_convention_is_rejected_not_guessed);
    RUN(units_from_values_and_headers);
    RUN(blackbox_field_names_keep_their_grouping);
    RUN(exotic_grouping_characters_are_not_decimal_points);
    RUN(exponents_and_degenerate_input);
    RUN(a_dot_grouped_ambiguous_file_follows_the_fallback);
    RUN(a_version_or_an_address_casts_no_vote);
    return test_summary("log_numbers");
}
