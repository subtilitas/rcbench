/*
 * The writes of the Hitec register models (protocols/hitec/hitec_reg.c):
 * the edits' side effects and the write plans, on the bench reads of
 * hitec_reg_fix.h.  The expected writes are those the HFP-30 (D485HW) and
 * the HPP-21 Plus (HS-5645MG, HS-7955TG) sent on the wire for each
 * setting.
 *
 * Under test:
 *  - the side effects of the classic centre, direction and fail-safe
 *    position, the end points by side, the factory reset;
 *  - the balance after every classic setting, none above 0x1C, the repair
 *    of a bad checksum, a restore of every cell;
 *  - the registers of every D series setting, the mirror of a direction
 *    change, the save and the reboot, a plan with registers not read.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_reg_fix.h"

/* ---------------------------------------------------------------- cases */

TEST_CASE(classic_edits_write_the_hpp21_side_effects)
{
    hitec_image_t img;

    /* HS-5645MG centre at target 0x1770: 0x0F6C, both ends 0x10. */
    hs5645(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE,
                             0x1770), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x07], 0x0F);
    CHECK_EQ(img.mem[0x08], 0x6C);
    CHECK_EQ(img.mem[0x13], 0x10);
    CHECK_EQ(img.mem[0x14], 0x10);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    /* HS-7955TG centre at target 0x1788: 0x0F88. */
    hs7955(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_CENTRE,
                             0x1788), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x07], 0x0F);
    CHECK_EQ(img.mem[0x08], 0x88);
    /* HS-5645MG fail-safe position at 0x1770: 0x176C and bit 1. */
    hs5645(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_FS_POSITION,
                             0x1770), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x19], 0x17);
    CHECK_EQ(img.mem[0x1A], 0x6C);
    CHECK_EQ(img.mem[0x1B], 0x03);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_FAILSAFE,
                 HITEC_GET_OK).value, HITEC_ON);
    hs7955(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_FS_POSITION,
                             6000), HITEC_SET_ERR_RO);
    /* Fail-safe on and off keep bit 0. */
    hs5645(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_FAILSAFE,
                             HITEC_ON), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x1B], 0x03);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_FAILSAFE,
                             HITEC_OFF), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x1B], 0x01);
    /* Direction: the ends swap, the fail-safe position comes from the
     * factory block, the left end stays the left end. */
    hs5645(&img);
    img.mem[0x19] = 0x12;
    img.mem[0x1B] = 0x03;
    hitec_c_rebalance(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DIRECTION,
                             HITEC_CCW), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x13], 0xA2);
    CHECK_EQ(img.mem[0x14], 0xC2);
    CHECK_EQ(img.mem[0x19], 0x17);
    CHECK_EQ(img.mem[0x1A], 0x70);
    CHECK_EQ(img.mem[0x1B], 0x02);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_LEFT_END,
                 HITEC_GET_OK).value, 0xA2);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_RIGHT_END,
                 HITEC_GET_OK).value, 0xC2);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DIRECTION,
                             HITEC_CW), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x13], 0xC2);
    CHECK_EQ(img.mem[0x1B], 0x03);
    /* End points by side: HS-5645MG (CW) right 90 in 0x13, left 91 in
     * 0x14; HS-7955TG (CCW) left 196 in 0x13, right 189 in 0x14. */
    hs5645(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_RIGHT_END, 90),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_LEFT_END, 91),
             HITEC_SET_OK);
    CHECK_EQ(img.mem[0x13], 90);
    CHECK_EQ(img.mem[0x14], 91);
    hs7955(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_LEFT_END, 196),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_RIGHT_END,
                             189), HITEC_SET_OK);
    CHECK_EQ(img.mem[0x13], 196);
    CHECK_EQ(img.mem[0x14], 189);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
}

