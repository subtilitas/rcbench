/*
 * The workbench pieces every device screen shares: the task strip under
 * the band (ui_workbench) and the output card's TARGET row
 * (ui_target_row).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_target_row.h"
#include "ui_text.h"
#include "ui_workbench.h"
#include "ui_theme.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
}

static touch_event_t evt(touch_event_type_t t, int x, int y, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 1 } };
    return e;
}

static int mid_x(gfx_rect_t r) { return r.x + r.w / 2; }
static int mid_y(gfx_rect_t r) { return r.y + r.h / 2; }

/* ---------------------------------------------------------------- the strip */

/* The tasks in the strip's order, LIVE always among them, a task the mask
 * lacks not drawn, and the bits past SETTINGS ignored. */
TEST_CASE(the_strip_holds_its_tasks_in_order_with_live_always)
{
    static const struct {
        unsigned mask;
        int      count;
        uint8_t  task[UI_WB_TASKS];
    } k[] = {
        { 0u, 1, { UI_WB_LIVE } },
        { UI_WB_HAS(UI_WB_SETTINGS), 2, { UI_WB_LIVE, UI_WB_SETTINGS } },
        { UI_WB_HAS(UI_WB_TEST) | UI_WB_HAS(UI_WB_SETTINGS), 3,
          { UI_WB_LIVE, UI_WB_TEST, UI_WB_SETTINGS } },
        { 0xFFu, 4,
          { UI_WB_LIVE, UI_WB_TEST, UI_WB_PROGRAM, UI_WB_SETTINGS } },
        { 0xF0u | UI_WB_HAS(UI_WB_PROGRAM), 2,
          { UI_WB_LIVE, UI_WB_PROGRAM } },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        ui_wb_strip_t w;
        ui_wb_strip_init(&w, k[i].mask, UI_WB_STRIP_X, UI_WB_STRIP_Y);
        CHECK_EQ(w.tabs.count, k[i].count);
        CHECK_EQ(ui_wb_strip_task(&w), UI_WB_LIVE);
        int x = UI_WB_STRIP_X;
        for (int t = 0; t < k[i].count; ++t) {
            CHECK_EQ(w.task[t], k[i].task[t]);
            CHECK(ui_wb_strip_has(&w, (ui_wb_task_t)k[i].task[t]));
            const gfx_rect_t r =
                ui_wb_strip_tab_rect(&w, (ui_wb_task_t)k[i].task[t]);
            CHECK_EQ(r.x, x);
            CHECK_EQ(r.y, UI_WB_STRIP_Y);
            CHECK_EQ(r.w, UI_WB_TAB_W);
            CHECK_EQ(r.h, UI_WB_STRIP_H);
            x += UI_WB_TAB_W + UI_WB_TAB_GAP;
        }
        const gfx_rect_t all = ui_wb_strip_rect(&w);
        CHECK_EQ(all.x, UI_WB_STRIP_X);
        CHECK_EQ(all.w, k[i].count * UI_WB_TAB_W
                            + (k[i].count - 1) * UI_WB_TAB_GAP);
    }
    /* Four tabs fit the 488px page area. */
    CHECK(4 * UI_WB_TAB_W + 3 * UI_WB_TAB_GAP <= 488);
    ui_wb_strip_t w;
    ui_wb_strip_init(&w, UI_WB_HAS(UI_WB_TEST), 0, 0);
    CHECK(!ui_wb_strip_has(&w, UI_WB_PROGRAM));
    const gfx_rect_t none = ui_wb_strip_tab_rect(&w, UI_WB_PROGRAM);
    CHECK_EQ(none.w, 0);
    CHECK_EQ(none.h, 0);
}

/* select() shows a task the strip has and says whether it changed; a task
 * it lacks changes nothing; open() is LIVE whatever was shown; both drop a
 * press under way. */
