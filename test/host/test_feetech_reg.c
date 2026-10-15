/*
 * The Feetech register tables (protocols/feetech/feetech_reg.c): byte
 * order per series, the tables by role, the rates of register 6, the
 * sign-and-magnitude values and the live block.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "feetech_reg.h"

TEST_CASE(two_byte_values_are_low_first_on_sts_and_high_first_on_scs)
{
    uint8_t b[2];

    feetech_put16(FEETECH_STS, b, 0x0518);
    CHECK_EQ(b[0], 0x18);
    CHECK_EQ(b[1], 0x05);
    CHECK_EQ(feetech_get16(FEETECH_STS, b), 0x0518);
    feetech_put16(FEETECH_SCS, b, 0x0518);
    CHECK_EQ(b[0], 0x05);
    CHECK_EQ(b[1], 0x18);
    CHECK_EQ(feetech_get16(FEETECH_SCS, b), 0x0518);
    /* The manual's reply 18 05: 1304 on STS, 6149 on SCS. */
    b[0] = 0x18;
    b[1] = 0x05;
    CHECK_EQ(feetech_get16(FEETECH_STS, b), 1304);
    CHECK_EQ(feetech_get16(FEETECH_SCS, b), 6149);
    feetech_put16(FEETECH_STS, b, 0xFFFF);
    CHECK_EQ(feetech_get16(FEETECH_SCS, b), 0xFFFF);
}

TEST_CASE(the_tables_hold_each_series_registers_and_ranges)
{
    feetech_reg_t r;

    CHECK(feetech_reg(FEETECH_STS, FEETECH_R_GOAL, &r));
    CHECK_EQ(r.addr, 42);
    CHECK_EQ(r.size, 2);
    CHECK_EQ(r.max, 4095);
    CHECK(!r.eeprom);
    CHECK(feetech_reg(FEETECH_SCS, FEETECH_R_GOAL, &r));
    CHECK_EQ(r.max, 1023);
    CHECK(feetech_reg(FEETECH_STS, FEETECH_R_SPEED, &r));
    CHECK_EQ(r.max, 3400);
    CHECK(feetech_reg(FEETECH_SCS, FEETECH_R_SPEED, &r));
    CHECK_EQ(r.max, 1000);
    CHECK(feetech_reg(FEETECH_STS, FEETECH_R_LOCK, &r));
    CHECK_EQ(r.addr, 55);
    CHECK(r.eeprom);
    CHECK(feetech_reg(FEETECH_SCS, FEETECH_R_LOCK, &r));
    CHECK_EQ(r.addr, 48);
    CHECK(feetech_reg(FEETECH_STS, FEETECH_R_MODE, &r));
    CHECK_EQ(r.addr, 33);
    CHECK_EQ(r.max, 3);
    CHECK(!feetech_reg(FEETECH_SCS, FEETECH_R_MODE, &r));
    CHECK(!feetech_reg(FEETECH_SCS, FEETECH_R_ACC, &r));
    CHECK(feetech_reg(FEETECH_STS, FEETECH_R_ACC, NULL));
    CHECK(!feetech_reg(FEETECH_SERIES_COUNT, FEETECH_R_ID, &r));
    CHECK(!feetech_reg(FEETECH_STS, FEETECH_R_COUNT, &r));
    /* Every EEPROM register lies below the RAM, the lock flag aside. */
    for (unsigned s = 0; s < FEETECH_SERIES_COUNT; ++s) {
        for (unsigned i = 0; i < FEETECH_R_COUNT; ++i) {
            if (feetech_reg((feetech_series_t)s, (feetech_role_t)i, &r)
                && i != FEETECH_R_LOCK) {
                CHECK_EQ(r.eeprom, r.addr < FEETECH_RAM_FIRST);
            }
        }
    }
    CHECK_EQ(feetech_lock_addr(FEETECH_SERIES_COUNT), 0);
    CHECK_EQ(feetech_live_bytes(FEETECH_SERIES_COUNT), 0);
    CHECK_EQ(feetech_position_max(FEETECH_STS), 4095);
    CHECK_EQ(feetech_position_max(FEETECH_SCS), 1023);
}

TEST_CASE(register_6_holds_eight_rates)
{
    CHECK_EQ(feetech_baud(0), 1000000u);
    CHECK_EQ(feetech_baud(1), 500000u);
    CHECK_EQ(feetech_baud(2), 250000u);
    CHECK_EQ(feetech_baud(3), 128000u);
    CHECK_EQ(feetech_baud(4), 115200u);
    CHECK_EQ(feetech_baud(5), 76800u);
    CHECK_EQ(feetech_baud(6), 57600u);
    CHECK_EQ(feetech_baud(7), 38400u);
    CHECK_EQ(feetech_baud(8), 0u);
    CHECK_EQ(feetech_baud(FEETECH_BAUD_DEFAULT), 1000000u);
}

