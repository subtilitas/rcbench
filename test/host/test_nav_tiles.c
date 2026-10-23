/**
 * @file test_nav_tiles.c
 * @brief The shell's navigation: the SETUP tiles, WiFi and its opener, the home tag
 * climbing one level from every screen, the home grid with its texts in both
 * languages, and every screen's title.
 *
 * Cases:
 * - the_setup_tiles_open_the_outputs_the_picker_wifi_and_updates
 * - the_band_titles_match_the_tiles
 * - wifi_returns_to_the_screen_that_opened_it
 * - wifi_keeps_its_opener_over_stop_link_loss_and_home
 * - the_home_tag_climbs_one_level_from_every_screen
 * - the_home_tag_climbs_the_stick_session_one_level_at_a_time
 * - the_home_grid_is_four_by_two_by_kind
 * - every_home_tile_text_fits_in_both_languages
 * - the_home_tiles_carry_the_planned_names_and_lines
 * - the_setup_hub_names_its_first_category_panel_in_both_languages
 * - the_home_tag_on_servo_closes_a_panel_climbs_and_leaves
 * - the_home_tag_climbs_from_the_programmer_to_its_list_to_the_menu
 * - the_home_tag_climbs_from_a_setup_list_to_the_tiles_to_the_menu
 * - the_home_tag_closes_what_a_tool_screen_opened_first
 * - the_home_tag_leaves_supply_under_a_drag
 * - a_tile_navigates_and_a_slip_does_not
 * - every_screen_renders_something_and_has_a_title
 * - a_cancelled_tile_press_navigates_nowhere
 * - a_cancel_reaches_a_screen_left_during_the_frame
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"
#include "motor_spots.h"
#include "wifi_spots.h"

static void tap_tile(int index)
{
    int x = 0, y = 0;
    hub_point(index, &x, &y);
    tap(x, y);
}

static void tap_home(void)
{
    const gfx_rect_t home =
        ui_home_tag_rect(ui_router_title(ui_router_current()));
    tap(home.x + home.w / 2, home.y + home.h / 2);
}

/*
 * SETUP's tiles.  They are the only way to either outputs view and to
 * UPDATES, so a tile that stopped navigating would leave a screen nobody
 * can reach.  The table's order, row by row: the two categories,
 * PANEL and INTERFACES, and OUTPUTS, then PICK A PIN, WI-FI and
 * UPDATES; the third row stays free.  ESC / BENCH is no tile: its rows are
 * MOTOR & ESC > SETTINGS'.
 */
