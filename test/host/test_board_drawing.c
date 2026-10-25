/*
 * The IOmcu's drawing of its board against the panel's catalogue, and
 * on a canvas.
 *
 * The drawing under test is the one the IOmcu image carries
 * (firmware/iomcu/src/board_drawing_rp2350_can.c, generated from the
 * configurator), served by the IOmcu's own code and assembled by the
 * panel's.  The failure under test is a drawing that shows one board while
 * the picker binds another: a GPIO on the wrong pad, a pad off the grid the
 * catalogue places its keys on, a board the build does not know.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "art_fetch.h"
#include "board_drawing.h"
#include "drawing_fixture.h"
#include "gfx.h"
#include "guard_canvas.h"
#include "link_artxfer.h"
#include "link_crc.h"
#include "link_drawing.h"
#include "link_pages.h"
#include "out_bind.h"

/* firmware/iomcu/src/board_drawing_rp2350_can.c */
extern const uint8_t  board_drawing_rp2350_can[];
extern const uint16_t board_drawing_rp2350_can_width;
extern const uint16_t board_drawing_rp2350_can_height;
extern const uint32_t board_drawing_rp2350_can_bytes;
extern const uint16_t board_drawing_rp2350_can_crc;
extern const uint32_t board_drawing_rp2350_can_hash;

static uint8_t g_copy[LINK_DRAW_MAX_BYTES];

static const outbind_board_t *pico(void)
{
    return outbind_board((uint16_t)OUTBIND_BOARD_PICO_HEADER);
}

/* A copy of the served drawing in g_copy, parsed into @p d. */
static bool copy(link_drawing_t *d)
{
    memcpy(g_copy, board_drawing_rp2350_can, board_drawing_rp2350_can_bytes);
    return link_drawing_parse(d, g_copy, board_drawing_rp2350_can_bytes, NULL);
}

/* The record of pad @p pad in g_copy. */
static uint8_t *pad_record(const link_drawing_t *d, uint8_t pad)
{
    for (uint8_t i = 0; i < d->pads; ++i) {
        uint8_t *p = fx_pad_at(g_copy, i);
        if (p[0] == pad) {
            return p;
        }
    }
    return NULL;
}

TEST_CASE(the_served_drawing_is_what_the_generator_says_it_is)
{
    const uint8_t *b = board_drawing_rp2350_can;
    const uint32_t n = board_drawing_rp2350_can_bytes;
    CHECK_EQ(link_crc(0u, b, n), board_drawing_rp2350_can_crc);
    CHECK_EQ(link_fnv1a32(LINK_FNV_INIT, b, n), board_drawing_rp2350_can_hash);
    CHECK(n <= LINK_DRAW_MAX_BYTES);

    link_drawing_t d;
    const char *why = NULL;
    CHECK(link_drawing_parse(&d, b, n, &why));
    CHECK_STR_EQ(why, "");
    CHECK_EQ(d.width, board_drawing_rp2350_can_width);
    CHECK_EQ(d.height, board_drawing_rp2350_can_height);
    CHECK_EQ(d.pads, 40);
    /* Pad 1 is the square one, and no other is. */
    for (uint8_t i = 0; i < d.pads; ++i) {
        link_draw_pad_t p;
        CHECK(link_drawing_pad_at(&d, i, &p));
        CHECK_EQ(p.pad, i + 1u);
        CHECK_EQ((p.flags & LINK_DRAW_PAD_SQUARE) != 0u, p.pad == 1u);
    }
}

TEST_CASE(the_served_drawing_agrees_with_the_catalogue_pad_for_pad)
{
    /*
     * Every one of the 40 pads where outbind_pad_xy() puts it -- the
     * position the picker's keys, markers and leads are drawn at -- with
     * the catalogue's GPIO, ground or rail on it.
     */
    const outbind_board_t *board = pico();
    CHECK(board != NULL && board->shape != NULL);
    link_drawing_t d;
    CHECK(copy(&d));
    const char *why = NULL;
    CHECK(board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "");

    CHECK_EQ(d.width, board->shape->width_cmm);
    CHECK_EQ(d.height, board->shape->height_cmm);
    unsigned gpio = 0u;
    for (uint8_t pad = 1; pad <= 40u; ++pad) {
        link_draw_pad_t p;
        uint16_t x = 0u, y = 0u;
        CHECK(link_drawing_find_pad(&d, pad, &p));
        CHECK(outbind_pad_xy(board->shape, pad, &x, &y));
        CHECK_EQ(p.x, x);
        CHECK_EQ(p.y, y);
        if (p.kind == (uint8_t)LINK_DRAW_PAD_GPIO) {
            ++gpio;
        }
    }
    CHECK_EQ(gpio, board->count);
    CHECK_EQ(d.pads - gpio, board->pad_count);
}