TEST_CASE(classic_factory_reset_copies_the_factory_block)
{
    hitec_image_t img;
    hitec_image_t from;
    hitec_plan_t p;

    hs7955(&from);
    img = from;
    CHECK(hitec_c_factory(&img));
    CHECK(memcmp(img.mem, fix_hs7955_fac(), 0x24) == 0);
    CHECK_EQ(img.mem[0x1C], 0x1D);
    /* The HPP-21 Plus writes all 24 settings; a plan writes the changed
     * ones: the centre, both ends, the direction, the resolution. */
    CHECK_EQ(hitec_plan(&from, &img, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 10);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x07, 0x0F);
    check_step(&p.step[1], HITEC_STEP_C_CELL, 0x08, 0x70);
    check_step(&p.step[3], HITEC_STEP_C_CELL, 0x13, 0xDC);
    check_step(&p.step[5], HITEC_STEP_C_CELL, 0x14, 0xDC);
    check_step(&p.step[7], HITEC_STEP_C_CELL, 0x1B, 0x01);
    check_step(&p.step[8], HITEC_STEP_C_SUM, 0x1C, 0x1D);
    check_step(&p.step[9], HITEC_STEP_C_CELL, 0x22, 0x0B);
    /* Not read, wrong series, NULL. */
    img.known[0x50 / 8u] = 0;
    CHECK(!hitec_c_factory(&img));
    hs7955(&img);
    img.known[0x23 / 8u] = 0;
    CHECK(!hitec_c_factory(&img));
    d485(&img);
    CHECK(!hitec_c_factory(&img));
    CHECK(!hitec_c_factory(NULL));
}

TEST_CASE(a_classic_setting_writes_its_cells_then_the_balance)
{
    hitec_image_t from;
    hitec_image_t to;
    hitec_plan_t p;

    /* Speed 50% on the HS-5645MG: 0x06 = 0x1C, then 0x1C. */
    hs5645(&from);
    p = plan_edit(&from, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED, 50);
    CHECK_EQ(p.n, 2);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x06, 0x1C);
    check_step(&p.step[1], HITEC_STEP_C_SUM, 0x1C,
               (uint8_t)(0x8C + 0x40 - 0x1C));
    CHECK_EQ(p.step[0].wait_ms, 0);
    /* Centre: the pair, the balance, each end, the balance. */
    p = plan_edit(&from, HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE, 0x1770);
    CHECK_EQ(p.n, 7);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x07, 0x0F);
    check_step(&p.step[1], HITEC_STEP_C_CELL, 0x08, 0x6C);
    CHECK_EQ(p.step[2].kind, HITEC_STEP_C_SUM);
    check_step(&p.step[3], HITEC_STEP_C_CELL, 0x13, 0x10);
    CHECK_EQ(p.step[4].kind, HITEC_STEP_C_SUM);
    check_step(&p.step[5], HITEC_STEP_C_CELL, 0x14, 0x10);
    to = from;
    hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE, 0x1770);
    check_step(&p.step[6], HITEC_STEP_C_SUM, 0x1C, to.mem[0x1C]);
    /* Direction: 0x13, 0x14, the fail-safe pair, 0x1B, a balance after
     * each. */
    hs7955(&from);
    p = plan_edit(&from, HITEC_LAYOUT_HS7, HITEC_P_C_DIRECTION, HITEC_CW);
    CHECK_EQ(p.n, 6);   /* 0x19/0x1A equal the factory block: no write */
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x13, 0xB2);
    check_step(&p.step[2], HITEC_STEP_C_CELL, 0x14, 0xA2);
    check_step(&p.step[4], HITEC_STEP_C_CELL, 0x1B, 0x01);
    CHECK_EQ(p.step[5].kind, HITEC_STEP_C_SUM);
    /* HS-7955TG overload 30%: 0x20 = 00 and 0x21 = 1E, no balance. */
    p = plan_edit(&from, HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD, 30);
    CHECK_EQ(p.n, 2);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x20, 0x00);
    check_step(&p.step[1], HITEC_STEP_C_CELL, 0x21, 0x1E);
    /* HS-5645MG overload 10%: 0x20 = 0A and 0x21 = FF, no balance. */
    hs5645(&from);
    from.mem[0x20] = 0x64;
    p = plan_edit(&from, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD, 10);
    CHECK_EQ(p.n, 2);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x20, 0x0A);
    check_step(&p.step[1], HITEC_STEP_C_CELL, 0x21, 0xFF);
    /* Dead band: one cell and the balance. */
    hs7955(&from);
    p = plan_edit(&from, HITEC_LAYOUT_HS7, HITEC_P_C_DEAD_BAND, 5);
    CHECK_EQ(p.n, 2);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x02, 0x10);
    CHECK_EQ(p.step[1].kind, HITEC_STEP_C_SUM);
    /* Fail-safe position: the pair and its balance, 0x1B and its. */
    hs5645(&from);
    p = plan_edit(&from, HITEC_LAYOUT_HS5, HITEC_P_C_FS_POSITION, 0x1770);
    CHECK_EQ(p.n, 5);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x19, 0x17);
    check_step(&p.step[1], HITEC_STEP_C_CELL, 0x1A, 0x6C);
    check_step(&p.step[3], HITEC_STEP_C_CELL, 0x1B, 0x03);
}

