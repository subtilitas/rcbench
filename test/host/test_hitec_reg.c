/*
 * The register models of Hitec servos (protocols/hitec/hitec_reg.c): the
 * image, the parameter table, every encoding and model identification, on
 * the bench reads of hitec_reg_fix.h.  The edits' side effects and the
 * write plans are in test_hitec_reg_plan.c.
 *
 * Under test:
 *  - every encoding at both ends of its range and one step past, its round
 *    trip, the clamp, the raw fallback of a value that fits no encoding;
 *  - every refusal of an edit;
 *  - the checksum and its wrap modulo 256;
 *  - identification of every known model, the fallbacks and an image not
 *    read.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_reg_fix.h"

/* ---------------------------------------------------------------- cases */

TEST_CASE(the_image_stores_what_was_read_and_nothing_else)
{
    hitec_image_t img;

    hitec_image_init(&img, HITEC_D);
    CHECK(!hitec_image_known(&img, 0x00));
    CHECK(hitec_image_put_reg(&img, 0xFE, 0xBEEF));
    CHECK(hitec_image_known(&img, 0xFE));
    CHECK(hitec_image_known(&img, 0xFF));
    CHECK_EQ(hitec_image_reg(&img, 0xFE), 0xBEEF);
    CHECK_EQ(hitec_image_cell(&img, 0xFE), 0xEF);
    CHECK_EQ(hitec_image_cell(&img, 0xFF), 0xBE);
    CHECK(!hitec_image_put_reg(&img, 0x0D, 1));
    CHECK_EQ(hitec_image_reg(&img, 0x0D), 0);
    CHECK(!hitec_image_put_cell(&img, 0x00, 1));
    CHECK(!hitec_image_put_version(&img, 1));
    CHECK(!hitec_image_put_reg(NULL, 0x00, 1));
    CHECK(!hitec_image_known(NULL, 0x00));
    CHECK_EQ(hitec_image_reg(NULL, 0x00), 0);
    CHECK_EQ(hitec_image_cell(NULL, 0x00), 0);
    hitec_image_init(NULL, HITEC_D);

    hitec_image_init(&img, HITEC_CLASSIC);
    CHECK(hitec_image_put_cell(&img, 0xFF, 0x5A));
    CHECK(hitec_image_known(&img, 0xFF));
    CHECK(!hitec_image_known(&img, 0xFE));
    CHECK(!hitec_image_put_reg(&img, 0x00, 1));
    CHECK(!hitec_image_put_cell(NULL, 0x00, 1));
    CHECK(!hitec_image_put_version(NULL, 1));
    CHECK(hitec_image_put_version(&img, 0x26));
    CHECK_EQ(img.version, 0x26);
    CHECK_EQ(img.has_version, 1);
}

TEST_CASE(a_full_read_takes_21_registers_or_100_cells)
{
    size_t n = 0;
    const uint8_t *d = hitec_d_read_list(&n);
    const hitec_range_t *r;
    unsigned cells = 0;

    CHECK_EQ(n, 21);
    for (size_t i = 1; i < n; ++i) {
        CHECK(d[i] > d[i - 1]);
        CHECK_EQ(d[i] & 1u, 0);
    }
    CHECK(hitec_d_read_list(NULL) == d);
    r = hitec_c_read_ranges(&n);
    CHECK_EQ(n, 5);
    for (size_t i = 0; i < n; ++i) {
        cells += r[i].count;
    }
    CHECK_EQ(cells, 100);
    CHECK(hitec_c_read_ranges(NULL) == r);
    /* Every register a parameter reads is in the D series list. */
    for (unsigned id = 0; id < HITEC_P_COUNT; ++id) {
        const hitec_param_info_t *p = hitec_param(HITEC_LAYOUT_D, (uint8_t)id);
        bool found = false;
        bool found2 = p == NULL || p->addr2 == HITEC_ADDR_NONE;
        for (size_t i = 0; p != NULL && i < 21u; ++i) {
            found = found || d[i] == p->addr;
            found2 = found2 || d[i] == p->addr2;
        }
        CHECK(p == NULL || (found && found2));
    }
}

TEST_CASE(the_checksum_wraps_modulo_256)
{
    hitec_image_t img;

    hitec_image_init(&img, HITEC_CLASSIC);
    CHECK(!hitec_c_rebalance(&img));          /* nothing read */
    for (unsigned c = 0; c < 0x1Du; ++c) {
        hitec_image_put_cell(&img, (uint8_t)c, 0x00);
    }
    CHECK_EQ(hitec_c_balance(&img), 0x00);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0x00);
    /* 28 cells of 0xFF sum to 0x1BE4: the balance is 0x1C. */
    for (unsigned c = 0; c < 0x1Cu; ++c) {
        hitec_image_put_cell(&img, (uint8_t)c, 0xFF);
    }
    CHECK_EQ(hitec_c_balance(&img), 0x1C);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0xE4);
    CHECK(hitec_c_rebalance(&img));
    CHECK_EQ(img.mem[0x1C], 0x1C);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    /* Cells above 0x1C are not in the sum. */
    hitec_image_put_cell(&img, 0x1D, 0x77);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    /* One cell at 0x01 needs a balance of 0xFF. */
    for (unsigned c = 0; c < 0x1Cu; ++c) {
        hitec_image_put_cell(&img, (uint8_t)c, c == 5u ? 0x01 : 0x00);
    }
    CHECK_EQ(hitec_c_balance(&img), 0xFF);
    /* The bench reads balance. */
    hs5645(&img);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    CHECK_EQ(hitec_c_balance(&img), 0x8C);
    hs7955(&img);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    hs5955(&img);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    {
        uint8_t f[0x1D];
        memcpy(f, fix_hs5645_fac(), sizeof(f));
        unsigned s = 0;
        for (unsigned c = 0; c < sizeof(f); ++c) {
            s += f[c];
        }
        CHECK_EQ(s % 256u, 0);
    }
    CHECK_EQ(hitec_c_eeprom_sum(NULL), 0);
    CHECK_EQ(hitec_c_balance(NULL), 0);
    CHECK(!hitec_c_rebalance(NULL));
    d485(&img);
    CHECK(!hitec_c_rebalance(&img));
}

