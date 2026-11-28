/*
 * The Dial's battery (shared/dial/dial_batt.c): the share the faces draw,
 * and the Guition JC3636K518C's supply reading with its USB detection,
 * LOW and EMPTY.
 *
 * The failures this is about: a USB rail shown as a full cell; a USB host
 * with no reading left as a cell; a cell's LOW or EMPTY set by one dip or
 * cleared by one spike; a flag that flickers at its threshold; a sample
 * spike that moves the reading; and LOW or EMPTY that stays on USB.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "dial_batt.h"

/**
 * @brief Step the supply @p n times with one rail.
 * @param s    the supply
 * @param mv   the rail
 * @param host a USB host is there
 * @param n    readings
 */
static void steps(dk518_supply_t *s, uint16_t mv, bool host, int n)
{
    for (int i = 0; i < n; ++i) {
        dk518_supply_step(s, mv, host);
    }
}

TEST_CASE(the_share_is_linear_from_3300mv_to_4200mv_and_clamped)
{
    CHECK_EQ(dial_batt_pct(0), 0);
    CHECK_EQ(dial_batt_pct(3300), 0);
    CHECK_EQ(dial_batt_pct(3301), 0);
    CHECK_EQ(dial_batt_pct(3750), 50);
    CHECK_EQ(dial_batt_pct(3920), 68);
    CHECK_EQ(dial_batt_pct(4199), 99);
    CHECK_EQ(dial_batt_pct(4200), 100);
    CHECK_EQ(dial_batt_pct(65535), 100);
}

TEST_CASE(a_reading_drops_its_extremes_and_doubles_the_mean)
{
    uint16_t pin[DK518_SAMPLES];
    for (unsigned i = 0; i < DK518_SAMPLES; ++i) {
        pin[i] = 1900;
    }
    CHECK_EQ(dk518_rail_mv(pin), 3800);
    pin[3] = 3300;  /* one spike up */
    pin[9] = 0;     /* one spike down */
    CHECK_EQ(dk518_rail_mv(pin), 3800);
    for (unsigned i = 0; i < DK518_SAMPLES; ++i) {
        pin[i] = 65535;
    }
    CHECK_EQ(dk518_rail_mv(pin), 65535);
}

TEST_CASE(a_cell_reads_its_voltage)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    CHECK(!s.known);
    dk518_supply_step(&s, 3920, false);
    CHECK(s.known);
    CHECK(!s.usb);
    CHECK_EQ(s.mv, 3920);
    dk518_supply_step(&s, DK518_USB_MV, false);  /* a full cell's top */
    CHECK(s.known);
    CHECK_EQ(s.mv, DK518_USB_MV);
}

TEST_CASE(usb_is_a_rail_above_4450mv_or_a_host)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    dk518_supply_step(&s, DK518_USB_MV + 1u, false);
    CHECK(s.usb);
    CHECK(!s.known);
    CHECK_EQ(s.mv, 0);
    dk518_supply_step(&s, 3900, true);
    CHECK(s.usb);
    CHECK(!s.known);
    dk518_supply_step(&s, 3900, false);
    CHECK(!s.usb);
    CHECK(s.known);
}

TEST_CASE(no_plausible_cell_below_2500mv_is_unknown)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    dk518_supply_step(&s, DK518_NO_CELL_MV - 1u, false);
    CHECK(!s.known);
    CHECK(!s.usb);
    CHECK_EQ(s.mv, 0);
    steps(&s, DK518_NO_CELL_MV - 1u, false, 5);
    CHECK(!s.low.on);
    CHECK(!s.empty.on);
    dk518_supply_step(&s, DK518_NO_CELL_MV, false);
    CHECK(s.known);
}

TEST_CASE(low_sets_after_three_readings_below_3500mv)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    steps(&s, 3499, false, 2);
    CHECK(!s.low.on);
    dk518_supply_step(&s, 3600, false);   /* a spike up resets the run */
    steps(&s, 3499, false, 2);
    CHECK(!s.low.on);
    dk518_supply_step(&s, 3499, false);
    CHECK(s.low.on);
    CHECK(!s.empty.on);
}

TEST_CASE(low_holds_between_3500mv_and_3600mv_and_ends_after_three_above)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    steps(&s, 3400, false, 3);
    CHECK(s.low.on);
    steps(&s, 3550, false, 10);
    CHECK(s.low.on);
    steps(&s, 3600, false, 2);
    dk518_supply_step(&s, 3499, false);   /* a dip resets the run */
    steps(&s, 3600, false, 2);
    CHECK(s.low.on);
    dk518_supply_step(&s, 3600, false);
    CHECK(!s.low.on);
}

TEST_CASE(a_reading_between_the_thresholds_keeps_the_run)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    steps(&s, 3499, false, 2);
    dk518_supply_step(&s, 3550, false);
    dk518_supply_step(&s, 3499, false);
    CHECK(s.low.on);
}

TEST_CASE(empty_sets_after_three_readings_below_3350mv)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    steps(&s, 3349, false, 2);
    CHECK(!s.empty.on);
    dk518_supply_step(&s, 3349, false);
    CHECK(s.empty.on);
    CHECK(s.low.on);
    steps(&s, 3400, false, 5);
    CHECK(s.empty.on);
    steps(&s, 3450, false, 3);
    CHECK(!s.empty.on);
    CHECK(s.low.on);
}

TEST_CASE(usb_ends_low_and_empty_and_their_runs)
{
    dk518_supply_t s;
    dk518_supply_init(&s);
    steps(&s, 3300, false, 3);
    CHECK(s.low.on);
    CHECK(s.empty.on);
    dk518_supply_step(&s, 4700, false);
    CHECK(!s.low.on);
    CHECK(!s.empty.on);
    steps(&s, 3300, false, 2);
    dk518_supply_step(&s, 3300, true);
    dk518_supply_step(&s, 3300, false);
    CHECK(!s.low.on);
}

int main(void)
{
    RUN(the_share_is_linear_from_3300mv_to_4200mv_and_clamped);
    RUN(a_reading_drops_its_extremes_and_doubles_the_mean);
    RUN(a_cell_reads_its_voltage);
    RUN(usb_is_a_rail_above_4450mv_or_a_host);
    RUN(no_plausible_cell_below_2500mv_is_unknown);
    RUN(low_sets_after_three_readings_below_3500mv);
    RUN(low_holds_between_3500mv_and_3600mv_and_ends_after_three_above);
    RUN(a_reading_between_the_thresholds_keeps_the_run);
    RUN(empty_sets_after_three_readings_below_3350mv);
    RUN(usb_ends_low_and_empty_and_their_runs);
    return test_summary("dial_batt");
}
