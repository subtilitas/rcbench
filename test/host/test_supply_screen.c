/*
 * What the supply screen decides: the switch's hold and tap, the set points
 * and their snapping, what a stop and a touch loss leave behind, and where a
 * run's trace starts and stops.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "settings.h"
#include "supply.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_color_t *fb2;
static gfx_canvas_t cv;
static gfx_canvas_t cv2;
static const ui_screen_t *scr;

static void drain(void)
{
    supply_cmd_t junk;
    while (supply_screen_poll_cmd(&junk)) { }
}

static void fresh(void)
{
    if (fb == NULL) {
        fb  = calloc((size_t)W * H, sizeof(gfx_color_t));
        fb2 = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    memset(fb2, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    gfx_canvas_init(&cv2, fb2, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    scr = supply_screen();
    scr->reset();
    supply_screen_settings_loaded();
    drain();
}

static void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}
static void tap(int x, int y) { ev(x, y, TOUCH_EVENT_DOWN, 1);
                                ev(x, y, TOUCH_EVENT_UP, 1); }

/*
 * Geometry mirrored from supply_screen.c, screen-local: the router strips the
 * band before a screen sees an event.
 */
#define OUT_X     676          /* OUTPUT ON / OFF, right rail */
#define OUT_Y     318
#define RESET_X   676          /* RESET PEAKS */
#define RESET_Y   402
#define V_DOWN_X  39           /* the set points' fine steps */
#define V_UP_X    519
#define V_ROW_Y   335
#define I_ROW_Y   397
#define TRACK_X   72           /* both tracks: x 72..485 */
#define TRACK_W   414
#define TABLE_X   99           /* the TABLE tab */
#define PLOT_TAB_X 37          /* the PLOT tab */
#define TABLE_Y   11
#define OFF_X     300          /* the plot, clear of every control */
#define OFF_Y     120

/* The overlays, from supply_screen.c: SETTINGS at the strip's right end,
 * CLOSE, the settings' value rows, the rail's cards, the set points' values,
 * and the question's two buttons. */
#define SETB_X    746
#define SETB_Y    11
#define CLOSE_X   487
#define CLOSE_Y   49
#define ROW_L_X   144          /* the left column's rows */
#define ROW_R_X   413          /* the right column's */
#define VMAX_Y    120
#define VSTART_Y  238
#define ISTART_Y  280
#define TRIPI_Y   120
#define CONF_SL_Y 342
#define CONF_KP_Y 386
#define CARD_X    676
#define CARD_V_Y  52
#define CARD_I_Y  113
#define TEXT_X    412
#define TEXT_V_Y  305
#define TEXT_I_Y  367
#define APPLY_X   151
#define DISCARD_X 407
#define ASK_Y     365
#define MODRESET_Y 342         /* RESET PD MINI, left column, SETTINGS */          /* inside both, and on no control without them */

#define TICK_S     0.05f
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static void tick_for(int steps)
{
    for (int i = 0; i < steps; ++i) {
        scr->tick(TICK_S);
    }
}

/* A key of the keypad, where the screen opens it: over the left column. */
static void key(ui_key_t k)
{
    ui_keypad_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, (gfx_rect_t){ 6, 24, 546, 402 }, "", "", 0.0f,
                   0.0f, 1.0f, 2);
    const gfx_rect_t r = ui_keypad_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

static void keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
    key(UI_KEY_OK);
}

static void hold_on(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

/* ---------------------------------------------------------------- switch */

TEST_CASE(the_output_comes_on_with_a_hold_and_only_once)
{
    fresh();
    supply_cmd_t c;
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    CHECK(!supply_screen_poll_cmd(&c));       /* half a hold asks nothing */
    tick_for(HOLD_TICKS);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
    tick_for(HOLD_TICKS);                     /* still down: no second ON */
    CHECK(!supply_screen_poll_cmd(&c));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(a_short_press_on_output_on_does_nothing)
{
    fresh();
    tap(OUT_X, OUT_Y);
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_finger_that_leaves_the_switch_switches_nothing)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, 1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_MOVE, 1);    /* coming back is not resuming */
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(the_output_goes_off_with_a_tap)
{
    fresh();
    supply_screen_set_output(true);
    supply_cmd_t c;
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);

    /* A press that slides off before lifting is not a tap. */
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(the_release_that_ends_the_hold_does_not_switch_off)
{
    /* The supply reports the output on while the finger is still down. */
    fresh();
    supply_cmd_t c;
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));

    /* And after the finger has lifted. */
    fresh();
    hold_on();
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_output(true);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(an_off_outranks_an_on_in_the_same_poll)
{
    fresh();
    supply_cmd_t c;
    hold_on();                         /* ON posted, not yet collected */
    supply_screen_set_output(true);    /* the supply reports it on */
    tap(OUT_X, OUT_Y);                 /* and the operator turns it off */
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);

    /* And an ON completed after an OFF that is still pending: the output
     * went off under it (a stop) before the OFF was collected. */
    fresh();
    supply_screen_set_output(true);
    tap(OUT_X, OUT_Y);
    supply_screen_set_output(false);
    hold_on();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);
}

TEST_CASE(a_second_contact_cannot_steal_the_release)
{
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!supply_screen_poll_cmd(NULL));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

