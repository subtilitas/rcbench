/*
 * The Guition JC3636K518C's faces (shared/dial/dk518_draw.c), drawn
 * natively at 360 x 360 from the scene the M5Stack Dial's faces use.
 *
 * The failures this is about: a string whose cells reach past radius 150,
 * where the rings run; a cell's text outside its cell; ink past the
 * outermost ring; a face drawn again although nothing changed, or not
 * drawn after a reading changed or the face changed; a long title or list
 * row outside the radius; and a canvas of the M5's 240 x 240 written to.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dial_models.h"
#include "dk518_face.h"
#include "ui_theme.h"

/** @brief The frame. */
static gfx_color_t s_fb[DK518_SIZE * DK518_SIZE];
/** @brief The canvas over it. */
static gfx_canvas_t s_c;
/** @brief The renderer. */
static dk518_render_t s_r;

/** @brief What the watcher saw. */
typedef struct {
    int strings;   /**< strings drawn */
    int outside;   /**< strings whose cells leave DK518_R_TEXT */
    int off_cell;  /**< cell strings outside their cell */
} seen_t;

/** @brief The watcher: counts strings and those out of place.
 *  @param ctx the seen_t @param t the string */
static void watch(void *ctx, const dk518_seen_t *t)
{
    seen_t *s = ctx;
    ++s->strings;
    if (!dk518_box_inside(t->box, DK518_R_TEXT)) {
        ++s->outside;
    }
    if (t->cell >= 0) {
        const gfx_rect_t c = dk518_cell_rect(t->cell);
        if (t->box.x < c.x || t->box.y < c.y
            || t->box.x + t->box.w > c.x + c.w
            || t->box.y + t->box.h > c.y + c.h) {
            ++s->off_cell;
        }
    }
}

/** @brief Pixels other than the background outside radius @p r.
 *  @param r the radius @return the count */
static int ink_outside(int r)
{
    const gfx_color_t bg = dk518_color(UI_C_BG, 255u);
    int n = 0;
    for (int y = 0; y < DK518_SIZE; ++y) {
        for (int x = 0; x < DK518_SIZE; ++x) {
            const long dx = 2L * x + 1 - DK518_SIZE;
            const long dy = 2L * y + 1 - DK518_SIZE;
            if (dx * dx + dy * dy > 4L * r * r
                && s_fb[y * DK518_SIZE + x] != bg) {
                ++n;
            }
        }
    }
    return n;
}

/** @brief A fresh renderer and canvas watched into @p s. @param s the
 *         counts */
static void start(seen_t *s)
{
    memset(s, 0, sizeof *s);
    CHECK(dk518_render_init(&s_r));
    s_r.seen = watch;
    s_r.seen_ctx = s;
    gfx_canvas_init(&s_c, s_fb, DK518_SIZE, DK518_SIZE, 0);
}

TEST_CASE(every_face_keeps_its_strings_inside_radius_150)
{
    for (int lang = 0; lang < 2; ++lang) {
        for (int id = 0; id < DM_COUNT; ++id) {
            dial_model_t m;
            seen_t s;
            start(&s);
            dial_model(&m, id, (uint8_t)lang);
            CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
            CHECK(s.strings >= 2);
            CHECK_EQ(s.outside, 0);
            CHECK_EQ(s.off_cell, 0);
            CHECK_EQ(s_r.shortened, 0u);
            /* Nothing past the outermost ring's outer edge. */
            CHECK_EQ(ink_outside(DK518_RING_R0 + 2), 0);
        }
    }
}

TEST_CASE(every_reading_face_draws_its_grid_and_rings)
{
    static const int ids[] = { DM_MOTOR_ARMED, DM_MOTOR_SAFE, DM_SERVO_LIVE };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        dial_model_t m;
        seen_t s;
        start(&s);
        dial_model(&m, ids[i], DK_LANG_EN);
        CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
        /* Title, pill, six labels, six values, units and two hints. */
        CHECK(s.strings >= 16);
        CHECK(ink_outside(DK518_RING_R0 - 2) > 0);
    }
}