TEST_CASE(the_setup_tiles_open_the_outputs_the_picker_wifi_and_updates)
{
    static const struct {
        ui_screen_id_t id;
        const char    *title;
    } k_tools[] = {
        { SCREEN_OUTPUTS, "OUTPUTS" }, { SCREEN_PICKER, "PICK A PIN" },
        { SCREEN_WIFI, "WI-FI" },      { SCREEN_UPDATES, "UPDATES" },
    };
    const int n = (int)(sizeof(k_tools) / sizeof(k_tools[0]));
    fresh();
    CHECK_EQ(settings_hub_tiles(), SET_CAT_SETUP_COUNT + n);
    CHECK_EQ(SET_CAT_SETUP_COUNT, 4);   /* PANEL, INTERFACES, SD CARD,
                                           SENSORS */
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        ui_router_goto(SCREEN_SETUP);
        tap_tile(cat);
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), cat);
        ui_router_goto(SCREEN_OVERVIEW);
    }
    for (int i = 0; i < n; ++i) {
        ui_router_goto(SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
        tap_tile(SET_CAT_SETUP_COUNT + i);
        CHECK_EQ(ui_router_current(), k_tools[i].id);
        CHECK_STR_EQ(ui_router_title(k_tools[i].id), k_tools[i].title);
        CHECK(settings_screen_opens(k_tools[i].id));
        /* Its home tag leads back to the tiles, not to the menu. */
        tap_home();
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
    }

    /* A tile's edge rows are the tile's; the margin between two is not. */
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    CHECK(settings_hub_slot(SET_CAT_SETUP_COUNT, &r));
    ui_router_goto(SCREEN_SETUP);
    tap(r.x + 1, UI_BAND_H + r.y + r.h - 1);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    ui_router_goto(SCREEN_SETUP);
    tap(r.x + r.w + 4, UI_BAND_H + r.y + r.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    /* An empty slot opens nothing, and neither does the last. */
    for (int slot = settings_hub_tiles(); slot < SETTINGS_HUB_SLOTS; ++slot) {
        int x = 0, y = 0;
        hub_point(slot, &x, &y);
        tap(x, y);
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
    }

    /* And a press that slides off a tile is not a tap on it. */
    int x = 0, y = 0;
    hub_point(4, &x, &y);
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch(x + 300, y, TOUCH_EVENT_MOVE, 1);
    touch(x + 300, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* The reminder's own screen is not a tile's: its tag leaves to the
     * menu, as every screen's that no tile opens. */
    CHECK(!settings_screen_opens(SCREEN_UPD_REMIND));
    CHECK(!settings_screen_opens(SCREEN_MOTOR));
    CHECK(!settings_screen_opens(SCREEN_SETUP));
    CHECK(!settings_screen_opens(SCREEN_LOGS));
}

/* The band names each screen by its tile: SETUP, LOGS, WI-FI. */
TEST_CASE(the_band_titles_match_the_tiles)
{
    fresh();
    CHECK_STR_EQ(ui_router_title(SCREEN_SETUP), "SETUP");
    CHECK_STR_EQ(ui_router_title(SCREEN_LOGS), "LOGS");
    CHECK_STR_EQ(ui_router_title(SCREEN_WIFI), "WI-FI");
    /* No screen is PROGRAMMER any more: programming is a task of MOTOR &
     * ESC and of SERVO. */
    for (int id = 0; id < SCREEN_COUNT; ++id) {
        CHECK(strcmp(ui_router_title((ui_screen_id_t)id), "PROGRAMMER")
              != 0);
    }
}

/* LOGS' WI-FI button, mirrored from log_viewer_int.h (BR_WIFI): x 144
 * to 263, 42 px tall, its foot 4 px above the alert band. */
#define LOGS_WIFI_X 204

#define LOGS_WIFI_Y (UI_BAND_H + UI_BODY_H - UI_ALERT_H - 4 - 21)

static void wifi_from_logs(void)
{
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    tap(LOGS_WIFI_X, LOGS_WIFI_Y);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
}

static void wifi_from_setup(void)
{
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    tap_tile(SET_CAT_SETUP_COUNT + 2);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
}

/*
 * WI-FI has two openers, LOGS' WI-FI button and SETUP's tile, and its home
 * tag returns to the one that opened it: one level, never the menu.  So
 * does PANEL UPDATE's way back after an upload (ui_router_parent()).
 */
TEST_CASE(wifi_returns_to_the_screen_that_opened_it)
{
    fresh();
    to_overview();
    /* Not yet opened: SETUP, the tile's screen. */
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_SETUP);

    wifi_from_logs();
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_LOGS);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    wifi_from_setup();
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_SETUP);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* Alternating, each opener wins in turn. */
    for (int k = 0; k < 4; ++k) {
        if ((k & 1) == 0) {
            wifi_from_logs();
        } else {
            wifi_from_setup();
        }
        tap_home();
        CHECK_EQ(ui_router_current(),
                 (k & 1) == 0 ? SCREEN_LOGS : SCREEN_SETUP);
    }

    /* A screen WI-FI leads to and back from keeps the opener: PANEL
     * UPDATE, the panel's own update, after an upload. */
    wifi_from_logs();
    ui_router_goto(SCREEN_UPDATE);
    ui_router_goto(ui_router_parent(SCREEN_WIFI));
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    wifi_from_setup();
    ui_router_goto(SCREEN_UPDATE);
    ui_router_goto(SCREEN_WIFI);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* A new start forgets the opener. */
    wifi_from_logs();
    fresh();
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_SETUP);
}

/* The status band's STOP, mirrored from ui_band.c. */
static void tap_stop(void)
{
    const gfx_rect_t stop = ui_band_stop_rect();
    tap(stop.x + stop.w / 2, stop.y + stop.h / 2);
}

/*
 * The opener holds through what can happen on WI-FI in between: a STOP, a
 * link that goes and comes back, and a trip home and back by the other
 * opener.  STOP never navigates.
 */
TEST_CASE(wifi_keeps_its_opener_over_stop_link_loss_and_home)
{
    for (int from = 0; from < 2; ++from) {
        const ui_screen_id_t opener = from ? SCREEN_SETUP : SCREEN_LOGS;
        const ui_screen_id_t other  = from ? SCREEN_LOGS : SCREEN_SETUP;
        fresh();
        to_overview();

        /* STOP on WI-FI. */
        from ? wifi_from_setup() : wifi_from_logs();
        tap_stop();
        CHECK(ui_router_take_stop());
        CHECK_EQ(ui_router_current(), SCREEN_WIFI);
        tap_home();
        CHECK_EQ(ui_router_current(), opener);

        /* The link lost and back while WI-FI shows. */
        from ? wifi_from_setup() : wifi_from_logs();
        ui_bench_status_t st;
        memset(&st, 0, sizeof(st));
        st.link_up = true;
        ui_router_set_status(&st);
        st.link_up = false;
        st.faults  = 0x01u;
        ui_router_set_status(&st);
        CHECK_EQ(ui_router_current(), SCREEN_WIFI);
        tap_home();
        CHECK_EQ(ui_router_current(), opener);
        st.link_up = true;
        st.faults  = 0u;
        ui_router_set_status(&st);

        /* The link-lost screen the application opens elsewhere, its
         * acknowledgement to the menu, and WI-FI again. */
        from ? wifi_from_setup() : wifi_from_logs();
        tap_home();
        ui_router_goto(SCREEN_BUSFAULT);
        ui_router_goto(SCREEN_OVERVIEW);
        from ? wifi_from_setup() : wifi_from_logs();
        tap_home();
        CHECK_EQ(ui_router_current(), opener);

        /* Home in between, and the other opener: the newer one wins, and
         * the first again after it. */
        from ? wifi_from_setup() : wifi_from_logs();
        tap_home();
        tap_home();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        from ? wifi_from_logs() : wifi_from_setup();
        tap_home();
        CHECK_EQ(ui_router_current(), other);
        from ? wifi_from_setup() : wifi_from_logs();
        tap_home();
        CHECK_EQ(ui_router_current(), opener);
    }
}

