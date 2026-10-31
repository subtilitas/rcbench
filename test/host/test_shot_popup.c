/**
 * @file
 * @brief The screenshot's gate and its popup against the bench, the screen and
 *        the card.
 *
 * SPDX-License-Identifier: MIT
 */
#include "shot_rig.h"

/* -------------------------------------------------------------- the gate */

/**
 * @brief All facts true but one.
 * @param off  the field's offset, or sizeof for none
 * @return the facts
 */
static shot_facts_t all_but(size_t off)
{
    shot_facts_t f;
    memset(&f, 1, sizeof(f));
    if (off < sizeof(f)) {
        *((bool *)&f + off) = false;
    }
    return f;
}

TEST_CASE(the_gate_is_true_only_with_every_fact_idle)
{
    static const struct {
        size_t off;
        shot_wait_t why;
    } k[] = {
        { offsetof(shot_facts_t, link_known), SHOT_WAIT_LINK },
        { offsetof(shot_facts_t, safe), SHOT_WAIT_ARMED },
        { offsetof(shot_facts_t, stop_served), SHOT_WAIT_STOP },
        { offsetof(shot_facts_t, supply_off), SHOT_WAIT_SUPPLY },
        { offsetof(shot_facts_t, prog5v_off), SHOT_WAIT_PROG5V },
        { offsetof(shot_facts_t, port_free), SHOT_WAIT_PORT },
        { offsetof(shot_facts_t, no_session), SHOT_WAIT_SESSION },
        { offsetof(shot_facts_t, no_program_io), SHOT_WAIT_PROGRAM_IO },
        { offsetof(shot_facts_t, no_motion), SHOT_WAIT_MOTION },
        { offsetof(shot_facts_t, no_test), SHOT_WAIT_TEST },
        { offsetof(shot_facts_t, no_stick_run), SHOT_WAIT_STICK },
        { offsetof(shot_facts_t, no_update), SHOT_WAIT_UPDATE },
        { offsetof(shot_facts_t, no_fault), SHOT_WAIT_FAULT },
        { offsetof(shot_facts_t, no_card_fault), SHOT_WAIT_CARD_FAULT },
        { offsetof(shot_facts_t, no_recording), SHOT_WAIT_RECORDING },
    };
    /* Every fact has its row, and every row one fact. */
    CHECK_EQ(sizeof(k) / sizeof(k[0]), sizeof(shot_facts_t));
    CHECK_EQ(sizeof(k) / sizeof(k[0]), SHOT_WAIT_COUNT - 1);
    const shot_facts_t idle = all_but(sizeof(shot_facts_t));
    CHECK_EQ(shot_gate(&idle), SHOT_IDLE);
    CHECK(shot_gate_idle(&idle));
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        const shot_facts_t f = all_but(k[i].off);
        CHECK_EQ(shot_gate(&f), k[i].why);
        CHECK(!shot_gate_idle(&f));
    }
    const shot_facts_t none = { 0 };
    CHECK(!shot_gate_idle(&none));
    CHECK(!shot_gate_idle(NULL));
}

/* ------------------------------------------------------------- the popup */

/**
 * @brief The readings of an idle bench with one set by the case.
 * @param off  the field's offset; sizeof for none
 * @return the bench
 */
static shot_bench_t bench_with(size_t off)
{
    shot_bench_t b = { .read = true, .link_up = true };
    if (off < sizeof(b)) {
        *((bool *)&b + off) = true;
    }
    return b;
}

