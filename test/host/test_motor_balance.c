/**
 * @file test_motor_balance.c
 * @brief MOTOR & ESC > BALANCE (motor_balance.h) and the BALANCE screen's
 *        node readings, drawn with model values.
 *
 * Cases:
 * - the_balance_tab_draws_its_pane_without_a_node
 * - take_keys_capture_both_runs_and_the_result_draws
 * - four_runs_show_four_take_keys
 * - take_without_a_fresh_report_says_so_on_the_band
 * - take_works_while_armed
 * - the_pane_repaints_on_a_new_reading_in_each_buffer
 * - the_balance_screen_shows_the_node_and_drops_simulated
 * - the_balance_screen_shows_dashes_for_a_stale_report
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_rig.h"

#include "balance_screen.h"
#include "motor_balance.h"
#include "node_balance.h"

/** @brief BALANCE's pane, in screen coordinates. */
static const gfx_rect_t k_area = { MO_BAL_X, MO_BAL_Y, MO_BAL_W, MO_BAL_H };

/** @brief A steady report: valid, the index seen, quality 94%. */
static rcnode_bal_report_t rep(uint16_t mg, uint16_t tenths)
{
    rcnode_bal_report_t r;
    memset(&r, 0, sizeof r);
    r.hdr.type = RCNODE_T_BAL_REPORT;
    r.flags = RCNODE_BF_VALID | RCNODE_BF_INDEX_SEEN | RCNODE_BF_DC_OK
              | RCNODE_BF_QUALITY;
    r.quality = 94u;
    r.rpm = 8000u;
    r.mag_1x = mg;
    r.phase_1x = tenths;
    return r;
}

/** @brief @p n new reports of @p r into the panel's instance. */
static void feed(rcnode_bal_report_t *r, unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        ++r->hdr.seq;
        r->hdr.t_node += 250000u;
        node_balance_feed(true, r, 10u);
    }
}