TEST_CASE(a_second_contact_on_another_button_cannot_steal_the_off)
{
    fresh();
    supply_cmd_t c;
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 2);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_UP, 2);
    ev(V_UP_X, V_ROW_Y, TOUCH_EVENT_DOWN, 3);
    ev(V_UP_X, V_ROW_Y, TOUCH_EVENT_UP, 3);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.reset);

    /* And the buttons answer again once the first contact has lifted. */
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.reset);
}

TEST_CASE(a_stop_abandons_the_hold_and_drops_a_posted_on)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    supply_screen_cancel_on();
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* An ON already posted and not collected goes too. */
    hold_on();
    supply_screen_cancel_on();
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_stop_while_the_output_reports_off_strands_no_press)
{
    /* The output goes off under a finger on OUTPUT OFF: the press neither
     * switches it back on nor waits to. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_set_output(false);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_touch_loss_drops_an_on_and_keeps_an_off)
{
    supply_cmd_t c;
    fresh();
    hold_on();
    scr->cancel();
    CHECK(!supply_screen_poll_cmd(&c));

    /* A hold under way when the loss lands asks for nothing. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->cancel();
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(&c));

    /* A press on OUTPUT OFF whose release went missing is an OFF made. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
}

TEST_CASE(leaving_under_a_press_on_output_off_sends_the_off)
{
    /* A second finger on HOME leaves the screen while the first is still
     * on OUTPUT OFF: the OFF it was making is sent, not lost. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    scr->leave();
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    /* And a hold toward ON left the same way asks for nothing. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(leaving_keeps_the_output_and_drops_the_hold)
{
    fresh();
    supply_screen_set_output(true);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));

    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* And the switch comes back unfaded: the screen on return draws as one
     * that was never pressed. */
    fresh();
    scr->leave();
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    scr->render(&cv, 0);
    const int same = memcmp(fb, was, (size_t)W * H * sizeof(gfx_color_t));
    free(was);
    CHECK_EQ(same, 0);
}

TEST_CASE(reset_peaks_posts_its_own_command)
{
    fresh();
    supply_cmd_t c;
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.reset);
    CHECK(!c.on && !c.off);

    /* Slid off before the release: nothing. */
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

/* ------------------------------------------------------------ set points */

TEST_CASE(the_fine_steps_move_a_set_point_by_a_tenth)
{
    fresh();
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);

    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);

    tap(V_DOWN_X, I_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.9f, 1e-4f);

    /* Down past the bottom of the range stops at it. */
    for (int k = 0; k < 40; ++k) {
        tap(V_DOWN_X, I_ROW_Y);
    }
    CHECK_NEAR(supply_screen_set_i(), 0.5f, 1e-4f);

    /* A set point is a level, not a command: nothing is posted. */
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_tap_on_a_track_sets_a_value_the_supply_takes)
{
    fresh();
    tap(TRACK_X + TRACK_W / 2, V_ROW_Y);
    /* About the middle of 3.3 to 21 V, on a 20 mV step above 3.3 V. */
    const float v = supply_screen_set_v();
    CHECK(v > 11.5f && v < 12.7f);
    const float steps = (v - 3.3f) / 0.02f;
    CHECK_NEAR(steps, roundf(steps), 1e-2f);

    /* The current track the same, on its 50 mA step. */
    tap(TRACK_X + TRACK_W - 1, I_ROW_Y);
    CHECK_NEAR(supply_screen_set_i(), 5.0f, 1e-4f);
}

TEST_CASE(new_caps_pull_the_set_points_into_range)
{
    fresh();
    const supply_caps_t narrow = { 5.0f, 5.0f, 0.0f, 0.5f, 3.0f, 0.05f };
    supply_screen_set_caps(&narrow);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    const supply_caps_t low = { 3.3f, 21.0f, 0.02f, 0.5f, 1.0f, 0.05f };
    supply_screen_set_caps(&low);
    CHECK_NEAR(supply_screen_set_i(), 1.0f, 1e-4f);
    supply_screen_set_caps(NULL);
    CHECK(!supply_screen_poll_cmd(NULL));     /* caps are not a command */
}

/* ------------------------------------------------- settings and keypad */

TEST_CASE(the_settings_overlay_sets_a_cap_the_set_points_obey)
{
    fresh();
    tap(SETB_X, SETB_Y);                 /* SETTINGS */
    tap(ROW_L_X, VMAX_Y);                /* VOLTAGE MAX */
    keys("8.4");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 8.4f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().v_max, 8.4f, 1e-4f);
    CHECK(settings_save_asked());
    /* The set point's controls are under the overlay. */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(CLOSE_X, CLOSE_Y);
    for (int k = 0; k < 40; ++k) {
        tap(V_UP_X, V_ROW_Y);
    }
    CHECK_NEAR(supply_screen_set_v(), 8.4f, 1e-4f);
    /* SETTINGS opens and closes. */
    tap(SETB_X, SETB_Y);
    tap(SETB_X, SETB_Y);
    tap(V_DOWN_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 8.3f, 1e-4f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_lowered_cap_brings_the_set_point_and_the_start_down)
{
    fresh();
    tap(SETB_X, SETB_Y);
    tap(ROW_L_X, VSTART_Y);
    keys("12");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 12.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);   /* at a restart only */
    tap(CLOSE_X, CLOSE_Y);
    tap(CARD_X, CARD_V_Y);
    keys("12");
    CHECK_NEAR(supply_screen_set_v(), 12.0f, 1e-4f);
    tap(SETB_X, SETB_Y);
    tap(ROW_L_X, VMAX_Y);
    keys("8");
    CHECK_NEAR(supply_screen_set_v(), 8.0f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 8.0f, 1e-4f);
    /* And the keypad for a start value offers only what the cap allows:
     * 12 V is refused, and the start stays where it was. */
    tap(ROW_L_X, VSTART_Y);
    keys("7");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 7.0f, 1e-4f);
    tap(ROW_L_X, VSTART_Y);
    keys("12");
    key(UI_KEY_CANCEL);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 7.0f, 1e-4f);
}

