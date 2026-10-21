/*
 * The numeric keypad: what a key adds, what OK hands back and what it
 * refuses, and the press contract every button here keeps.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "ui_keypad.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "touch_feed.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

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
    REQUIRE_ALLOC(fb);
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

/* ------------------------------------------------- the Wi-Fi keyboard */

/* The key that types @p ch on the Wi-Fi keyboard's @p layer, or -1. */
static int wf_key(ui_textkey_layer_t layer, char ch)
{
    ui_textkey_t probe = tk;
    probe.layer = layer;
    for (int k = 0; k < UI_TK_DEL; ++k) {
        if (k == UI_TK_LAYER || (k == UI_TK_SHIFT && layer != UI_TK_MARKS)) {
            continue;
        }
        const char *l = ui_textkey_key_label(&probe, k);
        if (l[0] == ch && l[1] == '\0') {
            return k;
        }
    }
    return -1;
}

/* Type @p s, switching layers as a hand would. */
static void wf_type(const char *s)
{
    char out[80];
    for (; *s != '\0'; ++s) {
        if (*s == ' ') {
            (void)tk_press(UI_TK_SPACE, out, sizeof(out));
            continue;
        }
        ui_textkey_layer_t want = UI_TK_MARKS;
        if ((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9')
            || *s == '-') {
            want = (tk.layer == UI_TK_UPPER && !(*s >= 'a' && *s <= 'z'))
                       ? UI_TK_UPPER : UI_TK_LOWER;
        } else if (*s >= 'A' && *s <= 'Z') {
            want = UI_TK_UPPER;
        }
        if (want != tk.layer) {
            if (tk.layer == UI_TK_MARKS || want == UI_TK_MARKS) {
                (void)tk_press(UI_TK_LAYER, out, sizeof(out));
            }
            if (want == UI_TK_UPPER && tk.layer != UI_TK_UPPER) {
                (void)tk_press(UI_TK_SHIFT, out, sizeof(out));
            } else if (want == UI_TK_LOWER && tk.layer == UI_TK_UPPER) {
                (void)tk_press(UI_TK_SHIFT, out, sizeof(out));
            }
        }
        CHECK_EQ((int)tk.layer, (int)want);
        const int key = wf_key(tk.layer, *s);
        CHECK(key >= 0);
        (void)tk_press(key, out, sizeof(out));
    }
}

/* Every printable ASCII character has one key on one layer, and the layer
 * keys type nothing. */
TEST_CASE(the_wifi_layers_hold_every_printable_character_once)
{
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", "", true);
    CHECK(tk.wifi && tk.secret && !tk.search);
    CHECK_EQ(tk.max_len, 63);
    CHECK_EQ((int)tk.layer, (int)UI_TK_LOWER);
    int seen[128] = { 0 };
    for (int layer = UI_TK_LOWER; layer <= UI_TK_MARKS; ++layer) {
        for (int k = 0; k < UI_TK_DEL; ++k) {
            ui_textkey_t probe = tk;
            probe.layer = (ui_textkey_layer_t)layer;
            const char *l = ui_textkey_key_label(&probe, k);
            if (k == UI_TK_LAYER
                || (k == UI_TK_SHIFT && layer != UI_TK_MARKS)
                || l[0] == '\0') {
                continue;
            }
            CHECK_EQ(l[1], '\0');
            const int ch = (unsigned char)l[0];
            CHECK(ch > 0x20 && ch < 0x7F);
            /* Digits and "-" stand on both letter layers. */
            if (layer == UI_TK_UPPER
                && ((ch >= '0' && ch <= '9') || ch == '-')) {
                continue;
            }
            ++seen[ch];
        }
    }
    for (int ch = 0x21; ch < 0x7F; ++ch) {
        if (ch == '-') {
            CHECK_EQ(seen[ch], 2);       /* the letters' and the marks' */
        } else if (seen[ch] != 1) {
            T_FAIL("character 0x%02X on %d keys", ch, seen[ch]);
        }
    }
    /* The layer keys' legends, and what they do. */
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_SHIFT), "ABC");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_LAYER), "#+=");
    char out[80];
    (void)tk_press(UI_TK_SHIFT, out, sizeof(out));
    CHECK_EQ((int)tk.layer, (int)UI_TK_UPPER);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_SHIFT), "abc");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, tk_key('Q')), "Q");
    (void)tk_press(UI_TK_LAYER, out, sizeof(out));
    CHECK_EQ((int)tk.layer, (int)UI_TK_MARKS);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_LAYER), "abc");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, 0), "!");
    (void)tk_press(UI_TK_LAYER, out, sizeof(out));
    CHECK_EQ((int)tk.layer, (int)UI_TK_LOWER);
    CHECK_EQ(tk.len, 0);                 /* no layer key typed anything */
    /* A blank of the marks layer types nothing either. */
    (void)tk_press(UI_TK_LAYER, out, sizeof(out));
    (void)tk_press(UI_TK_DEL - 2, out, sizeof(out));
    CHECK_EQ(tk.len, 0);
}

