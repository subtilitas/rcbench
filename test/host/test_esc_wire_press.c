/*
 * The ESC signal-wire session's presses: every button shows the pressed colour from
 * press to release and only its own pixels change, every hold fills and only its own
 * pixels change, and a move redraws nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

#include "ew_id.h"
#include "link_pages.h"

#include "esc_wire_rig.h"

/** @brief The canvas's width. */
#define W 1024
/** @brief The canvas's height. */
#define H 600

static gfx_color_t *g_fb;
static gfx_color_t *g_was;
static gfx_canvas_t g_cv;

/** @brief The session drawn on the canvas. */
static void draw(void)
{
    if (g_fb == NULL) {
        g_fb = calloc((size_t)W * H, sizeof(*g_fb));
        REQUIRE_ALLOC(g_fb);
        g_was = calloc((size_t)W * H, sizeof(*g_was));
        REQUIRE_ALLOC(g_was);
        gfx_canvas_init(&g_cv, g_fb, W, H, W);
    }
    gfx_fill_rect(&g_cv, 0, 0, W, H, 0u);
    esc_wire_pages.render(&g_cv);
}

/** @brief What is drawn now kept to compare with. */
static void keep(void)
{
    draw();
    memcpy(g_was, g_fb, (size_t)W * H * sizeof(*g_fb));
}

/**
 * @brief The pixels that differ from what was kept.
 * @param r      A rectangle.
 * @param inside Count those inside it, or those outside it.
 * @return Their count.
 */
static int changed(gfx_rect_t r, bool inside)
{
    int n = 0;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (gfx_rect_contains(r, x, y) == inside
                && g_fb[(size_t)y * W + (size_t)x] != g_was[(size_t)y * W + (size_t)x]) {
                ++n;
            }
        }
    }
    return n;
}

/**
 * @brief A finger's event at the middle of a control.
 * @param type  The event.
 * @param r     The control.
 * @param flags Its flags.
 */
static void touch(touch_event_type_t type, gfx_rect_t r, uint8_t flags)
{
    const touch_event_t e = { .type = type, .flags = flags,
                              .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                         .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    esc_wire_pages.event(&e);
}

/**
 * @brief A button: pressed, it alone changes and only the press's revision moves; a
 *        press cut there leaves the page as it was.
 * @param r The button.
 */
static void button_shows_its_press(gfx_rect_t r)
{
    CHECK(r.w > 0);
    ewp_words_clear();
    keep();
    const uint32_t rev = esc_wire_pages.revision();
    const uint32_t hold = esc_wire_pages.hold_revision();

    touch(TOUCH_EVENT_DOWN, r, 0u);
    CHECK_EQ(esc_wire_pages.revision(), rev);
    CHECK(esc_wire_pages.hold_revision() != hold);
    CHECK(esc_wire_pages.hold_rect().x == r.x && esc_wire_pages.hold_rect().y == r.y);
    draw();
    CHECK(changed(r, true) > 0);
    CHECK_EQ(changed(r, false), 0);
    /* A move changes nothing shown. */
    touch(TOUCH_EVENT_MOVE, r, 0u);
    CHECK_EQ(esc_wire_pages.revision(), rev);
    touch(TOUCH_EVENT_UP, r, TOUCH_FLAG_NO_TAP);
    CHECK(esc_wire_pages.revision() != rev);
    draw();
    CHECK_EQ(changed(r, true), 0);
    CHECK_EQ(changed(r, false), 0);
}

/**
 * @brief A hold: its button alone fills, in hold_revision.
 */
static void hold_fills(void)
{
    const gfx_rect_t r = esc_wire_panel_hold_rect();

    CHECK(r.w > 0);
    ewp_words_clear();
    keep();
    const uint32_t rev = esc_wire_pages.revision();

    touch(TOUCH_EVENT_DOWN, r, 0u);
    const uint32_t hold = esc_wire_pages.hold_revision();

    CHECK(esc_wire_pages.hold_rect().x == r.x && esc_wire_pages.hold_rect().w == r.w);
    esc_wire_pages.tick(1.0f);
    CHECK_EQ(esc_wire_pages.revision(), rev);
    CHECK(esc_wire_pages.hold_revision() != hold);
    draw();
    CHECK(changed(r, true) > 0);
    CHECK_EQ(changed(r, false), 0);
    touch(TOUCH_EVENT_UP, r, TOUCH_FLAG_NO_TAP);
    draw();
    CHECK_EQ(changed(r, true), 0);
    ewp_s.page = EWP_PAGE_NONE;
    ++ewp_s.rev;
}

TEST_CASE(every_button_of_connect_and_a_session_shows_its_press)
{
    session_up(up_am_rig);
    button_shows_its_press(esc_wire_panel_back_rect());
    button_shows_its_press(esc_wire_panel_value_rect(2));
    connected();
    button_shows_its_press(esc_wire_panel_back_rect());
    button_shows_its_press(esc_wire_panel_action_rect(0));
    button_shows_its_press(esc_wire_panel_action_rect(1));
    ew_tap(esc_wire_panel_tab_rect(1));
    button_shows_its_press(esc_wire_panel_value_rect(0));
}

TEST_CASE(a_dimmed_button_takes_the_press_and_does_not_light)
{
    session_up(up_am_rig);
    connected();
    ewp_words_clear();
    keep();
    /* WRITE 0 VALUES. */
    touch(TOUCH_EVENT_DOWN, esc_wire_panel_action_rect(2), 0u);
    draw();
    CHECK_EQ(changed(esc_wire_panel_action_rect(2), true), 0);
}

TEST_CASE(every_button_of_a_stage_shows_its_press)
{
    session_up(up_am_rig);
    connected();
    esc_link_stop(&g_l);
    bench_run(200u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    button_shows_its_press(esc_wire_panel_action_rect(0));
    button_shows_its_press(esc_wire_panel_action_rect(1));
    button_shows_its_press(esc_wire_panel_back_rect());
    g_f.rd.flags = 0u;
    g_f.rd.state = LINK_ESC_OPEN;
    g_f.rd.held = true;
    g_f.rd.ours = true;
    g_hand = true;
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    button_shows_its_press(esc_wire_panel_action_rect(2));
    g_f.rd.state = LINK_ESC_HAND_WAIT;
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_HAND);
    button_shows_its_press(esc_wire_panel_action_rect(1));
    g_f.rd.state = LINK_ESC_NOT_BACK;
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_NOT_BACK);
    button_shows_its_press(esc_wire_panel_action_rect(0));
    g_f.rd.state = LINK_ESC_OPEN;
    g_f.rd.ours = false;
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_OTHER);
    button_shows_its_press(esc_wire_panel_action_rect(0));
}

