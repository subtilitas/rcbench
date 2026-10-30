/**
 * @file test_kst_panel.c
 * @brief The overlay of the programmer for KST and Chaservo servos
 * (shared/ui/kst_panel.c): what a touch does in each state, what each page
 * asks for, and what the operator is told.  The bench under it is
 * kst_panel_rig.h on kst_rig.h: the simulated servo, the IOmcu's port,
 * link dispatch and the panel's link module.  The servo files, the cases with
 * no card and the limit run are test_kst_panel_files.c,
 * test_kst_panel_nocard.c and test_kst_panel_limrun.c.
 *
 * This suite holds the port, the tabs and the programming voltage.
 * Not what it looks like; that is the golden images' job.  Every state is
 * rendered here all the same, so a draw that reads past a table falls over
 * under the sanitizers.
 *
 * Cases:
 * - without_a_link_nothing_on_the_servo_tab_takes_a_tap
 * - a_coprocessor_without_the_page_leaves_the_programmer_unavailable
 * - programming_is_locked_until_the_port_is_the_testers_target
 * - a_port_without_a_pin_is_refused_by_name
 * - a_port_without_a_servo_says_so_and_gives_the_port_back
 * - the_programming_voltage_is_typed_inside_1_to_6_v
 * - the_carrier_shows_the_ports_5v_as_fixed
 * - a_programming_voltage_above_6_v_needs_hv_servo_and_a_hold
 * - a_stored_programming_voltage_above_6_v_is_loaded_as_6_v
 * - a_confirmed_voltage_above_6_v_is_not_kept
 * - the_fetched_backup_redraws_restore
 * - a_locked_session_found_at_the_start_is_enabled_and_restored
 * - a_read_under_a_locked_session_shows_the_servo_and_keeps_the_lock
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"
#include "ui_board.h"

/* ------------------------------------------------------------ port and tabs */

TEST_CASE(without_a_link_nothing_on_the_servo_tab_takes_a_tap)
{
    fresh();
    rig_frame();
    show();
    CHECK_EQ(kst_prog_state(M), KST_PROG_DOWN);
    for (int row = 0; row < 5; ++row) {
        tap(val(PG_SERVO, row, 0));
        tap(val(PG_SERVO, row, 1));
    }
    rig_frames(10);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.starts, 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    CHECK(!kst_panel_holds_port());
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(a_coprocessor_without_the_page_leaves_the_programmer_unavailable)
{
    fresh();
    CHECK(rig_link_up((uint16_t)(LINK_MINOR_KST - 1u)));
    show();
    CHECK_EQ(kst_prog_state(M), KST_PROG_NO_PAGE);
    tap(val(PG_SERVO, 0, 0));
    rig_frames(50);
    CHECK_EQ(g_rig.starts, 0);
    CHECK(!kst_panel_holds_port());
    /* The other tabs show no values and take no tap. */
    for (int page = PG_TUNE; page <= PG_LOCKED; ++page) {
        tap(kst_panel_tab_rect(page));
        CHECK_EQ(kst_panel_page(), page);
        tap(val(page, 1, 0));
        CHECK_EQ(kst_panel_editor(), 0);
    }
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(programming_is_locked_until_the_port_is_the_testers_target)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.target = 0u;
        link_up();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_STR_EQ(status(0), TR(KP_NEED_TARGET));
        CHECK_STR_EQ(status(1), TR(KP_NEED_TARGET_2));
        /* No row takes a tap: nothing is asked of the port. */
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 2; ++col) {
                tap(val(PG_SERVO, row, col));
                CHECK_EQ(kst_panel_editor(), 0);
            }
        }
        settle();
        CHECK_EQ(g_rig.starts, 0);
        CHECK(!kst_panel_holds_port());
        /* The model says the same to whoever asks it. */
        CHECK_EQ(kst_prog_connect(M, RIG_PORT), KST_PROG_MSG_NEED_TARGET);
        CHECK_STR_EQ(said(), TR(KP_NEED_TARGET));
        /* Another port as the target is not this one. */
        g_rig.target = 2u + RIG_PORT;
        settle();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(g_rig.starts, 1);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_PORT);
        CHECK_STR_EQ(said(), TR(KP_R_PORT));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        /* The port as the target: the rows are live. */
        g_rig.target = 1u + RIG_PORT;
        settle();
        snprintf(want, sizeof(want), TR(KP_STATE_PWM_AS_IS), RIG_PORT + 1u);
        CHECK_STR_EQ(status(0), want);
        /* No PD mini: the servo powers up into the entry by the
         * operator's hand. */
        CHECK_STR_EQ(status(1), TR(KP_NOPD_ENTRY));
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        /* The target moves while the port is held: nothing is locked. */
        g_rig.target = 0u;
        settle();
        snprintf(want, sizeof(want), TR(KP_STATE_PROGRAMMING), RIG_PORT + 1u);
        CHECK_STR_EQ(status(0), want);
        tap(val(PG_SERVO, 1, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_READ);
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_STR_EQ(status(0), TR(KP_NEED_TARGET));
    }
}