TEST_CASE(the_d_mirror_is_16384_less_the_position)
{
    uint16_t m = 0;

    CHECK(hitec_d_mirror(8192, &m));
    CHECK_EQ(m, 8192);
    CHECK(hitec_d_mirror(1, &m));
    CHECK_EQ(m, 16383);
    CHECK(hitec_d_mirror(16383, &m));
    CHECK_EQ(m, 1);
    /* The HFP-30's capture: 10105, 1492, 15730. */
    CHECK(hitec_d_mirror(10105, &m));
    CHECK_EQ(m, 6279);
    CHECK(hitec_d_mirror(15730, &m));
    CHECK_EQ(m, 654);
    CHECK(hitec_d_mirror(1492, &m));
    CHECK_EQ(m, 14892);
    m = 7;
    CHECK(!hitec_d_mirror(0, &m));
    CHECK(!hitec_d_mirror(16384, &m));
    CHECK_EQ(m, 7);
    CHECK(!hitec_d_mirror(8192, NULL));
}

TEST_CASE(the_models_are_known_by_number_and_name)
{
    hitec_image_t img;
    hitec_model_t m;

    d485(&img);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_D485HW);
    CHECK_EQ(m.layout, HITEC_LAYOUT_D);
    CHECK_EQ(m.src, HITEC_SRC_RCBENCH);
    CHECK_EQ(m.number, 34485);
    CHECK_STR_EQ(m.name, "");
    hitec_image_put_reg(&img, 0x00, 34645);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_D645MW);
    CHECK_EQ(m.src, HITEC_SRC_DOC);
    /* 485 is the number reported with another programmer. */
    hitec_image_put_reg(&img, 0x00, 485);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_D_OTHER);
    CHECK_EQ(m.layout, HITEC_LAYOUT_D);
    CHECK_EQ(m.number, 485);
    hitec_image_init(&img, HITEC_D);
    CHECK(!hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_NONE);

    hs5645(&img);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_HS5645MG);
    CHECK_EQ(m.layout, HITEC_LAYOUT_HS5);
    CHECK_EQ(m.src, HITEC_SRC_CAPTURED);
    CHECK_STR_EQ(m.name, "HS5645MG");
    CHECK_EQ(m.number, 0);
    hs7955(&img);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_HS7955TG);
    CHECK_EQ(m.layout, HITEC_LAYOUT_HS7);
    CHECK_STR_EQ(m.name, "HS7955");
    hs5955(&img);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_HS5955TG);
    CHECK_EQ(m.layout, HITEC_LAYOUT_HS5);
    CHECK_EQ(m.src, HITEC_SRC_READ);
    /* Other names take their series' layout, documentation only. */
    memcpy(&img.mem[0x60], "HS5245MG", 8);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_HS5_OTHER);
    CHECK_EQ(m.layout, HITEC_LAYOUT_HS5);
    CHECK_EQ(m.src, HITEC_SRC_DOC);
    memcpy(&img.mem[0x60], "HS7940TH", 8);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_HS7_OTHER);
    CHECK_EQ(m.layout, HITEC_LAYOUT_HS7);
    memcpy(&img.mem[0x60], "HSR5995T", 8);
    CHECK(hitec_identify(&img, &m));
    CHECK_EQ(m.model, HITEC_MODEL_C_OTHER);
    CHECK_EQ(m.layout, HITEC_LAYOUT_HS5);
    /* All 16 cells a name, bytes outside printable ASCII as '?'. */
    memcpy(&img.mem[0x60], "HS7\x01\x7F" "ABCDEFGHIJK", 16);
    CHECK(hitec_identify(&img, &m));
    CHECK_STR_EQ(m.name, "HS7??ABCDEFGHIJK");
    CHECK_EQ(m.model, HITEC_MODEL_HS7_OTHER);
    /* A name cell not read. */
    hitec_image_init(&img, HITEC_CLASSIC);
    for (unsigned c = 0x60; c < 0x6F; ++c) {
        hitec_image_put_cell(&img, (uint8_t)c, 'H');
    }
    CHECK(!hitec_identify(&img, &m));
    CHECK(!hitec_identify(NULL, &m));
    CHECK(!hitec_identify(&img, NULL));

    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_D485HW), "D485HW");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_HS5645MG), "HS-5645MG");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_HS7955TG), "HS-7955TG");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_HS5955TG), "HS-5955TG");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_D645MW), "D645MW");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_D_OTHER), "");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_NONE), "");
    CHECK_STR_EQ(hitec_model_name(HITEC_MODEL_COUNT), "");
    CHECK_EQ(hitec_layout_series(HITEC_LAYOUT_D), HITEC_D);
    CHECK_EQ(hitec_layout_series(HITEC_LAYOUT_HS5), HITEC_CLASSIC);
    CHECK_EQ(hitec_layout_series(HITEC_LAYOUT_HS7), HITEC_CLASSIC);
}

