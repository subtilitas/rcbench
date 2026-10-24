/**
 * @file
 * @brief Host unit tests for the four faces of the 1024 x 600 layout.
 *
 * The faces are the 10x20, 12x24 and 20x36 text faces and the 32x44 numeral
 * face.  The cases cover cell size, glyph coverage, the German letters, cap
 * height, baseline and the cut at the canvas's right and bottom edge.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include "gfx.h"
#include "ui_theme.h"

/* A canvas narrower than its buffer: the columns right of CW and the rows
 * below CH belong to the buffer, not to the canvas, and must stay empty. */
#define CW     100
#define CH     50
#define STRIDE 160
#define ROWS   64

static gfx_color_t s_pixels[STRIDE * ROWS];
static gfx_canvas_t s_c;

static void fresh(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
    gfx_canvas_init(&s_c, s_pixels, CW, CH, STRIDE);
}

typedef struct {
    const gfx_font_t *font;
    const char *name;
    int w, h;
    bool numerals;      /* digits and punctuation only, 0x20..0x3A          */
    char ref;           /* the glyph the cap height is measured on           */
    int cap_top;        /* first row with ink of ref                         */
    int baseline;       /* row after the last row with ink of ref            */
} face_t;

/* Cap top and baseline are the rows 'H' (text faces) and '0' (numerals)
 * occupy; the measured cap heights are 12, 15, 23 and 40px; the 20x36 face is
 * generated at TrueType size 31 so that its descenders end inside the cell. */
static const face_t k_faces[] = {
    { &gfx_font_10x20,     "10x20", 10, 20, false, 'H', 3, 15 },
    { &gfx_font_12x24,     "12x24", 12, 24, false, 'H', 4, 19 },
    { &gfx_font_20x36,     "20x36", 20, 36, false, 'H', 6, 29 },
    { &gfx_font_num_32x44, "32x44", 32, 44, true,  '0', 1, 41 },
};
#define N_FACES (sizeof(k_faces) / sizeof(k_faces[0]))

static int ink(void)
{
    int n = 0;
    for (int i = 0; i < STRIDE * ROWS; ++i) {
        n += s_pixels[i] != 0;
    }
    return n;
}

/* Pixels outside the canvas: right of CW or below CH. */
static int outside(void)
{
    int n = 0;
    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < STRIDE; ++x) {
            if ((x >= CW || y >= CH) && s_pixels[y * STRIDE + x] != 0) {
                ++n;
            }
        }
    }
    return n;
}

static bool same_pixels(const gfx_color_t *a, const gfx_color_t *b)
{
    return memcmp(a, b, sizeof(s_pixels)) == 0;
}

/* The pixels one code point draws, as a copy. */
static void draw_one(const face_t *f, const char *utf8, gfx_color_t *out)
{
    fresh();
    gfx_text(&s_c, 0, 0, utf8, f->font, GFX_RED, 1);
    memcpy(out, s_pixels, sizeof(s_pixels));
}

static gfx_color_t s_a[STRIDE * ROWS];
static gfx_color_t s_q[STRIDE * ROWS];

TEST_CASE(new_faces_have_the_planned_cells)
{
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        const gfx_font_t *ft = f->font;
        CHECK_EQ(ft->width, f->w);
        CHECK_EQ(ft->height, f->h);
        CHECK_EQ(ft->bpp, 8);
        CHECK_EQ(ft->bytes_per_row, f->w);
        CHECK_EQ(ft->first, 0x20);
        CHECK_EQ(ft->last, f->numerals ? 0x3A : 0x7E);
        CHECK_EQ(ft->extra_count, f->numerals ? 0 : 9);
        CHECK(f->numerals || ft->extra != NULL);

        CHECK_EQ(gfx_text_width(ft, "ABC", 1), 3 * f->w);
        CHECK_EQ(gfx_text_width(ft, "ABC", 2), 6 * f->w);
        CHECK_EQ(gfx_text_width(ft, "", 1), 0);
        CHECK_EQ(gfx_text_height(ft, 1), f->h);
        CHECK_EQ(gfx_text_height(ft, 3), 3 * f->h);
        /* One code point is one cell, also for a two-byte letter. */
        CHECK_EQ(gfx_text_width(ft, "\xc3\x84", 1), f->w);
    }
}

