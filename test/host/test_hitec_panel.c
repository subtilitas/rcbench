/*
 * The Hitec programmer's overlay (shared/ui/hitec_panel*.c) on the whole
 * loopback of hitec_prog_rig.h: the SERIES row before CONNECT and kept in
 * the settings, a change of series dropping the other series' read; CONNECT, the rows a read fills; the PORT row naming the
 * port and its GPIO in every state; CLOSE while the port is held opening
 * LEAVE, and a hold of 2s leaving; a port another session holds offering
 * RELEASE PORT; a classic servo with no PD mini refused with its reason;
 * the supply's lock while a port is held; the restore question after a
 * LEAVE and after no other end of the hold; every control a tap target above
 * the alert band, every label and value inside its field and every note
 * above the band, in English and German, for both series.
 *
 * SPDX-License-Identifier: MIT
 */
#include "shot_facts_rig.h"
#include "hitec_panel_rig.h"

/* --------------------------------------------------------------- series */

TEST_CASE(the_series_is_chosen_before_connect_and_kept_in_the_settings)
{
    open_en();
    CHECK_EQ(hitec_prog_series(model()), HITEC_D);
    CHECK_EQ(strcmp(hitec_panel_label(HITEC_PANEL_SERVO, 1, 0),
                    TR(HP_SERIES)),
             0);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 1, 0),
                    TR(HP_SERIES_D)),
             0);
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 1, 0));
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK_EQ(strcmp(hitec_panel_choice_label(0), TR(HP_SERIES_D)), 0);
    CHECK(strstr(hitec_panel_choice_label(1), TR(HP_SERIES_C)) != NULL);
    CHECK(strstr(hitec_panel_choice_label(1), "HS-5xxx") != NULL);
    CHECK(hitec_panel_choice_label(2) == NULL);
    tap(hitec_panel_choice_rect(1));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_series(model()), HITEC_CLASSIC);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_CLASSIC);
    /* A fresh start reads it back. */
    hitec_panel_init(AREA);
    hitec_panel_open();
    CHECK_EQ(hitec_prog_series(model()), HITEC_CLASSIC);
    /* CANCEL keeps it. */
    rig_at(SUPPLY_ON, true);
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_cancel_rect());
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_series(model()), HITEC_CLASSIC);
}

TEST_CASE(the_series_row_and_more_take_no_tap_while_the_port_is_held)
{
    open_en();
    servo_d485();
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_SERVO, 1, 0), TR(HP_SERIES));
    tap_connect();
    /* The PORT row names the series; FILES takes SERIES' place. */
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_SERVO, 1, 0), TR(KP_FILES));
    CHECK(strstr(hitec_panel_port_line(0), TR(HP_SERIES_D)) != NULL);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 0));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 1, 1));   /* READ ALL */
    /* LEAVE gives SERIES back. */
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    hold(2.1f);
    until(HITEC_PROG_PWM);
    show();
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_SERVO, 1, 0), TR(HP_SERIES));
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 1, 0));
}

/**
 * @brief Checks the value tabs as they stand before a read: each says NOT
 *        READ and shows its first row with "--".
 */
static void tabs_not_read(void)
{
    for (int page = HITEC_PANEL_MOTION; page <= HITEC_PANEL_PROTECT; ++page) {
        bool said_not_read = false;
        const char *v;

        tap(hitec_panel_tab_rect(page));
        for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
            said_not_read |= strcmp(hitec_panel_note(i), TR(HP_NOT_READ)) == 0;
        }
        CHECK(said_not_read);
        v = hitec_panel_value(page, 0, 0);
        CHECK(hitec_panel_label(page, 0, 0) != NULL);
        CHECK(v != NULL && strcmp(v, "--") == 0);
    }
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
}

/* Hitec D receive on MORE, in PWM, beside the series: the list names both
 * modes, the one set lit; a tap sets it and asks a save; the IOmcu's word is
 * owed through the settings as before. */