TEST_CASE(a_switch_in_the_settings_flips_on_a_tap)
{
    fresh();
    tap(SETB_X, SETB_Y);
    tap(ROW_R_X, CONF_SL_Y);
    CHECK(!settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE));
    CHECK(settings_get_bool(SET_SUPPLY_CONFIRM_KEYS));
    tap(ROW_R_X, CONF_KP_Y);
    CHECK(!settings_get_bool(SET_SUPPLY_CONFIRM_KEYS));
    tap(ROW_R_X, CONF_SL_Y);
    CHECK(settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE));
    /* A trip is typed, in amps, and 0 turns it off. */
    tap(ROW_R_X, TRIPI_Y);
    keys("2.5");
    CHECK_NEAR(supply_screen_limits().trip_i, 2.5f, 1e-4f);
    tap(ROW_R_X, TRIPI_Y);
    keys("0");
    CHECK_EQ(supply_screen_limits().trip_i, 0.0f);
}

TEST_CASE(a_start_value_read_over_its_cap_is_corrected_and_kept)
{
    /* Read back over the cap, as a save that reached one key and not the
     * other leaves them: brought down, and a save asked for. */
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 8.0f);
    settings_set(SET_SUPPLY_V_START, 12.0f);
    CHECK(!settings_save_asked());
    supply_screen_settings_loaded();
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 8.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_v(), 8.0f, 1e-4f);
    CHECK(settings_save_asked());
}

