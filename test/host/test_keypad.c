/*
 * The numeric keypad: what a key adds, what OK hands back and what it
 * refuses, and the press contract every button here keeps.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_keypad.h"
#include "ui_textkey.h"
#include "ui_theme.h"

#define W 800
#define H 480

static const gfx_rect_t k_area = { 6, 24, 546, 402 };

static ui_keypad_t kp;

static void open_v(void)
{
    ui_keypad_open(&kp, k_area, "VOLTAGE", "V", 6.0f, 3.3f, 21.0f, 2);
}

static ui_keypad_result_t ev(int x, int y, touch_event_type_t t, uint8_t id,
                             float *out)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    return ui_keypad_event(&kp, &e, out);
}

static ui_keypad_result_t press(ui_key_t key, float *out)
{
    const gfx_rect_t r = ui_keypad_key_rect(&kp, key);
    const int x = r.x + r.w / 2, y = r.y + r.h / 2;
    (void)ev(x, y, TOUCH_EVENT_DOWN, 1, out);
    return ev(x, y, TOUCH_EVENT_UP, 1, out);
}

static void type(const char *s)
{
    for (const char *p = s; *p != '\0'; ++p) {
        ui_key_t k = UI_KEY_0;
        switch (*p) {
        case '0': k = UI_KEY_0; break;
        case '1': k = UI_KEY_1; break;
        case '2': k = UI_KEY_2; break;
        case '3': k = UI_KEY_3; break;
        case '4': k = UI_KEY_4; break;
        case '5': k = UI_KEY_5; break;
        case '6': k = UI_KEY_6; break;
        case '7': k = UI_KEY_7; break;
        case '8': k = UI_KEY_8; break;
        case '9': k = UI_KEY_9; break;
        default:  k = UI_KEY_DOT; break;
        }
        (void)press(k, NULL);
    }
}

TEST_CASE(typed_digits_come_back_on_ok)
{
    open_v();
    type("12.34");
    CHECK_STR_EQ(kp.entry, "12.34");
    float v = 0.0f;
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, 12.34f, 1e-4f);
    CHECK(!kp.open);
}

TEST_CASE(the_entry_takes_one_point_and_the_digits_it_keeps)
{
    open_v();
    type(".");                       /* a leading point is "0." */
    CHECK_STR_EQ(kp.entry, "0.");
    type(".5");                      /* a second point is ignored */
    CHECK_STR_EQ(kp.entry, "0.5");
    type("67");                      /* two decimals and no more */
    CHECK_STR_EQ(kp.entry, "0.56");
    (void)press(UI_KEY_DEL, NULL);
    CHECK_STR_EQ(kp.entry, "0.5");
    (void)press(UI_KEY_CLR, NULL);
    CHECK_STR_EQ(kp.entry, "");
    (void)press(UI_KEY_DEL, NULL);   /* nothing to delete */
    CHECK_EQ(kp.len, 0);

    /* No decimals: the point is not a key that does anything. */
    ui_keypad_open(&kp, k_area, "TRIP TIME", "ms", 100.0f, 0.0f, 5000.0f, 0);
    type("2.5");
    CHECK_STR_EQ(kp.entry, "25");

    /* And the entry has a length. */
    type("0000000000");
    CHECK_EQ(kp.len, UI_KEYPAD_ENTRY_MAX);
}

TEST_CASE(a_value_outside_the_range_is_refused_and_the_keypad_stays)
{
    open_v();
    type("25");
    float v = -1.0f;
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_NONE);
    CHECK(kp.open);
    CHECK(kp.refused);
    CHECK_EQ(v, -1.0f);
    type("1");                       /* typing again clears the refusal */
    CHECK(!kp.refused);
    (void)press(UI_KEY_CLR, NULL);
    type("2");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_NONE);   /* under 3.3 */
    CHECK(kp.refused);

    /* A range end inside half the last place goes through, clamped. */
    (void)press(UI_KEY_CLR, NULL);
    type("21.00");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, 21.0f, 1e-4f);
}

TEST_CASE(ok_on_nothing_typed_and_cancel_leave_the_value)
{
    float v = -1.0f;
    open_v();
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_CANCEL);
    CHECK(!kp.open);
    CHECK_EQ(v, -1.0f);
    open_v();
    type("7");
    CHECK_EQ(press(UI_KEY_CANCEL, &v), UI_KEYPAD_CANCEL);
    CHECK_EQ(v, -1.0f);
}

