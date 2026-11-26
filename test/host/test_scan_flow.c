/**
 * @file
 * @brief The scan flow (scan_flow.h) and the band's servo chip: a scanned
 *        line's notice, DUT NAME of a known servo, the refusal during a
 *        run, the mailbox, and the chip on the band.
 *
 * The failures this is about: a scan during a run that moves the run's
 * servo; a scan that says nothing, so the operator cannot tell a refusal
 * from a take; a known servo filed under the name typed for the servo
 * before it; a line posted from the console lost or run twice; and a
 * chip that pushes the band's other chips off or does not repaint.
 *
 * SPDX-License-Identifier: MIT
 */
#include "nav_rig.h"

#include "scan_flow.h"
#include "servo_active.h"

/** @brief A condition the rest of the case stands on: fail and return. */
#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief A case's start: the menu up, no servo, no hook, a fresh clock. */
static uint32_t start(void)
{
    static uint32_t t = 5000000u;
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    scan_flow_reset();
    (void)servo_active_clear(false);
    ui_router_set_alert(NULL);
    t += 100000u;
    return t;
}

/** @brief The alert band's text, "" for none. */
static const char *notice(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief The active id, "" for none. */
static const char *active(void)
{
    const char *id = servo_active_id();
    return id != NULL ? id : "";
}

/** @brief What the hook answers next, and what it was asked. */
static scan_card_t s_card;
static char        s_name[SETTINGS_TEXT_MAX];
static char        s_asked[32];
static int         s_looks;

/** @brief The card's look-up as the filing gives it, from s_card. */
static scan_card_t look(const char *id, char *name, size_t n)
{
    (void)snprintf(s_asked, sizeof(s_asked), "%s", id);
    ++s_looks;
    (void)snprintf(name, n, "%s", s_name);
    return s_card;
}

/** @brief The hook answering @p card with @p name. */
static void hook(scan_card_t card, const char *name)
{
    s_card = card;
    (void)snprintf(s_name, sizeof(s_name), "%s", name);
    s_asked[0] = '\0';
    s_looks = 0;
    scan_flow_set_look(look);
}

/** @brief The bench ARMED on SERVO, or SAFE. */
static void armed_servo(bool on)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = on;
    st.armed_roles = on ? UI_ARMED_SERVO : 0u;
    ui_router_set_status(&st);
}

TEST_CASE(an_id_with_no_hook_is_set_and_named_alone)
{
    const uint32_t t = start();
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SET);
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A");
    CHECK(ui_router_alert_is_notice());
    /* No screen change. */
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(an_unknown_id_is_new_on_this_card_and_keeps_dut_name)
{
    const uint32_t t = start();
    settings_set_text(SET_TEXT_DUT_NAME, "OLD NAME");
    hook(SCAN_CARD_NEW, "");
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK_STR_EQ(s_asked, "KST_X10-0000A");
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A  NEW ON THIS CARD");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
}

TEST_CASE(a_known_id_sets_dut_name_from_its_newest_run)
{
    const uint32_t t = start();
    settings_set_text(SET_TEXT_DUT_NAME, "OLD NAME");
    hook(SCAN_CARD_KNOWN, "LEFT AILERON");
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A  LEFT AILERON");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "LEFT AILERON");
    /* A known servo whose runs carry no name: DUT NAME stays. */
    hook(SCAN_CARD_KNOWN, "");
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 10u));
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000B");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "LEFT AILERON");
}

TEST_CASE(no_card_is_named_in_the_notice)
{
    const uint32_t t = start();
    hook(SCAN_CARD_NONE, "");
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A  NO CARD: RUNS ARE NOT FILED");
}

TEST_CASE(a_malformed_line_is_named_and_changes_nothing)
{
    const uint32_t t = start();
    hook(SCAN_CARD_NEW, "");
    REQUIRE(scan_flow_line("KST_X10-0000A", false, t));
    CHECK(scan_flow_line("https://example.com/a/very/long/path", false,
                         t + 10u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_MALFORMED);
    /* The line's first 24 characters. */
    CHECK_STR_EQ(notice(), "NOT A SERVO ID: https://example.com/a/ve");
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK(scan_flow_line("kst_x10-0000b", false, t + 20u));
    CHECK_STR_EQ(notice(), "NOT A SERVO ID: kst_x10-0000b");
    CHECK(scan_flow_line("X", true, t + 30u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_TOO_LONG);
    CHECK_STR_EQ(notice(), "NOT A SERVO ID: CODE TOO LONG");
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    /* Refused lines ask the card nothing. */
    CHECK_EQ(s_looks, 1);
}

TEST_CASE(during_a_servo_run_another_id_is_refused_and_the_run_keeps_its_servo)
{
    const uint32_t t = start();
    REQUIRE(scan_flow_line("KST_X10-0000A", false, t));
    armed_servo(true);
    CHECK(scan_flow_run_active());
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 5000u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_REFUSED_RUN);
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_STR_EQ(notice(),
                 "RUN IN PROGRESS: IT STAYS ON KST_X10-0000A. SCAN AFTER THE RUN.");
    /* The run's own label: dropped, the notice as it was. */
    ui_router_set_alert(NULL);
    CHECK(scan_flow_line("KST_X10-0000A", false, t + 10000u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_RUN_OWN);
    CHECK_STR_EQ(notice(), "");
    /* An arm on MOTOR alone is no servo run. */
    armed_servo(false);
    ui_bench_status_t st = *ui_router_status();
    st.armed = true;
    st.armed_roles = UI_ARMED_MOTOR;
    ui_router_set_status(&st);
    CHECK(!scan_flow_run_active());
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 15000u));
    CHECK_STR_EQ(active(), "KST_X10-0000B");
}

