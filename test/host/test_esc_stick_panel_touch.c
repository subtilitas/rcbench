/**
 * @file test_esc_stick_panel_touch.c
 * @brief ESC STICK's touch rules: a control acts on a tap and on nothing less,
 * ABORT acts on the press, and a second finger presses nothing.
 *
 * Cases:
 * - a_control_acts_on_a_tap_and_on_nothing_less
 * - defaults_needs_a_tap
 * - a_second_finger_presses_nothing_on_the_programmer
 * - a_press_whose_control_changes_under_it_is_no_tap
 * - abort_acts_on_the_press
 * - abort_is_named_to_the_host_while_a_run_is_under_way
 * - a_manual_step_takes_done_as_a_tap_and_abort_on_the_press
 * - hold_to_run_keeps_its_two_seconds
 * - a_stick_control_released_for_the_finger_is_not_pressed
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"
#include "motor_spots.h"

/* ------------------------------------------------- taps, tracker-fed */

/*
 * The cases below put finger frames through the tracker (touch_feed.h) and
 * hand its events to the screen, or to the router where the band matters.
 * A control is a tap: pressed and released in place it acts, and a finger
 * that crosses it, leaves it or travels past 8 px does nothing.
 */
#define TAP_ID 3

static void on_screen(void)
{
    feed_reset();
    feed_to_screen(scr);
}

static void f_at(int x, int y)    { finger(TAP_ID, x, y + UI_BAND_H); }

static void f_to(int x, int y, int step)
{
    glide(TAP_ID, x, y + UI_BAND_H, step);
}

static void f_lift(void)          { lift(TAP_ID); }

static void at_jazz(void)      { at_steps(); tap(CANCEL_X, HOLD_Y); }

/* Every control of the screen that acts on a tap. */
static const control_t k_controls[] = {
    { "maker row",             at_makers,    ROW_CX, SP_ROW_CY(1) },
    { "list down",             at_makers,    LIST_DN_X, LIST_CY },
    { "search field",          at_makers,    FIND_X, FIND_Y },
    { "search X",              at_searched,  FIND_CLR_X, FIND_Y },
    { "BACK, makers",          at_makers,    BACK_X, BACK_Y },
    { "model row",             at_models,    ROW_CX, -1 },
    { "BACK, models",          at_models,    BACK_X, BACK_Y },
    { "item step up",          at_items,     STEP_UP_X, STEP_CY(2) },
    { "item step down",        at_picked,    STEP_DN_X, STEP_CY(2) },
    { "item page down",        at_items,     PAGE_DN_X, PAGE_CY },
    { "item name",             at_items,     120, STEP_CY(3) },
    { "TIMING",                at_items,     TIMING_X, TIMING_Y },
    { "BACK, items",           at_items,     BACK_X, BACK_Y },
    { "RUN",                   at_picked,    WRITE_X, BTN_CY },
    { "CANCEL, warning",       at_warning,   CANCEL_X, HOLD_Y },
    { "timing step up",        at_timing,    STEP_UP_X, STEP_CY(3) },
    { "timing step down",      at_timing,    STEP_DN_X, STEP_CY(2) },
    { "timing page down",      at_timing,    PAGE_DN_X, PAGE_CY },
    { "timing name",           at_timing,    120, STEP_CY(4) },
    { "DEFAULTS",              at_timing,    READ_X, BTN_CY },
    { "CLOSE",                 at_timing,    WRITE_X, BTN_CY },
    { "BACK, timing",          at_timing,    BACK_X, BACK_Y },
    { "OK, manual steps",      at_steps,     CANCEL_X, HOLD_Y },
    { "MANUAL INTERVENTION",   at_jazz,      HAND_X, HAND_Y },
    { "OK, result",            at_result,    WRITE_X, BTN_CY },
    { "BACK, result",          at_result,    BACK_X, BACK_Y },
};

#define CONTROLS ((int)(sizeof(k_controls) / sizeof(k_controls[0])))

/* The defect: every control acted on the DOWN, so a finger that came down
 * on one and went on to somewhere else had pressed it. */
