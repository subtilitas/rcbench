/*
 * The C6 radio's update (fw_c6.c): the carried image's check, the C6 RADIO
 * row's state and the order of its reasons, and the update's steps: every
 * answer of every step, a late answer and another update's answer
 * ignored, each step's limit at one millisecond under it and at it, the
 * same across the 32-bit millisecond wrap, the activate call only from
 * 2.6.0 on, and a failed write closed without an activate.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_c6.h"
#include "fw_fixture.h"

static uint8_t *g_img;

static const fw_version_t k_carried = { 2u, 13u, 0u };
static const fw_version_t k_old = { 0u, 0u, 0u };
static const fw_version_t k_26 = { 2u, 6u, 0u };
static const fw_version_t k_25 = { 2u, 5u, 9u };

static fw_fix_t c6_fix(void)
{
    fw_fix_t f = fw_fix_default();
    f.chip = FW_CHIP_ESP32C6;
    f.project = FW_C6_PROJECT;
    f.version = "2.13.0";
    return f;
}

/* ------------------------------------------------------------ the image */

TEST_CASE(the_c6_image_passes_and_says_its_version)
{
    const fw_fix_t f = c6_fix();
    const uint32_t len = fw_fix_make(&f, g_img);
    fw_image_info_t info;
    CHECK_EQ(fw_c6_image_check(g_img, len, &info), FW_OK);
    CHECK_EQ(info.bytes, len);
    CHECK_EQ(info.version.major, 2u);
    CHECK_EQ(info.version.minor, 13u);
    CHECK_EQ(info.version.patch, 0u);
}

TEST_CASE(a_panel_image_is_no_c6_image)
{
    fw_fix_t f = c6_fix();
    f.chip = FW_CHIP_ESP32P4;
    uint32_t len = fw_fix_make(&f, g_img);
    CHECK_EQ(fw_c6_image_check(g_img, len, NULL), FW_ERR_CHIP);
    f = c6_fix();
    f.project = FW_PROJECT;
    len = fw_fix_make(&f, g_img);
    CHECK_EQ(fw_c6_image_check(g_img, len, NULL), FW_ERR_PROJECT);
    /* And the panel's own check refuses a C6 image. */
    f = c6_fix();
    len = fw_fix_make(&f, g_img);
    CHECK_EQ(fw_image_check(g_img, len, 0x3E0000u, NULL), FW_ERR_CHIP);
}

TEST_CASE(no_image_and_a_damaged_one_are_refused)
{
    CHECK_EQ(fw_c6_image_check(NULL, 0u, NULL), FW_ERR_EMPTY);
    const fw_fix_t f = c6_fix();
    const uint32_t len = fw_fix_make(&f, g_img);
    g_img[len / 2u] ^= 0x01u;
    CHECK_EQ(fw_c6_image_check(g_img, len, NULL), FW_ERR_HASH);
}

TEST_CASE(the_c6_slot_is_the_limit_to_the_byte)
{
    fw_fix_t f = c6_fix();
    f.body = 0u;
    const uint32_t empty = fw_fix_len(&f);
    /* The body that makes the image the slot's size exactly: segments of
     * 3, the length padded to 16 and the hash. */
    f.body = FW_C6_SLOT_BYTES - empty - 15u;
    while (fw_fix_len(&f) < FW_C6_SLOT_BYTES) {
        ++f.body;
    }
    while (fw_fix_len(&f) > FW_C6_SLOT_BYTES) {
        --f.body;
    }
    uint32_t len = fw_fix_make(&f, g_img);
    CHECK_EQ(len, FW_C6_SLOT_BYTES);
    CHECK_EQ(fw_c6_image_check(g_img, len, NULL), FW_OK);
    f.body += 16u;
    len = fw_fix_make(&f, g_img);
    CHECK(len > FW_C6_SLOT_BYTES);
    CHECK_EQ(fw_c6_image_check(g_img, len, NULL), FW_ERR_TOO_BIG);
}

/* ------------------------------------------------------------- the row */

static fw_c6_feed_t ready_feed(void)
{
    fw_c6_feed_t f;
    memset(&f, 0, sizeof(f));
    f.have_image = true;
    f.image_ok = true;
    f.carried = k_carried;
    return f;
}