TEST_CASE(hitec_d_receive_is_set_on_more_in_pwm)
{
    open_en();
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 0);
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK(strstr(hitec_panel_choice_label(2), "PULL-UP") != NULL);
    CHECK(strstr(hitec_panel_choice_label(2), ui_setting_label(SET_HITEC_RX))
          != NULL);
    tap(hitec_panel_choice_rect(2));
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK_STR_EQ(hitec_panel_choice_label(0), ui_setting_help(SET_HITEC_RX));
    CHECK_STR_EQ(hitec_panel_choice_label(1), TR(SET_HITEC_RX_PRE));
    CHECK(hitec_panel_choice_label(2) == NULL);
    CHECK(!settings_save_asked());
    tap(hitec_panel_choice_rect(1));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 1);
    CHECK(settings_save_asked());
    /* MORE names what is set; CANCEL keeps it. */
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    CHECK(strstr(hitec_panel_choice_label(2), "PRECHARGE") != NULL);
    tap(hitec_panel_choice_rect(2));
    tap(hitec_panel_choice_cancel_rect());
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 1);
}

/* A session offers no change: a list left open closes as a session starts, and
 * what a tap there lands on is no mode.  MORE in a session lists no such row
 * (test_hitec_panel_write.c). */
TEST_CASE(a_session_closes_hitec_d_receives_list)
{
    open_en();
    servo_d485();
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    tap(hitec_panel_choice_rect(2));
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    prun(5000u);
    CHECK(hitec_prog_state(model()) != HITEC_PROG_PWM);
    CHECK_EQ(hitec_panel_editor(), 0);
    tap(hitec_panel_choice_rect(1));
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 0);
}

/* What the IOmcu does not hold shows under the modes as a line that sets
 * nothing: each mark its words, EN and DE, none while held. */
TEST_CASE(hitec_d_receive_says_what_the_iomcu_does_not_hold)
{
    static const struct { uint8_t mark; ui_text_id_t why; } k[] = {
        { HITEC_RX_LINK_OLD, TX_SET_HITEC_RX_OLD },
        { HITEC_RX_LINK_NO_LINE, TX_SET_HITEC_RX_NO_LINE },
        { HITEC_RX_LINK_PENDING, TX_SET_UNHELD },
        { HITEC_RX_LINK_REFUSED, TX_SET_UNHELD },
    };
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            open_at(SUPPLY_ON, (ui_lang_t)lang);
            settings_set(SET_HITEC_RX, 1.0f);
            g_rx_mark = k[i].mark;
            prun(20u);
            tap(val(HITEC_PANEL_SERVO, 2, 0));
            tap(hitec_panel_choice_rect(2));
            CHECK_STR_EQ(hitec_panel_choice_label(2), ui_tr(k[i].why));
            tap(hitec_panel_choice_rect(2));        /* a line to read */
            CHECK_EQ(hitec_panel_editor(), 0);
            CHECK_EQ(settings_get_int(SET_HITEC_RX), 1);
            g_rx_mark = (uint8_t)HITEC_RX_LINK_HELD;
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_change_of_series_drops_what_the_other_series_read)
{
    open_en();
    /* D series read, LEAVE, then CLASSIC: nothing of the D485HW is left. */
    servo_d485();
    tap_connect();
    CHECK(hitec_prog_model(model()) != NULL);
    tap(hitec_panel_close_rect());
    hold(2.1f);
    until(HITEC_PROG_PWM);
    show();
    CHECK(hitec_prog_servo(model()) != NULL);    /* kept on its own series */
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    CHECK_EQ(hitec_prog_series(model()), HITEC_CLASSIC);
    CHECK(hitec_prog_servo(model()) == NULL);
    CHECK(hitec_prog_model(model()) == NULL);
    CHECK_EQ(hitec_prog_layout(model()), HITEC_LAYOUT_HS5);
    show();
    CHECK(hitec_panel_label(HITEC_PANEL_SERVO, 1, 1) == NULL
          || strcmp(hitec_panel_label(HITEC_PANEL_SERVO, 1, 1), "D485HW")
                 != 0);
    tabs_not_read();
    /* Classic read, LEAVE, then D SERIES: nothing of the HS-5645MG. */
    servo_hs5645();
    tap_connect();
    CHECK(hitec_prog_model(model()) != NULL);
    CHECK_EQ(hitec_prog_layout(model()), HITEC_LAYOUT_HS5);
    tap(hitec_panel_close_rect());
    hold(2.1f);
    until(HITEC_PROG_PWM);
    show();
    /* The same series again keeps the read. */
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    CHECK(hitec_prog_servo(model()) != NULL);
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(0));
    CHECK_EQ(hitec_prog_series(model()), HITEC_D);
    CHECK(hitec_prog_servo(model()) == NULL);
    CHECK_EQ(hitec_prog_layout(model()), HITEC_LAYOUT_D);
    show();
    tabs_not_read();
    /* A change refused while the port is held keeps the read. */
    servo_d485();
    tap_connect();
    CHECK(!hitec_prog_series_set(model(), HITEC_CLASSIC));
    CHECK_EQ(hitec_prog_series(model()), HITEC_D);
    CHECK(hitec_prog_servo(model()) != NULL);
    CHECK_EQ(hitec_prog_layout(model()), HITEC_LAYOUT_D);
}