TEST_CASE(a_port_without_a_pin_is_refused_by_name)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.no_pin = true;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(g_rig.starts, 1);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_PIN);
        CHECK_STR_EQ(said(), TR(KP_R_NO_PIN));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
        tap(kst_panel_close_rect());
        CHECK(!kst_panel_is_open());
    }
}

TEST_CASE(a_port_without_a_servo_says_so_and_gives_the_port_back)
{
    fresh();
    g_rig.sim.powered = false;
    link_up();
    CHECK_EQ(kst_link_faults(&g_rig.link), 0u);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_NO_SERVO);
    CHECK_STR_EQ(said(), TR(KP_S_NO_SERVO));
    /* A session fault, counted once as the control task takes it: an
     * error of the bench (error_sites_follow()). */
    CHECK_EQ(kst_link_faults(&g_rig.link), 1u);
    CHECK(!kst_prog_have_servo(M));
    if (kst_prog_state(M) == KST_PROG_PROGRAMMING) {
        /* Nothing to edit, nothing to write; LEAVE is the way out. */
        tap(val(PG_SERVO, 3, 0));
        CHECK_EQ(kst_panel_editor(), 0);
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
    }
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(kst_link_faults(&g_rig.link), 1u);
}

/* On a board that switches the ports' own 5V (the rbmod carrier, GPIO
 * 21): the voltage and the current are not offered, MORE names the 5V as
 * fixed, its line sets nothing and releases nothing, and the state and
 * the PORT row say 5V. */