/* A password: 8 to 63 characters, any of them, spaces kept. */
TEST_CASE(a_password_takes_8_to_63_characters_as_typed)
{
    char out[80] = "unset";
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", "", true);
    wf_type("Ab1 %~x");
    CHECK_STR_EQ(tk.text, "Ab1 %~x");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
    CHECK(tk.refused);                   /* 7 characters */
    CHECK(tk.open);
    CHECK_STR_EQ(out, "unset");
    wf_type("\\");
    CHECK(!tk.refused);
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "Ab1 %~x\\");      /* 8, as typed */
    CHECK(!tk.open);

    /* 63 is the most; the 64th is refused. */
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", "", true);
    for (int i = 0; i < 63; ++i) {
        wf_type((i % 2 == 0) ? "q" : "\"");
    }
    CHECK_EQ(tk.len, 63);
    wf_type("z");
    CHECK_EQ(tk.len, 63);
    CHECK_EQ(tk.text[62], 'q');
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_EQ((int)strlen(out), 63);

    /* Opened on a text longer than 63, it keeps 63. */
    char long_[80];
    memset(long_, 'p', sizeof(long_) - 1u);
    long_[sizeof(long_) - 1u] = '\0';
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", long_, true);
    CHECK_EQ(tk.len, 63);
}

/* A network name: up to 32 bytes, an empty one refused. */
TEST_CASE(a_network_name_takes_1_to_32_bytes)
{
    char out[80] = "unset";
    ui_textkey_open_wifi(&tk, k_area, "NETWORK NAME", "", false);
    CHECK(tk.wifi && !tk.secret);
    CHECK_EQ(tk.max_len, 32);
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
    CHECK(tk.refused);
    for (int i = 0; i < 33; ++i) {
        wf_type("N");
    }
    CHECK_EQ(tk.len, 32);
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_EQ((int)strlen(out), 32);
    /* No SHOW on a name; the bottom row is the search keyboard's. */
    ui_textkey_open_wifi(&tk, k_area, "NETWORK NAME", "Lab 2", false);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_SHOW).w, 0);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_SHOW), "");
    wf_type(" ");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "Lab 2 ");         /* spaces are part of a name */
}

/* A password shows as "*" until SHOW, and SHOW again hides it. */
TEST_CASE(a_password_is_masked_until_shown)
{
    gfx_color_t *fb = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
    CHECK(fb != NULL);
    if (fb == NULL) {
        return;
    }
    gfx_canvas_t cv;
    gfx_canvas_init(&cv, fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    char out[80];
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", "secret99", true);
    const gfx_rect_t show = ui_textkey_key_rect(&tk, UI_TK_SHOW);
    CHECK(show.w >= 40 && show.h >= 40);
    CHECK(show.x >= k_area.x && show.x + show.w <= k_area.x + k_area.w);
    /* No bottom key overlaps another. */
    for (int a = UI_TK_SPACE; a <= UI_TK_SHOW; ++a) {
        for (int b = a + 1; b <= UI_TK_SHOW; ++b) {
            const gfx_rect_t ra = ui_textkey_key_rect(&tk, a);
            const gfx_rect_t rb = ui_textkey_key_rect(&tk, b);
            CHECK(ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x);
        }
    }
    CHECK(!tk.show);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_SHOW), "SHOW");
    ui_textkey_render(&tk, &cv);
    gfx_color_t *masked = malloc((size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t));
    CHECK(masked != NULL);
    if (masked == NULL) {
        free(fb);
        return;
    }
    memcpy(masked, fb, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t));
    (void)tk_press(UI_TK_SHOW, out, sizeof(out));
    CHECK(tk.show);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_SHOW), "HIDE");
    CHECK_EQ(tk.len, 8);                 /* SHOW types nothing */
    ui_textkey_render(&tk, &cv);
    CHECK(memcmp(masked, fb, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t)) != 0);
    (void)tk_press(UI_TK_SHOW, out, sizeof(out));
    CHECK(!tk.show);
    ui_textkey_render(&tk, &cv);
    CHECK(memcmp(masked, fb, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t)) == 0);
    /* The same password with other characters masks to the same picture. */
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", "QQQQQQQQ", true);
    ui_textkey_render(&tk, &cv);
    CHECK(memcmp(masked, fb, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t)) == 0);
    free(masked);
    free(fb);
}

