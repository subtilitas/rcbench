/**
 * @file test_nav_bubble.c
 * @brief The band's chips and their bubbles: what each opens, that none covers
 * STOP, and how each follows the bits it names.
 *
 * Cases:
 * - the_armed_chip_names_the_role
 * - the_band_renders_the_role_of_the_arm
 * - a_tap_on_each_chip_opens_its_bubble
 * - a_slide_off_a_chip_opens_nothing
 * - a_tap_closes_the_bubble_and_does_nothing_else
 * - a_tap_on_another_chip_moves_the_bubble_to_it
 * - stop_works_through_an_open_bubble
 * - the_bubble_never_covers_stop_and_fits_in_both_languages
 * - the_widest_chip_row_clears_the_home_tag
 * - the_fault_bubble_follows_the_bits_while_open
 * - a_bubble_whose_chip_goes_closes
 * - the_link_bubble_follows_the_link
 * - the_inject_bubble_explains_the_injector
 * - the_armed_bubble_follows_the_roles
 * - the_link_bubble_carries_the_refused_frames
 * - the_refused_line_follows_while_open
 * - a_bubble_follows_its_chip_when_the_band_moves
 * - a_closed_bubble_leaves_no_pixels_behind
 * - leaving_the_screen_closes_the_bubble
 * - opening_a_bubble_releases_the_screens_contacts
 * - a_screen_without_a_band_opens_no_bubble
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

/* The band's chip names the role an arm covers: the motor's, the servo's,
 * or ARMED alone for both, as an arm with no link names no role. */
TEST_CASE(the_armed_chip_names_the_role)
{
    CHECK_STR_EQ(ui_band_armed_label(UI_ARMED_MOTOR), "ARMED MOTOR");
    CHECK_STR_EQ(ui_band_armed_label(UI_ARMED_SERVO), "ARMED SERVO");
    CHECK_STR_EQ(ui_band_armed_label(UI_ARMED_MOTOR | UI_ARMED_SERVO),
                 "ARMED");
    CHECK_STR_EQ(ui_band_armed_label(0u), "ARMED");
    CHECK_STR_EQ(ui_band_armed_label(0xF0u | UI_ARMED_SERVO), "ARMED SERVO");
}

/* The band draws the label, not only the label function returns it: an arm
 * for one role paints a wider chip than ARMED alone, and the two roles paint
 * different ones. */
static void band_look(uint8_t roles, int *danger, uint32_t *hash)
{
    const ui_bench_status_t st = {
        .link_up = true, .armed = true, .armed_roles = roles,
        .run_seconds = 0, .mode = NULL,
    };
    fresh();
    to_overview();
    ui_router_set_status(&st);
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    *danger = 0;
    *hash   = 2166136261u;
    for (int y = 0; y < UI_BAND_H; ++y) {
        for (int x = 0; x < W; ++x) {
            const gfx_color_t px = fb[(size_t)y * W + x];
            if (px == ui_theme_color(UI_C_DANGER)) { ++*danger; }
            *hash = (*hash ^ (uint32_t)px) * 16777619u;
        }
    }
}

TEST_CASE(the_band_renders_the_role_of_the_arm)
{
    int d_none, d_motor, d_servo, d_both;
    uint32_t h_none, h_motor, h_servo, h_both;
    band_look(0u, &d_none, &h_none);
    band_look(UI_ARMED_MOTOR, &d_motor, &h_motor);
    band_look(UI_ARMED_SERVO, &d_servo, &h_servo);
    band_look(UI_ARMED_MOTOR | UI_ARMED_SERVO, &d_both, &h_both);
    CHECK(d_motor > d_none);
    CHECK(d_servo > d_none);
    CHECK_EQ(d_both, d_none);
    CHECK_EQ(h_both, h_none);
    CHECK(h_motor != h_none);
    CHECK(h_servo != h_none);
    CHECK(h_motor != h_servo);
}

/* ------------------------------------------------------- help bubbles */

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* Every chip on the band at once: the link up, BENCH, a fault, SAFE and
 * a run clock. */
static const ui_bench_status_t k_all_chips = {
    .link_up = true, .armed = false, .faults = 0x10, .run_seconds = 5,
    .mode = "BENCH",
};

