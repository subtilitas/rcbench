/*
 * Reading a board drawing (LINK_ART_DRAWING) off bytes somebody else wrote.
 *
 * The failure under test is a drawing taken in part: a pad record read past
 * the end of a truncated payload, a shape drawn off the board, a pad number
 * that matches nothing.  Each case spoils one field of a valid drawing and
 * expects the whole of it refused with a reason, and the description left
 * zeroed so nothing reads through it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "drawing_fixture.h"
#include "link_drawing.h"
#include "link_pages.h"

static uint8_t g_buf[LINK_DRAW_MAX_BYTES + 16u];

/* A valid small drawing in g_buf; its length. */
static uint32_t fresh(void)
{
    memset(g_buf, 0, sizeof(g_buf));
    return fx_draw_small(g_buf);
}

/* Parses false, leaves the description zeroed and names a reason. */
static int refused(uint32_t bytes)
{
    link_drawing_t d;
    memset(&d, 0xA5, sizeof(d));
    const char *why = NULL;
    const bool ok = link_drawing_parse(&d, g_buf, bytes, &why);
    link_drawing_t zero;
    memset(&zero, 0, sizeof(zero));
    return !ok && memcmp(&d, &zero, sizeof(d)) == 0 && why != NULL
           && why[0] != '\0';
}

TEST_CASE(a_valid_drawing_is_read_field_for_field)
{
    const uint32_t n = fresh();
    link_drawing_t d;
    const char *why = "unset";
    CHECK(link_drawing_parse(&d, g_buf, n, &why));
    CHECK_STR_EQ(why, "");
    CHECK(d.data == g_buf);
    CHECK_EQ(d.bytes, n);
    CHECK_EQ(d.pads, FX_SMALL_PADS);
    CHECK_EQ(d.shapes, FX_SMALL_SHAPES);
    CHECK_EQ(d.width, FX_SMALL_W);
    CHECK_EQ(d.height, FX_SMALL_H);
    CHECK_EQ(d.colour, 0x1A52);
    CHECK_EQ(d.ring, 0xCE79);
    CHECK_EQ(d.ring_r, 40);
    CHECK_EQ(d.hole_r, 20);

    link_draw_pad_t p;
    CHECK(link_drawing_pad_at(&d, 0, &p));
    CHECK_EQ(p.pad, 1);
    CHECK_EQ(p.kind, LINK_DRAW_PAD_GPIO);
    CHECK_EQ(p.flags, LINK_DRAW_PAD_SQUARE);
    CHECK_EQ(p.x, 100);
    CHECK_EQ(p.y, 100);
    CHECK(link_drawing_find_pad(&d, 3u, &p));
    CHECK_EQ(p.kind, LINK_PAD_POWER);
    CHECK_EQ(p.value, 33);
    CHECK_EQ(p.y, 400);
    CHECK(!link_drawing_find_pad(&d, 5u, &p));
    CHECK(!link_drawing_pad_at(&d, FX_SMALL_PADS, &p));

    link_draw_shape_t s;
    CHECK(link_drawing_shape_at(&d, 1, &s));
    CHECK_EQ(s.kind, LINK_DRAW_RRECT);
    CHECK_EQ(s.param, 20);
    CHECK_EQ(s.a, 750);
    CHECK_EQ(s.c, 200);
    CHECK(link_drawing_shape_at(&d, 3, &s));
    CHECK_EQ(s.kind, LINK_DRAW_LINE);
    CHECK_EQ(s.d, 250);
    CHECK(!link_drawing_shape_at(&d, FX_SMALL_SHAPES, &s));
}

