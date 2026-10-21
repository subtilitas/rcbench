/*
 * The SETUP screen: keys, the SAVE and RESET buttons, save failures and
 * held values.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "hitec_rx_link.h"

/* ---------------------------------------------------------------- screen */

TEST_CASE(screen_switches_category_and_renders)
{
    fresh_screen();
    ui_router_render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 100) != 0);

    for (int i = 0; i < SET_CAT_SETUP_COUNT; ++i) {
        open_category((setting_cat_t)i);
        CHECK_EQ(settings_screen_category(), i);
        ui_router_render(&s_c, 0);
        CHECK(gfx_pixel_get(&s_c, 400, 100) != 0);
    }
}

TEST_CASE(plus_and_minus_change_the_setting_under_them)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);

    int before = settings_get_int(ids[0]);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK_EQ(settings_get_int(ids[0]) - before,
             (long)settings_def(ids[0])->step);

    tap(MINUS_X + BTN_W / 2, row_y(0));
    CHECK_EQ(settings_get_int(ids[0]), before);

    /* The second row is a different setting, and only it moves. */
    int row1 = settings_get_int(ids[1]);
    tap(PLUS_X + BTN_W / 2, row_y(1));
    CHECK(settings_get_int(ids[1]) != row1);
    CHECK_EQ(settings_get_int(ids[0]), before);
}

/*
 * The operator's own path: the key under a SETUP row that the IOmcu (the
 * RP2350 board) is sent, INA228 SHUNT on INTERFACES.
 *
 * The model, the screen and the observer are one chain, and the panel hangs
 * the write to the IOmcu off the far end of it.  A break anywhere in that
 * chain leaves the panel with nothing to hang the write on, which is the
 * defect this guards, seen from where the finger is.  Motor poles, the
 * row this used before it moved to MOTOR & ESC > SETTINGS, is held to the
 * same chain there (test_motor_workbench).
 */
TEST_CASE(a_setup_key_notifies_once_per_press)
{
    fresh_screen();
    const int row = row_of(SET_INA228_UOHM);
    CHECK(row >= 0);

    s_observed = 0;
    settings_set_observer(observer);

    int before = settings_get_int(SET_INA228_UOHM);
    tap(PLUS_X + BTN_W / 2, row_y(row));
    CHECK_EQ(settings_get_int(SET_INA228_UOHM) - before,
             (long)settings_def(SET_INA228_UOHM)->step);
    CHECK_EQ(s_observed, 1);
    CHECK_EQ(s_last_observed, SET_INA228_UOHM);

    tap(MINUS_X + BTN_W / 2, row_y(row));
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), before);
    CHECK_EQ(s_observed, 2);

    settings_set_observer(NULL);
}

TEST_CASE(changing_the_theme_from_the_screen_takes_effect)
{
    fresh_screen();
    open_category(SET_CAT_APP);

    setting_id_t ids[64];
    int n = settings_in_category(SET_CAT_APP, ids, 64);
    int theme_row = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == SET_THEME) { theme_row = i; }
    }
    CHECK(theme_row >= 0);

    gfx_color_t before = ui_theme_color(UI_C_BG);
    tap(PLUS_X + BTN_W / 2, row_y(theme_row));
    CHECK_EQ(settings_get_int(SET_THEME), UI_THEME_LIGHT);
    CHECK(ui_theme_color(UI_C_BG) != before);

    tap(MINUS_X + BTN_W / 2, row_y(theme_row));
    CHECK_EQ(settings_get_int(SET_THEME), UI_THEME_DARK);
    CHECK_EQ(ui_theme_color(UI_C_BG), before);
}

/*
 * Design stepped to LIGHT and back to DARK is no change: the first frame
 * after the step back shows SAVED, in every framebuffer the panel draws
 * into in turn, as after any other row stepped back.  Brightness the same,
 * for comparison.
 */