/* A tap at the middle of @p chip as @p st lays the band out. */
static void tap_chip(const ui_bench_status_t *st, ui_band_chip_t chip)
{
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(st, chip, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* The menu with @p st on the band and @p chip's bubble open. */
static void open_on_overview(const ui_bench_status_t *st, ui_band_chip_t chip)
{
    fresh();
    to_overview();
    ui_router_set_status(st);
    tap_chip(st, chip);
}

TEST_CASE(a_tap_on_each_chip_opens_its_bubble)
{
    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        open_on_overview(&k_all_chips, (ui_band_chip_t)chip);
        CHECK_EQ(ui_router_help(), (ui_band_chip_t)chip);
        gfx_rect_t r;
        CHECK(ui_router_help_rect(&r));
        /* Under the band, in the panel. */
        CHECK(r.y >= UI_BAND_H);
        CHECK(r.x >= 0 && r.x + r.w <= W);
        /* Drawn: the bubble's fill where the menu has its background. */
        ui_router_render(&cv, 0);
        CHECK_EQ(fb[(size_t)(r.y + r.h - 4) * W + r.x + r.w / 2],
                 ui_theme_color(UI_C_PANEL));
    }
}

/* The DOWN that lands on a chip and the release that leaves it open
 * nothing; nor does a tap past the chips' ends. */
TEST_CASE(a_slide_off_a_chip_opens_nothing)
{
    fresh();
    to_overview();
    ui_router_set_status(&k_all_chips);
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_LINK, &r));
    touch(r.x + 4, 20, TOUCH_EVENT_DOWN, 1);
    touch(r.x - 40, 20, TOUCH_EVENT_MOVE, 1);
    touch(r.x - 40, 20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    /* From one chip to the next. */
    gfx_rect_t m;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_MODE, &m));
    touch(r.x + 4, 20, TOUCH_EVENT_DOWN, 1);
    touch(m.x + 4, 20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    /* A gesture cancelled between the DOWN and the UP. */
    touch(r.x + 4, 20, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    touch(r.x + 4, 20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    /* The band between the wordmark and the first chip. */
    tap(r.x - 20, 20);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* Any tap closes the bubble and does nothing else: not the tile under it,
 * not the chip it lands on, not the alert's x. */
TEST_CASE(a_tap_closes_the_bubble_and_does_nothing_else)
{
    open_on_overview(&k_all_chips, UI_BAND_CHIP_LINK);
    const uint32_t sent = ui_router_dispatched();
    tap(80, UI_BAND_H + 100);                       /* MOTOR & ESC's tile */
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_EQ(ui_router_dispatched(), sent);

    /* A tap on the open bubble's own chip closes it and opens nothing. */
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_LINK);
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    /* The alert stays, and so does the home tag's screen. */
    ui_router_goto(SCREEN_ANALYSER);
    ui_router_set_alert("a passing alert");
    tap_chip(&k_all_chips, UI_BAND_CHIP_ARMED);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_ARMED);
    tap(W / 2, H - 10);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK(ui_router_alert() != NULL);
    tap_chip(&k_all_chips, UI_BAND_CHIP_ARMED);
    tap(UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_ANALYSER);
    CHECK(!ui_router_take_stop());

    /* The closing contact's MOVE and UP reach no screen either. */
    tap_chip(&k_all_chips, UI_BAND_CHIP_ARMED);
    const uint32_t before = ui_router_dispatched();
    touch(300, 300, TOUCH_EVENT_DOWN, 3);
    touch(320, 310, TOUCH_EVENT_MOVE, 3);
    touch(320, 310, TOUCH_EVENT_UP, 3);
    CHECK_EQ(ui_router_dispatched(), before);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* With a bubble open, one tap on another chip closes it and opens that
 * chip's: the DOWN closes, the release on the same chip opens.  A DOWN on
 * the other chip that slides off it, or a gesture cancelled under it,
 * leaves no bubble open.  Every chip to every other one. */
TEST_CASE(a_tap_on_another_chip_moves_the_bubble_to_it)
{
    for (int from = UI_BAND_CHIP_LINK; from < UI_BAND_CHIP_COUNT; ++from) {
        for (int to = UI_BAND_CHIP_LINK; to < UI_BAND_CHIP_COUNT; ++to) {
            if (to == from) {
                continue;
            }
            open_on_overview(&k_all_chips, (ui_band_chip_t)from);
            REQUIRE(ui_router_help() == (ui_band_chip_t)from);
            const uint32_t sent = ui_router_dispatched();
            gfx_rect_t r;
            REQUIRE(ui_band_chip_rect(&k_all_chips, (ui_band_chip_t)to, &r));
            touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
            /* Closed at the DOWN, as any other closing tap. */
            CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
            touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
            CHECK_EQ(ui_router_help(), (ui_band_chip_t)to);
            /* Nothing reached the screen. */
            CHECK_EQ(ui_router_dispatched(), sent);
            CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        }
    }

    /* The DOWN on another chip and a release beside the band: closed,
     * nothing opened. */
    open_on_overview(&k_all_chips, UI_BAND_CHIP_LINK);
    gfx_rect_t m;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_MODE, &m));
    touch(m.x + 4, m.y + m.h / 2, TOUCH_EVENT_DOWN, 1);
    touch(m.x + 4, UI_BAND_H + 120, TOUCH_EVENT_MOVE, 1);
    touch(m.x + 4, UI_BAND_H + 120, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    /* A gesture cancelled between the DOWN and the UP. */
    open_on_overview(&k_all_chips, UI_BAND_CHIP_LINK);
    touch(m.x + 4, m.y + m.h / 2, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    touch(m.x + 4, m.y + m.h / 2, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    /* Then a tap on the new bubble's chip closes it, and one on the band
     * beside the chips too. */
    open_on_overview(&k_all_chips, UI_BAND_CHIP_LINK);
    tap_chip(&k_all_chips, UI_BAND_CHIP_MODE);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_MODE);
    tap_chip(&k_all_chips, UI_BAND_CHIP_MODE);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    tap_chip(&k_all_chips, UI_BAND_CHIP_MODE);
    REQUIRE(ui_router_help() == UI_BAND_CHIP_MODE);
    gfx_rect_t l;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_LINK, &l));
    tap(l.x - 20, 20);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* STOP is not under the bubble's rule: its DOWN presses it at once, the
 * bubble closes with it, and the release latches the stop. */
TEST_CASE(stop_works_through_an_open_bubble)
{
    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        open_on_overview(&k_all_chips, (ui_band_chip_t)chip);
        const gfx_rect_t r = ui_band_stop_rect();
        touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
        CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
        /* Pressed: drawn lighter than the button at rest. */
        ui_router_render(&cv, 0);
        CHECK(fb[(size_t)(r.y + 4) * W + r.x + 10]
              != ui_theme_color(UI_C_DANGER));
        touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
        CHECK(ui_router_take_stop());
    }
}

