/**
 * @file
 * @brief The pick of a cat among the poses that fit a screen's spots, its
 *        drawing, and the random source.  See include/ui_cats.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_cats.h"

#include <stddef.h>

_Static_assert(UI_CAT_POSES >= 1 && UI_CAT_POSES <= 32,
               "a pick counts the poses in a 32-bit mask");
_Static_assert(UI_CAT_SPOTS <= 32, "a pose's spots are a 32-bit mask");

/** @brief The source ui_cats_roll() reads; NULL for the xorshift. */
static ui_cats_random_fn s_random;
/** @brief The xorshift's state, never 0. */
static uint32_t s_state = 1u;
/** @brief The pose ui_cats_force() set, UI_CAT_RANDOM by default. */
static int s_forced = UI_CAT_RANDOM;

void ui_cats_set_random(ui_cats_random_fn fn) { s_random = fn; }

void ui_cats_seed(uint32_t seed) { s_state = (seed != 0u) ? seed : 1u; }

uint32_t ui_cats_roll(void)
{
    if (s_random != NULL) {
        return s_random();
    }
    /* Marsaglia's xorshift32: period 2^32 - 1 over the nonzero states. */
    uint32_t v = s_state;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    s_state = v;
    return v;
}

void ui_cats_force(int pose)
{
    s_forced = (pose == UI_CAT_RANDOM || (pose >= 0 && pose < UI_CAT_POSES))
                   ? pose : UI_CAT_NONE;
}

int ui_cats_forced(void) { return s_forced; }

gfx_rect_t ui_cat_rect(int pose, const ui_cat_spot_t *spot)
{
    if (spot == NULL || pose < 0 || pose >= UI_CAT_POSES) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    const gfx_sprite_t *a = &ui_cat_poses[pose].art;
    const int x = (spot->side == UI_CAT_FROM_RIGHT) ? spot->x - a->w
                                                     : spot->x;
    return gfx_rect_make(x, spot->y - a->h, a->w, a->h);
}

/**
 * @brief The spots of @p spots that pose @p pose may take and that are
 *        clear, as a mask of indexes.
 * @param pose index into ui_cat_poses[]
 * @param spots the screen's spots
 * @param n how many, at most 32
 * @param clear the screen's test of a box
 * @return bit i set for spots[i]
 */
static uint32_t clear_spots(int pose, const ui_cat_spot_t *spots, int n,
                            ui_cat_clear_fn clear)
{
    uint32_t m = 0u;
    for (int i = 0; i < n; ++i) {
        if ((unsigned)spots[i].id < 32u
            && (ui_cat_poses[pose].spots & UI_CAT_IN(spots[i].id)) != 0u
            && clear(ui_cat_rect(pose, &spots[i]))) {
            m |= (uint32_t)1u << (unsigned)i;
        }
    }
    return m;
}

/**
 * @brief The index of the @p k-th set bit of @p m, counting from 0.
 * @param m the mask, with more than @p k bits set
 * @param k which set bit
 * @return its index
 */
static int nth_bit(uint32_t m, unsigned k)
{
    for (int i = 0; i < 32; ++i) {
        if ((m & ((uint32_t)1u << (unsigned)i)) != 0u && k-- == 0u) {
            return i;
        }
    }
    return 0;
}

/**
 * @brief The set bits of @p m.
 * @param m the mask
 * @return how many
 */
static unsigned bits(uint32_t m)
{
    unsigned n = 0u;
    for (; m != 0u; m &= m - 1u) {
        ++n;
    }
    return n;
}

ui_cat_pick_t ui_cats_pick(const ui_cat_spot_t *spots, int n,
                           ui_cat_clear_fn clear, uint32_t roll)
{
    if (spots == NULL || clear == NULL || n <= 0 || n > 32
        || s_forced == UI_CAT_NONE) {
        return UI_CAT_PICK_NONE;
    }
    uint32_t fit[UI_CAT_POSES];
    uint32_t poses = 0u;
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        fit[p] = (s_forced == UI_CAT_RANDOM || s_forced == p)
                     ? clear_spots(p, spots, n, clear) : 0u;
        if (fit[p] != 0u) {
            poses |= (uint32_t)1u << (unsigned)p;
        }
    }
    const unsigned np = bits(poses);
    if (np == 0u) {
        return UI_CAT_PICK_NONE;
    }
    const int pose = nth_bit(poses, roll % np);
    const unsigned ns = bits(fit[pose]);
    const int spot = nth_bit(fit[pose], (roll / np) % ns);
    return (ui_cat_pick_t){ (int8_t)pose, (int8_t)spot };
}

gfx_rect_t ui_cats_draw(gfx_canvas_t *c, ui_cat_pick_t pick,
                        const ui_cat_spot_t *spots, int n)
{
    if (spots == NULL || pick.pose < 0 || pick.pose >= UI_CAT_POSES
        || pick.spot < 0 || pick.spot >= n) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    const gfx_rect_t r = ui_cat_rect(pick.pose, &spots[pick.spot]);
    (void)gfx_sprite_draw(c, r.x, r.y, &ui_cat_poses[pick.pose].art);
    return r;
}
