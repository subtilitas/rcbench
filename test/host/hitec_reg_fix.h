/*
 * The bench reads the Hitec register model suites run on
 * (test_hitec_reg.c and test_hitec_reg_plan.c), and their helpers: the
 * D485HW's 128 registers through the IOmcu (2026-10-20), the HS-5645MG's
 * and HS-7955TG's cells 0x00 to 0x23 and factory blocks read by the HPP-21
 * Plus (2026-10-18), the HS-5955TG's cells through the IOmcu (2026-10-20),
 * and the name, date and serial cells of all three.  Only static inline
 * helpers; every suite includes the header whole.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"

#include "hitec_reg.h"

/* D485HW, even registers 0x00 to 0xFE, by address / 2. */
static inline const uint16_t *fix_d485(void)
{
    static const uint16_t k_d485[128] = {
        34485, 4097, 39, 33, 0, 0, 8129, 1,
        0, 505, 165, 0, 0, 0, 0, 0,
        20210, 0, 12071, 0, 0, 0, 0, 0,
        0, 0, 5, 2, 60, 60, 0, 0,
        300, 30, 0, 0, 0, 0, 1, 1,
        16383, 0, 4095, 4095, 0, 0, 0, 0,
        1, 0, 4095, 0, 0, 0, 0, 0,
        0, 0, 65535, 0, 0, 100, 5000, 32768,
        0, 0, 0, 550, 30, 2500, 4350, 0,
        0, 10324, 0, 0, 200, 3, 100, 0,
        50, 1, 10, 64, 64, 64, 0, 0,
        13003, 3351, 0, 0, 950, 24, 33568, 0,
        2, 8192, 1300, 0, 10, 65280, 0, 518,
        50, 0, 0, 0, 16383, 0, 1000, 0,
        0, 0, 0, 10260, 2048, 2966, 48093, 5404,
        12825, 1026, 65280, 10400, 0, 1, 0, 0,
    };
    return k_d485;
}