/*
 * The home tag climbs one level from the first level of every screen that
 * has one: a screen a SETUP tile opens to SETUP, WI-FI to its opener, the
 * rest to the menu.  None skips to the menu past a level.
 */
TEST_CASE(the_home_tag_climbs_one_level_from_every_screen)
{
    for (int id = SCREEN_MOTOR; id < SCREEN_COUNT; ++id) {
        if (id == SCREEN_BUSFAULT || id == SCREEN_UPDATE
            || id == SCREEN_COPROC_UPDATE) {
            continue;   /* no home tag, or one only the update's question has */
        }
        fresh();
        to_overview();
        if (id == SCREEN_WIFI) {
            wifi_from_logs();
        } else {
            ui_router_goto(settings_screen_opens((ui_screen_id_t)id)
                               ? SCREEN_SETUP : SCREEN_OVERVIEW);
            ui_router_goto((ui_screen_id_t)id);
        }
        const ui_screen_id_t want = (id == SCREEN_WIFI) ? SCREEN_LOGS
            : settings_screen_opens((ui_screen_id_t)id) ? SCREEN_SETUP
            : SCREEN_OVERVIEW;
        CHECK_EQ(ui_router_parent((ui_screen_id_t)id), want);
        tap_home();
        if (ui_router_current() != want) {
            T_FAIL("screen %d: the home tag led to %d, not %d", id,
                   ui_router_current(), want);
        }
    }
}

/* MOTOR & ESC's PROGRAM tab and its list's first row, and ESC STICK's
 * list rows, in panel y (motor_workbench.c, motor_program_page.c,
 * esc_stick_panel.c). */
#define MO_TAB_PROGRAM_X MO_TAB_X(1)

#define MO_TAB_PY        (UI_BAND_H + MO_TAB_Y)


#define MO_PROG_ROW_PY   (UI_BAND_H + MO_PROG_ROW_Y)

#define SP_ROW_Y(n)      (UI_BAND_H + 70 + (n) * 36 + 16)

/* The makers' row of @p name, or -1. */
static int stick_maker_row(const char *name)
{
    for (int i = 0; i < 9; ++i) {
        const char *m = esc_stick_panel_maker_at(i);
        if (m != NULL && strcmp(m, name) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * MOTOR & ESC > PROGRAM through the router: the tab shows the list, its
 * STICK MENU row opens ESC STICK full width, and a maker and a model go
 * one level each.  The home tag climbs them one at a time as BACK does:
 * the profile's page to the models, the models to the makers, the makers
 * out of the session to the list, and only then to the menu, switching
 * the bench to SAFE.
 */
TEST_CASE(the_home_tag_climbs_the_stick_session_one_level_at_a_time)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    tap(MO_TAB_PROGRAM_X, MO_TAB_PY);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!motor_screen_session_shown());
    tap(MO_PROG_ROW_X, MO_PROG_ROW_PY);
    CHECK(motor_screen_session_shown());
    CHECK_EQ(esc_stick_panel_level(), 0);
    const int dualsky = stick_maker_row("Dualsky");
    if (dualsky < 0) {
        T_FAIL("Dualsky is not on the makers' first page");
        return;
    }
    tap(400, SP_ROW_Y(dualsky));
    CHECK_EQ(esc_stick_panel_level(), 1);
    tap(400, SP_ROW_Y(0));
    if (esc_stick_panel_page() == NULL) {
        T_FAIL("Dualsky's first model opened no page");
        return;
    }

    const uint32_t nav = ui_router_navigations();
    motor_cmd_t mc;
    while (motor_screen_poll_cmd(&mc)) {
    }
    tap_home();                                /* the page: the models */
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK(esc_stick_panel_page() == NULL);
    CHECK_EQ(esc_stick_panel_level(), 1);
    tap_home();                                /* the makers */
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK(motor_screen_session_shown());
    tap_home();                                /* out to the list */
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK(!motor_screen_session_shown());
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK(!motor_screen_poll_cmd(&mc));        /* nothing asked yet */
    tap_home();                                /* the menu, SAFE */
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(motor_screen_poll_cmd(&mc));
    CHECK_EQ(mc.kind, MOTOR_CMD_DISARM);

    /* The tile opens LIVE again, and PROGRAM the list, the session
     * closed. */
    ui_router_goto(SCREEN_MOTOR);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
    tap(MO_TAB_PROGRAM_X, MO_TAB_PY);
    CHECK(!motor_screen_session_shown());
    CHECK_EQ(esc_stick_panel_stage(), 0);
}

/*
 * SERVO through the router's home tag: over a list it closes the list and
 * SERVO stays on SETTINGS; from a run's settings it climbs to TEST; from a
 * task it goes to the menu, and the tile opens LIVE again.
 */
TEST_CASE(the_home_tag_on_servo_closes_a_panel_climbs_and_leaves)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_SERVO);
    tap(SV_TAB_SETTINGS_X, UI_BAND_H + SV_TAB_Y);
    tap(100, UI_BAND_H + SV_ROW_Y(0));                 /* TYPE's list */
    const uint32_t nav = ui_router_navigations();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(ui_router_navigations(), nav);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    tap(SV_TAB_TEST_X, UI_BAND_H + SV_TAB_Y);
    tap(SV_RUN_SET_X, UI_BAND_H + SV_RUN_SET_Y(0));
    CHECK_EQ(servo_screen_run_settings(), 1);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_run_settings(), 0);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * SERVO > PROGRAM through the router: the tab shows the list, its row the
 * programmer one level down; the home tag climbs from the programmer to
 * the list, from the list to the menu, one level a tap.  With SERVO PWM
 * not reported the row is dimmed and the tag climbs from the list.
 */
