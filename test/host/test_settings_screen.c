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
    tap(MINUS_X + BTN_W / 2, row_y(1));
    CHECK(settings_get_int(ids[1]) != row1);
    CHECK_EQ(settings_get_int(ids[0]), before);
}

/*
 * The operator's own path: the key under a SETUP row that the IOmcu (the
 * RP2350 board) is sent, INA228 SHUNT on SENSORS.
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
    fresh_hub();
    open_sensor(SET_INA228_UOHM);
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
    fresh_hub();
    open_sensor(SET_INA228_UOHM);
    setting_id_t ids[64];
    settings_in_category(SET_CAT_SENSORS, ids, 64);

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
    settings_set(SET_PACK_CELLS, 4);

    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(settings_get_int(ids[0]), (long)settings_def(ids[0])->def);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);   /* other category kept */
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

    /* Press the tile of the outputs screen, then the events stop
     * arriving. */
    const int dx = tile_x(OUTPUTS_TILE);
    const int dy = tile_y(OUTPUTS_TILE);
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
 * bits, in SENSORS beside the sensor bus. */
TEST_CASE(the_encoder_settings_start_off_with_a_12_bit_centre)
{
    settings_set_store(NULL);
    settings_init();
    CHECK_EQ(settings_get_int(SET_ENC_EN), 0);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
    const setting_def_t *d = settings_def(SET_ENC_EN);
    CHECK_EQ(d->type, SET_TYPE_BOOL);
    CHECK_EQ(d->cat, SET_CAT_SENSORS);
    CHECK_STR_EQ(settings_label(SET_ENC_EN), "AS5600");
    d = settings_def(SET_ENC_CENTRE);
    CHECK_EQ(d->type, SET_TYPE_INT);
    CHECK_EQ(d->cat, SET_CAT_SENSORS);
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
        /* OUTPUTS, PORT 1 to PORT 4: a step moves to the next port. */
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), (int)target + 1);
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
        feed_tap(FEED_LONE, XM, y);
        CHECK(settings_get(s_row_held) != s_before[s_row_held]);
        feed_tap(FEED_LONE, XM, row_y(1 - row));
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

/* ------------------------------------------------- Screen rotation */

/* SETUP's ARMED source, as the panel installs it (panel_boot.c). */
static bool router_armed(void)
{
    return ui_router_status()->armed;
}

static void set_bench_armed(bool armed)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = armed;
    ui_router_set_status(&st);
}

/* The sum of the pixels of the ">" key of PANEL's row @p index. */
static unsigned long plus_key_sum(int index)
{
    unsigned long sum = 0;
    const int y0 = row_y(index) - 20;
    for (int y = y0; y < y0 + 40; ++y) {
        for (int x = PLUS_X + 4; x < PLUS_X + BTN_W - 4; ++x) {
            sum += (unsigned long)s_fb[y * W + x];
        }
    }
    return sum;
}

TEST_CASE(screen_rotation_steps_when_safe_and_is_locked_while_armed)
{
    fresh_hub();
    settings_set_armed(router_armed);
    set_bench_armed(false);
    open_category(SET_CAT_APP);
    setting_id_t ids[32];
    const int n = settings_in_category(SET_CAT_APP, ids, 32);
    int row = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == SET_SCREEN_ROT) {
            row = i;
        }
    }
    CHECK(row >= 0);
    if (row < 0) {
        settings_set_armed(NULL);
        return;
    }
    /* The row is in view with the list at its top: rows 0 to 5 show. */
    CHECK(row < 6);

    /* SAFE: ">" steps NORMAL -> 180 and back, "<" too. */
    tap(PLUS_X + BTN_W / 2, row_y(row));
    CHECK_EQ(settings_get_int(SET_SCREEN_ROT), 1);
    tap(MINUS_X + BTN_W / 2, row_y(row));
    CHECK_EQ(settings_get_int(SET_SCREEN_ROT), 0);
    ui_router_render(&s_c, 0);
    const unsigned long keys_safe = plus_key_sum(row);

    /* ARMED: the next frame draws the row without keys, and a tap on
     * where they were changes nothing. */
    set_bench_armed(true);
    ui_router_tick(0.016f);
    ui_router_render(&s_c, 0);
    const unsigned long keys_armed = plus_key_sum(row);
    CHECK(keys_armed != keys_safe);
    tap(PLUS_X + BTN_W / 2, row_y(row));
    tap(MINUS_X + BTN_W / 2, row_y(row));
    CHECK_EQ(settings_get_int(SET_SCREEN_ROT), 0);
    /* RESET CATEGORY while ARMED leaves it too. */
    settings_set_armed(NULL);
    settings_set(SET_SCREEN_ROT, 1.0f);
    settings_set_armed(router_armed);
    tap(RESET_X + 40, RESET_Y + RESET_H / 2);
    CHECK_EQ(settings_get_int(SET_SCREEN_ROT), 1);
    /* Brightness, the row two above, steps while ARMED as before. */
    const int bright = settings_get_int(SET_BRIGHTNESS);
    tap(MINUS_X + BTN_W / 2, row_y(1));
    CHECK(settings_get_int(SET_BRIGHTNESS) != bright);
    tap(PLUS_X + BTN_W / 2, row_y(1));
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS), bright);

    /* SAFE again: the keys are back on the next frame and step. */
    set_bench_armed(false);
    ui_router_tick(0.016f);
    ui_router_render(&s_c, 0);
    CHECK_EQ(plus_key_sum(row), keys_safe);
    tap(PLUS_X + BTN_W / 2, row_y(row));
    CHECK_EQ(settings_get_int(SET_SCREEN_ROT), 0);
    settings_set_armed(NULL);
}