TEST_CASE(a_c6_that_reports_no_version_is_offered_the_update)
{
    fw_c6_feed_t f = ready_feed();
    fw_c6_why_t why = FW_C6_WHY_RADIO;
    CHECK_EQ(fw_c6_row(&f, &why), FW_C6_ROW_READY);
    CHECK_EQ(why, FW_C6_WHY_NONE);
    f.known = true;
    f.running = k_old;
    CHECK_EQ(fw_c6_row(&f, &why), FW_C6_ROW_READY);
    f.running = k_carried;
    CHECK_EQ(fw_c6_row(&f, &why), FW_C6_ROW_SAME);
    /* A newer C6 than carried is offered the carried one: the host side
     * matches the carried image. */
    f.running.minor = 14u;
    CHECK_EQ(fw_c6_row(&f, NULL), FW_C6_ROW_READY);
}

TEST_CASE(the_row_names_the_first_reason_in_order)
{
    CHECK_EQ(fw_c6_row(NULL, NULL), FW_C6_ROW_NO_IMAGE);
    fw_c6_feed_t f = ready_feed();
    f.armed = f.supply = f.run = f.drawing = f.iomcu = f.installing = true;
    f.radio_up = f.ending = true;
    static const fw_c6_why_t order[] = {
        FW_C6_WHY_ARMED, FW_C6_WHY_SUPPLY, FW_C6_WHY_RUN, FW_C6_WHY_DRAWING,
        FW_C6_WHY_IOMCU, FW_C6_WHY_INSTALL, FW_C6_WHY_RADIO, FW_C6_WHY_ENDING,
    };
    bool *flags[] = { &f.armed, &f.supply, &f.run, &f.drawing, &f.iomcu,
                      &f.installing, &f.radio_up, &f.ending };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        fw_c6_why_t why = FW_C6_WHY_NONE;
        CHECK_EQ(fw_c6_row(&f, &why), FW_C6_ROW_BLOCKED);
        CHECK_EQ(why, order[i]);
        *flags[i] = false;
    }
    CHECK_EQ(fw_c6_row(&f, NULL), FW_C6_ROW_READY);
}

TEST_CASE(image_update_link_and_same_come_before_the_bench)
{
    fw_c6_feed_t f = ready_feed();
    f.armed = true;
    f.known = true;
    f.running = k_carried;
    CHECK_EQ(fw_c6_row(&f, NULL), FW_C6_ROW_SAME);
    f.link_failed = true;
    CHECK_EQ(fw_c6_row(&f, NULL), FW_C6_ROW_LINK_FAILED);
    f.updating = true;
    CHECK_EQ(fw_c6_row(&f, NULL), FW_C6_ROW_UPDATING);
    f.image_ok = false;
    CHECK_EQ(fw_c6_row(&f, NULL), FW_C6_ROW_BAD_IMAGE);
    f.have_image = false;
    fw_c6_why_t why = FW_C6_WHY_RADIO;
    CHECK_EQ(fw_c6_row(&f, &why), FW_C6_ROW_NO_IMAGE);
    CHECK_EQ(why, FW_C6_WHY_NONE);
}

/* ------------------------------------------------------------ the steps */

#define T0 1000u
#define WRAP_T0 (0xFFFFFFFFu - 99u)   /* 100ms before the wrap */
#define TOTAL (3u * FW_C6_RUN_BYTES + 100u)

static fw_c6_ota_t g_o;

/* A step a case cannot go on without: names the line and ends the
 * process. */
#define MUST(cond)                                                            \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("      %s:%d: expected %s\n", __FILE__, __LINE__, #cond);  \
            fflush(stdout);                                                   \
            abort();                                                          \
        }                                                                     \
    } while (0)

static fw_c6_step_t next(uint32_t now)
{
    fw_c6_step_t s;
    memset(&s, 0, sizeof(s));
    MUST(fw_c6_ota_next(&g_o, now, &s));
    return s;
}

static bool ok(void)
{
    return fw_c6_ota_answer(&g_o, g_o.gen, 0, false, k_old);
}

static bool fail(int32_t err)
{
    return fw_c6_ota_answer(&g_o, g_o.gen, err, false, k_old);
}

static void start(uint32_t t0)
{
    fw_c6_ota_init(&g_o);
    MUST(fw_c6_ota_start(&g_o, TOTAL, k_carried, t0));
}

/* Through LINK with @p known/@p running and BEGIN, to the first write. */
static void to_write(uint32_t t0, bool known, fw_version_t running)
{
    start(t0);
    CHECK_EQ(next(t0).act, FW_C6_ACT_LINK);
    CHECK(fw_c6_ota_answer(&g_o, g_o.gen, 0, known, running));
    CHECK_EQ(next(t0).act, FW_C6_ACT_BEGIN);
    CHECK(ok());
}

