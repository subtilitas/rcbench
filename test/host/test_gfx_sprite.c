/**
 * @file test_gfx_sprite.c
 * @brief gfx_sprite_draw(): each kind of run, the blend at every alpha and
 *        both extremes of the destination without a channel carrying into
 *        the next, clipping to the canvas and to a clip against an
 *        unclipped reference, the damage record and the fit check's box,
 *        the work counted, and a short, long or damaged table refused
 *        without a read past its end.  Every generated cat reads to its
 *        last byte.
 *
 * Cases:
 * - each_run_kind_draws_what_it_says
 * - the_blend_never_carries_between_channels
 * - a_clipped_draw_is_the_unclipped_one_cut
 * - the_damage_record_holds_the_drawn_box
 * - a_bad_table_is_refused_without_a_read_past_it
 * - every_cat_reads_to_its_last_byte
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "gfx_damage.h"
#include "gfx_sprite.h"
#include "gfx_work.h"
#include "ui_cats.h"

#define W 64
#define H 48

static gfx_color_t s_a[W * H];
static gfx_color_t s_b[W * H];

/** @brief A canvas over @p px, W x H, filled with @p bg. */
static gfx_canvas_t canvas(gfx_color_t *px, gfx_color_t bg)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, px, W, H, 0);
    gfx_clear(&c, bg);
    return c;
}

/** @brief A run's first byte. */
#define RLE(kind, n) ((uint8_t)(((kind) << 6) | ((n) - 1)))

/*
 * One 6 x 2 sprite: row 0 two transparent, then a FILL of 2 and a COPY of
 * 2; row 1 a BLEND of 3 (alphas 8, 1 and 14) and a SKIP of 3.
 */
static const uint8_t k_six[] = {
    RLE(GFX_RUN_SKIP, 2),
    RLE(GFX_RUN_FILL, 2), 0x1F, 0x00,                 /* blue */
    RLE(GFX_RUN_COPY, 2), 0x00, 0xF8, 0xE0, 0x07,     /* red, green */
    RLE(GFX_RUN_BLEND, 3), 0x18, 0x0E,                /* 8, 1 | 14 */
    0x00, 0x00, 0x00, 0x00, 0x7D, 0xEF,               /* black, black, white
                                                         at 14 of 15 */
    RLE(GFX_RUN_SKIP, 3),
};

TEST_CASE(each_run_kind_draws_what_it_says)
{
    const gfx_sprite_t sp = { 6, 2, sizeof(k_six), k_six };
    gfx_canvas_t c = canvas(s_a, GFX_WHITE);
    const unsigned long w0 = gfx_work();
    CHECK(gfx_sprite_draw(&c, 10, 20, &sp));
    /* Visits: 4 opaque and 3 blended pixels; the skips read nothing. */
    CHECK_EQ(gfx_work() - w0, 7ul);
    const gfx_color_t *r0 = &s_a[20 * W + 10];
    const gfx_color_t *r1 = &s_a[21 * W + 10];
    CHECK_EQ(r0[0], GFX_WHITE);
    CHECK_EQ(r0[1], GFX_WHITE);
    CHECK_EQ(r0[2], 0x001F);
    CHECK_EQ(r0[3], 0x001F);
    CHECK_EQ(r0[4], 0xF800);
    CHECK_EQ(r0[5], 0x07E0);
    /* Black at alpha 8 over white: white kept at floor(32 x 7 / 15) = 14
     * 32nds, 31 x 14 / 32 = 13 in red and blue, 63 x 14 / 32 = 27 in
     * green. */
    CHECK_EQ(r1[0], (13u << 11) | (27u << 5) | 13u);
    /* Alpha 1: 29 32nds kept, 28 and 57. */
    CHECK_EQ(r1[1], (28u << 11) | (57u << 5) | 28u);
    /* White at alpha 14, premultiplied to 29, 59, 29, over white with 2
     * 32nds kept: 1 and 3 added, 30, 62, 30. */
    CHECK_EQ(r1[2], (30u << 11) | (62u << 5) | 30u);
    CHECK_EQ(r1[3], GFX_WHITE);
    CHECK_EQ(r1[4], GFX_WHITE);
    CHECK_EQ(r1[5], GFX_WHITE);
    /* Nothing outside the box. */
    CHECK_EQ(s_a[19 * W + 12], GFX_WHITE);
    CHECK_EQ(s_a[20 * W + 16], GFX_WHITE);
    CHECK_EQ(s_a[22 * W + 10], GFX_WHITE);
}

/*
 * Every alpha 1 to 14, the brightest source the generator stores for it
 * (each channel round(max x a / 15)) over the brightest destination and
 * over black, and the darkest source over white: no channel exceeds its
 * maximum, so none carries into the next, and the result is within one
 * step of the exact blend.
 */
