/**
 * @file
 * @brief The screenshot: the 1000ms hold on the band's empty part, and the
 *        capture and its one picture.
 *
 * SPDX-License-Identifier: MIT
 */
#include "shot_rig.h"

/* ------------------------------------------------------------ the hold */

TEST_CASE(nothing_at_999ms_the_picture_at_1000ms)
{
    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(999);
    CHECK(!ui_shot_wanted());
    frame();
    CHECK(!ui_shot_held());
    ui_router_tick(0.001f);
    CHECK(ui_shot_wanted());
    frame();
    CHECK(ui_shot_held());
    CHECK_EQ(g_allocs, 1);
    CHECK_EQ(g_copies, 1);
}

TEST_CASE(the_release_does_nothing_and_a_hold_fires_once)
{
    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(999);
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    wait_ms(1000);
    CHECK(!ui_shot_wanted());
    CHECK(ui_router_alert() == NULL);
    /* Held on past 1000ms: one picture. */
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1000);
    frame();
    wait_ms(3000);
    frame();
    CHECK_EQ(g_copies, 1);
}

TEST_CASE(sliding_in_the_gap_stays_sliding_off_its_edge_ends_it)
{
    start();
    gfx_rect_t g;
    REQUIRE(ui_router_shot_gap(&g));
    touch(g.x + g.w - 1, 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    touch(g.x, 0, TOUCH_EVENT_MOVE, 1);          /* its corner: still in */
    wait_ms(500);
    CHECK(ui_shot_wanted());
    frame();

    start();
    REQUIRE(ui_router_shot_gap(&g));
    touch(g.x + g.w - 1, 30, TOUCH_EVENT_DOWN, 1);
    touch(g.x + g.w, 30, TOUCH_EVENT_MOVE, 1);   /* one past the right */
    touch(g.x + g.w - 1, 30, TOUCH_EVENT_MOVE, 1); /* back: no resuming */
    wait_ms(1500);
    CHECK(!ui_shot_wanted());

    start();
    touch(g.x, 30, TOUCH_EVENT_DOWN, 1);
    touch(g.x - 1, 30, TOUCH_EVENT_MOVE, 1);     /* one past the left */
    wait_ms(1500);
    CHECK(!ui_shot_wanted());

    start();
    touch(g.x, UI_BAND_H - 1, TOUCH_EVENT_DOWN, 1);
    touch(g.x, UI_BAND_H, TOUCH_EVENT_MOVE, 1);  /* down into the body */
    wait_ms(1500);
    CHECK(!ui_shot_wanted());
}

TEST_CASE(a_chip_that_appears_under_the_hold_ends_it)
{
    start();
    gfx_rect_t g;
    REQUIRE(ui_router_shot_gap(&g));
    touch(g.x + g.w - 1, 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    ui_bench_status_t st = status();
    st.faults = 1u;                     /* the FAULT chip takes room */
    ui_router_set_status(&st);
    gfx_rect_t after;
    REQUIRE(ui_router_shot_gap(&after));
    CHECK(after.w < g.w);
    wait_ms(1000);
    CHECK(!ui_shot_wanted());
}

TEST_CASE(a_second_contact_ends_it_and_blocks_it)
{
    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    touch(500, 300, TOUCH_EVENT_DOWN, 2);       /* on the body */
    touch(500, 300, TOUCH_EVENT_UP, 2);
    wait_ms(1000);
    CHECK(!ui_shot_wanted());

    start();
    touch(500, 300, TOUCH_EVENT_DOWN, 2);       /* first on the body */
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1500);
    CHECK(!ui_shot_wanted());

    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    touch(gap_x(), 30, TOUCH_EVENT_MOVE, 3);    /* another id, in the gap too */
    wait_ms(1000);
    CHECK(!ui_shot_wanted());
}

TEST_CASE(a_lost_event_and_a_change_of_screen_end_it)
{
    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    ui_router_cancel_gestures();
    wait_ms(1000);
    CHECK(!ui_shot_wanted());

    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    ui_router_goto(SCREEN_SUPPLY);
    wait_ms(1000);
    CHECK(!ui_shot_wanted());
}

TEST_CASE(a_late_frame_does_not_finish_the_hold)
{
    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    ui_router_tick(5.0f);
    CHECK(!ui_shot_wanted());
}

TEST_CASE(a_press_on_chip_tag_guide_or_stop_is_no_screenshot)
{
    start();
    ui_router_goto(SCREEN_SUPPLY);
    gfx_rect_t chip;
    REQUIRE(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_LINK, &chip));
    const gfx_rect_t tag = ui_home_tag_rect(ui_router_title(SCREEN_SUPPLY));
    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_rect_t guide = ui_band_guide_rect();
    const int spots[][2] = {
        { chip.x + 1, 30 }, { tag.x + tag.w - 1, 30 },
        { stop.x + 1, 30 }, { guide.x + 1, 30 },
    };
    for (size_t i = 0u; i < sizeof(spots) / sizeof(spots[0]); ++i) {
        touch(spots[i][0], spots[i][1], TOUCH_EVENT_DOWN, 1);
        wait_ms(1200);
        CHECK(!ui_shot_wanted());
        ui_router_cancel_gestures();
        (void)ui_router_take_stop();
    }
    /* Nor in the 8px each side keeps. */
    gfx_rect_t g;
    REQUIRE(ui_router_shot_gap(&g));
    CHECK_EQ(g.x, tag.x + tag.w + 8);
    CHECK_EQ(g.x + g.w, chip.x - 8);
    touch(g.x - 1, 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1200);
    CHECK(!ui_shot_wanted());
}