TEST_CASE(a_key_acts_on_its_own_release_only)
{
    open_v();
    const gfx_rect_t r7 = ui_keypad_key_rect(&kp, UI_KEY_7);
    const gfx_rect_t r8 = ui_keypad_key_rect(&kp, UI_KEY_8);
    /* Slid off before the release: nothing. */
    (void)ev(r7.x + 5, r7.y + 5, TOUCH_EVENT_DOWN, 1, NULL);
    (void)ev(r8.x + 5, r8.y + 5, TOUCH_EVENT_MOVE, 1, NULL);
    (void)ev(r8.x + 5, r8.y + 5, TOUCH_EVENT_UP, 1, NULL);
    CHECK_EQ(kp.len, 0);
    /* A second contact neither presses nor releases. */
    (void)ev(r7.x + 5, r7.y + 5, TOUCH_EVENT_DOWN, 1, NULL);
    (void)ev(r8.x + 5, r8.y + 5, TOUCH_EVENT_DOWN, 2, NULL);
    (void)ev(r8.x + 5, r8.y + 5, TOUCH_EVENT_UP, 2, NULL);
    CHECK_EQ(kp.len, 0);
    (void)ev(r7.x + 5, r7.y + 5, TOUCH_EVENT_UP, 1, NULL);
    CHECK_STR_EQ(kp.entry, "7");
    /* A press dropped for a touch loss types nothing. */
    (void)ev(r8.x + 5, r8.y + 5, TOUCH_EVENT_DOWN, 1, NULL);
    ui_keypad_cancel_press(&kp);
    (void)ev(r8.x + 5, r8.y + 5, TOUCH_EVENT_UP, 1, NULL);
    CHECK_STR_EQ(kp.entry, "7");
    /* Outside every key, and with the keypad closed, nothing. */
    (void)ev(k_area.x + 2, k_area.y + 2, TOUCH_EVENT_DOWN, 1, NULL);
    CHECK_EQ(kp.pressed, -1);
    ui_keypad_close(&kp);
    CHECK_EQ(press(UI_KEY_OK, NULL), UI_KEYPAD_NONE);
    CHECK_EQ(ui_keypad_event(NULL, NULL, NULL), UI_KEYPAD_NONE);
}

TEST_CASE(the_keys_tile_the_area_and_ok_spans_two)
{
    open_v();
    const gfx_rect_t r0  = ui_keypad_key_rect(&kp, UI_KEY_0);
    const gfx_rect_t rok = ui_keypad_key_rect(&kp, UI_KEY_OK);
    const gfx_rect_t r9  = ui_keypad_key_rect(&kp, UI_KEY_9);
    CHECK(rok.w > 2 * r0.w);
    CHECK(rok.y == r0.y);
    CHECK(r9.y < r0.y);
    for (int k = 0; k < UI_KEY_COUNT; ++k) {
        const gfx_rect_t r = ui_keypad_key_rect(&kp, (ui_key_t)k);
        CHECK(r.x >= k_area.x && r.x + r.w <= k_area.x + k_area.w);
        CHECK(r.y >= k_area.y && r.y + r.h <= k_area.y + k_area.h);
        CHECK(r.w >= 60 && r.h >= 50);       /* a fingertip and then some */
    }
    const gfx_rect_t none = ui_keypad_key_rect(&kp, UI_KEY_COUNT);
    CHECK_EQ(none.w, 0);
}

TEST_CASE(the_keypad_draws_inside_its_area_and_only_while_open)
{
    gfx_color_t *fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    gfx_canvas_t cv;
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    open_v();
    type("25");
    (void)press(UI_KEY_OK, NULL);    /* refused: the range in the warning */
    ui_keypad_render(&kp, &cv);
    int inside = 0, outside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[y * W + x] == 0) {
                continue;
            }
            const bool in = x >= k_area.x && x < k_area.x + k_area.w
                            && y >= k_area.y && y < k_area.y + k_area.h;
            if (in) {
                ++inside;
            } else {
                ++outside;
            }
        }
    }
    CHECK(inside > 0);
    CHECK_EQ(outside, 0);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_keypad_close(&kp);
    ui_keypad_render(&kp, &cv);
    int any = 0;
    for (int i = 0; i < W * H; ++i) {
        any += (fb[i] != 0) ? 1 : 0;
    }
    CHECK_EQ(any, 0);
    free(fb);
}

/* ------------------------------------------------------- text keyboard */

static ui_textkey_t tk;

static ui_textkey_result_t tk_press(int key, char *out, size_t n)
{
    const gfx_rect_t r = ui_textkey_key_rect(&tk, key);
    const touch_event_t d = { .type = TOUCH_EVENT_DOWN,
                              .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                         .y = (int16_t)(r.y + r.h / 2) } };
    touch_event_t u = d;
    u.type = TOUCH_EVENT_UP;
    (void)ui_textkey_event(&tk, &d, out, n);
    return ui_textkey_event(&tk, &u, out, n);
}