TEST_CASE(the_table_has_one_row_per_parameter_and_layout)
{
    /* Every parameter exists on its series' layouts, and only there. */
    static const struct {
        uint8_t id;
        uint8_t layouts;
        uint8_t access;
        uint8_t src;
    } want[] = {
        { HITEC_P_D_MODEL, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_POSITION, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_ID, 1, HITEC_RW, HITEC_SRC_RCBENCH },
        { HITEC_P_D_DIRECTION, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_NEUTRAL, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_LEFT_END, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_RIGHT_END, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_DEAD_BAND, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_SPEED, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_FAILSAFE, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_SOFT_START, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_OVERLOAD, 1, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_D_SMART_SENSOR, 1, HITEC_RW, HITEC_SRC_RCBENCH },
        { HITEC_P_D_SENSITIVITY, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_R04, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_R22, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_R8A, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_R8C, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_RD4, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_D_RD6, 1, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_C_VERSION, 6, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_C_DATE, 6, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_C_SERIAL, 6, HITEC_RO, HITEC_SRC_READ },
        { HITEC_P_C_CHECKSUM, 6, HITEC_RO, HITEC_SRC_CAPTURED },
        { HITEC_P_C_DEAD_BAND, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_SPEED, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_CENTRE, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_LEFT_END, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_RIGHT_END, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_DIRECTION, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_FAILSAFE, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_FS_POSITION, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_OVERLOAD, 6, HITEC_RW, HITEC_SRC_CAPTURED },
        { HITEC_P_C_RESOLUTION, 4, HITEC_RO, HITEC_SRC_CAPTURED },
    };
    const hitec_param_info_t *p;

    CHECK_EQ(sizeof(want) / sizeof(want[0]), HITEC_P_COUNT);
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
        CHECK_EQ(want[i].id, i);
        for (unsigned l = 0; l < HITEC_LAYOUT_COUNT; ++l) {
            p = hitec_param((uint8_t)l, want[i].id);
            if ((want[i].layouts & (1u << l)) == 0u) {
                CHECK(p == NULL);
                continue;
            }
            CHECK(p != NULL);
            if (p == NULL) {
                continue;
            }
            CHECK_EQ(p->id, want[i].id);
            if (want[i].id != HITEC_P_C_FS_POSITION || l == HITEC_LAYOUT_HS5) {
                CHECK_EQ(p->access, want[i].access);
                CHECK_EQ(p->src, want[i].src);
            }
            /* A writable row's range lies on its step. */
            if (p->access == HITEC_RW) {
                CHECK(p->step > 0u);
                CHECK_EQ((p->max - p->min) % p->step, 0);
            }
        }
    }
    /* The HS-7xxx fail-safe position is raw and read only. */
    p = hitec_param(HITEC_LAYOUT_HS7, HITEC_P_C_FS_POSITION);
    CHECK(p != NULL && p->access == HITEC_RO && p->src == HITEC_SRC_READ);
    /* The overload cell differs. */
    CHECK_EQ(hitec_param(HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD)->addr, 0x20);
    CHECK_EQ(hitec_param(HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD)->addr, 0x21);
    CHECK(hitec_param(HITEC_LAYOUT_COUNT, HITEC_P_D_ID) == NULL);
    CHECK(hitec_param(HITEC_LAYOUT_D, HITEC_P_COUNT) == NULL);
}

TEST_CASE(the_d485hw_read_decodes_as_the_hfp30_shows_it)
{
    hitec_image_t img;
    hitec_value_t v;

    d485(&img);
    v = get(&img, HITEC_LAYOUT_D, HITEC_P_D_MODEL, HITEC_GET_OK);
    CHECK_EQ(v.value, 34485);
    CHECK_EQ(v.raw, 34485);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_POSITION, HITEC_GET_OK).value,
             8129);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_ID, HITEC_GET_OK).value, 0);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                 HITEC_GET_OK).value, HITEC_CW);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, HITEC_GET_OK).value,
             8192);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END,
                 HITEC_GET_OK).value, 3351);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_RIGHT_END,
                 HITEC_GET_OK).value, 13003);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND,
                 HITEC_GET_OK).value, 1);
    /* The factory 4095 shows as 100%, the raw value beside it. */
    v = get(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, HITEC_GET_OK);
    CHECK_EQ(v.value, 100);
    CHECK_EQ(v.raw, 4095);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE,
                 HITEC_GET_OK).value, HITEC_FS_OFF);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START,
                 HITEC_GET_OK).value, 20);
    v = get(&img, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, HITEC_GET_OK);
    CHECK_EQ(v.value, HITEC_OVERLOAD_OFF);
    CHECK_EQ(v.raw, 100);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR,
                 HITEC_GET_OK).value, HITEC_ON);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_SENSITIVITY,
                 HITEC_GET_OK).value, 4095);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_R04, HITEC_GET_OK).value, 39);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_R22, HITEC_GET_OK).value, 0);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_R8A, HITEC_GET_OK).value,
             2500);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_R8C, HITEC_GET_OK).value,
             4350);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_RD4, HITEC_GET_OK).value, 0);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_RD6, HITEC_GET_OK).value, 0);
}

