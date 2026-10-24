/*
 * Host unit tests for touch coordinate mapping and event tracking.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <string.h>

#include "gt911_emu.h"
#include "touch_map.h"
#include "touch_merge.h"

static touch_map_t map_for(touch_rotation_t rot, bool mx, bool my)
{
    touch_map_t m = {
        .src_w = 800,
        .src_h = 480,
        .mirror_x = mx,
        .mirror_y = my,
        .rotation = rot,
    };
    return m;
}

static void expect(const touch_map_t *m, int in_x, int in_y, int want_x, int want_y)
{
    int16_t ox = 0, oy = 0;
    touch_map_apply(m, (int16_t)in_x, (int16_t)in_y, &ox, &oy);
    if (ox != want_x || oy != want_y) {
        T_FAIL("(%d,%d) -> (%d,%d), want (%d,%d)", in_x, in_y, ox, oy, want_x, want_y);
    }
}

TEST_CASE(identity_mapping)
{
    touch_map_t m = map_for(TOUCH_ROTATION_0, false, false);
    expect(&m, 0, 0, 0, 0);
    expect(&m, 799, 479, 799, 479);
    expect(&m, 400, 240, 400, 240);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 800);
    CHECK_EQ(h, 480);
}

TEST_CASE(rotation_90_swaps_axes)
{
    touch_map_t m = map_for(TOUCH_ROTATION_90, false, false);
    /* Native top-left ends up at the top-right of the rotated space. */
    expect(&m, 0, 0, 479, 0);
    expect(&m, 799, 0, 479, 799);
    expect(&m, 0, 479, 0, 0);
    expect(&m, 799, 479, 0, 799);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 480);
    CHECK_EQ(h, 800);
}

TEST_CASE(rotation_180_flips_both)
{
    touch_map_t m = map_for(TOUCH_ROTATION_180, false, false);
    expect(&m, 0, 0, 799, 479);
    expect(&m, 799, 479, 0, 0);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 800);
    CHECK_EQ(h, 480);
}

TEST_CASE(rotation_270_swaps_the_other_way)
{
    touch_map_t m = map_for(TOUCH_ROTATION_270, false, false);
    expect(&m, 0, 0, 0, 799);
    expect(&m, 799, 0, 0, 0);
    expect(&m, 0, 479, 479, 799);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 480);
    CHECK_EQ(h, 800);
}

TEST_CASE(mirrors_apply_before_rotation)
{
    touch_map_t mx = map_for(TOUCH_ROTATION_0, true, false);
    expect(&mx, 0, 100, 799, 100);
    expect(&mx, 799, 100, 0, 100);

    touch_map_t my = map_for(TOUCH_ROTATION_0, false, true);
    expect(&my, 100, 0, 100, 479);

    /* Both mirrors together are the same as a 180 rotation. */
    touch_map_t both = map_for(TOUCH_ROTATION_0, true, true);
    touch_map_t rot = map_for(TOUCH_ROTATION_180, false, false);
    for (int i = 0; i < 800; i += 137) {
        for (int j = 0; j < 480; j += 91) {
            int16_t ax, ay, bx, by;
            touch_map_apply(&both, (int16_t)i, (int16_t)j, &ax, &ay);
            touch_map_apply(&rot, (int16_t)i, (int16_t)j, &bx, &by);
            CHECK_EQ(ax, bx);
            CHECK_EQ(ay, by);
        }
    }
}

TEST_CASE(out_of_range_input_is_clamped)
{
    touch_map_t m = map_for(TOUCH_ROTATION_0, false, false);
    expect(&m, -50, -50, 0, 0);
    expect(&m, 5000, 5000, 799, 479);

    /* A degenerate source size must not divide by zero or go negative. */
    touch_map_t bad = { .src_w = 0, .src_h = 0, .rotation = TOUCH_ROTATION_0 };
    expect(&bad, 10, 10, 0, 0);
}