TEST_CASE(each_reading_of_the_panel_reaches_its_fact)
{
    start();
    shot_facts_t f;
    const shot_bench_t idle = bench_with(sizeof(shot_bench_t));
    shot_facts_read(&idle, &f);
    CHECK_EQ(shot_gate(&f), SHOT_IDLE);
    static const struct {
        size_t off;
        shot_wait_t why;
    } k[] = {
        { offsetof(shot_bench_t, armed), SHOT_WAIT_ARMED },
        { offsetof(shot_bench_t, arm_wanted), SHOT_WAIT_ARMED },
        { offsetof(shot_bench_t, stop_request), SHOT_WAIT_STOP },
        { offsetof(shot_bench_t, supply_on), SHOT_WAIT_SUPPLY },
        { offsetof(shot_bench_t, supply_sample), SHOT_WAIT_SUPPLY },
        { offsetof(shot_bench_t, supply_coming), SHOT_WAIT_SUPPLY },
        { offsetof(shot_bench_t, prog5v_on), SHOT_WAIT_PROG5V },
        { offsetof(shot_bench_t, prog5v_coming), SHOT_WAIT_PROG5V },
        { offsetof(shot_bench_t, hitec_holds), SHOT_WAIT_PORT },
        { offsetof(shot_bench_t, card_job_out), SHOT_WAIT_PROGRAM_IO },
        { offsetof(shot_bench_t, testing), SHOT_WAIT_TEST },
        { offsetof(shot_bench_t, cu_active), SHOT_WAIT_UPDATE },
        { offsetof(shot_bench_t, update_busy), SHOT_WAIT_UPDATE },
        { offsetof(shot_bench_t, fault_log_out), SHOT_WAIT_CARD_FAULT },
        { offsetof(shot_bench_t, run_open), SHOT_WAIT_RECORDING },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        const shot_bench_t b = bench_with(k[i].off);
        shot_facts_read(&b, &f);
        CHECK_EQ(shot_gate(&f), k[i].why);
    }
    shot_bench_t b = bench_with(sizeof(shot_bench_t));
    b.faults = 0x40u;
    shot_facts_read(&b, &f);
    CHECK_EQ(shot_gate(&f), SHOT_WAIT_FAULT);
    /* No link, and the simulated bench that stands in for it. */
    b = bench_with(sizeof(shot_bench_t));
    b.link_up = false;
    shot_facts_read(&b, &f);
    CHECK_EQ(shot_gate(&f), SHOT_WAIT_LINK);
    b.simulated = true;
    shot_facts_read(&b, &f);
    CHECK_EQ(shot_gate(&f), SHOT_IDLE);
    /* Readings nobody filled in. */
    const shot_bench_t none = { .link_up = true };
    shot_facts_read(&none, &f);
    CHECK(!shot_gate_idle(&f));
    shot_facts_read(NULL, &f);
    CHECK(!shot_gate_idle(&f));
    /* What SUPPLY says of a port. */
    supply_screen_set_port_held(true);
    shot_facts_read(&idle, &f);
    CHECK_EQ(shot_gate(&f), SHOT_WAIT_PORT);
    supply_screen_set_port_held(false);
}