TEST_CASE(the_overlay_tells_an_unsaved_setup_change_from_a_waiting_save)
{
    /* A change left on SETUP without SAVE is not a save waiting: nothing
     * will write it.  The overlay's bottom line says which. */
    fresh();
    tap(SETB_X, SETB_Y);
    settings_set(SET_PACK_CELLS, 4.0f);          /* SETUP's, no save asked */
    supply_invalidate();
    scr->render(&cv, 0);
    settings_request_save();
    supply_invalidate();
    scr->render(&cv2, 0);
    /* The bottom line: x 16..300, y 400..420. */
    int differ = 0;
    for (int y = 400; y < 420; ++y) {
        for (int x = 16; x < 300; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
}

TEST_CASE(the_set_points_start_at_the_start_values)
{
    fresh();
    settings_set(SET_SUPPLY_V_START, 7.5f);
    settings_set(SET_SUPPLY_I_START, 1.0f);
    settings_set(SET_SUPPLY_TRIP_MS, 250.0f);
    supply_screen_settings_loaded();
    CHECK_NEAR(supply_screen_set_v(), 7.5f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.0f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().trip_s, 0.25f, 1e-4f);
}

TEST_CASE(a_card_or_a_value_opens_the_keypad_for_its_set_point)
{
    fresh();
    tap(CARD_X, CARD_V_Y);
    keys("7.4");
    CHECK_NEAR(supply_screen_set_v(), 7.4f, 1e-4f);
    tap(CARD_X, CARD_I_Y);
    keys("1.5");
    CHECK_NEAR(supply_screen_set_i(), 1.5f, 1e-4f);
    tap(TEXT_X, TEXT_V_Y);
    keys("9");
    CHECK_NEAR(supply_screen_set_v(), 9.0f, 1e-4f);
    tap(TEXT_X, TEXT_I_Y);
    keys("30");                         /* refused: the keypad stays */
    key(UI_KEY_CANCEL);
    CHECK_NEAR(supply_screen_set_i(), 1.5f, 1e-4f);
    /* A typed value is rounded to the supply's step. */
    tap(CARD_X, CARD_V_Y);
    keys("5.01");
    CHECK_NEAR(supply_screen_set_v(), 5.02f, 1e-4f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_change_to_a_live_output_waits_for_the_question)
{
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);
    tap(DISCARD_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    /* From the keypad, the same question. */
    tap(CARD_X, CARD_I_Y);
    keys("1.2");
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_i(), 1.2f, 1e-4f);
    /* A tap on the track asks on the release. */
    tap(TRACK_X + TRACK_W / 2, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK(supply_screen_set_v() > 11.0f);
    /* With the output off, nothing is asked. */
    supply_screen_set_output(false);
    tap(V_DOWN_X, V_ROW_Y);
    CHECK(supply_screen_set_v() < 12.7f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(an_on_asked_for_counts_as_live_before_it_is_seen)
{
    /* The ON has gone; the first sample to show it has not arrived.  A step
     * in between still asks. */
    fresh();
    hold_on();
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    /* Seen applied, then off: no longer live, nothing asked. */
    supply_screen_set_output(true);
    supply_screen_set_output(false);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.2f, 1e-4f);
    /* An ON a stop dropped is not waited for. */
    hold_on();
    supply_screen_cancel_on();
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.3f, 1e-4f);
    /* Nor one a touch loss dropped. */
    hold_on();
    (void)supply_screen_poll_cmd(&c);
    scr->cancel();
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.4f, 1e-4f);
    /* Nor one an OFF overtook. */
    hold_on();
    supply_screen_set_output(true);
    supply_screen_set_output(false);
    tap(OUT_X, OUT_Y);                   /* off: nothing to switch off */
    hold_on();
    tap(OUT_X, OUT_Y);
    (void)supply_screen_poll_cmd(&c);
}

TEST_CASE(an_on_dropped_on_its_way_stops_counting_as_live)
{
    /* Handed over and then dropped by the application: told nothing is
     * coming, the screen stops asking.  One still held is not dropped. */
    fresh();
    hold_on();
    supply_screen_set_on_coming(false);     /* still held: kept */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_on_coming(true);      /* on its way */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    supply_screen_set_on_coming(false);     /* dropped */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

TEST_CASE(the_question_closes_when_the_output_goes_off)
{
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);                    /* asked */
    supply_screen_set_output(false);         /* STOP, a trip, OFF */
    tap(APPLY_X, ASK_Y);                     /* no question to answer */
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);                    /* off: no question */
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

TEST_CASE(each_question_is_switched_on_its_own)
{
    fresh();
    supply_screen_set_output(true);
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(CARD_X, CARD_V_Y);
    keys("7");
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 1.0f);
    settings_set(SET_SUPPLY_CONFIRM_KEYS, 0.0f);
    tap(CARD_X, CARD_V_Y);
    keys("7");
    CHECK_NEAR(supply_screen_set_v(), 7.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 7.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 7.1f, 1e-4f);
}

TEST_CASE(a_drag_moves_the_set_point_by_the_distance_dragged)
{
    /* Many small moves, as a finger makes them: the set point follows the
     * finger and does not run ahead of it.  17.7 V over 413 px is about
     * 43 mV a pixel. */
    fresh();
    const float per_px = (21.0f - 3.3f) / (float)(TRACK_W - 1);
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    const float start = supply_screen_set_v();
    for (int k = 1; k <= 20; ++k) {
        ev(TRACK_X + 100 + k, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    }
    CHECK_NEAR(supply_screen_set_v(), start + 20.0f * per_px, 0.03f);
    for (int k = 0; k < 5; ++k) {          /* a still finger, jittering in y */
        ev(TRACK_X + 120, V_ROW_Y + (k & 1), TOUCH_EVENT_MOVE, 1);
    }
    CHECK_NEAR(supply_screen_set_v(), start + 20.0f * per_px, 0.03f);
    ev(TRACK_X + 120, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), start + 20.0f * per_px, 0.03f);

    /* On a live output with the question on, the question offers the
     * distance dragged too. */
    fresh();
    supply_screen_set_output(true);
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int k = 1; k <= 10; ++k) {
        ev(TRACK_X + 100 + k, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    }
    ev(TRACK_X + 110, V_ROW_Y, TOUCH_EVENT_UP, 1);
    tap(APPLY_X, ASK_Y);
    const float at_100 = 3.3f + 100.0f * per_px;
    CHECK_NEAR(supply_screen_set_v(), at_100 + 10.0f * per_px, 0.03f);
}

TEST_CASE(a_typed_cap_rounds_down_and_a_typed_trip_stays_a_trip)
{
    fresh();
    tap(SETB_X, SETB_Y);
    tap(ROW_L_X, VMAX_Y);
    keys("12.01");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 12.0f, 1e-4f);
    tap(ROW_L_X, VMAX_Y + 42);           /* CURRENT MAX */
    keys("1.03");
    CHECK_NEAR(settings_get(SET_SUPPLY_I_MAX), 1.0f, 1e-4f);
    tap(ROW_R_X, TRIPI_Y);
    keys("0.02");
    CHECK_NEAR(settings_get(SET_SUPPLY_TRIP_I), 0.05f, 1e-4f);
    CHECK(supply_screen_limits().trip_i > 0.0f);
}

TEST_CASE(a_drag_on_a_live_output_changes_nothing_until_it_is_answered)
{
    fresh();
    supply_screen_set_output(true);
    ev(TRACK_X + 20, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);

    /* Without the question it is live as it moves. */
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    ev(TRACK_X + 20, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    const float first = supply_screen_set_v();
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    CHECK(supply_screen_set_v() > first);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_UP, 1);

    /* And a drag whose release went missing goes back to the set point. */
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 1.0f);
    const float was = supply_screen_set_v();
    ev(TRACK_X + 20, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 300, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    scr->cancel();
    ev(TRACK_X + 300, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), was, 1e-4f);
    tap(APPLY_X, ASK_Y);                 /* no question was asked */
    CHECK_NEAR(supply_screen_set_v(), was, 1e-4f);
    /* The next step starts from the set point, not from where the lost
     * drag had left the slider. */
    tap(V_UP_X, V_ROW_Y);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), was + 0.1f, 1e-3f);
}

TEST_CASE(the_output_switch_works_under_every_overlay)
{
    supply_cmd_t c;
    fresh();
    supply_screen_set_output(true);
    tap(SETB_X, SETB_Y);                 /* the settings */
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    tap(CARD_X, CARD_V_Y);               /* the keypad */
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    key(UI_KEY_CANCEL);
    tap(SETB_X, SETB_Y);
    tap(V_UP_X, V_ROW_Y);                /* the question */
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.reset);
    /* While a question is asked, SETTINGS and the cards wait for it. */
    tap(SETB_X, SETB_Y);
    tap(CARD_X, CARD_V_Y);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