TEST_CASE(map_point_updates_in_place)
{
    touch_map_t m = map_for(TOUCH_ROTATION_180, false, false);
    touch_point_t p = { .id = 3, .x = 0, .y = 0, .strength = 42 };
    touch_map_point(&m, &p);
    CHECK_EQ(p.x, 799);
    CHECK_EQ(p.y, 479);
    CHECK_EQ(p.id, 3);
    CHECK_EQ(p.strength, 42);

    /* NULL arguments are no-ops, not crashes. */
    touch_map_point(NULL, &p);
    touch_map_point(&m, NULL);
    touch_map_apply(NULL, 0, 0, NULL, NULL);
    touch_map_size(NULL, NULL, NULL);
    touch_map_apply(&m, 0, 0, NULL, NULL);
}

/*
 * The 7-inch ESP32-P4 board: the GT911 reports 1024 x 600, mirrored in x
 * and y.  The glass's corners land on the panel's opposite corners; the
 * pixels of an 800 x 480 area in the top-left corner end at (799, 479).
 */
TEST_CASE(a_1024_by_600_glass_mirrored_lands_on_the_panels_corners)
{
    const touch_map_t m = {
        .src_w = 1024, .src_h = 600, .mirror_x = true, .mirror_y = true,
        .rotation = TOUCH_ROTATION_0,
    };
    expect(&m, 0, 0, 1023, 599);
    expect(&m, 1023, 599, 0, 0);
    expect(&m, 224, 120, 799, 479);       /* an 800 x 480 area's last */
    expect(&m, 223, 119, 800, 480);       /* just outside it */
    expect(&m, 1024, 600, 0, 0);          /* past the range: clamped */
    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 1024);
    CHECK_EQ(h, 600);
}

/* ------------------------------------------------- Screen rotation 180 */

/* The 7-inch board's glass map (touch.c, board_pins.h): 1024 x 600,
 * mirrored in x and y. */
static const touch_map_t k_board_glass = {
    .src_w = 1024, .src_h = 600, .mirror_x = true, .mirror_y = true,
    .rotation = TOUCH_ROTATION_0,
};

/* The touch injector's map (touch_inject.c): logical pixels of the UI's
 * 1024 x 600, unmirrored. */
static const touch_map_t k_board_inject = {
    .src_w = 1024, .src_h = 600, .mirror_x = false, .mirror_y = false,
    .rotation = TOUCH_ROTATION_0,
};

TEST_CASE(the_turned_glass_lands_on_the_mirrored_corner_centre_and_band)
{
    /* Raw glass -> logical pixel, normal and turned: the turn is
     * x' = 1023 - x, y' = 599 - y of the normal pixel. */
    static const int k[][6] = {
        /* raw x, raw y, normal x, normal y, turned x, turned y */
        { 1023, 599,    0,   0, 1023, 599 },   /* top-left corner      */
        {    0, 599, 1023,   0,    0, 599 },   /* top-right            */
        { 1023,   0,    0, 599, 1023,   0 },   /* bottom-left          */
        {    0,   0, 1023, 599,    0,   0 },   /* bottom-right         */
        {  511, 299,  512, 300,  511, 299 },   /* centre               */
        {  123, 569,  900,  30,  123, 569 },   /* in the 60px band     */
        {  900,  30,  123, 569,  900,  30 },   /* turned into the band */
        { 1023, 540,    0,  59, 1023, 540 },   /* the band's last row  */
    };
    const touch_map_t turned = touch_map_turned(&k_board_glass, true);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        expect(&k_board_glass, k[i][0], k[i][1], k[i][2], k[i][3]);
        expect(&turned, k[i][0], k[i][1], k[i][4], k[i][5]);
    }
    /* The band, the top 60px, is the bottom 60px of the glass turned. */
    int16_t x = 0, y = 0;
    touch_map_apply(&turned, 500, 0, &x, &y);
    CHECK(y < 60);
    touch_map_apply(&turned, 500, 599, &x, &y);
    CHECK(y >= 540);
    int16_t w = 0, h = 0;
    touch_map_size(&turned, &w, &h);
    CHECK_EQ(w, 1024);
    CHECK_EQ(h, 600);
}

