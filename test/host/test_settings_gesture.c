/*
 * SETUP under touch: scrolling, key repeat and the gestures the tracker
 * feeds the router.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

/* -------------------------------------------------- gestures, tracker-fed */


/* Steps @p id stands from its default, in units of its own step. */
static int steps_off(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    return (int)lroundf((settings_get(id) - d->def) / d->step);
}

/*
 * Swipes of 150 px that start on column x, from start points 29 px apart
 * down the list, up and down, at seven speeds, on every category.  The
 * upward ones from the upper rows run into the band.  A downward swipe
 * starts on a list scrolled 100 px, so it has somewhere to go.  Returns how
 * many changed a setting; *unscrolled counts those that stayed in the body,
 * moved as one contact and left the list where it was although it had room.
 */
static int swipe_matrix(const int *cols, int ncols, int *unscrolled,
                        int *total)
{
    static const int speeds[] = { 2, 4, 9, 20, 60, 119, 130 };
    int bad = 0;
    *unscrolled = 0;
    *total = 0;
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        for (int c = 0; c < ncols; ++c) {
            for (size_t sp = 0; sp < sizeof(speeds) / sizeof(speeds[0]);
                 ++sp) {
                for (int y0 = LIST_Y + 4; y0 < LIST_Y + LIST_H; y0 += 29) {
                    for (int dir = -1; dir <= 1; dir += 2) {
                        gesture_screen((setting_cat_t)cat);
                        if (dir > 0) {
                            swipe(XL, LIST_Y + 300, XL, LIST_Y + 192, 4);
                            snap();
                        }
                        int max = 0;
                        const int from = settings_screen_scroll(&max);
                        int y1 = y0 + dir * 150;
                        if (y1 < 2)   { y1 = 2; }
                        if (y1 > 479) { y1 = 479; }
                        swipe(cols[c], y0, cols[c], y1, speeds[sp]);
                        idle(0.5f);
                        ++*total;
                        bad += (changed() != 0) ? 1 : 0;
                        const bool room = (dir < 0) ? from < max : from > 0;
                        if (speeds[sp] <= TOUCH_JUMP_PX && y1 >= UI_BAND_H
                            && room
                            && settings_screen_scroll(NULL) == from) {
                            ++*unscrolled;
                        }
                    }
                }
            }
        }
    }
    return bad;
}

/* The defect: a key stepped on its DOWN, so a scroll that started on a key
 * column had changed the setting under the finger before it scrolled. */
TEST_CASE(a_swipe_that_starts_on_a_key_column_scrolls_and_changes_no_value)
{
    static const int cols[] = { MINUS_X, XM, MINUS_X + BTN_W - 1,
                                PLUS_X, XP, PLUS_X + BTN_W - 1 };
    int unscrolled = 0, total = 0;
    const int bad = swipe_matrix(cols, 6, &unscrolled, &total);
    CHECK_EQ(total, SET_CAT_SETUP_COUNT * 6 * 7 * 12 * 2);
    CHECK_EQ(bad, 0);
    CHECK_EQ(unscrolled, 0);
}

/* The same beside the columns, one px outside each edge among them. */
TEST_CASE(a_swipe_beside_the_key_columns_scrolls_and_changes_no_value)
{
    static const int cols[] = { LIST_X + 4, XL, MINUS_X - 11, MINUS_X - 1,
                                MINUS_X + BTN_W, 660, PLUS_X - 1,
                                PLUS_X + BTN_W, 783 };
    int unscrolled = 0, total = 0;
    const int bad = swipe_matrix(cols, 9, &unscrolled, &total);
    CHECK_EQ(total, SET_CAT_SETUP_COUNT * 9 * 7 * 12 * 2);
    CHECK_EQ(bad, 0);
    CHECK_EQ(unscrolled, 0);
}

/*
 * Every list scrolled to its end shows its last row whole: the row inside
 * the list, above the strip, its bottom edge drawn and its help line in
 * the faint text colour.  Drags of 240 px until the scroll stops, in both
 * languages, as the German labels are the longer.
 */