TEST_CASE(a_theme_stepped_back_shows_saved_on_the_next_frame)
{
    static const setting_id_t ids_to_step[2] = { SET_THEME, SET_BRIGHTNESS };
    for (int k = 0; k < 2; ++k) {
        fresh_screen();
        open_category(SET_CAT_APP);
        setting_id_t ids[64];
        const int n = settings_in_category(SET_CAT_APP, ids, 64);
        int row = -1;
        for (int i = 0; i < n; ++i) {
            if (ids[i] == ids_to_step[k]) { row = i; }
        }
        CHECK(row >= 0);
        CHECK(!settings_dirty());
        const unsigned clean = save_face();

        /* Brightness is at its top: down and up again; the theme cycles. */
        const int first = (k == 0) ? PLUS_X : MINUS_X;
        const int back  = (k == 0) ? MINUS_X : PLUS_X;
        tap(first + BTN_W / 2, row_y(row));
        CHECK(settings_dirty());
        CHECK(save_face() != clean);
        for (int b = 0; b < 3; ++b) {
            ui_router_render(&s_c, b);
        }
        tap(back + BTN_W / 2, row_y(row));
        CHECK(!settings_dirty());
        CHECK_EQ(save_face(), clean);
        /* The panel draws into its buffers in turn: each shows SAVED. */
        for (int b = 1; b < 3; ++b) {
            ui_router_render(&s_c, b);
            unsigned long sum = 0;
            for (int y = SAVE_Y + 6; y < SAVE_Y + SAVE_H - 6; ++y) {
                for (int x = SAVE_X + 6; x < SAVE_X + 200; ++x) {
                    sum += (unsigned long)s_fb[y * W + x];
                }
            }
            CHECK_EQ((unsigned)(sum & 0xffffffffUL), clean);
        }
    }
}

TEST_CASE(a_drag_scrolls_instead_of_pressing)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);
    int before = settings_get_int(ids[0]);

    /* Press on the + key, then move well past the slop before releasing.
     * The press arms the key and steps nothing; the drag gives it up. */
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = PLUS_X + BTN_W / 2,
                                   .y = (int16_t)row_y(0) } };
    ui_router_event(&e);
    int after_press = settings_get_int(ids[0]);

    e.type = TOUCH_EVENT_MOVE;
    for (int i = 1; i <= 10; ++i) {
        e.point.y = (int16_t)(row_y(0) - i * 12);
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);

    CHECK_EQ(after_press, before);
    CHECK_EQ(settings_get_int(ids[0]), before);

    ui_router_render(&s_c, 0);
}

TEST_CASE(holding_a_key_repeats_with_acceleration)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);

    /* INA228 SHUNT: 50..20000 uOhm in steps of 1, which is why holding has
     * to work at all.  ids[] holds it at its row. */
    const int row = row_of(SET_INA228_UOHM);
    CHECK(row >= 0);
    ids[1] = SET_INA228_UOHM;
    settings_set(ids[1], settings_def(ids[1])->def);
    int start = settings_get_int(ids[1]);

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = PLUS_X + BTN_W / 2,
                                   .y = (int16_t)row_y(row) } };
    ui_router_event(&e);
    int after_press = settings_get_int(ids[1]);
    CHECK_EQ(after_press, start);       /* armed: the press steps nothing */

    /* Nothing repeats before the delay. */
    for (int i = 0; i < 5; ++i) {
        ui_router_tick(0.05f);
    }
    CHECK_EQ(settings_get_int(ids[1]), after_press);

    for (int i = 0; i < 20; ++i) {
        ui_router_tick(0.05f);
    }
    int slow = settings_get_int(ids[1]);
    CHECK(slow > after_press);

    /* And it speeds up rather than plodding through three hundred taps. */
    for (int i = 0; i < 40; ++i) {
        ui_router_tick(0.05f);
    }
    int fast = settings_get_int(ids[1]);
    CHECK((fast - slow) > (slow - after_press));

    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    int settled = settings_get_int(ids[1]);
    ui_router_tick(0.5f);
    CHECK_EQ(settings_get_int(ids[1]), settled);      /* release stops it */
}

TEST_CASE(reset_button_restores_the_open_category)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);

    settings_set(ids[0], settings_def(ids[0])->max);
    settings_set(SET_BRIGHTNESS, 50);

    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(settings_get_int(ids[0]), (long)settings_def(ids[0])->def);
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS), 50);   /* other category kept */
}