TEST_CASE(a_turn_reverses_both_axes_of_the_mapped_area_for_every_map)
{
    for (int r = 0; r < 4; ++r) {
        for (int mir = 0; mir < 4; ++mir) {
            const touch_map_t m = map_for((touch_rotation_t)r, (mir & 1) != 0,
                                          (mir & 2) != 0);
            const touch_map_t t = touch_map_turned(&m, true);
            int16_t w = 0, h = 0;
            touch_map_size(&m, &w, &h);
            for (int x = 0; x < 800; x += 37) {
                for (int y = 0; y < 480; y += 29) {
                    int16_t nx = 0, ny = 0, tx = 0, ty = 0;
                    touch_map_apply(&m, (int16_t)x, (int16_t)y, &nx, &ny);
                    touch_map_apply(&t, (int16_t)x, (int16_t)y, &tx, &ty);
                    if (tx != w - 1 - nx || ty != h - 1 - ny) {
                        T_FAIL("rotation %d mirrors %d: (%d,%d) turned to "
                               "(%d,%d), want (%d,%d)", r * 90, mir, x, y,
                               tx, ty, w - 1 - nx, h - 1 - ny);
                    }
                }
            }
        }
    }
}

TEST_CASE(no_turn_keeps_the_map_and_two_turns_are_none)
{
    const touch_map_t same = touch_map_turned(&k_board_glass, false);
    CHECK(memcmp(&same, &k_board_glass, sizeof(same)) == 0);
    const touch_map_t once = touch_map_turned(&k_board_glass, true);
    CHECK_EQ(once.rotation, TOUCH_ROTATION_180);
    CHECK_EQ(once.src_w, 1024);
    CHECK_EQ(once.src_h, 600);
    CHECK(once.mirror_x && once.mirror_y);
    const touch_map_t twice = touch_map_turned(&once, true);
    CHECK(memcmp(&twice, &k_board_glass, sizeof(twice)) == 0);
    const touch_map_t none = touch_map_turned(NULL, true);
    CHECK_EQ(none.src_w, 0);
    CHECK_EQ(none.src_h, 0);
}

/* The touch task's order (touch.c): the glass mapped with its map, turned
 * or not, the injector with its own, merged, cropped to 1024 x 600. */
static int board_frame(bool turned, const touch_point_t *glass, int ng,
                       const touch_point_t *inj, int ni, touch_point_t *out)
{
    touch_point_t g[TOUCH_MAX_POINTS];
    touch_point_t i[TOUCH_MAX_POINTS];
    if (ng > 0) {
        memcpy(g, glass, sizeof(g[0]) * (size_t)ng);
    }
    if (ni > 0) {
        memcpy(i, inj, sizeof(i[0]) * (size_t)ni);
    }
    const touch_map_t gm = touch_map_turned(&k_board_glass, turned);
    touch_map_points(&gm, g, ng);
    touch_map_points(&k_board_inject, i, ni);
    touch_merge_t m;
    touch_merge_attach(&m, 0);
    touch_merge_glass(&m, g, ng);
    (void)touch_merge_inject(&m, i, ni, true, 0);
    const int n = touch_merge_frame(&m, out, TOUCH_MAX_POINTS);
    return touch_map_crop(out, n, 1024, 600);
}

TEST_CASE(injected_contacts_keep_their_logical_pixels_in_both_orientations)
{
    /* The four corners, the centre, STOP's corner of the band and a point
     * in the band's middle: what station scripts and the injector's README
     * name. */
    static const int k[][2] = {
        { 0, 0 }, { 1023, 0 }, { 0, 599 }, { 1023, 599 }, { 512, 300 },
        { 980, 30 }, { 512, 59 },
    };
    for (int turned = 0; turned < 2; ++turned) {
        for (size_t c = 0; c < sizeof(k) / sizeof(k[0]); ++c) {
            const touch_point_t inj = { .id = 2, .x = (int16_t)k[c][0],
                                        .y = (int16_t)k[c][1] };
            /* A finger on the glass at the same raw spot beside it. */
            const touch_point_t glass = { .id = 4, .x = (int16_t)k[c][0],
                                          .y = (int16_t)k[c][1] };
            touch_point_t out[TOUCH_MAX_POINTS];
            const int n = board_frame(turned != 0, &glass, 1, &inj, 1, out);
            CHECK_EQ(n, 2);
            bool seen_inj = false, seen_glass = false;
            for (int p = 0; p < n; ++p) {
                if (out[p].id == 4) {
                    /* The glass: mirrored, and turned back when turned. */
                    seen_glass = true;
                    CHECK_EQ(out[p].x, turned ? k[c][0] : 1023 - k[c][0]);
                    CHECK_EQ(out[p].y, turned ? k[c][1] : 599 - k[c][1]);
                } else {
                    seen_inj = true;
                    CHECK_EQ(out[p].x, k[c][0]);
                    CHECK_EQ(out[p].y, k[c][1]);
                }
            }
            CHECK(seen_inj && seen_glass);
        }
    }
}