/* Every write to the end. */
static void write_all(uint32_t t)
{
    uint32_t want = 0u;
    while (g_o.phase == FW_C6_PH_WRITE) {
        const fw_c6_step_t s = next(t);
        CHECK_EQ(s.act, FW_C6_ACT_WRITE);
        CHECK_EQ(s.offset, want);
        const uint32_t left = TOTAL - want;
        CHECK_EQ(s.len, left < FW_C6_RUN_BYTES ? left : FW_C6_RUN_BYTES);
        want += s.len;
        CHECK(ok());
    }
    CHECK_EQ(g_o.done, TOTAL);
    CHECK_EQ(fw_c6_ota_percent(&g_o), 100u);
}

TEST_CASE(an_old_c6_is_updated_without_the_activate_call)
{
    to_write(T0, false, k_old);
    CHECK_EQ(fw_c6_ota_percent(&g_o), 0u);
    write_all(T0);
    CHECK_EQ(g_o.phase, FW_C6_PH_END);
    CHECK_EQ(next(T0).act, FW_C6_ACT_END);
    CHECK(ok());
    CHECK_EQ(g_o.phase, FW_C6_PH_ENDED);
    CHECK_EQ(g_o.result, FW_C6_RES_UPDATED);
    CHECK(!fw_c6_ota_active(&g_o));
    fw_c6_step_t s;
    CHECK(!fw_c6_ota_next(&g_o, T0, &s));
}

TEST_CASE(a_c6_from_2_6_0_is_activated_after_its_check)
{
    to_write(T0, true, k_26);
    write_all(T0);
    CHECK_EQ(next(T0).act, FW_C6_ACT_END);
    CHECK(ok());
    CHECK_EQ(next(T0).act, FW_C6_ACT_ACTIVATE);
    CHECK(ok());
    CHECK_EQ(g_o.result, FW_C6_RES_UPDATED);
    /* 2.5.9 is taken for an old one. */
    to_write(T0, true, k_25);
    write_all(T0);
    (void)next(T0);
    CHECK(ok());
    CHECK_EQ(g_o.result, FW_C6_RES_UPDATED);
    CHECK(!fw_c6_has_activate(false, k_carried));
    CHECK(fw_c6_has_activate(true, k_carried));
}

TEST_CASE(a_c6_that_runs_the_carried_version_is_sent_nothing)
{
    start(T0);
    (void)next(T0);
    CHECK(fw_c6_ota_answer(&g_o, g_o.gen, 0, true, k_carried));
    CHECK_EQ(g_o.result, FW_C6_RES_SAME);
    CHECK(!fw_c6_ota_active(&g_o));
}

TEST_CASE(each_failing_call_ends_the_update_with_its_reason)
{
    /* LINK */
    start(T0);
    (void)next(T0);
    CHECK(fail(-1));
    CHECK_EQ(g_o.result, FW_C6_RES_NO_LINK);
    CHECK_EQ(g_o.failed_in, FW_C6_PH_LINK);
    CHECK_EQ(g_o.error, -1);
    /* BEGIN: the C6 takes no update over SDIO */
    start(T0);
    (void)next(T0);
    CHECK(ok());
    (void)next(T0);
    CHECK(fail(0x107));
    CHECK_EQ(g_o.result, FW_C6_RES_REFUSED);
    CHECK_EQ(g_o.failed_in, FW_C6_PH_BEGIN);
    /* END: the C6's check of the image */
    to_write(T0, true, k_26);
    write_all(T0);
    (void)next(T0);
    CHECK(fail(0x1503));
    CHECK_EQ(g_o.result, FW_C6_RES_VERIFY);
    CHECK_EQ(g_o.failed_in, FW_C6_PH_END);
    /* ACTIVATE */
    to_write(T0, true, k_26);
    write_all(T0);
    (void)next(T0);
    CHECK(ok());
    (void)next(T0);
    CHECK(fail(-2));
    CHECK_EQ(g_o.result, FW_C6_RES_ACTIVATE);
    CHECK_EQ(g_o.failed_in, FW_C6_PH_ACTIVATE);
}