TEST_CASE(leaving_the_screen_no_longer_saves_on_its_own)
{
    /*
     * It used to. A save is a flash write, and on this board that stalls
     * both cores including the one that beats the safety line, so it does
     * not happen at a moment nobody chose.
     */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK(settings_dirty());

    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(settings_dirty());          /* still unwritten */
    CHECK_EQ(s_save_calls, 0);

    settings_set_store(NULL);
}

TEST_CASE(the_save_button_asks_and_the_application_decides_when)
{
    /*
     * The press is a request, not a write. Nothing reaches the store until
     * the application says the bench can afford to stop for it.
     */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());

    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_asked());
    CHECK_EQ(s_save_calls, 0);        /* asked, and nothing written */
    CHECK(settings_dirty());

    /* Not while the bench is busy, however long that lasts. */
    for (int i = 0; i < 20; ++i) {
        CHECK(!settings_save_tick(false));
    }
    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_save_asked());

    /* And then, once. */
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    CHECK(!settings_dirty());
    CHECK(!settings_save_asked());
    CHECK(!settings_save_tick(true));  /* nothing left to take */
    CHECK_EQ(s_save_calls, 1);

    settings_set_store(NULL);
}

TEST_CASE(the_request_outlives_the_screen)
{
    /* The operator has said what they want kept; walking away does not
     * unsay it, and the save still lands when the bench is idle. */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_asked());

    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(settings_save_asked());
    CHECK_EQ(s_save_calls, 0);

    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    settings_set_store(NULL);
}


TEST_CASE(a_press_while_the_save_is_pending_changes_nothing)
{
    /*
     * Once asked, the button reads WHEN IDLE and is drawn inert. A press
     * must not light it either: a highlight on a button that is doing
     * nothing is the same lie the old SAVED line told.
     */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(LIST_CAT, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));

    /* Offered: holding it down changes the face. */
    const unsigned offered = save_face();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = SAVE_X + 100,
                                   .y = (int16_t)(SAVE_Y + SAVE_H / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    CHECK(save_face() != offered);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(settings_save_asked());

    /* Asked for: holding it down does not. */
    const unsigned pending = save_face();
    e.type = TOUCH_EVENT_DOWN;
    ui_router_event(&e);
    CHECK_EQ(save_face(), pending);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);

    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_save_asked());
    settings_set_store(NULL);
}

TEST_CASE(asking_with_nothing_to_write_asks_for_nothing)
{
    /* A button that presses with nothing to save says a save happened. */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    CHECK(!settings_dirty());

    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(!settings_save_asked());
    CHECK(!settings_save_tick(true));
    CHECK_EQ(s_save_calls, 0);

    settings_cancel_save();
    settings_set_store(NULL);
}



/*
 * A hit held open acts on its release and the plus and minus keys repeat on
 * the frame timer while it stands, so a lost event must not leave either
 * running: the GT911 reuses track ids, and a later contact that began
 * somewhere else would be taken for this one.
 */
TEST_CASE(a_cancelled_hit_presses_nothing)
{
    fresh_hub();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* Press the tile of the outputs screen, the first after the
     * categories', then the events stop arriving. */
    const int dx = tile_x(SET_CAT_SETUP_COUNT);
    const int dy = tile_y(SET_CAT_SETUP_COUNT);
    touch_event_t d = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)dx,
                                   .y = (int16_t)dy } };
    ui_router_event(&d);

    ui_router_cancel_gestures();

    /*
     * The release the screen never saw arrives on a reused track id.  The
     * hit is gone, so it opens nothing: a navigation from a press nobody
     * made is the small version of what the same latch costs on the outputs
     * screen, where the release applies a binding change.
     */
    touch_event_t u = { .type = TOUCH_EVENT_UP,
                        .point = { .id = 1, .x = (int16_t)dx,
                                   .y = (int16_t)dy } };
    ui_router_event(&u);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* And a fresh press still opens it, so nothing is stuck. */
    ui_router_event(&d);
    ui_router_event(&u);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
}