/* ------------------------------------- Which controller is the injector */

/* A GT911's product ID, the 4 bytes at 0x8140: "911" and a NUL. */
static const char k_glass_id[4] = { '9', '1', '1', 0 };

/* What the touch injector's register file serves at 0x8140..0x8143. */
static void emu_product_id(char out[4])
{
    gt911_emu_t e;
    gt911_emu_init(&e, 1024, 600);
    for (unsigned i = 0; i < 4u; ++i) {
        out[i] = (char)gt911_emu_peek(&e, (uint16_t)(GT911_EMU_REG_PRODUCT + i));
    }
}

/* The map the touch task reads one report of the first controller with
 * (touch.c): its role from its address and product ID, then the map for
 * the turn as the flip reports it. */
static touch_map_t first_map(uint16_t addr, const char *id, bool turned)
{
    return touch_merge_role_map(touch_merge_role(addr, false, id),
                                &k_board_glass, &k_board_inject, turned);
}

/* STOP's middle, logical x 850-1018, y 6-54 (ui_band.c), and the raw glass
 * point a finger there reports: both axes mirrored. */
#define STOP_X   934
#define STOP_Y   30
#define STOP_RAW_X (1023 - STOP_X)
#define STOP_RAW_Y (599 - STOP_Y)

TEST_CASE(the_injector_is_known_by_its_product_id_and_glass_never_matches)
{
    char emu[4];
    emu_product_id(emu);
    CHECK(memcmp(emu, TOUCH_MERGE_INJECT_PRODUCT, 4) == 0);
    CHECK(memcmp(GT911_EMU_PRODUCT_ID, TOUCH_MERGE_INJECT_PRODUCT, 4) == 0);
    CHECK(touch_merge_is_injector(emu));

    /* Real parts' IDs, the old injector's "911", one byte off, and none. */
    static const char *const k_not[] = {
        "911", "9110", "9271", "917S", "928", "1151", "9INK", "8INJ",
        "9IN", "9iNJ", "",
    };
    for (size_t i = 0; i < sizeof(k_not) / sizeof(k_not[0]); ++i) {
        char id[4] = { 0 };
        memcpy(id, k_not[i], strlen(k_not[i]));
        if (touch_merge_is_injector(id)) {
            T_FAIL("\"%s\" taken for the injector", k_not[i]);
        }
    }
    CHECK(!touch_merge_is_injector(k_glass_id));
    CHECK(!touch_merge_is_injector(NULL));
}

TEST_CASE(the_role_follows_the_product_id_and_0x5d_wins)
{
    char inj[4];
    emu_product_id(inj);
    /* addr, glass found before it, product ID, role */
    static const struct {
        uint16_t addr;
        bool glass_found;
        int id;     /* 0 glass, 1 injector, 2 NULL (the read failed) */
        touch_role_t want;
    } k[] = {
        { 0x5D, false, 0, TOUCH_ROLE_GLASS },
        { 0x5D, false, 1, TOUCH_ROLE_GLASS },     /* 0x5D is the glass   */
        { 0x5D, false, 2, TOUCH_ROLE_GLASS },
        { 0x14, false, 1, TOUCH_ROLE_INJECTOR },  /* the injector alone  */
        { 0x14, true,  1, TOUCH_ROLE_INJECTOR },  /* beside the glass    */
        { 0x14, false, 0, TOUCH_ROLE_GLASS },     /* glass latched 0x14  */
        { 0x14, false, 2, TOUCH_ROLE_GLASS },     /* unread: the glass   */
        { 0x14, true,  0, TOUCH_ROLE_NONE },      /* second GT911: unread */
        { 0x14, true,  2, TOUCH_ROLE_NONE },
        { 0x15, false, 1, TOUCH_ROLE_NONE },
        { 0x13, true,  1, TOUCH_ROLE_NONE },
        { 0x5C, false, 0, TOUCH_ROLE_NONE },
        { 0x5E, false, 0, TOUCH_ROLE_NONE },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const char *id = k[i].id == 0 ? k_glass_id
                       : k[i].id == 1 ? inj : NULL;
        const touch_role_t got = touch_merge_role(k[i].addr, k[i].glass_found,
                                                  id);
        if (got != k[i].want) {
            T_FAIL("row %zu: 0x%02x found %d id %d gave role %d, want %d",
                   i, k[i].addr, (int)k[i].glass_found, k[i].id, (int)got,
                   (int)k[i].want);
        }
    }
}