/* ------------------------------------------------------- connect and read */

TEST_CASE(connect_reads_the_d485hw_and_the_tabs_show_its_values)
{
    open_en();
    servo_d485();
    tap_connect();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK(strstr(said(), "34485") != NULL);
    CHECK_EQ(strcmp(hitec_panel_label(HITEC_PANEL_SERVO, 1, 1), "D485HW"),
             0);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0),
                    TR(KP_LEAVE)),
             0);
    /* TRAVEL: NEUTRAL in counts with degrees, at the D485HW's 8192. */
    tap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    CHECK_EQ(hitec_panel_page(), HITEC_PANEL_TRAVEL);
    CHECK_EQ(strcmp(hitec_panel_label(HITEC_PANEL_TRAVEL, 0, 0),
                    TR(HP_NEUTRAL)),
             0);
    CHECK(strstr(hitec_panel_value(HITEC_PANEL_TRAVEL, 0, 0), "8192") != NULL);
    CHECK(strstr(hitec_panel_value(HITEC_PANEL_TRAVEL, 0, 0), "+0.0") != NULL);
    /* Every value tab: no row reads "--" after a full read. */
    for (int page = HITEC_PANEL_MOTION; page <= HITEC_PANEL_INFO; ++page) {
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 2; ++col) {
                const char *v = hitec_panel_value(page, row, col);

                if (v != NULL) {
                    CHECK(strcmp(v, "--") != 0);
                }
            }
        }
    }
    /* Every row of TRAVEL is writable and takes a tap: an edit of the
     * draft. */
    for (int row = 0; row < 4; ++row) {
        CHECK(hitec_panel_row_live(HITEC_PANEL_TRAVEL, row, 0));
    }
    CHECK(!hitec_panel_row_live(HITEC_PANEL_TRAVEL, 4, 0));
    /* INFO's model is read only. */
    CHECK(!hitec_panel_row_live(HITEC_PANEL_INFO, 0, 0));
}

TEST_CASE(before_a_read_the_value_tabs_say_so)
{
    bool said_not_read = false;

    open_en();
    tap(hitec_panel_tab_rect(HITEC_PANEL_MOTION));
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        said_not_read |= strcmp(hitec_panel_note(i), TR(HP_NOT_READ)) == 0;
    }
    CHECK(said_not_read);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_MOTION, 0, 0), "--"), 0);
}

TEST_CASE(a_classic_connect_without_a_pd_mini_is_refused_with_its_reason)
{
    open_at(SUPPLY_NONE, UI_LANG_EN);
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK_EQ(msg(), HITEC_PROG_MSG_NEED_PD_C);
    CHECK_EQ(strcmp(said(), TR(HP_NEED_PD_C)), 0);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    prun(100u);
    CHECK_EQ(g_cmds, 0u);
}

/* ------------------------------------------------------------- PORT row */

TEST_CASE(the_port_row_names_the_port_and_its_gpio_in_every_state)
{
    open_en();
    servo_d485();
    /* The tester has it. */
    CHECK(strstr(hitec_panel_port_line(0), "PROG PORT 1 GPIO 13") != NULL);
    CHECK(strstr(hitec_panel_port_line(0), TR(HP_SERIES_D)) != NULL);
    CHECK(strstr(hitec_panel_port_line(1), "5.0V 1000mA") != NULL);
    CHECK_EQ(hitec_panel_pin(), PIN);
    /* Held. */
    tap_connect();
    CHECK(strstr(hitec_panel_port_line(0), "PROG PORT 1 GPIO 13") != NULL);
    CHECK_EQ(hitec_panel_pin(), PIN);
    /* Held by another session after a panel restart. */
    hitec_panel_init(AREA);
    hitec_panel_open();
    panel_restart();
    until(HITEC_PROG_OTHER);
    show();
    CHECK(strstr(hitec_panel_port_line(0), "PROG PORT 1 GPIO 13") != NULL);
    CHECK(strstr(hitec_panel_port_line(0), TR(HP_HELD)) != NULL);
    CHECK_EQ(strcmp(hitec_panel_port_line(1), TR(HP_OTHER_LINE)), 0);
    CHECK_EQ(hitec_panel_pin(), PIN);
    /* No programming port: the tester on the standard outputs. */
    open_en();
    g_target = 0u;
    prun(50u);
    show();
    CHECK_EQ(strcmp(hitec_panel_port_line(0), TR(HP_PORT_NONE)), 0);
    CHECK(hitec_panel_port_line(2) == NULL);
}