TEST_CASE(the_blend_never_carries_between_channels)
{
    for (unsigned a = 1; a <= 14; ++a) {
        const unsigned r = (31u * a * 2u + 15u) / 30u;
        const unsigned g = (63u * a * 2u + 15u) / 30u;
        const gfx_color_t src = (gfx_color_t)((r << 11) | (g << 5) | r);
        const gfx_color_t dsts[3] = { GFX_WHITE, GFX_BLACK, 0x7BEF };
        const gfx_color_t srcs[2] = { src, 0x0000 };
        for (int si = 0; si < 2; ++si) {
            for (int di = 0; di < 3; ++di) {
                const uint8_t t[4] = { RLE(GFX_RUN_BLEND, 1), (uint8_t)a,
                                       (uint8_t)(srcs[si] & 0xFF),
                                       (uint8_t)(srcs[si] >> 8) };
                const gfx_sprite_t sp = { 1, 1, sizeof(t), t };
                gfx_canvas_t c = canvas(s_a, dsts[di]);
                CHECK(gfx_sprite_draw(&c, 0, 0, &sp));
                const unsigned o = s_a[0], d = dsts[di], s = srcs[si];
                const unsigned fields[3][2] = { { 11, 31 }, { 5, 63 },
                                                { 0, 31 } };
                for (int f = 0; f < 3; ++f) {
                    const unsigned sh = fields[f][0], mx = fields[f][1];
                    const unsigned ov = (o >> sh) & mx;
                    const double want = (double)((s >> sh) & mx)
                        + (double)((d >> sh) & mx) * (15.0 - a) / 15.0;
                    CHECK(ov <= mx);
                    CHECK((double)ov <= want + 0.5);
                    CHECK((double)ov >= want - 2.5);
                }
            }
        }
    }
}

/*
 * A generated cat drawn at every offset around the canvas edges and
 * through a clip: inside the clip every pixel is the unclipped draw's,
 * outside it every pixel is the background.
 */
TEST_CASE(a_clipped_draw_is_the_unclipped_one_cut)
{
    static gfx_color_t big[400 * 300];
    const gfx_sprite_t *sp = &ui_cat_poses[0].art;
    const int offs[][2] = { { -50, -40 }, { 30, -100 }, { -150, 10 },
                            { 40, 20 }, { -170, -170 }, { 63, 47 } };
    for (unsigned k = 0; k < sizeof(offs) / sizeof(offs[0]); ++k) {
        for (int clip = 0; clip < 2; ++clip) {
            /* The reference: the whole sprite on a canvas 200px larger
             * on each side than W x H. */
            gfx_canvas_t ref;
            gfx_canvas_init(&ref, big, 400, 300, 0);
            gfx_clear(&ref, GFX_GREY(40));
            CHECK(gfx_sprite_draw(&ref, offs[k][0] + 200, offs[k][1] + 200,
                                  sp));
            gfx_canvas_t c = canvas(s_a, GFX_GREY(40));
            const gfx_rect_t cl = clip ? gfx_rect_make(5, 7, 31, 23)
                                       : gfx_rect_make(0, 0, W, H);
            CHECK(gfx_clip_set(&c, cl));
            CHECK(gfx_sprite_draw(&c, offs[k][0], offs[k][1], sp));
            unsigned bad = 0u;
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const gfx_color_t want = gfx_rect_contains(cl, x, y)
                        ? big[(y + 200) * 400 + (x + 200)] : GFX_GREY(40);
                    bad += (s_a[y * W + x] != want) ? 1u : 0u;
                }
            }
            CHECK_EQ(bad, 0u);
        }
    }
    /* Wholly outside: nothing drawn, the table still read whole. */
    gfx_canvas_t c = canvas(s_a, GFX_GREY(40));
    memcpy(s_b, s_a, sizeof(s_b));
    CHECK(gfx_sprite_draw(&c, W, 0, sp));
    CHECK(gfx_sprite_draw(&c, -sp->w, 0, sp));
    CHECK(gfx_sprite_draw(&c, 0, -sp->h, sp));
    CHECK(memcmp(s_a, s_b, sizeof(s_a)) == 0);
}

/* The damage record holds the clipped box, and nothing for a draw wholly
 * outside. */