TEST_CASE(a_glass_at_0x14_maps_as_at_0x5d_mirrored_and_turned)
{
    for (int turned = 0; turned < 2; ++turned) {
        const touch_map_t at14 = first_map(0x14, k_glass_id, turned != 0);
        const touch_map_t at5d = first_map(0x5D, k_glass_id, turned != 0);
        const touch_map_t want = touch_map_turned(&k_board_glass, turned != 0);
        CHECK(memcmp(&at14, &want, sizeof(want)) == 0);
        CHECK(memcmp(&at5d, &want, sizeof(want)) == 0);
        CHECK(at14.mirror_x && at14.mirror_y);
        CHECK_EQ(at14.rotation, turned ? TOUCH_ROTATION_180 : TOUCH_ROTATION_0);
    }
    /* A finger on STOP, NORMAL: the mirrored raw point lands on STOP, not
     * at the bottom left where the injector's map put it. */
    const touch_map_t normal = first_map(0x14, k_glass_id, false);
    expect(&normal, STOP_RAW_X, STOP_RAW_Y, STOP_X, STOP_Y);
    /* TURNED: the picture's STOP is under the raw point it shows at. */
    const touch_map_t turned = first_map(0x14, k_glass_id, true);
    expect(&turned, STOP_X, STOP_Y, STOP_X, STOP_Y);
    /* A failed product ID read is the glass too. */
    const touch_map_t unread = first_map(0x14, NULL, false);
    expect(&unread, STOP_RAW_X, STOP_RAW_Y, STOP_X, STOP_Y);
}

TEST_CASE(the_injector_at_0x14_keeps_logical_pixels_and_never_turns)
{
    char inj[4];
    emu_product_id(inj);
    for (int turned = 0; turned < 2; ++turned) {
        const touch_map_t m = first_map(0x14, inj, turned != 0);
        CHECK(memcmp(&m, &k_board_inject, sizeof(m)) == 0);
        expect(&m, STOP_X, STOP_Y, STOP_X, STOP_Y);
        expect(&m, 0, 0, 0, 0);
        expect(&m, 1023, 599, 1023, 599);
    }
}

TEST_CASE(a_turn_that_falls_back_takes_the_glass_at_0x14_back_to_normal)
{
    /* gfx_turn_shown() reads true while turned frames reach the glass and
     * false again from the first frame the flip sends unturned. */
    char inj[4];
    emu_product_id(inj);
    static const bool k_shown[] = { false, true, true, false, false };
    for (size_t f = 0; f < sizeof(k_shown) / sizeof(k_shown[0]); ++f) {
        const bool shown = k_shown[f];
        const touch_map_t glass = first_map(0x14, k_glass_id, shown);
        const touch_map_t inject = first_map(0x14, inj, shown);
        /* A finger on the picture's STOP is at raw (STOP) when turned and
         * at raw (mirrored STOP) when normal; it lands on STOP each time. */
        if (shown) {
            expect(&glass, STOP_X, STOP_Y, STOP_X, STOP_Y);
            expect(&glass, STOP_RAW_X, STOP_RAW_Y, STOP_RAW_X, STOP_RAW_Y);
        } else {
            expect(&glass, STOP_RAW_X, STOP_RAW_Y, STOP_X, STOP_Y);
            expect(&glass, STOP_X, STOP_Y, STOP_RAW_X, STOP_RAW_Y);
        }
        expect(&inject, STOP_X, STOP_Y, STOP_X, STOP_Y);
    }
}