TEST_CASE(a_run_without_a_servo_refuses_with_the_short_notice)
{
    const uint32_t t = start();
    armed_servo(true);
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK_STR_EQ(notice(), "RUN IN PROGRESS: SCAN AFTER THE RUN.");
    CHECK_STR_EQ(active(), "");
}

TEST_CASE(the_same_id_twice_is_dropped_then_already_active)
{
    const uint32_t t = start();
    REQUIRE(scan_flow_line("KST_X10-0000A", false, t));
    ui_router_set_alert(NULL);
    CHECK(scan_flow_line("KST_X10-0000A", false, t + 2999u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_REPEAT);
    CHECK_STR_EQ(notice(), "");
    CHECK(scan_flow_line("KST_X10-0000A", false, t + 3000u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SAME);
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A  ALREADY ACTIVE");
}

TEST_CASE(the_splash_and_an_update_take_no_line)
{
    const uint32_t t = start();
    ui_router_goto(SCREEN_SPLASH);
    CHECK(!scan_flow_line("KST_X10-0000A", false, t));
    CHECK_STR_EQ(active(), "");
    ui_router_goto(SCREEN_SERVO);
    CHECK(scan_flow_line("KST_X10-0000A", false, t + 10u));
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
}

TEST_CASE(a_posted_line_runs_once_at_the_next_tick)
{
    const uint32_t t = start();
    CHECK(scan_flow_post("KST_X10-0000A"));
    /* One line waits: a second is refused, not queued. */
    CHECK(!scan_flow_post("KST_X10-0000B"));
    CHECK_STR_EQ(active(), "");
    scan_flow_tick(t);
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    ui_router_set_alert(NULL);
    scan_flow_tick(t + 5000u);
    CHECK_STR_EQ(notice(), "");
    CHECK(!scan_flow_post(NULL));
    /* A console line past 64 bytes goes as too long. */
    char longer[80];
    memset(longer, 'A', sizeof(longer) - 1u);
    longer[sizeof(longer) - 1u] = '\0';
    CHECK(scan_flow_post(longer));
    scan_flow_tick(t + 6000u);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_TOO_LONG);
}

TEST_CASE(the_status_carries_the_servo_its_source_and_the_run)
{
    const uint32_t t = start();
    ui_bench_status_t st = *ui_router_status();
    scan_flow_status(&st);
    CHECK(st.servo_id == NULL);
    CHECK(!st.servo_run);
    REQUIRE(scan_flow_line("KST_X10-0000A", false, t));
    scan_flow_status(&st);
    CHECK_STR_EQ(st.servo_id, "KST_X10-0000A");
    CHECK_EQ(st.servo_from, SERVO_ACTIVE_SCAN);
    CHECK(!st.servo_run);
    armed_servo(true);
    scan_flow_status(&st);
    CHECK(st.servo_run);
    scan_flow_status(NULL);
}

/* ------------------------------------------------------------ the chip */

/** @brief A SAFE bench with the link up and BENCH, and @p id active. */
static ui_bench_status_t band_with(const char *id)
{
    ui_bench_status_t st = *ui_router_status();
    st.link_up = true;
    st.armed = false;
    st.faults = 0u;
    st.run_seconds = 0u;
    st.mode = ui_band_mode(true);
    st.servo_id = id;
    st.servo_from = (uint8_t)SERVO_ACTIVE_SCAN;
    st.servo_run = false;
    return st;
}

TEST_CASE(no_servo_no_chip_and_the_band_as_before)
{
    (void)start();
    ui_bench_status_t st = band_with(NULL);
    gfx_rect_t link_before, link_after;
    CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, NULL));
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_LINK, &link_before));
    st.servo_id = "";
    CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, NULL));
    /* The chip stands left of LINK and moves no other chip. */
    st.servo_id = "KST_X10-0000A";
    gfx_rect_t r;
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, &r));
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_LINK, &link_after));
    CHECK_EQ(link_after.x, link_before.x);
    CHECK(r.x + r.w < link_after.x);
    CHECK(r.x >= 272);
    CHECK_EQ(ui_band_chip_at(&st, r.x + r.w / 2, 20), UI_BAND_CHIP_SERVO);
}

