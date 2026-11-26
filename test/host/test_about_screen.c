/**
 * @file test_about_screen.c
 * @brief ABOUT (about_screen.h): SETUP > PANEL's last row opens it, BACK
 *        and the home tag return to PANEL's list as it was left, the bar
 *        and a drag scroll the column to its last row, PAGE UP and PAGE
 *        DOWN move a screen, and the column follows the language.
 *
 * SPDX-License-Identifier: MIT
 */
#include "nav_rig.h"

#include "about_gen.h"
#include "about_screen.h"

/** @brief A condition the rest of the case stands on: fail and return. */
#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief A frame. */
static void frame(void) { ui_router_tick(1.0f / 60.0f); }

/** @brief Drag contact 1 from (@p x, @p y0) to (@p x, @p y1) in 8px steps,
 *         body coordinates, and let go. */
static void drag(int x, int y0, int y1)
{
    const int step = (y1 > y0) ? 8 : -8;
    touch(x, UI_BAND_H + y0, TOUCH_EVENT_DOWN, 1);
    for (int y = y0; (step > 0) ? y < y1 : y > y1; y += step) {
        touch(x, UI_BAND_H + y, TOUCH_EVENT_MOVE, 1);
    }
    touch(x, UI_BAND_H + y1, TOUCH_EVENT_MOVE, 1);
    touch(x, UI_BAND_H + y1, TOUCH_EVENT_UP, 1);
    frame();
}

/** @brief Tap the middle of @p r, body coordinates. */
static void tap_body(gfx_rect_t r)
{
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
    frame();
}

/** @brief The menu up, SETUP open on PANEL's list scrolled to its end. */
static void on_panel_end(void)
{
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    ui_router_goto(SCREEN_SETUP);
    int x = 0;
    int y = 0;
    hub_point(settings_hub_cat_tile(SET_CAT_APP), &x, &y);
    tap(x, y);
    frame();
    const gfx_rect_t l = settings_screen_list_rect();
    for (int i = 0; i < 6; ++i) {
        drag(l.x + l.w / 2, l.y + l.h - 10, l.y + 10);
    }
}

/* PANEL's list ends on About rcbench, and a tap on it opens ABOUT at its
 * top. */
TEST_CASE(panels_last_row_opens_about)
{
    on_panel_end();
    REQUIRE(settings_screen_category() == SET_CAT_APP);
    int max = 0;
    const int scroll = settings_screen_scroll(&max);
    CHECK(max > 0);
    CHECK_EQ(scroll, max);
    const int last = settings_screen_rows(SET_CAT_APP) - 1;
    const gfx_rect_t r = settings_screen_row_rect(last);
    REQUIRE(r.y >= settings_screen_list_rect().y);
    tap(r.x + 40, UI_BAND_H + r.y + r.h / 2);
    frame();
    REQUIRE(ui_router_current() == SCREEN_ABOUT);
    CHECK_EQ(about_screen_top(), 0);
    CHECK(strcmp(ui_router_title(SCREEN_ABOUT), "ABOUT") == 0);
    CHECK(about_setup_row()->label == TX_ABOUT_ROW);
    /* The screen's own rows come first: the name, two versions, an empty
     * row; the first generated row follows the panel's heading. */
    for (int k = 0; k < 4; ++k) {
        CHECK(about_screen_gen_text(k) == NULL);
    }
}

/* BACK returns to SETUP on PANEL's list at the scroll it was left at; so
 * does the home tag. */
TEST_CASE(back_and_the_home_tag_return_to_panels_list)
{
    on_panel_end();
    int max = 0;
    const int scroll = settings_screen_scroll(&max);
    about_setup_row()->tap();
    frame();
    REQUIRE(ui_router_current() == SCREEN_ABOUT);
    tap_body(about_screen_part(ABOUT_PART_BACK));
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), SET_CAT_APP);
    CHECK_EQ(settings_screen_scroll(NULL), scroll);

    about_setup_row()->tap();
    frame();
    REQUIRE(ui_router_current() == SCREEN_ABOUT);
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_ABOUT));
    tap(home.x + home.w / 2, home.y + home.h / 2);
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), SET_CAT_APP);
}

/* The thumb dragged to the bar's foot shows the last row: the last row of
 * the notice texts, at the column's foot. */