TEST_CASE(the_home_tag_climbs_from_the_programmer_to_its_list_to_the_menu)
{
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = LINK_CAP_SERVO_PWM;
    ui_router_set_status(&st);
    ui_router_goto(SCREEN_SERVO);
    tap(SV_TAB_PROGRAM_X, UI_BAND_H + SV_TAB_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!kst_panel_is_open());
    tap(SV_PROG_ROW_X, UI_BAND_H + SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    const uint32_t nav = ui_router_navigations();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK(!kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    CHECK_EQ(ui_router_navigations(), nav);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* The tile opens LIVE again; with no SERVO PWM the row opens
     * nothing. */
    st.capabilities = 0u;
    ui_router_set_status(&st);
    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    tap(SV_TAB_PROGRAM_X, UI_BAND_H + SV_TAB_Y);
    tap(SV_PROG_ROW_X, UI_BAND_H + SV_PROG_ROW_Y(0));
    CHECK(!kst_panel_is_open());
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * A category's tile opens its list on the same screen, and the home tag
 * goes back up: from the list to the tiles, from the tiles to the menu.
 */
TEST_CASE(the_home_tag_climbs_from_a_setup_list_to_the_tiles_to_the_menu)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_SETUP);
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        const uint32_t n0 = ui_router_navigations();
        tap_tile(cat);
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), cat);
        CHECK_EQ(ui_router_navigations(), n0);
        tap_home();
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
        CHECK_EQ(ui_router_navigations(), n0);
    }
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* SETUP opens on its tiles again, whichever list was open when it
     * was left. */
    ui_router_goto(SCREEN_SETUP);
    tap_tile(SET_CAT_IFACE);
    CHECK_EQ(settings_screen_category(), SET_CAT_IFACE);
    ui_router_goto(SCREEN_MOTOR);
    ui_router_goto(SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
}

/*
 * What the four tool screens open over themselves, in panel coordinates
 * (supply_screen.c, wifi_screen.c, outputs_screen.c, picker_screen.c).
 */
static void open_supply_settings(void) { tap(181, UI_BAND_H + 18); }

static void open_supply_keypad(void)   { tap(676, UI_BAND_H + 65); }

/* SETTINGS, then VOLTAGE MAX's keypad over its page. */
static void open_supply_settings_keypad(void)
{
    open_supply_settings();
    tap(144, UI_BAND_H + 94);
}

static void open_wifi_list(void)
{
    tap(WF_BTN_CX(2), UI_BAND_H + WF_BTN_CY(2));   /* CHOOSE */
}

static void open_wifi_keyboard(void)
{
    open_wifi_list();
    tap(WF_HIDDEN_CX, UI_BAND_H + WF_SCAN_CY);   /* HIDDEN: the name */
}

static void open_outputs_list(void)    { tap(149, UI_BAND_H + 71); }

/* OUTPUTS on its PROG PORT entry, as SERVO's TARGET row opens it, and the
 * list opened over that. */
static void open_outputs_list_on_port(void)
{
    outputs_screen_select_port(0u);
    CHECK_EQ(outputs_screen_port_shown(), 0);
    open_outputs_list();
}

static void open_picker_list(void)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
    picker_screen_set_binding(&b);
    const gfx_rect_t r = picker_screen_list_face();
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
    CHECK(picker_screen_list_open());
}