TEST_CASE(the_carrier_shows_the_ports_5v_as_fixed)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        link_up();
        ui_board_set((uint16_t)OUTBIND_BOARD_RBMOD);
        snprintf(want, sizeof(want), TR(KP_STATE_PWM_5V), RIG_PORT + 1u,
                 21u);
        CHECK_STR_EQ(status(0), want);
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(choices(), 2);
        snprintf(want, sizeof(want), TR(KP_SUPPLY_5V_FIXED), 21u);
        CHECK_STR_EQ(kst_panel_choice_label(0), want);
        const uint16_t mv = kst_prog_supply_mv(M);
        const unsigned said_before = msg();
        tap(kst_panel_choice_rect(0));
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(msg(), said_before);      /* no RELEASE asked, no refusal */
        CHECK_EQ(kst_prog_supply_mv(M), mv);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        /* Back on the module: the voltage and the current again. */
        ui_board_set((uint16_t)OUTBIND_BOARD_PICO_HEADER);
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(choices(), 3);
        snprintf(want, sizeof(want), TR(KP_SUPPLY_V), 5u, 0u);
        CHECK_STR_EQ(kst_panel_choice_label(0), want);
        tap(kst_panel_choice_cancel_rect());
        ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_programming_voltage_is_typed_inside_1_to_6_v)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        link_up();
        /* 5.0V and 1000mA until they are set. */
        snprintf(want, sizeof(want), TR(KP_STATE_PWM), RIG_PORT + 1u, 5u, 0u,
                 1000u);
        CHECK_STR_EQ(status(0), want);
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(choices(), 3);
        snprintf(want, sizeof(want), TR(KP_SUPPLY_V), 5u, 0u);
        CHECK_STR_EQ(kst_panel_choice_label(0), want);
        snprintf(want, sizeof(want), TR(KP_SUPPLY_A), 1000u);
        CHECK_STR_EQ(kst_panel_choice_label(1), want);
        tap(kst_panel_choice_rect(0));
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_PROG_VOLTAGE));
        CHECK_NEAR(kst_panel_keypad()->value, 5.0f, 0.001f);
        CHECK_NEAR(kst_panel_keypad()->min,
                   (float)KST_PORT_SUPPLY_MIN_MV / 1000.0f, 0.001f);
        CHECK_NEAR(kst_panel_keypad()->max,
                   (float)KST_PANEL_STD_MV / 1000.0f, 0.001f);
        /* 6.1V and 4.7V cannot be entered. */
        type_text("6.1");
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK(kst_panel_keypad()->refused);
        type_text("4.7");
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK(kst_panel_keypad()->refused);
        type_text("1.0");
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
        /* The ends can, and neither asks for a hold. */
        type_text("6.0");
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_supply_mv(M), 6000u);
        more_input(0);
        type_text("5.5");
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_supply_mv(M), 5500u);
        /* CANCEL keeps what was set. */
        more_input(0);
        key(UI_KEY_4);
        key(UI_KEY_CANCEL);
        CHECK_EQ(kst_prog_supply_mv(M), 5500u);
        more_input(0);
        type_text("4.8");
        snprintf(want, sizeof(want), TR(KP_STATE_PWM), RIG_PORT + 1u, 4u, 8u,
                 1000u);
        CHECK_STR_EQ(status(0), want);
        CHECK_EQ(g_rig.sup_sets, 0);
        /* CONNECT sets it, and the line shows what the supply reads. */
        g_rig.load_ma = 37u;
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(g_rig.sup_on);
        CHECK_EQ(g_rig.sup_mv, 4800u);
        CHECK_EQ(g_rig.sup_ma, 1000u);
        snprintf(want, sizeof(want), TR(KP_STATE_PROG_V), RIG_PORT + 1u, 4u,
                 80u, 37u);
        CHECK_STR_EQ(status(0), want);
        /* In programming mode MORE is the servo's list. */
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(choices(), 3);
        CHECK(choice(TR(KP_VERIFY)) >= 0);
        tap(kst_panel_choice_cancel_rect());
        CHECK_EQ(kst_prog_supply_mv(M), 4800u);
    }
}