TEST_CASE(a_control_acts_on_a_tap_and_on_nothing_less)
{
    for (int i = 0; i < CONTROLS; ++i) {
        control_t at = k_controls[i];
        const control_t *c = &at;
        c->page();
        if (at.y < 0) {
            at.y = s_model_y;
        }
        on_screen();
        scr->tick(0.010f);
        const uint32_t was = look();
        int moved = 0;

        /* The press alone, and held a second. */
        f_at(c->x, c->y);
        moved += (look() != was) ? 1 : 0;
        for (int f = 0; f < 100; ++f) {
            scr->tick(0.010f);
        }
        moved += (look() != was) ? 2 : 0;
        /* Carried 60 px away, down the glass and across it, and lifted. */
        f_to(c->x, c->y + 60, 9);
        f_lift();
        moved += (look() != was) ? 4 : 0;
        f_at(c->x, c->y);
        f_to(c->x - 60, c->y, 9);
        f_lift();
        moved += (look() != was) ? 8 : 0;
        /* A swipe that starts 60 px above it and crosses it. */
        f_at(c->x, c->y - 60);
        f_to(c->x, c->y + 60, 9);
        f_lift();
        moved += (look() != was) ? 16 : 0;
        /* Pressed, carried 60 px away and back onto it. */
        f_at(c->x, c->y);
        f_to(c->x + 60, c->y, 9);
        f_to(c->x, c->y, 9);
        f_lift();
        moved += (look() != was) ? 32 : 0;
        /* 9 px from the press, in one report and px by px. */
        f_at(c->x, c->y);
        f_to(c->x + 9, c->y, 9);
        f_lift();
        f_at(c->x, c->y);
        f_to(c->x, c->y + 9, 1);
        f_lift();
        moved += (look() != was) ? 64 : 0;
        if (moved != 0) {
            T_FAIL("%s acted without a tap (%d)", c->name, moved);
        }

        /* A tap acts; so does one that moves 8 px, or wobbles. */
        for (int k = 0; k < 3; ++k) {
            c->page();
            on_screen();
            scr->tick(0.010f);
            const uint32_t before = look();
            f_at(c->x, c->y);
            if (k == 1) {
                f_to(c->x - 8, c->y, 8);
            } else if (k == 2) {
                f_to(c->x + 2, c->y - 3, 1);
                f_to(c->x, c->y, 1);
            }
            if (look() != before) {
                T_FAIL("%s acted on the press (%d)", c->name, k);
            }
            f_lift();
            if (look() == before) {
                T_FAIL("%s did not act on a tap (%d)", c->name, k);
            }
        }
    }
}

/* DEFAULTS resets a category of settings: it takes a tap. */
TEST_CASE(defaults_needs_a_tap)
{
    at_timing();
    on_screen();
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    f_at(READ_X, BTN_CY);                    /* pressed */
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    f_to(READ_X, BTN_CY - 40, 9);            /* and carried off */
    f_lift();
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    f_at(READ_X, BTN_CY - 40);               /* a swipe that ends on it */
    f_to(READ_X, BTN_CY, 9);
    f_lift();
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    f_at(READ_X, BTN_CY);
    f_lift();
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);

    /* A step key of the page, the same. */
    f_at(STEP_UP_X, STEP_CY(2));
    f_to(STEP_UP_X, STEP_CY(2) + 100, 9);
    f_lift();
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);
    f_at(STEP_UP_X, STEP_CY(2));
    f_lift();
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
}

/* One press at a time: a second finger arms nothing, and the first one's
 * release still acts. */
TEST_CASE(a_second_finger_presses_nothing_on_the_programmer)
{
    at_timing();
    on_screen();
    finger(1, STEP_UP_X, STEP_CY(3) + UI_BAND_H);    /* rests on a key */
    finger(2, READ_X, BTN_CY + UI_BAND_H);           /* DEFAULTS */
    lift(2);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    const float gap = settings_get(SET_STICK_GAP_MIN);
    lift(1);
    CHECK(settings_get(SET_STICK_GAP_MIN) != gap);
    /* The glass clear, the next tap is taken whichever id it has. */
    finger(2, READ_X, BTN_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);
}