TEST_CASE(the_classic_reads_decode_as_the_hpp21_shows_them)
{
    hitec_image_t img;
    hitec_value_t v;

    hs5645(&img);
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_VERSION, HITEC_GET_OK);
    CHECK_EQ(v.value, 0x16);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DATE, HITEC_GET_OK).value,
             20151201);
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_SERIAL, HITEC_GET_OK);
    CHECK_EQ(v.value, 1515);
    CHECK_EQ(v.raw, 0);
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_CHECKSUM, HITEC_GET_OK);
    CHECK_EQ(v.value, HITEC_SUM_OK);
    CHECK_EQ(v.raw, 0);
    /* The dump's dead band 02 is not one of the HPP-21 Plus's steps. */
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DEAD_BAND, HITEC_GET_RAW);
    CHECK_EQ(v.value, 2);
    CHECK_EQ(v.raw, 2);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED, HITEC_GET_OK).value,
             100);
    /* 0x0F70 + 2052 = 6004: 1501us. */
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE, HITEC_GET_OK);
    CHECK_EQ(v.value, 6004);
    CHECK_EQ(v.raw, 0x0F70);
    /* CW: the right end in 0x13, the left in 0x14. */
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_RIGHT_END,
                 HITEC_GET_OK).value, 0xC2);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_LEFT_END,
                 HITEC_GET_OK).value, 0xA2);
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DIRECTION, HITEC_GET_OK);
    CHECK_EQ(v.value, HITEC_CW);
    CHECK_EQ(v.raw, 0x01);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_FAILSAFE,
                 HITEC_GET_OK).value, HITEC_OFF);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_FS_POSITION,
                 HITEC_GET_OK).value, 6004);
    /* 0x20 = FF: no encoding. */
    v = get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD, HITEC_GET_RAW);
    CHECK_EQ(v.value, 0xFF);
    get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_RESOLUTION, HITEC_GET_ERR_ARG);

    hs7955(&img);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_DATE, HITEC_GET_OK).value,
             20100726);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SERIAL, HITEC_GET_OK).value,
             825798);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_DEAD_BAND,
                 HITEC_GET_OK).value, 1);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SPEED, HITEC_GET_OK).value,
             100);
    /* 0x0F94 + 2048 = 6036. */
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_CENTRE, HITEC_GET_OK).value,
             6036);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_DIRECTION,
                 HITEC_GET_OK).value, HITEC_CCW);
    /* CCW: the left end in 0x13, the right in 0x14. */
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_LEFT_END,
                 HITEC_GET_OK).value, 0xA2);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_RIGHT_END,
                 HITEC_GET_OK).value, 0xB2);
    v = get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_FS_POSITION, HITEC_GET_OK);
    CHECK_EQ(v.value, 0x1770);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD,
                 HITEC_GET_OK).value, HITEC_OVERLOAD_OFF);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_RESOLUTION,
                 HITEC_GET_OK).value, HITEC_RES_NORMAL);
    img.mem[0x22] = 0x0B;   /* the factory value shows as High */
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_RESOLUTION,
                 HITEC_GET_OK).value, HITEC_RES_HIGH);
    img.mem[0x22] = 0x01;
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_RESOLUTION,
                 HITEC_GET_OK).value, HITEC_RES_HIGH);
    img.mem[0x22] = 0x02;
    get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_RESOLUTION, HITEC_GET_RAW);

    hs5955(&img);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DATE, HITEC_GET_OK).value,
             20080110);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE, HITEC_GET_OK).value,
             0x0FA0 + 2052);
    get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_DEAD_BAND, HITEC_GET_RAW);
    get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD, HITEC_GET_RAW);
}