/* ---------------------------------------------------- LEAVE and RELEASE */

TEST_CASE(close_while_held_opens_leave_and_a_2s_hold_leaves)
{
    open_en();
    servo_d485();
    tap_connect();
    tap(hitec_panel_close_rect());
    CHECK(hitec_panel_is_open());
    CHECK(!shot_facts_now().no_session);
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_EQ(strcmp(hitec_panel_page_line(0), TR(KP_LEAVE_TITLE)), 0);
    CHECK_EQ(strcmp(hitec_panel_page_line(2), TR(HP_LEAVE_D)), 0);
    /* 1.5s is not enough. */
    hold(1.5f);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(hitec_panel_editor(), 3);
    hold(2.1f);
    CHECK(pstate() == HITEC_PROG_LEAVING || pstate() == HITEC_PROG_PWM);
    until(HITEC_PROG_PWM);
    show();
    CHECK_EQ(strcmp(said(), TR(KP_M_LEFT)), 0);
    CHECK(!hitec_panel_holds_port());
    /* Now CLOSE closes. */
    tap(hitec_panel_close_rect());
    CHECK(!hitec_panel_is_open());
}

TEST_CASE(a_classic_leave_page_says_the_supply_cycles)
{
    open_en();
    servo_hs5645();
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_EQ(strcmp(hitec_panel_page_line(1), TR(HP_LEAVE_C)), 0);
    tap(hitec_panel_cancel_rect());
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

TEST_CASE(a_port_another_session_holds_offers_release_port)
{
    char want[HITEC_PANEL_LINE];

    open_en();
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    until(HITEC_PROG_OTHER);
    show();
    CHECK(hitec_panel_holds_port());
    const bool held = supply_screen_port_held();
    supply_screen_set_port_held(false);
    CHECK(!shot_facts_now().port_free);
    supply_screen_set_port_held(held);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0),
                    TR(HP_RELEASE)),
             0);
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 0, 0));
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_EQ(strcmp(hitec_panel_page_line(0), TR(HP_RELEASE)), 0);
    snprintf(want, sizeof(want), TR(HP_RELEASE_1), 1u);
    CHECK_EQ(strcmp(hitec_panel_page_line(1), want), 0);
    hold(2.1f);
    until(HITEC_PROG_PWM);
    show();
    snprintf(want, sizeof(want), TR(HP_RELEASED), 1u);
    CHECK_EQ(strcmp(said(), want), 0);
    CHECK(!hitec_panel_holds_port());
}

/* ----------------------------------------------------------- the supply */

TEST_CASE(the_supply_is_locked_while_a_port_is_held)
{
    open_en();
    servo_d485();
    CHECK(!supply_screen_port_held());
    tap_connect();
    CHECK(supply_screen_port_held());
    tap(hitec_panel_close_rect());
    hold(2.1f);
    until(HITEC_PROG_PWM);
    prun(50u);
    CHECK(!supply_screen_port_held());
    /* Another session's port locks it too. */
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    until(HITEC_PROG_OTHER);
    CHECK(supply_screen_port_held());
}

/**
 * @brief A D series session on the PD mini: the operator's 5.5V 1.5A on
 *        SUPPLY, CONNECT, and the port's 5.0V 1.0A on the live output, as
 *        panel_render.c hands them over (supply_screen_adopt()).
 */
static void session_on_the_pd_mini(void)
{
    open_en();
    supply_screen_put(5.5f, 1.5f);
    servo_d485();
    tap_connect();
    CHECK(supply_screen_port_held());
    supply_screen_adopt(5.0f, 1.0f);
    supply_screen_set_output(true);
}

/* The SERVO screen's restore question follows a LEAVE only: a LEAVE offers
 * the operator's set points back onto the live output.  RELEASE PORT of a
 * port another session holds, also the one a lost link leaves behind, ends
 * the hold with no LEAVE and offers nothing, even with the output still
 * read on. */