/* The index of a character key. */
static int tk_key(char ch)
{
    static const char chars[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.";
    const char *p = strchr(chars, ch);
    return (p != NULL) ? (int)(p - chars) : -1;
}

TEST_CASE(the_keyboard_edits_the_name_it_was_opened_with)
{
    char out[32] = "";
    ui_textkey_open(&tk, k_area, "NAME", "SERVO", 23);
    CHECK_EQ(tk.len, 5);
    (void)tk_press(UI_TK_DEL, out, sizeof(out));
    CHECK_STR_EQ(tk.text, "SERV");
    (void)tk_press(tk_key('-'), out, sizeof(out));
    (void)tk_press(tk_key('7'), out, sizeof(out));
    (void)tk_press(UI_TK_SPACE, out, sizeof(out));
    CHECK_STR_EQ(tk.text, "SERV-7 ");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "SERV-7");              /* the trailing space dropped */
    CHECK(!tk.open);
}

TEST_CASE(the_keyboard_refuses_an_empty_name_and_keeps_its_length)
{
    char out[8] = "unset";
    ui_textkey_open(&tk, k_area, "NAME", "AB", 4);
    (void)tk_press(UI_TK_CLR, out, sizeof(out));
    (void)tk_press(UI_TK_SPACE, out, sizeof(out));
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
    CHECK(tk.refused);
    CHECK(tk.open);
    CHECK_STR_EQ(out, "unset");
    for (const char *p = "QWERTY"; *p != '\0'; ++p) {
        (void)tk_press(tk_key(*p), out, sizeof(out));
    }
    CHECK_STR_EQ(tk.text, " QWE");            /* four, and no more */
    CHECK(!tk.refused);
    /* A name longer than what it is handed back into is cut, terminated. */
    char small[3];
    CHECK_EQ(tk_press(UI_TK_OK, small, sizeof(small)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(small, "QW");
    ui_textkey_open(&tk, k_area, "NAME", "AB", 23);
    CHECK_EQ(tk_press(UI_TK_CANCEL, out, sizeof(out)), UI_TEXTKEY_CANCELLED);
    CHECK(!tk.open);
}

/* Opened for a search: "*" in place of "_", the text as typed, an empty
 * one taken, and a bottom row that still tiles its width. */
TEST_CASE(a_search_keyboard_types_a_star_and_takes_an_empty_text)
{
    char out[24] = "unset";
    ui_textkey_open_search(&tk, k_area, "SEARCH", "SKY", 16);
    CHECK(tk.search);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_MARK), "*");
    CHECK_STR_EQ(ui_textkey_label(UI_TK_MARK), "_");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, tk_key('V')), "V");
    (void)tk_press(UI_TK_MARK, out, sizeof(out));
    (void)tk_press(tk_key('V'), out, sizeof(out));
    (void)tk_press(UI_TK_SPACE, out, sizeof(out));
    CHECK_STR_EQ(tk.text, "SKY*V ");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "SKY*V ");                 /* as typed, not trimmed */
    CHECK(!tk.open);

    ui_textkey_open_search(&tk, k_area, "SEARCH", "SKY", 16);
    (void)tk_press(UI_TK_CLR, out, sizeof(out));
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "");                       /* no search */
    CHECK(!tk.refused);

    /* The bottom row: SPACE and CANCEL three columns, CLR and OK two, end
     * to end across the row of characters above. */
    ui_textkey_open_search(&tk, k_area, "SEARCH", "", 16);
    const gfx_rect_t z = ui_textkey_key_rect(&tk, tk_key('Z'));
    const gfx_rect_t del = ui_textkey_key_rect(&tk, UI_TK_DEL);
    const gfx_rect_t sp = ui_textkey_key_rect(&tk, UI_TK_SPACE);
    const gfx_rect_t clr = ui_textkey_key_rect(&tk, UI_TK_CLR);
    const gfx_rect_t can = ui_textkey_key_rect(&tk, UI_TK_CANCEL);
    const gfx_rect_t ok = ui_textkey_key_rect(&tk, UI_TK_OK);
    CHECK_EQ(sp.x, z.x);
    CHECK_EQ(ok.x + ok.w, del.x + del.w);
    CHECK_EQ(sp.w, can.w);
    CHECK_EQ(clr.w, ok.w);
    CHECK(sp.w > clr.w);
    CHECK(clr.x > sp.x + sp.w && can.x > clr.x + clr.w
          && ok.x > can.x + can.w);
    /* CANCEL hands nothing back. */
    strcpy(out, "kept");
    CHECK_EQ(tk_press(UI_TK_CANCEL, out, sizeof(out)), UI_TEXTKEY_CANCELLED);
    CHECK_STR_EQ(out, "kept");
    /* A name keyboard opened after it is a name keyboard again. */
    ui_textkey_open(&tk, k_area, "NAME", "", 23);
    CHECK(!tk.search);
    (void)tk_press(UI_TK_MARK, out, sizeof(out));
    CHECK_STR_EQ(tk.text, "_");
    ui_textkey_open_search(NULL, k_area, "SEARCH", "", 16);
    CHECK_STR_EQ(ui_textkey_key_label(NULL, UI_TK_MARK), "_");
}