/* Assembled the way the panel does, from the way the IOmcu serves. */
static bool dev_read(void *ctx, uint8_t page, uint8_t count, uint16_t *regs)
{
    const uint16_t *selected = ctx;
    uint16_t full[LINK_MAX_REGS];
    if (page == (uint8_t)LINK_PAGE_ARTWORK) {
        link_artserve_meta(full, (uint16_t)LINK_ART_DRAWING,
                           board_drawing_rp2350_can_width,
                           board_drawing_rp2350_can_height,
                           board_drawing_rp2350_can_bytes,
                           board_drawing_rp2350_can_crc,
                           board_drawing_rp2350_can_hash);
    } else if (page == (uint8_t)LINK_PAGE_ART_DATA) {
        link_artserve_block(full, board_drawing_rp2350_can,
                            board_drawing_rp2350_can_bytes, *selected);
    } else {
        return false;
    }
    memcpy(regs, full, sizeof(uint16_t) * count);
    return true;
}

static bool dev_write(void *ctx, uint8_t page, uint8_t reg, uint16_t value)
{
    uint16_t *selected = ctx;
    if (page != (uint8_t)LINK_PAGE_ART_DATA || reg != (uint8_t)LINK_AD_BLOCK) {
        return false;
    }
    *selected = value;
    return true;
}

TEST_CASE(the_drawing_crosses_the_link_and_is_read_as_it_was_served)
{
    uint16_t selected = 0xFFFFu;
    const art_transport_t t = { dev_read, dev_write, &selected };
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0u;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    CHECK_EQ(bytes, board_drawing_rp2350_can_bytes);
    CHECK_EQ(meta[LINK_AW_BLOCKS],
             (board_drawing_rp2350_can_bytes + LINK_AD_BYTES - 1u)
                 / LINK_AD_BYTES);

    uint8_t *buf = malloc(bytes);
    REQUIRE_ALLOC(buf);
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, buf, bytes));
    art_fetch_state_t st = ART_FETCH_RUNNING;
    for (unsigned i = 0; i < 100u && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 4u);
    }
    CHECK_EQ(st, ART_FETCH_DONE);
    CHECK_EQ(memcmp(buf, board_drawing_rp2350_can, bytes), 0);

    link_drawing_t d;
    CHECK(link_drawing_parse(&d, buf, bytes, NULL));
    CHECK(board_drawing_agrees(&d, pico(), NULL));

    /* The key it is kept under is every field of the page. */
    art_entry_t key;
    CHECK(art_fetch_key(meta, 1u, &key));
    CHECK_EQ(key.hash, board_drawing_rp2350_can_hash);
    CHECK_EQ(key.crc, board_drawing_rp2350_can_crc);
    free(buf);
}

TEST_CASE(a_board_this_build_does_not_know_gets_no_drawing)
{
    link_drawing_t d;
    CHECK(copy(&d));
    const char *why = NULL;
    CHECK(!board_drawing_agrees(&d, NULL, &why));
    CHECK_STR_EQ(why, "a board this build does not know");
    CHECK(outbind_board(0x7FFFu) == NULL);
    CHECK(!board_drawing_agrees(&d, outbind_board(0x7FFFu), NULL));

    /* A board with no pins is not one to pick on either. */
    outbind_board_t empty;
    memset(&empty, 0, sizeof(empty));
    CHECK(!board_drawing_agrees(&d, &empty, &why));
    CHECK_STR_EQ(why, "a board this build does not know");
    /* And no drawing is no drawing. */
    CHECK(!board_drawing_agrees(NULL, pico(), &why));
    CHECK(why[0] != '\0');
}