TEST_CASE(only_a_leave_offers_the_operators_set_points_back)
{
    float v = 0.0f;
    float i = 0.0f;

    session_on_the_pd_mini();
    tap(hitec_panel_close_rect());
    hold(2.1f);
    until(HITEC_PROG_PWM);
    prun(50u);
    CHECK(!supply_screen_port_held());
    CHECK(supply_screen_restore_offer(&v, &i));
    CHECK(fabsf(v - 5.5f) < 0.001f && fabsf(i - 1.5f) < 0.001f);

    /* RELEASE PORT. */
    open_en();
    supply_screen_put(5.5f, 1.5f);
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    until(HITEC_PROG_OTHER);
    show();
    CHECK(supply_screen_port_held());
    supply_screen_adopt(5.0f, 1.0f);
    supply_screen_set_output(true);
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    hold(2.1f);
    until(HITEC_PROG_PWM);
    prun(50u);
    CHECK(!supply_screen_port_held());
    CHECK(!supply_screen_restore_offer(NULL, NULL));

    /* The link goes under the session and comes back: the IOmcu still
     * holds the port, for no session of this panel, and RELEASE PORT gives
     * it back. */
    session_on_the_pd_mini();
    link_silent();
    prun(500u);
    link_back();
    until(HITEC_PROG_OTHER);
    show();
    CHECK(!supply_screen_restore_offer(NULL, NULL));
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    hold(2.1f);
    until(HITEC_PROG_PWM);
    prun(50u);
    CHECK(!supply_screen_port_held());
    CHECK(!supply_screen_restore_offer(NULL, NULL));
}

/* A KST session on the PD mini holds the SUPPLY page too, and the Hitec
 * overlay passes the page's word on (LINK_SP_PORT) with no Hitec session.
 * The KST LEAVE's word lets go first; the release at the page's word a
 * read later offers the set points back.  Without the LEAVE it offers
 * nothing. */
TEST_CASE(the_supply_pages_word_after_a_kst_leave_asks)
{
    for (int leave = 0; leave < 2; ++leave) {
        hitec_prog_feed_t f;

        open_en();
        memset(&f, 0, sizeof(f));
        f.supply_held = true;
        hitec_panel_feed(&f);
        supply_screen_put(5.5f, 1.5f);
        supply_screen_set_port_held(true);
        supply_screen_adopt(5.0f, 1.0f);
        supply_screen_set_output(true);
        if (leave != 0) {
            supply_screen_kst_leave_done();
        }
        supply_screen_set_port_held(false);
        CHECK(supply_screen_port_held());
        f.supply_held = false;
        hitec_panel_feed(&f);
        CHECK(!supply_screen_port_held());
        CHECK_EQ(supply_screen_restore_offer(NULL, NULL), leave != 0);
    }
}

/* ----------------------------------------------------------- the layout */

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
           && b.y < a.y + a.h;
}

static void target(gfx_rect_t r)
{
    CHECK(r.w >= UI_TAP_MIN);
    CHECK(r.h >= UI_TAP_MIN);
    CHECK(r.x >= AREA.x);
    CHECK(r.y >= AREA.y);
    CHECK(r.x + r.w <= AREA.x + AREA.w);
    CHECK(r.y + r.h <= FOOT);
}

/* The left edge of the row a value of column @p col sits in. */
static int row_x(int col)
{
    return col == 0 ? AREA.x + 10 : AREA.x + 20 + (AREA.w - 30) / 2;
}

/* Every tab's controls tap targets with no overlap, every label left of
 * its control, every value inside it, the PORT lines left of the PORT
 * button, the notes above the band. */
static void check_layout(void)
{
    for (int page = HITEC_PANEL_SERVO; page <= HITEC_PANEL_INFO; ++page) {
        gfx_rect_t c[24];
        int n = 0;

        tap(hitec_panel_tab_rect(page));
        CHECK_EQ(hitec_panel_page(), page);
        for (int t = 0; t < HITEC_PANEL_PAGES; ++t) {
            c[n++] = hitec_panel_tab_rect(t);
        }
        c[n++] = hitec_panel_close_rect();
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 2; ++col) {
                const gfx_rect_t v = val(page, row, col);
                const char *label = hitec_panel_label(page, row, col);
                const char *value = hitec_panel_value(page, row, col);

                if (v.w == 0) {
                    continue;
                }
                c[n++] = v;
                if (!(page == HITEC_PANEL_SERVO && row == 0)
                    && row_x(col) + gfx_text_cells(label) * 10 + 8 > v.x) {
                    T_FAIL("label %s at %d %d %d", label, page, row, col);
                }
                CHECK(gfx_text_cells(value) * 10 + 8 <= v.w);
            }
        }
        for (int i = 0; i < n; ++i) {
            target(c[i]);
            for (int j = i + 1; j < n; ++j) {
                CHECK(!overlap(c[i], c[j]));
            }
        }
        if (page == HITEC_PANEL_SERVO) {
            for (int i = 0; i < 2; ++i) {
                CHECK(row_x(0) + gfx_text_cells(hitec_panel_port_line(i)) * 10
                          + 8
                      <= val(page, 0, 0).x);
            }
        }
        {
            const gfx_rect_t d = hitec_panel_notes_drawn();

            if (d.h > 0) {
                CHECK(d.y + d.h <= BAND_TOP - 4);
            }
        }
    }
}