TEST_CASE(armed_a_hold_acts_on_no_control_and_disarm_stays_live)
{
    start();
    ui_router_goto(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    ui_bench_status_t st = status();
    st.armed = true;
    st.armed_roles = UI_ARMED_MOTOR;
    ui_router_set_status(&st);
    ui_router_shot_set_idle(false);
    motor_cmd_t c;
    while (motor_screen_poll_cmd(&c)) { }

    /* A whole hold: no command. */
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    CHECK(ui_shot_held());
    CHECK(!motor_screen_poll_cmd(&c));

    /* DISARM's press during a hold: it acts on its press, and ends the
     * hold. */
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(500);
    touch(MO_ARM_X, UI_BAND_H + MO_ARM_Y, TOUCH_EVENT_DOWN, 2);
    REQUIRE(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    wait_ms(1000);
    CHECK(!ui_shot_wanted());
    touch(MO_ARM_X, UI_BAND_H + MO_ARM_Y, TOUCH_EVENT_UP, 2);
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);

    /* The disarm comes through: the popup after the finger has lifted, and
     * the press that disarmed was no press of the popup. */
    ui_router_shot_set_idle(true);
    CHECK(ui_shot_open());
    motor_screen_set_armed(false);
}

/**
 * @brief Tap the key that shows @p ch on the popup's keyboard.
 * @param ch  the character, on the layer showing
 */
static void type_char(char ch)
{
    const char want[2] = { ch, '\0' };
    for (int key = 0; key < UI_TK_DEL; ++key) {
        if (strcmp(ui_textkey_key_label(ui_shot_keyboard(), key), want) == 0) {
            tap_key(key);
            return;
        }
    }
    T_FAIL("no key shows %c", ch);
}

/**
 * @brief Clear the name and type @p text in capitals, then tap SAVE.
 * @param text  the name, in capitals and digits
 */
static void type_and_save(const char *text)
{
    tap_key(UI_TK_CLR);
    for (const char *p = text; *p != '\0'; ++p) {
        type_char(*p);
    }
    tap_key(UI_TK_OK);
}

TEST_CASE(names_the_rules_refuse_save_nothing_and_stay)
{
    /* A device name, alone. */
    start();
    open_popup();
    g_answered = true;
    ui_router_tick(0.016f);
    g_job = UI_SHOT_JOB_NONE;
    type_and_save("CON");
    CHECK(ui_shot_open());
    CHECK(g_job != UI_SHOT_JOB_SAVE);
    CHECK_EQ(ui_shot_keyboard()->why, FM_NAME_RESERVED);
    /* A space at the end is the keyboard's refusal as well. */
    tap_key(UI_TK_CLR);
    type_char('A');
    tap_key(UI_TK_SPACE);
    tap_key(UI_TK_OK);
    CHECK(g_job != UI_SHOT_JOB_SAVE);
    CHECK_EQ(ui_shot_keyboard()->why, FM_NAME_EDGE);
    /* 55 bytes go in; the 56th is not typed, and it saves. */
    tap_key(UI_TK_CLR);
    for (int i = 0; i < 60; ++i) {
        type_char('A');
    }
    CHECK_EQ(strlen(ui_shot_name()), SHOT_BASE_MAX);
    tap_key(UI_TK_OK);
    CHECK_EQ(g_job, UI_SHOT_JOB_SAVE);
    CHECK_EQ(strlen(g_base), SHOT_BASE_MAX);
    /* And a name the file's own check refuses never reaches the task: the
     * extension counts. */
    CHECK_EQ(shot_name_check(g_base), FM_NAME_OK);
}

TEST_CASE(armed_the_picture_waits_and_stop_and_stays_live)
{
    start();
    ui_bench_status_t st = status();
    st.armed = true;
    ui_router_set_status(&st);
    ui_router_shot_set_idle(false);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    ui_router_tick(0.016f);
    CHECK(ui_shot_held());
    CHECK(!ui_shot_open());
    CHECK_STR_EQ(ui_router_alert(), TR(SHOT_WAITS));
    /* STOP while it waits. */
    const gfx_rect_t stop = ui_band_stop_rect();
    touch(stop.x + 5, 20, TOUCH_EVENT_DOWN, 2);
    CHECK(ui_router_take_stop());
    touch(stop.x + 5, 20, TOUCH_EVENT_UP, 2);
    /* Idle: it opens, on the next frame. */
    ui_router_shot_set_idle(true);
    CHECK(ui_shot_open());
}

TEST_CASE(the_finger_that_made_the_bench_idle_presses_nothing)
{
    start();
    ui_router_shot_set_idle(false);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    /* A finger on the glass as the bench turns idle: no popup yet. */
    touch(700, 300, TOUCH_EVENT_DOWN, 2);
    ui_router_shot_set_idle(true);
    ui_router_tick(0.016f);
    CHECK(!ui_shot_open());
    touch(700, 300, TOUCH_EVENT_UP, 2);
    ui_router_tick(0.016f);
    REQUIRE(ui_shot_open());

    /* Lost touch: the table clears with the finger still down, the popup
     * opens, and that finger's release on a key presses nothing. */
    start();
    ui_router_shot_set_idle(false);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    touch(700, 300, TOUCH_EVENT_DOWN, 2);
    ui_router_cancel_gestures();
    ui_router_shot_set_idle(true);
    REQUIRE(ui_shot_open());
    const gfx_rect_t cancel = ui_shot_key_rect(UI_TK_CANCEL);
    touch(cancel.x + 5, cancel.y + 5 + UI_BAND_H, TOUCH_EVENT_UP, 2);
    CHECK(ui_shot_open());
    /* A new press on it acts. */
    tap_key(UI_TK_CANCEL);
    CHECK(!ui_shot_held());
}

TEST_CASE(not_idle_closes_the_popup_and_the_picture_waits_with_its_name)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    tap_key(UI_TK_CLR);
    tap_key(10 + 1);        /* a letter */
    char typed[80];
    snprintf(typed, sizeof(typed), "%s", ui_shot_name());
    CHECK(typed[0] != '\0');
    ui_router_shot_set_idle(false);
    CHECK(!ui_shot_open());
    CHECK(ui_shot_held());
    CHECK_EQ(g_releases, 0);
    ui_router_shot_set_idle(true);
    CHECK(ui_shot_open());
    CHECK_STR_EQ(ui_shot_name(), typed);
}