TEST_CASE(no_role_or_no_map_gives_an_empty_map)
{
    const touch_map_t none = touch_merge_role_map(TOUCH_ROLE_NONE,
                                                  &k_board_glass,
                                                  &k_board_inject, false);
    CHECK_EQ(none.src_w, 0);
    CHECK_EQ(none.src_h, 0);
    const touch_map_t no_glass = touch_merge_role_map(TOUCH_ROLE_GLASS, NULL,
                                                      &k_board_inject, true);
    CHECK_EQ(no_glass.src_w, 0);
    const touch_map_t no_inject = touch_merge_role_map(TOUCH_ROLE_INJECTOR,
                                                       &k_board_glass, NULL,
                                                       false);
    CHECK_EQ(no_inject.src_w, 0);
    const touch_map_t bad = touch_merge_role_map((touch_role_t)7,
                                                 &k_board_glass,
                                                 &k_board_inject, false);
    CHECK_EQ(bad.src_w, 0);
}

TEST_CASE(a_crop_keeps_the_points_inside_and_their_order)
{
    touch_point_t p[5] = {
        { .id = 1, .x = 0,   .y = 0 },      /* the first pixel: kept */
        { .id = 2, .x = 800, .y = 10 },     /* one column past: dropped */
        { .id = 3, .x = 799, .y = 479 },    /* the last pixel: kept */
        { .id = 4, .x = 10,  .y = 480 },    /* one row past: dropped */
        { .id = 5, .x = 400, .y = 240, .strength = 9 },
    };
    CHECK_EQ(touch_map_crop(p, 5, 800, 480), 3);
    CHECK_EQ(p[0].id, 1);
    CHECK_EQ(p[1].id, 3);
    CHECK_EQ(p[2].id, 5);
    CHECK_EQ(p[2].strength, 9);
}

TEST_CASE(a_crop_of_nothing_or_into_nothing_keeps_nothing)
{
    touch_point_t p[2] = {
        { .id = 1, .x = 1023, .y = 599 },
        { .id = 2, .x = -1,   .y = 5 },     /* never mapped this way: dropped */
    };
    CHECK_EQ(touch_map_crop(p, 2, 800, 480), 0);
    CHECK_EQ(touch_map_crop(NULL, 2, 800, 480), 0);
    CHECK_EQ(touch_map_crop(p, 0, 800, 480), 0);
    CHECK_EQ(touch_map_crop(p, -1, 800, 480), 0);
    p[0].x = 0;
    p[0].y = 0;
    CHECK_EQ(touch_map_crop(p, 1, 0, 480), 0);
    CHECK_EQ(touch_map_crop(p, 1, 800, 0), 0);
    /* The full panel keeps every mapped point. */
    p[0].x = 1023;
    p[0].y = 599;
    CHECK_EQ(touch_map_crop(p, 1, 1024, 600), 1);
}

/*
 * A drag that leaves the UI's area ends there as a release, and one that
 * comes back is a new press: the tracker sees the cropped frames only.
 */
TEST_CASE(a_drag_out_of_the_crop_is_a_release_and_back_in_a_press)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[8];

    touch_point_t f1[1] = { { .id = 2, .x = 790, .y = 100 } };
    int n = touch_tracker_update(&t, f1, touch_map_crop(f1, 1, 800, 480),
                                 ev, 8);
    CHECK_EQ(n, 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);

    touch_point_t f2[1] = { { .id = 2, .x = 805, .y = 100 } };
    n = touch_tracker_update(&t, f2, touch_map_crop(f2, 1, 800, 480), ev, 8);
    CHECK_EQ(n, 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);

    touch_point_t f3[1] = { { .id = 2, .x = 795, .y = 100 } };
    n = touch_tracker_update(&t, f3, touch_map_crop(f3, 1, 800, 480), ev, 8);
    CHECK_EQ(n, 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
}

/* --------------------------------------------------------------- tracker */