TEST_CASE(select_and_open_show_a_task_and_drop_a_press)
{
    ui_wb_strip_t w;
    ui_wb_strip_init(&w, UI_WB_HAS(UI_WB_TEST) | UI_WB_HAS(UI_WB_SETTINGS),
                     UI_WB_STRIP_X, UI_WB_STRIP_Y);
    CHECK(ui_wb_strip_select(&w, UI_WB_SETTINGS));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_SETTINGS);
    CHECK(!ui_wb_strip_select(&w, UI_WB_SETTINGS));     /* no change */
    CHECK(!ui_wb_strip_select(&w, UI_WB_PROGRAM));      /* not there */
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_SETTINGS);
    CHECK(!ui_wb_strip_select(&w, UI_WB_TASKS));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_SETTINGS);

    const gfx_rect_t test = ui_wb_strip_tab_rect(&w, UI_WB_TEST);
    touch_event_t e = evt(TOUCH_EVENT_DOWN, mid_x(test), mid_y(test), 1);
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK(ui_wb_strip_pressed(&w));
    ui_wb_strip_open(&w);
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_LIVE);
    CHECK(!ui_wb_strip_pressed(&w));
    e.type = TOUCH_EVENT_UP;                     /* the dropped press's UP */
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_LIVE);

    /* open() on LIVE is LIVE, and drops a press there too. */
    e.type = TOUCH_EVENT_DOWN;
    (void)ui_wb_strip_event(&w, &e);
    ui_wb_strip_open(&w);
    CHECK(!ui_wb_strip_pressed(&w));
    (void)ui_wb_strip_select(&w, UI_WB_TEST);
    (void)ui_wb_strip_event(&w, &e);
    CHECK(ui_wb_strip_pressed(&w));
    CHECK(!ui_wb_strip_select(&w, UI_WB_TEST));
    CHECK(!ui_wb_strip_pressed(&w));
}

/* A tap on another tab switches; a slide off it, a release the finger did
 * not make, another contact's release, a tap on the tab shown and a press
 * off the strip do not. */
TEST_CASE(a_tap_switches_the_task_and_nothing_else_does)
{
    ui_wb_strip_t w;
    ui_wb_strip_init(&w, UI_WB_HAS(UI_WB_TEST) | UI_WB_HAS(UI_WB_SETTINGS),
                     UI_WB_STRIP_X, UI_WB_STRIP_Y);
    const gfx_rect_t test = ui_wb_strip_tab_rect(&w, UI_WB_TEST);
    const gfx_rect_t live = ui_wb_strip_tab_rect(&w, UI_WB_LIVE);
    const int tx = mid_x(test), ty = mid_y(test);

    touch_event_t e = evt(TOUCH_EVENT_DOWN, tx, ty, 1);
    CHECK(!ui_wb_strip_event(&w, &e));
    e = evt(TOUCH_EVENT_UP, tx, 200, 1);                /* slid off */
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_LIVE);

    e = evt(TOUCH_EVENT_DOWN, tx, ty, 1);
    (void)ui_wb_strip_event(&w, &e);
    e = evt(TOUCH_EVENT_UP, tx, ty, 1);
    e.flags = TOUCH_FLAG_NO_TAP;                        /* not the finger's */
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_LIVE);

    e = evt(TOUCH_EVENT_DOWN, tx, ty, 1);
    (void)ui_wb_strip_event(&w, &e);
    e = evt(TOUCH_EVENT_UP, tx, ty, 2);                 /* another contact */
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK(ui_wb_strip_pressed(&w));
    e = evt(TOUCH_EVENT_UP, tx, ty, 1);
    CHECK(ui_wb_strip_event(&w, &e));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_TEST);

    e = evt(TOUCH_EVENT_DOWN, tx, ty, 1);               /* the tab shown */
    (void)ui_wb_strip_event(&w, &e);
    e.type = TOUCH_EVENT_UP;
    CHECK(!ui_wb_strip_event(&w, &e));

    e = evt(TOUCH_EVENT_DOWN, 300, 300, 1);             /* off the strip */
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK(!ui_wb_strip_pressed(&w));

    /* A tap through the tracker, as the screen gets it. */
    canvas();
    ui_wb_strip_render(&w, &cv);
    e = evt(TOUCH_EVENT_DOWN, mid_x(live), mid_y(live), 1);
    (void)ui_wb_strip_event(&w, &e);
    ui_wb_strip_cancel(&w);
    e.type = TOUCH_EVENT_UP;
    CHECK(!ui_wb_strip_event(&w, &e));
    CHECK_EQ(ui_wb_strip_task(&w), UI_WB_TEST);
}