TEST_CASE(a_fault_screen_closes_it_and_it_waits_for_the_acknowledge)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    ui_router_goto(SCREEN_BUSFAULT);
    CHECK(!ui_shot_open());
    CHECK(ui_shot_held());
    ui_router_tick(0.016f);
    CHECK(!ui_shot_open());
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(ui_shot_open());
}

TEST_CASE(a_keypad_or_question_keeps_it_shut_until_closed)
{
    start();
    ui_router_shot_set_idle(false);
    ui_router_goto(SCREEN_SUPPLY);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    tap(800, UI_BAND_H + 88);          /* the voltage's keypad */
    REQUIRE(supply_screen_keypad()->open);
    ui_router_shot_set_idle(true);
    ui_router_tick(0.016f);
    CHECK(!ui_shot_open());
    CHECK(ui_shot_held());
    /* The home tag closes it; then the popup opens. */
    const gfx_rect_t tag = ui_home_tag_rect(ui_router_title(SCREEN_SUPPLY));
    tap(tag.x + 5, 30);
    ui_router_tick(0.016f);
    CHECK(ui_shot_open());
}

TEST_CASE(screens_that_cannot_say_keep_it_shut)
{
    start();
    ui_router_shot_set_idle(false);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    ui_router_goto(SCREEN_COPROC_UPDATE);
    ui_router_shot_set_idle(true);
    ui_router_tick(0.016f);
    CHECK(!ui_shot_open());
    ui_router_goto(SCREEN_UPD_REMIND);
    ui_router_tick(0.016f);
    CHECK(!ui_shot_open());
    /* Every screen the menu reaches can say. */
    static const ui_screen_id_t k_menu[] = {
        SCREEN_OVERVIEW, SCREEN_MOTOR, SCREEN_SERVO, SCREEN_ANALYSER,
        SCREEN_LOGS, SCREEN_SETUP, SCREEN_BATTERY, SCREEN_BALANCE,
        SCREEN_OUTPUTS, SCREEN_SUPPLY, SCREEN_WIFI, SCREEN_UPDATES,
        SCREEN_FILES, SCREEN_LAYOUT,
    };
    for (size_t i = 0u; i < sizeof(k_menu) / sizeof(k_menu[0]); ++i) {
        ui_router_shot_set_idle(false);
        ui_router_goto(k_menu[i]);
        ui_router_shot_set_idle(true);
        if (!ui_shot_open()) {
            T_FAIL("no popup over screen %d", (int)k_menu[i]);
        }
    }
}

TEST_CASE(stop_acts_over_the_popup_and_the_band_stays_the_tag_does_nothing)
{
    start();
    ui_router_goto(SCREEN_SUPPLY);
    open_popup();
    REQUIRE(ui_shot_open());
    const uint32_t navs = ui_router_navigations();
    const gfx_rect_t stop = ui_band_stop_rect();
    touch(stop.x + 5, 20, TOUCH_EVENT_DOWN, 1);
    CHECK(ui_router_take_stop());
    touch(stop.x + 5, 20, TOUCH_EVENT_UP, 1);
    const gfx_rect_t tag = ui_home_tag_rect(ui_router_title(SCREEN_SUPPLY));
    tap(tag.x + 5, 30);
    CHECK_EQ(ui_router_navigations(), navs);
    CHECK(ui_shot_open());
    CHECK(!ui_router_take_knob());
    /* A hold in the gap over the popup is none. */
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1500);
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    CHECK(!ui_shot_wanted());
    CHECK_EQ(g_allocs, 1);
    /* No screen is handed anything under it. */
    const uint32_t d = ui_router_dispatched();
    tap(100, 500);
    CHECK_EQ(ui_router_dispatched(), d);
}