TEST_CASE(every_button_of_a_page_and_a_list_shows_its_press)
{
    session_up(up_am_rig);
    connected();
    /* A long WRITE list: NEXT, and on its next page PREV. */
    memset(ewp_s.draft, 0x01, sizeof(ewp_s.draft));
    ewp_s.page = EWP_PAGE_WRITE;
    ewp_s.w_page = 0;
    ++ewp_s.rev;
    CHECK(ewp_write_pages() > 1);
    button_shows_its_press(esc_wire_panel_next_rect());
    button_shows_its_press(esc_wire_panel_cancel_rect());
    ewp_s.w_page = 1;
    ++ewp_s.rev;
    button_shows_its_press(esc_wire_panel_prev_rect());
    ewp_s.page = EWP_PAGE_NONE;
    /* A list. */
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(1));
    CHECK(ewp_s.ch_what != EWP_CH_NONE);
    button_shows_its_press(esc_wire_panel_choice_rect(0));
    button_shows_its_press(esc_wire_panel_cancel_rect());
}

TEST_CASE(every_hold_fills_its_button_and_nothing_else)
{
    static const int k_pages[] = { EWP_PAGE_LEAVE, EWP_PAGE_RELEASE, EWP_PAGE_RESTORE,
                                   EWP_PAGE_NO_BACKUP, EWP_PAGE_WRITE };

    session_up(up_am_rig);
    hold_fills();
    connected();
    for (size_t i = 0u; i < sizeof(k_pages) / sizeof(k_pages[0]); ++i) {
        ewp_s.page = k_pages[i];
        ewp_s.page_lines = 0;
        ewp_s.w_seen_last = true;
        ++ewp_s.rev;
        hold_fills();
    }
    /* ENTER AGAIN. */
    g_f.rd.flags = 0u;
    g_f.rd.state = LINK_ESC_OPEN;
    g_f.rd.held = true;
    g_f.rd.ours = true;
    g_hand = true;
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    hold_fills();
}

int main(void)
{
    RUN(every_button_of_connect_and_a_session_shows_its_press);
    RUN(a_dimmed_button_takes_the_press_and_does_not_light);
    RUN(every_button_of_a_stage_shows_its_press);
    RUN(every_button_of_a_page_and_a_list_shows_its_press);
    RUN(every_hold_fills_its_button_and_nothing_else);
    return test_summary("esc_wire_press");
}