/* Every bubble in every state of its chip, both languages: clear of STOP,
 * inside the panel, and above the alert strip, so the tallest text fits. */
static void bubble_in_bounds(const ui_bench_status_t *st)
{
    const gfx_rect_t stop = ui_band_stop_rect();
    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        gfx_rect_t r;
        if (!ui_band_help_rect(st, (ui_band_chip_t)chip, &r)) {
            continue;
        }
        const bool apart = r.y >= stop.y + stop.h || r.x >= stop.x + stop.w
                           || r.x + r.w <= stop.x || r.y + r.h <= stop.y;
        if (!apart || r.x < 0 || r.x + r.w > W || r.y < UI_BAND_H
            || r.y + r.h > UI_ALERT_Y) {
            T_FAIL("chip %d, faults %04X, language %d: bubble %d,%d %dx%d",
                   chip, (unsigned)st->faults, (int)ui_text_language(),
                   r.x, r.y, r.w, r.h);
            return;
        }
    }
}

TEST_CASE(the_bubble_never_covers_stop_and_fits_in_both_languages)
{
    static const uint16_t faults[] = { 0x01, 0x10, 0x40, 0x51, 0x7F, 0xFF,
                                       0x80, 0xFFFF };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t f = 0; f < sizeof(faults) / sizeof(faults[0]); ++f) {
            for (int armed = 0; armed < 2; ++armed) {
                for (uint8_t roles = 0; roles < 4u; ++roles) {
                    for (int link = 0; link < 2; ++link) {
                        ui_bench_status_t st = k_all_chips;
                        st.faults      = faults[f];
                        st.armed       = armed != 0;
                        st.armed_roles = roles;
                        st.link_up     = link != 0;
                        st.mode        = ui_band_mode(st.link_up);
                        st.run_seconds = 3600u * 9u + 59u;
                        st.link_refused = UINT32_MAX;
                        bubble_in_bounds(&st);
                        st.mode        = ui_band_mode_inject();
                        bubble_in_bounds(&st);
                    }
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The widest row of chips under an hour, MOTOR & ESC ARMED in SIM, starts
 * right of that screen's home tag in both languages. */
/* The leftmost chip's x for @p st. */
static int chips_left(const ui_bench_status_t *st)
{
    int left = W;
    for (int chip = UI_BAND_CHIP_NONE + 1; chip < UI_BAND_CHIP_COUNT; ++chip) {
        gfx_rect_t r;
        if (ui_band_chip_rect(st, (ui_band_chip_t)chip, &r) && r.x < left) {
            left = r.x;
        }
    }
    return left;
}

/*
 * Every screen with the band and a home tag (all but the splash, the menu,
 * the bus-fault screen and the panel's own update, as in ui_router.c), every
 * row of chips the band can show, in each language: the row starts right of
 * the tag.  The link down means SIM, no fault and the arm local; a clock of
 * minutes, of hours, of 10 hours and of 99.  MOTOR & ESC's tag is the
 * widest and ends at x 268; the widest row, ARMED with a FAULT code in
 * German, starts at x 276.
 */
TEST_CASE(the_widest_chip_row_clears_the_home_tag)
{
    const uint32_t runs[] = { 0u, 59u * 60u + 59u, 3600u,
                              9u * 3600u + 59u * 60u + 59u, 10u * 3600u,
                              99u * 3600u + 59u * 60u + 59u };
    const uint8_t roles[] = { 0u, UI_ARMED_MOTOR, UI_ARMED_SERVO,
                              UI_ARMED_MOTOR | UI_ARMED_SERVO };
    int widest = 0;

    for (int id = SCREEN_MOTOR; id < SCREEN_COUNT; ++id) {
        if (id == SCREEN_BUSFAULT || id == SCREEN_UPDATE) {
            continue;
        }
        const gfx_rect_t tag = ui_home_tag_rect(
            ui_router_title((ui_screen_id_t)id));
        const int end = tag.x + tag.w;
        widest = end > widest ? end : widest;
        for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
            ui_text_set_language((ui_lang_t)lang);
            for (int up = 0; up < 2; ++up) {
                for (int armed = 0; armed < 2; ++armed) {
                    for (int ro = 0; ro < 4; ++ro) {
                        for (int fault = 0; fault < 1 + up; ++fault) {
                            for (size_t k = 0; k < sizeof(runs) / sizeof(runs[0]);
                                 ++k) {
                                if (!armed && ro > 0) {
                                    continue;
                                }
                                ui_bench_status_t st;
                                memset(&st, 0, sizeof(st));
                                st.link_up     = up != 0;
                                st.mode        = ui_band_mode(up != 0);
                                st.armed       = armed != 0;
                                st.armed_roles = roles[ro];
                                st.faults      = fault ? 0x7Fu : 0u;
                                st.run_seconds = runs[k];
                                const int left = chips_left(&st);
                                if (left < end + 6) {
                                    T_FAIL("screen %d, language %d, link %d, "
                                           "armed %d roles %u, faults %d, "
                                           "run %lu s: chips from x %d, the "
                                           "tag ends at %d", id, lang, up,
                                           armed, (unsigned)roles[ro], fault,
                                           (unsigned long)runs[k], left, end);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    /* MOTOR & ESC's tag is the widest; every other ends at x 248 or
     * less. */
    CHECK_EQ(widest, 268);
    CHECK_EQ(widest, ui_home_tag_rect(ui_router_title(SCREEN_MOTOR)).x
                     + ui_home_tag_rect(ui_router_title(SCREEN_MOTOR)).w);
    for (int id = SCREEN_MOTOR + 1; id < SCREEN_COUNT; ++id) {
        if (id == SCREEN_BUSFAULT || id == SCREEN_UPDATE) {
            continue;
        }
        const gfx_rect_t tag = ui_home_tag_rect(
            ui_router_title((ui_screen_id_t)id));
        if (tag.x + tag.w > 248) {
            T_FAIL("screen %d: its tag ends at x %d", id, tag.x + tag.w);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The framebuffer of @p chip's bubble opened on the menu with @p first and
 * then shown with @p then, and of the bubble opened with @p then from the
 * start.  The two match when the bubble follows the state: same text, same
 * place, and nothing left of what it was.
 */
static uint32_t frame_hash(void)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        h = (h ^ (uint32_t)fb[i]) * 16777619u;
    }
    return h;
}

static void follows(const ui_bench_status_t *first,
                    const ui_bench_status_t *then, ui_band_chip_t chip)
{
    open_on_overview(then, chip);
    REQUIRE(ui_router_help() == chip);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    const uint32_t want = frame_hash();

    open_on_overview(first, chip);
    REQUIRE(ui_router_help() == chip);
    ui_router_render(&cv, 0);
    ui_router_set_status(then);
    CHECK_EQ(ui_router_help(), chip);
    ui_router_render(&cv, 0);
    CHECK_EQ(frame_hash(), want);
}

TEST_CASE(the_fault_bubble_follows_the_bits_while_open)
{
    ui_bench_status_t a = k_all_chips, b = k_all_chips;
    a.faults = 0x10;
    b.faults = 0x11;                       /* LINK_SILENT appears */
    follows(&a, &b, UI_BAND_CHIP_FAULT);
    follows(&b, &a, UI_BAND_CHIP_FAULT);   /* and goes */
    b.faults = 0x7F;                       /* all seven at once */
    follows(&a, &b, UI_BAND_CHIP_FAULT);
    follows(&b, &a, UI_BAND_CHIP_FAULT);
    b.faults = 0x90;                       /* one this panel has no name for */
    follows(&a, &b, UI_BAND_CHIP_FAULT);
    b.faults = 0x0100;
    follows(&a, &b, UI_BAND_CHIP_FAULT);

    /* More bits, a taller bubble; an unknown bit says so with its advice. */
    gfx_rect_t r1, r2, r7, ru;
    ui_bench_status_t st = k_all_chips;
    st.faults = 0x01;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &r1));
    st.faults = 0x11;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &r2));
    st.faults = 0x7F;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &r7));
    st.faults = 0x80;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &ru));
    CHECK(r2.h > r1.h);
    CHECK(r7.h > r2.h);
    CHECK(ru.h > 30 + 10 + 18);           /* two paragraphs, not none */
}

/* The bubble closes with its chip: the faults clear, or the link goes and
 * takes the bitmap with it. */
TEST_CASE(a_bubble_whose_chip_goes_closes)
{
    open_on_overview(&k_all_chips, UI_BAND_CHIP_FAULT);
    ui_router_render(&cv, 0);
    ui_bench_status_t st = k_all_chips;
    st.faults = 0;
    ui_router_set_status(&st);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    /* The clock goes when a run under a second ends. */
    st = k_all_chips;
    st.armed = true;
    st.run_seconds = 0;
    open_on_overview(&st, UI_BAND_CHIP_CLOCK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_CLOCK);
    st.armed = false;
    ui_router_set_status(&st);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

TEST_CASE(the_link_bubble_follows_the_link)
{
    ui_bench_status_t up = k_all_chips, down = k_all_chips;
    down.link_up = false;
    down.faults  = 0;                 /* the panel shows none without a link */
    down.mode    = ui_band_mode(false);
    up.mode      = ui_band_mode(true);
    follows(&up, &down, UI_BAND_CHIP_LINK);
    follows(&down, &up, UI_BAND_CHIP_LINK);
    follows(&up, &down, UI_BAND_CHIP_MODE);
    follows(&down, &up, UI_BAND_CHIP_MODE);
}

/*
 * INJECT, the mode a build with RCBENCH_TOUCH_INJECTOR names while a touch
 * injector is attached, has both its paragraphs under its title in both
 * languages; a mode string the band has no text for is its title alone.
 * The floor is each paragraph's cells over a line's 60, at 21 px a line
 * and 4 px between the two (ui_band.c's HELP_PITCH and HELP_PARA): a
 * bubble that drops either paragraph is shorter.
 */
TEST_CASE(the_inject_bubble_explains_the_injector)
{
    const int title_only = 30 + 10;   /* HELP_TOP + HELP_BOTTOM */
    const int cells = 60, pitch = 21, para = 4;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        int floor_h = title_only + para;
        floor_h += pitch * ((gfx_text_cells(ui_tr(TX_BH_MODE_INJECT))
                             + cells - 1) / cells);
        floor_h += pitch * ((gfx_text_cells(ui_tr(TX_BH_MODE_INJECT_LOST))
                             + cells - 1) / cells);
        for (int link = 0; link < 2; ++link) {
            ui_bench_status_t st = k_all_chips;
            st.link_up = link != 0;
            st.mode    = ui_band_mode_inject();
            gfx_rect_t r;
            REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_MODE, &r));
            if (r.h < floor_h) {
                T_FAIL("language %d, link %d: INJECT's bubble is %d px, "
                       "under the %d px its two paragraphs take",
                       lang, link, r.h, floor_h);
            }
            st.mode = "OTHER";
            REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_MODE, &r));
            CHECK_EQ(r.h, title_only);
        }
        /* Each paragraph whole in the bubble's 640-byte buffer. */
        CHECK(strlen(ui_tr(TX_BH_MODE_INJECT)) < 640u);
        CHECK(strlen(ui_tr(TX_BH_MODE_INJECT_LOST)) < 640u);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(ui_band_mode_inject(), "INJECT");
}