/* A control that changes under the finger is not the one that was pressed:
 * RUN's place is ABORT's while a run is under way and OK's after it. */
TEST_CASE(a_press_whose_control_changes_under_it_is_no_tap)
{
    at_warning();
    on_screen();
    f_at(CANCEL_X, HOLD_Y);                  /* CANCEL, pressed */
    scr->cancel();                           /* touch lost: nothing held */
    f_lift();
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    const uint32_t open = look();
    /* Pressed, and the warning goes while it is down. */
    f_at(CANCEL_X, HOLD_Y);
    scr->leave();
    scr->enter();
    const uint32_t left = look();
    CHECK(left != open);
    f_lift();
    CHECK_EQ(look(), left);

    /* The list is built again under a finger on one of its rows: VOLTAGE
     * decides which models run.  The row is not opened. */
    at_makers();
    on_screen();
    f_at(ROW_CX, SP_ROW_CY(1));
    settings_set(SET_STICK_V, 5.0f);
    scr->tick(0.010f);
    f_lift();
    CHECK_EQ(esc_stick_panel_level(), 0);
    f_at(ROW_CX, SP_ROW_CY(1));
    f_lift();
    CHECK_EQ(esc_stick_panel_level(), 1);

    /* The search filters the list under a finger on one of its rows: a
     * second finger types a letter.  The row is not opened. */
    at_makers();
    tap(FIND_X, FIND_Y);
    on_screen();
    f_at(100, SP_ROW_CY(1));
    {
        ui_textkey_t k;
        memset(&k, 0, sizeof(k));
        ui_textkey_open_search(&k, k_dock, "", "", 16);
        const gfx_rect_t r = ui_textkey_key_rect(&k, 14);   /* T */
        finger(1, r.x + r.w / 2, r.y + r.h / 2 + UI_BAND_H);
        lift(1);
    }
    CHECK_STR_EQ(esc_stick_panel_search(), "T");
    f_lift();
    CHECK_EQ(esc_stick_panel_level(), 0);
    f_at(100, SP_ROW_CY(0));
    f_lift();
    CHECK_EQ(esc_stick_panel_level(), 1);
}

/* ABORT stops a motor and acts on the press, with no release and whatever
 * else is on the glass. */
TEST_CASE(abort_acts_on_the_press)
{
    for (int k = 0; k < 3; ++k) {
        at_warning();
        hold_for(2.25f);
        static rig_t r;
        rig_start(&r);
        for (int i = 0; i < 12000; ++i) {
            rig_step(&r);                    /* powered, in the menu */
        }
        CHECK(r.on && r.armed);
        const esc_stick_t *run = esc_stick_panel_stick();
        CHECK(esc_stick_running(run));
        on_screen();
        if (k == 0) {
            /* A swipe that crosses ABORT presses nothing. */
            f_at(WRITE_X, BTN_CY - 60);
            f_to(WRITE_X, BTN_CY, 9);
            for (int i = 0; i < 50; ++i) {
                rig_step(&r);
            }
            f_lift();
            CHECK(esc_stick_running(run));
            f_at(WRITE_X, BTN_CY);           /* the press, held */
        } else if (k == 1) {
            /* A finger resting elsewhere does not keep ABORT from acting. */
            finger(1, BACK_X, BACK_Y + UI_BAND_H);
            finger(2, WRITE_X, BTN_CY + UI_BAND_H);
        } else {
            f_at(WRITE_X, BTN_CY);
        }
        rig_run(&r, 1000u);
        CHECK_EQ(run->phase, ESC_STICK_ABORTED);
        CHECK_EQ(run->reason, ESC_STICK_R_USER);
        CHECK(!r.armed);
        CHECK(!r.on);
        /* The finger lifts on what is OK by then: the result stays. */
        const uint32_t shown = look();
        if (k == 1) {
            lift(2);
            lift(1);
        } else {
            f_lift();
        }
        CHECK_EQ(look(), shown);
    }
}