TEST_CASE(a_drawing_that_says_another_thing_than_the_catalogue_is_refused)
{
    const outbind_board_t *board = pico();
    link_drawing_t d;
    const char *why = NULL;

    /* Another GPIO on pad 2. */
    CHECK(copy(&d));
    pad_record(&d, 2u)[2] = 9u;
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "a pin's pad is not that GPIO in the drawing");

    /* A pin's pad drawn as a ground. */
    CHECK(copy(&d));
    pad_record(&d, 2u)[1] = (uint8_t)LINK_PAD_GROUND;
    pad_record(&d, 2u)[2] = 0u;
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "a pin's pad is not that GPIO in the drawing");

    /* A ground drawn as a GPIO: one GPIO pad more than the catalogue has. */
    CHECK(copy(&d));
    pad_record(&d, 3u)[1] = (uint8_t)LINK_DRAW_PAD_GPIO;
    pad_record(&d, 3u)[2] = 40u;
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "the drawing has GPIO pads the catalogue does not");

    /* The 3.3V rail at 5.0V, and a ground drawn as RUN's kind. */
    CHECK(copy(&d));
    pad_record(&d, 36u)[2] = 50u;
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "a ground or rail differs from the board's");
    CHECK(copy(&d));
    pad_record(&d, 3u)[1] = (uint8_t)LINK_PAD_OTHER;
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "a ground or rail differs from the board's");

    /* Pad 1 a hundredth of a millimetre off the grid, either axis. */
    for (int axis = 0; axis < 2; ++axis) {
        CHECK(copy(&d));
        uint8_t *p = pad_record(&d, 1u) + 4 + 2 * axis;
        fx_put16(p, (uint16_t)((p[0] | (p[1] << 8)) + 1u));
        CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
        CHECK(!board_drawing_agrees(&d, board, &why));
        CHECK_STR_EQ(why, "a pad is off the board's pad grid");
    }

    /* An outline a hundredth wider. */
    CHECK(copy(&d));
    fx_put16(g_copy + 4, (uint16_t)(board_drawing_rp2350_can_width + 1u));
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, board, &why));
    CHECK_STR_EQ(why, "the outline is not the board's shape");
}

TEST_CASE(a_board_without_a_shape_is_held_to_its_pins_and_pads_only)
{
    /*
     * A learned board that served no shape page: the pads cannot be checked
     * against a grid, the pins and pads still are.
     */
    outbind_board_t b = *pico();
    b.shape = NULL;
    link_drawing_t d;
    CHECK(copy(&d));
    CHECK(board_drawing_agrees(&d, &b, NULL));
    pad_record(&d, 1u)[4] ^= 1u;             /* off the grid: unseen */
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(board_drawing_agrees(&d, &b, NULL));
    pad_record(&d, 2u)[2] = 9u;              /* a wrong GPIO: seen */
    CHECK(link_drawing_parse(&d, g_copy, d.bytes, NULL));
    CHECK(!board_drawing_agrees(&d, &b, NULL));

    /* And one with no pad list: the pins alone. */
    b = *pico();
    b.pads = NULL;
    b.pad_count = 0u;
    CHECK(copy(&d));
    CHECK(board_drawing_agrees(&d, &b, NULL));
}

TEST_CASE(a_length_is_scaled_and_rounded_from_its_own_value)
{
    CHECK_EQ(board_drawing_scale(5100u, 500, 5100u), 500);
    CHECK_EQ(board_drawing_scale(0u, 500, 5100u), 0);
    CHECK_EQ(board_drawing_scale(2550u, 500, 5100u), 250);
    /* 137 of 5100 at 500px is 13.43: 13.  At 613px it is 16.47: 16. */
    CHECK_EQ(board_drawing_scale(137u, 500, 5100u), 13);
    CHECK_EQ(board_drawing_scale(137u, 613, 5100u), 16);
    /* A half rounds up: 51 of 5100 at 50px is 0.5. */
    CHECK_EQ(board_drawing_scale(51u, 50, 5100u), 1);
    CHECK_EQ(board_drawing_scale(50u, 50, 5100u), 0);
    /* At the 16-bit limit nothing wraps. */
    CHECK_EQ(board_drawing_scale(65535u, 1024, 65535u), 1024);
    CHECK_EQ(board_drawing_scale(65535u, 32767, 65535u), 32767);
    /* No outline and no pixels are 0, not a division by zero. */
    CHECK_EQ(board_drawing_scale(100u, 500, 0u), 0);
    CHECK_EQ(board_drawing_scale(100u, 0, 5100u), 0);
    CHECK_EQ(board_drawing_scale(100u, -5, 5100u), 0);

    /*
     * Forty pads at 0.1 inch scaled each from its own position: the last
     * one is where 4963 hundredths put it, not where 39 rounded pitches
     * would (39 x 25 = 975 at 500px against 973).
     */
    CHECK_EQ(board_drawing_scale(4963u, 500, 5100u), 487);
}