TEST_CASE(a_truncated_or_padded_drawing_is_refused)
{
    /* Every length short of the whole, and one byte over it. */
    const uint32_t n = fresh();
    for (uint32_t len = 0; len < n; ++len) {
        CHECK(refused(len));
    }
    CHECK(refused(n + 1u));

    /* Counts that ask for more records than the bytes hold. */
    fresh();
    g_buf[1] = (uint8_t)(FX_SMALL_PADS + 1u);
    CHECK(refused(n));
    fresh();
    fx_put16(g_buf + 2, (uint16_t)(FX_SMALL_SHAPES + 1u));
    CHECK(refused(n));
    /* And fewer. */
    fresh();
    fx_put16(g_buf + 2, (uint16_t)(FX_SMALL_SHAPES - 1u));
    CHECK(refused(n));

    /* No data at all. */
    link_drawing_t d;
    CHECK(!link_drawing_parse(&d, NULL, n, NULL));
    CHECK(!link_drawing_parse(NULL, g_buf, n, NULL));
}

TEST_CASE(a_header_out_of_its_ranges_is_refused)
{
    const uint32_t n = fresh();
    g_buf[0] = 2u;                            /* a later version */
    CHECK(refused(n));
    fresh();
    g_buf[15] = 1u;                           /* the reserved byte */
    CHECK(refused(n));
    fresh();
    fx_put16(g_buf + 4, 0u);                  /* no width */
    CHECK(refused(n));
    fresh();
    fx_put16(g_buf + 6, 0u);                  /* no height */
    CHECK(refused(n));
    fresh();
    g_buf[13] = 0u;                           /* no ring */
    CHECK(refused(n));
    fresh();
    g_buf[14] = g_buf[13];                    /* a hole as large as its ring */
    CHECK(refused(n));

    /* The outline corner at half the shorter side is a corner; one more is
     * not. */
    fresh();
    g_buf[12] = 250u;
    link_drawing_t d;
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));
    fresh();
    fx_put16(g_buf + 6, 499u);                /* the rings still fit */
    g_buf[12] = 250u;
    CHECK(refused(n));
}

TEST_CASE(the_pad_and_shape_counts_stop_at_their_limits)
{
    /*
     * LINK_DRAW_MAX_PADS pads and LINK_DRAW_MAX_SHAPES shapes is the
     * largest drawing, LINK_DRAW_MAX_BYTES long; one more of either is
     * refused even when the bytes are there for it.
     */
    memset(g_buf, 0, sizeof(g_buf));
    const uint16_t w = 60000u, h = 1000u;
    fx_draw_head(g_buf, (uint8_t)LINK_DRAW_MAX_PADS,
                 (uint16_t)LINK_DRAW_MAX_SHAPES, w, h);
    for (unsigned i = 0; i < LINK_DRAW_MAX_PADS; ++i) {
        /* Pads 1 to 63 exist; the 64th record has nowhere to go. */
        fx_draw_pad(g_buf, i, (uint8_t)(i + 1u), (uint8_t)LINK_PAD_OTHER, 0u,
                    0u, (uint16_t)(100u + i * 200u), 500u);
    }
    for (unsigned i = 0; i < LINK_DRAW_MAX_SHAPES; ++i) {
        fx_draw_shape(g_buf, LINK_DRAW_MAX_PADS, i, (uint8_t)LINK_DRAW_RECT,
                      0u, 0u, (uint16_t)(i * 100u), 0u, 100u, 100u);
    }
    /* 64 pads cannot all be numbered 1 to 63. */
    CHECK(refused(LINK_DRAW_MAX_BYTES));

    /* 63 pads and 400 shapes are taken. */
    g_buf[1] = 63u;
    memmove(fx_shape_at(g_buf, 63u, 0), fx_shape_at(g_buf, 64u, 0),
            LINK_DRAW_MAX_SHAPES * LINK_DRAW_SHAPE_BYTES);
    const uint32_t big = LINK_DRAW_MAX_BYTES - LINK_DRAW_PAD_BYTES;
    link_drawing_t d;
    const char *why = NULL;
    CHECK(link_drawing_parse(&d, g_buf, big, &why));
    CHECK_STR_EQ(why, "");
    CHECK_EQ(d.pads, 63);
    CHECK_EQ(d.shapes, LINK_DRAW_MAX_SHAPES);

    /* 401 shapes, the bytes present: refused. */
    fx_put16(g_buf + 2, (uint16_t)(LINK_DRAW_MAX_SHAPES + 1u));
    fx_draw_shape(g_buf, 63u, LINK_DRAW_MAX_SHAPES, (uint8_t)LINK_DRAW_RECT,
                  0u, 0u, 0u, 0u, 100u, 100u);
    CHECK(refused(big + LINK_DRAW_SHAPE_BYTES));

    /* No shapes is a drawing: the outline and its pads. */
    const uint32_t n = fresh();
    fx_put16(g_buf + 2, 0u);
    CHECK(link_drawing_parse(&d, g_buf, n - FX_SMALL_SHAPES
                                            * LINK_DRAW_SHAPE_BYTES, NULL));
    /* No pads is not: there is nothing to pick. */
    fresh();
    g_buf[1] = 0u;
    CHECK(refused(n - FX_SMALL_PADS * LINK_DRAW_PAD_BYTES));
}