/* The points of the body under the alert band the session names as a
 * stop, every 2 px. */
static int stops_under_the_band(const prog_page_set_t *ps)
{
    int n = 0;
    for (int y = UI_ALERT_Y - UI_BAND_H; y < UI_BODY_H; y += 2) {
        for (int x = 0; x < UI_PANEL_W; x += 2) {
            n += ps->safe_at(x, y) ? 1 : 0;
        }
    }
    return n;
}

/*
 * The session names ABORT to its host while a run is under way, so the
 * router hands a press on it through its help bubble.  ABORT ends 4 px
 * above the alert band (panel y 496 to 551), and nothing under the band is
 * a stop.  Nothing before the run: the warning's
 * HOLD TO RUN and CANCEL are no stop.  On a manual step it is the prompt's
 * ABORT, and the foot row's place is not.  After the run, OK is not.
 */
TEST_CASE(abort_is_named_to_the_host_while_a_run_is_under_way)
{
    const prog_page_set_t *ps = &esc_stick_pages;
    at_warning();
    CHECK(!ps->safe_at(HOLD_X, HOLD_Y));
    CHECK(!ps->safe_at(CANCEL_X, HOLD_Y));
    CHECK(!ps->safe_at(WRITE_X, BTN_CY));
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    for (int i = 0; i < 12000; ++i) {
        rig_step(&r);
    }
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    CHECK(ps->safe_at(WRITE_X, BTN_CY));
    CHECK(!ps->safe_at(WRITE_X, UI_ALERT_Y - UI_BAND_H));
    CHECK_EQ(stops_under_the_band(ps), 0);
    CHECK(!ps->safe_at(BACK_X, BACK_Y));
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    rig_run(&r, 1000u);
    CHECK(!esc_stick_running(esc_stick_panel_stick()));
    CHECK(!ps->safe_at(WRITE_X, BTN_CY));    /* OK */

    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));
    }
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);
    CHECK(ps->safe_at(CANCEL_X, HOLD_Y));
    CHECK(!ps->safe_at(HOLD_X, HOLD_Y));     /* DONE */
    CHECK_EQ(stops_under_the_band(ps), 0);
    CHECK(!ps->safe_at(WRITE_X, BTN_CY));
    tap(CANCEL_X, HOLD_Y);
    rig_run(&r, 1000u);
    CHECK(!ps->safe_at(CANCEL_X, HOLD_Y));
}

/* On a manual step: ABORT on the press, DONE on a tap. */
TEST_CASE(a_manual_step_takes_done_as_a_tap_and_abort_on_the_press)
{
    for (int k = 0; k < 2; ++k) {
        fresh();
        descend_to_jazz();
        tap(CANCEL_X, HOLD_Y);
        for (int i = 0; i < 3; ++i) {
            tap(STEP_UP_X, STEP_CY(0));
        }
        supply_reads_off();
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        static rig_t r;
        rig_start(&r);
        const esc_stick_t *run = esc_stick_panel_stick();
        for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
            rig_step(&r);
        }
        CHECK(run->hand_menu);
        for (uint32_t i = 0; i < ESC_STICK_HAND_MIN_MS; ++i) {
            rig_step(&r);
        }
        esc_sim_hand(&r.sim, r.now);
        on_screen();
        if (k == 0) {
            f_at(HOLD_X, HOLD_Y);            /* DONE, pressed */
            rig_step(&r);
            CHECK(run->hand_menu);
            f_to(HOLD_X, HOLD_Y - 80, 9);    /* and carried off */
            f_lift();
            rig_step(&r);
            CHECK(run->hand_menu);
            f_at(HOLD_X, HOLD_Y);
            f_lift();
            rig_step(&r);
            CHECK(!run->hand_menu);
            CHECK_EQ(run->phase, ESC_STICK_VALUES);
            scr->leave();
        } else {
            f_at(CANCEL_X, HOLD_Y);          /* ABORT, pressed */
            rig_run(&r, 1000u);
            CHECK_EQ(run->phase, ESC_STICK_ABORTED);
            CHECK_EQ(run->reason, ESC_STICK_R_USER);
            f_lift();
        }
    }
}