TEST_CASE(a_programming_voltage_above_6_v_needs_hv_servo_and_a_hold)
{
    char want[KST_PANEL_LINE];

    fresh();
    g_rig.rail = true;
    kst_panel_set_hv(true);
    link_up();
    more_input(0);
    CHECK_NEAR(kst_panel_keypad()->max,
               (float)KST_PORT_SUPPLY_MAX_MV / 1000.0f, 0.001f);
    type_text("8.5");
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    /* 6.1V is the first that asks. */
    type_text("6.1");
    CHECK_EQ(kst_panel_editor(), 3);
    snprintf(want, sizeof(want), TR(KP_SUPPLY_V), 6u, 1u);
    CHECK_STR_EQ(kst_panel_warning_line(0), want);
    CHECK(warns(TR(SV_HV_1)));
    CHECK(warns(TR(SV_HV_4)));
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    /* 1.9s is not a hold; CANCEL sets nothing. */
    hold(1.9f);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    if (kst_panel_editor() == 3) {
        tap(kst_panel_cancel_rect());
    }
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    /* The port's highest, held. */
    more_input(0);
    type_text("8.4");
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PORT_SUPPLY_MAX_MV);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK_EQ(g_rig.sup_mv, KST_PORT_SUPPLY_MAX_MV);
    /* HV SERVO off while the port is held changes nothing on the servo;
     * the next CONNECT asks for 5.0V again. */
    kst_panel_set_hv(false);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    CHECK_EQ(g_rig.sup_mv, KST_PORT_SUPPLY_MAX_MV);

    /* HV SERVO off closes the keypad and the page that offer more. */
    fresh();
    g_rig.rail = true;
    kst_panel_set_hv(true);
    link_up();
    more_input(0);
    CHECK_EQ(kst_panel_editor(), 1);
    kst_panel_set_hv(false);
    CHECK_EQ(kst_panel_editor(), 0);
    kst_panel_set_hv(true);
    more_input(0);
    type_text("7.4");
    CHECK_EQ(kst_panel_editor(), 3);
    kst_panel_set_hv(false);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    more_input(0);
    CHECK_NEAR(kst_panel_keypad()->max, (float)KST_PANEL_STD_MV / 1000.0f,
               0.001f);
    type_text("7.4");
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CANCEL);
    /* A value inside the standard range stays when HV SERVO goes. */
    more_input(0);
    type_text("5.5");
    kst_panel_set_hv(true);
    kst_panel_set_hv(false);
    CHECK_EQ(kst_prog_supply_mv(M), 5500u);
}

/* No stored voltage above 6.0V reaches the programmer, with HV SERVO off
 * or on, at an opening, a screen entry or a start under a held port. */
TEST_CASE(a_stored_programming_voltage_above_6_v_is_loaded_as_6_v)
{
    static const struct {
        float stored;
        unsigned want_mv;
    } v[] = {
        { 4.8f, 4800u }, { 5.9f, 5900u }, { 6.0f, 6000u },
        { 6.1f, 6000u }, { 7.4f, 6000u }, { 8.4f, 6000u },
    };

    for (int hv = 0; hv < 2; ++hv) {
        for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
            for (int entry = 0; entry < 2; ++entry) {
                fresh();
                g_rig.rail = true;
                settings_set(SET_SERVO_PROG_V, v[i].stored);
                (void)settings_save_asked();
                kst_panel_init(AREA);
                kst_panel_set_hv(hv != 0);
                if (entry != 0) {
                    kst_panel_screen_entry();
                } else {
                    kst_panel_open();
                }
                CHECK_EQ(kst_prog_supply_mv(M), v[i].want_mv);
                /* The settings hold what is used. */
                CHECK_NEAR(settings_get(SET_SERVO_PROG_V),
                           (float)v[i].want_mv / 1000.0f, 0.001f);
                CHECK_EQ(settings_save_asked(), v[i].stored > 6.0f);
                /* CONNECT asks the supply for it. */
                kst_panel_open();
                link_up();
                tap(val(PG_SERVO, 0, 0));
                settle();
                CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
                CHECK_EQ(g_rig.sup_mv, v[i].want_mv);
            }
        }
    }
}

/* A voltage above 6.0V is the hold's and the session's: the settings do
 * not keep it, and the next opening, entry or start has 6.0V. */