TEST_CASE(a_second_finger_on_a_track_changes_nothing_while_off_is_held)
{
    fresh();
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + TRACK_W - 2, V_ROW_Y, TOUCH_EVENT_DOWN, 2);
    ev(TRACK_X + TRACK_W - 2, V_ROW_Y, TOUCH_EVENT_UP, 2);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

TEST_CASE(leaving_drops_the_question_and_the_overlays)
{
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    scr->leave();
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(SETB_X, SETB_Y);
    scr->leave();
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    tap(V_UP_X, V_ROW_Y);                /* the overlay is gone */
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(CARD_X, CARD_V_Y);
    scr->leave();
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.2f, 1e-4f);
}

/* Pixels that differ in the header strip, y 0..23, between the two
 * buffers. */
static int header_differs(void)
{
    int differ = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < W; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    return differ;
}

/*
 * The rate AUTO found is in the header, after ONLINE, and not in the alert
 * band, which stays until another alert replaces it.  A change redraws the
 * header without anything else moving.
 */
TEST_CASE(the_pd_minis_rate_is_shown_after_online)
{
    fresh();
    supply_screen_set_model(false);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    supply_screen_push(&st);
    supply_screen_set_baud(0u);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));

    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK(header_differs() > 0);

    /* The same rate again changes nothing. */
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);

    /* Not while the module does not answer: NOT ANSWERING alone. */
    st.online = false;
    supply_screen_push(&st);
    supply_screen_set_baud(0u);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);

    /* Nor for the model. */
    supply_screen_set_model(true);
    st.online = true;
    supply_screen_push(&st);
    supply_screen_set_baud(0u);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);
}