TEST_CASE(sign_and_magnitude_round_trip_and_clamp)
{
    CHECK_EQ(feetech_sign_mag(0), 0);
    CHECK_EQ(feetech_sign_mag(1000), 1000);
    CHECK_EQ(feetech_sign_mag(-1000), 0x83E8);
    CHECK_EQ(feetech_from_sign_mag(0x83E8), -1000);
    CHECK_EQ(feetech_sign_mag(40000), 32767);
    CHECK_EQ(feetech_sign_mag(-40000), 0xFFFF);
    CHECK_EQ(feetech_from_sign_mag(0xFFFF), -32767);
    CHECK_EQ(feetech_from_sign_mag(0x8000), 0);
}

TEST_CASE(the_live_block_decodes_in_each_byte_order)
{
    uint8_t sts[FEETECH_LIVE_STS] = { 0 };
    uint8_t scs[FEETECH_LIVE_SCS] = { 0 };
    feetech_live_t l;

    feetech_put16(FEETECH_STS, &sts[0], 4095);
    feetech_put16(FEETECH_STS, &sts[2], feetech_sign_mag(-50));
    feetech_put16(FEETECH_STS, &sts[4], 0x0400u | 123u);
    sts[6] = 120;
    sts[7] = 45;
    sts[9] = FEETECH_ERR_OVERLOAD | FEETECH_ERR_VOLTAGE;
    sts[10] = 1;
    feetech_put16(FEETECH_STS, &sts[13], 200);
    CHECK(feetech_live(FEETECH_STS, sts, sizeof(sts), &l));
    CHECK_EQ(l.position, 4095);
    CHECK_EQ(l.speed, -50);
    CHECK_EQ(l.load, 123);
    CHECK_EQ(l.voltage_dv, 120);
    CHECK_EQ(l.temp_c, 45);
    CHECK_EQ(l.status, FEETECH_ERR_OVERLOAD | FEETECH_ERR_VOLTAGE);
    CHECK_EQ(l.moving, 1);
    CHECK(l.has_current);
    CHECK_EQ(l.current_raw, 200);
    CHECK_EQ(feetech_current_ma(200), 1300u);
    CHECK_EQ(feetech_current_ma(1), 7u);   /* 6.5 rounds up */
    CHECK_EQ(feetech_current_ma(0), 0u);
    /* Bit 15 is the direction: 0x8001 is one count, not 32769. */
    feetech_put16(FEETECH_STS, &sts[13], 0x8001u);
    CHECK(feetech_live(FEETECH_STS, sts, sizeof(sts), &l));
    CHECK_EQ(l.current_raw, 1);
    CHECK_EQ(feetech_current_ma(l.current_raw), 7u);
    feetech_put16(FEETECH_STS, &sts[13], 0xFFFFu);
    CHECK(feetech_live(FEETECH_STS, sts, sizeof(sts), &l));
    CHECK_EQ(l.current_raw, 0x7FFF);
    CHECK_EQ(feetech_current_ma(l.current_raw), 212986u);

    feetech_put16(FEETECH_SCS, &scs[0], 1023);
    scs[6] = 60;
    CHECK(feetech_live(FEETECH_SCS, scs, sizeof(scs), &l));
    CHECK_EQ(l.position, 1023);
    CHECK_EQ(scs[0], 0x03);
    CHECK_EQ(l.voltage_dv, 60);
    CHECK(!l.has_current);
    /* A block of another length is refused, the output cleared. */
    CHECK(!feetech_live(FEETECH_SCS, sts, sizeof(sts), &l));
    CHECK_EQ(l.position, 0);
    CHECK(!feetech_live(FEETECH_STS, sts, 0, &l));
    CHECK(!feetech_live(FEETECH_STS, NULL, sizeof(sts), &l));
    CHECK(!feetech_live(FEETECH_STS, sts, sizeof(sts), NULL));
}

int main(void)
{
    RUN(two_byte_values_are_low_first_on_sts_and_high_first_on_scs);
    RUN(the_tables_hold_each_series_registers_and_ranges);
    RUN(register_6_holds_eight_rates);
    RUN(sign_and_magnitude_round_trip_and_clamp);
    RUN(the_live_block_decodes_in_each_byte_order);
    return test_summary("feetech_reg");
}
