/**
 * @file test_textkey_cursor.c
 * @brief The cursor of the file-name keyboard: typing at it, DEL before
 *        it, "<-" and "->" one character at a time, the most a name takes,
 *        CLR and an empty name, a tap in the field, the window of a long
 *        name, the start before the extension, the bottom row and its
 *        face, and the keyboards that keep no cursor.
 *
 * Cases:
 * - typing_inserts_at_the_start_the_middle_and_the_end
 * - del_takes_the_character_before_the_cursor
 * - the_arrows_step_one_character_and_stop_at_the_ends
 * - a_full_name_refuses_a_key_and_keeps_the_cursor
 * - clr_and_an_empty_name_leave_the_cursor_at_0
 * - a_tap_in_the_field_places_the_cursor_in_the_nearest_gap
 * - a_press_that_slides_off_or_a_second_contact_moves_nothing
 * - a_long_name_scrolls_to_keep_the_cursor_in_sight
 * - the_cursor_is_a_bar_before_its_cell
 * - the_start_is_before_the_last_full_stop
 * - a_refused_name_keeps_the_text_and_the_cursor
 * - the_bottom_row_fits_and_keeps_its_face_in_both_languages
 * - the_other_keyboards_keep_no_cursor
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "fm_name.h"
#include "greatest.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"

/** @brief FILES' keyboard area: 1008 x 488px from y 4. */
static const gfx_rect_t k_area = { 8, 4, 1008, 488 };

/** @brief The canvas: the body of the panel, 1024 x 540px. */
#define CW 1024
#define CH 540

/** @brief The small letters' layer, as the file keyboard opens. */
static const char k_lower[] = "1234567890" "qwertyuiop" "asdfghjkl-"
                              " zxcvbnm  ";

static ui_textkey_t tk;
static char s_out[64];

/**
 * @brief A DOWN and an UP of contact @p id at @p x, @p y.
 * @param id The contact.
 * @param x  Where, keyboard coordinates.
 * @param y  Where.
 * @return The UP's answer.
 */
static ui_textkey_result_t tap_at(uint8_t id, int x, int y)
{
    touch_event_t e = {
        .type = TOUCH_EVENT_DOWN,
        .point = { .id = id, .x = (int16_t)x, .y = (int16_t)y },
    };
    (void)ui_textkey_event(&tk, &e, s_out, sizeof(s_out));
    e.type = TOUCH_EVENT_UP;
    return ui_textkey_event(&tk, &e, s_out, sizeof(s_out));
}

/**
 * @brief Press and release the middle of @p key with contact 1.
 * @param key The key.
 * @return The release's answer.
 */