TEST_CASE(a_keyboard_key_acts_on_its_own_release_only)
{
    char out[32] = "";
    ui_textkey_open(&tk, k_area, "NAME", "", 23);
    const gfx_rect_t a = ui_textkey_key_rect(&tk, tk_key('A'));
    const gfx_rect_t b = ui_textkey_key_rect(&tk, tk_key('B'));
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(a.x + 5),
                                   .y = (int16_t)(a.y + 5) } };
    (void)ui_textkey_event(&tk, &e, out, sizeof(out));
    e.point.id = 2;                           /* a second contact: ignored */
    e.point.x = (int16_t)(b.x + 5);
    e.point.y = (int16_t)(b.y + 5);
    (void)ui_textkey_event(&tk, &e, out, sizeof(out));
    e.type = TOUCH_EVENT_UP;
    (void)ui_textkey_event(&tk, &e, out, sizeof(out));
    CHECK_EQ(tk.len, 0);
    e.point.id = 1;                           /* the first, slid off onto B */
    (void)ui_textkey_event(&tk, &e, out, sizeof(out));
    CHECK_EQ(tk.len, 0);
    /* A press dropped for a touch loss types nothing. */
    e.type = TOUCH_EVENT_DOWN;
    e.point.x = (int16_t)(a.x + 5);
    e.point.y = (int16_t)(a.y + 5);
    (void)ui_textkey_event(&tk, &e, out, sizeof(out));
    ui_textkey_cancel_press(&tk);
    e.type = TOUCH_EVENT_UP;
    (void)ui_textkey_event(&tk, &e, out, sizeof(out));
    CHECK_EQ(tk.len, 0);
    /* Every key inside the area; none outside the range. */
    for (int k = 0; k < UI_TK_KEYS; ++k) {
        const gfx_rect_t r = ui_textkey_key_rect(&tk, k);
        CHECK(r.x >= k_area.x && r.x + r.w <= k_area.x + k_area.w);
        CHECK(r.y >= k_area.y && r.y + r.h <= k_area.y + k_area.h);
        CHECK(r.w >= 40 && r.h >= 40);
        CHECK(ui_textkey_label(k)[0] != '\0');
    }
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_KEYS).w, 0);
    CHECK_STR_EQ(ui_textkey_label(-1), "");
    ui_textkey_close(&tk);
    CHECK_EQ(ui_textkey_event(&tk, &e, out, sizeof(out)), UI_TEXTKEY_NONE);
}

TEST_CASE(the_keyboard_draws_inside_its_area_and_only_while_open)
{
    gfx_color_t *fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    gfx_canvas_t cv;
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    char out[4];
    ui_textkey_open(&tk, k_area, "NAME", "", 23);
    (void)tk_press(UI_TK_OK, out, sizeof(out));   /* refused: in the warning */
    ui_textkey_render(&tk, &cv);
    int outside = 0, inside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[y * W + x] == 0) {
                continue;
            }
            const bool in = x >= k_area.x && x < k_area.x + k_area.w
                            && y >= k_area.y && y < k_area.y + k_area.h;
            if (in) { ++inside; } else { ++outside; }
        }
    }
    CHECK(inside > 0);
    CHECK_EQ(outside, 0);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_textkey_close(&tk);
    ui_textkey_render(&tk, &cv);
    int any = 0;
    for (int i = 0; i < W * H; ++i) {
        any += (fb[i] != 0) ? 1 : 0;
    }
    CHECK_EQ(any, 0);
    free(fb);
}

int main(void)
{
    RUN(typed_digits_come_back_on_ok);
    RUN(the_entry_takes_one_point_and_the_digits_it_keeps);
    RUN(a_value_outside_the_range_is_refused_and_the_keypad_stays);
    RUN(ok_on_nothing_typed_and_cancel_leave_the_value);
    RUN(a_key_acts_on_its_own_release_only);
    RUN(the_keys_tile_the_area_and_ok_spans_two);
    RUN(the_keypad_draws_inside_its_area_and_only_while_open);
    RUN(the_keyboard_edits_the_name_it_was_opened_with);
    RUN(the_keyboard_refuses_an_empty_name_and_keeps_its_length);
    RUN(a_search_keyboard_types_a_star_and_takes_an_empty_text);
    RUN(a_keyboard_key_acts_on_its_own_release_only);
    RUN(the_keyboard_draws_inside_its_area_and_only_while_open);
    return test_summary("keypad");
}