/* Type @p text on the keyboard, a key a character, "/" by the dot key
 * of a folder keyboard. */
static void tk_type(const char *text)
{
    char out[4];
    for (const char *p = text; *p != '\0'; ++p) {
        const int key = (*p == '/') ? UI_TK_DOT
                        : (*p == ' ') ? UI_TK_SPACE : tk_key(*p);
        (void)tk_press(key, out, sizeof(out));
    }
}

/*
 * Opened for a folder: the "." key shows and types "/", OK hands back the
 * folder as card_folder_check() writes it, and the name keyboard opened
 * after it types "." again.
 */
TEST_CASE(a_folder_keyboard_types_a_slash_and_takes_a_checked_folder)
{
    char out[CARD_DIR_MAX] = "unset";
    ui_textkey_open_folder(&tk, k_area, "Auto log folder", "BENCH/LOGS");
    CHECK(tk.folder);
    CHECK(!tk.search && !tk.wifi);
    CHECK_EQ(tk.max_len, UI_TEXTKEY_MAX);
    CHECK_STR_EQ(tk.text, "BENCH/LOGS");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_DOT), "/");
    CHECK_STR_EQ(ui_textkey_label(UI_TK_DOT), ".");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_MARK), "_");
    (void)tk_press(UI_TK_CLR, out, sizeof(out));
    tk_type("MY_LOG-2/RUNS");
    CHECK_STR_EQ(tk.text, "MY_LOG-2/RUNS");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "MY_LOG-2/RUNS");
    CHECK(!tk.open);
    CHECK(!tk.refused);

    /* The name keyboard after it: "." is a dot. */
    ui_textkey_open(&tk, k_area, "NAME", "", 23);
    CHECK(!tk.folder);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_DOT), ".");
    (void)tk_press(UI_TK_DOT, out, sizeof(out));
    CHECK_STR_EQ(tk.text, ".");

    /* A text opened in lower case is handed back in upper case; one
     * longer than the keyboard takes is cut to its 23 characters. */
    ui_textkey_open_folder(&tk, k_area, "F", "my/logs");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "MY/LOGS");
    ui_textkey_open_folder(&tk, k_area, "F", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK_EQ(tk.len, UI_TEXTKEY_MAX);
    tk_type("A");
    CHECK_EQ(tk.len, UI_TEXTKEY_MAX);
}

/* A refused OK keeps the keyboard open with what was typed, says why, and
 * hands nothing back; the next key clears the refusal. */