static ui_textkey_result_t press(int key)
{
    const gfx_rect_t r = ui_textkey_key_rect(&tk, key);
    return tap_at(1u, r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief Type @p ch from the small letters' layer.
 * @param ch A small letter, a digit or "-".
 */
static void type(char ch)
{
    const char *at = strchr(k_lower, ch);
    press((int)(at - k_lower));
}

/** @brief Where the field's text starts, keyboard coordinates. */
static int text_x(void)
{
    return ui_textkey_field_rect(&tk).x + UI_TEXTKEY_FIELD_IN;
}

/** @brief The middle row of the field. */
static int field_y(void)
{
    const gfx_rect_t f = ui_textkey_field_rect(&tk);
    return f.y + f.h / 2;
}

TEST_CASE(typing_inserts_at_the_start_the_middle_and_the_end)
{
    ui_textkey_open_file(&tk, k_area, "T", "ABC");
    CHECK_EQ(tk.cur, 3);                /* it opens at the end */
    ui_textkey_set_cursor(&tk, 0);
    type('x');
    CHECK_STR_EQ(tk.text, "xABC");
    CHECK_EQ(tk.cur, 1);
    ui_textkey_set_cursor(&tk, 2);
    type('1');
    CHECK_STR_EQ(tk.text, "xA1BC");
    CHECK_EQ(tk.cur, 3);
    press(UI_TK_RIGHT);
    press(UI_TK_RIGHT);
    press(UI_TK_SPACE);
    CHECK_STR_EQ(tk.text, "xA1BC ");
    CHECK_EQ(tk.cur, 6);
    CHECK_EQ(tk.len, 6);
    /* NULL and "" type nothing. */
    CHECK(!ui_textkey_insert(&tk, NULL));
    CHECK(!ui_textkey_insert(&tk, ""));
    CHECK(!ui_textkey_insert(NULL, "a"));
    CHECK_EQ(tk.len, 6);
}

TEST_CASE(del_takes_the_character_before_the_cursor)
{
    /* At 0 nothing goes. */
    ui_textkey_open_file(&tk, k_area, "T", "a\xc3\xa4" "b");
    ui_textkey_set_cursor(&tk, 0);
    press(UI_TK_DEL);
    CHECK_STR_EQ(tk.text, "a\xc3\xa4" "b");
    CHECK_EQ(tk.cur, 0);
    /* After the ä it takes both its bytes. */
    ui_textkey_set_cursor(&tk, 3);
    press(UI_TK_DEL);
    CHECK_STR_EQ(tk.text, "ab");
    CHECK_EQ(tk.cur, 1);
    CHECK_EQ(tk.len, 2);
    /* In the middle and at the end. */
    ui_textkey_open_file(&tk, k_area, "T", "abc");
    ui_textkey_set_cursor(&tk, 2);
    press(UI_TK_DEL);
    CHECK_STR_EQ(tk.text, "ac");
    CHECK_EQ(tk.cur, 1);
    ui_textkey_set_cursor(&tk, 99);
    press(UI_TK_DEL);
    CHECK_STR_EQ(tk.text, "a");
    CHECK_EQ(tk.cur, 1);
    ui_textkey_backspace(NULL);
}

TEST_CASE(the_arrows_step_one_character_and_stop_at_the_ends)
{
    ui_textkey_open_file(&tk, k_area, "T", "\xc3\xa4\xc3\x9f" "z");  /* äßz */
    ui_textkey_set_cursor(&tk, 0);
    press(UI_TK_LEFT);
    CHECK_EQ(tk.cur, 0);
    press(UI_TK_RIGHT);
    CHECK_EQ(tk.cur, 2);
    press(UI_TK_RIGHT);
    CHECK_EQ(tk.cur, 4);
    press(UI_TK_RIGHT);
    CHECK_EQ(tk.cur, 5);
    press(UI_TK_RIGHT);
    CHECK_EQ(tk.cur, 5);
    press(UI_TK_LEFT);
    CHECK_EQ(tk.cur, 4);
    CHECK_EQ(ui_textkey_cursor_chars(&tk), 2);
    ui_textkey_move(&tk, -10);
    CHECK_EQ(tk.cur, 0);
    ui_textkey_move(NULL, 1);
    /* A cursor set inside a character goes to its start. */
    ui_textkey_set_cursor(&tk, 3);
    CHECK_EQ(tk.cur, 2);
    ui_textkey_set_cursor(&tk, -4);
    CHECK_EQ(tk.cur, 0);
}

TEST_CASE(a_full_name_refuses_a_key_and_keeps_the_cursor)
{
    char full[FM_NAME_MAX + 1];
    memset(full, 'a', FM_NAME_MAX);
    full[FM_NAME_MAX] = '\0';
    ui_textkey_open_file(&tk, k_area, "T", full);
    CHECK_EQ(tk.len, 59);
    ui_textkey_set_cursor(&tk, 10);
    type('x');
    CHECK_EQ(tk.len, 59);
    CHECK_EQ(tk.cur, 10);
    CHECK_STR_EQ(tk.text, full);
    press(UI_TK_SPACE);
    CHECK_EQ(tk.cur, 10);
    CHECK_STR_EQ(tk.text, full);
    /* A two-byte character at 58 bytes would make 60: refused. */
    full[58] = '\0';
    ui_textkey_open_file(&tk, k_area, "T", full);
    ui_textkey_set_cursor(&tk, 4);
    CHECK(!ui_textkey_insert(&tk, "\xc3\xa4"));
    CHECK_EQ(tk.len, 58);
    CHECK_EQ(tk.cur, 4);
    CHECK(ui_textkey_insert(&tk, "b"));
    CHECK_EQ(tk.len, 59);
    CHECK_EQ(tk.cur, 5);
    CHECK_EQ(tk.text[4], 'b');
    /* A caller's own most, as a label field sets it after the open. */
    ui_textkey_open_file(&tk, k_area, "T", "");
    tk.max_len = 3;
    type('a');
    type('b');
    type('c');
    ui_textkey_set_cursor(&tk, 1);
    type('d');
    CHECK_STR_EQ(tk.text, "abc");
    CHECK_EQ(tk.cur, 1);
}

TEST_CASE(clr_and_an_empty_name_leave_the_cursor_at_0)
{
    ui_textkey_open_file(&tk, k_area, "T", NULL);
    CHECK_EQ(tk.cur, 0);
    CHECK_EQ(tk.view, 0);
    press(UI_TK_LEFT);
    press(UI_TK_RIGHT);
    press(UI_TK_DEL);
    CHECK_EQ(tk.cur, 0);
    CHECK_EQ(tap_at(1u, text_x() + 200, field_y()), UI_TEXTKEY_NONE);
    CHECK_EQ(tk.cur, 0);
    CHECK_EQ(press(UI_TK_OK), UI_TEXTKEY_NONE);
    CHECK(tk.open && tk.refused);
    CHECK_EQ(tk.why, (uint8_t)FM_NAME_EMPTY);
    /* CLR on a long name with the cursor in the middle. */
    ui_textkey_open_file(&tk, k_area, "T", "MOTOR042.CSV");
    ui_textkey_set_cursor(&tk, 5);
    press(UI_TK_CLR);
    CHECK_EQ(tk.len, 0);
    CHECK_EQ(tk.cur, 0);
    CHECK_EQ(tk.view, 0);
    CHECK_STR_EQ(tk.text, "");
    type('q');
    CHECK_STR_EQ(tk.text, "q");
    CHECK_EQ(tk.cur, 1);
}

TEST_CASE(a_tap_in_the_field_places_the_cursor_in_the_nearest_gap)
{
    ui_textkey_open_file(&tk, k_area, "T", "MOTOR042.CSV");
    const int w = ui_textkey_field_face(&tk)->width;
    CHECK_EQ(w, 20);
    const int y = field_y();
    /* Left of the text, in the field's 10px edge. */
    tap_at(1u, ui_textkey_field_rect(&tk).x + 2, y);
    CHECK_EQ(tk.cur, 0);
    /* 3.2 cells in: the gap after the third character. */
    tap_at(1u, text_x() + 3 * w + 4, y);
    CHECK_EQ(tk.cur, 3);
    /* 3.6 cells in: the gap after the fourth. */
    tap_at(1u, text_x() + 3 * w + 12, y);
    CHECK_EQ(tk.cur, 4);
    /* Right of the text: its end. */
    tap_at(1u, text_x() + 30 * w, y);
    CHECK_EQ(tk.cur, 12);
    /* A German letter is one cell: the gap after "Mä" is byte 3. */
    ui_textkey_open_file(&tk, k_area, "T", "M\xc3\xa4\xc3\x9fig.txt");
    tap_at(1u, text_x() + 2 * w, y);
    CHECK_EQ(tk.cur, 3);
    tap_at(1u, text_x() + 3 * w - 2, y);
    CHECK_EQ(tk.cur, 5);
    ui_textkey_tap_text(NULL, 0);
}

TEST_CASE(a_press_that_slides_off_or_a_second_contact_moves_nothing)
{
    ui_textkey_open_file(&tk, k_area, "T", "MOTOR042.CSV");
    const int y = field_y();
    /* Down in the field, up on a key: no cursor move, no key. */
    touch_event_t e = {
        .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1u, .x = (int16_t)(text_x() + 40), .y = (int16_t)y },
    };
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    const gfx_rect_t q = ui_textkey_key_rect(&tk, 10);
    e.type = TOUCH_EVENT_UP;
    e.point.y = (int16_t)(q.y + q.h / 2);
    CHECK_EQ(ui_textkey_event(&tk, &e, NULL, 0u), UI_TEXTKEY_NONE);
    CHECK_EQ(tk.cur, 12);
    CHECK_STR_EQ(tk.text, "MOTOR042.CSV");
    /* A second contact while one is down is ignored. */
    e.type = TOUCH_EVENT_DOWN;
    e.point.y = (int16_t)y;
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    touch_event_t other = e;
    other.point.id = 2u;
    other.point.x = (int16_t)(text_x() + 100);
    (void)ui_textkey_event(&tk, &other, NULL, 0u);
    other.type = TOUCH_EVENT_UP;
    (void)ui_textkey_event(&tk, &other, NULL, 0u);
    CHECK_EQ(tk.cur, 12);
    /* The first contact's own release places it. */
    e.type = TOUCH_EVENT_UP;
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    CHECK_EQ(tk.cur, 2);
    /* A release the finger did not make places nothing. */
    e.type = TOUCH_EVENT_DOWN;
    e.point.x = (int16_t)(text_x() + 100);
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    e.type = TOUCH_EVENT_UP;
    e.flags = TOUCH_FLAG_NO_TAP;
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    CHECK_EQ(tk.cur, 2);
}

TEST_CASE(a_long_name_scrolls_to_keep_the_cursor_in_sight)
{
    CHECK_EQ(ui_textkey_field_cells(NULL), 1);
    char name[FM_NAME_MAX + 1];
    for (int i = 0; i < (int)FM_NAME_MAX; ++i) {
        name[i] = (char)('A' + i % 26);
    }
    name[FM_NAME_MAX] = '\0';
    ui_textkey_open_file(&tk, k_area, "T", name);
    const int fit = ui_textkey_field_cells(&tk);
    CHECK_EQ(fit, 48);                   /* 972px of 20 x 36 */
    /* At the end: the last 48 characters, the bar after them. */
    CHECK_EQ(ui_textkey_view(&tk), 59 - 48);
    /* Left through the window: it stays until the cursor leaves it. */
    for (int i = 0; i < 48; ++i) {
        press(UI_TK_LEFT);
    }
    CHECK_EQ(tk.cur, 11);
    CHECK_EQ(ui_textkey_view(&tk), 11);
    press(UI_TK_LEFT);
    CHECK_EQ(ui_textkey_view(&tk), 10);
    /* From the start right: the window moves once the cursor passes 48. */
    ui_textkey_set_cursor(&tk, 0);
    CHECK_EQ(ui_textkey_view(&tk), 0);
    for (int i = 0; i < 48; ++i) {
        press(UI_TK_RIGHT);
    }
    CHECK_EQ(ui_textkey_view(&tk), 0);
    press(UI_TK_RIGHT);
    CHECK_EQ(ui_textkey_view(&tk), 1);
    /* A tap in the moved window counts from its first character. */
    tap_at(1u, text_x() + 5 * 20, field_y());
    CHECK_EQ(tk.cur, 6);
    CHECK_EQ(ui_textkey_view(&tk), 1);
    /* DEL at the end pulls the window back while there is room. */
    ui_textkey_set_cursor(&tk, 59);
    for (int i = 0; i < 6; ++i) {
        press(UI_TK_DEL);
    }
    CHECK_EQ(tk.len, 53);
    CHECK_EQ(ui_textkey_view(&tk), 53 - 48);
    for (int i = 0; i < 5; ++i) {
        press(UI_TK_DEL);
    }
    CHECK_EQ(ui_textkey_view(&tk), 0);
    /* The window counts characters: 5 ä and 49 letters are 59 bytes and
     * 54 characters. */
    char wide[FM_NAME_MAX + 1];
    for (int i = 0; i < 5; ++i) {
        wide[2 * i] = '\xc3';
        wide[2 * i + 1] = '\xa4';
    }
    memset(wide + 10, 'a', 49);
    wide[59] = '\0';
    ui_textkey_open_file(&tk, k_area, "T", wide);
    CHECK_EQ(tk.len, 59);
    CHECK_EQ(ui_textkey_view(&tk), 54 - 48);
    ui_textkey_move(&tk, -51);           /* character 3, byte 6 */
    CHECK_EQ(tk.cur, 6);
    CHECK_EQ(ui_textkey_view(&tk), 3);
    gfx_color_t *fb = malloc((size_t)CW * CH * sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, CW, CH, CW);
    ui_textkey_render(&tk, &c);
    free(fb);
}

TEST_CASE(the_cursor_is_a_bar_before_its_cell)
{
    gfx_color_t *fb = malloc((size_t)CW * CH * sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, CW, CH, CW);
    ui_textkey_open_file(&tk, k_area, "T", "AB  CD");
    ui_textkey_set_cursor(&tk, 3);       /* between the two spaces */
    ui_textkey_render(&tk, &c);
    const gfx_color_t accent = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t sunk = ui_theme_color(UI_C_PANEL_SUNK);
    const int x = text_x() + 3 * 20;
    const int y = field_y();
    CHECK_EQ(fb[(size_t)y * CW + (size_t)(x - 1)], accent);
    CHECK_EQ(fb[(size_t)y * CW + (size_t)x], accent);
    CHECK_EQ(fb[(size_t)y * CW + (size_t)(x + 1)], sunk);
    CHECK_EQ(fb[(size_t)y * CW + (size_t)(x - 2)], sunk);
    /* No trailing caret: the cell after the text is empty. */
    const int end = text_x() + 6 * 20;
    const gfx_rect_t f = ui_textkey_field_rect(&tk);
    int inked = 0;
    for (int row = f.y; row < f.y + f.h; ++row) {
        for (int dx = 2; dx < 20; ++dx) {
            inked += fb[(size_t)row * CW + (size_t)(end + dx)] != sunk;
        }
    }
    CHECK_EQ(inked, 0);
    free(fb);
}

TEST_CASE(the_start_is_before_the_last_full_stop)
{
    CHECK_EQ(ui_textkey_stem_end("MOTOR042.CSV"), 8);
    CHECK_EQ(ui_textkey_stem_end("README"), 6);
    CHECK_EQ(ui_textkey_stem_end(".hidden"), 7);
    CHECK_EQ(ui_textkey_stem_end("a.b.c"), 3);
    CHECK_EQ(ui_textkey_stem_end(""), 0);
    CHECK_EQ(ui_textkey_stem_end(NULL), 0);
    ui_textkey_open_file(&tk, k_area, "T", "MOTOR042.CSV");
    ui_textkey_set_cursor(&tk, ui_textkey_stem_end(tk.text));
    type('-');
    type('k');
    type('v');
    CHECK_STR_EQ(tk.text, "MOTOR042-kv.CSV");
    CHECK_EQ(press(UI_TK_OK), UI_TEXTKEY_OK);
    CHECK_STR_EQ(s_out, "MOTOR042-kv.CSV");
    ui_textkey_set_cursor(NULL, 0);
}

TEST_CASE(a_refused_name_keeps_the_text_and_the_cursor)
{
    static const char marks[] = "!\"#$%&'()*" "+,-./:;<=>" "?@[\\]^_`{|";
    const char *const bad = ":*?<>\\|/\"";
    for (const char *p = bad; *p != '\0'; ++p) {
        ui_textkey_open_file(&tk, k_area, "T", "MOTOR042.CSV");
        ui_textkey_set_cursor(&tk, 8);
        press(UI_TK_LAYER);
        press((int)(strchr(marks, *p) - marks));
        press(UI_TK_LAYER);
        CHECK_EQ(tk.cur, 9);
        CHECK_EQ(press(UI_TK_OK), UI_TEXTKEY_NONE);
        CHECK(tk.open && tk.refused);
        CHECK_EQ(tk.why, (uint8_t)FM_NAME_CHAR);
        CHECK_EQ(tk.cur, 9);
        CHECK_EQ(tk.len, 13);
        CHECK_EQ(tk.text[8], *p);
    }
    /* DEL takes it, and OK then takes the name. */
    press(UI_TK_DEL);
    CHECK(!tk.refused);
    CHECK_EQ(press(UI_TK_OK), UI_TEXTKEY_OK);
    CHECK_STR_EQ(s_out, "MOTOR042.CSV");
}

TEST_CASE(the_bottom_row_fits_and_keeps_its_face_in_both_languages)
{
    ui_textkey_open_file(&tk, k_area, "T", "");
    /* SPACE 3, CLR 1, "<-" 1, "->" 1, CANCEL 2, OK 2 columns. */
    static const struct { int key; int col; int span; } row[] = {
        { UI_TK_SPACE, 0, 3 }, { UI_TK_CLR, 3, 1 }, { UI_TK_LEFT, 4, 1 },
        { UI_TK_RIGHT, 5, 1 }, { UI_TK_CANCEL, 6, 2 }, { UI_TK_OK, 8, 2 },
    };
    const gfx_rect_t one = ui_textkey_key_rect(&tk, 30);  /* 93 x 72px */
    CHECK_EQ(one.w, 93);
    CHECK_EQ(one.h, 72);
    for (size_t i = 0; i < sizeof(row) / sizeof(row[0]); ++i) {
        const gfx_rect_t r = ui_textkey_key_rect(&tk, row[i].key);
        CHECK_EQ(r.x, k_area.x + 8 + row[i].col * (93 + 6));
        CHECK_EQ(r.w, row[i].span * 93 + (row[i].span - 1) * 6);
        CHECK_EQ(r.h, one.h);
    }
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_LEFT), "<-");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_RIGHT), "->");
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_SHOW).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_ENTER).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_DONE).w, 0);
    /* Every key inside the area, no two overlapping. */
    for (int a = 0; a < UI_TK_ALL; ++a) {
        const gfx_rect_t ra = ui_textkey_key_rect(&tk, a);
        if (ra.w == 0) {
            continue;
        }
        CHECK(ra.x >= k_area.x && ra.x + ra.w <= k_area.x + k_area.w);
        CHECK(ra.y >= k_area.y && ra.y + ra.h <= k_area.y + k_area.h);
        for (int b = a + 1; b < UI_TK_ALL; ++b) {
            const gfx_rect_t rb = ui_textkey_key_rect(&tk, b);
            if (rb.w == 0) {
                continue;
            }
            CHECK(ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x
                  || ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y);
        }
    }
    /* ABBRECHEN in 2 columns: 9 x 20 + 8 = 188px of 192px, so the word
     * keys keep 20 x 36 in German as in English. */
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(ui_textkey_word_font(&tk) == &gfx_font_20x36);
    }
    /* The screenshot popup's keyboard, x 280 to the panel's edge: keys of
     * 67 x 74px, the words in 16 x 28; ABBRECHEN in 2 columns (140px)
     * takes 12 x 24. */
    ui_text_set_language(UI_LANG_EN);
    ui_textkey_open_file(&tk, (gfx_rect_t){ 280, 0, 744, 540 }, "T", "");
    CHECK_EQ(ui_textkey_key_rect(&tk, 30).w, 67);
    CHECK_EQ(ui_textkey_key_rect(&tk, 30).h, 74);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_CANCEL).w, 140);
    CHECK(ui_textkey_word_font(&tk) == &gfx_font_16x28);
    ui_text_set_language(UI_LANG_DE);
    CHECK(ui_textkey_word_font(&tk) == &gfx_font_12x24);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_other_keyboards_keep_no_cursor)
{
    /* Wi-Fi: no arrows, its bottom row as it was, a field tap moves
     * nothing, DEL takes the last character. */
    ui_textkey_open_wifi(&tk, k_area, "T", "lab", false);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_LEFT).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_RIGHT).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_CANCEL).w, 3 * 93 + 2 * 6);
    tap_at(1u, text_x() + 20, field_y());
    type('x');
    CHECK_STR_EQ(tk.text, "labx");
    press(UI_TK_DEL);
    CHECK_STR_EQ(tk.text, "lab");
    /* A name's keyboard: the same. */
    ui_textkey_open(&tk, k_area, "NAME", "AB", 23);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_LEFT).w, 0);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_LEFT), "");
    /* The editor's has no field. */
    ui_textkey_open_editor(&tk, k_area);
    CHECK_EQ(ui_textkey_field_rect(&tk).w, 0);
    CHECK_EQ(ui_textkey_field_rect(NULL).w, 0);
}

int main(void)
{
    RUN(typing_inserts_at_the_start_the_middle_and_the_end);
    RUN(del_takes_the_character_before_the_cursor);
    RUN(the_arrows_step_one_character_and_stop_at_the_ends);
    RUN(a_full_name_refuses_a_key_and_keeps_the_cursor);
    RUN(clr_and_an_empty_name_leave_the_cursor_at_0);
    RUN(a_tap_in_the_field_places_the_cursor_in_the_nearest_gap);
    RUN(a_press_that_slides_off_or_a_second_contact_moves_nothing);
    RUN(a_long_name_scrolls_to_keep_the_cursor_in_sight);
    RUN(the_cursor_is_a_bar_before_its_cell);
    RUN(the_start_is_before_the_last_full_stop);
    RUN(a_refused_name_keeps_the_text_and_the_cursor);
    RUN(the_bottom_row_fits_and_keeps_its_face_in_both_languages);
    RUN(the_other_keyboards_keep_no_cursor);
    return test_summary("textkey_cursor");
}