TEST_CASE(the_menus_guide_chip_goes_with_the_popup_and_save_reads_save)
{
    start();
    CHECK(ui_router_guide_shown());
    open_popup();
    REQUIRE(ui_shot_open());
    CHECK(!ui_router_guide_shown());
    CHECK_STR_EQ(ui_textkey_key_label(ui_shot_keyboard(), UI_TK_OK),
                 TR(SHOT_SAVE));
    /* A press on a key slid onto the band acts on nothing. */
    const gfx_rect_t cancel = ui_shot_key_rect(UI_TK_CANCEL);
    touch(cancel.x + 5, cancel.y + 5 + UI_BAND_H, TOUCH_EVENT_DOWN, 1);
    touch(gap_x(), 30, TOUCH_EVENT_MOVE, 1);
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    CHECK(ui_shot_open());
}

TEST_CASE(the_preview_is_of_the_picture_that_replaced_the_last)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    ui_router_shot_set_idle(false);         /* the picture waits, previewed */
    gfx_clear(&cv, GFX_RED);
    ui_router_goto(SCREEN_SUPPLY);
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    ui_router_shot_set_idle(true);
    REQUIRE(ui_shot_open());
    const gfx_color_t *img = ui_shot_image();
    const gfx_color_t *pv = ui_shot_preview();
    int differ = 0;
    for (int y = 0; y < UI_SHOT_PREVIEW_H; ++y) {
        for (int x = 0; x < UI_SHOT_PREVIEW_W; ++x) {
            unsigned r = 0u, g = 0u, b = 0u;
            for (int j = 0; j < 4; ++j) {
                for (int i = 0; i < 4; ++i) {
                    const unsigned v = img[(y * 4 + j) * W + x * 4 + i];
                    r += v >> 11;
                    g += (v >> 5) & 63u;
                    b += v & 31u;
                }
            }
            const unsigned want = (((r + 8u) / 16u) << 11)
                                  | (((g + 8u) / 16u) << 5) | ((b + 8u) / 16u);
            if (pv[y * UI_SHOT_PREVIEW_W + x] != want) {
                ++differ;
            }
        }
    }
    CHECK_EQ(differ, 0);
}

TEST_CASE(the_default_name_is_the_next_number_a_typed_one_stays)
{
    start();
    g_number = 42u;
    open_popup();
    CHECK_EQ(g_job, UI_SHOT_JOB_NEXT);
    CHECK_STR_EQ(ui_shot_name(), "SHOT0001");
    g_answered = true;
    ui_router_tick(0.016f);
    CHECK_STR_EQ(ui_shot_name(), "SHOT0042");

    start();
    g_number = 42u;
    open_popup();
    tap_key(UI_TK_DEL);
    g_answered = true;
    ui_router_tick(0.016f);
    CHECK_STR_EQ(ui_shot_name(), "SHOT000");
}

TEST_CASE(save_shows_progress_and_a_good_write_ends_the_picture)
{
    start();
    open_popup();
    g_err = FM_OK;
    g_answered = true;      /* the number */
    ui_router_tick(0.016f);
    tap_key(UI_TK_OK);
    CHECK_EQ(g_job, UI_SHOT_JOB_SAVE);
    CHECK_STR_EQ(g_base, "SHOT0001");
    CHECK(g_job_px == ui_shot_image());
    frame();
    /* Keys but CANCEL take nothing while it writes. */
    tap_key(UI_TK_DEL);
    CHECK_STR_EQ(ui_shot_name(), "SHOT0001");
    g_answered = true;
    ui_router_tick(0.016f);
    CHECK(!ui_shot_held());
    CHECK(!ui_shot_open());
    CHECK_EQ(g_releases, 1);
    REQUIRE(ui_router_alert() != NULL);
    CHECK(strstr(ui_router_alert(), "screenshots/SHOT0001.PNG") != NULL);
}