TEST_CASE(a_failed_write_is_closed_and_never_activated)
{
    to_write(T0, true, k_26);
    (void)next(T0);
    CHECK(ok());
    const fw_c6_step_t w = next(T0);
    CHECK_EQ(w.offset, FW_C6_RUN_BYTES);
    CHECK(fail(-3));
    CHECK_EQ(g_o.phase, FW_C6_PH_CLOSE);
    CHECK_EQ(g_o.done, FW_C6_RUN_BYTES);
    CHECK_EQ(next(T0).act, FW_C6_ACT_CLOSE);
    /* The close's answer, OK or not, ends it as the write's failure. */
    CHECK(ok());
    CHECK_EQ(g_o.result, FW_C6_RES_WRITE);
    CHECK_EQ(g_o.failed_in, FW_C6_PH_WRITE);
    CHECK_EQ(g_o.error, -3);
    to_write(T0, true, k_26);
    (void)next(T0);
    CHECK(fail(-3));
    (void)next(T0);
    CHECK(fail(-4));
    CHECK_EQ(g_o.result, FW_C6_RES_WRITE);
}

/* Takes the update to @p ph at @p t0 with its step out at @p t0. */
static void to_phase(fw_c6_phase_t ph, uint32_t t0)
{
    if (ph == FW_C6_PH_LINK) {
        start(t0);
    } else if (ph == FW_C6_PH_BEGIN) {
        start(t0);
        (void)next(t0);
        CHECK(ok());
    } else {
        to_write(t0, true, k_26);
        if (ph == FW_C6_PH_END || ph == FW_C6_PH_ACTIVATE) {
            write_all(t0);
            if (ph == FW_C6_PH_ACTIVATE) {
                (void)next(t0);
                CHECK(ok());
            }
        } else if (ph == FW_C6_PH_CLOSE) {
            (void)next(t0);
            CHECK(fail(-3));
        }
    }
    CHECK_EQ(g_o.phase, ph);
    (void)next(t0);
}

static void limit_holds(fw_c6_phase_t ph, uint32_t t0)
{
    const uint32_t lim = fw_c6_ota_limit_ms(ph);
    CHECK(lim > 0u);
    to_phase(ph, t0);
    fw_c6_ota_tick(&g_o, t0 + lim - 1u);
    CHECK_EQ(g_o.phase, ph);
    CHECK(fw_c6_ota_active(&g_o));
    fw_c6_ota_tick(&g_o, t0 + lim);
    CHECK_EQ(g_o.phase, FW_C6_PH_ENDED);
    CHECK_EQ(g_o.result, ph == FW_C6_PH_CLOSE ? FW_C6_RES_WRITE
                                              : FW_C6_RES_NO_ANSWER);
    /* The late answer changes nothing. */
    CHECK(!ok());
    CHECK_EQ(g_o.phase, FW_C6_PH_ENDED);
}

static const fw_c6_phase_t k_steps[] = {
    FW_C6_PH_LINK, FW_C6_PH_BEGIN, FW_C6_PH_WRITE, FW_C6_PH_END,
    FW_C6_PH_ACTIVATE, FW_C6_PH_CLOSE,
};

TEST_CASE(every_step_ends_at_its_limit_and_not_a_millisecond_before)
{
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_LINK), 30000u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_BEGIN), 20000u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_WRITE), 10000u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_END), 10000u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_ACTIVATE), 10000u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_CLOSE), 10000u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_IDLE), 0u);
    CHECK_EQ(fw_c6_ota_limit_ms(FW_C6_PH_ENDED), 0u);
    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); ++i) {
        limit_holds(k_steps[i], T0);
    }
}

TEST_CASE(every_limit_holds_across_the_millisecond_wrap)
{
    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); ++i) {
        limit_holds(k_steps[i], WRAP_T0);
    }
}

TEST_CASE(the_limit_runs_from_when_the_step_went_out)
{
    start(T0);
    /* The step goes out 5s after the start: its 30s count from there. */
    (void)next(T0 + 5000u);
    fw_c6_ota_tick(&g_o, T0 + 5000u + FW_C6_LINK_MS - 1u);
    CHECK(fw_c6_ota_active(&g_o));
    fw_c6_ota_tick(&g_o, T0 + 5000u + FW_C6_LINK_MS);
    CHECK_EQ(g_o.result, FW_C6_RES_NO_ANSWER);
    /* No step out, no limit. */
    start(T0);
    fw_c6_ota_tick(&g_o, T0 + 10u * FW_C6_LINK_MS);
    CHECK(fw_c6_ota_active(&g_o));
}