TEST_CASE(the_armed_bubble_follows_the_roles)
{
    static const uint8_t roles[] = {
        0u, UI_ARMED_MOTOR, UI_ARMED_SERVO, UI_ARMED_MOTOR | UI_ARMED_SERVO,
    };
    ui_bench_status_t safe = k_all_chips;
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
        ui_bench_status_t armed = k_all_chips;
        armed.armed       = true;
        armed.armed_roles = roles[i];
        follows(&safe, &armed, UI_BAND_CHIP_ARMED);
        follows(&armed, &safe, UI_BAND_CHIP_ARMED);
        /* With no link the arm runs the model, and the bubble says so. */
        ui_bench_status_t sim = armed;
        sim.link_up = false;
        sim.faults  = 0;
        sim.mode    = ui_band_mode(false);
        follows(&armed, &sim, UI_BAND_CHIP_ARMED);
        gfx_rect_t a, b;
        REQUIRE(ui_band_help_rect(&armed, UI_BAND_CHIP_ARMED, &a));
        REQUIRE(ui_band_help_rect(&sim, UI_BAND_CHIP_ARMED, &b));
        CHECK(b.h > a.h);
    }
    /* The three roles read three ways. */
    ui_bench_status_t m = k_all_chips, v = k_all_chips, all = k_all_chips;
    m.armed = v.armed = all.armed = true;
    m.armed_roles = UI_ARMED_MOTOR;
    v.armed_roles = UI_ARMED_SERVO;
    all.armed_roles = 0u;
    uint32_t hm, hv, ha;
    open_on_overview(&m, UI_BAND_CHIP_ARMED);
    ui_router_render(&cv, 0);
    hm = frame_hash();
    open_on_overview(&v, UI_BAND_CHIP_ARMED);
    ui_router_render(&cv, 0);
    hv = frame_hash();
    open_on_overview(&all, UI_BAND_CHIP_ARMED);
    ui_router_render(&cv, 0);
    ha = frame_hash();
    CHECK(hm != hv);
    CHECK(hm != ha);
    CHECK(hv != ha);
}