TEST_CASE(a_card_error_stays_on_the_popup_with_the_picture)
{
    static const struct {
        fm_err_t err;
        ui_text_id_t text;
    } k[] = {
        { FM_ERR_NO_CARD, TX_SHOT_E_NOCARD },
        { FM_ERR_CARD_GONE, TX_SHOT_E_NOCARD },
        { FM_ERR_FULL, TX_SHOT_E_FULL },
        { FM_ERR_EXISTS, TX_SHOT_E_EXISTS },
        { FM_ERR_DENIED, TX_SHOT_E_DENIED },
        { FM_ERR_NAME, TX_SHOT_E_NAME },
        { FM_ERR_BUSY, TX_SHOT_E_BUSY },
        { SHOT_ERR_MEMORY, TX_SHOT_E_MEMORY },
        { FM_ERR_IO, TX_SHOT_E_WRITE },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        start();
        open_popup();
        g_answered = true;
        ui_router_tick(0.016f);
        tap_key(UI_TK_OK);
        g_err = k[i].err;
        g_answered = true;
        ui_router_tick(0.016f);
        CHECK(ui_shot_open());
        CHECK(ui_shot_image() != NULL);
        CHECK_STR_EQ(ui_shot_status(), ui_tr(k[i].text));
        frame();
        /* And SAVE again. */
        g_err = FM_OK;
        tap_key(UI_TK_OK);
        CHECK_EQ(g_job, UI_SHOT_JOB_SAVE);
        g_answered = true;
        ui_router_tick(0.016f);
        CHECK(!ui_shot_held());
    }
}

TEST_CASE(a_task_that_will_not_start_is_said)
{
    start();
    open_popup();
    g_answered = true;
    ui_router_tick(0.016f);
    g_start_fails = true;
    tap_key(UI_TK_OK);
    CHECK(ui_shot_open());
    CHECK_STR_EQ(ui_shot_status(), TR(SHOT_E_BUSY));
}

TEST_CASE(cancel_drops_the_picture)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    tap_key(UI_TK_CANCEL);
    CHECK(!ui_shot_held());
    CHECK_EQ(g_releases, 1);
    /* The next one starts over. */
    g_answered = true;
    ui_router_tick(0.016f);
    open_popup();
    CHECK(ui_shot_open());
    CHECK_STR_EQ(ui_shot_name(), "SHOT0001");
}

TEST_CASE(cancel_while_it_writes_stops_it_and_then_drops_the_picture)
{
    start();
    open_popup();
    g_answered = true;
    ui_router_tick(0.016f);
    tap_key(UI_TK_OK);
    tap_key(UI_TK_CANCEL);
    CHECK_EQ(g_cancels, 1);
    CHECK(!ui_shot_open());
    CHECK_EQ(g_releases, 0);        /* the task still reads it */
    /* A hold now takes no picture: its memory is the task's. */
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1000);
    frame();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(ui_router_alert(), TR(SHOT_BUSY));
    g_err = FM_ERR_CANCELLED;
    g_answered = true;
    ui_router_tick(0.016f);
    CHECK_EQ(g_releases, 1);
    CHECK(!ui_shot_held());
}

TEST_CASE(not_idle_while_it_writes_stops_it_and_the_picture_waits)
{
    start();
    open_popup();
    g_answered = true;
    ui_router_tick(0.016f);
    tap_key(UI_TK_OK);
    ui_router_shot_set_idle(false);
    CHECK_EQ(g_cancels, 1);
    CHECK(!ui_shot_open());
    /* Idle again before the answer: the popup waits for it. */
    ui_router_shot_set_idle(true);
    CHECK(!ui_shot_open());
    g_err = FM_ERR_CANCELLED;
    g_answered = true;
    ui_router_tick(0.016f);
    CHECK(ui_shot_open());
    CHECK(ui_shot_image() != NULL);
    CHECK_STR_EQ(ui_shot_status(), "");

    /* A write that finished whole as the state turned: the file is there. */
    start();
    open_popup();
    g_answered = true;
    ui_router_tick(0.016f);
    tap_key(UI_TK_OK);
    ui_router_shot_set_idle(false);
    g_err = FM_OK;
    g_answered = true;
    ui_router_tick(0.016f);
    CHECK(!ui_shot_held());
}