TEST_CASE(an_answer_for_another_update_or_no_step_is_ignored)
{
    start(T0);
    const uint32_t first = g_o.gen;
    /* No step out yet. */
    CHECK(!ok());
    (void)next(T0);
    CHECK(!fw_c6_ota_answer(&g_o, first + 1u, 0, false, k_old));
    CHECK(!fw_c6_ota_answer(&g_o, first - 1u, 0, false, k_old));
    CHECK_EQ(g_o.phase, FW_C6_PH_LINK);
    /* An ended update's step answering late: the next update's gen moves
     * on, so its answer is not taken for the new one's. */
    fw_c6_ota_tick(&g_o, T0 + FW_C6_LINK_MS);
    CHECK(fw_c6_ota_start(&g_o, TOTAL, k_carried, T0));
    CHECK_EQ(g_o.gen, first + 1u);
    (void)next(T0);
    CHECK(!fw_c6_ota_answer(&g_o, first, 0, true, k_carried));
    CHECK_EQ(g_o.phase, FW_C6_PH_LINK);
    /* One step out at a time. */
    fw_c6_step_t s;
    CHECK(!fw_c6_ota_next(&g_o, T0, &s));
    CHECK(!fw_c6_ota_next(&g_o, T0, NULL));
}

TEST_CASE(a_start_while_one_runs_or_out_of_range_changes_nothing)
{
    start(T0);
    (void)next(T0);
    const uint32_t gen = g_o.gen;
    CHECK(!fw_c6_ota_start(&g_o, TOTAL, k_carried, T0));
    CHECK_EQ(g_o.gen, gen);
    CHECK(g_o.issued);
    fw_c6_ota_init(&g_o);
    CHECK(!fw_c6_ota_start(&g_o, 0u, k_carried, T0));
    CHECK(!fw_c6_ota_start(&g_o, FW_C6_SLOT_BYTES + 1u, k_carried, T0));
    CHECK(!fw_c6_ota_active(&g_o));
    CHECK(fw_c6_ota_start(&g_o, FW_C6_SLOT_BYTES, k_carried, T0));
    CHECK(!fw_c6_ota_active(NULL));
    CHECK_EQ(fw_c6_ota_percent(NULL), 0u);
}

TEST_CASE(the_last_write_is_the_rest_of_the_image)
{
    fw_c6_ota_init(&g_o);
    MUST(fw_c6_ota_start(&g_o, 1u, k_carried, T0));
    (void)next(T0);
    CHECK(ok());
    (void)next(T0);
    CHECK(ok());
    const fw_c6_step_t s = next(T0);
    CHECK_EQ(s.act, FW_C6_ACT_WRITE);
    CHECK_EQ(s.offset, 0u);
    CHECK_EQ(s.len, 1u);
    CHECK(ok());
    CHECK_EQ(g_o.phase, FW_C6_PH_END);
    /* An image of exactly one run. */
    fw_c6_ota_init(&g_o);
    MUST(fw_c6_ota_start(&g_o, FW_C6_RUN_BYTES, k_carried, T0));
    (void)next(T0);
    CHECK(ok());
    (void)next(T0);
    CHECK(ok());
    CHECK_EQ(next(T0).len, FW_C6_RUN_BYTES);
    CHECK(ok());
    CHECK_EQ(g_o.phase, FW_C6_PH_END);
}

int main(void)
{
    g_img = malloc(FW_C6_SLOT_BYTES + 64u);
    REQUIRE_ALLOC(g_img);
    RUN(the_c6_image_passes_and_says_its_version);
    RUN(a_panel_image_is_no_c6_image);
    RUN(no_image_and_a_damaged_one_are_refused);
    RUN(the_c6_slot_is_the_limit_to_the_byte);
    RUN(a_c6_that_reports_no_version_is_offered_the_update);
    RUN(the_row_names_the_first_reason_in_order);
    RUN(image_update_link_and_same_come_before_the_bench);
    RUN(an_old_c6_is_updated_without_the_activate_call);
    RUN(a_c6_from_2_6_0_is_activated_after_its_check);
    RUN(a_c6_that_runs_the_carried_version_is_sent_nothing);
    RUN(each_failing_call_ends_the_update_with_its_reason);
    RUN(a_failed_write_is_closed_and_never_activated);
    RUN(every_step_ends_at_its_limit_and_not_a_millisecond_before);
    RUN(every_limit_holds_across_the_millisecond_wrap);
    RUN(the_limit_runs_from_when_the_step_went_out);
    RUN(an_answer_for_another_update_or_no_step_is_ignored);
    RUN(a_start_while_one_runs_or_out_of_range_changes_nothing);
    RUN(the_last_write_is_the_rest_of_the_image);
    free(g_img);
    return test_summary("fw_c6");
}