/*
 * The LINK bubble carries the IOmcu's refused frames as MOTOR & ESC's
 * header strip has them, ui_link_refused_label()'s line, while the link is
 * up and the count is above 0.
 */
TEST_CASE(the_link_bubble_carries_the_refused_frames)
{
    ui_bench_status_t none = k_all_chips, one = k_all_chips,
                      most = k_all_chips, down = k_all_chips;
    none.link_refused = 0u;
    one.link_refused  = 1u;
    most.link_refused = UINT32_MAX;
    down.link_up   = false;
    down.faults    = 0;
    down.mode      = ui_band_mode(false);
    ui_bench_status_t down_n = down;
    down_n.link_refused = 37u;

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        gfx_rect_t r0, r1, rm, rd, rdn;
        REQUIRE(ui_band_help_rect(&none, UI_BAND_CHIP_LINK, &r0));
        REQUIRE(ui_band_help_rect(&one, UI_BAND_CHIP_LINK, &r1));
        REQUIRE(ui_band_help_rect(&most, UI_BAND_CHIP_LINK, &rm));
        REQUIRE(ui_band_help_rect(&down, UI_BAND_CHIP_LINK, &rd));
        REQUIRE(ui_band_help_rect(&down_n, UI_BAND_CHIP_LINK, &rdn));
        /* One line more at 1, and the widest count still one line. */
        CHECK_EQ(r1.h, r0.h + 21 + 4);   /* HELP_PITCH, HELP_PARA */
        CHECK_EQ(rm.h, r1.h);
        /* None with the link down, whatever the count. */
        CHECK_EQ(rdn.h, rd.h);

        /* The line drawn is the label, in the language showing. */
        char label[64];
        REQUIRE(ui_link_refused_label(&most, label, sizeof(label)));
        CHECK(gfx_text_cells(label)
              <= (rm.w - 32) / gfx_text_width(UI_FONT_LABEL, "M", 1));
    }
    ui_text_set_language(UI_LANG_EN);

    /* The link down draws the same bubble with a count as without. */
    open_on_overview(&down, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    const uint32_t without = frame_hash();
    open_on_overview(&down_n, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    CHECK_EQ(frame_hash(), without);

    /* Each count draws its own number. */
    open_on_overview(&one, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    const uint32_t at_one = frame_hash();
    open_on_overview(&most, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    CHECK(frame_hash() != at_one);
}

/* The count follows while the bubble is open: the line appears, its number
 * moves, it goes with the link. */
TEST_CASE(the_refused_line_follows_while_open)
{
    ui_bench_status_t none = k_all_chips, one = k_all_chips,
                      many = k_all_chips, down = k_all_chips;
    one.link_refused  = 1u;
    many.link_refused = 37u;
    down.link_up      = false;
    down.faults       = 0;
    down.mode         = ui_band_mode(false);
    down.link_refused = 37u;
    follows(&none, &one, UI_BAND_CHIP_LINK);   /* the line appears */
    follows(&one, &many, UI_BAND_CHIP_LINK);   /* the number moves */
    follows(&many, &none, UI_BAND_CHIP_LINK);  /* the count reads 0 again */
    follows(&many, &down, UI_BAND_CHIP_LINK);  /* the link goes */
    follows(&down, &many, UI_BAND_CHIP_LINK);
}

/* The clock appears while the LINK bubble is open and moves every chip
 * left; the bubble moves with its chip and leaves nothing behind. */
TEST_CASE(a_bubble_follows_its_chip_when_the_band_moves)
{
    ui_bench_status_t still = k_all_chips, run = k_all_chips;
    still.run_seconds = 0;
    run.run_seconds   = 3600u * 2u;    /* H:MM:SS, the widest clock */
    gfx_rect_t a, b;
    REQUIRE(ui_band_chip_rect(&still, UI_BAND_CHIP_MODE, &a));
    REQUIRE(ui_band_chip_rect(&run, UI_BAND_CHIP_MODE, &b));
    REQUIRE(a.x != b.x);
    follows(&still, &run, UI_BAND_CHIP_MODE);
    follows(&run, &still, UI_BAND_CHIP_MODE);
    follows(&still, &run, UI_BAND_CHIP_FAULT);
    follows(&still, &run, UI_BAND_CHIP_ARMED);
}

/* Two framebuffers, as on the panel: the menu draws its tiles once into
 * each, so pixels a bubble leaves are not painted over by themselves. */
TEST_CASE(a_closed_bubble_leaves_no_pixels_behind)
{
    gfx_color_t *fb1 = calloc((size_t)W * H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb1);
    gfx_canvas_t cv1;
    gfx_canvas_init(&cv1, fb1, W, H, W);

    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        /* The menu never opened. */
        fresh();
        to_overview();
        ui_router_set_status(&k_all_chips);
        ui_router_render(&cv, 0);
        const uint32_t want = frame_hash();

        fresh();
        memset(fb1, 0, (size_t)W * H * sizeof(gfx_color_t));
        to_overview();
        ui_router_set_status(&k_all_chips);
        ui_router_render(&cv, 0);
        ui_router_render(&cv1, 1);
        tap_chip(&k_all_chips, (ui_band_chip_t)chip);
        ui_router_render(&cv, 0);
        ui_router_render(&cv1, 1);
        tap(W / 2, 300);                        /* close */
        ui_router_render(&cv, 0);
        CHECK_EQ(frame_hash(), want);
        ui_router_render(&cv1, 1);
        CHECK_EQ(memcmp(fb1, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    }
    free(fb1);
}

/* Leaving the screen closes the bubble, by HOME's way or any other. */
TEST_CASE(leaving_the_screen_closes_the_bubble)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    ui_router_set_status(&k_all_chips);
    tap_chip(&k_all_chips, UI_BAND_CHIP_FAULT);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_FAULT);
    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    gfx_rect_t r;
    CHECK(!ui_router_help_rect(&r));
}

/* A drag the screen owns when a second finger opens a bubble is released
 * at the edge's way, a release that activates nothing, and the dragging
 * finger reaches the screen no more. */
TEST_CASE(opening_a_bubble_releases_the_screens_contacts)
{
    on_screen(SCREEN_MOTOR);
    ui_router_set_status(&k_all_chips);
    motor_screen_set_armed(true);
    drain_motor();
    touch(300, M_TRACK_Y, TOUCH_EVENT_DOWN, 1);
    touch(360, M_TRACK_Y, TOUCH_EVENT_MOVE, 1);
    const float dragged = motor_screen_throttle();
    CHECK(dragged > 0.0f);
    const uint32_t before = ui_router_dispatched();
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_LINK, &r));
    touch(r.x + 10, 20, TOUCH_EVENT_DOWN, 2);
    touch(r.x + 10, 20, TOUCH_EVENT_UP, 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_dispatched(), before + 1u);   /* the release */
    touch(420, M_TRACK_Y, TOUCH_EVENT_MOVE, 1);
    touch(420, M_TRACK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_dispatched(), before + 1u);
    CHECK_NEAR(motor_screen_throttle(), dragged, 0.01f);
    CHECK(!ui_router_take_stop());
    drain_motor();
}

