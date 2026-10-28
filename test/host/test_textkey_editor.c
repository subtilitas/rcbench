/**
 * @file test_textkey_editor.c
 * @brief The text keyboard's editor mode: the keys fill the area, the
 *        bottom row is ENTER, SPACE, "<-", "->" and DONE, every key answers
 *        what it was, the three layers type every printable character, and
 *        a second contact is ignored.
 *
 * Cases:
 * - the_keys_fill_the_area_and_the_bottom_row_spans_ten_columns
 * - every_key_answers_what_it_is
 * - the_layers_type_every_printable_character
 * - a_key_acts_on_its_own_release_and_a_second_contact_is_ignored
 * - the_keyboard_draws_in_its_area_its_word_keys_in_one_face
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/** @brief The editor's keyboard area on FILES: under a panel of 4 rows. */
static const gfx_rect_t k_area = { 8, 158, 1008, 338 };

static ui_textkey_t tk;

/**
 * @brief Press and release the middle of @p key with contact 1.
 * @return the release's answer
 */
static ui_textkey_result_t press(int key)
{
    const gfx_rect_t r = ui_textkey_key_rect(&tk, key);
    touch_event_t e = {
        .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1u, .x = (int16_t)(r.x + r.w / 2),
                   .y = (int16_t)(r.y + r.h / 2) },
    };
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    e.type = TOUCH_EVENT_UP;
    return ui_textkey_event(&tk, &e, NULL, 0u);
}

TEST_CASE(the_keys_fill_the_area_and_the_bottom_row_spans_ten_columns)
{
    ui_textkey_open_editor(&tk, k_area);
    CHECK(tk.open);
    CHECK(tk.editor);
    const gfx_rect_t first = ui_textkey_key_rect(&tk, 0);
    CHECK_EQ(first.x, 16);
    CHECK_EQ(first.y, 166);                 /* no title, no field */
    CHECK(first.h >= 48);
    const gfx_rect_t enter = ui_textkey_key_rect(&tk, UI_TK_ENTER);
    const gfx_rect_t space = ui_textkey_key_rect(&tk, UI_TK_SPACE);
    const gfx_rect_t left = ui_textkey_key_rect(&tk, UI_TK_LEFT);
    const gfx_rect_t right = ui_textkey_key_rect(&tk, UI_TK_RIGHT);
    const gfx_rect_t done = ui_textkey_key_rect(&tk, UI_TK_DONE);
    const gfx_rect_t z = ui_textkey_key_rect(&tk, 30);
    /* ENTER 2 columns, SPACE 4, the arrows 1 each, DONE 2, in that order,
     * 6px apart, from the left edge of the grid to its right. */
    CHECK_EQ(enter.x, first.x);
    CHECK_EQ(enter.w, 2 * first.w + 6);
    CHECK_EQ(space.x, enter.x + enter.w + 6);
    CHECK_EQ(space.w, 4 * first.w + 3 * 6);
    CHECK_EQ(left.x, space.x + space.w + 6);
    CHECK_EQ(left.w, first.w);
    CHECK_EQ(right.x, left.x + left.w + 6);
    CHECK_EQ(done.x, right.x + right.w + 6);
    CHECK_EQ(done.x + done.w, ui_textkey_key_rect(&tk, 9).x
                                  + ui_textkey_key_rect(&tk, 9).w);
    CHECK_EQ(enter.y, z.y + z.h + 6);
    CHECK(enter.y + enter.h <= UI_KEYS_FOOT);
    /* The name keyboard's CLR, CANCEL, OK and SHOW are not there. */
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_CLR).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_CANCEL).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_OK).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_SHOW).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_ALL).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&tk, -1).w, 0);
    /* And another keyboard has none of the editor's. */
    ui_textkey_t name;
    memset(&name, 0, sizeof(name));
    ui_textkey_open_file(&name, k_area, "NAME", "");
    CHECK_EQ(ui_textkey_key_rect(&name, UI_TK_ENTER).w, 0);
    CHECK_EQ(ui_textkey_key_rect(&name, UI_TK_DONE).w, 0);
    ui_textkey_open_editor(NULL, k_area);
}