TEST_CASE(every_control_label_value_and_note_fits_in_both_languages)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };

    for (size_t k = 0; k < sizeof(langs) / sizeof(langs[0]); ++k) {
        /* The tester has the port, D series. */
        open_at(SUPPLY_ON, langs[k]);
        check_layout();
        /* The D485HW read. */
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        servo_d485();
        tap_connect();
        check_layout();
        /* Stopped, with the latch: the longest state line. */
        press_stop();
        until(HITEC_PROG_HELD);
        prun(50u);
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        check_layout();
        /* The HS-5645MG read. */
        open_at(SUPPLY_ON, langs[k]);
        tap(val(HITEC_PANEL_SERVO, 1, 0));
        tap(hitec_panel_choice_rect(1));
        servo_hs5645();
        tap_connect();
        check_layout();
        /* Another session's port. */
        open_at(SUPPLY_ON, langs[k]);
        (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
        until(HITEC_PROG_OTHER);
        check_layout();
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        tap(val(HITEC_PANEL_SERVO, 0, 0));
        CHECK_EQ(hitec_panel_editor(), 3);
        target(hitec_panel_apply_rect());
        target(hitec_panel_cancel_rect());
        CHECK(!overlap(hitec_panel_apply_rect(), hitec_panel_cancel_rect()));
        show();
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(every_message_fits_its_line_in_both_languages)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };

    open_en();
    for (size_t k = 0; k < sizeof(langs) / sizeof(langs[0]); ++k) {
        ui_text_set_language(langs[k]);
        for (unsigned id = 1; id < HITEC_PROG_MSG_COUNT; ++id) {
            char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
            const hitec_prog_msg_t m = { .id = (uint8_t)id, .arg = 65535u,
                                         .reg = 65535u };
            const int n = hitec_panel_message_lines(&m, t);

            CHECK(n >= 1 && n <= HITEC_PANEL_MSG_LINES);
            for (int i = 0; i < n; ++i) {
                /* Not cut: the line ends before its buffer does, and two
                 * rows of the notes hold it. */
                CHECK(strlen(t[i]) + 1u < sizeof(t[i]));
                CHECK(hitec_panel_text_rows(t[i]) <= 2);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_series_is_chosen_before_connect_and_kept_in_the_settings);
    RUN(the_series_row_and_more_take_no_tap_while_the_port_is_held);
    RUN(hitec_d_receive_is_set_on_more_in_pwm);
    RUN(a_session_closes_hitec_d_receives_list);
    RUN(hitec_d_receive_says_what_the_iomcu_does_not_hold);
    RUN(a_change_of_series_drops_what_the_other_series_read);
    RUN(connect_reads_the_d485hw_and_the_tabs_show_its_values);
    RUN(before_a_read_the_value_tabs_say_so);
    RUN(a_classic_connect_without_a_pd_mini_is_refused_with_its_reason);
    RUN(the_port_row_names_the_port_and_its_gpio_in_every_state);
    RUN(close_while_held_opens_leave_and_a_2s_hold_leaves);
    RUN(a_classic_leave_page_says_the_supply_cycles);
    RUN(a_port_another_session_holds_offers_release_port);
    RUN(the_supply_is_locked_while_a_port_is_held);
    RUN(only_a_leave_offers_the_operators_set_points_back);
    RUN(the_supply_pages_word_after_a_kst_leave_asks);
    RUN(every_control_label_value_and_note_fits_in_both_languages);
    RUN(every_message_fits_its_line_in_both_languages);
    free(fb);
    return test_summary("hitec_panel");
}