TEST_CASE(a_confirmed_voltage_above_6_v_is_not_kept)
{
    fresh();
    g_rig.rail = true;
    kst_panel_set_hv(true);
    link_up();
    more_input(0);
    type_text("8.4");
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PORT_SUPPLY_MAX_MV);
    CHECK_NEAR(settings_get(SET_SERVO_PROG_V), 6.0f, 0.001f);
    /* The current limit set after it stores no more either. */
    more_input(1);
    type_text("800");
    CHECK_EQ(kst_prog_supply_mv(M), KST_PORT_SUPPLY_MAX_MV);
    CHECK_NEAR(settings_get(SET_SERVO_PROG_V), 6.0f, 0.001f);
    /* An opening with no port held reads the settings: 6.0V. */
    kst_panel_close();
    CHECK_EQ(kst_prog_supply_mv(M), KST_PORT_SUPPLY_MAX_MV);
    kst_panel_open();
    CHECK_EQ(kst_prog_supply_mv(M), KST_PANEL_STD_MV);
    more_input(0);
    type_text("7.4");
    hold(2.1f);
    CHECK_EQ(kst_prog_supply_mv(M), 7400u);
    kst_panel_screen_entry();
    CHECK_EQ(kst_prog_supply_mv(M), KST_PANEL_STD_MV);

    /* A held port keeps the session's voltage over an opening; a panel
     * that starts under it has none above 6.0V. */
    more_input(0);
    type_text("7.4");
    hold(2.1f);
    CHECK_EQ(kst_prog_supply_mv(M), 7400u);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK_EQ(g_rig.sup_mv, 7400u);
    kst_panel_close();
    kst_panel_open();
    kst_panel_screen_entry();
    CHECK_EQ(kst_prog_supply_mv(M), 7400u);
    kst_panel_init(AREA);
    kst_panel_set_hv(true);
    g_rig.outcomes = 0;
    g_rig.answer_id = 0;
    rig_frames(20);
    kst_panel_open();
    CHECK(kst_panel_holds_port());
    CHECK_EQ(kst_prog_supply_mv(M), KST_PANEL_STD_MV);
}

/* A panel that starts under a locked session with no record: RESTORE waits
 * for the backup the model fetches, and its arrival alone redraws the row. */
TEST_CASE(the_fetched_backup_redraws_restore)
{
    static gfx_color_t before[W * H];
    const gfx_rect_t r = val(PG_SERVO, 4, 0);
    uint32_t rev = 0;
    bool waited = false;
    bool differs = false;

    fresh();
    cut_write();
    come_back();
    tap(val(PG_SERVO, 1, 0));           /* READ ALL: the session locks */
    settle();
    CHECK((M->f.rd.flags & LINK_KS_F_LOCKED) != 0u);
    kst_link_init(&g_rig.link);
    kst_panel_init(AREA);
    kst_panel_open();
    g_rig.outcomes = 0;
    g_rig.answer_id = 0;
    g_rig.safe = true;
    kst_link_came_up(&g_rig.link, LINK_PROTOCOL_MINOR);
    for (unsigned i = 0; i < 60000u; ++i) {
        rig_frame();
        if (kst_prog_state(M) != KST_PROG_PROGRAMMING) {
            continue;
        }
        if (!M->f.have_backup) {
            waited = true;
            CHECK(!M->journal);
            CHECK(!kst_prog_restore_offered(M, KST_PROG_FROM_SESSION));
            show();
            memcpy(before, fb, sizeof(before));
            rev = kst_panel_revision();
        } else {
            break;
        }
    }
    CHECK(waited);
    CHECK(M->f.have_backup);
    CHECK(kst_prog_restore_offered(M, KST_PROG_FROM_SESSION));
    /* No input since: the revision moved, and the row is drawn anew. */
    CHECK(kst_panel_revision() != rev);
    show();
    for (int y = r.y; y < r.y + r.h && !differs; ++y) {
        differs = memcmp(&before[(size_t)y * W + (size_t)r.x],
                         &fb[(size_t)y * W + (size_t)r.x],
                         (size_t)r.w * sizeof(gfx_color_t)) != 0;
    }
    CHECK(differs);
    tap(r);
    CHECK_EQ(kst_panel_editor(), 2);
}

/* A panel that starts under a locked session, its backup fetched and no
 * servo read. */
static void restart_locked(void)
{
    fresh();
    cut_write();
    come_back();
    tap(val(PG_SERVO, 1, 0));           /* READ ALL: the session locks */
    settle();
    CHECK((M->f.rd.flags & LINK_KS_F_LOCKED) != 0u);
    kst_link_init(&g_rig.link);
    kst_panel_init(AREA);
    kst_panel_open();
    g_rig.outcomes = 0;
    g_rig.answer_id = 0;
    g_rig.safe = true;
    kst_link_came_up(&g_rig.link, LINK_PROTOCOL_MINOR);
    for (unsigned i = 0; i < 60000u && !M->f.have_backup; ++i) {
        rig_frame();
    }
    rig_frames(600);                    /* the fetch ends */
    tick(0.02f);
    show();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(M->f.have_backup);
    CHECK(!kst_prog_have_servo(M));
    CHECK(kst_prog_restore_only(M));
}

