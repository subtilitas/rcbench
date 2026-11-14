/*
 * Every string of the Dial's faces fits where it is drawn (shared/dial):
 * its ink inside radius 100 of the round display, a reading's inside its
 * cell, no two strings on each other, in English and in German, for the
 * fixed models and for models at the ends of every field.  Each string's
 * ink lies inside the band a partial draw clears for it, and none lies in
 * a cell's clear rectangle but the cell's own; a value face shows no
 * number in part.  Every cell lies
 * inside radius 100, and no shape reaches the innermost ring: the annulus
 * from radius 101 to 102 stays the background on every face.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "dial_face.h"
#include "dial_models.h"
#include "ui_theme.h"

#define N (DIAL_FACE_SIZE * DIAL_FACE_SIZE)
#define MAX_SEEN 64

typedef struct {
    gfx_rect_t ink;
    int cell;
    dial_band_t band;
    char text[DIAL_TXT];
} seen_t;

static gfx_color_t s_px[N];
static dial_render_t s_r;
static seen_t s_seen[MAX_SEEN];
static int s_n;

static void on_seen(void *ctx, const dial_seen_t *t)
{
    (void)ctx;
    if (s_n < MAX_SEEN) {
        seen_t *e = &s_seen[s_n++];
        dial_ink(t->font, t->s, t->x, t->y, &e->ink);
        e->cell = t->cell;
        e->band = t->band;
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
    const bool reading = s_r.shown.face == DIAL_FACE_READING;
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
        int y0 = 0;
        int y1 = 0;
        if (e->cell < 0 && e->ink.h > 0
            && (!dial_band_rows(e->band, &y0, &y1) || e->ink.y < y0
                || e->ink.y + e->ink.h - 1 > y1)) {
            T_FAIL("%s (%u): \"%s\" rows %d to %d leave band %d (%d to %d)",
                   what, lang, e->text, e->ink.y, e->ink.y + e->ink.h - 1,
                   (int)e->band, y0, y1);
        }
        if (e->cell >= 0 && e->band != DIAL_BAND_CELL) {
            T_FAIL("%s (%u): \"%s\" in cell %d, band %d", what, lang,
                   e->text, e->cell, (int)e->band);
        }
        /* A cell's repaint clears its rectangle: no other string's ink,
         * an umlaut's dots included, may lie in it. */
        for (int k = 0; e->cell < 0 && reading && k < DIAL_CELLS; ++k) {
            if (overlap(e->ink, dial_cell_rect(k))) {
                T_FAIL("%s (%u): \"%s\" lies in cell %d's clear", what,
                       lang, e->text, k);
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
    CHECK(dial_render_init(&s_r));
    for (uint8_t l = 0; l < 2; ++l) {
        for (int id = 0; id < DM_COUNT; ++id) {
            const uint32_t cut = s_r.shortened;
            dial_model(&m, id, l);
            fits(&m, l, k_dial_model_name[id]);
            /* The fixed words are drawn whole: no part dropped, no line
             * cut. */
            if (s_r.shortened != cut) {
                T_FAIL("%s (%u): a line is drawn short of its text",
                       k_dial_model_name[id], l);
            }
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
    /* A value before SET: its hints are whole too. */
    for (uint8_t l = 0; l < 2; ++l) {
        const uint32_t cut = s_r.shortened;
        dial_model(&m, DM_VALUE_SET, l);
        CHECK(dm_value(&m, "min", 0, 0, 60, 1, 5, 5, 0));
        fits(&m, l, "value before SET");
        CHECK_EQ(s_r.shortened, cut);
    }
}

TEST_CASE(every_string_fits_at_the_ends_of_every_field)
{
    dial_model_t m;
    CHECK(dial_render_init(&s_r));
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

/** Whether every word with a digit among the strings drawn is one of
 *  @p want, whole. */
static void numbers_whole(const char *const *want, int n_want,
                          const char *what)
{
    for (int i = 0; i < s_n; ++i) {
        char buf[DIAL_TXT];
        dk_text_copy(buf, sizeof buf, s_seen[i].text);
        for (const char *w = strtok(buf, " "); w != NULL; w = strtok(NULL, " ")) {
            bool ok = strpbrk(w, "0123456789") == NULL;
            for (int k = 0; k < n_want && !ok; ++k) {
                ok = strcmp(w, want[k]) == 0;
            }
            if (!ok) {
                T_FAIL("%s: \"%s\" shows the number \"%s\" in part", what,
                       s_seen[i].text, w);
            }
        }
    }
}

/** Whether a drawn string holds @p part. */
static bool drawn(const char *part)
{
    for (int i = 0; i < s_n; ++i) {
        if (strstr(s_seen[i].text, part) != NULL) {
            return true;
        }
    }
    return false;
}

TEST_CASE(a_value_face_shows_no_number_in_part)
{
    static const struct {
        const char *unit;
        uint8_t dec;
        int32_t min, max, step, value, was;
    } k_case[] = {
        { "\xC2\xB0", 1, -1000, 1000, 5, 125, 0 },
        { "ms", 3, INT32_MIN, INT32_MAX, 1, -1, INT32_MIN },
        { "\xC2\xB0" "C", 4, INT32_MIN, INT32_MAX, 1, INT32_MIN, INT32_MAX },
        { "%", 2, -99999, 99999, 25, 12345, -99999 },
        { "", 0, 0, 1000000, 1000, 500000, 999000 },
    };
    dial_model_t m;
    CHECK(dial_render_init(&s_r));
    for (uint8_t l = 0; l < 2; ++l) {
        for (size_t i = 0; i < sizeof k_case / sizeof k_case[0]; ++i) {
            char t[5][24];
            const char *want[5];
            const int32_t v[5] = { k_case[i].value, k_case[i].min,
                                   k_case[i].max, k_case[i].step,
                                   k_case[i].was };
            for (int k = 0; k < 5; ++k) {
                dk_value_text(v[k], k_case[i].dec, "", t[k], sizeof t[k]);
                want[k] = t[k];
            }
            dial_model(&m, DM_VALUE_SET, l);
            CHECK(dm_value(&m, k_case[i].unit, k_case[i].dec, k_case[i].min,
                           k_case[i].max, k_case[i].step, k_case[i].value,
                           k_case[i].was, DK_VAL_SET));
            fits(&m, l, "value");
            numbers_whole(want, 5, l ? "value (de)" : "value (en)");
            /* The value itself is always drawn, and the step, whose part
             * of its line comes first. */
            char st[40];
            snprintf(st, sizeof st, l ? "SCHRITT %s" : "STEP %s", t[3]);
            CHECK(drawn(t[0]));
            CHECK(drawn(st));
        }
        /* A trim of -100.0 to 100.0 in steps of 0.5 shows its step. */
        dial_model(&m, DM_VALUE_SET, l);
        CHECK(dm_value(&m, "\xC2\xB0", 1, -1000, 1000, 5, 125, 0,
                       DK_VAL_SET));
        fits(&m, l, "trim");
        CHECK(drawn(l ? "SCHRITT 0.5" : "STEP 0.5"));
        CHECK(drawn("-100.0"));
        CHECK(drawn("100.0"));
    }
}

int main(void)
{
    RUN(every_cell_lies_inside_radius_100);
    RUN(every_string_of_the_fixed_models_fits_where_it_is_drawn);
    RUN(every_string_fits_at_the_ends_of_every_field);
    RUN(a_value_face_shows_no_number_in_part);
    return test_summary("dial_fit");
}