static inline const uint8_t *fix_hs5645(void)
{
    static const uint8_t k_hs5645[0x24] = {
        0x50, 0x6e, 0x02, 0xa0, 0x01, 0x08, 0x40, 0x0f, 0x70,
        0x0b, 0xb8, 0x23, 0x28, 0x00, 0x0a, 0x03, 0xe8, 0x17,
        0x70, 0xc2, 0xa2, 0x03, 0xe8, 0x03, 0xe8, 0x17, 0x70,
        0x01, 0x8c, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    return k_hs5645;
}
static inline const uint8_t *fix_hs5645_fac(void)
{
    static const uint8_t k_hs5645_fac[0x24] = {
        0x50, 0x6e, 0x02, 0xa0, 0x01, 0x08, 0x40, 0x0f, 0x70,
        0x0b, 0xb8, 0x23, 0x28, 0x00, 0x0a, 0x03, 0xe8, 0x17,
        0x70, 0x74, 0x74, 0x03, 0xe8, 0x03, 0xe8, 0x17, 0x70,
        0x01, 0x08, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    return k_hs5645_fac;
}
static inline const uint8_t *fix_hs7955(void)
{
    static const uint8_t k_hs7955[0x24] = {
        0xe6, 0xfa, 0x01, 0x23, 0x01, 0x1b, 0x40, 0x0f, 0x94,
        0x0b, 0xb8, 0x23, 0x28, 0x00, 0x04, 0x03, 0xfc, 0x17,
        0x70, 0xa2, 0xb2, 0x01, 0x4a, 0x05, 0xdc, 0x17, 0x70,
        0x00, 0x5e, 0x46, 0x00, 0x32, 0x00, 0x64, 0x00, 0x01,
    };
    return k_hs7955;
}
static inline const uint8_t *fix_hs7955_fac(void)
{
    static const uint8_t k_hs7955_fac[0x24] = {
        0xe6, 0xfa, 0x01, 0x23, 0x01, 0x1b, 0x40, 0x0f, 0x70,
        0x0b, 0xb8, 0x23, 0x28, 0x00, 0x04, 0x03, 0xfc, 0x17,
        0x70, 0xdc, 0xdc, 0x01, 0x4a, 0x05, 0xdc, 0x17, 0x70,
        0x01, 0x1d, 0x46, 0x00, 0x32, 0x00, 0x64, 0x0b, 0x01,
    };
    return k_hs7955_fac;
}
static inline const uint8_t *fix_hs5955(void)
{
    static const uint8_t k_hs5955[0x24] = {
        0x78, 0xb4, 0x02, 0x46, 0x01, 0x05, 0x40, 0x0f, 0xa0,
        0x0b, 0xb8, 0x23, 0x28, 0x00, 0x0a, 0x03, 0xe8, 0x17,
        0x70, 0x74, 0x74, 0x03, 0xe8, 0x03, 0xe8, 0x17, 0x70,
        0x01, 0xc7, 0x12, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff,
    };
    return k_hs5955;
}

static inline void d485(hitec_image_t *img)
{
    hitec_image_init(img, HITEC_D);
    for (unsigned i = 0; i < 128u; ++i) {
        CHECK(hitec_image_put_reg(img, (uint8_t)(2u * i), fix_d485()[i]));
    }
}

/* A classic servo: every cell 0xFF, then the settings, the factory block,
 * the 16 name cells, the date and the 8 serial cells as read. */
static inline void classic(hitec_image_t *img, const uint8_t *set,
                    const uint8_t *fac, const char *name, const uint8_t *date,
                    const char *serial, uint8_t version)
{
    hitec_image_init(img, HITEC_CLASSIC);
    for (unsigned c = 0; c < 256u; ++c) {
        CHECK(hitec_image_put_cell(img, (uint8_t)c, 0xFF));
    }
    for (unsigned c = 0; c < 0x24u; ++c) {
        hitec_image_put_cell(img, (uint8_t)c, set[c]);
        hitec_image_put_cell(img, (uint8_t)(0x30u + c), fac[c]);
    }
    for (unsigned c = 0; c < 16u; ++c) {
        hitec_image_put_cell(img, (uint8_t)(0x60u + c), (uint8_t)name[c]);
    }
    for (unsigned c = 0; c < 4u; ++c) {
        hitec_image_put_cell(img, (uint8_t)(0x70u + c), date[c]);
    }
    for (unsigned c = 0; c < 8u; ++c) {
        hitec_image_put_cell(img, (uint8_t)(0x80u + c), (uint8_t)serial[c]);
    }
    CHECK(hitec_image_put_version(img, version));
}

static inline void hs5645(hitec_image_t *img)
{
    static const uint8_t date[4] = { 0x01, 0x0c, 0x07, 0xdf };
    classic(img, fix_hs5645(), fix_hs5645_fac(),
            "HS5645MG\0\0\0\0\0\0\0\0", date, "0001515", 0x16);
}

static inline void hs7955(hitec_image_t *img)
{
    static const uint8_t date[4] = { 0x1a, 0x07, 0x07, 0xda };
    classic(img, fix_hs7955(), fix_hs7955_fac(), "HS7955\0TIALIZED\0", date,
            "0825798", 0x26);
}

static inline void hs5955(hitec_image_t *img)
{
    static const uint8_t date[4] = { 0x0a, 0x01, 0x07, 0xd8 };
    classic(img, fix_hs5955(), fix_hs5955(), "HS5955TG\0ALIZED\0", date,
            "0694247",
            0x15);
}

static inline hitec_value_t get(const hitec_image_t *img, uint8_t layout,
                         uint8_t id, hitec_get_t want)
{
    hitec_value_t v;
    const hitec_get_t r = hitec_param_get(img, layout, id, &v);
    if (r != want) {
        T_FAIL("param %u: get %d, want %d", id, (int)r, (int)want);
    }
    return v;
}

static inline void check_step(const hitec_step_t *s, uint8_t kind, uint8_t addr,
                       uint16_t value)
{
    if (s->kind != kind || s->addr != addr || s->value != value) {
        T_FAIL("step %u %02X %u, want %u %02X %u", s->kind, s->addr,
               s->value, kind, addr, value);
    }
}

/* A D series plan's tail: the save and the reboot. */
static inline void check_save(const hitec_plan_t *p)
{
    CHECK(p->n >= 2u);
    if (p->n >= 2u) {
        check_step(&p->step[p->n - 2u], HITEC_STEP_D_SAVE, 0x70, 0xFFFF);
        CHECK_EQ(p->step[p->n - 2u].wait_ms, 100);
        check_step(&p->step[p->n - 1u], HITEC_STEP_D_REBOOT, 0x46, 1);
        CHECK_EQ(p->step[p->n - 1u].wait_ms, 1000);
    }
}

/* Edit a copy of @p from and plan it. */
static inline hitec_plan_t plan_edit(const hitec_image_t *from, uint8_t layout,
                              uint8_t id, int32_t value)
{
    hitec_image_t to = *from;
    hitec_plan_t p;

    CHECK_EQ(hitec_param_set(&to, layout, id, value), HITEC_SET_OK);
    CHECK_EQ(hitec_plan(from, &to, &p), HITEC_PLAN_OK);
    return p;
}
