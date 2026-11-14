/*
 * Every string of the Dial's faces fits where it is drawn (shared/dial):
 * its ink inside radius 100 of the round display, a reading's inside its
 * cell, no two strings on each other, in English and in German, for the
 * fixed models and for models at the ends of every field.  Every cell lies
 * inside radius 100, and no shape reaches the innermost ring: the annulus
 * from radius 101 to 102 stays the background on every face.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "greatest.h"

#include "dial_face.h"
#include "dial_models.h"
#include "ui_theme.h"

#define N (DIAL_FACE_SIZE * DIAL_FACE_SIZE)
#define MAX_SEEN 64

typedef struct {
    gfx_rect_t ink;
    int cell;
    char text[DIAL_TXT];
} seen_t;

static gfx_color_t s_px[N];
static dial_render_t s_r;
static uint32_t s_keys[DIAL_RING_KEYS];
static seen_t s_seen[MAX_SEEN];
static int s_n;

static void on_seen(void *ctx, const dial_seen_t *t)
{
    (void)ctx;
    if (s_n < MAX_SEEN) {
        seen_t *e = &s_seen[s_n++];
        dial_ink(t->font, t->s, t->x, t->y, &e->ink);
        e->cell = t->cell;
        dk_text_copy(e->text, sizeof e->text, t->s);
    }
}

static gfx_color_t bg(void)
{
    const uint32_t v = ui_theme_rgb24(UI_THEME_DARK, UI_C_BG);
    return GFX_RGB((v >> 16) & 0xFFu, (v >> 8) & 0xFFu, v & 0xFFu);
}

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.w > 0 && b.w > 0 && a.x < b.x + b.w && b.x < a.x + a.w
           && a.y < b.y + b.h && b.y < a.y + a.h;
}

/** Draw a model in full and hold every string and shape to its place. */
static void fits(const dial_model_t *m, uint8_t lang, const char *what)
{
    gfx_canvas_t c;
    s_n = 0;
    dial_render_invalidate(&s_r);
    s_r.lang = lang;
    s_r.seen = on_seen;
    gfx_canvas_init(&c, s_px, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    dial_render(&s_r, &c, &m->face, &m->view);
    s_r.seen = NULL;
    if (s_n == 0 || s_n >= MAX_SEEN) {
        T_FAIL("%s (%u): %d strings", what, lang, s_n);
    }
    for (int i = 0; i < s_n; ++i) {
        const seen_t *e = &s_seen[i];
        if (!dial_box_inside(e->ink, DIAL_R_TEXT)) {
            T_FAIL("%s (%u): \"%s\" at %d,%d %dx%d leaves radius %d", what,
                   lang, e->text, e->ink.x, e->ink.y, e->ink.w, e->ink.h,
                   DIAL_R_TEXT);
        }
        if (e->cell >= 0) {
            const gfx_rect_t b = dial_cell_rect(e->cell);
            gfx_rect_t in;
            if (e->ink.w > 0
                && (!gfx_rect_intersect(e->ink, b, &in) || in.w != e->ink.w
                    || in.h != e->ink.h)) {
                T_FAIL("%s (%u): \"%s\" leaves cell %d", what, lang, e->text,
                       e->cell);
            }
        }
        for (int k = 0; k < i; ++k) {
            if (overlap(e->ink, s_seen[k].ink)) {
                T_FAIL("%s (%u): \"%s\" lies on \"%s\"", what, lang, e->text,
                       s_seen[k].text);
            }
        }
    }
    /* Between radius 101 and 102: nothing but the background. */
    for (int y = 0; y < DIAL_FACE_SIZE; ++y) {
        for (int x = 0; x < DIAL_FACE_SIZE; ++x) {
            const int dx = 2 * x - DIAL_FACE_C2;
            const int dy = 2 * y - DIAL_FACE_C2;
            const int d2 = dx * dx + dy * dy;
            if (d2 > 4 * DIAL_R_SHAPE * DIAL_R_SHAPE
                && d2 <= 4 * DIAL_R_CLEAR * DIAL_R_CLEAR
                && s_px[y * DIAL_FACE_SIZE + x] != bg()) {
                T_FAIL("%s (%u): pixel %d,%d between radius %d and %d", what,
                       lang, x, y, DIAL_R_SHAPE, DIAL_R_CLEAR);
                return;
            }
        }
    }
}

TEST_CASE(every_cell_lies_inside_radius_100)
{
    for (int i = 0; i < DIAL_CELLS; ++i) {
        const gfx_rect_t b = dial_cell_rect(i);
        CHECK_EQ(b.w, DIAL_CELL_W);
        CHECK_EQ(b.h, DIAL_CELL_H);
        CHECK(dial_box_inside(b, DIAL_R_TEXT));
        for (int k = 0; k < i; ++k) {
            CHECK(!overlap(b, dial_cell_rect(k)));
        }
    }
    /* The rings: radius 118 outward first, 3px apart, the innermost 3px
     * outside the cells' circle. */
    CHECK_EQ(dial_ring_radius(0), 118);
    CHECK_EQ(dial_ring_radius(DIAL_RINGS - 1), 103);
    CHECK(!dial_box_inside(gfx_rect_make(0, 0, 1, 1), DIAL_R_TEXT));
    CHECK(dial_box_inside(gfx_rect_make(0, 0, 0, 0), DIAL_R_TEXT));
}

TEST_CASE(every_string_of_the_fixed_models_fits_where_it_is_drawn)
{
    dial_model_t m;
    dial_scene_t sc;
    CHECK(dial_render_init(&s_r, s_keys));
    for (uint8_t l = 0; l < 2; ++l) {
        for (int id = 0; id < DM_COUNT; ++id) {
            dial_model(&m, id, l);
            fits(&m, l, k_dial_model_name[id]);
            /* The fixed models' titles are drawn whole, not cut to fit
             * the tag. */
            dial_scene_build(&sc, &m.face, &m.view, l);
            bool whole = !sc.has_title;
            for (int i = 0; i < s_n; ++i) {
                whole = whole || strcmp(s_seen[i].text, sc.title) == 0;
            }
            if (!whole) {
                T_FAIL("%s (%u): the title \"%s\" is cut",
                       k_dial_model_name[id], l, sc.title);
            }
        }
    }
}

TEST_CASE(every_string_fits_at_the_ends_of_every_field)
{
    dial_model_t m;
    CHECK(dial_render_init(&s_r, s_keys));
    for (uint8_t l = 0; l < 2; ++l) {
        /* MOTOR: every reading at its widest, the clock at its longest. */
        const uint16_t clocks[] = { 0, 599, 3599, 36000, 65535 };
        for (size_t k = 0; k < sizeof clocks / sizeof clocks[0]; ++k) {
            const dk_live_motor_t lm = {
                0, DK_LIVE_ARMED, 0x3F, 10000, 65535, 65535, -32768, 65535,
                -40, 125, clocks[k] };
            dial_model(&m, DM_MOTOR_ARMED, l);
            CHECK(dm_motor(&m, lm));
            m.view.batt_mv = 3300;
            fits(&m, l, "motor at its ends");
            m.view.batt_mv = 0;
            m.view.usb = true;
            fits(&m, l, "motor at its ends, on USB");
        }
        {
            const dk_live_motor_t lm = {
                0, 0, 0x3F, 9999, 9999, 9999, 999, 999, 125, -40, 0 };
            dial_model(&m, DM_MOTOR_SAFE, l);
            CHECK(dm_motor(&m, lm));
            fits(&m, l, "motor SAFE");
        }
        {
            const dk_live_servo_t ls = {
                0, DK_LIVE_ARMED | DK_LIVE_MEAS | DK_LIVE_RAIL, 2500, -1800,
                -18000, 65535, -32768, 65535, 0, 0 };
            dial_model(&m, DM_SERVO_LIVE, l);
            CHECK(dm_servo(&m, ls));
            fits(&m, l, "servo at its ends");
        }
        /* A list of the longest rows and the most rows. */
        dial_model(&m, DM_NAVIGATION, l);
        CHECK(dm_nav(&m, 254, 255, DK_SLOT_PREV, "MMMMMMMMMMMMMMM"));
        CHECK(dm_nav(&m, 254, 255, DK_SLOT_FOCUS, "MMMMMMMMMMMMMMM"));
        CHECK(dm_nav(&m, 254, 255, DK_SLOT_NEXT, "MMMMMMMMMMMMMMM"));
        fits(&m, l, "long list");
        dial_model(&m, DM_NAVIGATION, l);
        CHECK(dm_nav(&m, 0, 0, DK_SLOT_FOCUS, ""));
        fits(&m, l, "empty list");
        /* A value at the ends of int32 with 4 decimals and a unit. */
        dial_model(&m, DM_VALUE_SET, l);
        CHECK(dm_value(&m, "\xC2\xB0" "C", 4, INT32_MIN, INT32_MAX, 1,
                       INT32_MIN, INT32_MAX, DK_VAL_SET | DK_VAL_LIMIT));
        fits(&m, l, "value at its ends");
        dial_model(&m, DM_VALUE_SET, l);
        CHECK(dm_value(&m, "ms", 0, 0, 1000, 10, 990, 20, 0));
        fits(&m, l, "value not set");
        /* The longest title, on a face with the pill and on one without. */
        dial_model(&m, DM_OTHER, l);
        CHECK(dm_screen(&m, DK_FACE_OTHER, l, "WWWWWWWWWWWWWWW"));
        CHECK(dm_state(&m, DK_STA_ARMED | DK_STA_DEGRADED));
        fits(&m, l, "long title");
        dial_model(&m, DM_MOTOR_ARMED, l);
        CHECK(dm_screen(&m, DK_FACE_MOTOR, l, "WWWWWWWWWWWWWWW"));
        fits(&m, l, "long motor title");
        /* The longest name a Dial carries. */
        dial_model(&m, DM_BIND, l);
        m.view.name = "DIAL-WWWWWWW";
        fits(&m, l, "long name");
        dial_model(&m, DM_WAITING, l);
        m.view.name = "DIAL-WWWWWWW";
        fits(&m, l, "long name waiting");
    }
}

int main(void)
{
    RUN(every_cell_lies_inside_radius_100);
    RUN(every_string_of_the_fixed_models_fits_where_it_is_drawn);
    RUN(every_string_fits_at_the_ends_of_every_field);
    return test_summary("dial_fit");
}