TEST_CASE(every_list_at_its_end_shows_its_last_row_whole)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
            gesture_screen((setting_cat_t)cat);
            ui_text_set_language((ui_lang_t)lang);
            const int n = settings_in_category((setting_cat_t)cat, NULL, 0);
            int max = 0;
            for (int k = 0; k < 2 * n; ++k) {
                swipe(XL, LIST_Y + LIST_H - 8, XL, LIST_Y + LIST_H - 248, 4);
            }
            const int scroll = settings_screen_scroll(&max);
            CHECK_EQ(scroll, max);
            const int top = LIST_Y + (n - 1) * ROW_PITCH - scroll;
            const int bottom = top + ROW_H;
            if (top < LIST_Y || bottom > LIST_Y + LIST_H
                || bottom > STRIP_Y) {
                T_FAIL("category %d, language %d: last row %d..%d", cat,
                       lang, top, bottom);
                continue;
            }
            memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            settings_screen_invalidate();
            ui_router_render(&s_c, 0);
            CHECK_EQ(gfx_pixel_get(&s_c, LIST_X + 300, bottom - 1),
                     ui_theme_color(UI_C_EDGE));
            int help = 0;
            for (int y = top + 33; y < top + 49; ++y) {
                for (int x = LIST_X + 16; x < MINUS_X - 10; ++x) {
                    help += gfx_pixel_get(&s_c, x, y)
                            == ui_theme_color(UI_C_TEXT_FAINT);
                }
            }
            if (help == 0) {
                T_FAIL("category %d, language %d: no help line on the last "
                       "row", cat, lang);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* What a tester's panel showed: INA3221 shunt 99.9 mOhm and Sensor SDA 15
 * after the INTERFACES list was scrolled twice from its bottom row. */
TEST_CASE(two_scrolls_from_the_bottom_row_leave_the_shunt_and_the_pins)
{
    gesture_screen(SET_CAT_IFACE);
    CHECK_EQ(row_of(SET_INA3221_MOHM), 6);
    CHECK_EQ(row_of(SET_SENSE_SDA), 8);
    swipe(XM, LIST_Y + LIST_H - 8, XM, LIST_Y + LIST_H - 132, 4);
    CHECK_EQ(settings_screen_scroll(NULL), 116);
    swipe(XM, LIST_Y + LIST_H - 8, XM, LIST_Y + LIST_H - 132, 4);
    CHECK_EQ(settings_screen_scroll(NULL), 232);
    CHECK_NEAR(settings_get(SET_INA3221_MOHM), 100.0f, 1e-4);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(changed(), 0);
}

/* One tap, one step: "-" and "+" on a number, "<" and ">" on a switch and
 * on a choice.  The press alone steps nothing. */
TEST_CASE(a_tap_steps_each_kind_of_key_once)
{
    bool seen[4] = { false, false, false, false };
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        setting_id_t ids[64];
        const int n = settings_in_category((setting_cat_t)cat, ids, 64);
        for (int row = 0; row < n && row < 6; ++row) {
            const setting_def_t *d = settings_def(ids[row]);
            if (seen[d->type]) {
                continue;
            }
            seen[d->type] = true;
            for (int by = -1; by <= 1; by += 2) {
                gesture_screen((setting_cat_t)cat);
                /* Off both ends, so either key has a step to make. */
                if (d->type == SET_TYPE_INT || d->type == SET_TYPE_FLOAT) {
                    settings_set(ids[row], d->min + 3.0f * d->step);
                }
                snap();
                const float was = settings_get(ids[row]);
                settings_adjust(ids[row], by);
                const float want = settings_get(ids[row]);
                settings_set(ids[row], was);
                CHECK(want != was);

                finger(FEED_LONE, (by < 0) ? XM : XP, row_y(row));
                idle(0.20f);
                CHECK_EQ(changed(), 0);               /* armed, not stepped */
                lift(FEED_LONE);
                CHECK(settings_get(ids[row]) == want);
                CHECK_EQ(changed(), 1);
                idle(1.0f);
                CHECK(settings_get(ids[row]) == want);
            }
        }
    }
    CHECK(seen[SET_TYPE_INT]);
    CHECK(seen[SET_TYPE_FLOAT]);
    CHECK(seen[SET_TYPE_BOOL]);
    CHECK(seen[SET_TYPE_ENUM]);
}

/* A key held: nothing for 0.45 s, then 8 steps a second, 30 a second from
 * 2.25 s.  5 steps after 1.00 s and 38 after 3.00 s. */