TEST_CASE(a_value_fitting_no_encoding_is_raw_and_an_edit_sets_a_table_value)
{
    static const struct {
        uint8_t id;
        uint8_t addr;
        uint16_t raw;
        int32_t set;      /* the table value an edit sets */
        uint16_t stored;  /* the register it writes */
    } d[] = {
        { HITEC_P_D_ID, 0x32, 256, 7, 7 },
        { HITEC_P_D_DIRECTION, 0x5E, 2, HITEC_CCW, 1 },
        { HITEC_P_D_NEUTRAL, 0xC2, 16384, 8000, 8000 },
        { HITEC_P_D_LEFT_END, 0xB2, 65535, 3000, 3000 },
        /* the manual's step 2: the raw 2 is not the step 2 */
        { HITEC_P_D_DEAD_BAND, 0x4E, 2, 2, 4 },
        { HITEC_P_D_DEAD_BAND, 0x4E, 0, 1, 1 },
        { HITEC_P_D_SPEED, 0x54, 7, 30, 6 },
        { HITEC_P_D_SPEED, 0x54, 0, 10, 2 },
        { HITEC_P_D_SPEED, 0x54, 22, 100, 20 },
        { HITEC_P_D_FAILSAFE, 0x4C, 5001, 1500, 1500 },
        { HITEC_P_D_SOFT_START, 0x60, 10, 100, 100 },  /* the manual's 100% */
        { HITEC_P_D_OVERLOAD, 0x9C, 25, 30, 30 },
        { HITEC_P_D_OVERLOAD, 0x9C, 0, HITEC_OVERLOAD_OFF, 100 },
    };
    hitec_image_t img;
    hitec_value_t v;

    for (size_t i = 0; i < sizeof(d) / sizeof(d[0]); ++i) {
        d485(&img);
        hitec_image_put_reg(&img, d[i].addr, d[i].raw);
        v = get(&img, HITEC_LAYOUT_D, d[i].id, HITEC_GET_RAW);
        CHECK_EQ(v.value, d[i].raw);
        CHECK_EQ(v.raw, d[i].raw);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, d[i].id, d[i].set),
                 HITEC_SET_OK);
        CHECK_EQ(hitec_image_reg(&img, d[i].addr), d[i].stored);
        CHECK_EQ(get(&img, HITEC_LAYOUT_D, d[i].id, HITEC_GET_OK).value,
                 d[i].set);
    }
    /* The raw value is no value an edit takes. */
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 7),
             HITEC_SET_ERR_RANGE);
    CHECK(!hitec_param_ok(HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 25));
    /* A raw direction: the present direction is not known, so the edit
     * writes 0x5E alone and mirrors nothing, also beside a position the
     * mirror would leave. */
    d485(&img);
    hitec_image_put_reg(&img, 0x5E, 2);
    hitec_image_put_reg(&img, 0xB2, 0);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CW), HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0x5E), 0);
    CHECK_EQ(hitec_image_reg(&img, 0xB2), 0);
    CHECK_EQ(hitec_image_reg(&img, 0xC2), 8192);
    CHECK_EQ(hitec_image_reg(&img, 0xB0), 13003);
    /* From the decoded CW the mirror holds again. */
    hitec_image_put_reg(&img, 0xB2, 3351);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CCW), HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0xB2), 16384 - 13003);
    CHECK_EQ(hitec_image_reg(&img, 0xB0), 16384 - 3351);
    /* The smart sensor: neither the on nor the off pattern. */
    d485(&img);
    hitec_image_put_reg(&img, 0x6C, 2500);
    v = get(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_GET_RAW);
    CHECK_EQ(v.value, 2500);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR,
                             HITEC_ON), HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0x6C), 0);
    CHECK_EQ(hitec_image_reg(&img, 0x44), 0);
    /* The off pattern, and the comparison values not read. */
    hitec_image_put_reg(&img, 0x6C, 2500);
    hitec_image_put_reg(&img, 0x44, 4350);
    CHECK_EQ(get(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR,
                 HITEC_GET_OK).value, HITEC_OFF);
    img.known[0x8A / 8u] = 0;
    v = get(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_GET_UNKNOWN);
    CHECK_EQ(v.value, 0);
    CHECK_EQ(v.raw, 0);

    /* Classic. */
    hs7955(&img);
    img.mem[0x06] = 0x02;
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SPEED,
                 HITEC_GET_RAW).value, 2);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SPEED, 50),
             HITEC_SET_OK);
    CHECK_EQ(img.mem[0x06], 0x1C);
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
    img.mem[0x21] = 0x33;
    get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD, HITEC_GET_RAW);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD, 10),
             HITEC_SET_OK);
    CHECK_EQ(img.mem[0x21], 0x0A);
    CHECK_EQ(img.mem[0x20], 0x00);
    /* Dates: day 0, day 32, month 0, month 13. */
    static const uint8_t bad_dates[][2] = { { 0, 1 }, { 32, 1 }, { 1, 0 },
                                            { 1, 13 } };
    for (size_t i = 0; i < 4u; ++i) {
        img.mem[0x70] = bad_dates[i][0];
        img.mem[0x71] = bad_dates[i][1];
        v = get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_DATE, HITEC_GET_RAW);
        CHECK_EQ(v.raw, (uint32_t)bad_dates[i][0] << 24
                        | (uint32_t)bad_dates[i][1] << 16 | 0x07DAu);
    }
    img.mem[0x70] = 31;
    img.mem[0x71] = 12;
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_DATE, HITEC_GET_OK).value,
             20101231);
    /* Serial digits: a byte below '0' and one above '9'. */
    img.mem[0x86] = '/';
    get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SERIAL, HITEC_GET_RAW);
    img.mem[0x86] = ':';
    get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SERIAL, HITEC_GET_RAW);
    memcpy(&img.mem[0x80], "9999999", 7);
    CHECK_EQ(get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SERIAL,
                 HITEC_GET_OK).value, 9999999);
    /* A bad checksum is a value, not a fault. */
    hs7955(&img);
    img.mem[0x00] ^= 0x01;
    v = get(&img, HITEC_LAYOUT_HS7, HITEC_P_C_CHECKSUM, HITEC_GET_OK);
    CHECK_EQ(v.value, HITEC_SUM_BAD);
    CHECK_EQ(v.raw, 1);
}

