/*
 * The X-series control table of ROBOTIS Dynamixel servos
 * (protocols/dynamixel/dxl_table.c): the fields, the EEPROM and RAM
 * classification of a write per address, ranges, the baud rate register,
 * the model numbers, the unit conversions and the restore of an EEPROM
 * backup.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "dxl_table.h"

TEST_CASE(the_fields_are_in_address_order_and_do_not_overlap)
{
    unsigned end = 0;

    for (unsigned i = 0; i < (unsigned)DXL_R_COUNT; ++i) {
        const dxl_reg_t *r = dxl_reg((dxl_reg_id_t)i);

        CHECK(r != NULL);
        if (r == NULL) {
            return;
        }
        if (r->addr < end) {
            T_FAIL("%s at %u overlaps", r->name, r->addr);
        }
        CHECK(r->size == 1u || r->size == 2u || r->size == 4u);
        CHECK_EQ(r->area, r->addr < DXL_EEPROM_SIZE ? DXL_AREA_EEPROM
                                                    : DXL_AREA_RAM);
        CHECK(r->min <= r->max);
        CHECK(dxl_reg_at(r->addr) == r);
        end = (unsigned)r->addr + r->size;
    }
    CHECK_EQ(end, DXL_TABLE_END);
    CHECK(dxl_reg(DXL_R_COUNT) == NULL);
    CHECK(dxl_reg_at(1) == NULL);
}

/* The fields the task names, at their addresses, sizes and areas. */
TEST_CASE(the_fields_a_tester_uses_sit_where_the_e_manual_puts_them)
{
    static const struct {
        dxl_reg_id_t id;
        uint8_t addr, size, area, access, unit;
    } k[] = {
        { DXL_R_MODEL_NUMBER, 0, 2, DXL_AREA_EEPROM, DXL_ACC_R, DXL_UNIT_RAW },
        { DXL_R_FIRMWARE, 6, 1, DXL_AREA_EEPROM, DXL_ACC_R, DXL_UNIT_RAW },
        { DXL_R_ID, 7, 1, DXL_AREA_EEPROM, DXL_ACC_RW, DXL_UNIT_RAW },
        { DXL_R_BAUD, 8, 1, DXL_AREA_EEPROM, DXL_ACC_RW, DXL_UNIT_BAUD },
        { DXL_R_RETURN_DELAY, 9, 1, DXL_AREA_EEPROM, DXL_ACC_RW, DXL_UNIT_US_2 },
        { DXL_R_PROTOCOL, 13, 1, DXL_AREA_EEPROM, DXL_ACC_RW, DXL_UNIT_RAW },
        { DXL_R_MAX_POSITION, 48, 4, DXL_AREA_EEPROM, DXL_ACC_RW, DXL_UNIT_PULSE },
        { DXL_R_MIN_POSITION, 52, 4, DXL_AREA_EEPROM, DXL_ACC_RW, DXL_UNIT_PULSE },
        { DXL_R_TORQUE_ENABLE, 64, 1, DXL_AREA_RAM, DXL_ACC_RW, DXL_UNIT_RAW },
        { DXL_R_LED, 65, 1, DXL_AREA_RAM, DXL_ACC_RW, DXL_UNIT_RAW },
        { DXL_R_HW_ERROR, 70, 1, DXL_AREA_RAM, DXL_ACC_R, DXL_UNIT_RAW },
        { DXL_R_PROFILE_ACCELERATION, 108, 4, DXL_AREA_RAM, DXL_ACC_RW, DXL_UNIT_ACC_214 },
        { DXL_R_PROFILE_VELOCITY, 112, 4, DXL_AREA_RAM, DXL_ACC_RW, DXL_UNIT_RPM_229 },
        { DXL_R_GOAL_POSITION, 116, 4, DXL_AREA_RAM, DXL_ACC_RW, DXL_UNIT_PULSE },
        { DXL_R_PRESENT_LOAD, 126, 2, DXL_AREA_RAM, DXL_ACC_R, DXL_UNIT_PERMILLE },
        { DXL_R_PRESENT_POSITION, 132, 4, DXL_AREA_RAM, DXL_ACC_R, DXL_UNIT_PULSE },
        { DXL_R_PRESENT_VOLTAGE, 144, 2, DXL_AREA_RAM, DXL_ACC_R, DXL_UNIT_DECIVOLT },
        { DXL_R_PRESENT_TEMPERATURE, 146, 1, DXL_AREA_RAM, DXL_ACC_R, DXL_UNIT_DEG_C },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const dxl_reg_t *r = dxl_reg(k[i].id);

        CHECK_EQ(r->addr, k[i].addr);
        CHECK_EQ(r->size, k[i].size);
        CHECK_EQ(r->area, k[i].area);
        CHECK_EQ(r->access, k[i].access);
        CHECK_EQ(r->unit, k[i].unit);
    }
    CHECK_EQ(dxl_reg(DXL_R_ID)->max, 252);
    CHECK_EQ(dxl_reg(DXL_R_BAUD)->max, 7);
    CHECK_EQ(dxl_reg(DXL_R_RETURN_DELAY)->max, 254);
    CHECK_EQ(dxl_reg(DXL_R_MAX_POSITION)->max, 4095);
    CHECK((dxl_reg(DXL_R_ID)->flags & DXL_F_LINK) != 0u);
    CHECK((dxl_reg(DXL_R_BAUD)->flags & DXL_F_LINK) != 0u);
    CHECK((dxl_reg(DXL_R_PROTOCOL)->flags & DXL_F_NO_WRITE) != 0u);
}