TEST_CASE(a_held_key_repeats_five_times_in_one_second_and_38_in_three)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(KEY_ID));
    finger(FEED_LONE, XM, y);                 /* its tick: 0.01 s held */
    idle(0.43f);
    CHECK_EQ(steps_off(KEY_ID), 0);
    idle(0.56f);
    CHECK_EQ(steps_off(KEY_ID), -5);
    idle(2.00f);
    CHECK_EQ(steps_off(KEY_ID), -38);
    lift(FEED_LONE);
    CHECK_EQ(steps_off(KEY_ID), -38);  /* the release adds none */
    idle(1.0f);
    CHECK_EQ(steps_off(KEY_ID), -38);
    CHECK_EQ(changed(), 1);
}

/* A frame that stalls is credited 0.25 s and no more: a 1 s stall, before
 * the delay ends, while the repeat runs at 8 Hz and while it runs at 30 Hz,
 * steps at most as often as 0.25 s of hold does, plus the step the
 * crossing of the delay adds. */
TEST_CASE(a_stalled_frame_adds_at_most_the_capped_steps_to_a_held_key)
{
    gesture_screen(SET_CAT_IFACE);
    feed_tick_per_report(0.0f);
    const int y = row_y(row_of(KEY_ID));
    finger(FEED_LONE, XP, y);

    /* Before the delay: 1 s in one frame is 0.25 s of hold, no step. */
    ui_router_tick(1.0f);
    CHECK_EQ(steps_off(KEY_ID), 0);

    /* The slow rate: 0.25 s held, then 0.2 s more, then a 1 s stall. */
    ui_router_tick(0.2f);
    ui_router_tick(0.2f);
    int before = steps_off(KEY_ID);
    ui_router_tick(1.0f);
    int stall = steps_off(KEY_ID) - before;
    CHECK(stall >= 1 && stall <= 2);            /* 0.25 s at 8 Hz */

    /* The fast rate, from 2.25 s held. */
    for (int i = 0; i < 20; ++i) {
        ui_router_tick(0.1f);
    }
    before = steps_off(KEY_ID);
    ui_router_tick(1.0f);
    stall = steps_off(KEY_ID) - before;
    CHECK(stall >= 1 && stall <= 8);            /* 0.25 s at 30 Hz */
    lift(FEED_LONE);
}

/* The hold's limit: 449 ms is a tap that has not stepped yet, 450 ms is the
 * first step of the repeat.  Frames of at most 0.2 s, under the 0.25 s a
 * frame is credited with; the timer adds frame times and reads no tick
 * counter, so it has no wrap. */