/* Each piece takes a NULL and does nothing with it. */
TEST_CASE(the_pieces_refuse_a_null)
{
    const touch_event_t e = evt(TOUCH_EVENT_DOWN, 10, 10, 1);
    ui_wb_strip_init(NULL, 0u, 0, 0);
    CHECK_EQ(ui_wb_strip_task(NULL), UI_WB_LIVE);
    CHECK(!ui_wb_strip_has(NULL, UI_WB_LIVE));
    CHECK(!ui_wb_strip_select(NULL, UI_WB_LIVE));
    ui_wb_strip_open(NULL);
    CHECK(!ui_wb_strip_event(NULL, &e));
    CHECK(!ui_wb_strip_pressed(NULL));
    ui_wb_strip_cancel(NULL);
    CHECK_EQ(ui_wb_strip_rect(NULL).w, 0);
    CHECK_EQ(ui_wb_strip_tab_rect(NULL, UI_WB_LIVE).w, 0);
    canvas();
    ui_wb_strip_render(NULL, &cv);

    ui_target_row_init(NULL, (gfx_rect_t){ 0, 0, 100, 20 });
    CHECK_EQ(ui_target_row_value_rect(NULL).w, 0);
    CHECK_EQ(ui_target_row_cells(NULL), 0u);
    CHECK_EQ(ui_target_row_event(NULL, &e, false), UI_TARGET_NONE);
    ui_target_row_t t;
    ui_target_row_init(&t, (gfx_rect_t){ 0, 0, 100, 20 });
    CHECK_EQ(ui_target_row_event(&t, NULL, false), UI_TARGET_NONE);
    ui_target_row_cancel(NULL);
    ui_target_row_render(NULL, &cv, "T", "V", false, false);
    ui_target_row_render(&t, NULL, "T", "V", false, false);
    ui_target_row_render(&t, &cv, NULL, NULL, true, true);
}

/* ------------------------------------------------------------ the TARGET row */

/* The value box is the row less the label's 64px, its text 8px in from
 * each side; a row no wider than the label has no box. */
TEST_CASE(the_target_row_box_and_its_cells)
{
    ui_target_row_t t;
    ui_target_row_init(&t, (gfx_rect_t){ 514, 326, 268, 22 });
    const gfx_rect_t v = ui_target_row_value_rect(&t);
    CHECK_EQ(v.x, 514 + UI_TARGET_LABEL_W);
    CHECK_EQ(v.y, 326);
    CHECK_EQ(v.w, 268 - UI_TARGET_LABEL_W);
    CHECK_EQ(v.h, 22);
    CHECK_EQ(ui_target_row_cells(&t), (unsigned)((204 - 16) / 8));  /* 23 */

    ui_target_row_init(&t, (gfx_rect_t){ 0, 0, UI_TARGET_LABEL_W, 22 });
    CHECK_EQ(ui_target_row_value_rect(&t).w, 0);
    CHECK_EQ(ui_target_row_cells(&t), 0u);
    ui_target_row_init(&t, (gfx_rect_t){ 0, 0, UI_TARGET_LABEL_W + 16, 22 });
    CHECK_EQ(ui_target_row_cells(&t), 0u);
    ui_target_row_init(&t, (gfx_rect_t){ 0, 0, UI_TARGET_LABEL_W + 24, 22 });
    CHECK_EQ(ui_target_row_cells(&t), 1u);
}

/* A tap on the box opens the choice; on the label, nothing is the row's;
 * a slide off, a release the finger did not make and another contact's
 * release open nothing; a press locked part way opens nothing; a press
 * while locked is taken and leaves no press behind. */
