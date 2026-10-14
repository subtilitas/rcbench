/*
 * Writing a panel image into the slot that is not running.
 *
 * The failures under test: a flash touched by an image that was not
 * checked first, a flash touched while the bench is not quiet (each erase
 * stalls both cores, the control task's STOP with them), a write that went
 * wrong left as the slot that starts next, and the running build written
 * over itself for nothing.  The flash is memory with a budget on it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "fw_install.h"

#define SLOT 0x3E0000u

static uint8_t *g_img;
static uint8_t *g_slot;
static uint32_t g_len;
static unsigned g_begins, g_writes, g_finishes, g_aborts;
static int      g_write_budget;   /* writes left before one fails; -1 none */
static bool     g_finish_fails;
static bool     g_boot_switched;
static bool     g_begin_fails;

static bool f_begin(void *ctx, uint32_t bytes)
{
    (void)ctx;
    ++g_begins;
    return !g_begin_fails && bytes <= SLOT;
}

static bool f_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t len)
{
    (void)ctx;
    ++g_writes;
    if (g_write_budget == 0) {
        return false;
    }
    if (g_write_budget > 0) {
        --g_write_budget;
    }
    memset(g_slot + off, 0xFF, len);         /* the erase it does first */
    memcpy(g_slot + off, src, len);
    return true;
}

static bool f_finish(void *ctx)
{
    (void)ctx;
    ++g_finishes;
    if (g_finish_fails) {
        return false;
    }
    g_boot_switched = true;
    return true;
}

static void f_abort(void *ctx)
{
    (void)ctx;
    ++g_aborts;
}

static const fw_flash_t k_flash = { f_begin, f_write, f_finish, f_abort,
                                    NULL };

static unsigned ops(void)
{
    return g_begins + g_writes + g_finishes + g_aborts;
}

static void fresh(void)
{
    fw_fix_t f = fw_fix_default();
    f.body = 200000u;                        /* 4 writes of 64 kB */
    g_len = fw_fix_make(&f, g_img);
    memset(g_slot, 0xA5, SLOT);
    g_begins = g_writes = g_finishes = g_aborts = 0u;
    g_write_budget = -1;
    g_finish_fails = false;
    g_boot_switched = false;
}

static fw_install_status_t run(fw_install_t *in)
{
    fw_install_status_t st;
    do {
        st = fw_install_step(in, true);
    } while (st == FW_INSTALL_RUNNING);
    return st;
}

TEST_CASE(a_checked_image_is_written_whole_and_starts_next)
{
    fresh();
    const fw_image_info_t r = fw_fix_running("0.18.9", 0x11);
    fw_install_t in;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    CHECK_EQ(in.offer, FW_OFFER_NEWER);
    CHECK_EQ(ops(), 0u);                     /* beginning touches nothing */
    unsigned steps = 0u;
    fw_install_status_t st;
    do {
        st = fw_install_step(&in, true);
        ++steps;
        if (st == FW_INSTALL_RUNNING && in.done < in.bytes) {
            CHECK(in.done % FW_WRITE_CHUNK == 0u);
        }
    } while (st == FW_INSTALL_RUNNING);
    CHECK_EQ(st, FW_INSTALL_DONE);
    /* Open, one write a 64 kB block, the finish. */
    const unsigned blocks = (g_len + FW_WRITE_CHUNK - 1u) / FW_WRITE_CHUNK;
    CHECK_EQ(blocks, 4u);
    CHECK_EQ(steps, 1u + blocks + 1u);
    CHECK_EQ(g_writes, blocks);
    CHECK_EQ(g_finishes, 1u);
    CHECK_EQ(g_aborts, 0u);
    CHECK(g_boot_switched);
    CHECK_EQ(memcmp(g_slot, g_img, g_len), 0);
    CHECK_EQ(fw_install_step(&in, true), FW_INSTALL_IDLE);
}

TEST_CASE(nothing_is_written_while_the_bench_is_not_quiet)
{
    fresh();
    const fw_image_info_t r = fw_fix_running("0.18.9", 0x11);
    fw_install_t in;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    /* Waiting before the first step and between any two. */
    for (int step = 0; step < 6; ++step) {
        const unsigned before = ops();
        for (int i = 0; i < 3; ++i) {
            CHECK_EQ(fw_install_step(&in, false), FW_INSTALL_WAITING);
        }
        CHECK_EQ(ops(), before);
        if (fw_install_step(&in, true) == FW_INSTALL_DONE) {
            break;
        }
    }
    CHECK(g_boot_switched);
}

TEST_CASE(every_refused_image_is_refused_before_the_flash)
{
    const fw_image_info_t r = fw_fix_running("0.18.9", 0x11);
    fw_install_t in;
    fresh();
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, 0u, SLOT, &r),
             FW_ERR_EMPTY);
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, g_len - 1u, &r),
             FW_ERR_TOO_BIG);
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len - 1u, SLOT, &r),
             FW_ERR_SHORT);
    g_img[1000] ^= 1u;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r),
             FW_ERR_HASH);
    fw_fix_t f = fw_fix_default();
    f.chip = 2u;
    g_len = fw_fix_make(&f, g_img);
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r),
             FW_ERR_CHIP);
    f = fw_fix_default();
    f.project = "other";
    g_len = fw_fix_make(&f, g_img);
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r),
             FW_ERR_PROJECT);
    CHECK_EQ(fw_install_begin(&in, &k_flash, NULL, g_len, SLOT, &r),
             FW_ERR_MEMORY);
    CHECK_EQ(fw_install_begin(&in, NULL, g_img, g_len, SLOT, &r),
             FW_ERR_FLASH);
    /* Refused, the installer is idle and steps touch nothing. */
    CHECK_EQ(fw_install_step(&in, true), FW_INSTALL_IDLE);
    CHECK_EQ(ops(), 0u);
}