TEST_CASE(the_longest_id_shortens_then_the_mode_chip_makes_room)
{
    (void)start();
    ui_bench_status_t st = band_with("ABCDEFGHIJKLMNOPQ-ZZZZZ");
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, &r));
    /* 23 cells of 10px, the inset and the pad. */
    CHECK_EQ(r.w, 22 + 230 + 4);
    /* A servo run of 10min: the dash and 5 characters. */
    st.armed = true;
    st.armed_roles = UI_ARMED_SERVO;
    st.run_seconds = 600u;
    st.servo_run = true;
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, &r));
    CHECK_EQ(r.w, 22 + 60 + 4);
    CHECK(r.x >= 272);
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_MODE, NULL));
    /* FAULT beside it: BENCH gives way, LINK stays. */
    st.faults = 0x51u;
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, &r));
    CHECK_EQ(r.w, 22 + 60 + 4);
    CHECK(r.x >= 272);
    CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_MODE, NULL));
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_LINK, NULL));
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL));
    /* The widest row, 10h and German: the chip is left out, BENCH back. */
    ui_text_set_language(UI_LANG_DE);
    st.run_seconds = 36000u;
    CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, NULL));
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_MODE, NULL));
    ui_text_set_language(UI_LANG_EN);
    /* No chip ever starts left of x 272, for every id length. */
    static const char *const ids[] = { "A-00000", "KST_X10-0000A",
                                       "ABCDEFGHIJKLMNOPQ-ZZZZZ" };
    for (size_t i = 0; i < 3u; ++i) {
        st.servo_id = ids[i];
        for (int ch = UI_BAND_CHIP_NONE + 1; ch < UI_BAND_CHIP_COUNT; ++ch) {
            if (ui_band_chip_rect(&st, (ui_band_chip_t)ch, &r)) {
                CHECK(r.x >= 272);
            }
        }
    }
}

TEST_CASE(the_band_repaints_when_the_id_or_the_run_changes)
{
    (void)start();
    ui_bench_status_t st = band_with("KST_X10-0000A");
    const uint32_t a = ui_band_key("MENU", true, &st, NULL, false,
                                   UI_BAND_GUIDE_NONE);
    st.servo_id = "KST_X10-0000B";
    const uint32_t b = ui_band_key("MENU", true, &st, NULL, false,
                                   UI_BAND_GUIDE_NONE);
    st.servo_run = true;
    const uint32_t c = ui_band_key("MENU", true, &st, NULL, false,
                                   UI_BAND_GUIDE_NONE);
    CHECK(a != b);
    CHECK(b != c);
    gfx_color_t fill, ink;
    REQUIRE(ui_band_chip_colors(&st, UI_BAND_CHIP_SERVO, &fill, &ink));
    CHECK_EQ(fill, ui_theme_color(UI_C_ACCENT));
    st.servo_run = false;
    REQUIRE(ui_band_chip_colors(&st, UI_BAND_CHIP_SERVO, &fill, &ink));
    CHECK_EQ(fill, ui_theme_color(UI_C_PANEL_SUNK));
}

TEST_CASE(a_tap_on_the_chip_opens_its_box_under_the_band)
{
    (void)start();
    ui_bench_status_t st = band_with("KST_X10-0000A");
    ui_router_set_status(&st);
    gfx_rect_t r, box;
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_SERVO, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_SERVO);
    CHECK(ui_band_help_rect(&st, UI_BAND_CHIP_SERVO, &box));
    CHECK(box.y >= UI_BAND_H);
    /* With a run the box gains its paragraph, so it grows. */
    gfx_rect_t run_box;
    st.servo_run = true;
    CHECK(ui_band_help_rect(&st, UI_BAND_CHIP_SERVO, &run_box));
    CHECK(run_box.h > box.h);
    ui_router_render(&cv, 0);
    /* NEW ID and a pick have boxes of their own text. */
    st.servo_run = false;
    st.servo_from = (uint8_t)SERVO_ACTIVE_NEW;
    CHECK(ui_band_help_rect(&st, UI_BAND_CHIP_SERVO, &box));
    st.servo_from = (uint8_t)SERVO_ACTIVE_PICK;
    CHECK(ui_band_help_rect(&st, UI_BAND_CHIP_SERVO, &box));
    ui_band_help_render(&cv, &st, UI_BAND_CHIP_SERVO);
}

int main(void)
{
    RUN(an_id_with_no_hook_is_set_and_named_alone);
    RUN(an_unknown_id_is_new_on_this_card_and_keeps_dut_name);
    RUN(a_known_id_sets_dut_name_from_its_newest_run);
    RUN(no_card_is_named_in_the_notice);
    RUN(a_malformed_line_is_named_and_changes_nothing);
    RUN(during_a_servo_run_another_id_is_refused_and_the_run_keeps_its_servo);
    RUN(a_run_without_a_servo_refuses_with_the_short_notice);
    RUN(the_same_id_twice_is_dropped_then_already_active);
    RUN(the_splash_and_an_update_take_no_line);
    RUN(a_posted_line_runs_once_at_the_next_tick);
    RUN(the_status_carries_the_servo_its_source_and_the_run);
    RUN(no_servo_no_chip_and_the_band_as_before);
    RUN(the_longest_id_shortens_then_the_mode_chip_makes_room);
    RUN(the_band_repaints_when_the_id_or_the_run_changes);
    RUN(a_tap_on_the_chip_opens_its_box_under_the_band);
    return test_summary("scan_flow");
}