TEST_CASE(an_unchanged_scene_draws_nothing_and_a_new_reading_draws_again)
{
    dial_model_t m;
    seen_t s;
    start(&s);
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    const int drawn = s.strings;
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_SAME);
    CHECK_EQ(s.strings, drawn);
    m.face.motor.rpm = 12600;
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    CHECK_EQ(s.strings, 2 * drawn);
    /* The battery, the link bars and the title are part of the scene. */
    m.view.batt_mv = 3500;
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    m.view.rssi = -90;
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    dk_text_copy(m.face.screen.title, sizeof m.face.screen.title, "MOTOR");
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_SAME);
    /* A caller that lost the frame asks for a full draw. */
    s_r.valid = false;
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
}

TEST_CASE(a_new_face_draws_in_full_and_a_lost_link_shows_the_dashed_rim)
{
    dial_model_t m;
    seen_t s;
    start(&s);
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    static const int others[] = { DM_NAVIGATION, DM_VALUE_SET, DM_BIND,
                                  DM_WAITING, DM_LINK_LOST, DM_OFF_ASK,
                                  DM_UPDATE_ASK, DM_UPDATE_SEND };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; ++i) {
        dial_model(&m, others[i], DK_LANG_EN);
        CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
        CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_SAME);
    }
    /* LINK LOST's dashes lie between radius 165 and 174: ink at 170 on
     * some angle, none past 176. */
    dial_model(&m, DM_LINK_LOST, DK_LANG_EN);
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    CHECK(ink_outside(160) > 0);
    CHECK_EQ(ink_outside(DK518_RING_R0), 0);
}

TEST_CASE(the_widest_list_row_steps_down_the_chain_inside_radius_150)
{
    dial_model_t m;
    seen_t s;
    start(&s);
    dial_model(&m, DM_OTHER, DK_LANG_EN);
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    const uint32_t before = s_r.shortened;
    /* DK_ITEM_MAX characters of the widest letter: too wide for 20x36 in
     * the focus's row, whole in a smaller font. */
    dial_model(&m, DM_NAVIGATION, DK_LANG_EN);
    dk_text_copy(m.face.nav[DK_SLOT_FOCUS].item,
                 sizeof m.face.nav[DK_SLOT_FOCUS].item,
                 "WWWWWWWWWWWWWWW");
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    CHECK_EQ(s_r.shortened, before);
    CHECK_EQ(s.outside, 0);
}

/** @brief Draw the bind face with @p addr in the address's block.
 *  @param s the counts @param addr the text @return strings drawn */
static int bind_with(seen_t *s, const char *addr)
{
    dial_model_t m;
    start(s);
    dial_model(&m, DM_BIND, DK_LANG_EN);
    m.view.addr = addr;
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    CHECK_EQ(s->outside, 0);
    return s->strings;
}

TEST_CASE(a_text_too_wide_drops_its_last_part_and_never_cuts_a_number)
{
    seen_t s;
    const int whole = bind_with(&s, "A0:B1:C2:7F:3A:10");
    CHECK_EQ(s_r.shortened, 0u);
    /* A last part, two spaces apart, that fits nowhere: dropped whole. */
    CHECK_EQ(bind_with(&s, "A0:B1:C2:7F:3A:10  WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"),
             whole);
    CHECK_EQ(s_r.shortened, 1u);
    /* One part of 40 digits, 320px in 8x16, too wide for the row: not
     * drawn at all. */
    CHECK_EQ(bind_with(&s, "1234567890123456789012345678901234567890"),
             whole - 1);
    CHECK_EQ(s_r.shortened, 1u);
    /* One part without digits: cut by characters in 8x16. */
    CHECK_EQ(bind_with(&s, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"), whole);
    CHECK_EQ(s_r.shortened, 1u);
}

TEST_CASE(the_longest_title_steps_down_a_font_inside_the_shapes_radius)
{
    dial_model_t m;
    seen_t s;
    start(&s);
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    /* DK_TITLE_MAX characters: too wide for 12x24 and 10x20 in the tag,
     * whole in 8x16. */
    dk_text_copy(m.face.screen.title, sizeof m.face.screen.title,
                 "WWWWWWWWWWWWWWW");
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, &m.view), DK518_DRAWN);
    CHECK_EQ(s.outside, 0);
    CHECK_EQ(s_r.shortened, 0u);
}