TEST_CASE(a_pad_out_of_place_or_of_no_kind_is_refused)
{
    const uint32_t n = fresh();
    uint8_t *p = fx_pad_at(g_buf, 1);

    p[0] = 0u;                                /* pad 0 */
    CHECK(refused(n));
    fresh();
    p[0] = 64u;                               /* past 63 */
    CHECK(refused(n));
    fresh();
    p[0] = 1u;                                /* the same as the one before */
    CHECK(refused(n));
    fresh();
    fx_pad_at(g_buf, 2)[0] = 2u;              /* out of ascending order */
    CHECK(refused(n));
    fresh();
    p[1] = 0u;                                /* kind 0 */
    CHECK(refused(n));
    fresh();
    p[1] = 5u;                                /* a kind past GPIO */
    CHECK(refused(n));
    fresh();
    p[2] = 1u;                                /* a ground with a number */
    CHECK(refused(n));
    fresh();
    fx_pad_at(g_buf, 3)[2] = 1u;              /* an OTHER pad with one */
    CHECK(refused(n));
    fresh();
    p[3] = 0x02u;                             /* a flag nobody defined */
    CHECK(refused(n));

    /* A ring touching the edge is inside; a hundredth further is not, on
     * each of the four sides. */
    const uint16_t edge[4][2] = {
        { 40u, 250u }, { FX_SMALL_W - 40u, 250u },
        { 500u, 40u }, { 500u, FX_SMALL_H - 40u },
    };
    for (int side = 0; side < 4; ++side) {
        fresh();
        fx_put16(p + 4, edge[side][0]);
        fx_put16(p + 6, edge[side][1]);
        link_drawing_t d;
        CHECK(link_drawing_parse(&d, g_buf, n, NULL));
        fresh();
        fx_put16(p + 4, (uint16_t)(edge[side][0] + ((side == 1) ? 1 : 0)
                                   - ((side == 0) ? 1 : 0)));
        fx_put16(p + 6, (uint16_t)(edge[side][1] + ((side == 3) ? 1 : 0)
                                   - ((side == 2) ? 1 : 0)));
        CHECK(refused(n));
    }
    /* A centre at 0 does not wrap to a large x. */
    fresh();
    fx_put16(p + 4, 0u);
    CHECK(refused(n));
    fresh();
    fx_put16(p + 4, 0xFFFFu);
    CHECK(refused(n));
}