TEST_CASE(a_folder_keyboard_refuses_what_the_card_check_refuses)
{
    static const struct {
        const char   *text;
        card_folder_t why;
        const char   *line;
    } cases[] = {
        { "",           CARD_FOLDER_EMPTY,    "A folder name is empty" },
        { "/LOGS",      CARD_FOLDER_EMPTY,    "A folder name is empty" },
        { "LOGS/",      CARD_FOLDER_EMPTY,    "A folder name is empty" },
        { "A//B",       CARD_FOLDER_EMPTY,    "A folder name is empty" },
        { "BENCHLOGS",  CARD_FOLDER_LONG,     "8 characters a folder at most" },
        { "A/B/C",      CARD_FOLDER_DEEP,     "2 folders deep at most" },
        { "MY LOGS",    CARD_FOLDER_CHAR,     "A-Z, 0-9, _ and - only" },
        { "LOGS.OLD",   CARD_FOLDER_CHAR,     "A-Z, 0-9, _ and - only" },
        { "AUX/LOGS",   CARD_FOLDER_RESERVED, "Reserved name" },
        { "BENCH/KST",  CARD_FOLDER_BENCH,    "Folder of the bench's own files" },
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char out[CARD_DIR_MAX] = "unset";
        ui_textkey_open_folder(&tk, k_area, "F", cases[i].text);
        CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
        CHECK(tk.open);
        CHECK(tk.refused);
        CHECK_EQ(tk.why, (uint8_t)cases[i].why);
        CHECK_STR_EQ(tk.text, cases[i].text);
        CHECK_STR_EQ(out, "unset");
    }
    /* The texts, in both languages. */
    CHECK_STR_EQ(TR(TEXTKEY_DIR_LONG), "8 characters a folder at most");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(TEXTKEY_DIR_LONG), "Höchstens 8 Zeichen je Ordner");
    CHECK_STR_EQ(TR(TEXTKEY_DIR_BENCH), "Ordner der Prüfstand-Dateien");
    ui_text_set_language(UI_LANG_EN);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *want = cases[i].line;
        const char *got;
        switch (cases[i].why) {
        case CARD_FOLDER_LONG:     got = TR(TEXTKEY_DIR_LONG);     break;
        case CARD_FOLDER_DEEP:     got = TR(TEXTKEY_DIR_DEEP);     break;
        case CARD_FOLDER_CHAR:     got = TR(TEXTKEY_DIR_CHAR);     break;
        case CARD_FOLDER_RESERVED: got = TR(TEXTKEY_DIR_RESERVED); break;
        case CARD_FOLDER_BENCH:    got = TR(TEXTKEY_DIR_BENCH);    break;
        default:                   got = TR(TEXTKEY_DIR_EMPTY);    break;
        }
        CHECK_STR_EQ(got, want);
    }

    /* At the edges: 9 refused, one DEL and 8 taken; BENCH/KST refused, an
     * X more taken. */
    char out[CARD_DIR_MAX] = "unset";
    ui_textkey_open_folder(&tk, k_area, "F", "BENCHLOGS");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
    (void)tk_press(UI_TK_DEL, out, sizeof(out));
    CHECK(!tk.refused);
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "BENCHLOG");
    ui_textkey_open_folder(&tk, k_area, "F", "BENCH/KST");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
    tk_type("X");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "BENCH/KSTX");
    ui_textkey_open_folder(&tk, k_area, "F", "A/B/C");
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_NONE);
    (void)tk_press(UI_TK_DEL, out, sizeof(out));
    (void)tk_press(UI_TK_DEL, out, sizeof(out));
    CHECK_EQ(tk_press(UI_TK_OK, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "A/B");

    /* CANCEL hands nothing back. */
    strcpy(out, "unset");
    ui_textkey_open_folder(&tk, k_area, "F", "LOGS");
    tk_type("X");
    CHECK_EQ(tk_press(UI_TK_CANCEL, out, sizeof(out)), UI_TEXTKEY_CANCELLED);
    CHECK_STR_EQ(out, "unset");
    CHECK(!tk.open);
}

/* A refusal draws its line in the warning colour, and a different line for
 * a different refusal. */
TEST_CASE(a_folder_refusal_is_drawn_in_its_own_words)
{
    gfx_color_t *fb = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
    gfx_color_t *first = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
    REQUIRE_ALLOC(first);
    gfx_canvas_t cv;
    gfx_canvas_init(&cv, fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    char out[CARD_DIR_MAX];
    ui_textkey_open_folder(&tk, k_area, "F", "BENCHLOGS");
    (void)tk_press(UI_TK_OK, out, sizeof(out));
    ui_textkey_render(&tk, &cv);
    int warn = 0;
    for (int i = 0; i < UI_PANEL_W * UI_PANEL_H; ++i) {
        warn += (fb[i] == ui_theme_color(UI_C_WARN)) ? 1 : 0;
    }
    CHECK(warn > 0);
    memcpy(first, fb, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t));
    ui_textkey_open_folder(&tk, k_area, "F", "BENCH/KS");
    tk_type("T");
    (void)tk_press(UI_TK_OK, out, sizeof(out));
    CHECK_EQ(tk.why, (uint8_t)CARD_FOLDER_BENCH);
    memset(fb, 0, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t));
    ui_textkey_render(&tk, &cv);
    /* The title row: the title the same, the line not. */
    const size_t row0 = (size_t)(k_area.y + 6) * 800u;
    CHECK(memcmp(first + row0, fb + row0, (size_t)18 * 800u
                 * sizeof(gfx_color_t)) != 0);
    free(first);
    free(fb);
}