/*
 * The turn is the panel's scan (display.h): no screen draws differently.
 * A frame drawn with Screen rotation 180 is the frame drawn with NORMAL,
 * pixel for pixel, whole and in the partial repaint of the second
 * buffer, so the dirty rectangles, the double-buffer copy and the band
 * need no mapping.  The hub is drawn, where the value shows nowhere.
 */
TEST_CASE(screen_rotation_draws_the_same_frame_buffer)
{
    static gfx_color_t *normal;
    if (normal == NULL) {
        normal = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(normal);
    }
    for (int turned = 0; turned < 2; ++turned) {
        fresh_hub();
        /* Saved, so neither frame shows a value unsaved. */
        settings_set_store(&s_mem_store);
        settings_set(SET_SCREEN_ROT, (float)turned);
        CHECK(settings_save());
        CHECK(!settings_dirty());
        ui_router_render(&s_c, 0);
        ui_router_render(&s_c, 1);
        ui_router_tick(0.016f);
        ui_router_render(&s_c, 0);
        if (turned == 0) {
            memcpy(normal, s_fb, (size_t)W * H * sizeof(gfx_color_t));
        } else {
            CHECK(memcmp(normal, s_fb, (size_t)W * H * sizeof(gfx_color_t))
                  == 0);
        }
    }
    settings_set_store(NULL);
}

/*
 * Demo mode stepped from the screen: the SIMULATION mark leaves both
 * framebuffers on the next frames, with nothing of it left behind, and
 * stepping back brings it back.  The framebuffers are never cleared, as the
 * panel's are not.
 */
TEST_CASE(demo_mode_stepped_on_the_screen_takes_the_mark_off_both_buffers)
{
    enum { PX = UI_PANEL_W * UI_PANEL_H };
    fresh_screen();
    ui_bench_status_t st = *ui_router_status();
    st.simulated = true;
    ui_router_set_status(&st);

    gfx_color_t *buf[2];
    gfx_canvas_t cvs[2];
    for (int i = 0; i < 2; ++i) {
        buf[i] = calloc(PX, sizeof(gfx_color_t));
        REQUIRE_ALLOC(buf[i]);
        gfx_canvas_init(&cvs[i], buf[i], UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    }
    gfx_color_t *fresh_on = calloc(PX, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fresh_on);
    gfx_canvas_t fc;
    gfx_canvas_init(&fc, fresh_on, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);

    ui_router_render(&cvs[0], 0);
    ui_router_render(&cvs[1], 1);

    /* The row is past the 6 that fit: scroll to the end of the list. */
    for (int i = 0; i < 6; ++i) {
        swipe(300, UI_BAND_H + 400, 300, UI_BAND_H + 100, 8);
    }
    const int row = row_of(SET_DEMO_MODE);
    CHECK(row >= 0);
    const int y = row_y(row) - settings_screen_scroll(NULL);
    CHECK(y > UI_BAND_H && y < UI_PANEL_H);

    tap(PLUS_X + BTN_W / 2, y);
    CHECK(settings_get_bool(SET_DEMO_MODE));
    ui_router_render(&cvs[0], 0);
    ui_router_render(&cvs[1], 1);

    /* The same screen on a bench that is not simulated, drawn from nothing. */
    st.simulated = false;
    ui_router_set_status(&st);
    ui_router_render(&fc, 0);
    st.simulated = true;
    ui_router_set_status(&st);
    long left0 = 0, left1 = 0;
    for (int i = 0; i < PX; ++i) {
        left0 += (buf[0][i] != fresh_on[i]);
        left1 += (buf[1][i] != fresh_on[i]);
    }
    CHECK_EQ(left0, 0);
    CHECK_EQ(left1, 0);

    tap(MINUS_X + BTN_W / 2, y);
    CHECK(!settings_get_bool(SET_DEMO_MODE));
    ui_router_render(&cvs[0], 0);
    ui_router_render(&cvs[1], 1);
    long back = 0;
    for (int i = 0; i < PX; ++i) {
        back += (buf[0][i] != buf[1][i]);
    }
    CHECK_EQ(back, 0);
    long off_mark = 0;
    for (int i = 0; i < PX; ++i) {
        off_mark += (buf[0][i] != fresh_on[i]);
    }
    CHECK(off_mark > 3000);
    free(fresh_on);
    free(buf[0]);
    free(buf[1]);
}

int main(void)
{
    RUN(a_held_setting_is_left_by_every_call_that_changes_one);
    RUN(setup_changes_no_held_setting);
    RUN(the_encoder_settings_start_off_with_a_12_bit_centre);
    RUN(screen_switches_category_and_renders);
    RUN(plus_and_minus_change_the_setting_under_them);
    RUN(a_setup_key_notifies_once_per_press);
    RUN(changing_the_theme_from_the_screen_takes_effect);
    RUN(demo_mode_stepped_on_the_screen_takes_the_mark_off_both_buffers);
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
    RUN(screen_rotation_steps_when_safe_and_is_locked_while_armed);
    RUN(screen_rotation_draws_the_same_frame_buffer);
    return test_summary("settings_screen");
}