/*
 * A store that refuses leaves the values dirty.  The screen's label is the
 * whole of the feedback, and it reads SAVED only when nothing is left to
 * write; reporting that against a store that wrote nothing would be a claim
 * the next boot contradicts.
 */
TEST_CASE(a_refused_save_keeps_the_values_dirty)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(settings_dirty());
    CHECK(!settings_save_failed());

    CHECK(!settings_save());
    CHECK(settings_dirty());          /* still to be written */
    CHECK(settings_save_failed());
    CHECK(!settings_save_asked());    /* the request is answered */
    CHECK_EQ(s_save_calls, 1);
}

/*
 * No store at all is a failed save and not a no-op.  settings_set_store(NULL)
 * is what a panel gets when NVS cannot be brought up, and every save it takes
 * for the rest of that session writes nothing.
 */
TEST_CASE(a_missing_store_is_a_failed_save)
{
    fresh_model();
    settings_set_store(NULL);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_dirty());
    CHECK(settings_save_failed());
}

/* A store that takes them clears both the dirt and the failure. */
TEST_CASE(a_successful_save_retires_an_earlier_failure)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();
    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());

    settings_set_store(&s_mem_store);
    CHECK(settings_save());
    CHECK(!settings_dirty());
    CHECK(!settings_save_failed());
}

/*
 * And so does an edit: the failure described values these no longer are, and
 * a stale NOT SAVED beside a number the operator has just changed says the
 * wrong thing about the wrong value.
 */
TEST_CASE(an_edit_retires_an_earlier_failure)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();
    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());

    settings_set(SET_MOTOR_POLES, 14.0f);
    CHECK(!settings_save_failed());
    CHECK(settings_dirty());
}

/* The idle write reports what the store did rather than that it was tried. */
TEST_CASE(the_idle_save_reports_a_refusal)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();
    settings_set(SET_MOTOR_POLES, 12.0f);
    settings_request_save();
    CHECK(settings_save_asked());

    CHECK(!settings_save_tick(true));
    CHECK(settings_dirty());
    CHECK(settings_save_failed());
}

/*
 * A reset that changes nothing is not an edit.  A category already holding
 * its defaults is reset to what it has; treating that as an edit would drop
 * NOT SAVED while the values whose write was refused are still the ones in
 * memory and nothing has been written since.
 */
TEST_CASE(a_reset_that_changes_nothing_keeps_the_failure)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());

    /* APP holds its defaults, so this moves nothing. */
    settings_reset(SET_CAT_APP);
    CHECK(settings_save_failed());

    /* ESC / BENCH holds the edited pole count, so this does move something
     * and is an edit like any other. */
    settings_reset(SET_CAT_ESC);
    CHECK(!settings_save_failed());
    CHECK(settings_dirty());
}

/*
 * The write is taken outside this screen, by settings_save_tick() in the
 * panel's render loop, so a pending write completing or being refused moves
 * the button's state with no touch to invalidate the cached framebuffers.
 * The screen has to notice that itself, or a refusal goes on drawing
 * WHEN IDLE until something else repaints.
 */
TEST_CASE(a_refused_pending_write_repaints_the_button)
{
    fresh_screen();
    settings_set_store(&s_refusing_store);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    settings_request_save();
    CHECK(settings_save_asked());

    /* Drawn once as WHEN IDLE, with the chrome cached for this buffer. */
    ui_router_render(&s_c, 0);
    static gfx_color_t before[(size_t)W * H];
    memcpy(before, s_fb, sizeof(before));

    /* The write is taken and refused, away from any touch. */
    CHECK(!settings_save_tick(true));
    CHECK(settings_save_failed());

    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(before, s_fb, sizeof(before)) != 0);
}

/* The output encoder: off at the start, a centre inside the sensor's 12
 * bits, in INTERFACES beside the sensor bus. */