/* The session's restore from there: the switch, then the restore. */
static void restore_locked(void)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now = rig_servo();

    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) != 0);
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    hold(1.9f);
    rig_frames(50);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) != 0);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) == 0);
    CHECK((M->f.rd.flags & LINK_KS_F_LOCKED) == 0u);
}

/* The switch of a locked session takes its tap with no servo read: the
 * restore of the held backup is the way out, and the one write it opens. */
TEST_CASE(a_locked_session_found_at_the_start_is_enabled_and_restored)
{
    unsigned writes;

    restart_locked();
    writes = g_rig.sim.n_writes;
    /* RESTORE with the switch off says what it needs. */
    tap(val(PG_SERVO, 4, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    /* The switch takes the tap and its hold. */
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(1.9f);
    CHECK(!kst_prog_write_enabled(M));
    hold(2.1f);
    CHECK(kst_prog_write_enabled(M));
    /* It opens no other write: nothing is edited, written, released or
     * loaded with no servo read. */
    CHECK(!kst_prog_editable(M));
    CHECK_EQ(kst_prog_edit(M, KST_F_DEAD_BAND, 3), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_write_check(M), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_release_check(M), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_load(M, "KST001"), KST_PROG_MSG_NEED_READ);
    tap(val(PG_SERVO, 4, 1));
    CHECK_EQ(kst_panel_editor(), 0);
    tap(val(PG_SERVO, 2, 1));           /* MORE needs a servo read */
    CHECK_EQ(kst_panel_editor(), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, writes);
    restore_locked();
}

/* READ ALL in that state reads the servo and leaves the lock: the values
 * are shown, none is edited, and the restore is still the one write. */
TEST_CASE(a_read_under_a_locked_session_shows_the_servo_and_keeps_the_lock)
{
    unsigned writes;

    restart_locked();
    writes = g_rig.sim.n_writes;
    tap(val(PG_SERVO, 1, 0));
    settle();
    CHECK(kst_prog_have_servo(M));
    CHECK(kst_prog_in_doubt(M));
    CHECK((M->f.rd.flags & LINK_KS_F_LOCKED) != 0u);
    CHECK(!kst_prog_editable(M));
    CHECK(kst_prog_restore_only(M));
    enable();
    CHECK_EQ(kst_prog_edit(M, KST_F_DEAD_BAND, 3), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_write_check(M), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_release_check(M), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(g_rig.sim.n_writes, writes);
    restore_locked();
}

int main(void)
{
    RUN(without_a_link_nothing_on_the_servo_tab_takes_a_tap);
    RUN(a_coprocessor_without_the_page_leaves_the_programmer_unavailable);
    RUN(programming_is_locked_until_the_port_is_the_testers_target);
    RUN(a_port_without_a_pin_is_refused_by_name);
    RUN(a_port_without_a_servo_says_so_and_gives_the_port_back);
    RUN(the_programming_voltage_is_typed_inside_1_to_6_v);
    RUN(the_carrier_shows_the_ports_5v_as_fixed);
    RUN(a_programming_voltage_above_6_v_needs_hv_servo_and_a_hold);
    RUN(a_stored_programming_voltage_above_6_v_is_loaded_as_6_v);
    RUN(a_confirmed_voltage_above_6_v_is_not_kept);
    RUN(the_fetched_backup_redraws_restore);
    RUN(a_locked_session_found_at_the_start_is_enabled_and_restored);
    RUN(a_read_under_a_locked_session_shows_the_servo_and_keeps_the_lock);
    return test_summary("kst_panel");
}