TEST_CASE(the_damage_record_holds_the_drawn_box)
{
    static gfx_damage_t d;
    gfx_canvas_t c = canvas(s_a, GFX_BLACK);
    gfx_damage_init(&d, &c);
    gfx_damage_attach(&d);
    gfx_damage_reset(&d);
    const gfx_sprite_t *sp = &ui_cat_poses[0].art;
    CHECK(gfx_sprite_draw(&c, W + 1, 0, sp));
    CHECK(gfx_damage_empty(&d));
    CHECK(gfx_sprite_draw(&c, 20, 16, sp));
    CHECK(!gfx_damage_empty(&d));
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    const int n = gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS);
    CHECK(n >= 1);
    int x0 = W, x1 = 0, y0 = H, y1 = 0;
    for (int i = 0; i < n; ++i) {
        x0 = (r[i].x < x0) ? r[i].x : x0;
        y0 = (r[i].y < y0) ? r[i].y : y0;
        x1 = (r[i].x + r[i].w > x1) ? r[i].x + r[i].w : x1;
        y1 = (r[i].y + r[i].h > y1) ? r[i].y + r[i].h : y1;
    }
    CHECK(x0 <= 20);
    CHECK(y0 <= 16);
    CHECK_EQ(x1, W);
    CHECK_EQ(y1, H);
    gfx_damage_attach(NULL);
}

/*
 * NULL, an empty sprite, a table cut short at every length, one with a
 * byte past its last row, and a run across the end of its row: each is
 * refused, and a table cut short draws nothing it did not read.
 */
TEST_CASE(a_bad_table_is_refused_without_a_read_past_it)
{
    gfx_canvas_t c = canvas(s_a, GFX_BLACK);
    CHECK(!gfx_sprite_draw(&c, 0, 0, NULL));
    const gfx_sprite_t empty = { 0, 2, sizeof(k_six), k_six };
    CHECK(!gfx_sprite_draw(&c, 0, 0, &empty));
    const gfx_sprite_t none = { 6, 2, sizeof(k_six), NULL };
    CHECK(!gfx_sprite_draw(&c, 0, 0, &none));
    /* Cut short at every length: copied to the end of a buffer of exactly
     * that length, so a read past it would be past the copy. */
    for (uint32_t n = 0; n < sizeof(k_six); ++n) {
        uint8_t cut[sizeof(k_six)];
        memcpy(cut, k_six, n);
        const gfx_sprite_t sp = { 6, 2, n, cut };
        CHECK(!gfx_sprite_draw(&c, 0, 0, &sp));
    }
    uint8_t longer[sizeof(k_six) + 1];
    memcpy(longer, k_six, sizeof(k_six));
    longer[sizeof(k_six)] = 0;
    const gfx_sprite_t extra = { 6, 2, sizeof(longer), longer };
    CHECK(!gfx_sprite_draw(&c, 0, 0, &extra));
    /* A SKIP of 7 in a row of 6. */
    const uint8_t across[] = { RLE(GFX_RUN_SKIP, 7) };
    const gfx_sprite_t wide = { 6, 1, sizeof(across), across };
    CHECK(!gfx_sprite_draw(&c, 0, 0, &wide));
    /* The longest run, 64 pixels, fills a row of 64. */
    const uint8_t full[] = { RLE(GFX_RUN_FILL, GFX_RUN_MAX), 0x1F, 0x00 };
    const gfx_sprite_t row = { 64, 1, sizeof(full), full };
    CHECK(gfx_sprite_draw(&c, 0, 0, &row));
    CHECK_EQ(s_a[0], 0x001F);
    CHECK_EQ(s_a[63], 0x001F);
    const gfx_rect_t box = gfx_sprite_rect(&row, 3, 4);
    CHECK_EQ(box.w, 64);
    CHECK_EQ(box.h, 1);
    CHECK(gfx_rect_empty(gfx_sprite_rect(NULL, 3, 4)));
}

/* Every generated pose reads to its last byte, drawn on a canvas or with
 * none. */
TEST_CASE(every_cat_reads_to_its_last_byte)
{
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        const gfx_sprite_t *sp = &ui_cat_poses[p].art;
        CHECK(sp->w > 0 && sp->h > 0 && sp->bytes > 0u);
        gfx_canvas_t c = canvas(s_a, GFX_BLACK);
        CHECK(gfx_sprite_draw(&c, 0, 0, sp));
        CHECK(gfx_sprite_draw(NULL, 0, 0, sp));
        CHECK(ui_cat_poses[p].spots != 0u);
        CHECK(ui_cat_poses[p].name != NULL);
    }
}

int main(void)
{
    RUN(each_run_kind_draws_what_it_says);
    RUN(the_blend_never_carries_between_channels);
    RUN(a_clipped_draw_is_the_unclipped_one_cut);
    RUN(the_damage_record_holds_the_drawn_box);
    RUN(a_bad_table_is_refused_without_a_read_past_it);
    RUN(every_cat_reads_to_its_last_byte);
    return test_summary("gfx_sprite");
}