TEST_CASE(new_text_faces_list_the_german_letters_in_order)
{
    /* The degree sign first, then the German letters, then the ohm sign
     * (U+03A9, Greek capital omega): ascending. */
    static const uint16_t k_german[] = {
        0xB0, 0xC4, 0xD6, 0xDC, 0xDF, 0xE4, 0xF6, 0xFC, 0x3A9,
    };
    for (size_t i = 0; i < N_FACES; ++i) {
        const gfx_font_t *ft = k_faces[i].font;
        if (k_faces[i].numerals) {
            continue;
        }
        for (size_t k = 0; k < 9; ++k) {
            CHECK_EQ(ft->extra[k], k_german[k]);
        }
    }
}

/* Every code point of the block has a glyph of its own: the space is empty,
 * every other glyph has ink and differs from the '?' a missing code point
 * falls back to. */
TEST_CASE(new_faces_hold_every_glyph)
{
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        draw_one(f, "?", s_q);     /* the fallback; a box in the numerals */
        for (int code = f->font->first; code <= f->font->last; ++code) {
            const char s[2] = { (char)code, 0 };
            fresh();
            int adv = gfx_text(&s_c, 0, 0, s, f->font, GFX_RED, 1);
            CHECK_EQ(adv, f->w);
            if (code == ' ') {
                CHECK_EQ(ink(), 0);
                continue;
            }
            if (ink() == 0) {
                T_FAIL("face %s code 0x%02x draws nothing", f->name, code);
            }
            if (code != '?' && same_pixels(s_pixels, s_q)) {
                T_FAIL("face %s code 0x%02x draws as the fallback", f->name,
                       code);
            }
            CHECK_EQ(outside(), 0);
        }
    }
}

/* Each German letter draws, differs from the fallback and from its base
 * letter. */
TEST_CASE(new_text_faces_draw_the_german_letters)
{
    static const struct {
        const char *utf8;
        const char *base;
    } k_letters[] = {
        { "\xc3\x84", "A" }, { "\xc3\x96", "O" }, { "\xc3\x9c", "U" },
        { "\xc3\x9f", "B" },
        { "\xc3\xa4", "a" }, { "\xc3\xb6", "o" }, { "\xc3\xbc", "u" },
    };
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        if (f->numerals) {
            continue;
        }
        draw_one(f, "?", s_q);
        for (size_t k = 0; k < sizeof(k_letters) / sizeof(k_letters[0]); ++k) {
            draw_one(f, k_letters[k].utf8, s_a);
            const int n = ink();
            if (n == 0) {
                T_FAIL("face %s letter %zu draws nothing", f->name, k);
            }
            CHECK_EQ(gfx_text(&s_c, 0, 0, k_letters[k].utf8, f->font,
                              GFX_RED, 1), f->w);
            if (same_pixels(s_a, s_q)) {
                T_FAIL("face %s letter %zu draws as '?'", f->name, k);
            }
            draw_one(f, k_letters[k].base, s_q);
            if (same_pixels(s_a, s_q)) {
                T_FAIL("face %s letter %zu draws as %s", f->name, k,
                       k_letters[k].base);
            }
            draw_one(f, "?", s_q);
        }
        /* The diaeresis sits above the cap line: Ä has ink in a row where A
         * has none. */
        draw_one(f, "\xc3\x84", s_a);
        int top_a = -1;
        for (int y = 0; y < f->h && top_a < 0; ++y) {
            for (int x = 0; x < f->w; ++x) {
                if (s_a[y * STRIDE + x]) {
                    top_a = y;
                    break;
                }
            }
        }
        draw_one(f, "A", s_q);
        int top_plain = -1;
        for (int y = 0; y < f->h && top_plain < 0; ++y) {
            for (int x = 0; x < f->w; ++x) {
                if (s_q[y * STRIDE + x]) {
                    top_plain = y;
                    break;
                }
            }
        }
        CHECK(top_a >= 0 && top_plain >= 0 && top_a < top_plain);
    }
}

/* Cap height and baseline: the rows the reference glyph occupies. */
TEST_CASE(new_faces_have_the_planned_cap_height_and_baseline)
{
    static const int k_cap[] = { 12, 15, 23, 40 };
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        const char s[2] = { f->ref, 0 };
        fresh();
        gfx_text(&s_c, 0, 0, s, f->font, GFX_RED, 1);
        int top = -1;
        int bottom = -1;
        for (int y = 0; y < CH; ++y) {
            for (int x = 0; x < CW; ++x) {
                if (s_pixels[y * STRIDE + x]) {
                    if (top < 0) {
                        top = y;
                    }
                    bottom = y + 1;
                    break;
                }
            }
        }
        CHECK_EQ(top, f->cap_top);
        CHECK_EQ(bottom, f->baseline);
        CHECK_EQ(bottom - top, k_cap[i]);
    }
}

/* The descenders end above the cell's last row: a stroke cut by the cell
 * would leave ink on that row. */