TEST_CASE(every_key_answers_what_it_is)
{
    ui_text_set_language(UI_LANG_EN);
    ui_textkey_open_editor(&tk, k_area);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_ENTER), "ENTER");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_LEFT), "<-");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_RIGHT), "->");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_DONE), "DONE");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_DEL), "DEL");
    CHECK_EQ(press(UI_TK_ENTER), UI_TEXTKEY_ENTER);
    CHECK_EQ(press(UI_TK_LEFT), UI_TEXTKEY_LEFT);
    CHECK_EQ(press(UI_TK_RIGHT), UI_TEXTKEY_RIGHT);
    CHECK_EQ(press(UI_TK_DEL), UI_TEXTKEY_BACKSPACE);
    CHECK_EQ(press(UI_TK_SPACE), UI_TEXTKEY_TYPED);
    CHECK_EQ(tk.typed, ' ');
    CHECK_EQ(press(10), UI_TEXTKEY_TYPED);  /* q */
    CHECK_EQ(tk.typed, 'q');
    CHECK_EQ(press(0), UI_TEXTKEY_TYPED);
    CHECK_EQ(tk.typed, '1');
    /* The editor keeps no text. */
    CHECK_EQ(tk.len, 0);
    CHECK(tk.open);
    CHECK_EQ(press(UI_TK_DONE), UI_TEXTKEY_DONE);
    CHECK(!tk.open);
    CHECK_EQ(press(UI_TK_ENTER), UI_TEXTKEY_NONE);   /* closed */
    ui_text_set_language(UI_LANG_DE);
    ui_textkey_open_editor(&tk, k_area);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_DONE), "FERTIG");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_ENTER), "ENTER");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_layers_type_every_printable_character)
{
    ui_textkey_open_editor(&tk, k_area);
    int seen[128] = { 0 };
    for (int layer = 0; layer < 3; ++layer) {
        CHECK_EQ(tk.layer, (ui_textkey_layer_t)layer);
        for (int key = 0; key < UI_TK_DEL; ++key) {
            if (key == UI_TK_LAYER
                || (key == UI_TK_SHIFT && tk.layer != UI_TK_MARKS)) {
                continue;
            }
            const ui_textkey_result_t r = press(key);
            if (r == UI_TEXTKEY_TYPED) {
                CHECK(tk.typed > ' ' && tk.typed < 0x7F);
                seen[(unsigned char)tk.typed & 0x7Fu] = 1;
            } else {
                CHECK_EQ(r, UI_TEXTKEY_NONE);   /* a blank */
            }
        }
        /* lower -> capitals -> marks */
        if (layer < 2) {
            CHECK_EQ(press(layer == 0 ? UI_TK_SHIFT : UI_TK_LAYER),
                     UI_TEXTKEY_NONE);
        }
    }
    CHECK_EQ(tk.layer, UI_TK_MARKS);
    for (int ch = 0x21; ch < 0x7F; ++ch) {
        if (seen[ch] != 1) {
            CHECK_EQ(ch, 0);                /* names the one missing */
        }
    }
    CHECK_EQ(press(UI_TK_LAYER), UI_TEXTKEY_NONE);
    CHECK_EQ(tk.layer, UI_TK_LOWER);
}

TEST_CASE(a_key_acts_on_its_own_release_and_a_second_contact_is_ignored)
{
    ui_textkey_open_editor(&tk, k_area);
    const gfx_rect_t a = ui_textkey_key_rect(&tk, UI_TK_ENTER);
    const gfx_rect_t b = ui_textkey_key_rect(&tk, UI_TK_DONE);
    touch_event_t e = {
        .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1u, .x = (int16_t)(a.x + 4), .y = (int16_t)(a.y + 4) },
    };
    CHECK_EQ(ui_textkey_event(&tk, &e, NULL, 0u), UI_TEXTKEY_NONE);
    /* A second finger on DONE while ENTER is held. */
    touch_event_t f = e;
    f.point.id = 2u;
    f.point.x = (int16_t)(b.x + 4);
    f.point.y = (int16_t)(b.y + 4);
    CHECK_EQ(ui_textkey_event(&tk, &f, NULL, 0u), UI_TEXTKEY_NONE);
    f.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_textkey_event(&tk, &f, NULL, 0u), UI_TEXTKEY_NONE);
    CHECK(tk.open);
    /* The first one let go off its key: nothing. */
    e.type = TOUCH_EVENT_UP;
    e.point.x = (int16_t)(b.x + 4);
    CHECK_EQ(ui_textkey_event(&tk, &e, NULL, 0u), UI_TEXTKEY_NONE);
    /* A press dropped by a lost event acts on no release. */
    e.type = TOUCH_EVENT_DOWN;
    e.point.x = (int16_t)(a.x + 4);
    (void)ui_textkey_event(&tk, &e, NULL, 0u);
    ui_textkey_cancel_press(&tk);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_textkey_event(&tk, &e, NULL, 0u), UI_TEXTKEY_NONE);
    CHECK_EQ(press(UI_TK_ENTER), UI_TEXTKEY_ENTER);
}

TEST_CASE(the_keyboard_draws_in_its_area_its_word_keys_in_one_face)
{
    const int w = 1024;
    const int h = 540;
    gfx_color_t *fb = calloc((size_t)w * h, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, w, h, w);
    ui_textkey_open_editor(&tk, k_area);
    /* Characters in 20 x 36; the words share one face no larger. */
    CHECK(ui_textkey_char_font(&tk) == &gfx_font_20x36);
    const gfx_font_t *wf = ui_textkey_word_font(&tk);
    CHECK(wf->height <= gfx_font_20x36.height);
    ui_textkey_render(&tk, &c);
    int outside = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool in = x >= k_area.x && x < k_area.x + k_area.w
                            && y >= k_area.y && y < k_area.y + k_area.h;
            outside += (!in && fb[(size_t)y * w + x] != 0u) ? 1 : 0;
        }
    }
    CHECK_EQ(outside, 0);
    free(fb);
}

int main(void)
{
    RUN(the_keys_fill_the_area_and_the_bottom_row_spans_ten_columns);
    RUN(every_key_answers_what_it_is);
    RUN(the_layers_type_every_printable_character);
    RUN(a_key_acts_on_its_own_release_and_a_second_contact_is_ignored);
    RUN(the_keyboard_draws_in_its_area_its_word_keys_in_one_face);
    return test_summary("textkey_editor");
}