TEST_CASE(tracker_reports_down_move_up)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[8];

    touch_point_t a = { .id = 1, .x = 10, .y = 20 };
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.x, 10);

    /* Same position: no event. */
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 0);

    a.x = 11;
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);
    CHECK_EQ(ev[0].point.x, 11);

    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.x, 11);

    /* Nothing left to release. */
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 0);
}

TEST_CASE(tracker_handles_multiple_contacts)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[8];

    touch_point_t two[2] = {
        { .id = 1, .x = 1, .y = 1 },
        { .id = 2, .x = 2, .y = 2 },
    };
    CHECK_EQ(touch_tracker_update(&t, two, 2, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[1].type, TOUCH_EVENT_DOWN);

    /* Lift id 1, move id 2: the release is reported first. */
    touch_point_t one[1] = { { .id = 2, .x = 5, .y = 5 } };
    CHECK_EQ(touch_tracker_update(&t, one, 1, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.id, 1);
    CHECK_EQ(ev[1].type, TOUCH_EVENT_MOVE);
    CHECK_EQ(ev[1].point.id, 2);

    /* A brand new id while another is held is a DOWN, not a MOVE. */
    touch_point_t mixed[2] = {
        { .id = 2, .x = 5, .y = 5 },
        { .id = 7, .x = 9, .y = 9 },
    };
    CHECK_EQ(touch_tracker_update(&t, mixed, 2, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.id, 7);
}

TEST_CASE(tracker_clamps_and_survives_small_buffers)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[1];

    touch_point_t pts[TOUCH_MAX_POINTS + 3];
    for (int i = 0; i < TOUCH_MAX_POINTS + 3; ++i) {
        pts[i].id = (uint8_t)i;
        pts[i].x = (int16_t)i;
        pts[i].y = (int16_t)i;
        pts[i].strength = 0;
    }

    /* More contacts than the controller can produce are truncated, and only
     * as many events as fit are written, but state advances regardless. */
    CHECK_EQ(touch_tracker_update(&t, pts, TOUCH_MAX_POINTS + 3, ev, 1), 1);
    CHECK_EQ(t.prev_count, TOUCH_MAX_POINTS);

    /* Releasing everything: 5 UPs happened, 1 fits. */
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 1), 1);
    CHECK_EQ(t.prev_count, 0);

    /* Negative counts and NULL output are tolerated. */
    CHECK_EQ(touch_tracker_update(&t, pts, -4, ev, 1), 0);
    CHECK_EQ(touch_tracker_update(&t, pts, 2, NULL, 8), 0);
    CHECK_EQ(t.prev_count, 2);
    CHECK_EQ(touch_tracker_update(NULL, pts, 2, ev, 1), 0);

    touch_tracker_reset(NULL);
}

/*
 * The GT911 reuses track ids.  A missed release frame followed by a new
 * contact with the same id must not read as one finger crossing the panel in
 * one poll: a consumer waiting for an UP that gets a MOVE instead leaves the
 * throttle drag latched to whatever moves next.
 */