TEST_CASE(a_classic_plan_repairs_a_bad_checksum_and_restores_every_cell)
{
    hitec_image_t from;
    hitec_image_t to;
    hitec_plan_t p;

    /* No change, a bad checksum: one balance write. */
    hs5645(&from);
    from.mem[0x1C] = 0x00;
    to = from;
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 1);
    check_step(&p.step[0], HITEC_STEP_C_SUM, 0x1C, 0x8C);
    /* A change above 0x1C alone, from a bad checksum: the cell, then the
     * repair. */
    to.mem[0x1D] = 0x05;
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 2);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x1D, 0x05);
    check_step(&p.step[1], HITEC_STEP_C_SUM, 0x1C, 0x8C);
    /* A good image, no change: nothing. */
    hs5645(&from);
    to = from;
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 0);
    /* Cells outside 0x00 to 0x23 are not written: the factory block, the
     * name. */
    to.mem[0x30] = 0x00;
    to.mem[0x60] = 'X';
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 0);
    /* Every writable cell changed: 35 cells, 19 balances. */
    to = from;
    for (unsigned c = 0; c < 0x24u; ++c) {
        to.mem[c] = (uint8_t)(to.mem[c] + 1u);
    }
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 54);
    {
        unsigned cells = 0;
        unsigned sums = 0;
        uint8_t servo[0x24];
        memcpy(servo, from.mem, sizeof(servo));
        for (unsigned i = 0; i < p.n; ++i) {
            servo[p.step[i].addr] = (uint8_t)p.step[i].value;
            if (p.step[i].kind == HITEC_STEP_C_SUM) {
                ++sums;
                unsigned s = 0;
                for (unsigned c = 0; c <= 0x1Cu; ++c) {
                    s += servo[c];
                }
                CHECK_EQ(s % 256u, 0);
            } else {
                ++cells;
                CHECK(p.step[i].addr != 0x1C);
            }
        }
        CHECK_EQ(cells, 35);
        CHECK_EQ(sums, 19);
        CHECK(memcmp(&servo[0x1D], &to.mem[0x1D], 0x24 - 0x1D) == 0);
        CHECK(memcmp(servo, to.mem, 0x1C) == 0);
    }
    CHECK(p.n <= HITEC_PLAN_MAX);
    /* A cell of 0x00 to 0x23 not read, in either image. */
    hs5645(&from);
    to = from;
    to.known[0x23 / 8u] = 0;
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_ERR_UNKNOWN);
    CHECK_EQ(p.n, 0);
    CHECK_EQ(hitec_plan(&to, &from, &p), HITEC_PLAN_ERR_UNKNOWN);
}

TEST_CASE(a_d_setting_writes_the_hfp30_registers_then_saves)
{
    hitec_image_t from;
    hitec_image_t to;
    hitec_plan_t p;

    d485(&from);
    /* Speed 30%: 0x54 = 6. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 30);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x54, 6);
    CHECK_EQ(p.step[0].wait_ms, 20);
    check_save(&p);
    /* Dead band 1 to 5: 0x4E = 16. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND, 5);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x4E, 16);
    /* ID 4. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_ID, 4);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x32, 4);
    /* Fail-safe position 1485, limp. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 1485);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x4C, 1485);
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, HITEC_FS_LIMP);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x4C, 0);
    /* Soft start 60%: 0x60 = 6. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 60);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x60, 6);
    /* Overload 30%: 0x9C, 0x9A = 3, 0x98 = 200. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 30);
    CHECK_EQ(p.n, 5);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x9C, 30);
    check_step(&p.step[1], HITEC_STEP_D_WRITE, 0x9A, 3);
    check_step(&p.step[2], HITEC_STEP_D_WRITE, 0x98, 200);
    check_save(&p);
    /* Smart sensor off: 0x72 = 0x4E54, 0x6C = 2500, 0x44 = 4350. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_OFF);
    CHECK_EQ(p.n, 5);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x72, 0x4E54);
    check_step(&p.step[1], HITEC_STEP_D_WRITE, 0x6C, 2500);
    check_step(&p.step[2], HITEC_STEP_D_WRITE, 0x44, 4350);
    /* And back on: 0, 0. */
    to = from;
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_OFF);
    p = plan_edit(&to, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_ON);
    check_step(&p.step[1], HITEC_STEP_D_WRITE, 0x6C, 0);
    check_step(&p.step[2], HITEC_STEP_D_WRITE, 0x44, 0);
    /* Neutral, ends: one register each. */
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 10105);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0xC2, 10105);
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END, 1492);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0xB2, 1492);
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_RIGHT_END, 15730);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0xB0, 15730);
}