TEST_CASE(the_repeat_starts_at_450_ms)
{
    static const struct { float held; int while_down; } k[] = {
        { 0.449f, 0 }, { 0.450f, 1 }, { 0.451f, 1 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        gesture_screen(SET_CAT_IFACE);
        feed_tick_per_report(0.0f);
        const int y = row_y(row_of(KEY_ID));
        finger(FEED_LONE, XP, y);
        ui_router_tick(0.2f);
        ui_router_tick(k[i].held - 0.2f);
        CHECK_EQ(steps_off(KEY_ID), k[i].while_down);
        lift(FEED_LONE);
        /* One step either way: the tap's, or the repeat's first. */
        CHECK_EQ(steps_off(KEY_ID), 1);
    }
    /* In 10 ms frames: 44 are short of it and 46 are past it. */
    for (int frames = 44; frames <= 46; frames += 2) {
        gesture_screen(SET_CAT_IFACE);
        feed_tick_per_report(0.0f);
        finger(FEED_LONE, XP, row_y(row_of(KEY_ID)));
        for (int f = 0; f < frames; ++f) {
            ui_router_tick(REPORT_S);
        }
        CHECK_EQ(steps_off(KEY_ID), (frames == 44) ? 0 : 1);
        /* A swipe from here leaves what the hold has stepped and adds
         * nothing. */
        glide(FEED_LONE, XP, row_y(row_of(KEY_ID)) - 100, 9);
        lift(FEED_LONE);
        CHECK_EQ(steps_off(KEY_ID), (frames == 44) ? 0 : 1);
    }
}

/* A held key that starts to scroll: it repeats while the finger is within
 * the 8 px, and stops at 9 px. */
TEST_CASE(a_hold_that_turns_into_a_swipe_stops_repeating_at_the_slop)
{
    for (int dir = -1; dir <= 1; dir += 2) {
        gesture_screen(SET_CAT_IFACE);
        const int y = row_y(row_of(KEY_ID));
        finger(FEED_LONE, XM, y);
        idle(0.99f);
        CHECK_EQ(steps_off(KEY_ID), -5);
        feed_tick_per_report(0.0f);
        glide(FEED_LONE, XM, y + dir * 8, 1);
        idle(0.50f);                          /* 1.50 s held: 4 more */
        CHECK_EQ(steps_off(KEY_ID), -9);
        CHECK_EQ(settings_screen_scroll(NULL), 0);
        glide(FEED_LONE, XM, y + dir * 9, 1);
        idle(1.00f);
        CHECK_EQ(steps_off(KEY_ID), -9);
        /* Up the glass scrolls the list on; down, it is at its top. */
        CHECK_EQ(settings_screen_scroll(NULL), (dir < 0) ? 1 : 0);
        glide(FEED_LONE, XM, y + dir * 20, 1);
        lift(FEED_LONE);
        idle(1.00f);
        CHECK_EQ(steps_off(KEY_ID), -9);
        CHECK_EQ(changed(), 1);
    }
}

/* The slop of a tap: 8 px from the press in x or in y is a tap, 9 px is
 * not. */
TEST_CASE(a_tap_may_move_8_px_and_not_9)
{
    static const struct { int dx, dy, steps; } k[] = {
        {  0,  0, 1 },
        {  8,  0, 1 }, {  9,  0, 0 }, { -8,  0, 1 }, { -9,  0, 0 },
        {  0,  8, 1 }, {  0,  9, 0 }, {  0, -8, 1 }, {  0, -9, 0 },
        {  8,  8, 1 }, {  8, -8, 1 }, {  9,  9, 0 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        for (int stride = 1; stride <= 9; stride += 8) {
            gesture_screen(SET_CAT_IFACE);
            const int y = row_y(row_of(KEY_ID));
            finger(FEED_LONE, XP, y);
            /* Px by px, and in one report. */
            glide(FEED_LONE, XP + k[i].dx, y + k[i].dy, stride);
            lift(FEED_LONE);
            idle(0.5f);
            CHECK_EQ(steps_off(KEY_ID), k[i].steps);
            CHECK_EQ(changed(), k[i].steps);
        }
    }
}

/* Sideways off a key gives it up: no step on the release, and no repeat
 * while the finger rests where it went. */
TEST_CASE(a_press_that_slides_off_a_key_sideways_steps_nothing)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(KEY_ID));

    /* 300 px left onto the label, and held there. */
    finger(FEED_LONE, XM, y);
    glide(FEED_LONE, XM - 300, y, 5);
    idle(1.0f);
    CHECK_EQ(changed(), 0);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* From "-" onto "+" of the same row. */
    finger(FEED_LONE, XM, y);
    glide(FEED_LONE, XP, y, 5);
    idle(1.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* Out of the key within the slop: 5 px left of its edge. */
    finger(FEED_LONE, MINUS_X, y);
    glide(FEED_LONE, MINUS_X - 5, y, 1);
    idle(1.0f);
    CHECK_EQ(changed(), 0);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* And back onto it does not arm it again. */
    finger(FEED_LONE, MINUS_X, y);
    glide(FEED_LONE, MINUS_X - 5, y, 1);
    glide(FEED_LONE, MINUS_X + 2, y, 1);
    idle(1.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
}

/* A key is its column over its own row.  The 4 px between two rows belong
 * to neither: a tap there steps nothing, and a press that leaves the row
 * into the gap is not a tap. */
TEST_CASE(the_gap_between_two_rows_is_no_key)
{
    gesture_screen(SET_CAT_IFACE);
    setting_id_t ids[64];
    settings_in_category(SET_CAT_IFACE, ids, 64);
    const int row = 2;
    const int top = LIST_Y + row * ROW_PITCH;
    for (int y = top + ROW_H; y < top + ROW_PITCH; ++y) {
        feed_tap(FEED_LONE, XM, y);
        feed_tap(FEED_LONE, XP, y);
    }
    CHECK_EQ(changed(), 0);

    /* The row's first and last px are the key. */
    feed_tap(FEED_LONE, XM, top);
    CHECK(settings_get(ids[row]) != s_before[ids[row]]);
    CHECK_EQ(changed(), 1);
    feed_tap(FEED_LONE, XP, top + ROW_H - 1);
    CHECK_EQ(changed(), 0);

    finger(FEED_LONE, XM, top + ROW_H - 1);
    glide(FEED_LONE, XM, top + ROW_H + 2, 1);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
}

/* One contact holds the screen.  A second one presses nothing and ends
 * nothing. */
TEST_CASE(a_second_finger_does_nothing_on_the_setup_screen)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(KEY_ID));

    /* The first scrolls on the label; the second rests 2 s on a key. */
    finger(0, XL, LIST_Y + 300);
    glide(0, XL, LIST_Y + 260, 4);
    const int mid = settings_screen_scroll(NULL);
    CHECK(mid > 0);
    finger(1, XM, y);
    idle(2.0f);
    CHECK_EQ(changed(), 0);
    glide(0, XL, LIST_Y + 220, 4);            /* the first goes on scrolling */
    CHECK_EQ(settings_screen_scroll(NULL), mid + 40);
    lift(1);
    CHECK_EQ(changed(), 0);
    lift(0);
    CHECK_EQ(changed(), 0);

    /* The first rests on the label; the second taps a key, then holds it. */
    gesture_screen(SET_CAT_IFACE);
    finger(0, XL, y);
    finger(1, XM, y);
    lift(1);
    finger(1, XP, y);
    idle(2.0f);
    lift(1);
    CHECK_EQ(changed(), 0);
    lift(0);
    CHECK_EQ(changed(), 0);

    /* The first holds a key; the second's tap elsewhere does not end the
     * repeat and steps nothing of its own. */
    gesture_screen(SET_CAT_IFACE);
    finger(0, XM, y);
    idle(0.99f);
    CHECK_EQ(steps_off(KEY_ID), -5);
    feed_tick_per_report(0.0f);
    finger(1, XP, row_y(1));
    lift(1);
    finger(1, XL, row_y(2));
    lift(1);
    idle(0.50f);
    CHECK_EQ(steps_off(KEY_ID), -9);
    lift(0);
    CHECK_EQ(changed(), 1);

    /* With the glass clear the next tap is taken, whichever id it has. */
    feed_tap(3, XP, y);
    CHECK_EQ(steps_off(KEY_ID), -8);
}