TEST_CASE(a_trip_shows_on_the_mode_card)
{
    fresh();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.mode = SUPPLY_MODE_OFF;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv, 0);
    st.trip = SUPPLY_TRIP_CURRENT;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv2, 0);
    /* The MODE card, the rail's fourth: x 558..794, y 207..264. */
    int differ = 0;
    for (int y = 207; y < 264; ++y) {
        for (int x = 558; x < 794; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
    /* And the voltage trip reads differently from the current trip. */
    memcpy(fb, fb2, (size_t)W * H * sizeof(gfx_color_t));
    st.trip = SUPPLY_TRIP_VOLTAGE;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv2, 0);
    differ = 0;
    for (int y = 207; y < 264; ++y) {
        for (int x = 558; x < 794; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
}

/* ------------------------------------------------------------------ plot */

static supply_state_t reading(float v, float i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.v = v; st.i = i; st.p = v * i;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.online = true;
    st.mode = SUPPLY_MODE_CV;
    return st;
}

/* A whole frame drawn from nothing, so two screens can be compared by their
 * pixels: the trace, the readouts and the panel's title tag. */
static void render_both(void)
{
    scr->render(&cv, 0);
    supply_invalidate();
    scr->render(&cv, 0);
}

TEST_CASE(the_heading_says_what_the_output_does_on_either_pane)
{
    /* TABLE with the output off reads as PLOT with nothing recorded does,
     * not as a live output. */
    fresh();
    tap(TABLE_X, TABLE_Y);
    supply_invalidate();
    scr->render(&cv, 0);
    supply_screen_set_output(true);
    supply_state_t st = reading(6.0f, 1.0f);
    st.output = true;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv2, 0);
    /* The upper panel's title tag: x 6..130, y 24..44. */
    int differ = 0;
    for (int y = 24; y < 44; ++y) {
        for (int x = 6; x < 130; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);

    /* After a run: PLOT holds it, TABLE has nothing to hold and is idle --
     * so TABLE now reads as it did before any run. */
    supply_screen_set_output(false);
    supply_invalidate();
    scr->render(&cv2, 0);                /* TABLE, after the run */
    int same = 0, total = 0;
    for (int y = 24; y < 44; ++y) {
        for (int x = 6; x < 130; ++x) {
            same += (fb[y * W + x] == fb2[y * W + x]) ? 1 : 0;
            ++total;
        }
    }
    CHECK_EQ(same, total);
    tap(PLOT_TAB_X, TABLE_Y);
    supply_invalidate();
    scr->render(&cv2, 0);                /* PLOT, holding the run */
    differ = 0;
    for (int y = 24; y < 44; ++y) {
        for (int x = 6; x < 130; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
}

TEST_CASE(a_reading_that_did_not_arrive_is_not_plotted_as_the_last_one)
{
    /* Two runs alike but for the value left in a reading whose flag says it
     * did not arrive: the plot draws them the same. */
    gfx_color_t *want = malloc((size_t)W * H * sizeof(gfx_color_t));
    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        supply_screen_set_output(true);
        supply_state_t st = reading(6.0f, 1.5f);
        st.output = true;
        for (int k = 0; k < 20; ++k) {
            supply_screen_push(&st);
        }
        /* The readings stop arriving; one run keeps the stale numbers. */
        st.ok = 0u;
        if (pass == 1) {
            st.v = 0.0f;
            st.i = 0.0f;
            st.p = 0.0f;
        }
        for (int k = 0; k < 20; ++k) {
            supply_screen_push(&st);
        }
        supply_invalidate();
        scr->render(&cv, 0);
        if (pass == 0) {
            memcpy(want, fb, (size_t)W * H * sizeof(gfx_color_t));
        }
    }
    const int same = memcmp(fb, want, (size_t)W * H * sizeof(gfx_color_t));
    free(want);
    CHECK_EQ(same, 0);
}

TEST_CASE(a_run_is_traced_from_the_switch_on_and_held_after_it)
{
    fresh();
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);               /* off: no trace */
    render_both();

    /* The reference: a screen that only ever saw the run. */
    supply_screen_set_output(true);
    for (int k = 0; k < 30; ++k) {
        st = reading(6.0f, 1.0f + 0.02f * (float)k);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);
    st = reading(0.0f, 0.0f);
    supply_screen_push(&st);              /* after the run: readouts only */
    render_both();
    gfx_color_t *want = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(want, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* The same run with off-time samples before it on a used screen. */
    fresh();
    for (int k = 0; k < 50; ++k) {
        st = reading(9.0f, 3.0f);
        supply_screen_push(&st);
    }
    supply_screen_set_output(true);        /* a run before it, */
    for (int k = 0; k < 40; ++k) {
        st = reading(9.0f, 3.0f);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);       /* held, */
    supply_screen_set_output(true);        /* and cleared by the next */
    for (int k = 0; k < 30; ++k) {
        st = reading(6.0f, 1.0f + 0.02f * (float)k);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);
    st = reading(0.0f, 0.0f);
    supply_screen_push(&st);
    render_both();
    const int same = memcmp(fb, want, (size_t)W * H * sizeof(gfx_color_t));
    free(want);
    CHECK_EQ(same, 0);
}

/* ---------------------------------------------------------------- redraw */

/*
 * Drawing state B over state A must give the pixels B gives on a buffer that
 * never saw A; with two framebuffers, anything left over shows as flicker.
 */
static void check_redraw(void (*to_b)(void))
{
    fresh();
    supply_state_t st = reading(6.0f, 1.2f);
    supply_screen_push(&st);
    scr->render(&cv, 0);
    to_b();
    for (int k = 0; k < 8; ++k) {          /* past any flash */
        scr->render(&cv, 0);
    }
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
}

static void b_output_on(void)
{
    supply_screen_set_output(true);
    supply_state_t st = reading(5.95f, 1.8f);
    st.output = true;
    st.mode = SUPPLY_MODE_CC;
    supply_screen_push(&st);
}
static void b_set_point(void) { tap(V_UP_X, V_ROW_Y); drain(); }
static void b_table(void)     { tap(TABLE_X, TABLE_Y); }
static void b_offline(void)
{
    supply_state_t st = reading(0.0f, 0.0f);
    st.online = false;
    st.ok = 0u;
    supply_screen_set_model(true);
    supply_screen_push(&st);
}
static void b_table_then_set(void)
{
    tap(TABLE_X, TABLE_Y);
    scr->render(&cv, 0);
    tap(V_DOWN_X, I_ROW_Y);
    drain();
}
static void b_settings(void) { tap(SETB_X, SETB_Y); }
static void b_keypad(void)   { tap(CARD_X, CARD_V_Y); key(UI_KEY_7); }
static void b_question(void)
{
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
}
static void b_question_new_caps(void)
{
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    scr->render(&cv, 0);
    const supply_caps_t caps = { 9.0f, 21.0f, 0.02f, 0.5f, 5.0f, 0.05f };
    supply_screen_set_caps(&caps);
}
static void b_closed(void)
{
    tap(SETB_X, SETB_Y);
    scr->render(&cv, 0);
    tap(CLOSE_X, CLOSE_Y);
}
static void b_hold_half(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

TEST_CASE(a_redraw_leaves_no_stale_pixels)
{
    check_redraw(b_output_on);
    check_redraw(b_set_point);
    check_redraw(b_table);
    check_redraw(b_offline);
    check_redraw(b_table_then_set);
    check_redraw(b_hold_half);
    check_redraw(b_settings);
    check_redraw(b_keypad);
    check_redraw(b_question);
    check_redraw(b_closed);
    check_redraw(b_question_new_caps);
}

TEST_CASE(a_set_point_moved_offline_repaints_the_cards_in_both_buffers)
{
    /* Offline, the cards' brackets show the screen's own set point; a drag
     * that moves it repaints them in each buffer, not only in the one that
     * also took a sample. */
    fresh();
    supply_state_t st = reading(0.0f, 0.0f);
    st.online = false;
    st.ok = 0u;
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->render(&cv, 0);
    scr->render(&cv2, 1);                /* both buffers hold the press */
    supply_screen_push(&st);
    scr->render(&cv, 0);                 /* A takes the sample */
    ev(TRACK_X + 160, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    scr->render(&cv2, 1);                /* B: the sample and the move */
    scr->render(&cv, 0);                 /* A: the move alone */
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
    ev(TRACK_X + 160, V_ROW_Y, TOUCH_EVENT_UP, 1);
}

TEST_CASE(each_framebuffer_is_updated_independently)
{
    fresh();
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);
    scr->render(&cv, 0);
    scr->render(&cv2, 1);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A run, a set point and the switch, painted into buffer 1 first. */
    b_output_on();
    tap(V_UP_X, V_ROW_Y);
    drain();
    for (int k = 0; k < 8; ++k) {
        scr->render(&cv2, 1);
        scr->render(&cv, 0);
    }
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A frame with nothing new leaves a correct buffer correct. */
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
}

TEST_CASE(a_screen_without_a_sample_draws)
{
    /* No sample yet, no caps from a driver: the screen draws the model's
     * defaults and nothing reads uninitialised. */
    fresh();
    scr->render(&cv, 0);
    supply_screen_push(NULL);
    scr->event(NULL);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* SETTINGS offers RESET PD MINI while the PD mini is the supply, and not
 * while the panel runs its model; it asks for the restart with the output
 * off. */
TEST_CASE(reset_pd_mini_is_offered_only_for_the_module)
{
    fresh();
    supply_screen_set_model(true);
    tap(SETB_X, SETB_Y);
    tap(ROW_L_X, MODRESET_Y);
    supply_cmd_t c;
    CHECK(!supply_screen_poll_cmd(&c));
    tap(SETB_X, SETB_Y);                     /* closed again */

    supply_screen_set_model(false);
    tap(SETB_X, SETB_Y);
    tap(ROW_L_X, MODRESET_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.module_reset);
    CHECK(c.off);
    CHECK(!c.on);
}

/* A second hold on OUTPUT ON while the first one's ON is on its way: the
 * output reporting on during it does not make its release an OFF. */
TEST_CASE(an_on_reported_during_a_second_hold_is_not_an_off)
{
    fresh();
    hold_on();
    drain();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(4);
    supply_screen_set_output(true);
    tick_for(1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(!supply_screen_poll_cmd(&c) || !c.off);
}

/* The question asked while an ON was on its way goes when that ON is
 * dropped -- a STOP -- though the output never reported on. */
TEST_CASE(a_question_for_an_on_that_was_dropped_goes)
{
    fresh();
    hold_on();
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    CHECK_EQ(supply_screen_set_v(), was);       /* waiting for APPLY */
    supply_screen_cancel_on();
    tick_for(1);
    tap(APPLY_X, ASK_Y);                        /* nothing there now */
    CHECK_EQ(supply_screen_set_v(), was);
}

/* APPLY pressed on a question about a coming ON, then STOP drops the ON
 * before the finger lifts: the release applies nothing. */
TEST_CASE(an_apply_held_as_its_on_is_dropped_applies_nothing)
{
    fresh();
    hold_on();
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_cancel_on();
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
}

/* The same, with the frame's tick between: the question goes, and takes
 * the press on its APPLY with it. */
TEST_CASE(a_dropped_question_lets_go_of_its_apply)
{
    fresh();
    hold_on();
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_cancel_on();
    tick_for(1);
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
}

/* The same on SUPPLY with the output already on: STOP lets go of APPLY. */
TEST_CASE(stop_lets_go_of_supplys_apply)
{
    fresh();
    supply_screen_set_output(true);
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_cancel_on();
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
}

/* STOP and a whole APPLY tap in one frame on SUPPLY: the stop is seen
 * first, while the output still reports on.  The tap applies nothing, and
 * the question has gone by the frame's tick. */
TEST_CASE(stop_and_an_apply_tap_in_one_frame_apply_nothing_on_supply)
{
    fresh();
    supply_screen_set_output(true);
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    supply_screen_cancel_on();
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
    tick_for(1);
    tap(APPLY_X, ASK_Y);                        /* nothing there now */
    CHECK_EQ(supply_screen_set_v(), was);
}

/* A question asked after a stop, the output still on, stands. */
TEST_CASE(a_question_asked_after_a_stop_stands_on_supply)
{
    fresh();
    supply_screen_set_output(true);
    supply_screen_cancel_on();
    tick_for(1);
    tap(CARD_X, CARD_V_Y);
    keys("7");
    tap(APPLY_X, ASK_Y);
    CHECK_EQ(supply_screen_set_v(), 7.0f);
}

/* ------------------------------------------- a press that lost its release */

/*
 * The cases below feed the screen frames of contacts through the tracker
 * (touch_feed.h), with no router in front of it.  The feed works in panel
 * coordinates and removes the band's height, as the router does.
 */
#define P(y) ((y) + UI_BAND_H)

static void fed(void)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
}

/*
 * One contact holds the screen, and a contact that has gone cannot.  A drag
 * on the voltage track loses its release, with no loss told.  The next
 * press carries the same track id: it says the first contact is over, so
 * the screen takes it.  A hold on OUTPUT ON switches on at the set point the
 * supply has, and the lift moves no set point.
 */
TEST_CASE(a_press_that_lost_its_release_gives_way_to_the_next_press)
{
    fed();
    finger(FEED_LONE, TRACK_X + 60, P(V_ROW_Y));
    glide(FEED_LONE, TRACK_X + 80, P(V_ROW_Y), 8);
    const float set = supply_screen_set_v();
    feed_lose_next(1);
    lift(FEED_LONE);
    drain();

    finger(FEED_LONE, OUT_X, P(OUT_Y));
    tick_for(HOLD_TICKS);
    supply_cmd_t c = { 0 };
    const bool posted = supply_screen_poll_cmd(&c);
    lift(FEED_LONE);
    CHECK(posted && c.on);
    CHECK_NEAR(supply_screen_set_v(), set, 0.001f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* On a live output the press that went missing was a tap on OUTPUT OFF: an
 * OFF the operator made, sent when the next press shows it is over. */
TEST_CASE(a_lost_release_on_output_off_is_sent_at_the_next_press)
{
    fed();
    supply_screen_set_output(true);
    drain();
    finger(FEED_LONE, OUT_X, P(OUT_Y));
    feed_lose_next(1);
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));

    finger(FEED_LONE, OFF_X, P(OFF_Y));
    supply_cmd_t c = { 0 };
    const bool posted = supply_screen_poll_cmd(&c);
    lift(FEED_LONE);
    CHECK(posted && c.off);
}

/* A second finger, with a track id of its own, still cannot take the screen
 * from a press that is being held. */
TEST_CASE(a_second_contact_does_not_end_a_press_that_is_held)
{
    fed();
    finger(0, TRACK_X + 60, P(V_ROW_Y));
    glide(0, TRACK_X + 80, P(V_ROW_Y), 8);
    finger(1, OUT_X, P(OUT_Y));
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
    lift(1);
    const float before = supply_screen_set_v();
    glide(0, TRACK_X + 120, P(V_ROW_Y), 8);
    lift(0);
    CHECK(supply_screen_set_v() > before);
}

int main(void)
{
    RUN(reset_pd_mini_is_offered_only_for_the_module);
    RUN(the_output_comes_on_with_a_hold_and_only_once);
    RUN(a_short_press_on_output_on_does_nothing);
    RUN(a_finger_that_leaves_the_switch_switches_nothing);
    RUN(the_output_goes_off_with_a_tap);
    RUN(the_release_that_ends_the_hold_does_not_switch_off);
    RUN(an_off_outranks_an_on_in_the_same_poll);
    RUN(a_second_contact_cannot_steal_the_release);
    RUN(a_second_contact_on_another_button_cannot_steal_the_off);
    RUN(a_stop_abandons_the_hold_and_drops_a_posted_on);
    RUN(a_stop_while_the_output_reports_off_strands_no_press);
    RUN(a_touch_loss_drops_an_on_and_keeps_an_off);
    RUN(leaving_under_a_press_on_output_off_sends_the_off);
    RUN(leaving_keeps_the_output_and_drops_the_hold);
    RUN(reset_peaks_posts_its_own_command);
    RUN(the_fine_steps_move_a_set_point_by_a_tenth);
    RUN(a_tap_on_a_track_sets_a_value_the_supply_takes);
    RUN(new_caps_pull_the_set_points_into_range);
    RUN(the_settings_overlay_sets_a_cap_the_set_points_obey);
    RUN(a_lowered_cap_brings_the_set_point_and_the_start_down);
    RUN(a_switch_in_the_settings_flips_on_a_tap);
    RUN(a_start_value_read_over_its_cap_is_corrected_and_kept);
    RUN(the_overlay_tells_an_unsaved_setup_change_from_a_waiting_save);
    RUN(the_set_points_start_at_the_start_values);
    RUN(a_card_or_a_value_opens_the_keypad_for_its_set_point);
    RUN(a_change_to_a_live_output_waits_for_the_question);
    RUN(an_on_asked_for_counts_as_live_before_it_is_seen);
    RUN(the_heading_says_what_the_output_does_on_either_pane);
    RUN(an_on_dropped_on_its_way_stops_counting_as_live);
    RUN(the_question_closes_when_the_output_goes_off);
    RUN(each_question_is_switched_on_its_own);
    RUN(a_drag_moves_the_set_point_by_the_distance_dragged);
    RUN(a_typed_cap_rounds_down_and_a_typed_trip_stays_a_trip);
    RUN(a_drag_on_a_live_output_changes_nothing_until_it_is_answered);
    RUN(the_output_switch_works_under_every_overlay);
    RUN(a_second_finger_on_a_track_changes_nothing_while_off_is_held);
    RUN(leaving_drops_the_question_and_the_overlays);
    RUN(a_reading_that_did_not_arrive_is_not_plotted_as_the_last_one);
    RUN(a_trip_shows_on_the_mode_card);
    RUN(a_run_is_traced_from_the_switch_on_and_held_after_it);
    RUN(a_redraw_leaves_no_stale_pixels);
    RUN(each_framebuffer_is_updated_independently);
    RUN(a_set_point_moved_offline_repaints_the_cards_in_both_buffers);
    RUN(a_screen_without_a_sample_draws);
    RUN(the_pd_minis_rate_is_shown_after_online);
    RUN(an_on_reported_during_a_second_hold_is_not_an_off);
    RUN(a_question_for_an_on_that_was_dropped_goes);
    RUN(an_apply_held_as_its_on_is_dropped_applies_nothing);
    RUN(a_dropped_question_lets_go_of_its_apply);
    RUN(stop_lets_go_of_supplys_apply);
    RUN(stop_and_an_apply_tap_in_one_frame_apply_nothing_on_supply);
    RUN(a_question_asked_after_a_stop_stands_on_supply);
    RUN(a_press_that_lost_its_release_gives_way_to_the_next_press);
    RUN(a_lost_release_on_output_off_is_sent_at_the_next_press);
    RUN(a_second_contact_does_not_end_a_press_that_is_held);
    free(fb);
    free(fb2);
    return test_summary("supply_screen");
}