/* The class of a 1-byte write at every address, and of every field
 * written whole, against the table read independently. */
TEST_CASE(every_address_classifies_as_the_table_says)
{
    unsigned eeprom = 0;
    unsigned ram = 0;

    for (unsigned a = 0; a < 256u; ++a) {
        const dxl_reg_t *r = dxl_reg_at((uint16_t)a);
        dxl_wclass_t want;

        if (r == NULL) {
            /* Not the start of a field: a 1-byte write is refused unless the
             * byte belongs to a read-only field. */
            continue;
        }
        if (r->access == DXL_ACC_R) {
            want = DXL_WR_READ_ONLY;
        } else if ((r->flags & DXL_F_NO_WRITE) != 0u) {
            want = DXL_WR_UNKNOWN;
        } else {
            want = r->area == DXL_AREA_EEPROM ? DXL_WR_EEPROM : DXL_WR_RAM;
        }
        CHECK_EQ(dxl_write_class((uint16_t)a, r->size), want);
        if (want == DXL_WR_EEPROM) {
            ++eeprom;
        } else if (want == DXL_WR_RAM) {
            ++ram;
        }
        /* Part of a field is refused. */
        if (r->size > 1u && r->access == DXL_ACC_RW) {
            CHECK_EQ(dxl_write_class((uint16_t)a, (uint16_t)(r->size - 1u)),
                     DXL_WR_UNKNOWN);
            CHECK_EQ(dxl_write_class((uint16_t)(a + 1u), 1),
                     DXL_WR_UNKNOWN);
        }
    }
    /* 18 EEPROM fields a tester may write, 10 RAM ones. */
    CHECK_EQ(eeprom, 18u);
    CHECK_EQ(ram, 10u);
}

TEST_CASE(the_writes_a_tester_makes_classify_as_the_owner_rule_needs)
{
    /* EEPROM: behind the hold. */
    CHECK_EQ(dxl_write_class(7, 1), DXL_WR_EEPROM);    /* ID */
    CHECK_EQ(dxl_write_class(8, 1), DXL_WR_EEPROM);    /* baud */
    CHECK_EQ(dxl_write_class(9, 1), DXL_WR_EEPROM);    /* return delay */
    CHECK_EQ(dxl_write_class(48, 4), DXL_WR_EEPROM);   /* max position */
    CHECK_EQ(dxl_write_class(52, 4), DXL_WR_EEPROM);   /* min position */
    CHECK_EQ(dxl_write_class(48, 8), DXL_WR_EEPROM);   /* both limits */
    CHECK_EQ(dxl_write_class(7, 3), DXL_WR_EEPROM);    /* ID, baud, delay */
    /* RAM: free. */
    CHECK_EQ(dxl_write_class(64, 1), DXL_WR_RAM);      /* torque */
    CHECK_EQ(dxl_write_class(65, 1), DXL_WR_RAM);      /* LED */
    CHECK_EQ(dxl_write_class(116, 4), DXL_WR_RAM);     /* goal position */
    CHECK_EQ(dxl_write_class(112, 4), DXL_WR_RAM);     /* profile velocity */
    CHECK_EQ(dxl_write_class(108, 8), DXL_WR_RAM);     /* acc, vel */
    CHECK_EQ(dxl_write_class(108, 12), DXL_WR_RAM);    /* acc, vel, goal */
    /* Read-only and unknown. */
    CHECK_EQ(dxl_write_class(0, 2), DXL_WR_READ_ONLY); /* model */
    CHECK_EQ(dxl_write_class(6, 2), DXL_WR_READ_ONLY); /* firmware + ID */
    CHECK_EQ(dxl_write_class(132, 4), DXL_WR_READ_ONLY);
    CHECK_EQ(dxl_write_class(146, 1), DXL_WR_READ_ONLY);
    CHECK_EQ(dxl_write_class(13, 1), DXL_WR_UNKNOWN);  /* protocol type */
    CHECK_EQ(dxl_write_class(14, 1), DXL_WR_UNKNOWN);  /* a gap */
    CHECK_EQ(dxl_write_class(2, 1), DXL_WR_UNKNOWN);   /* not read */
    CHECK_EQ(dxl_write_class(147, 1), DXL_WR_UNKNOWN); /* past the table */
    CHECK_EQ(dxl_write_class(65, 2), DXL_WR_UNKNOWN);  /* LED + gap 66 */
    CHECK_EQ(dxl_write_class(63, 2), DXL_WR_EEPROM);   /* shutdown + torque */
    CHECK_EQ(dxl_write_class(64, 0), DXL_WR_UNKNOWN);
    CHECK_EQ(dxl_write_class(65535, 2), DXL_WR_UNKNOWN);
}