TEST_CASE(new_text_faces_end_descenders_inside_the_cell)
{
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        if (f->numerals) {
            continue;
        }
        for (const char *d = "gjpqy"; *d; ++d) {
            const char s[2] = { *d, 0 };
            fresh();
            gfx_text(&s_c, 0, 0, s, f->font, GFX_RED, 1);
            CHECK(ink() > 0);
            for (int x = 0; x < f->w; ++x) {
                if (s_pixels[(f->h - 1) * STRIDE + x] != 0) {
                    T_FAIL("face %s '%c' has ink on the cell's last row",
                           f->name, *d);
                }
            }
        }
    }
}

/* A string that runs past the canvas's right edge is cut there: nothing
 * lands in the buffer columns beyond it, and the advance is still the whole
 * string's. */
TEST_CASE(new_faces_clip_at_the_right_edge)
{
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        const char *s = f->numerals ? "000" : "WWW";
        /* One and a half cells fit; the second glyph is cut. */
        const int x0 = CW - f->w - f->w / 2;

        fresh();
        int adv = gfx_text(&s_c, x0, 0, s, f->font, GFX_RED, 1);
        CHECK_EQ(adv, 3 * f->w);
        const int cut = ink();
        CHECK_EQ(outside(), 0);

        /* The same two glyphs drawn where they fit, for the full count. */
        fresh();
        gfx_text(&s_c, 0, 0, "WW", f->font, GFX_RED, 1);
        if (f->numerals) {
            fresh();
            gfx_text(&s_c, 0, 0, "00", f->font, GFX_RED, 1);
        }
        const int whole = ink();
        CHECK(cut > 0);
        CHECK(cut < whole);

        /* Right of the cut the first glyph is whole: columns inside the
         * canvas carry ink up to the edge. */
        fresh();
        gfx_text(&s_c, x0, 0, s, f->font, GFX_RED, 1);
        bool edge_ink = false;
        for (int y = 0; y < CH; ++y) {
            edge_ink = edge_ink || s_pixels[y * STRIDE + (CW - 1)] != 0;
        }
        CHECK(edge_ink);

        /* A glyph wholly right of the edge draws nothing. */
        fresh();
        gfx_text(&s_c, CW, 0, s, f->font, GFX_RED, 1);
        CHECK_EQ(ink(), 0);
    }
}

/* The bottom edge cuts the same way, and a text box cuts a string to its
 * own right edge as it does for the 8x16 face. */
TEST_CASE(new_faces_clip_at_the_bottom_edge_and_in_a_box)
{
    for (size_t i = 0; i < N_FACES; ++i) {
        const face_t *f = &k_faces[i];
        const char *s = f->numerals ? "0" : "H";
        fresh();
        gfx_text(&s_c, 0, CH - f->h / 2, s, f->font, GFX_RED, 1);
        CHECK(ink() > 0);
        CHECK_EQ(outside(), 0);

        fresh();
        gfx_text_in(&s_c, gfx_rect_make(0, 0, f->w, f->h),
                    f->numerals ? "0000" : "HHHH", f->font, GFX_RED, 1,
                    GFX_ALIGN_LEFT);
        CHECK(ink() > 0);
        for (int y = 0; y < ROWS; ++y) {
            for (int x = f->w; x < STRIDE; ++x) {
                CHECK_EQ(s_pixels[y * STRIDE + x], 0);
            }
        }
    }
}

/* The roles name the faces the plan gives them; LABEL, HEAD and NUM keep
 * today's faces until a screen is moved. */
TEST_CASE(font_roles_point_at_the_planned_faces)
{
    CHECK(UI_FONT_FINE == &gfx_font_8x16);
    CHECK(UI_FONT_BODY == &gfx_font_12x24);
    CHECK(UI_FONT_LABEL == &gfx_font_8x16);
    CHECK(UI_FONT_HEAD == &gfx_font_16x28);
    CHECK(UI_FONT_NUM == &gfx_font_num_24x30);
}

int main(void)
{
    RUN(new_faces_have_the_planned_cells);
    RUN(new_text_faces_list_the_german_letters_in_order);
    RUN(new_faces_hold_every_glyph);
    RUN(new_text_faces_draw_the_german_letters);
    RUN(new_faces_have_the_planned_cap_height_and_baseline);
    RUN(new_text_faces_end_descenders_inside_the_cell);
    RUN(new_faces_clip_at_the_right_edge);
    RUN(new_faces_clip_at_the_bottom_edge_and_in_a_box);
    RUN(font_roles_point_at_the_planned_faces);
    return test_summary("gfx_faces");
}