TEST_CASE(the_gap_follows_title_and_chips_and_a_screen_without_band_has_none)
{
    start();
    gfx_rect_t menu;
    REQUIRE(ui_router_shot_gap(&menu));
    ui_router_goto(SCREEN_MOTOR);
    gfx_rect_t motor;
    REQUIRE(ui_router_shot_gap(&motor));
    CHECK(motor.x > menu.x);
    ui_router_goto(SCREEN_BUSFAULT);
    CHECK(!ui_router_shot_gap(NULL));
    ui_router_goto(SCREEN_SPLASH);
    CHECK(!ui_router_shot_gap(NULL));
    ui_router_goto(SCREEN_UPDATE);
    CHECK(!ui_router_shot_gap(NULL));
    touch(300, 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1200);
    CHECK(!ui_shot_wanted());
}

/**
 * @brief The smallest gap over every screen with the band in one status.
 * @param st  the status
 * @return its width in px, 0 for a screen with none
 */
static int narrowest_gap(const ui_bench_status_t *st)
{
    int least = 1024;
    ui_router_set_status(st);
    for (int id = SCREEN_OVERVIEW; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        if (!ui_router_stop_live()) {
            continue;
        }
        gfx_rect_t g = { 0, 0, 0, 0 };
        (void)ui_router_shot_gap(&g);
        if (g.w < least) {
            least = g.w;
        }
    }
    return least;
}