TEST_CASE(a_parameter_needs_its_bytes_read)
{
    hitec_image_t img;
    hitec_value_t v;

    hitec_image_init(&img, HITEC_D);
    for (unsigned id = HITEC_P_D_MODEL; id <= HITEC_P_D_RD6; ++id) {
        v = get(&img, HITEC_LAYOUT_D, (uint8_t)id, HITEC_GET_UNKNOWN);
        CHECK_EQ(v.value, 0);
    }
    /* Smart sensor: 0x44 read, 0x6C not. */
    hitec_image_put_reg(&img, 0x44, 0);
    get(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR, HITEC_GET_UNKNOWN);
    hitec_image_init(&img, HITEC_CLASSIC);
    for (unsigned id = HITEC_P_C_VERSION; id < HITEC_P_COUNT; ++id) {
        get(&img, HITEC_LAYOUT_HS7, (uint8_t)id, HITEC_GET_UNKNOWN);
    }
    /* An end point needs 0x1B as well as its cell. */
    hitec_image_put_cell(&img, 0x13, 1);
    hitec_image_put_cell(&img, 0x14, 1);
    get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_LEFT_END, HITEC_GET_UNKNOWN);
    hitec_image_put_cell(&img, 0x1B, 1);
    get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_LEFT_END, HITEC_GET_OK);
    /* An edit needs 0x00 to 0x1C for the balance. */
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_LEFT_END, 5),
             HITEC_SET_ERR_UNKNOWN);

    /* Wrong series, wrong layout, NULL. */
    d485(&img);
    CHECK_EQ(hitec_param_get(&img, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED, &v),
             HITEC_GET_ERR_ARG);
    CHECK_EQ(v.value, 0);
    CHECK_EQ(hitec_param_get(&img, HITEC_LAYOUT_D, HITEC_P_C_SPEED, &v),
             HITEC_GET_ERR_ARG);
    CHECK_EQ(hitec_param_get(NULL, HITEC_LAYOUT_D, HITEC_P_D_ID, &v),
             HITEC_GET_ERR_ARG);
    CHECK_EQ(hitec_param_get(&img, HITEC_LAYOUT_D, HITEC_P_D_ID, NULL),
             HITEC_GET_ERR_ARG);
    CHECK_EQ(hitec_param_set(NULL, HITEC_LAYOUT_D, HITEC_P_D_ID, 1),
             HITEC_SET_ERR_ARG);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_SPEED, 50),
             HITEC_SET_ERR_ARG);
}

/* Set every writable parameter of @p layout to its smallest and largest
 * value and back, one past each end and off its step. */
static void round_trip(const hitec_image_t *base, uint8_t layout)
{
    for (unsigned n = 0; n < HITEC_P_COUNT; ++n) {
        const uint8_t id = (uint8_t)n;
        const hitec_param_info_t *p = hitec_param(layout, id);
        hitec_image_t img = *base;
        hitec_value_t v;

        if (p == NULL || p->access != HITEC_RW) {
            continue;
        }
        /* The D series ends and the neutral stay in order. */
        if (layout == HITEC_LAYOUT_D) {
            hitec_image_put_reg(&img, 0xB2, 1);
            hitec_image_put_reg(&img, 0xC2, 8192);
            hitec_image_put_reg(&img, 0xB0, 16383);
            if (id == HITEC_P_D_NEUTRAL) {
                hitec_image_put_reg(&img, 0xB2, 0);
            }
        }
        const int32_t ends[2] = {
            id == HITEC_P_D_LEFT_END ? 1 : p->min,
            id == HITEC_P_D_RIGHT_END ? (int32_t)HITEC_D_POS_MAX : p->max,
        };
        for (unsigned e = 0; e < 2u; ++e) {
            int32_t want = ends[e];
            if (id == HITEC_P_D_NEUTRAL) {
                want = e == 0u ? 1 : 16382;   /* inside the ends */
            }
            if (id == HITEC_P_D_LEFT_END && e == 1u) {
                want = 8191;
            }
            if (id == HITEC_P_D_RIGHT_END && e == 0u) {
                want = 8193;
            }
            const hitec_set_t r = hitec_param_set(&img, layout, id, want);
            if (r != HITEC_SET_OK) {
                T_FAIL("param %u layout %u value %d: set %d", id, layout,
                       want, (int)r);
                continue;
            }
            if (hitec_param_get(&img, layout, id, &v) != HITEC_GET_OK
                || v.value != want) {
                T_FAIL("param %u layout %u: %d back as %d", id, layout, want,
                       v.value);
            }
            if (layout != HITEC_LAYOUT_D) {
                CHECK_EQ(hitec_c_eeprom_sum(&img), 0);
            }
        }
        /* One step past each end, and between two steps. */
        hitec_image_t keep = img;
        const int32_t below = p->min - p->step;
        CHECK_EQ(hitec_param_set(&img, layout, id,
                                 p->unit == HITEC_UNIT_FS_US ? 899 : below),
                 HITEC_SET_ERR_RANGE);
        CHECK_EQ(hitec_param_set(&img, layout, id, p->max + p->step),
                 HITEC_SET_ERR_RANGE);
        CHECK(!hitec_param_ok(layout, id, p->max + p->step));
        if (p->step > 1u) {
            CHECK_EQ(hitec_param_set(&img, layout, id, p->min + 1),
                     HITEC_SET_ERR_RANGE);
        }
        CHECK(memcmp(&img, &keep, sizeof(img)) == 0);
    }
}