TEST_CASE(values_are_held_to_their_field_s_range)
{
    CHECK(dxl_reg_value_ok(dxl_reg(DXL_R_ID), 0));
    CHECK(dxl_reg_value_ok(dxl_reg(DXL_R_ID), 252));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_ID), 253));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_ID), -1));
    CHECK(dxl_reg_value_ok(dxl_reg(DXL_R_BAUD), 7));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_BAUD), 8));
    CHECK(dxl_reg_value_ok(dxl_reg(DXL_R_RETURN_DELAY), 254));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_RETURN_DELAY), 255));
    CHECK(dxl_reg_value_ok(dxl_reg(DXL_R_MIN_POSITION), 4095));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_MIN_POSITION), 4096));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_PROTOCOL), 2));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_PRESENT_POSITION), 0));
    CHECK(dxl_reg_value_ok(dxl_reg(DXL_R_GOAL_POSITION), -1048575));
    CHECK(!dxl_reg_value_ok(dxl_reg(DXL_R_GOAL_POSITION), 1048576));
    CHECK(!dxl_reg_value_ok(NULL, 0));
}

TEST_CASE(a_field_reads_from_a_table_slice_with_its_sign)
{
    uint8_t t[DXL_TABLE_END];
    int32_t v = 0;

    memset(t, 0, sizeof(t));
    t[0] = 0x24;   /* 1060 */
    t[1] = 0x04;
    t[126] = 0x18; /* -1000 */
    t[127] = 0xFC;
    t[132] = 0xFF; /* -1 */
    t[133] = 0xFF;
    t[134] = 0xFF;
    t[135] = 0xFF;
    t[144] = 120;  /* 12.0 V */
    CHECK(dxl_reg_get(dxl_reg(DXL_R_MODEL_NUMBER), t, 0, sizeof(t), &v));
    CHECK_EQ(v, 1060);
    CHECK(dxl_reg_get(dxl_reg(DXL_R_PRESENT_LOAD), t, 0, sizeof(t), &v));
    CHECK_EQ(v, -1000);
    CHECK(dxl_reg_get(dxl_reg(DXL_R_PRESENT_POSITION), t, 0, sizeof(t), &v));
    CHECK_EQ(v, -1);
    /* A read of 126 to 146 as one slice. */
    CHECK(dxl_reg_get(dxl_reg(DXL_R_PRESENT_VOLTAGE), &t[126], 126, 21, &v));
    CHECK_EQ(v, 120);
    CHECK(!dxl_reg_get(dxl_reg(DXL_R_PRESENT_TEMPERATURE), &t[126], 126, 20,
                       &v));
    CHECK(!dxl_reg_get(dxl_reg(DXL_R_MODEL_NUMBER), &t[126], 126, 21, &v));
    CHECK(!dxl_reg_get(NULL, t, 0, sizeof(t), &v));
    CHECK(!dxl_reg_get(dxl_reg(DXL_R_ID), NULL, 0, sizeof(t), &v));
    CHECK(!dxl_reg_get(dxl_reg(DXL_R_ID), t, 0, sizeof(t), NULL));
}

TEST_CASE(the_baud_register_maps_8_rates_and_no_other)
{
    static const uint32_t k[8] = { 9600, 57600, 115200, 1000000, 2000000,
                                   3000000, 4000000, 4500000 };

    for (uint8_t i = 0; i < 8u; ++i) {
        CHECK_EQ(dxl_baud_rate(i), k[i]);
        CHECK_EQ(dxl_baud_index(k[i]), i);
    }
    CHECK_EQ(dxl_baud_rate(8), 0u);
    CHECK_EQ(dxl_baud_index(19200), -1);
    CHECK_EQ(dxl_baud_index(0), -1);
}