/*
 * A drag on SUPPLY's voltage track and the home tag under a second finger:
 * the router releases the drag first, and that release opens no question,
 * so the tag leaves SUPPLY for the menu, with the question on and off and
 * the output live and not.
 */
TEST_CASE(the_home_tag_leaves_supply_under_a_drag)
{
    for (int k = 0; k < 4; ++k) {
        const bool ask  = (k & 1) != 0;
        const bool live = (k & 2) != 0;
        fresh();
        settings_init();
        to_overview();
        settings_set(SET_SUPPLY_CONFIRM_SLIDE, ask ? 1.0f : 0.0f);
        ui_router_goto(SCREEN_SUPPLY);
        supply_screen_set_output(live);
        touch(82, UI_BAND_H + 335, TOUCH_EVENT_DOWN, 1);
        touch(282, UI_BAND_H + 335, TOUCH_EVENT_MOVE, 1);
        const gfx_rect_t home = ui_home_tag_rect("SUPPLY");
        touch(home.x + home.w / 2, home.y + home.h / 2, TOUCH_EVENT_DOWN, 2);
        touch(home.x + home.w / 2, home.y + home.h / 2, TOUCH_EVENT_UP, 2);
        if (ui_router_current() != SCREEN_OVERVIEW) {
            T_FAIL("ask %d live %d: the home tag led to %d", ask, live,
                   ui_router_current());
        }
        touch(282, UI_BAND_H + 335, TOUCH_EVENT_UP, 1);
        settings_cancel_save();
    }
}

/*
 * SUPPLY, WI-FI, OUTPUTS and PICK A PIN through the router's home tag:
 * over what the screen opened the tag closes it, one level a tap, and the
 * screen stays; with nothing open it goes to the screen's parent, for
 * OUTPUTS the workbench whose TARGET row opened it.  SUPPLY's SETTINGS is
 * a task of its strip, no level: the tag leaves from it.
 */
TEST_CASE(the_home_tag_closes_what_a_tool_screen_opened_first)
{
    static const struct {
        ui_screen_id_t id;
        ui_screen_id_t from;
        void         (*open)(void);
        int            levels;
    } k_cases[] = {
        { SCREEN_SUPPLY,  SCREEN_OVERVIEW, open_supply_settings, 0 },
        { SCREEN_SUPPLY,  SCREEN_OVERVIEW, open_supply_keypad,   1 },
        { SCREEN_SUPPLY,  SCREEN_OVERVIEW, open_supply_settings_keypad, 1 },
        { SCREEN_WIFI,    SCREEN_SETUP,    open_wifi_list,       1 },
        { SCREEN_WIFI,    SCREEN_SETUP,    open_wifi_keyboard,   2 },
        { SCREEN_OUTPUTS, SCREEN_SETUP,    open_outputs_list,    1 },
        { SCREEN_OUTPUTS, SCREEN_MOTOR,    open_outputs_list,    1 },
        { SCREEN_OUTPUTS, SCREEN_SERVO,    open_outputs_list,    1 },
        { SCREEN_OUTPUTS, SCREEN_SERVO,    open_outputs_list_on_port, 1 },
        { SCREEN_PICKER,  SCREEN_SETUP,    open_picker_list,     1 },
    };
    for (size_t k = 0; k < sizeof(k_cases) / sizeof(k_cases[0]); ++k) {
        fresh();
        to_overview();
        if (k_cases[k].id == SCREEN_WIFI) {
            wifi_from_setup();
        } else {
            ui_router_goto(k_cases[k].from);
            ui_router_goto(k_cases[k].id);
        }
        k_cases[k].open();
        const uint32_t nav = ui_router_navigations();
        for (int l = 0; l < k_cases[k].levels; ++l) {
            tap_home();
            if (ui_router_current() != k_cases[k].id) {
                T_FAIL("case %d: level %d left the screen", (int)k, l);
            }
            CHECK_EQ(ui_router_navigations(), nav);
        }
        tap_home();
        if (ui_router_current() != k_cases[k].from) {
            T_FAIL("case %d: the home tag led to %d, not %d", (int)k,
                   ui_router_current(), k_cases[k].from);
        }
    }
}

/* Tiles navigate, and a press that slid off its tile is not a tap on it. */
/*
 * A tile navigates on its release, so a press left latched after a lost
 * event is a navigation waiting for any release over that tile.  The GT911
 * reuses track ids, so that release need not belong to the same contact.
 */