TEST_CASE(every_encoding_round_trips_at_its_limits)
{
    hitec_image_t img;

    d485(&img);
    round_trip(&img, HITEC_LAYOUT_D);
    hs5645(&img);
    img.mem[0x02] = 0x01;    /* dead band step 1 */
    img.mem[0x20] = 0x64;    /* overload off */
    hitec_c_rebalance(&img);
    round_trip(&img, HITEC_LAYOUT_HS5);
    hs7955(&img);
    round_trip(&img, HITEC_LAYOUT_HS7);
}

TEST_CASE(every_step_of_every_table_encodes_as_captured)
{
    static const uint16_t d_speed[] = { 2, 4, 6, 8, 10, 12, 14, 16, 18, 20 };
    static const uint16_t d_soft[] = { 1, 3, 6, 8, 100 };
    static const uint8_t c_speed[] = { 0x01, 0x07, 0x0E, 0x15, 0x1C, 0x23,
                                       0x2A, 0x31, 0x38, 0x40 };
    static const uint8_t step5[] = { 0x01, 0x04, 0x08, 0x0C, 0x10 };
    static const uint8_t overload[] = { 0x64, 0x0A, 0x14, 0x1E, 0x28, 0x32 };
    hitec_image_t img;
    hitec_image_t c5;
    hitec_image_t c7;

    d485(&img);
    for (unsigned i = 0; i < 10u; ++i) {
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED,
                                 (int32_t)(10u * (i + 1u))), HITEC_SET_OK);
        CHECK_EQ(hitec_image_reg(&img, 0x54), d_speed[i]);
    }
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START,
                                 (int32_t)(20u * (i + 1u))), HITEC_SET_OK);
        CHECK_EQ(hitec_image_reg(&img, 0x60), d_soft[i]);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND,
                                 (int32_t)(i + 1u)), HITEC_SET_OK);
        CHECK_EQ(hitec_image_reg(&img, 0x4E), step5[i]);
    }
    for (unsigned i = 0; i < 6u; ++i) {
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD,
                                 (int32_t)(10u * i)), HITEC_SET_OK);
        CHECK_EQ(hitec_image_reg(&img, 0x9C), overload[i]);
    }
    /* Fail-safe: limp, off, a position. */
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 1485),
             HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0x4C), 1485);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE,
                             HITEC_FS_LIMP), HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0x4C), 0);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE,
                             HITEC_FS_OFF), HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0x4C), 1);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 2),
             HITEC_SET_ERR_RANGE);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, -1),
             HITEC_SET_ERR_RANGE);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_ID, 255),
             HITEC_SET_OK);
    CHECK_EQ(hitec_image_reg(&img, 0x32), 255);

    hs5645(&c5);
    hs7955(&c7);
    for (unsigned i = 0; i < 10u; ++i) {
        CHECK_EQ(hitec_param_set(&c5, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED,
                                 (int32_t)(10u * (i + 1u))), HITEC_SET_OK);
        CHECK_EQ(c5.mem[0x06], c_speed[i]);
        CHECK_EQ(hitec_c_eeprom_sum(&c5), 0);
    }
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK_EQ(hitec_param_set(&c7, HITEC_LAYOUT_HS7, HITEC_P_C_DEAD_BAND,
                                 (int32_t)(i + 1u)), HITEC_SET_OK);
        CHECK_EQ(c7.mem[0x02], step5[i]);
    }
    /* HS-7xxx overload in 0x21 with 0x20 = 00; HS-5xxx in 0x20 with
     * 0x21 = FF. */
    c5.mem[0x20] = 0x64;
    hitec_c_rebalance(&c5);
    for (unsigned i = 0; i < 6u; ++i) {
        const int32_t pc = (int32_t)(10u * ((i + 1u) % 6u));
        CHECK_EQ(hitec_param_set(&c7, HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD,
                                 pc), HITEC_SET_OK);
        CHECK_EQ(c7.mem[0x21], overload[(i + 1u) % 6u]);
        CHECK_EQ(c7.mem[0x20], 0x00);
        CHECK_EQ(hitec_param_set(&c5, HITEC_LAYOUT_HS5, HITEC_P_C_OVERLOAD,
                                 pc), HITEC_SET_OK);
        CHECK_EQ(c5.mem[0x20], overload[(i + 1u) % 6u]);
        CHECK_EQ(c5.mem[0x21], 0xFF);
    }
}

TEST_CASE(the_clamp_takes_the_nearest_step)
{
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_SPEED, 0), 10);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_SPEED, 14), 10);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_SPEED, 15), 20);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_SPEED, 1000), 100);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 29), 20);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 30), 40);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 4), 0);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 5), 10);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 51), 50);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, -9), 0);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, -5),
             HITEC_FS_LIMP);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 0),
             HITEC_FS_LIMP);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 1),
             HITEC_FS_OFF);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 2), 900);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 5000),
             2100);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 0), 1);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 20000),
             16383);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE, 3000),
             3600);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_HS7, HITEC_P_C_DEAD_BAND, 9), 5);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_HS5, HITEC_P_C_RIGHT_END, 256),
             255);
    /* Read only and unknown: unchanged. */
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_D, HITEC_P_D_MODEL, 7), 7);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_HS5, HITEC_P_C_RESOLUTION, 7), 7);
    CHECK_EQ(hitec_param_clamp(HITEC_LAYOUT_COUNT, HITEC_P_D_ID, 7), 7);
    /* The ok test agrees. */
    CHECK(hitec_param_ok(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, HITEC_FS_LIMP));
    CHECK(hitec_param_ok(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 900));
    CHECK(!hitec_param_ok(HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE, 899));
    CHECK(!hitec_param_ok(HITEC_LAYOUT_D, HITEC_P_D_SPEED, 95));
    CHECK(!hitec_param_ok(HITEC_LAYOUT_D, HITEC_P_D_MODEL, 1));
    CHECK(!hitec_param_ok(HITEC_LAYOUT_HS5, HITEC_P_D_ID, 1));
}