TEST_CASE(the_running_build_is_not_written_over_itself)
{
    fresh();
    const fw_image_info_t r = fw_fix_running("0.19.0", 0x5A);
    fw_install_t in;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r),
             FW_ERR_RUNNING);
    CHECK_EQ(fw_install_step(&in, true), FW_INSTALL_IDLE);
    CHECK_EQ(ops(), 0u);
}

TEST_CASE(an_older_release_and_another_build_are_installed_when_asked)
{
    fresh();
    fw_install_t in;
    fw_image_info_t r = fw_fix_running("0.20.1", 0x5A);
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    CHECK_EQ(in.offer, FW_OFFER_OLDER);
    CHECK_EQ(run(&in), FW_INSTALL_DONE);
    fresh();
    r = fw_fix_running("0.19.0", 0x33);
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    CHECK_EQ(in.offer, FW_OFFER_REBUILD);
    CHECK_EQ(run(&in), FW_INSTALL_DONE);
}

TEST_CASE(a_write_that_fails_part_way_leaves_the_boot_slot_alone)
{
    fresh();
    const fw_image_info_t r = fw_fix_running("0.18.9", 0x11);
    fw_install_t in;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    g_write_budget = 2;
    CHECK_EQ(run(&in), FW_INSTALL_FAILED);
    CHECK_EQ(in.err, FW_ERR_FLASH);
    CHECK_EQ(g_writes, 3u);
    CHECK_EQ(g_finishes, 0u);
    CHECK_EQ(g_aborts, 1u);
    CHECK(!g_boot_switched);
    CHECK_EQ(fw_install_step(&in, true), FW_INSTALL_IDLE);
}

TEST_CASE(a_finish_that_fails_leaves_the_boot_slot_alone)
{
    fresh();
    const fw_image_info_t r = fw_fix_running("0.18.9", 0x11);
    fw_install_t in;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    g_finish_fails = true;
    fw_install_status_t st;
    do {
        st = fw_install_step(&in, true);
        if (st == FW_INSTALL_RUNNING && in.done == in.bytes) {
            CHECK(fw_install_finishing(&in));
        }
    } while (st == FW_INSTALL_RUNNING);
    CHECK_EQ(st, FW_INSTALL_FAILED);
    CHECK_EQ(g_aborts, 1u);
    CHECK(!g_boot_switched);
    CHECK(!fw_install_finishing(&in));
}

TEST_CASE(a_slot_that_will_not_open_writes_nothing)
{
    fresh();
    const fw_image_info_t r = fw_fix_running("0.18.9", 0x11);
    fw_install_t in;
    CHECK_EQ(fw_install_begin(&in, &k_flash, g_img, g_len, SLOT, &r), FW_OK);
    g_begin_fails = true;
    CHECK_EQ(fw_install_step(&in, true), FW_INSTALL_FAILED);
    g_begin_fails = false;
    CHECK_EQ(in.err, FW_ERR_FLASH);
    CHECK_EQ(g_writes, 0u);
    CHECK_EQ(g_aborts, 0u);                 /* nothing was opened */
    CHECK(!g_boot_switched);
}

/* ---------------------------------------------------------------- gate */

TEST_CASE(the_gate_names_the_first_reason_that_holds)
{
    CHECK_EQ(fw_gate(false, false, false, false), FW_GATE_OPEN);
    CHECK_EQ(fw_gate(true, false, false, false), FW_GATE_ARMED);
    CHECK_EQ(fw_gate(false, true, false, false), FW_GATE_SUPPLY);
    CHECK_EQ(fw_gate(false, false, true, false), FW_GATE_RUN);
    CHECK_EQ(fw_gate(false, false, false, true), FW_GATE_DRAWING);
    CHECK_EQ(fw_gate(true, true, true, true), FW_GATE_ARMED);
    CHECK_EQ(fw_gate(false, true, true, true), FW_GATE_SUPPLY);
    CHECK_EQ(fw_gate(false, false, true, true), FW_GATE_RUN);
}

int main(void)
{
    g_img = malloc(SLOT + 64u);
    g_slot = malloc(SLOT + 64u);
    REQUIRE_ALLOC(g_img);
    REQUIRE_ALLOC(g_slot);
    RUN(a_checked_image_is_written_whole_and_starts_next);
    RUN(nothing_is_written_while_the_bench_is_not_quiet);
    RUN(every_refused_image_is_refused_before_the_flash);
    RUN(the_running_build_is_not_written_over_itself);
    RUN(an_older_release_and_another_build_are_installed_when_asked);
    RUN(a_write_that_fails_part_way_leaves_the_boot_slot_alone);
    RUN(a_finish_that_fails_leaves_the_boot_slot_alone);
    RUN(a_slot_that_will_not_open_writes_nothing);
    RUN(the_gate_names_the_first_reason_that_holds);
    free(g_img);
    free(g_slot);
    return test_summary("fw_install");
}