TEST_CASE(the_target_row_opens_on_a_tap_and_never_while_locked)
{
    ui_target_row_t t;
    ui_target_row_init(&t, (gfx_rect_t){ 514, 326, 268, 22 });
    const gfx_rect_t v = ui_target_row_value_rect(&t);
    const int x = mid_x(v), y = mid_y(v);

    touch_event_t e = evt(TOUCH_EVENT_DOWN, 520, y, 1);  /* the label */
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_NONE);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_NONE);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAKEN);
    CHECK(t.down);
    e = evt(TOUCH_EVENT_MOVE, x + 4, y, 1);
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAKEN);
    e = evt(TOUCH_EVENT_UP, x + 4, y, 1);
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAP);
    CHECK(!t.down);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);
    (void)ui_target_row_event(&t, &e, false);
    e = evt(TOUCH_EVENT_UP, x, y - 40, 1);               /* slid off */
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAKEN);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);
    (void)ui_target_row_event(&t, &e, false);
    e = evt(TOUCH_EVENT_UP, x, y, 1);
    e.flags = TOUCH_FLAG_NO_TAP;
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAKEN);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);
    (void)ui_target_row_event(&t, &e, false);
    e = evt(TOUCH_EVENT_DOWN, x + 20, y, 2);             /* a second finger */
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAKEN);
    CHECK_EQ(t.id, 1);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_NONE);
    e = evt(TOUCH_EVENT_UP, x, y, 1);
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_TAP);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);                  /* locked part way */
    (void)ui_target_row_event(&t, &e, false);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_target_row_event(&t, &e, true), UI_TARGET_TAKEN);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);                  /* locked */
    CHECK_EQ(ui_target_row_event(&t, &e, true), UI_TARGET_TAKEN);
    CHECK(!t.down);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_NONE);

    e = evt(TOUCH_EVENT_DOWN, x, y, 1);                  /* cancelled */
    (void)ui_target_row_event(&t, &e, false);
    ui_target_row_cancel(&t);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_target_row_event(&t, &e, false), UI_TARGET_NONE);
}

/* Pixels of @p col inside @p r. */
static unsigned ink(gfx_rect_t r, gfx_color_t col)
{
    unsigned n = 0u;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += (fb[(size_t)y * W + (size_t)x] == col) ? 1u : 0u;
        }
    }
    return n;
}

/* The warning colour on the label and the value when the target is none
 * or not known; a locked box outlined as a control that takes no press. */
TEST_CASE(the_target_row_draws_the_warning_and_the_lock)
{
    ui_target_row_t t;
    ui_target_row_init(&t, (gfx_rect_t){ 514, 326, 268, 22 });
    const gfx_rect_t label = { 514, 326, UI_TARGET_LABEL_W, 22 };
    const gfx_rect_t v = ui_target_row_value_rect(&t);
    const gfx_color_t warn = ui_theme_color(UI_C_WARN);

    canvas();
    ui_target_row_render(&t, &cv, "TARGET", "SERVO PWM GPIO 2", false, false);
    CHECK_EQ(ink(label, warn), 0u);
    CHECK_EQ(ink(v, warn), 0u);
    CHECK(ink(v, ui_theme_color(UI_C_TEXT)) > 20u);
    CHECK_EQ(ink(v, UI_EDGE), 0u);

    canvas();
    ui_target_row_render(&t, &cv, "TARGET", "NONE", true, false);
    CHECK(ink(label, warn) > 20u);
    CHECK(ink(v, warn) > 20u);

    canvas();
    ui_target_row_render(&t, &cv, "TARGET", "SERVO PWM GPIO 2", false, true);
    CHECK(ink(v, UI_EDGE) > 100u);
    CHECK(ink(v, ui_theme_color(UI_C_TEXT_DIM)) > 20u);
    CHECK_EQ(ink(v, ui_theme_color(UI_C_TEXT)), 0u);

    /* A press under way lightens the box; locked, it does not. */
    canvas();
    ui_target_row_render(&t, &cv, NULL, NULL, false, false);
    const gfx_color_t idle = fb[(size_t)(v.y + 10) * W + (size_t)(v.x + 20)];
    t.down = true;
    canvas();
    ui_target_row_render(&t, &cv, NULL, NULL, false, false);
    CHECK(fb[(size_t)(v.y + 10) * W + (size_t)(v.x + 20)] != idle);
    canvas();
    ui_target_row_render(&t, &cv, NULL, NULL, false, true);
    CHECK_EQ(fb[(size_t)(v.y + 10) * W + (size_t)(v.x + 20)], idle);
}