TEST_CASE(every_screen_with_the_band_keeps_a_gap_but_at_its_fullest)
{
    start();
    ui_bench_status_t st = status();
    st.run_seconds = 359999u;
    const ui_bench_status_t safe = st;
    st.armed = true;
    st.armed_roles = UI_ARMED_MOTOR;
    const ui_bench_status_t armed = st;
    st.faults = 0xFFFFu;
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(narrowest_gap(&safe) >= 40);
        CHECK(narrowest_gap(&armed) >= 40);
        CHECK_EQ(narrowest_gap(&st), 0);
    }
    /* MOTOR & ESC, ARMED MOTOR and every FAULT bit up: no room is no hold,
     * and the band's controls are not crowded. */
    ui_router_goto(SCREEN_MOTOR);
    gfx_rect_t g;
    if (ui_router_shot_gap(&g)) {
        CHECK(g.w > 0);
    } else {
        touch(280, 30, TOUCH_EVENT_DOWN, 1);
        wait_ms(1200);
        CHECK(!ui_shot_wanted());
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(no_screen_sees_the_hold_and_it_works_over_a_keypad)
{
    start();
    ui_router_goto(SCREEN_BALANCE);
    const uint32_t before = ui_router_dispatched();
    const uint32_t navs = ui_router_navigations();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    touch(gap_x() + 1, 30, TOUCH_EVENT_MOVE, 1);
    wait_ms(1000);
    touch(gap_x() + 1, 30, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_dispatched(), before);
    CHECK_EQ(ui_router_navigations(), navs);
}

TEST_CASE(a_screens_press_is_its_own_as_before)
{
    start();
    ui_router_goto(SCREEN_SUPPLY);
    const uint32_t before = ui_router_dispatched();
    touch(300, 300, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(ui_router_dispatched(), before + 1u);
    /* Slid onto the gap it is released, and starts no hold. */
    touch(gap_x(), 30, TOUCH_EVENT_MOVE, 1);
    CHECK_EQ(ui_router_dispatched(), before + 2u);
    wait_ms(1500);
    CHECK(!ui_shot_wanted());
}

/* --------------------------------------------------------- the capture */

TEST_CASE(the_picture_is_the_frame_with_the_band_and_without_the_notice)
{
    start();
    ui_router_shot_set_idle(false);
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1000);
    frame();
    const gfx_color_t *img = ui_shot_image();
    REQUIRE(img != NULL);
    CHECK(memcmp(img, fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);
    CHECK(ui_router_alert_is_notice());
    /* The next frame shows the notice; the picture does not change. */
    frame();
    CHECK(memcmp(img, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
}

TEST_CASE(no_memory_says_so_in_the_band_and_goes_on)
{
    start();
    g_alloc_fails = true;
    hold_shot();
    CHECK(!ui_shot_held());
    REQUIRE(ui_router_alert() != NULL);
    CHECK(!ui_router_alert_is_notice());
    CHECK_STR_EQ(ui_router_alert(), TR(SHOT_NO_MEMORY));
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    ui_router_tick(0.016f);
    CHECK(!ui_shot_open());
}

TEST_CASE(a_second_hold_replaces_the_waiting_picture_in_the_same_memory)
{
    start();
    ui_router_shot_set_idle(false);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(ui_router_alert(), TR(SHOT_WAITS));
    const gfx_color_t *first = ui_shot_image();
    /* Something else on the glass next time. */
    gfx_fill_rect(&cv, 0, 70, 100, 100, GFX_RED);
    ui_router_goto(SCREEN_MOTOR);
    frame();
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    CHECK_EQ(g_allocs, 1);
    CHECK(ui_shot_image() == first);
    CHECK(memcmp(first, fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);
    CHECK_STR_EQ(ui_router_alert(), TR(SHOT_REPLACED));
}

TEST_CASE(the_preview_is_the_mean_of_four_by_four)
{
    start();
    /* Draw over the frame's own content in the picture by a picture of
     * known pixels: the band and the screen are drawn, then read back. */
    open_popup();
    const gfx_color_t *img = ui_shot_image();
    const gfx_color_t *pv = ui_shot_preview();
    REQUIRE(img != NULL);
    REQUIRE(pv != NULL);
    for (int y = 0; y < UI_SHOT_PREVIEW_H; y += 37) {
        for (int x = 0; x < UI_SHOT_PREVIEW_W; x += 41) {
            unsigned r = 0u, g = 0u, b = 0u;
            for (int j = 0; j < 4; ++j) {
                for (int i = 0; i < 4; ++i) {
                    const unsigned v = img[(y * 4 + j) * W + x * 4 + i];
                    r += v >> 11;
                    g += (v >> 5) & 63u;
                    b += v & 31u;
                }
            }
            const unsigned want = (((r + 8u) / 16u) << 11)
                                  | (((g + 8u) / 16u) << 5) | ((b + 8u) / 16u);
            CHECK_EQ(pv[y * UI_SHOT_PREVIEW_W + x], want);
        }
    }
}

int main(void)
{
    RUN(nothing_at_999ms_the_picture_at_1000ms);
    RUN(the_release_does_nothing_and_a_hold_fires_once);
    RUN(sliding_in_the_gap_stays_sliding_off_its_edge_ends_it);
    RUN(a_chip_that_appears_under_the_hold_ends_it);
    RUN(a_second_contact_ends_it_and_blocks_it);
    RUN(a_lost_event_and_a_change_of_screen_end_it);
    RUN(a_late_frame_does_not_finish_the_hold);
    RUN(a_press_on_chip_tag_guide_or_stop_is_no_screenshot);
    RUN(the_gap_follows_title_and_chips_and_a_screen_without_band_has_none);
    RUN(every_screen_with_the_band_keeps_a_gap_but_at_its_fullest);
    RUN(no_screen_sees_the_hold_and_it_works_over_a_keypad);
    RUN(a_screens_press_is_its_own_as_before);
    RUN(the_picture_is_the_frame_with_the_band_and_without_the_notice);
    RUN(no_memory_says_so_in_the_band_and_goes_on);
    RUN(a_second_hold_replaces_the_waiting_picture_in_the_same_memory);
    RUN(the_preview_is_the_mean_of_four_by_four);
    return test_summary("shot");
}