/* The name and search keyboards are what they were: no SHOW, capitals. */
TEST_CASE(the_name_keyboards_have_no_wifi_keys)
{
    ui_textkey_open_wifi(&tk, k_area, "PASSWORD", "", true);
    ui_textkey_open(&tk, k_area, "NAME", "", 23);
    CHECK(!tk.wifi && !tk.secret);
    CHECK_EQ(ui_textkey_key_rect(&tk, UI_TK_SHOW).w, 0);
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_SHIFT), "Z");
    CHECK_STR_EQ(ui_textkey_key_label(&tk, UI_TK_LAYER), ".");
    char out[8];
    (void)tk_press(UI_TK_SHIFT, out, sizeof(out));
    CHECK_STR_EQ(tk.text, "Z");
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

/* A contact the tracker ends for a jump types nothing: its release is on
 * the key it pressed and the finger did not make it.  The press the tracker
 * starts where the contact is next reported types its key on its lift. */
static void to_keypad(const touch_event_t *e)
{
    (void)ui_keypad_event(&kp, e, NULL);
}

static void to_keyboard(const touch_event_t *e)
{
    static char out[32];
    (void)ui_textkey_event(&tk, e, out, sizeof(out));
}

TEST_CASE(a_release_the_tracker_makes_for_a_jump_types_nothing)
{
    open_v();
    const gfx_rect_t r7 = ui_keypad_key_rect(&kp, UI_KEY_7);
    const gfx_rect_t r3 = ui_keypad_key_rect(&kp, UI_KEY_3);
    CHECK(r3.y - r7.y > TOUCH_JUMP_PX || r3.x - r7.x > TOUCH_JUMP_PX
          || (r3.y - r7.y) + (r3.x - r7.x) > TOUCH_JUMP_PX);
    feed_reset();
    feed_to_handler(to_keypad);
    finger(FEED_LONE, r7.x + 5, r7.y + 5);
    finger(FEED_LONE, r3.x + 5, r3.y + 5);    /* the id, reused elsewhere */
    CHECK_EQ(kp.len, 0);
    lift(FEED_LONE);
    CHECK_STR_EQ(kp.entry, "3");
    feed_tap(FEED_LONE, r7.x + 5, r7.y + 5);
    CHECK_STR_EQ(kp.entry, "37");
    ui_keypad_close(&kp);

    ui_textkey_open(&tk, k_area, "NAME", "", 23);
    const gfx_rect_t q = ui_textkey_key_rect(&tk, tk_key('Q'));
    const gfx_rect_t m = ui_textkey_key_rect(&tk, tk_key('M'));
    CHECK((m.x - q.x) + (m.y - q.y) > TOUCH_JUMP_PX);
    feed_reset();
    feed_to_handler(to_keyboard);
    finger(FEED_LONE, q.x + 5, q.y + 5);
    finger(FEED_LONE, m.x + 5, m.y + 5);
    CHECK_EQ(tk.len, 0);
    lift(FEED_LONE);
    CHECK_STR_EQ(tk.text, "M");
    feed_tap(FEED_LONE, q.x + 5, q.y + 5);
    CHECK_STR_EQ(tk.text, "MQ");
    ui_textkey_close(&tk);
}

TEST_CASE(the_keyboard_draws_inside_its_area_and_only_while_open)
{
    gfx_color_t *fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
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
    RUN(the_wifi_layers_hold_every_printable_character_once);
    RUN(a_password_takes_8_to_63_characters_as_typed);
    RUN(a_network_name_takes_1_to_32_bytes);
    RUN(a_password_is_masked_until_shown);
    RUN(a_folder_keyboard_types_a_slash_and_takes_a_checked_folder);
    RUN(a_folder_keyboard_refuses_what_the_card_check_refuses);
    RUN(a_folder_refusal_is_drawn_in_its_own_words);
    RUN(the_name_keyboards_have_no_wifi_keys);
    RUN(a_keyboard_key_acts_on_its_own_release_only);
    RUN(a_release_the_tracker_makes_for_a_jump_types_nothing);
    RUN(the_keyboard_draws_inside_its_area_and_only_while_open);
    return test_summary("keypad");
}