/* No bubble where there is no band. */
TEST_CASE(a_screen_without_a_band_opens_no_bubble)
{
    fresh();
    ui_router_set_status(&k_all_chips);
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);     /* the splash */
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    to_overview();
    ui_router_goto(SCREEN_BUSFAULT);
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

int main(void)
{
    RUN(the_armed_chip_names_the_role);
    RUN(the_band_renders_the_role_of_the_arm);
    RUN(a_tap_on_each_chip_opens_its_bubble);
    RUN(a_slide_off_a_chip_opens_nothing);
    RUN(a_tap_closes_the_bubble_and_does_nothing_else);
    RUN(a_tap_on_another_chip_moves_the_bubble_to_it);
    RUN(stop_works_through_an_open_bubble);
    RUN(the_bubble_never_covers_stop_and_fits_in_both_languages);
    RUN(the_widest_chip_row_clears_the_home_tag);
    RUN(the_fault_bubble_follows_the_bits_while_open);
    RUN(a_bubble_whose_chip_goes_closes);
    RUN(the_link_bubble_follows_the_link);
    RUN(the_inject_bubble_explains_the_injector);
    RUN(the_armed_bubble_follows_the_roles);
    RUN(the_link_bubble_carries_the_refused_frames);
    RUN(the_refused_line_follows_while_open);
    RUN(a_bubble_follows_its_chip_when_the_band_moves);
    RUN(a_closed_bubble_leaves_no_pixels_behind);
    RUN(leaving_the_screen_closes_the_bubble);
    RUN(opening_a_bubble_releases_the_screens_contacts);
    RUN(a_screen_without_a_band_opens_no_bubble);
    return test_summary("nav_bubble");
}