/* A contact the controller leaves out of one report arrives as an UP and a
 * new DOWN.  Mid-swipe, the new DOWN travels on and is no tap. */
TEST_CASE(a_contact_that_drops_out_of_a_swipe_changes_no_value)
{
    /* On the label, and it returns on a key column. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XL, LIST_Y + LIST_H - 8);
    glide(FEED_LONE, XL, LIST_Y + 330, 9);
    lift(FEED_LONE);
    finger(FEED_LONE, XM, LIST_Y + 290);
    glide(FEED_LONE, XM, LIST_Y + 150, 9);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);

    /* On a key column from the start. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + LIST_H - 8);
    glide(FEED_LONE, XM, LIST_Y + 330, 9);
    lift(FEED_LONE);
    finger(FEED_LONE, XM, LIST_Y + 321);
    glide(FEED_LONE, XM, LIST_Y + 150, 9);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);
    CHECK(settings_screen_scroll(NULL) > 100);
}

/* More than 120 px between two reports: the tracker ends the contact with a
 * flagged UP and starts one with a flagged DOWN.  Neither steps a key. */
TEST_CASE(a_flick_the_tracker_splits_changes_no_value)
{
    /* Down a key column, 140 px a report, lifted on the column. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 340);
    finger(FEED_LONE, XM, LIST_Y + 200);
    finger(FEED_LONE, XM, LIST_Y + 60);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(feed_downs, 3);         /* the list opened as LIST_CAT: 3 */
    CHECK_EQ(changed(), 0);

    /* From the label onto a key in one report, then a swipe. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM - 70, LIST_Y + 340);
    finger(FEED_LONE, XM, LIST_Y + 260);
    glide(FEED_LONE, XM, LIST_Y + 150, 9);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);

    /* From the label onto a key in one report, lifted in the next: a DOWN
     * and an UP in one place, which is a tap but for the DOWN's flag. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XL, LIST_Y + LIST_H - 8);
    finger(FEED_LONE, XM, LIST_Y + 236);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);
    /* It rests there instead: no repeat either. */
    finger(FEED_LONE, XL, LIST_Y + LIST_H - 8);
    finger(FEED_LONE, XM, LIST_Y + 236);
    idle(2.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* The tap after it is a tap. */
    feed_tap(FEED_LONE, XM, row_y(row_of(KEY_ID)));
    CHECK_EQ(steps_off(KEY_ID), -1);
}

/* A jump's DOWN is a press on every control that is not a key of the
 * scrolling list: a category's tile pressed right after a touch elsewhere
 * whose release frame went missing is still opened. */
TEST_CASE(a_category_takes_the_press_the_tracker_made_for_a_jump)
{
    gesture_screen(LIST_CAT);
    feed_tap(FEED_LONE, home_x(), home_y());
    setting_id_t app[64];
    settings_in_category(SET_CAT_APP, app, 64);
    finger(FEED_LONE, tile_x(LIST_CAT), tile_y(LIST_CAT) + 200);
    finger(FEED_LONE, tile_x(SET_CAT_APP), tile_y(SET_CAT_APP));
    lift(FEED_LONE);
    CHECK_EQ(settings_screen_category(), SET_CAT_APP);
    snap();
    feed_tap(FEED_LONE, XP, row_y(0));
    CHECK(settings_get(app[0]) != s_before[app[0]]);
    CHECK_EQ(changed(), 1);
}

/* A key pressed and carried into the band: the router releases it for the
 * screen where the screen last saw it, which is on the key.  That release
 * is not the finger's and steps nothing; and nothing repeats afterwards
 * (v0.14.0 went on stepping with no finger on the glass). */
TEST_CASE(a_key_carried_into_the_band_steps_nothing_and_never_repeats)
{
    /* Straight into the band in one report: the release is at the press. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 4);
    finger(FEED_LONE, XM, UI_BAND_H - 8);
    CHECK_EQ(changed(), 0);
    idle(3.0f);
    CHECK_EQ(changed(), 0);
    lift(FEED_LONE);
    idle(3.0f);
    CHECK_EQ(changed(), 0);

    /* 8 px up inside the body first. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 12);
    finger(FEED_LONE, XM, LIST_Y + 4);
    finger(FEED_LONE, XM, UI_BAND_H - 8);
    idle(3.0f);
    lift(FEED_LONE);
    idle(3.0f);
    CHECK_EQ(changed(), 0);

    /* Held until it repeats, then into the band: the repeat ends there. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XP, row_y(2));
    idle(0.99f);
    setting_id_t ids[64];
    settings_in_category(SET_CAT_IFACE, ids, 64);
    const float held = settings_get(ids[2]);
    CHECK(held != s_before[ids[2]]);
    finger(FEED_LONE, XP, row_y(2) - 100);
    finger(FEED_LONE, XP, UI_BAND_H - 8);
    idle(3.0f);
    lift(FEED_LONE);
    idle(3.0f);
    CHECK(settings_get(ids[2]) == held);

    /* The same contact back in the body is the router's: it steps nothing.
     * The next one is a tap. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 4);
    finger(FEED_LONE, XM, UI_BAND_H - 8);
    finger(FEED_LONE, XM, LIST_Y + 4);
    idle(1.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
    feed_tap(FEED_LONE, XM, LIST_Y + 4);
    CHECK_EQ(changed(), 1);
}

/* The same release on the tiles and on the strip's buttons: a tile and
 * RESET CATEGORY act on a release the finger made. */
TEST_CASE(a_button_carried_into_the_band_is_not_pressed)
{
    gesture_screen(LIST_CAT);
    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);

    /* The other category's tile, into the band in one report. */
    feed_tap(FEED_LONE, home_x(), home_y());
    finger(FEED_LONE, tile_x(SET_CAT_APP), tile_y(SET_CAT_APP));
    finger(FEED_LONE, tile_x(SET_CAT_APP), UI_BAND_H - 8);
    lift(FEED_LONE);
    CHECK_EQ(settings_screen_category(), -1);
    feed_tap(FEED_LONE, tile_x(LIST_CAT), tile_y(LIST_CAT));
    feed_tap(FEED_LONE, XP, row_y(0));
    CHECK(settings_get(ids[0]) != s_before[ids[0]]);  /* LIST_CAT open */
    CHECK_EQ(changed(), 1);

    /* The tracker's own release, of a jump: RESET CATEGORY pressed, the
     * contact next reported 300 px away. */
    finger(FEED_LONE, RESET_X + 100, RESET_Y + RESET_H / 2);
    finger(FEED_LONE, XL, RESET_Y - 200);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 1);                   /* not reset */
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* And a tap on it resets. */
    feed_tap(FEED_LONE, RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(changed(), 0);
}

/* The columns' outer edges, and the scroll bar 19 px right of "+". */
TEST_CASE(a_drag_at_the_edge_of_the_plus_column_and_on_the_scroll_bar)
{
    gesture_screen(SET_CAT_IFACE);
    swipe(PLUS_X + BTN_W - 1, LIST_Y + LIST_H - 8, PLUS_X + BTN_W - 1,
          LIST_Y + LIST_H - 208, 9);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_screen_scroll(NULL), 200);

    gesture_screen(SET_CAT_IFACE);
    swipe(789, LIST_Y + LIST_H - 8, 789, LIST_Y + LIST_H - 208, 9);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
}

/* A press whose release is lost, or that the screen is left under, holds
 * nothing afterwards: no repeat, and the next touch is taken. */
TEST_CASE(a_key_press_that_loses_its_release_stops_and_holds_nothing)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(KEY_ID));
    finger(0, XM, y);
    idle(0.99f);
    CHECK_EQ(steps_off(KEY_ID), -5);
    ui_router_cancel_gestures();
    idle(2.0f);
    CHECK_EQ(steps_off(KEY_ID), -5);
    lift(0);
    CHECK_EQ(steps_off(KEY_ID), -5);
    feed_tap(1, XM, y);
    CHECK_EQ(steps_off(KEY_ID), -6);

    /* Left under the finger and entered again: on the tiles, and the
     * category's tile opens the list where it was. */
    finger(0, XM, y);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    idle(2.0f);
    lift(0);
    CHECK_EQ(settings_screen_category(), -1);
    feed_tap(1, tile_x(SET_CAT_IFACE), tile_y(SET_CAT_IFACE));
    CHECK_EQ(steps_off(KEY_ID), -6);
    feed_tap(1, XM, y);
    CHECK_EQ(steps_off(KEY_ID), -7);
}