/* `protocol pins` at its limits: the pins that fit and +N, the protocol
 * alone with no pin or no room, the protocol cut to the cells, the buffer's
 * size held, and the pins alone with no protocol. */
TEST_CASE(the_target_text_fits_its_cells_and_its_buffer)
{
    static const struct {
        const char *proto;
        uint64_t    pins;
        unsigned    cells;
        size_t      size;
        const char *want;
    } k[] = {
        { "SERVO PWM", (1u << 2) | (1u << 4), 23u, 32u,
          "SERVO PWM GPIO 2 GPIO 4" },
        { "PROG PORT", 1u << 13, 23u, 32u, "PROG PORT GPIO 13" },
        { "SERVO PWM", 0u, 23u, 32u, "SERVO PWM" },
        { "SERVO PWM", 1u << 2, 9u, 32u, "SERVO PWM" },     /* no room */
        { "SERVO PWM", 1u << 2, 10u, 32u, "SERVO PWM" },    /* a space only */
        { "SERVO PWM", 1u << 2, 16u, 32u, "SERVO PWM GPIO 2" },/* exact */
        { "SERVO PWM", 1u << 2, 15u, 32u, "SERVO PWM +1" },
        { "SERVO PWM", (1u << 2) | (1u << 4), 19u, 32u, "SERVO PWM GPIO 2 +1" },
        { "SERVO PWM", (1u << 2) | (1u << 4), 12u, 32u, "SERVO PWM +2" },
        { "SERVO PWM", 1u << 2, 5u, 32u, "SERVO" },          /* cut */
        { "SERVO PWM", 1u << 2, 23u, 8u, "SERVO P" },        /* the buffer */
        { "SERVO PWM", 1u << 2, 23u, 17u, "SERVO PWM GPIO 2" }, /* exact fit */
        { "SERVO PWM", 1u << 2, 23u, 16u, "SERVO PWM +1" },
        { "SERVO PWM", 1u << 2, 23u, 1u, "" },
        { NULL, 1u << 2, 23u, 32u, "GPIO 2" },
        { "", (1u << 2) | (1u << 3), 23u, 32u, "GPIO 2 GPIO 3" },
        { NULL, 0u, 23u, 32u, "" },
        { "SERVO PWM", 1u << 2, 0u, 32u, "" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char out[32];
        memset(out, 'x', sizeof(out));
        const size_t n = ui_target_text(out, k[i].size, k[i].proto, k[i].pins,
                                        k[i].cells);
        CHECK_STR_EQ(out, k[i].want);
        CHECK_EQ(n, strlen(k[i].want));
        CHECK(n < k[i].size);
        CHECK(n <= k[i].cells);
    }
    char one = 'x';
    CHECK_EQ(ui_target_text(&one, 0u, "SERVO PWM", 1u << 2, 23u), 0u);
    CHECK_EQ(one, 'x');
    CHECK_EQ(ui_target_text(NULL, 32u, "SERVO PWM", 1u << 2, 23u), 0u);
}

int main(void)
{
    RUN(the_strip_holds_its_tasks_in_order_with_live_always);
    RUN(select_and_open_show_a_task_and_drop_a_press);
    RUN(a_tap_switches_the_task_and_nothing_else_does);
    RUN(the_pieces_refuse_a_null);
    RUN(the_target_row_box_and_its_cells);
    RUN(the_target_row_opens_on_a_tap_and_never_while_locked);
    RUN(the_target_row_draws_the_warning_and_the_lock);
    RUN(the_target_text_fits_its_cells_and_its_buffer);
    return test_summary("workbench");
}