TEST_CASE(a_contact_that_teleports_is_a_release_and_a_new_press)
{
    touch_tracker_t t;
    memset(&t, 0, sizeof(t));
    touch_event_t ev[8];

    touch_point_t a = { .id = 0, .x = 120, .y = 338 };
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);

    /* A real drag is still a drag. */
    touch_point_t near = { .id = 0, .x = 150, .y = 350 };
    CHECK_EQ(touch_tracker_update(&t, &near, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);

    /* Further than a finger can move in one poll: UP then DOWN, in that
     * order, so a consumer that latches on DOWN is released first. */
    touch_point_t far_away = { .id = 0, .x = 700, .y = 60 };
    CHECK_EQ(touch_tracker_update(&t, &far_away, 1, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.x, 150);          /* released where it last was */
    CHECK_EQ(ev[1].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[1].point.x, 700);

    /* And the new position is the tracked one. */
    touch_point_t after = { .id = 0, .x = 705, .y = 62 };
    CHECK_EQ(touch_tracker_update(&t, &after, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);
}

/* Two contacts with the same track id in one frame is the controller
 * misbehaving; the tracker reports one finger, not two DOWNs and later two
 * UPs. */
TEST_CASE(a_duplicate_track_id_in_one_frame_is_counted_once)
{
    touch_tracker_t t;
    memset(&t, 0, sizeof(t));
    touch_event_t ev[8];

    touch_point_t dup[2] = { { .id = 3, .x = 10, .y = 10 },
                             { .id = 3, .x = 400, .y = 300 } };
    CHECK_EQ(touch_tracker_update(&t, dup, 2, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.x, 10);

    /* Lifting produces exactly one UP, not two. */
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
}

/* What the tracker makes for a jump is marked as made: the UP ends the
 * contact and is no release by the finger, the DOWN starts one that may be
 * a finger in flight.  Every other event carries no flag, the UP of a
 * contact missing from a report included. */
TEST_CASE(the_tracker_flags_the_up_and_the_down_it_makes_for_a_jump)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[8];
    memset(ev, 0xff, sizeof(ev));

    touch_point_t a = { .id = 1, .x = 100, .y = 100 };
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].flags, 0);

    /* 120px is one drag. */
    a.x = 160;
    a.y = 160;
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);
    CHECK_EQ(ev[0].flags, 0);

    /* 121px is a release where it was and a press where it is. */
    a.x = 281;
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.x, 160);
    CHECK_EQ(ev[0].flags, TOUCH_FLAG_NO_TAP);
    CHECK(!touch_event_is_tap_up(&ev[0]));
    CHECK_EQ(ev[1].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[1].point.x, 281);
    CHECK_EQ(ev[1].flags, TOUCH_FLAG_JUMP);

    /* The contact goes on as any other, and its own lift is a lift. */
    a.x = 282;
    memset(ev, 0xff, sizeof(ev));
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);
    CHECK_EQ(ev[0].flags, 0);
    memset(ev, 0xff, sizeof(ev));
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].flags, 0);
    CHECK(touch_event_is_tap_up(&ev[0]));

    /* Back in the next report, with the same id: a DOWN like any first. */
    memset(ev, 0xff, sizeof(ev));
    a.x = 700;
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].flags, 0);
    CHECK(!touch_event_is_tap_up(&ev[0]));
}

int main(void)
{
    RUN(identity_mapping);
    RUN(rotation_90_swaps_axes);
    RUN(rotation_180_flips_both);
    RUN(rotation_270_swaps_the_other_way);
    RUN(mirrors_apply_before_rotation);
    RUN(out_of_range_input_is_clamped);
    RUN(map_point_updates_in_place);
    RUN(a_1024_by_600_glass_mirrored_lands_on_the_panels_corners);
    RUN(the_turned_glass_lands_on_the_mirrored_corner_centre_and_band);
    RUN(a_turn_reverses_both_axes_of_the_mapped_area_for_every_map);
    RUN(no_turn_keeps_the_map_and_two_turns_are_none);
    RUN(injected_contacts_keep_their_logical_pixels_in_both_orientations);
    RUN(the_injector_is_known_by_its_product_id_and_glass_never_matches);
    RUN(the_role_follows_the_product_id_and_0x5d_wins);
    RUN(a_glass_at_0x14_maps_as_at_0x5d_mirrored_and_turned);
    RUN(the_injector_at_0x14_keeps_logical_pixels_and_never_turns);
    RUN(a_turn_that_falls_back_takes_the_glass_at_0x14_back_to_normal);
    RUN(no_role_or_no_map_gives_an_empty_map);
    RUN(a_crop_keeps_the_points_inside_and_their_order);
    RUN(a_crop_of_nothing_or_into_nothing_keeps_nothing);
    RUN(a_drag_out_of_the_crop_is_a_release_and_back_in_a_press);
    RUN(tracker_reports_down_move_up);
    RUN(tracker_handles_multiple_contacts);
    RUN(tracker_clamps_and_survives_small_buffers);
    RUN(a_contact_that_teleports_is_a_release_and_a_new_press);
    RUN(a_duplicate_track_id_in_one_frame_is_counted_once);
    RUN(the_tracker_flags_the_up_and_the_down_it_makes_for_a_jump);
    return test_summary("touch_map");
}