TEST_CASE(the_bar_scrolls_to_the_last_row)
{
    on_panel_end();
    about_setup_row()->tap();
    frame();
    REQUIRE(ui_router_current() == SCREEN_ABOUT);
    const int rows = about_screen_rows();
    REQUIRE(rows > ABOUT_ROWS);
    CHECK(rows > (int)about_gen_part[ABOUT_GEN_PARTS]);
    CHECK(!about_screen_at_end());
    const gfx_rect_t bar = about_screen_part(ABOUT_PART_BAR);
    drag(bar.x + bar.w / 2, bar.y + 4, bar.y + bar.h - 2);
    CHECK(about_screen_at_end());
    CHECK_EQ(about_screen_top(), rows - ABOUT_ROWS);
    const int last = (int)about_gen_part[ABOUT_GEN_PARTS] - 1;
    CHECK(about_screen_gen_text(rows - 1) == about_gen_text[last]);
    CHECK(about_screen_gen_text(rows) == NULL);
    /* A press on the track above the thumb puts its middle there: back at
     * the top. */
    tap_body(gfx_rect_make(bar.x, bar.y, bar.w, 8));
    CHECK_EQ(about_screen_top(), 0);
    /* Drawn at the end, the column's foot holds ink. */
    about_screen_scroll(rows);
    ui_router_render(&cv, 0);
    const gfx_rect_t t = about_screen_part(ABOUT_PART_TEXT);
    int lit = 0;
    for (int y = t.y + t.h - 24; y < t.y + t.h; ++y) {
        for (int x = t.x; x < t.x + 400; ++x) {
            lit += fb[(size_t)(UI_BAND_H + y) * W + (size_t)x] != UI_BG;
        }
    }
    CHECK(lit > 0);
}

/* A drag on the rows moves them a row per 24px and taps nothing; PAGE DOWN
 * and PAGE UP move 17 rows, held to the ends. */
TEST_CASE(a_drag_and_the_page_keys_scroll)
{
    on_panel_end();
    about_setup_row()->tap();
    frame();
    REQUIRE(ui_router_current() == SCREEN_ABOUT);
    const gfx_rect_t t = about_screen_part(ABOUT_PART_TEXT);
    drag(t.x + t.w / 2, t.y + t.h - 8, t.y + t.h - 8 - 10 * 24);
    CHECK_EQ(about_screen_top(), 10);
    CHECK_EQ(ui_router_current(), SCREEN_ABOUT);
    drag(t.x + t.w / 2, t.y + 8, t.y + 8 + 4 * 24);
    CHECK_EQ(about_screen_top(), 6);
    tap_body(about_screen_part(ABOUT_PART_PAGE_DN));
    CHECK_EQ(about_screen_top(), 6 + ABOUT_ROWS);
    tap_body(about_screen_part(ABOUT_PART_PAGE_UP));
    tap_body(about_screen_part(ABOUT_PART_PAGE_UP));
    CHECK_EQ(about_screen_top(), 0);
    /* Held at the end: PAGE DOWN from the last screen stays there. */
    about_screen_scroll(about_screen_rows());
    const int end = about_screen_top();
    tap_body(about_screen_part(ABOUT_PART_PAGE_DN));
    CHECK_EQ(about_screen_top(), end);
    CHECK(about_screen_at_end());
}

/* The rows follow the language: the German column is built of the same
 * generated rows under German headings. */
TEST_CASE(the_column_follows_the_language)
{
    on_panel_end();
    about_setup_row()->tap();
    frame();
    const int en = about_screen_rows();
    ui_text_set_language(UI_LANG_DE);
    frame();
    const int de = about_screen_rows();
    CHECK(de >= (int)about_gen_part[ABOUT_GEN_PARTS] + ABOUT_ROWS);
    about_screen_scroll(de);
    CHECK_EQ(about_screen_top(), de - ABOUT_ROWS);
    ui_text_set_language(UI_LANG_EN);
    frame();
    CHECK_EQ(about_screen_rows(), en);
    CHECK_EQ(about_screen_top(), en - ABOUT_ROWS);
}

/* Every component's row starts with its name, every generated row fits
 * ABOUT_GEN_CELLS cells and carries a style the drawing knows. */
TEST_CASE(the_generated_rows_fit_and_carry_a_style)
{
    const int n = (int)about_gen_part[ABOUT_GEN_PARTS];
    CHECK_EQ(about_gen_part[0], 0u);
    for (int p = 0; p < ABOUT_GEN_PARTS; ++p) {
        CHECK(about_gen_part[p] < about_gen_part[p + 1]);
    }
    int wide = 0;
    int bad = 0;
    for (int i = 0; i < n; ++i) {
        int cells = 0;
        for (const char *s = about_gen_text[i]; *s != '\0'; ++s) {
            cells += ((unsigned char)*s & 0xC0u) != 0x80u;
        }
        wide += cells > ABOUT_GEN_CELLS;
        bad += strchr("NDSTB", about_gen_style[i]) == NULL;
    }
    CHECK_EQ(wide, 0);
    CHECK_EQ(bad, 0);
    CHECK(strncmp(about_gen_text[0], "ESP-IDF", 7) == 0);
    CHECK(strncmp(about_gen_text[about_gen_part[ABOUT_GEN_IOMCU]],
                  "Raspberry Pi Pico SDK", 21) == 0);
}

int main(void)
{
    RUN(panels_last_row_opens_about);
    RUN(back_and_the_home_tag_return_to_panels_list);
    RUN(the_bar_scrolls_to_the_last_row);
    RUN(a_drag_and_the_page_keys_scroll);
    RUN(the_column_follows_the_language);
    RUN(the_generated_rows_fit_and_carry_a_style);
    return test_summary("about_screen");
}