/* HOLD TO RUN keeps its 2 s, fed as a finger: short of it nothing starts,
 * and a release the tracker makes for a jump is a release. */
TEST_CASE(hold_to_run_keeps_its_two_seconds)
{
    /* In frames of 250 ms, which add up exactly. */
    static const struct { int frames; unsigned runs; } k[] = {
        { 7, 0u }, { 8, 1u }, { 9, 1u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        at_warning();
        on_screen();
        f_at(HOLD_X, HOLD_Y);
        for (int f = 0; f < k[i].frames; ++f) {
            scr->tick(0.25f);
        }
        f_lift();
        scr->tick(0.25f);
        CHECK_EQ(esc_stick_panel_runs(), k[i].runs);
        scr->leave();
    }
    at_warning();
    on_screen();
    f_at(HOLD_X, HOLD_Y);
    for (int f = 0; f < 4; ++f) {
        scr->tick(0.25f);
    }
    f_at(HOLD_X + 300, HOLD_Y - 200);        /* the contact jumps */
    for (int f = 0; f < 8; ++f) {
        scr->tick(0.25f);
    }
    f_lift();
    CHECK_EQ(esc_stick_panel_runs(), 0u);
}

/* MOTOR & ESC's PROGRAM tab and its STICK MENU row, in body coordinates
 * (motor_workbench.c's strip, motor_program_page.c's list). */
#define MO_PROGRAM_TAB_X MO_TAB_X(1)


#define MO_ROW_X         MO_PROG_ROW_X

#define MO_ROW_Y         MO_PROG_ROW_Y

/* Through the router and MOTOR & ESC: a control carried into the band, or
 * ended by the tracker for a jump, is released for the screen and is not
 * pressed. */
TEST_CASE(a_stick_control_released_for_the_finger_is_not_pressed)
{
    fresh();
    ui_router_init();
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    feed_tap(TAP_ID, MO_PROGRAM_TAB_X, MO_TAB_Y + UI_BAND_H);
    feed_tap(TAP_ID, MO_ROW_X, MO_ROW_Y + UI_BAND_H);
    CHECK(motor_screen_session_shown());
    CHECK_EQ(esc_stick_panel_stage(), 1);            /* the makers */
    /* BACK, 45 px under the band's edge, and up into the band. */
    finger(TAP_ID, BACK_X, BACK_Y + UI_BAND_H);
    finger(TAP_ID, BACK_X, UI_BAND_H - 8);
    lift(TAP_ID);
    /* BACK, then the contact reported 300 px away. */
    finger(TAP_ID, BACK_X, BACK_Y + UI_BAND_H);
    finger(TAP_ID, BACK_X + 300, BACK_Y + UI_BAND_H + 360);
    lift(TAP_ID);
    /* Still on the makers; a tap on BACK closes the session to the
     * list. */
    CHECK_EQ(esc_stick_panel_stage(), 1);
    CHECK(motor_screen_session_shown());
    feed_tap(TAP_ID, BACK_X, BACK_Y + UI_BAND_H);
    CHECK_EQ(esc_stick_panel_stage(), 0);
    CHECK(!motor_screen_session_shown());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    ui_router_goto(SCREEN_OVERVIEW);
}

int main(void)
{
    RUN(a_control_acts_on_a_tap_and_on_nothing_less);
    RUN(defaults_needs_a_tap);
    RUN(a_second_finger_presses_nothing_on_the_programmer);
    RUN(a_press_whose_control_changes_under_it_is_no_tap);
    RUN(abort_acts_on_the_press);
    RUN(abort_is_named_to_the_host_while_a_run_is_under_way);
    RUN(a_manual_step_takes_done_as_a_tap_and_abort_on_the_press);
    RUN(hold_to_run_keeps_its_two_seconds);
    RUN(a_stick_control_released_for_the_finger_is_not_pressed);
    return test_summary("esc_stick_panel_touch");
}