TEST_CASE(a_d_direction_change_mirrors_as_the_hfp30_wrote_it)
{
    hitec_image_t from;
    hitec_image_t to;
    hitec_plan_t p;

    /* The HFP-30's capture: neutral 10105, ends 1492 and 15730, CW to
     * CCW: 0xC2 = 6279, 0xB2 = 654, 0xB0 = 14892, 0x4C unchanged,
     * 0x5E = 1. */
    d485(&from);
    hitec_image_put_reg(&from, 0xC2, 10105);
    hitec_image_put_reg(&from, 0xB2, 1492);
    hitec_image_put_reg(&from, 0xB0, 15730);
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION, HITEC_CCW);
    CHECK_EQ(p.n, 7);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0xC2, 6279);
    check_step(&p.step[1], HITEC_STEP_D_WRITE, 0xB2, 654);
    check_step(&p.step[2], HITEC_STEP_D_WRITE, 0xB0, 14892);
    check_step(&p.step[3], HITEC_STEP_D_WRITE, 0x4C, 1);
    check_step(&p.step[4], HITEC_STEP_D_WRITE, 0x5E, 1);
    check_save(&p);
    /* Direction and neutral together: 0xC2 once, with the draft's value. */
    to = from;
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CCW), HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 7000),
             HITEC_SET_OK);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 7);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0xC2, 7000);
    /* Back to CW: the mirror of the mirror. */
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CW), HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&to, 0xC2), 16384 - 7000);
    CHECK_EQ(hitec_image_reg(&to, 0xB2), 1492);
    CHECK_EQ(hitec_image_reg(&to, 0xB0), 15730);
    /* Every D setting at once: 12 registers, 3 fixed values, save,
     * reboot. */
    to = from;
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_ID, 3);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION, HITEC_CCW);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND, 2);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 1500);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 100);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 50);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_OFF);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 17);
    for (unsigned i = 0; i + 2u < p.n; ++i) {
        CHECK_EQ(p.step[i].kind, HITEC_STEP_D_WRITE);
        CHECK(hitec_writable(HITEC_D, p.step[i].addr)
              || p.step[i].addr == 0x72 || p.step[i].addr == 0x98
              || p.step[i].addr == 0x9A);
    }
    check_save(&p);
}

TEST_CASE(a_d_plan_writes_only_what_it_knows)
{
    hitec_image_t from;
    hitec_image_t to;
    hitec_plan_t p;

    d485(&from);
    to = from;
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 0);   /* no write, no save */
    /* A change of a register no plan writes. */
    hitec_image_put_reg(&to, 0x0C, 9000);
    hitec_image_put_reg(&to, 0x22, 1);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 0);
    /* A setting the draft knows and the servo's image does not. */
    hitec_image_init(&from, HITEC_D);
    hitec_image_put_reg(&from, 0x54, 4095);
    to = from;
    hitec_image_put_reg(&to, 0x32, 2);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_ERR_UNKNOWN);
    CHECK_EQ(p.n, 0);
    /* A register the draft does not know is not compared. */
    to = from;
    hitec_image_put_reg(&to, 0x54, 6);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 3);
    /* A direction change whose draft lacks 0x4C. */
    d485(&from);
    to = from;
    hitec_image_put_reg(&to, 0x5E, 1);
    to.known[0x4C / 8u] = (uint8_t)(to.known[0x4C / 8u] & ~0x30u);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_ERR_UNKNOWN);
    /* Arguments. */
    hs5645(&to);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_ERR_ARG);
    CHECK_EQ(hitec_plan(NULL, &to, &p), HITEC_PLAN_ERR_ARG);
    CHECK_EQ(hitec_plan(&from, NULL, &p), HITEC_PLAN_ERR_ARG);
    CHECK_EQ(hitec_plan(&from, &to, NULL), HITEC_PLAN_ERR_ARG);
}