TEST_CASE(the_encoder_settings_start_off_with_a_12_bit_centre)
{
    settings_set_store(NULL);
    settings_init();
    CHECK_EQ(settings_get_int(SET_ENC_EN), 0);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
    const setting_def_t *d = settings_def(SET_ENC_EN);
    CHECK_EQ(d->type, SET_TYPE_BOOL);
    CHECK_EQ(d->cat, SET_CAT_IFACE);
    CHECK_STR_EQ(d->label, "AS5600");
    d = settings_def(SET_ENC_CENTRE);
    CHECK_EQ(d->type, SET_TYPE_INT);
    CHECK_EQ(d->cat, SET_CAT_IFACE);
    CHECK_EQ((int)d->min, 0);
    CHECK_EQ((int)d->max, 4095);
    settings_set(SET_ENC_CENTRE, 5000.0f);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 4095);
    settings_set(SET_ENC_CENTRE, -3.0f);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
}

/* ------------------------------------------------------------ held values */

/* A held setting is changed by no call of the model: not set, not stepped,
 * not reset with its category and not reset with all of them.  Nothing is
 * observed and nothing is to save.  The hold over, each call takes. */
TEST_CASE(a_held_setting_is_left_by_every_call_that_changes_one)
{
    static const setting_id_t held[] = { SET_SERVO_TARGET,
                                         SET_SERVO_PORT_PWM };

    for (unsigned was = 0; was < 4u; ++was) {
        /* Each pair of values the two can stand at when the hold begins. */
        const float target = (float)(was & 1u);
        const float pwm    = (float)((was >> 1) & 1u);

        fresh_model();
        settings_set_hold(port_hold);
        s_hold_on = false;
        settings_set(SET_SERVO_TARGET, target);
        settings_set(SET_SERVO_PORT_PWM, pwm);
        settings_set(SET_BRIGHTNESS, 50.0f);
        settings_set_store(&s_mem_store);
        CHECK(settings_save());
        settings_set_observer(observer);
        s_observed = 0;
        s_hold_on = true;
        s_hold_asked = 0;
        for (unsigned i = 0; i < 2u; ++i) {
            CHECK(settings_held(held[i]));
            settings_set(held[i], 0.0f);
            settings_set(held[i], 1.0f);
            settings_adjust(held[i], 1);
            settings_adjust(held[i], -1);
            settings_adjust(held[i], 3);
        }
        CHECK(s_hold_asked >= 12);
        settings_reset(SET_CAT_SERVO);
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), (int)target);
        CHECK_EQ(settings_get_int(SET_SERVO_PORT_PWM), (int)pwm);
        CHECK_EQ(s_observed, 0);
        CHECK(!settings_dirty());
        /* Every other value goes to its default, these two stay. */
        settings_reset_all();
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), (int)target);
        CHECK_EQ(settings_get_int(SET_SERVO_PORT_PWM), (int)pwm);
        CHECK_EQ(settings_get_int(SET_BRIGHTNESS),
                 (int)settings_def(SET_BRIGHTNESS)->def);
        CHECK_EQ(s_observed, 1);
        CHECK_EQ(s_last_observed, SET_BRIGHTNESS);
        /* A setting nobody holds is taken as ever. */
        CHECK(!settings_held(SET_BRIGHTNESS));
        settings_set(SET_BRIGHTNESS, 40.0f);
        CHECK_EQ(settings_get_int(SET_BRIGHTNESS), 40);
        /* The hold over. */
        s_hold_on = false;
        CHECK(!settings_held(SET_SERVO_TARGET));
        settings_adjust(SET_SERVO_TARGET, 1);
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1 - (int)target);
        settings_set(SET_SERVO_PORT_PWM, 1.0f - pwm);
        CHECK_EQ(settings_get_int(SET_SERVO_PORT_PWM), 1 - (int)pwm);
        settings_reset(SET_CAT_SERVO);
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET),
                 (int)settings_def(SET_SERVO_TARGET)->def);
        CHECK_EQ(settings_get_int(SET_SERVO_PORT_PWM),
                 (int)settings_def(SET_SERVO_PORT_PWM)->def);
    }
    /* No id is no setting, and nobody asked holds nothing. */
    s_hold_on = true;
    s_hold_asked = 0;
    CHECK(!settings_held(SETTING_COUNT));
    CHECK(!settings_held((setting_id_t)-1));
    CHECK_EQ(s_hold_asked, 0);
    settings_set_hold(NULL);
    CHECK(!settings_held(SET_SERVO_TARGET));
    /* A start-up takes the defaults whatever is held. */
    settings_set_hold(port_hold);
    settings_set_store(NULL);
    settings_set_observer(NULL);
    settings_set_hold(NULL);
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set_hold(port_hold);
    settings_init();
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    settings_set_hold(NULL);
    s_hold_on = false;
}