TEST_CASE(a_name_the_rules_refuse_is_said_and_saves_nothing)
{
    start();
    open_popup();
    g_answered = true;
    ui_router_tick(0.016f);
    for (int i = 0; i < 8; ++i) {
        tap_key(UI_TK_DEL);
    }
    tap_key(UI_TK_OK);      /* empty: the keyboard's own refusal */
    CHECK(ui_shot_open());
    CHECK(g_job != UI_SHOT_JOB_SAVE);
    frame();
}

TEST_CASE(the_popup_covers_the_screen_and_repaints_it_closed)
{
    start();
    frame();
    open_popup();
    REQUIRE(ui_shot_open());
    frame();
    /* The preview at 12, 12 of the body. */
    CHECK_EQ(gfx_pixel_get(&cv, 12, 12 + UI_BAND_H), ui_shot_preview()[0]);
    tap_key(UI_TK_CANCEL);
    frame();
    /* The tile back where the keyboard was. */
    CHECK(gfx_pixel_get(&cv, 300, 100) != UI_BG
          || gfx_pixel_get(&cv, 400, 200) != UI_BG);
}

TEST_CASE(a_picture_of_no_canvas_is_no_picture)
{
    start();
    ui_shot_want();
    ui_shot_capture(NULL, false);
    CHECK(!ui_shot_wanted());
    CHECK(!ui_shot_held());
    ui_shot_want();
    gfx_canvas_t small = cv;
    small.width = W - 1;
    ui_shot_capture(&small, false);
    CHECK(!ui_shot_held());
    CHECK_EQ(g_allocs, 0);
}

TEST_CASE(a_hold_that_ends_on_a_screen_without_the_band_takes_that_screen)
{
    start();
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1000);
    REQUIRE(ui_shot_wanted());
    ui_router_goto(SCREEN_BUSFAULT);
    frame();
    CHECK(!ui_shot_wanted());
    CHECK(ui_shot_held());
    CHECK(!ui_shot_open());
    const gfx_color_t *img = ui_shot_image();
    REQUIRE(img != NULL);
    CHECK(memcmp(img, fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);
}

TEST_CASE(a_lost_touch_event_drops_the_keys_press_and_the_popup_stays)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    const gfx_rect_t key = ui_shot_key_rect(UI_TK_CANCEL);
    const int x = key.x + key.w / 2;
    const int y = key.y + key.h / 2 + UI_BAND_H;
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    touch(x, y, TOUCH_EVENT_UP, 1);
    CHECK(ui_shot_open());
}

TEST_CASE(the_popup_draws_once_per_buffer_until_something_changes)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    ui_shot_render(NULL, 0);
    gfx_canvas_t *body = &cv;
    ui_shot_invalidate();
    ui_shot_render(body, 0);
    const gfx_color_t was = body->pixels[0];
    body->pixels[0] = (gfx_color_t)~was;
    ui_shot_render(body, 0);
    CHECK_EQ(body->pixels[0], (gfx_color_t)~was);
    /* The other buffer has its own turn. */
    body->pixels[0] = (gfx_color_t)~was;
    ui_shot_render(body, 1);
    CHECK_EQ(body->pixels[0], was);
    ui_shot_invalidate();
    body->pixels[0] = (gfx_color_t)~was;
    ui_shot_render(body, 0);
    CHECK_EQ(body->pixels[0], was);
    /* Closed, it draws nothing. */
    tap_key(UI_TK_CANCEL);
    REQUIRE(!ui_shot_held());
    body->pixels[0] = (gfx_color_t)~was;
    ui_shot_render(body, 0);
    CHECK_EQ(body->pixels[0], (gfx_color_t)~was);
}