TEST_CASE(the_writable_set_and_the_d_factory_reset)
{
    hitec_plan_t p;
    unsigned n = 0;

    for (unsigned a = 0; a < 256u; ++a) {
        const bool w = hitec_writable(HITEC_CLASSIC, (uint8_t)a);
        CHECK_EQ(w, a < 0x24u && a != 0x1Cu);
        n += hitec_writable(HITEC_D, (uint8_t)a) ? 1u : 0u;
    }
    CHECK_EQ(n, 12);
    CHECK(hitec_writable(HITEC_D, 0x32));
    CHECK(hitec_writable(HITEC_D, 0x5E));
    CHECK(hitec_writable(HITEC_D, 0x44));
    CHECK(!hitec_writable(HITEC_D, 0x00));
    CHECK(!hitec_writable(HITEC_D, 0x72));
    CHECK(!hitec_writable(HITEC_D, 0x98));
    CHECK(!hitec_writable(HITEC_D, 0x6E));

    hitec_d_factory_plan(&p);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x6E, 0x0F0F);
    check_save(&p);
    hitec_d_factory_plan(NULL);
}

TEST_CASE(a_raw_value_is_replaced_by_a_table_value)
{
    hitec_image_t from;
    hitec_image_t to;
    hitec_plan_t p;

    /* HS-5645MG: dead band 02 to step 2 (04), overload 0x20 = FF to 10%. */
    hs5645(&from);
    to = from;
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_DEAD_BAND, 2),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD, 10),
             HITEC_SET_OK);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 4);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x02, 0x04);
    check_step(&p.step[1], HITEC_STEP_C_SUM, 0x1C, 0x8A);
    check_step(&p.step[2], HITEC_STEP_C_CELL, 0x20, 0x0A);
    check_step(&p.step[3], HITEC_STEP_C_CELL, 0x21, 0xFF);
    CHECK_EQ(get(&to, HITEC_LAYOUT_HS5, HITEC_P_C_DEAD_BAND,
                 HITEC_GET_OK).value, 2);
    CHECK_EQ(get(&to, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD,
                 HITEC_GET_OK).value, 10);
    /* HS-5955TG, the same; its 0x21 reads 00 and the layout writes FF. */
    hs5955(&from);
    to = from;
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_DEAD_BAND, 2),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD, 10),
             HITEC_SET_OK);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 4);
    check_step(&p.step[0], HITEC_STEP_C_CELL, 0x02, 0x04);
    check_step(&p.step[1], HITEC_STEP_C_SUM, 0x1C, 0xC5);
    check_step(&p.step[2], HITEC_STEP_C_CELL, 0x20, 0x0A);
    check_step(&p.step[3], HITEC_STEP_C_CELL, 0x21, 0xFF);
    /* D series: a direction of 0x5E = 2 to CCW writes 0x5E alone, then
     * the save; a neutral edit in the same draft still writes 0xC2. */
    d485(&from);
    hitec_image_put_reg(&from, 0x5E, 2);
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION, HITEC_CCW);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x5E, 1);
    check_save(&p);
    to = from;
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION, HITEC_CCW);
    hitec_param_set(&to, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 9000);
    CHECK_EQ(hitec_plan(&from, &to, &p), HITEC_PLAN_OK);
    CHECK_EQ(p.n, 4);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x5E, 1);
    check_step(&p.step[1], HITEC_STEP_D_WRITE, 0xC2, 9000);
    /* D series: speed 7 to 30%. */
    d485(&from);
    hitec_image_put_reg(&from, 0x54, 7);
    p = plan_edit(&from, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 30);
    CHECK_EQ(p.n, 3);
    check_step(&p.step[0], HITEC_STEP_D_WRITE, 0x54, 6);
    check_save(&p);
}

int main(void)
{
    RUN(classic_edits_write_the_hpp21_side_effects);
    RUN(classic_factory_reset_copies_the_factory_block);
    RUN(a_classic_setting_writes_its_cells_then_the_balance);
    RUN(a_classic_plan_repairs_a_bad_checksum_and_restores_every_cell);
    RUN(a_d_setting_writes_the_hfp30_registers_then_saves);
    RUN(a_d_direction_change_mirrors_as_the_hfp30_wrote_it);
    RUN(a_d_plan_writes_only_what_it_knows);
    RUN(the_writable_set_and_the_d_factory_reset);
    RUN(a_raw_value_is_replaced_by_a_table_value);
    return test_summary("hitec_reg_plan");
}