/* The first two rows of SETUP's ESC category, held by turns. */
static setting_id_t s_row_held;

static bool row_hold(setting_id_t id)
{
    return s_hold_on && id == s_row_held;
}

/* A held setting's row on SETUP has no keys: a tap and a hold where the
 * other rows have theirs change nothing, RESET CATEGORY leaves the value
 * and resets the rest, and the keys are back when the hold is over.  No
 * category SETUP lists has a held setting today; the rule is the list's. */
TEST_CASE(setup_changes_no_held_setting)
{
    for (int row = 0; row < 2; ++row) {
        setting_id_t ids[64];

        gesture_screen(LIST_CAT);
        settings_in_category(LIST_CAT, ids, 64);
        s_row_held = ids[row];
        const setting_id_t other = ids[1 - row];
        settings_set_hold(row_hold);
        s_hold_on = false;
        const int y = row_y(row);
        /* Not held: the keys step it. */
        feed_tap(FEED_LONE, XP, y);
        CHECK(settings_get(s_row_held) != s_before[s_row_held]);
        feed_tap(FEED_LONE, XP, row_y(1 - row));
        CHECK(settings_get(other) != s_before[other]);
        snap();
        s_hold_on = true;
        static const int xs[] = { MINUS_X, XM, MINUS_X + BTN_W - 1,
                                  PLUS_X, XP, PLUS_X + BTN_W - 1 };
        for (size_t k = 0u; k < sizeof(xs) / sizeof(xs[0]); ++k) {
            feed_tap(FEED_LONE, xs[k], y);
            finger(FEED_LONE, xs[k], y);
            idle(3.0f);
            lift(FEED_LONE);
            idle(0.5f);
        }
        CHECK_EQ(changed(), 0);
        /* No key is drawn on the row, and both are on the row beside. */
        unsigned drawn = 0u;
        unsigned beside = 0u;
        s_hold_on = false;
        settings_screen_invalidate();
        ui_router_render(&s_c, 0);
        const gfx_color_t face = gfx_pixel_get(&s_c, PLUS_X + 3, y);
        s_hold_on = true;
        settings_screen_invalidate();
        ui_router_render(&s_c, 0);
        for (int x = MINUS_X; x < PLUS_X + BTN_W; ++x) {
            drawn += gfx_pixel_get(&s_c, x, y) == face ? 1u : 0u;
            beside += gfx_pixel_get(&s_c, x, row_y(1 - row)) == face ? 1u
                                                                     : 0u;
        }
        CHECK_EQ(drawn, 0u);
        CHECK(beside > (unsigned)BTN_W);
        /* A drag from where a key would be scrolls the list. */
        const int from = settings_screen_scroll(NULL);
        swipe(XM, y + 20, XM, y + 20 - 40, 4);
        CHECK_EQ(settings_screen_scroll(NULL), from + 32);
        swipe(XL, y + 20, XL, y + 20 + 40, 4);
        CHECK_EQ(settings_screen_scroll(NULL), from);
        CHECK_EQ(changed(), 0);
        /* RESET CATEGORY: the other row goes to its default. */
        feed_tap(FEED_LONE, RESET_X + 100, RESET_Y + RESET_H / 2);
        CHECK_EQ(changed(), 1);
        CHECK(settings_get(other) == settings_def(other)->def);
        CHECK(settings_get(s_row_held) != settings_def(s_row_held)->def);
        /* The hold over, the keys are back. */
        s_hold_on = false;
        snap();
        feed_tap(FEED_LONE, XM, y);
        CHECK_EQ(changed(), 1);
        CHECK(settings_get(s_row_held) != s_before[s_row_held]);
        settings_set_hold(NULL);
    }
}