TEST_CASE(edits_refuse_what_the_servo_would_not_take)
{
    hitec_image_t img;
    hitec_image_t keep;

    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_MODEL, 1),
             HITEC_SET_ERR_RO);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SENSITIVITY, 1),
             HITEC_SET_ERR_RO);
    /* The neutral past an end, an end past the neutral, equal values. */
    keep = img;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 13003),
             HITEC_SET_ERR_ORDER);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 3351),
             HITEC_SET_ERR_ORDER);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END, 8192),
             HITEC_SET_ERR_ORDER);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_RIGHT_END, 8192),
             HITEC_SET_ERR_ORDER);
    CHECK(memcmp(&img, &keep, sizeof(img)) == 0);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 13002),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END, 13001),
             HITEC_SET_OK);
    /* Beside a position that fits no encoding, on either side. */
    d485(&img);
    hitec_image_put_reg(&img, 0xB0, 16384);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 8000),
             HITEC_SET_ERR_RAW);
    hitec_image_put_reg(&img, 0xB0, 13003);
    hitec_image_put_reg(&img, 0xB2, 16384);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_RIGHT_END, 14000),
             HITEC_SET_ERR_RAW);
    hitec_image_put_reg(&img, 0xB2, 3351);
    hitec_image_put_reg(&img, 0xC2, 16384);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END, 3000),
             HITEC_SET_ERR_RAW);
    /* Smart sensor off with off values of 0: it would read as on. */
    d485(&img);
    hitec_image_put_reg(&img, 0x8A, 0);
    hitec_image_put_reg(&img, 0x8C, 0);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR,
                             HITEC_OFF), HITEC_SET_ERR_RAW);
    hitec_image_put_reg(&img, 0x8C, 1);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR,
                             HITEC_OFF), HITEC_SET_OK);
    /* The mirror leaves the range: a position at 0 or above 16383. */
    d485(&img);
    hitec_image_put_reg(&img, 0xB2, 0);
    keep = img;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CCW), HITEC_SET_ERR_MIRROR);
    CHECK(memcmp(&img, &keep, sizeof(img)) == 0);
    d485(&img);
    hitec_image_put_reg(&img, 0xB0, 16384);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CCW), HITEC_SET_ERR_MIRROR);
    d485(&img);
    hitec_image_put_reg(&img, 0xC2, 0);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CCW), HITEC_SET_ERR_MIRROR);
    /* An edit whose side effect needs a byte not read. */
    d485(&img);
    img.known[0xB0 / 8u] = 0;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CCW), HITEC_SET_ERR_UNKNOWN);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 8000),
             HITEC_SET_ERR_UNKNOWN);
    d485(&img);
    img.known[0x8C / 8u] = 0;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SMART_SENSOR,
                             HITEC_OFF), HITEC_SET_ERR_UNKNOWN);
    hs7955(&img);
    img.known[0x49 / 8u] = 0;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_DIRECTION,
                             HITEC_CW), HITEC_SET_ERR_UNKNOWN);
    hs7955(&img);
    img.known[0x20 / 8u] = (uint8_t)(img.known[0x20 / 8u] & ~1u);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS7, HITEC_P_C_OVERLOAD, 10),
             HITEC_SET_ERR_UNKNOWN);
    /* A value equal to the decoded one changes nothing: speed 100% stays
     * 4095, a centre edit to the same value keeps the end points. */
    d485(&img);
    keep = img;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 100),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                             HITEC_CW), HITEC_SET_OK);
    CHECK(memcmp(&img, &keep, sizeof(img)) == 0);
    hs5645(&img);
    keep = img;
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_HS5, HITEC_P_C_CENTRE, 6004),
             HITEC_SET_OK);
    CHECK(memcmp(&img, &keep, sizeof(img)) == 0);
}

int main(void)
{
    RUN(the_image_stores_what_was_read_and_nothing_else);
    RUN(a_full_read_takes_21_registers_or_100_cells);
    RUN(the_checksum_wraps_modulo_256);
    RUN(the_d_mirror_is_16384_less_the_position);
    RUN(the_models_are_known_by_number_and_name);
    RUN(the_table_has_one_row_per_parameter_and_layout);
    RUN(the_d485hw_read_decodes_as_the_hfp30_shows_it);
    RUN(the_classic_reads_decode_as_the_hpp21_shows_them);
    RUN(a_value_fitting_no_encoding_is_raw_and_an_edit_sets_a_table_value);
    RUN(a_parameter_needs_its_bytes_read);
    RUN(every_encoding_round_trips_at_its_limits);
    RUN(every_step_of_every_table_encodes_as_captured);
    RUN(the_clamp_takes_the_nearest_step);
    RUN(edits_refuse_what_the_servo_would_not_take);
    return test_summary("hitec_reg");
}