TEST_CASE(a_press_on_the_alert_under_the_popup_is_the_alerts)
{
    start();
    open_popup();
    REQUIRE(ui_shot_open());
    ui_router_set_alert("FAULT");
    const int y = H - UI_ALERT_H / 2;
    touch(100, y, TOUCH_EVENT_DOWN, 1);
    touch(101, y, TOUCH_EVENT_DOWN, 2);
    CHECK(ui_router_alert() != NULL);
    touch(100, y, TOUCH_EVENT_UP, 1);
    touch(101, y, TOUCH_EVENT_UP, 2);
    CHECK(ui_router_alert() == NULL);
    CHECK(ui_shot_open());
    CHECK(g_job != UI_SHOT_JOB_SAVE);
}

TEST_CASE(a_press_in_the_gap_under_the_popup_closes_a_bubble_and_takes_nothing)
{
    start();
    ui_router_goto(SCREEN_SUPPLY);
    open_popup();
    REQUIRE(ui_shot_open());
    gfx_rect_t chip;
    REQUIRE(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_LINK, &chip));
    touch(chip.x + 1, 30, TOUCH_EVENT_DOWN, 2);
    touch(chip.x + 1, 30, TOUCH_EVENT_UP, 2);
    REQUIRE(ui_router_help() == UI_BAND_CHIP_LINK);
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    CHECK(ui_router_help() == UI_BAND_CHIP_NONE);
    wait_ms(1200);
    frame();
    CHECK(!ui_shot_wanted());
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(facts_with_nowhere_to_go_are_refused)
{
    const shot_bench_t idle = { .read = true };
    shot_facts_read(&idle, NULL);
    shot_facts_t f;
    memset(&f, 0xff, sizeof(f));
    shot_facts_read(NULL, &f);
    CHECK(shot_gate(&f) != SHOT_IDLE);
}

int main(void)
{
    RUN(the_gate_is_true_only_with_every_fact_idle);
    RUN(each_reading_of_the_panel_reaches_its_fact);
    RUN(armed_a_hold_acts_on_no_control_and_disarm_stays_live);
    RUN(names_the_rules_refuse_save_nothing_and_stay);
    RUN(armed_the_picture_waits_and_stop_and_stays_live);
    RUN(the_finger_that_made_the_bench_idle_presses_nothing);
    RUN(not_idle_closes_the_popup_and_the_picture_waits_with_its_name);
    RUN(a_fault_screen_closes_it_and_it_waits_for_the_acknowledge);
    RUN(a_keypad_or_question_keeps_it_shut_until_closed);
    RUN(screens_that_cannot_say_keep_it_shut);
    RUN(stop_acts_over_the_popup_and_the_band_stays_the_tag_does_nothing);
    RUN(the_menus_guide_chip_goes_with_the_popup_and_save_reads_save);
    RUN(the_preview_is_of_the_picture_that_replaced_the_last);
    RUN(the_default_name_is_the_next_number_a_typed_one_stays);
    RUN(save_shows_progress_and_a_good_write_ends_the_picture);
    RUN(a_card_error_stays_on_the_popup_with_the_picture);
    RUN(a_task_that_will_not_start_is_said);
    RUN(cancel_drops_the_picture);
    RUN(cancel_while_it_writes_stops_it_and_then_drops_the_picture);
    RUN(not_idle_while_it_writes_stops_it_and_the_picture_waits);
    RUN(a_name_the_rules_refuse_is_said_and_saves_nothing);
    RUN(the_popup_covers_the_screen_and_repaints_it_closed);
    RUN(a_picture_of_no_canvas_is_no_picture);
    RUN(a_hold_that_ends_on_a_screen_without_the_band_takes_that_screen);
    RUN(a_lost_touch_event_drops_the_keys_press_and_the_popup_stays);
    RUN(the_popup_draws_once_per_buffer_until_something_changes);
    RUN(a_press_on_the_alert_under_the_popup_is_the_alerts);
    RUN(a_press_in_the_gap_under_the_popup_closes_a_bubble_and_takes_nothing);
    RUN(facts_with_nowhere_to_go_are_refused);
    return test_summary("shot_popup");
}