TEST_CASE(a_shape_off_the_board_or_of_no_kind_is_refused)
{
    const uint32_t n = fresh();
    uint8_t *rect  = fx_shape_at(g_buf, FX_SMALL_PADS, 0);
    uint8_t *rrect = fx_shape_at(g_buf, FX_SMALL_PADS, 1);
    uint8_t *disc  = fx_shape_at(g_buf, FX_SMALL_PADS, 2);
    uint8_t *line  = fx_shape_at(g_buf, FX_SMALL_PADS, 3);
    link_drawing_t d;

    rect[0] = 0u;                             /* kind 0 */
    CHECK(refused(n));
    fresh();
    rect[0] = 5u;                             /* a kind past LINE */
    CHECK(refused(n));
    fresh();
    rect[1] = 1u;                             /* a plain RECT with a corner */
    CHECK(refused(n));
    fresh();
    fx_put16(rect + 8, 0u);                   /* no width */
    CHECK(refused(n));
    fresh();
    fx_put16(rect + 10, 0u);                  /* no height */
    CHECK(refused(n));
    fresh();
    fx_put16(rect + 4, 801u);                 /* 801 + 200 is past 1000 */
    CHECK(refused(n));
    fresh();
    fx_put16(rect + 4, 800u);                 /* and 800 + 200 is the edge */
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));
    fresh();
    fx_put16(rect + 4, 0xFFFFu);              /* a + c wraps in 16 bits */
    fx_put16(rect + 8, 2u);
    CHECK(refused(n));

    /* An RRECT whose corner is half its shorter side, and one more; on an
     * odd side, where twice the corner passes it by one. */
    fresh();
    rrect[1] = 50u;
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));
    rrect[1] = 51u;
    CHECK(refused(n));
    fresh();
    fx_put16(rrect + 10, 101u);
    rrect[1] = 50u;
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));
    rrect[1] = 51u;
    CHECK(refused(n));

    fresh();
    disc[1] = 1u;                             /* a DISC with a parameter */
    CHECK(refused(n));
    fresh();
    fx_put16(disc + 8, 0u);                   /* no radius */
    CHECK(refused(n));
    fresh();
    fx_put16(disc + 10, 1u);                  /* d not 0 */
    CHECK(refused(n));
    fresh();
    fx_put16(disc + 8, 151u);                 /* 850 + 151 is past 1000 */
    CHECK(refused(n));
    fresh();
    fx_put16(disc + 8, 150u);
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));

    fresh();
    line[1] = 0u;                             /* a LINE of no width */
    CHECK(refused(n));
    fresh();
    fx_put16(line + 4, 14u);                  /* half its width past x 0 */
    CHECK(refused(n));
    fresh();
    fx_put16(line + 4, 15u);
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));
    /* An odd width's half rounds up: 31 wide reaches 16 either side. */
    fresh();
    line[1] = 31u;
    fx_put16(line + 4, 15u);
    CHECK(refused(n));
    fx_put16(line + 4, 16u);
    CHECK(link_drawing_parse(&d, g_buf, n, NULL));
    fresh();
    fx_put16(line + 10, FX_SMALL_H - 14u);    /* its end past the bottom */
    CHECK(refused(n));
}

TEST_CASE(why_is_never_left_pointing_at_nothing)
{
    const uint32_t n = fresh();
    link_drawing_t d;
    const char *why = NULL;
    CHECK(link_drawing_parse(&d, g_buf, n, &why));
    CHECK(why != NULL);
    CHECK(!link_drawing_parse(&d, g_buf, 3u, &why));
    CHECK(why != NULL && why[0] != '\0');
    CHECK(!link_drawing_parse(&d, g_buf, 3u, NULL));

    link_draw_pad_t p;
    link_draw_shape_t s;
    CHECK(!link_drawing_pad_at(NULL, 0, &p));
    CHECK(!link_drawing_pad_at(&d, 0, NULL));
    CHECK(!link_drawing_find_pad(NULL, 1u, &p));
    CHECK(!link_drawing_shape_at(NULL, 0, &s));
    CHECK(!link_drawing_shape_at(&d, 0, NULL));
    /* A refused drawing has nothing to hand out. */
    CHECK(!link_drawing_pad_at(&d, 0, &p));
    CHECK(!link_drawing_shape_at(&d, 0, &s));
}

int main(void)
{
    RUN(a_valid_drawing_is_read_field_for_field);
    RUN(a_truncated_or_padded_drawing_is_refused);
    RUN(a_header_out_of_its_ranges_is_refused);
    RUN(the_pad_and_shape_counts_stop_at_their_limits);
    RUN(a_pad_out_of_place_or_of_no_kind_is_refused);
    RUN(a_shape_off_the_board_or_of_no_kind_is_refused);
    RUN(why_is_never_left_pointing_at_nothing);
    return test_summary("link_drawing");
}