/* ------------------------------------------------------------ headings */

/* INTERFACES carries five headings, each above the first row of its
 * section; no other list has one. */
TEST_CASE(interfaces_has_five_headings_above_its_sections)
{
    fresh_hub();
    setting_id_t ids[64];
    const int n = settings_in_category(SET_CAT_IFACE, ids, 64);
    CHECK_EQ(settings_screen_rows(SET_CAT_IFACE), n + 5);
    static const setting_id_t first[5] = { SET_INA228_EN, SET_TONE_EN,
                                           SET_VIBE_EN, SET_PDMINI_EN,
                                           SET_HITEC_RX };
    int prev = -1;
    for (int h = 0; h < 5; ++h) {
        const int row = settings_screen_row_of(SET_CAT_IFACE,
                                               row_of(first[h]));
        /* The heading stands on the row above, the section's first. */
        CHECK(row > prev + 1);
        CHECK_EQ(row, row_of(first[h]) + h + 1);
        prev = row;
    }
    /* Hitec D receive is the last row, alone under PROG PORT. */
    CHECK_EQ(row_of(SET_HITEC_RX), n - 1);
    CHECK_EQ(settings_screen_row_of(SET_CAT_IFACE, 0), 1);
    CHECK_EQ(settings_screen_row_of(SET_CAT_IFACE, n), -1);
    for (int c = 0; c < SET_CAT_SETUP_COUNT; ++c) {
        if (c != SET_CAT_IFACE) {
            CHECK_EQ(settings_screen_rows((setting_cat_t)c),
                     settings_in_category((setting_cat_t)c, ids, 64)
                         + settings_texts_in_category((setting_cat_t)c,
                                                      NULL, 0));
        }
    }
}

/* Hitec D receive is marked while the IOmcu does not hold it, and not by
 * the sensor or tap marks; HELD clears it. */
TEST_CASE(the_hitec_receive_row_is_marked_as_the_iomcu_holds_it)
{
    fresh_hub();
    settings_screen_set_unheld(0xFFFFu, 0xFFu);
    CHECK(!settings_screen_unheld(SET_HITEC_RX));
    settings_screen_set_unheld(0u, 0u);
    for (int m = HITEC_RX_LINK_PENDING; m <= HITEC_RX_LINK_NO_LINE; ++m) {
        settings_screen_set_hitec_rx((uint8_t)m);
        CHECK(settings_screen_unheld(SET_HITEC_RX));
        CHECK(!settings_screen_unheld(SET_PDMINI_BAUD));
    }
    settings_screen_set_hitec_rx(HITEC_RX_LINK_HELD);
    CHECK(!settings_screen_unheld(SET_HITEC_RX));
    /* Default PULL-UP; a switch that wraps round. */
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 0);
    CHECK_EQ(settings_def(SET_HITEC_RX)->option_count, 2);
    CHECK(settings_cycles(SET_HITEC_RX));
    settings_adjust(SET_HITEC_RX, 1);
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 1);
    settings_adjust(SET_HITEC_RX, 1);
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 0);
}

/* Hitec D receive's help line follows the value, in both languages; a
 * mark's line goes before it. */