#define CW 500
#define CH 206

static gfx_color_t pixel_at(const gfx_canvas_t *c, int x, int y)
{
    return c->pixels[(size_t)y * (size_t)c->stride + (size_t)x];
}

TEST_CASE(the_drawing_is_drawn_inside_its_box_with_every_pad_ring)
{
    static gfx_color_t buf[GUARD_CANVAS_PIXELS(CW, CH)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, buf, CW, CH);
    gfx_clear(&c, 0x0000u);

    link_drawing_t d;
    CHECK(copy(&d));
    const gfx_color_t hole = 0x0841u;
    board_drawing_render(&c, &d, 0, 0, CW, CH, hole);
    CHECK_EQ(guard_canvas_touched(&g), 0);

    /* Every pad: its hole at its centre, its ring beside it. */
    for (uint8_t i = 0; i < d.pads; ++i) {
        link_draw_pad_t p;
        CHECK(link_drawing_pad_at(&d, i, &p));
        const int x = board_drawing_scale(p.x, CW, d.width);
        const int y = board_drawing_scale(p.y, CH, d.height);
        CHECK_EQ(pixel_at(&c, x, y), hole);
        CHECK_EQ(pixel_at(&c, x + 5, y), d.ring);
        /* Pad 1's ring is square: its corner is ring, a round one's not. */
        const int r = board_drawing_scale(d.ring_r, CW, d.width);
        CHECK_EQ(pixel_at(&c, x + r, y + r) == d.ring, p.pad == 1u);
    }
    /* The board colour where nothing is drawn over it: the middle of the
     * left end, between USB-C's body and the rows. */
    CHECK_EQ(pixel_at(&c, 2, CH / 2 - 40), d.colour);
    /* And the outline's rounded corner leaves the canvas behind it. */
    CHECK_EQ(pixel_at(&c, 0, 0), 0x0000u);

    /* Drawn into a box anywhere on the canvas, it moves whole. */
    gfx_clear(&c, 0x0000u);
    board_drawing_render(&c, &d, 0, 0, 250, 103, hole);
    CHECK_EQ(guard_canvas_touched(&g), 0);
    CHECK_EQ(pixel_at(&c, 300, 50), 0x0000u);
}

TEST_CASE(a_box_too_small_or_nothing_to_draw_writes_nothing_off_it)
{
    static gfx_color_t buf[GUARD_CANVAS_PIXELS(CW, CH)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, buf, CW, CH);
    gfx_clear(&c, 0x0000u);
    link_drawing_t d;
    CHECK(copy(&d));

    /* A 1 by 1 box, a box half off every edge, no box at all. */
    board_drawing_render(&c, &d, 0, 0, 1, 1, 0xFFFFu);
    board_drawing_render(&c, &d, -250, -100, CW, CH, 0xFFFFu);
    board_drawing_render(&c, &d, 250, 100, CW, CH, 0xFFFFu);
    board_drawing_render(&c, &d, 0, 0, 0, CH, 0xFFFFu);
    board_drawing_render(&c, &d, 0, 0, CW, -1, 0xFFFFu);
    board_drawing_render(&c, NULL, 0, 0, CW, CH, 0xFFFFu);
    board_drawing_render(NULL, &d, 0, 0, CW, CH, 0xFFFFu);
    link_drawing_t none;
    memset(&none, 0, sizeof(none));
    board_drawing_render(&c, &none, 0, 0, CW, CH, 0xFFFFu);
    CHECK_EQ(guard_canvas_touched(&g), 0);
}

int main(void)
{
    RUN(the_served_drawing_is_what_the_generator_says_it_is);
    RUN(the_served_drawing_agrees_with_the_catalogue_pad_for_pad);
    RUN(the_drawing_crosses_the_link_and_is_read_as_it_was_served);
    RUN(a_board_this_build_does_not_know_gets_no_drawing);
    RUN(a_drawing_that_says_another_thing_than_the_catalogue_is_refused);
    RUN(a_board_without_a_shape_is_held_to_its_pins_and_pads_only);
    RUN(a_length_is_scaled_and_rounded_from_its_own_value);
    RUN(the_drawing_is_drawn_inside_its_box_with_every_pad_ring);
    RUN(a_box_too_small_or_nothing_to_draw_writes_nothing_off_it);
    return test_summary("board_drawing");
}