/** @brief A tap on key @p k of the pane. */
static void tap_key(int k)
{
    const gfx_rect_t r = mbal_key_rect(k, k_area);
    CHECK(r.w > 0);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Pixels of the pane that differ between @p a and @p b. */
static int pane_diff(const gfx_color_t *a, const gfx_color_t *b)
{
    int n = 0;
    for (int y = k_area.y; y < k_area.y + k_area.h; ++y) {
        for (int x = k_area.x; x < k_area.x + k_area.w; ++x) {
            n += a[(size_t)y * W + x] != b[(size_t)y * W + x];
        }
    }
    return n;
}

/** @brief A copy of the frame. */
static gfx_color_t *snap(void)
{
    gfx_color_t *p = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(p);
    memcpy(p, fb, (size_t)W * H * sizeof(gfx_color_t));
    return p;
}

/** @brief The bench screen fresh, no node, the BALANCE tab shown. */
static void fresh_tab(void)
{
    settings_reset_all();
    node_balance_reset();
    fresh();
    tap(MO_BAL_TAB_X, MO_PANE_Y);
}

TEST_CASE(the_balance_tab_draws_its_pane_without_a_node)
{
    fresh();
    node_balance_reset();
    scr->render(&cv, 0);
    gfx_color_t *plot = snap();
    tap(MO_BAL_TAB_X, MO_PANE_Y);
    CHECK_EQ(motor_screen_pane(), MOTOR_PANE_BAL);
    scr->render(&cv, 0);
    const int d = pane_diff(plot, fb);
    if (d < 3000) {
        T_FAIL("BALANCE differs from PLOT by %d pixels", d);
    }
    free(plot);
    /* The pane fits under the tabs inside the card. */
    CHECK(MO_BAL_Y + MO_BAL_H <= 56 + 230 - 10);
    for (int k = 0; k < MBAL_KEY_COUNT; ++k) {
        const gfx_rect_t r = mbal_key_rect(k, k_area);
        if (r.w == 0) {
            continue;
        }
        CHECK(r.h >= UI_TAP_MIN);
        CHECK(r.x >= k_area.x && r.x + r.w <= k_area.x + k_area.w);
        CHECK(r.y >= k_area.y && r.y + r.h <= k_area.y + k_area.h);
    }
    /* Two runs: two TAKE keys. */
    CHECK_EQ(mbal_key_rect(MBAL_KEY_TAKE2, k_area).w, 0);
    CHECK_EQ(mbal_key_rect(99, k_area).w, 0);
    /* And back to PLOT is PLOT again. */
    tap(MO_PLOT_TAB_X, MO_PANE_Y);
    CHECK_EQ(motor_screen_pane(), MOTOR_PANE_PLOT);
}

TEST_CASE(take_keys_capture_both_runs_and_the_result_draws)
{
    fresh_tab();
    rcnode_bal_report_t r = rep(420u, 2650u);
    node_balance_feed(true, &r, 10u);
    scr->render(&cv, 0);
    gfx_color_t *before = snap();
    tap_key(MBAL_KEY_TAKE0);
    const nb_session_t *s = node_balance_session();
    CHECK_EQ(s->cap[0].state, NB_CAP_TAKING);
    /* The other TAKE refuses while one runs. */
    tap_key(MBAL_KEY_TAKE1);
    CHECK_EQ(s->cap[1].state, NB_CAP_EMPTY);
    feed(&r, NB_CAP_REPORTS);
    CHECK_EQ(s->cap[0].state, NB_CAP_DONE);
    r = rep(300u, 1960u);
    tap_key(MBAL_KEY_TAKE1);
    feed(&r, NB_CAP_REPORTS);
    CHECK(nb_ready(s));
    vibe_solution_t sol;
    CHECK(node_balance_result(&sol, NULL));
    CHECK_EQ(sol.status, VIBE_SOLVE_OK);
    scr->render(&cv, 0);
    const int d = pane_diff(before, fb);
    if (d < 1500) {
        T_FAIL("the result changes %d pixels", d);
    }
    free(before);
    /* CLEAR empties both. */
    tap_key(MBAL_KEY_CLEAR);
    CHECK_EQ(s->cap[0].state, NB_CAP_EMPTY);
    CHECK(!node_balance_result(NULL, NULL));
}

TEST_CASE(four_runs_show_four_take_keys)
{
    fresh_tab();
    tap_key(MBAL_KEY_FOUR);
    CHECK_EQ(node_balance_session()->method, NB_FOUR_RUNS);
    for (int k = MBAL_KEY_TAKE0; k <= MBAL_KEY_TAKE3; ++k) {
        CHECK(mbal_key_rect(k, k_area).w > 0);
    }
    /* The keys do not overlap. */
    for (int a = 0; a < MBAL_KEY_COUNT; ++a) {
        for (int b = a + 1; b < MBAL_KEY_COUNT; ++b) {
            const gfx_rect_t ra = mbal_key_rect(a, k_area);
            const gfx_rect_t rb = mbal_key_rect(b, k_area);
            const bool apart = ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x
                               || ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y;
            CHECK(apart);
        }
    }
    scr->render(&cv, 0);
    tap_key(MBAL_KEY_TWO);
    CHECK_EQ(node_balance_session()->method, NB_TWO_RUNS);
}

TEST_CASE(take_without_a_fresh_report_says_so_on_the_band)
{
    fresh_tab();
    ui_router_set_alert(NULL);
    tap_key(MBAL_KEY_TAKE0);
    CHECK_EQ(node_balance_session()->cap[0].state, NB_CAP_EMPTY);
    CHECK(ui_router_alert() != NULL);
    if (ui_router_alert() != NULL) {
        CHECK_STR_EQ(ui_router_alert(), TR(MB_NO_DATA));
    }
    /* A report 1000ms old is no fresh one either. */
    ui_router_set_alert(NULL);
    rcnode_bal_report_t r = rep(420u, 2650u);
    node_balance_feed(true, &r, BAL_STALE_MS);
    tap_key(MBAL_KEY_TAKE0);
    CHECK_EQ(node_balance_session()->cap[0].state, NB_CAP_EMPTY);
    ui_router_set_alert(NULL);
}

TEST_CASE(take_works_while_armed)
{
    fresh_tab();
    motor_screen_set_armed(true);
    rcnode_bal_report_t r = rep(420u, 2650u);
    node_balance_feed(true, &r, 10u);
    tap_key(MBAL_KEY_TAKE0);
    CHECK_EQ(node_balance_session()->cap[0].state, NB_CAP_TAKING);
    /* The pane posts no command: a measurement drives nothing. */
    motor_cmd_t c;
    CHECK(!motor_screen_poll_cmd(&c) || c.kind != MOTOR_CMD_THROTTLE);
    motor_screen_set_armed(false);
}

TEST_CASE(the_pane_repaints_on_a_new_reading_in_each_buffer)
{
    fresh_tab();
    rcnode_bal_report_t r = rep(420u, 2650u);
    node_balance_feed(true, &r, 10u);
    scr->render(&cv, 0);
    scr->render(&cv, 1);
    /* Paint over the pane: a frame with nothing new leaves it. */
    gfx_fill_rect(&cv, k_area.x, k_area.y, k_area.w, k_area.h, 0x1234u);
    scr->render(&cv, 0);
    CHECK_EQ(fb[(size_t)(k_area.y + 2) * W + k_area.x + 300], 0x1234u);
    /* A new reading repaints buffer 0, then buffer 1 on its own frame. */
    r = rep(510u, 1200u);
    feed(&r, 1u);
    scr->render(&cv, 0);
    CHECK(fb[(size_t)(k_area.y + 2) * W + k_area.x + 300] != 0x1234u);
    gfx_fill_rect(&cv, k_area.x, k_area.y, k_area.w, k_area.h, 0x1234u);
    scr->render(&cv, 1);
    CHECK(fb[(size_t)(k_area.y + 2) * W + k_area.x + 300] != 0x1234u);
}

/** @brief The BALANCE screen fresh and drawn into @p buf. */
static void draw_balance(int buf)
{
    const ui_screen_t *b = balance_screen();
    if (b->tick != NULL) {
        b->tick(0.016f);
    }
    b->render(&cv, buf);
}

TEST_CASE(the_balance_screen_shows_the_node_and_drops_simulated)
{
    settings_reset_all();
    fresh();
    node_balance_reset();
    const ui_screen_t *b = balance_screen();
    b->reset();
    draw_balance(0);
    CHECK(balance_screen_model_mark() != NULL);
    gfx_color_t *model = snap();
    rcnode_bal_report_t r = rep(420u, 2650u);
    node_balance_feed(true, &r, 10u);
    CHECK(balance_screen_model_mark() == NULL);
    CHECK(balance_screen_model_note() == NULL);
    draw_balance(0);
    int d = 0;
    for (int i = 0; i < W * H; ++i) {
        d += model[i] != fb[i];
    }
    if (d < 2000) {
        T_FAIL("the node's readings change %d pixels", d);
    }
    /* The runs solved: ADD and WHERE and the mass's mark change it more. */
    gfx_color_t *live = snap();
    nb_session_t *s = node_balance_session();
    CHECK_EQ(nb_take(s, 0u, true), NB_TAKE_STARTED);
    feed(&r, NB_CAP_REPORTS);
    r = rep(300u, 1960u);
    CHECK_EQ(nb_take(s, 1u, true), NB_TAKE_STARTED);
    feed(&r, NB_CAP_REPORTS);
    draw_balance(0);
    d = 0;
    for (int i = 0; i < W * H; ++i) {
        d += live[i] != fb[i];
    }
    if (d < 500) {
        T_FAIL("the correction changes %d pixels", d);
    }
    free(model);
    free(live);
    /* The node gone: SIMULATED again. */
    node_balance_feed(false, NULL, 0u);
    CHECK(balance_screen_model_mark() != NULL);
    node_balance_reset();
}

TEST_CASE(the_balance_screen_shows_dashes_for_a_stale_report)
{
    fresh();
    node_balance_reset();
    balance_screen()->reset();
    rcnode_bal_report_t r = rep(420u, 2650u);
    node_balance_feed(true, &r, BAL_STALE_MS - 1u);
    draw_balance(0);
    gfx_color_t *fresh_px = snap();
    node_balance_feed(true, &r, BAL_STALE_MS);
    CHECK_EQ(node_balance_live()->live, NB_LIVE_STALE);
    draw_balance(0);
    /* Only the right card's readings change: the left card is the same. */
    int left = 0, right = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool d = fresh_px[(size_t)y * W + x] != fb[(size_t)y * W + x];
            if (x < 650) {
                left += d;
            } else {
                right += d;
            }
        }
    }
    CHECK_EQ(left, 0);
    CHECK(right > 100);
    free(fresh_px);
    node_balance_reset();
}

int main(void)
{
    RUN(the_balance_tab_draws_its_pane_without_a_node);
    RUN(take_keys_capture_both_runs_and_the_result_draws);
    RUN(four_runs_show_four_take_keys);
    RUN(take_without_a_fresh_report_says_so_on_the_band);
    RUN(take_works_while_armed);
    RUN(the_pane_repaints_on_a_new_reading_in_each_buffer);
    RUN(the_balance_screen_shows_the_node_and_drops_simulated);
    RUN(the_balance_screen_shows_dashes_for_a_stale_report);
    return test_summary("motor_balance");
}