TEST_CASE(the_two_verified_models_are_named_and_others_are_not)
{
    const dxl_model_t *m = dxl_model_find(1060);

    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_STR_EQ(m->name, "XL430-W250");
        CHECK(!m->current);
    }
    m = dxl_model_find(1030);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_STR_EQ(m->name, "XM430-W210");
        CHECK(m->current);
    }
    /* XM430-W350 (1020 by hearsay, unverified) and anything else: shown by
     * number. */
    CHECK(dxl_model_find(1020) == NULL);
    CHECK(dxl_model_find(0) == NULL);
    CHECK(dxl_model_find(65535) == NULL);
}

TEST_CASE(units_convert_with_integer_rounding)
{
    CHECK_EQ(dxl_pulse_to_mdeg(0), 0);
    CHECK_EQ(dxl_pulse_to_mdeg(1), 88);       /* 87.890625 */
    CHECK_EQ(dxl_pulse_to_mdeg(2048), 180000);
    CHECK_EQ(dxl_pulse_to_mdeg(4095), 359912);
    CHECK_EQ(dxl_pulse_to_mdeg(4096), 360000);
    CHECK_EQ(dxl_pulse_to_mdeg(-1), -88);
    CHECK_EQ(dxl_pulse_to_mdeg(INT32_MAX), INT32_MAX);
    CHECK_EQ(dxl_pulse_to_mdeg(INT32_MIN), INT32_MIN);
    CHECK_EQ(dxl_mdeg_to_pulse(180000), 2048);
    CHECK_EQ(dxl_mdeg_to_pulse(44), 1);       /* 0.5 pulse rounds up */
    CHECK_EQ(dxl_mdeg_to_pulse(43), 0);
    CHECK_EQ(dxl_mdeg_to_pulse(-90000), -1024);
    for (int32_t p = -5000; p <= 5000; ++p) {
        if (dxl_mdeg_to_pulse(dxl_pulse_to_mdeg(p)) != p) {
            T_FAIL("pulse %d does not survive", (int)p);
            break;
        }
    }
    CHECK_EQ(dxl_current_ma(100), 269);
    CHECK_EQ(dxl_current_ma(-1), -3);         /* -2.69 */
    CHECK_EQ(dxl_current_ma(1), 3);
    CHECK_EQ(dxl_voltage_mv(120), 12000);
    CHECK_EQ(dxl_return_delay_us(250), 500u);
    CHECK_EQ(dxl_return_delay_us(254), 508u);
    CHECK_EQ(dxl_return_delay_us(0), 0u);
}

TEST_CASE(hardware_error_bits_are_the_xm430_page_s)
{
    CHECK_EQ(DXL_HW_INPUT_VOLTAGE, 0x01u);
    CHECK_EQ(DXL_HW_OVERHEATING, 0x04u);
    CHECK_EQ(DXL_HW_ENCODER, 0x08u);
    CHECK_EQ(DXL_HW_SHOCK, 0x10u);
    CHECK_EQ(DXL_HW_OVERLOAD, 0x20u);
    /* The Shutdown default 52 is overheating, shock and overload. */
    CHECK_EQ(DXL_HW_OVERHEATING | DXL_HW_SHOCK | DXL_HW_OVERLOAD, 52u);
}

static void put_le(uint8_t *e, unsigned addr, uint32_t v, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) {
        e[addr + i] = (uint8_t)(v >> (8u * i));
    }
}

static void defaults(uint8_t *e)
{
    memset(e, 0, DXL_EEPROM_SIZE);
    e[0] = 0x24;
    e[1] = 0x04;
    e[6] = 45;
    e[7] = 1;
    e[8] = 1;
    e[9] = 250;
    e[11] = 3;
    e[12] = 255;
    e[13] = 2;
    e[24] = 10;
    e[31] = 72;
    e[32] = 140;
    e[34] = 60;
    put_le(e, 36, 885, 2);
    put_le(e, 44, 265, 4);
    put_le(e, 48, 4095, 4);
    e[63] = 52;
}