TEST_CASE(a_cancelled_tile_press_navigates_nowhere)
{
    to_overview();
    ui_router_render(&cv, 0);
    gfx_color_t *idle = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(idle);
    memcpy(idle, fb, (size_t)W * H * sizeof(gfx_color_t));

    touch(110, UI_BAND_H + 90, TOUCH_EVENT_DOWN, 3);
    ui_router_cancel_gestures();

    /* No contact is left to release the tile, so it is not drawn pressed. */
    ui_router_render(&cv, 0);
    CHECK_EQ(memcmp(idle, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(idle);

    touch(110, UI_BAND_H + 90, TOUCH_EVENT_UP, 3);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* And nothing is stuck: a fresh tap still navigates. */
    tap(110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/*
 * The loss is observed after the frame's events were dispatched, and one of
 * them can have navigated.  The screen that took the earlier events is then
 * off the top with its press still latched, and a cancel that reached only
 * the screen on top would leave it there for the next visit, where a
 * recycled id lifting over the tab would switch the pane.
 */
TEST_CASE(a_cancel_reaches_a_screen_left_during_the_frame)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_ANALYSER);
    ui_router_render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));

    touch(200, UI_BAND_H + 20, TOUCH_EVENT_DOWN, 2);   /* the second tab */
    ui_router_goto(SCREEN_OVERVIEW);                    /* a HOME that survived */
    ui_router_cancel_gestures();

    ui_router_goto(SCREEN_ANALYSER);
    touch(200, UI_BAND_H + 20, TOUCH_EVENT_UP, 2);      /* a recycled id */
    ui_router_render(&cv, 0);
    CHECK_EQ(memcmp(before, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And the row is not stuck. */
    tap(200, UI_BAND_H + 20);
    ui_router_render(&cv, 0);
    CHECK(memcmp(before, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    free(before);
}

/*
 * The menu is 4 x 2, each tile in a fixed place by kind: the workbenches
 * MOTOR & ESC, SERVO and SUPPLY in row 1, the instruments RECEIVER, BATTERY
 * and BALANCE in row 2, LOGS over SETUP in the last column.  No tile is
 * PROGRAMMER.  Each tile is 243 x 255 px with 10 px margins, and a tap on
 * it opens its screen.
 */
TEST_CASE(the_home_grid_is_four_by_two_by_kind)
{
    static const ui_screen_id_t k_want[] = {
        SCREEN_MOTOR, SCREEN_SERVO, SCREEN_SUPPLY, SCREEN_LOGS,
        SCREEN_ANALYSER, SCREEN_BATTERY, SCREEN_BALANCE, SCREEN_SETUP,
    };
    fresh();
    CHECK_EQ(overview_screen_columns(), 4);
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(overview_screen_tile(i), k_want[i]);
        CHECK(strcmp(overview_screen_tile_name(i), "PROGRAMMER") != 0);
        const gfx_rect_t r = overview_screen_tile_rect(i);
        CHECK_EQ(r.x, 10 + (i % 4) * 253);
        CHECK_EQ(r.y, 10 + (i / 4) * 265);
        CHECK_EQ(r.w, 243);
        CHECK_EQ(r.h, 255);
    }
    /* Past the grid, and before it: no tile. */
    CHECK_EQ(overview_screen_tile(8), SCREEN_COUNT);
    CHECK_EQ(overview_screen_tile(-1), SCREEN_COUNT);
    CHECK(overview_screen_tile_name(8) == NULL);
    CHECK(overview_screen_tile_line(8) == NULL);
    for (int i = 0; i < 8; ++i) {
        to_overview();
        const gfx_rect_t r = overview_screen_tile_rect(i);
        tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
        CHECK_EQ(ui_router_current(), k_want[i]);
    }
    /* The margin between two tiles is no tile's. */
    to_overview();
    tap(10 + 243 + 5, UI_BAND_H + 100);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * Every tile's name and line fit its 243 px less UI_TILE_PAD on either
 * side in both languages: the name on one row of the head face (20 x 36),
 * the line in the label face (10 x 20) on one row of 22 cells or, broken
 * at a space, on two (ui_tile()).
 */
TEST_CASE(every_home_tile_text_fits_in_both_languages)
{
    fresh();
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int i = 0; i < 8; ++i) {
            const char *name = overview_screen_tile_name(i);
            const char *line = overview_screen_tile_line(i);
            if (name == NULL || line == NULL) {
                T_FAIL("slot %d is empty", i);
                continue;
            }
            const int tw = overview_screen_tile_rect(i).w - 2 * UI_TILE_PAD;
            CHECK(gfx_text_width(&gfx_font_20x36, name, 1) <= tw);
            const int row = tw / 10;
            CHECK_EQ(row, 22);
            const int cells = gfx_text_cells(line);
            if (cells <= row) {
                continue;
            }
            /* The last space in the first row's cells breaks it. */
            size_t cut = 0;
            for (size_t k = gfx_text_prefix(line, row); k > 0u; --k) {
                if (line[k] == ' ') {
                    cut = k;
                    break;
                }
            }
            if (cut == 0u || gfx_text_cells(line + cut + 1) > row) {
                T_FAIL("lang %d slot %d: \"%s\" takes more than two rows",
                       lang, i, line);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The tiles' names and their lines, in slot order: the name is the title of
 * the screen the tile opens, English in both languages, and the line is the
 * menu plan's, German with the protocol and unit names as the field writes
 * them (S.BUS, PPM, V/A, iR, mAh).  No line carries the name ANALYSER.
 */
TEST_CASE(the_home_tiles_carry_the_planned_names_and_lines)
{
    static const char *const k_name[8] = {
        "MOTOR & ESC", "SERVO", "SUPPLY", "LOGS",
        "RECEIVER", "BATTERY", "BALANCE", "SETUP",
    };
    static const char *const k_line[UI_LANG_COUNT][8] = {
        [UI_LANG_EN] = {
            "drive, V/A, rpm", "travel, current, program", "set and log V/A",
            "runs and files", "S.BUS, PPM, frames", "cells, iR, mAh",
            "vibration, phase", "pins, sensors, panel",
        },
        [UI_LANG_DE] = {
            "Antrieb, V/A, Drehzahl", "Weg, Strom, Programm",
            "V/A einstellen, aufzeichnen", "Läufe und Dateien",
            "S.BUS, PPM, Frames", "Zellen, iR, mAh", "Vibration, Phase",
            "Pins, Sensoren, Panel",
        },
    };
    fresh();
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int i = 0; i < 8; ++i) {
            CHECK_STR_EQ(overview_screen_tile_name(i), k_name[i]);
            CHECK_STR_EQ(overview_screen_tile_line(i), k_line[lang][i]);
            /* The band of the screen the tile opens reads the tile's name. */
            CHECK_STR_EQ(ui_router_title(overview_screen_tile(i)), k_name[i]);
            CHECK(strstr(overview_screen_tile_line(i), "ANALYSER") == NULL);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The SETUP hub's first tile is PANEL in both languages, and the category's
 * name is the one the list draws. */
TEST_CASE(the_setup_hub_names_its_first_category_panel_in_both_languages)
{
    fresh();
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(ui_setting_category(SET_CAT_APP), "PANEL");
        CHECK_STR_EQ(ui_setting_category(SET_CAT_IFACE),
                     lang == UI_LANG_DE ? "ANSCHLÜSSE" : "INTERFACES");
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_tile_navigates_and_a_slip_does_not)
{
    fresh();
    to_overview();

    /* First tile: top-left of the body, which is UI_BAND_H down the panel. */
    tap(110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    ui_router_goto(SCREEN_OVERVIEW);
    touch(110, UI_BAND_H + 90, TOUCH_EVENT_DOWN, 1);
    touch(310, UI_BAND_H + 90, TOUCH_EVENT_MOVE, 1);
    touch(310, UI_BAND_H + 90, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* A screen that renders nothing is a screen nobody notices is empty. */
TEST_CASE(every_screen_renders_something_and_has_a_title)
{
    fresh();
    to_overview();
    for (int id = SCREEN_MOTOR; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_router_render(&cv, 0);

        int lit = 0;
        for (int y = UI_BAND_H; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG)) {
                    ++lit;
                }
            }
        }
        if (lit < 2000) {
            T_FAIL("screen %d drew only %d pixels", id, lit);
        }
        CHECK(ui_router_title((ui_screen_id_t)id)[0] != '\0');
    }
}

/**
 * @brief Pixels other than @p bg in rows @p y0 to @p y1 - 1 of tile @p r,
 *        clear of its rounded corners, and the leftmost and rightmost
 *        column they lie in.
 * @param r     The tile, in canvas coordinates.
 * @param y0    The first row.
 * @param y1    The row after the last.
 * @param bg    The tile's fill.
 * @param left  The leftmost inked column into, W when none.
 * @param right The rightmost inked column into, -1 when none.
 * @return The number of inked pixels.
 */
static int tile_ink(gfx_rect_t r, int y0, int y1, gfx_color_t bg, int *left,
                    int *right)
{
    int n = 0;
    *left = W;
    *right = -1;
    for (int y = y0; y < y1; ++y) {
        for (int x = r.x + UI_R_CARD; x < r.x + r.w - UI_R_CARD; ++x) {
            if (fb[y * W + x] != bg) {
                ++n;
                *left = (x < *left) ? x : *left;
                *right = (x > *right) ? x : *right;
            }
        }
    }
    return n;
}

/*
 * ui_tile()'s line, in the menu's look: one wider than the 22 cells inside
 * the tile's padding breaks at its last space that fits onto a second row
 * 24 px lower (German SUPPLY, 27 cells); a line of 24 cells with spaces
 * breaks the same way and stays 8 px clear of the tile's sides; a line
 * with no space stays on one row, clipped; a look with no second row
 * (line_pitch 0, SETUP's) keeps a breakable line on one row.
 */
TEST_CASE(a_tile_line_breaks_at_a_space_inside_its_padding)
{
    fresh();
    ui_tile_look_t look = {
        .icon_dy = 30, .icon = 40, .name_dy = 84, .line_dy = 128,
        .line_pitch = 24, .name_f = &gfx_font_20x36,
        .line_f = &gfx_font_10x20,
    };
    const gfx_rect_t r = gfx_rect_make(10, 10, 243, 255);
    const int row1 = r.y + look.line_dy;
    const int row2 = row1 + look.line_pitch;
    static const struct { const char *line; bool second; } k_case[] = {
        { "V/A einstellen, aufzeichnen", true },
        { "travel, current, program", true },
        { "ABCDEFGHIJKLMNOPQRSTUVWXYZABCD", false },
        { "runs and files", false },
    };
    for (size_t i = 0; i < sizeof(k_case) / sizeof(k_case[0]); ++i) {
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_tile(&cv, r, &look, NULL, "SUPPLY", k_case[i].line, false);
        const gfx_color_t bg = fb[(r.y + 4) * W + r.x + r.w / 2];
        int l1, r1, l2, r2;
        CHECK(tile_ink(r, row1, row1 + 20, bg, &l1, &r1) > 0);
        const int n2 = tile_ink(r, row2, row2 + 20, bg, &l2, &r2);
        if ((n2 > 0) != k_case[i].second) {
            T_FAIL("\"%s\": second row %s", k_case[i].line,
                   n2 > 0 ? "inked" : "empty");
        }
        /* Nothing nearer the sides than the padding. */
        CHECK(l1 >= r.x + UI_TILE_PAD && r1 < r.x + r.w - UI_TILE_PAD);
        if (n2 > 0) {
            CHECK(l2 >= r.x + UI_TILE_PAD && r2 < r.x + r.w - UI_TILE_PAD);
        }
    }
    /* No second row in a look that has none. */
    look.line_pitch = 0;
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_tile(&cv, r, &look, NULL, "SUPPLY", "V/A einstellen, aufzeichnen",
            false);
    const gfx_color_t bg = fb[(r.y + 4) * W + r.x + r.w / 2];
    int l, rr;
    CHECK(tile_ink(r, row1, row1 + 20, bg, &l, &rr) > 0);
    CHECK_EQ(tile_ink(r, row2, row2 + 20, bg, &l, &rr), 0);
}

/*
 * The menu's lower row ends 10 px above the panel's foot, under the alert
 * band; its SIMULATED marks, names and lines end above the band, so an
 * alert hides none of them.  With no IOmcu RECEIVER, BATTERY and BALANCE
 * carry the mark (SETUP never does): ink between the line and the band,
 * and on no lower tile from the band down.
 */
TEST_CASE(every_menu_mark_and_label_ends_above_the_alert_band)
{
    fresh();
    to_overview();
    ui_router_render(&cv, 0);
    for (int i = 4; i < 8; ++i) {
        gfx_rect_t r = overview_screen_tile_rect(i);
        r.y = (int16_t)(r.y + UI_BAND_H);
        const gfx_color_t bg = fb[(r.y + 4) * W + r.x + r.w / 2];
        int l, rr;
        CHECK(r.y + r.h > UI_ALERT_Y);
        if (tile_ink(r, UI_ALERT_Y, r.y + r.h - 2, bg, &l, &rr) != 0) {
            T_FAIL("tile %d inked under the alert band", i);
        }
        /* The mark, in the 40 px above the band. */
        if (i < 7) {
            CHECK(tile_ink(r, UI_ALERT_Y - 40, UI_ALERT_Y, bg, &l, &rr) > 0);
        }
    }
}

int main(void)
{
    RUN(the_setup_tiles_open_the_outputs_the_picker_wifi_and_updates);
    RUN(the_band_titles_match_the_tiles);
    RUN(wifi_returns_to_the_screen_that_opened_it);
    RUN(wifi_keeps_its_opener_over_stop_link_loss_and_home);
    RUN(the_home_tag_climbs_one_level_from_every_screen);
    RUN(the_home_tag_climbs_the_stick_session_one_level_at_a_time);
    RUN(the_home_grid_is_four_by_two_by_kind);
    RUN(every_home_tile_text_fits_in_both_languages);
    RUN(the_home_tiles_carry_the_planned_names_and_lines);
    RUN(a_tile_line_breaks_at_a_space_inside_its_padding);
    RUN(every_menu_mark_and_label_ends_above_the_alert_band);
    RUN(the_setup_hub_names_its_first_category_panel_in_both_languages);
    RUN(the_home_tag_on_servo_closes_a_panel_climbs_and_leaves);
    RUN(the_home_tag_climbs_from_the_programmer_to_its_list_to_the_menu);
    RUN(the_home_tag_climbs_from_a_setup_list_to_the_tiles_to_the_menu);
    RUN(the_home_tag_closes_what_a_tool_screen_opened_first);
    RUN(the_home_tag_leaves_supply_under_a_drag);
    RUN(a_tile_navigates_and_a_slip_does_not);
    RUN(every_screen_renders_something_and_has_a_title);
    RUN(a_cancelled_tile_press_navigates_nowhere);
    RUN(a_cancel_reaches_a_screen_left_during_the_frame);
    return test_summary("nav_tiles");
}