TEST_CASE(a_canvas_of_another_size_or_no_argument_draws_nothing)
{
    static gfx_color_t small[240 * 240];
    dial_model_t m;
    gfx_canvas_t c;
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(dk518_render_init(&s_r));
    gfx_canvas_init(&c, small, 240, 240, 0);
    CHECK_EQ(dk518_render(&s_r, &c, &m.face, &m.view), DK518_NONE);
    CHECK_EQ(small[120 * 240 + 120], 0u);
    gfx_canvas_init(&s_c, s_fb, DK518_SIZE, DK518_SIZE, 0);
    CHECK_EQ(dk518_render(NULL, &s_c, &m.face, &m.view), DK518_NONE);
    CHECK_EQ(dk518_render(&s_r, NULL, &m.face, &m.view), DK518_NONE);
    CHECK_EQ(dk518_render(&s_r, &s_c, NULL, &m.view), DK518_NONE);
    CHECK_EQ(dk518_render(&s_r, &s_c, &m.face, NULL), DK518_NONE);
    CHECK(!dk518_render_init(NULL));
}

TEST_CASE(the_geometry_answers_at_its_edges)
{
    CHECK(dk518_box_inside(gfx_rect_make(0, 0, 0, 0), 1));
    CHECK(dk518_box_inside(gfx_rect_make(179, 179, 2, 2), 1));
    CHECK(!dk518_box_inside(gfx_rect_make(179, 179, 3, 2), 1));
    CHECK(!dk518_box_inside(gfx_rect_make(0, 0, 10, 10), DK518_R_TEXT));
    CHECK_EQ(dk518_ring_radius(0), DK518_RING_R0);
    CHECK_EQ(dk518_ring_radius(DIAL_RINGS - 1),
             DK518_RING_R0 - (DIAL_RINGS - 1) * DK518_RING_PITCH);
    /* The innermost ring's inner edge clears the strings' radius. */
    CHECK(dk518_ring_radius(DIAL_RINGS - 1) - DK518_RING_W / 2
          > DK518_R_SHAPE);
    CHECK_EQ(dk518_cell_rect(-1).w, 0);
    CHECK_EQ(dk518_cell_rect(DIAL_CELLS).w, 0);
    for (int i = 0; i < DIAL_CELLS; ++i) {
        CHECK(dk518_box_inside(dk518_cell_rect(i), DK518_R_TEXT));
    }
    CHECK_EQ(dk518_color(UI_C_TEXT, 999u), dk518_color(UI_C_TEXT, 255u));
    CHECK_EQ(dk518_color(UI_C_TEXT, 0u), dk518_color(UI_C_BG, 255u));
}

int main(void)
{
    RUN(every_face_keeps_its_strings_inside_radius_150);
    RUN(every_reading_face_draws_its_grid_and_rings);
    RUN(an_unchanged_scene_draws_nothing_and_a_new_reading_draws_again);
    RUN(a_new_face_draws_in_full_and_a_lost_link_shows_the_dashed_rim);
    RUN(the_widest_list_row_steps_down_the_chain_inside_radius_150);
    RUN(a_text_too_wide_drops_its_last_part_and_never_cuts_a_number);
    RUN(the_longest_title_steps_down_a_font_inside_the_shapes_radius);
    RUN(a_canvas_of_another_size_or_no_argument_draws_nothing);
    RUN(the_geometry_answers_at_its_edges);
    return test_summary("dk518_face");
}