int main(void)
{
    RUN(a_swipe_that_starts_on_a_key_column_scrolls_and_changes_no_value);
    RUN(a_swipe_beside_the_key_columns_scrolls_and_changes_no_value);
    RUN(two_scrolls_from_the_bottom_row_leave_the_shunt_and_the_pins);
    RUN(every_list_at_its_end_shows_its_last_row_whole);
    RUN(a_tap_steps_each_kind_of_key_once);
    RUN(a_held_key_repeats_five_times_in_one_second_and_38_in_three);
    RUN(a_stalled_frame_adds_at_most_the_capped_steps_to_a_held_key);
    RUN(the_repeat_starts_at_450_ms);
    RUN(a_hold_that_turns_into_a_swipe_stops_repeating_at_the_slop);
    RUN(a_tap_may_move_8_px_and_not_9);
    RUN(a_press_that_slides_off_a_key_sideways_steps_nothing);
    RUN(the_gap_between_two_rows_is_no_key);
    RUN(a_second_finger_does_nothing_on_the_setup_screen);
    RUN(a_contact_that_drops_out_of_a_swipe_changes_no_value);
    RUN(a_flick_the_tracker_splits_changes_no_value);
    RUN(a_category_takes_the_press_the_tracker_made_for_a_jump);
    RUN(a_key_carried_into_the_band_steps_nothing_and_never_repeats);
    RUN(a_button_carried_into_the_band_is_not_pressed);
    RUN(a_drag_at_the_edge_of_the_plus_column_and_on_the_scroll_bar);
    RUN(a_key_press_that_loses_its_release_stops_and_holds_nothing);
    return test_summary("settings_gesture");
}