TEST_CASE(a_restore_writes_what_differs_with_id_then_baud_last)
{
    uint8_t backup[DXL_EEPROM_SIZE];
    uint8_t now[DXL_EEPROM_SIZE];
    dxl_wr_t w[DXL_RESTORE_MAX];
    int refused = 0;

    defaults(backup);
    memcpy(now, backup, sizeof(now));
    CHECK_EQ(dxl_restore_plan(backup, now, w, DXL_RESTORE_MAX, &refused), 0u);
    CHECK_EQ(refused, -1);

    /* Changed: baud, ID, both limits, return delay; also bytes a restore
     * never writes (model, firmware, protocol type). */
    now[8] = 3;
    now[7] = 9;
    put_le(now, 48, 3000, 4);
    put_le(now, 52, 1000, 4);
    now[9] = 0;
    now[0] = 0x99;
    now[6] = 46;
    now[13] = 1;
    const size_t n = dxl_restore_plan(backup, now, w, DXL_RESTORE_MAX,
                                      &refused);

    CHECK_EQ(refused, -1);
    CHECK_EQ(n, 5u);
    if (n == 5u) {
        CHECK_EQ(w[0].addr, 9);
        CHECK_EQ(w[0].value, 250u);
        CHECK_EQ(w[1].addr, 48);
        CHECK_EQ(w[1].size, 4);
        CHECK_EQ(w[1].value, 4095u);
        CHECK_EQ(w[2].addr, 52);
        CHECK_EQ(w[2].value, 0u);
        CHECK_EQ(w[3].addr, 7);
        CHECK_EQ(w[3].value, 1u);
        CHECK_EQ(w[4].addr, 8);
        CHECK_EQ(w[4].value, 1u);
    }
    /* Too little room: refused, not cut. */
    CHECK_EQ(dxl_restore_plan(backup, now, w, 4, &refused), 0u);
    CHECK_EQ(refused, DXL_RESTORE_NO_ROOM);
}

TEST_CASE(a_restore_from_a_backup_out_of_range_is_refused)
{
    uint8_t backup[DXL_EEPROM_SIZE];
    uint8_t now[DXL_EEPROM_SIZE];
    dxl_wr_t w[DXL_RESTORE_MAX];
    int refused = 0;

    defaults(backup);
    memcpy(now, backup, sizeof(now));
    backup[7] = 253;
    CHECK_EQ(dxl_restore_plan(backup, now, w, DXL_RESTORE_MAX, &refused), 0u);
    CHECK_EQ(refused, 7);
    CHECK_EQ(dxl_restore_plan(NULL, now, w, DXL_RESTORE_MAX, NULL), 0u);
    CHECK_EQ(dxl_restore_plan(backup, now, w, DXL_RESTORE_MAX, NULL), 0u);
}

TEST_CASE(every_writable_eeprom_field_fits_one_restore)
{
    uint8_t backup[DXL_EEPROM_SIZE];
    uint8_t now[DXL_EEPROM_SIZE];
    dxl_wr_t w[DXL_RESTORE_MAX];
    int refused = 0;

    defaults(backup);
    memset(now, 0, sizeof(now));
    now[32] = 61;
    now[34] = 61;
    for (unsigned i = 0; i < (unsigned)DXL_R_COUNT; ++i) {
        const dxl_reg_t *r = dxl_reg((dxl_reg_id_t)i);

        if (r->area == DXL_AREA_EEPROM && r->access == DXL_ACC_RW) {
            backup[r->addr] = (uint8_t)(now[r->addr] + 1u);
        }
    }
    backup[13] = 2;
    const size_t n = dxl_restore_plan(backup, now, w, DXL_RESTORE_MAX,
                                      &refused);

    CHECK_EQ(refused, -1);
    CHECK_EQ(n, 18u);
    CHECK(n <= DXL_RESTORE_MAX);
}

int main(void)
{
    RUN(the_fields_are_in_address_order_and_do_not_overlap);
    RUN(the_fields_a_tester_uses_sit_where_the_e_manual_puts_them);
    RUN(every_address_classifies_as_the_table_says);
    RUN(the_writes_a_tester_makes_classify_as_the_owner_rule_needs);
    RUN(values_are_held_to_their_field_s_range);
    RUN(a_field_reads_from_a_table_slice_with_its_sign);
    RUN(the_baud_register_maps_8_rates_and_no_other);
    RUN(the_two_verified_models_are_named_and_others_are_not);
    RUN(units_convert_with_integer_rounding);
    RUN(hardware_error_bits_are_the_xm430_page_s);
    RUN(a_restore_writes_what_differs_with_id_then_baud_last);
    RUN(a_restore_from_a_backup_out_of_range_is_refused);
    RUN(every_writable_eeprom_field_fits_one_restore);
    return test_summary("dxl_table");
}