TEST_CASE(the_hitec_receive_help_follows_the_value)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        fresh_hub();
        ui_text_set_language((ui_lang_t)lang);
        settings_screen_set_hitec_rx(HITEC_RX_LINK_HELD);
        CHECK_EQ(settings_get_int(SET_HITEC_RX), 0);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX),
                  ui_setting_help(SET_HITEC_RX));
        settings_adjust(SET_HITEC_RX, 1);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX), TR(SET_HITEC_RX_PRE));
        CHECK(strcmp(TR(SET_HITEC_RX_PRE), ui_setting_help(SET_HITEC_RX))
              != 0);
        /* The marks, at PRECHARGE as SETUP shows them. */
        settings_screen_set_hitec_rx(HITEC_RX_LINK_OLD);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX), TR(SET_HITEC_RX_OLD));
        settings_screen_set_hitec_rx(HITEC_RX_LINK_NO_LINE);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX),
                  TR(SET_HITEC_RX_NO_LINE));
        settings_screen_set_hitec_rx(HITEC_RX_LINK_PENDING);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX), TR(SET_UNHELD));
        settings_screen_set_hitec_rx(HITEC_RX_LINK_HELD);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX), TR(SET_HITEC_RX_PRE));
        /* Back to PULL-UP: the setting's help again; another row keeps
         * its own. */
        settings_adjust(SET_HITEC_RX, 1);
        CHECK_STR_EQ(settings_screen_help(SET_HITEC_RX),
                  ui_setting_help(SET_HITEC_RX));
        CHECK_STR_EQ(settings_screen_help(SET_PDMINI_BAUD),
                  ui_setting_help(SET_PDMINI_BAUD));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The heading shows its text in the accent colour in both languages, and
 * its row has no key. */
TEST_CASE(a_heading_shows_its_text_and_has_no_keys)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh_screen();
        gesture_screen(SET_CAT_IFACE);
        ui_router_render(&s_c, 0);
        int accent = 0;
        for (int y = LIST_Y; y < LIST_Y + ROW_H; ++y) {
            for (int x = 0; x < 300; ++x) {
                accent += (gfx_pixel_get(&s_c, x, y) == UI_ACCENT) ? 1 : 0;
            }
        }
        CHECK(accent > 40);
        /* No key column pixel above the rule differs from the row's
         * background. */
        const gfx_color_t bg = gfx_pixel_get(&s_c, MINUS_X, LIST_Y + 2);
        for (int y = LIST_Y + 2; y < LIST_Y + ROW_H - 4; ++y) {
            CHECK(gfx_pixel_get(&s_c, MINUS_X + 4, y) == bg);
            CHECK(gfx_pixel_get(&s_c, PLUS_X + BTN_W - 4, y) == bg);
        }
        feed_tap(FEED_LONE, XM, LIST_Y + ROW_H / 2);
        feed_tap(FEED_LONE, XP, LIST_Y + ROW_H / 2);
        feed_tap(FEED_LONE, XL, LIST_Y + ROW_H / 2);
        CHECK_EQ(changed(), 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(interfaces_has_five_headings_above_its_sections);
    RUN(the_hitec_receive_row_is_marked_as_the_iomcu_holds_it);
    RUN(the_hitec_receive_help_follows_the_value);
    RUN(a_heading_shows_its_text_and_has_no_keys);
    RUN(a_held_setting_is_left_by_every_call_that_changes_one);
    RUN(setup_changes_no_held_setting);
    RUN(the_encoder_settings_start_off_with_a_12_bit_centre);
    RUN(screen_switches_category_and_renders);
    RUN(plus_and_minus_change_the_setting_under_them);
    RUN(a_setup_key_notifies_once_per_press);
    RUN(changing_the_theme_from_the_screen_takes_effect);
    RUN(a_theme_stepped_back_shows_saved_on_the_next_frame);
    RUN(a_drag_scrolls_instead_of_pressing);
    RUN(holding_a_key_repeats_with_acceleration);
    RUN(reset_button_restores_the_open_category);
    RUN(leaving_the_screen_no_longer_saves_on_its_own);
    RUN(the_save_button_asks_and_the_application_decides_when);
    RUN(the_request_outlives_the_screen);
    RUN(a_press_while_the_save_is_pending_changes_nothing);
    RUN(asking_with_nothing_to_write_asks_for_nothing);
    RUN(a_cancelled_hit_presses_nothing);
    RUN(a_refused_save_keeps_the_values_dirty);
    RUN(a_missing_store_is_a_failed_save);
    RUN(a_successful_save_retires_an_earlier_failure);
    RUN(an_edit_retires_an_earlier_failure);
    RUN(the_idle_save_reports_a_refusal);
    RUN(a_reset_that_changes_nothing_keeps_the_failure);
    RUN(a_refused_pending_write_repaints_the_button);
    return test_summary("settings_screen");
}
