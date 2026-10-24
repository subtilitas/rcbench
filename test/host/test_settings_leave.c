/*
 * The stored copy of the values, DISCARD and the warning on leaving SETUP.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "outputs_screen.h"
#include "outputs_spots.h"
#include "settings_sensors.h"

/* -------------------------------------------- the stored copy and leaving */

/*
 * The warning's buttons take the strip's three columns: STAY under the
 * first, DISCARD under the second, SAVE where SAVE is.  Panel coordinates.
 */
#define STAY_CX    (8 + 128)
#define DISCARD_CX (RESET_X + 128)
#define LSAVE_CX   (SAVE_X + 128)
#define STRIP_CY   (STRIP_Y + STRIP_H / 2)

/* The middle of STOP, in panel coordinates. */
static void tap_stop(void)
{
    const gfx_rect_t r = ui_band_stop_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

static void set_armed(bool armed)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = armed;
    ui_router_set_status(&st);
}

/* Line @p i of the warning, or "" past the last. */
static const char *warn_line(int i)
{
    static char line[128];
    if (!settings_screen_warn_line(i, line, sizeof(line))) {
        line[0] = '\0';
    }
    return line;
}

/* How many lines the warning draws. */
static int warn_lines(void)
{
    char line[128];
    int n = 0;
    while (settings_screen_warn_line(n, line, sizeof(line))) {
        ++n;
    }
    return n;
}

/* SETUP's tiles with the warning up: the home tag pressed on them. */
static void warn_up(void)
{
    to_hub();
    tap(home_x(), home_y());
}

/* A value set back to the one stored is no change, a string as well: the
 * model has nothing to write, and the button reads SAVED. */
TEST_CASE(an_edit_back_to_the_stored_value_is_no_change)
{
    fresh_model();
    CHECK_EQ(settings_changes(), 0);
    settings_set(SET_PACK_CELLS, 4.0f);
    CHECK(settings_dirty());
    CHECK(settings_differs(SET_PACK_CELLS));
    CHECK_EQ(settings_changes(), 1);
    CHECK_EQ(settings_stored(SET_PACK_CELLS), 6.0f);
    settings_request_save();
    CHECK(settings_save_asked());
    settings_set(SET_PACK_CELLS, 6.0f);
    CHECK(!settings_dirty());
    CHECK(!settings_differs(SET_PACK_CELLS));
    CHECK_EQ(settings_changes(), 0);
    CHECK(!settings_save_asked());       /* nothing left to write */

    settings_set_text(SET_TEXT_DUT_NAME, "X8");
    CHECK(settings_dirty());
    CHECK(settings_text_differs(SET_TEXT_DUT_NAME));
    CHECK_STR_EQ(settings_stored_text(SET_TEXT_DUT_NAME), "SERVO");
    settings_set_text(SET_TEXT_DUT_NAME, "SERVO");
    CHECK(!settings_dirty());
    CHECK_EQ(settings_changes(), 0);

    /* Out of range is no setting and no change. */
    CHECK(!settings_differs((setting_id_t)-1));
    CHECK(!settings_differs(SETTING_COUNT));
    CHECK(!settings_text_differs((setting_text_id_t)-1));
    CHECK(!settings_text_differs(SETTING_TEXT_COUNT));
    CHECK_EQ(settings_stored(SETTING_COUNT), 0.0f);
    CHECK_STR_EQ(settings_stored_text(SETTING_TEXT_COUNT), "");
}

/*
 * The copy is what the medium holds: taken at load and after a successful
 * save, and left by a refused one.  After a refusal the medium may hold
 * part of the refused write, so an edit back to the copy does not clear
 * dirty; a save that succeeds does.
 */
TEST_CASE(the_stored_copy_follows_a_successful_save_only)
{
    fresh_model();
    s_has_saved = false;
    settings_set_store(&s_mem_store);
    settings_set(SET_PACK_CELLS, 4.0f);
    CHECK(settings_save());
    CHECK_EQ(settings_stored(SET_PACK_CELLS), 4.0f);
    CHECK_EQ(settings_changes(), 0);

    settings_set_store(&s_refusing_store);
    settings_set(SET_PACK_CELLS, 5.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());
    CHECK_EQ(settings_stored(SET_PACK_CELLS), 4.0f);
    settings_set(SET_PACK_CELLS, 4.0f);
    CHECK_EQ(settings_changes(), 0);
    CHECK(settings_dirty());             /* the medium is not known */
    CHECK(!settings_save_failed());      /* an edit retires the failure */

    settings_set_store(&s_mem_store);
    CHECK(settings_save());
    CHECK(!settings_dirty());
    settings_set(SET_PACK_CELLS, 5.0f);
    settings_set(SET_PACK_CELLS, 4.0f);
    CHECK(!settings_dirty());

    /* A load is the copy too. */
    settings_init();
    CHECK_EQ(settings_stored(SET_PACK_CELLS), 4.0f);
    CHECK(!settings_dirty());
    settings_set_store(NULL);
}

/*
 * DISCARD's model: every changed value back through settings_set(), so
 * the observer hears each one as it hears an edit by hand -- the PD mini's
 * pins and the sensor bus's included, Sensor SCL following its SDA -- and
 * the string through settings_set_text().
 */
TEST_CASE(discard_puts_back_every_value_through_the_observer)
{
    fresh_model();
    settings_set(SET_PDMINI_TX, 4.0f);
    settings_set(SET_PDMINI_RX, 5.0f);
    settings_set(SET_SENSE_SDA, 4.0f);
    settings_set(SET_PACK_MAH, 5200.0f);
    settings_set_text(SET_TEXT_DUT_NAME, "X8");
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 5);
    CHECK_EQ(settings_changes(), 6);     /* SCL with its SDA */

    memset(s_seen_ids, 0, sizeof(s_seen_ids));
    settings_set_observer(every_id_observer);
    CHECK_EQ(settings_discard(), 0);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 5000);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");
    CHECK_EQ(s_seen_ids[SET_PDMINI_TX], 1);
    CHECK_EQ(s_seen_ids[SET_PDMINI_RX], 1);
    CHECK_EQ(s_seen_ids[SET_SENSE_SDA], 1);
    CHECK_EQ(s_seen_ids[SET_SENSE_SCL], 1);
    CHECK_EQ(s_seen_ids[SET_PACK_MAH], 1);
    int others = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        others += s_seen_ids[i];
    }
    CHECK_EQ(others, 5);                 /* nothing that did not move */
    CHECK(!settings_dirty());
    CHECK_EQ(settings_discard(), 0);     /* nothing left: nothing heard */
    CHECK_EQ(s_seen_ids[SET_PDMINI_TX], 1);
    settings_set_observer(NULL);
}

/*
 * Every setting stepped one key away and one back is no change.  A float
 * comes back by arithmetic, off its stored word by a rounding (Voltage max:
 * 21.0 is stored, 20.999998 comes back), and Sensor SDA's SCL follows it
 * after the set: neither leaves SAVE offered or a line in the warning.  One
 * step away is a change.
 */
TEST_CASE(a_key_away_and_back_is_no_change_for_every_setting)
{
    settings_set_hold(NULL);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (settings_derived(id)) {
            continue;
        }
        const setting_def_t *d = settings_def(id);
        fresh_model();
        const int first = (settings_get(id) > d->min) ? -1 : 1;
        settings_adjust(id, first);
        if (settings_changes() == 0) {
            continue;                    /* one value only: no step */
        }
        CHECK(settings_differs(id));
        CHECK(settings_dirty());
        settings_adjust(id, -first);
        CHECK(!settings_differs(id));
        CHECK_EQ(settings_changes(), 0);
        CHECK(!settings_dirty());
    }
    /* The case that showed it, by name. */
    fresh_model();
    settings_adjust(SET_SUPPLY_V_MAX, -1);
    settings_adjust(SET_SUPPLY_V_MAX, 1);
    CHECK(!settings_dirty());
    fresh_model();
    settings_set(SET_SENSE_SDA, 4.0f);
    settings_set(SET_SENSE_SDA, 16.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK(!settings_dirty());
}

/*
 * A save asked for falls with the last change: the values are back, and a
 * later edit is not one the operator asked to keep.  The write waits for
 * a new SAVE.
 */
TEST_CASE(a_save_asked_falls_when_every_value_is_back)
{
    fresh_model();
    settings_set_store(&s_mem_store);
    settings_set(SET_PACK_CELLS, 4.0f);
    settings_request_save();
    CHECK(settings_save_asked());
    settings_set(SET_PACK_CELLS, 6.0f);
    CHECK(!settings_save_asked());
    settings_set(SET_PACK_MAH, 5200.0f);
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());       /* not the old request again */
    CHECK(!settings_save_tick(true));
    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_dirty());

    /* A request with a change left stands. */
    settings_set(SET_PACK_CELLS, 4.0f);
    settings_request_save();
    settings_set(SET_PACK_CELLS, 6.0f);
    CHECK(settings_save_asked());        /* Capacity still differs */
    settings_set_store(NULL);
}

/*
 * After a refused save the medium can hold part of that write.  DISCARD
 * puts the stored values back and asks for a write of them, so a restart
 * loads what was discarded to; without a refusal it asks nothing.
 */
TEST_CASE(discard_after_a_refused_save_asks_for_a_write)
{
    fresh_model();
    settings_set_store(&s_mem_store);
    settings_set(SET_PDMINI_TX, 4.0f);
    CHECK_EQ(settings_discard(), 0);
    CHECK(!settings_save_asked());
    CHECK(!settings_dirty());

    settings_set(SET_PDMINI_TX, 7.0f);
    CHECK(settings_save());              /* the medium holds 7 */
    settings_set(SET_PDMINI_TX, 4.0f);
    settings_set_store(&s_refusing_store);
    CHECK(!settings_save());
    s_saved[SET_PDMINI_TX] = 4.0f;       /* the part that reached it */
    CHECK_EQ(settings_discard(), 0);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 7);
    CHECK(settings_dirty());             /* the medium is not known */
    CHECK(settings_save_asked());

    settings_set_store(&s_mem_store);
    const int calls = s_save_calls;
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, calls + 1);
    CHECK(!settings_dirty());
    settings_set(SET_PDMINI_TX, 4.0f);
    settings_init();                     /* the restart */
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 7);
    settings_set_store(NULL);
}

/* A held value is left by DISCARD as by an edit, and stays to save. */
TEST_CASE(discard_leaves_a_held_value)
{
    fresh_model();
    settings_set_hold(port_hold);
    s_hold_on = false;
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_PACK_CELLS, 4.0f);
    s_hold_on = true;
    CHECK_EQ(settings_discard(), 1);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    CHECK(settings_dirty());
    s_hold_on = false;
    settings_set_hold(NULL);
}

/* Nothing changed: the home tag leaves at once. */
TEST_CASE(leaving_setup_with_nothing_changed_warns_nothing)
{
    fresh_hub();
    warn_up();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!settings_screen_warning());
    CHECK_EQ(warn_lines(), 0);
}

/*
 * One change: the home tag on the tiles is held, and the warning names the
 * setting with its stored value and the one in force, as the list shows
 * them, with the unit.  The way out is the menu, through the band's home
 * tag: the one way out of SETUP an operator has.
 */
TEST_CASE(leaving_with_one_change_warns_and_names_it)
{
    fresh_hub();
    settings_set(SET_PACK_MAH, 5200.0f);
    warn_up();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    CHECK_EQ(warn_lines(), 1);
    CHECK_STR_EQ(warn_line(0), "Capacity: 5000mAh -> 5200mAh");
    CHECK_STR_EQ(warn_line(1), "");

    /* An enum and a switch by their words, translated. */
    settings_set(SET_PACK_MAH, 5000.0f);
    settings_set(SET_PDMINI_EN, 1.0f);
    CHECK_STR_EQ(warn_line(0), "PD mini: OFF -> ON");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(warn_line(0), "PD mini: AUS -> EIN");
    ui_text_set_language(UI_LANG_EN);

    /* From a list the home tag climbs to the tiles and warns nothing. */
    tap(STAY_CX, STRIP_CY);
    open_category(LIST_CAT);
    tap(home_x(), home_y());
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
    CHECK(!settings_screen_warning());
    tap(home_x(), home_y());
    CHECK(settings_screen_warning());
    ui_router_render(&s_c, 0);
}

/* A pin's -1 is named as the row's help says it, "open"; a pin number takes
 * a space before GPIO, a word unit; GP0 is a pin, not open; and a value
 * whose 0 is OFF carries no unit there.  In German too. */
TEST_CASE(the_warning_names_a_pin_open_and_spaces_gpio)
{
    fresh_hub();
    settings_set(SET_PDMINI_TX, 4.0f);
    warn_up();
    CHECK(settings_screen_warning());
    CHECK_STR_EQ(warn_line(0), "PD mini TX: open -> 4 GPIO");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(warn_line(0), "PD mini TX: offen -> 4 GPIO");
    ui_text_set_language(UI_LANG_EN);

    /* GP0, the lowest pin, is a number like any other. */
    settings_set(SET_PDMINI_TX, 0.0f);
    CHECK_STR_EQ(warn_line(0), "PD mini TX: open -> 0 GPIO");

    /* Back to -1 from a stored pin: the saved side shows the number. */
    settings_set(SET_PDMINI_TX, 4.0f);
    settings_set_store(&s_mem_store);
    CHECK(settings_save());
    settings_set(SET_PDMINI_TX, -1.0f);
    CHECK_STR_EQ(warn_line(0), "PD mini TX: 4 GPIO -> open");
    settings_set(SET_PDMINI_TX, 4.0f);
    CHECK_STR_EQ(warn_line(0), "");

    /* A shunt's 0 is OFF and takes no unit; a value takes mΩ joined. */
    const setting_id_t sh = SET_INA_SH(0);
    const float was = settings_get(sh);
    settings_set(sh, 0.0f);
    char want[64];
    snprintf(want, sizeof(want), "%s: %.1fm\xCE\xA9 -> OFF",
             ui_setting_label(sh), (double)was);
    CHECK_STR_EQ(warn_line(0), want);
    settings_set(sh, was);

    /* The INA228's shunt, in uΩ, joins its number as well. */
    const float uohm = settings_get(SET_INA228_UOHM);
    settings_set(SET_INA228_UOHM, uohm + 50.0f);
    snprintf(want, sizeof(want), "%s: %.0fu\xCE\xA9 -> %.0fu\xCE\xA9",
             ui_setting_label(SET_INA228_UOHM), (double)uohm,
             (double)(uohm + 50.0f));
    CHECK_STR_EQ(warn_line(0), want);
    ui_router_render(&s_c, 0);
    settings_set_store(NULL);
}

/* A string has no row, and the warning names it all the same. */
TEST_CASE(a_changed_string_is_named_in_the_warning)
{
    fresh_hub();
    settings_set_text(SET_TEXT_DUT_NAME, "X8");
    warn_up();
    CHECK(settings_screen_warning());
    CHECK_STR_EQ(warn_line(0), "DUT name: SERVO -> X8");
    CHECK_EQ(warn_lines(), 1);
}

/* SETTINGS_WARN_LINES changes fit; one more leaves the last line to the
 * count of the rest. */
TEST_CASE(more_changes_than_fit_end_on_their_count)
{
    setting_id_t ids[SETTING_COUNT];
    const int n = settings_in_category(SET_CAT_ESC, ids, SETTING_COUNT);
    setting_id_t app[SETTING_COUNT];
    const int m = settings_in_category(SET_CAT_APP, app, SETTING_COUNT);
    CHECK(n + m > SETTINGS_WARN_LINES + 1);

    for (int extra = 0; extra <= 2; ++extra) {
        fresh_hub();
        int set = 0;
        for (int i = 0; i < n + m && set < SETTINGS_WARN_LINES + extra; ++i) {
            const setting_id_t id = (i < n) ? ids[i] : app[i - n];
            if (id == SET_LANGUAGE || id == SET_THEME) {
                continue;        /* the words and the colours stay */
            }
            settings_adjust(id, 1);
            set += settings_differs(id) ? 1 : 0;
        }
        CHECK_EQ(settings_changes(), SETTINGS_WARN_LINES + extra);
        warn_up();
        CHECK(settings_screen_warning());
        CHECK_EQ(warn_lines(), SETTINGS_WARN_LINES);
        if (extra == 0) {
            CHECK(strstr(warn_line(SETTINGS_WARN_LINES - 1), " -> ") != NULL);
        } else {
            char more[32];
            snprintf(more, sizeof(more), "and %d more", extra + 1);
            CHECK_STR_EQ(warn_line(SETTINGS_WARN_LINES - 1), more);
            CHECK(strstr(warn_line(SETTINGS_WARN_LINES - 2), " -> ")
                  != NULL);
        }
        CHECK_STR_EQ(warn_line(SETTINGS_WARN_LINES), "");
        ui_router_render(&s_c, 0);
    }
}

/* A value set back to the stored one holds nothing. */
TEST_CASE(a_value_edited_back_holds_nothing)
{
    fresh_screen();
    tap(XP, row_y(0));
    CHECK(settings_dirty());
    tap(XM, row_y(0));
    CHECK(!settings_dirty());
    warn_up();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!settings_screen_warning());
}

/*
 * A save asked for (WHEN IDLE) is the operator saying keep: no warning.  A
 * save the store refused (NOT SAVED) leaves the values unsaved: a warning,
 * with a line that says so.
 */
TEST_CASE(a_save_asked_holds_nothing_and_a_failed_one_warns)
{
    fresh_hub();
    settings_set(SET_PACK_CELLS, 4.0f);
    tap(LSAVE_CX, STRIP_CY);             /* SAVE on the tiles */
    CHECK(settings_save_asked());
    warn_up();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(settings_save_asked());

    fresh_hub();
    settings_set_store(&s_refusing_store);
    settings_set(SET_PACK_CELLS, 4.0f);
    settings_request_save();
    CHECK(!settings_save_tick(true));
    CHECK(settings_save_failed());
    warn_up();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    CHECK_STR_EQ(warn_line(0), "Cells: 6S -> 4S");
    ui_router_render(&s_c, 0);
    settings_set_store(NULL);
}

/* SAVE on the warning asks for the save, as the SAVE button does, and
 * leaves for the menu.  Nothing is written until the application says. */
TEST_CASE(save_on_the_warning_asks_and_leaves)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    settings_set(SET_PACK_CELLS, 4.0f);
    warn_up();
    CHECK(settings_screen_warning());
    tap(LSAVE_CX, STRIP_CY);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!settings_screen_warning());
    CHECK(settings_save_asked());
    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    CHECK_EQ(settings_stored(SET_PACK_CELLS), 4.0f);
    settings_set_store(NULL);
}

/*
 * DISCARD puts the PD mini's pins back by the observer, the path the
 * panel sends a pin to the IOmcu by, and the language back into the
 * interface, as edits by hand would; then it leaves for the menu.
 */
TEST_CASE(discard_on_the_warning_reverts_by_the_edit_path_and_leaves)
{
    fresh_hub();
    settings_set(SET_PDMINI_TX, 4.0f);
    settings_set(SET_LANGUAGE, 1.0f);
    settings_apply_ui();
    CHECK_EQ(ui_text_language(), UI_LANG_DE);
    memset(s_seen_ids, 0, sizeof(s_seen_ids));
    settings_set_observer(every_id_observer);
    warn_up();
    CHECK(settings_screen_warning());
    tap(DISCARD_CX, STRIP_CY);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    CHECK_EQ(s_seen_ids[SET_PDMINI_TX], 1);
    CHECK_EQ(s_seen_ids[SET_LANGUAGE], 1);
    CHECK_EQ(ui_text_language(), UI_LANG_EN);
    CHECK(!settings_dirty());
    settings_set_observer(NULL);
}

/*
 * STAY, and the home tag over the warning, close it: the tiles again, and
 * the list it was opened from keeps its category and its scroll.
 */
TEST_CASE(stay_keeps_the_list_and_its_scroll)
{
    for (int how = 0; how < 2; ++how) {
        fresh_hub();
        open_category(SET_CAT_IFACE);
        finger(FEED_LONE, XL, LIST_Y + 300);
        glide(FEED_LONE, XL, LIST_Y + 200, 10);
        lift(FEED_LONE);
        const int scrolled = settings_screen_scroll(NULL);
        CHECK(scrolled > 0);
        settings_set(SET_PACK_CELLS, 4.0f);
        warn_up();
        CHECK(settings_screen_warning());
        if (how == 0) {
            tap(STAY_CX, STRIP_CY);
        } else {
            tap(home_x(), home_y());
        }
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK(!settings_screen_warning());
        CHECK_EQ(settings_screen_category(), -1);
        CHECK_EQ(settings_screen_scroll(NULL), scrolled);
        CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
        CHECK(settings_dirty());
        open_category(SET_CAT_IFACE);
        CHECK_EQ(settings_screen_scroll(NULL), scrolled);
    }
}

/* A press on the warning where no button is presses nothing under it. */
TEST_CASE(the_warning_covers_the_tiles)
{
    fresh_hub();
    settings_set(SET_PACK_CELLS, 4.0f);
    warn_up();
    tap(tile_x(0), tile_y(0));
    CHECK(settings_screen_warning());
    CHECK_EQ(settings_screen_category(), -1);
    tap(tile_x(SET_CAT_SETUP_COUNT), tile_y(SET_CAT_SETUP_COUNT));
                                         /* OUTPUTS's tile */
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
}

/*
 * STOP over the warning stops: the band is the router's.  The warning
 * stays, and nothing is reverted or left.  Touch lost meanwhile drops the
 * press and not the warning.
 */
TEST_CASE(stop_while_the_warning_shows_stops_and_changes_nothing)
{
    fresh_hub();
    settings_set(SET_PACK_CELLS, 4.0f);
    warn_up();
    (void)ui_router_take_stop();
    tap_stop();
    CHECK(ui_router_take_stop());
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = DISCARD_CX, .y = STRIP_CY } };
    ui_router_event(&e);
    ui_router_cancel_gestures();
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
}

/*
 * ARMED: DISCARD is inert and changes nothing, also when the bench arms
 * between its press and its release.  SAVE and STAY still answer.
 */
TEST_CASE(discard_is_refused_while_armed)
{
    fresh_hub();
    settings_set(SET_PDMINI_TX, 4.0f);
    memset(s_seen_ids, 0, sizeof(s_seen_ids));
    settings_set_observer(every_id_observer);
    set_armed(true);
    warn_up();
    CHECK(settings_screen_warning());
    ui_router_render(&s_c, 0);
    tap(DISCARD_CX, STRIP_CY);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 4);
    CHECK_EQ(s_seen_ids[SET_PDMINI_TX], 0);

    /* Pressed SAFE, released ARMED. */
    set_armed(false);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = DISCARD_CX, .y = STRIP_CY } };
    ui_router_event(&e);
    set_armed(true);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 4);
    CHECK_EQ(s_seen_ids[SET_PDMINI_TX], 0);

    /* SAVE asks and leaves while ARMED; the write waits for idle. */
    tap(LSAVE_CX, STRIP_CY);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(settings_save_asked());
    settings_cancel_save();
    set_armed(false);
    settings_set_observer(NULL);
}

/*
 * A route the application takes leaves at once, the warning up or not:
 * the link lost (the bus-fault screen), an update, any ui_router_goto().
 * The values stay as they are, unsaved, and SETUP opens again on its tiles
 * without the warning.
 */
TEST_CASE(a_route_the_application_takes_is_never_held)
{
    fresh_hub();
    settings_set(SET_PACK_CELLS, 4.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!settings_screen_warning());

    ui_router_goto(SCREEN_SETUP);
    warn_up();
    CHECK(settings_screen_warning());
    ui_bench_status_t st = *ui_router_status();
    st.link_up = false;
    ui_router_set_status(&st);
    CHECK(settings_screen_warning());    /* the link alone moves nothing */
    ui_router_goto(SCREEN_BUSFAULT);
    CHECK_EQ(ui_router_current(), SCREEN_BUSFAULT);
    CHECK(!settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
    CHECK(settings_dirty());
    ui_router_goto(SCREEN_SETUP);
    CHECK(!settings_screen_warning());
    CHECK_EQ(settings_screen_category(), -1);
}

/* A tile that opens a screen of its own returns to SETUP: no way out, and
 * no warning, there or on its home tag back. */
TEST_CASE(a_tool_tile_is_no_way_out)
{
    for (int i = 0; i < settings_hub_tiles(); ++i) {
        fresh_hub();
        settings_set(SET_PACK_CELLS, 4.0f);
        tap(tile_x(i), tile_y(i));
        CHECK(!settings_screen_warning());
        if (ui_router_current() == SCREEN_SETUP) {
            CHECK(settings_screen_category() >= 0);   /* a category */
            continue;
        }
        CHECK(settings_screen_opens(ui_router_current()));
        tap(home_x(), home_y());
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK(!settings_screen_warning());
    }
}

static int s_out_applied;

static void count_apply(const outbind_t *b)
{
    (void)b;
    ++s_out_applied;
}

/* The OUTPUTS tile's slot: after the categories. */
#define OUTPUTS_TILE SET_CAT_SETUP_COUNT

/*
 * The OUTPUTS tile opens the overview, and nothing about it is a setting:
 * opening it and a tap on a pin write no binding, and neither they nor a
 * pin ticked and unticked leave anything unsaved or warn on leaving
 * SETUP.  A value changed after the visit still holds the home tag.
 */
TEST_CASE(the_outputs_overview_leaves_nothing_unsaved)
{
    fresh_hub();
    outputs_screen_set_apply(count_apply);
    s_out_applied = 0;
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
    outbind_set_proto(&b, 1u);                       /* SERVO PWM */
    CHECK(outbind_toggle(&b, outbind_index_of(OUTBIND_BOARD_PICO_HEADER, 2)));
    outputs_screen_set_binding(&b);

    tap(tile_x(OUTPUTS_TILE), tile_y(OUTPUTS_TILE));
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK(!settings_dirty());
    /* GP4, column 0 row 4 of the grid: no protocol, no write.  The same
     * tap with SERVO PWM picked ticks it, so the tap is on the pin. */
    const int gp4_x = OU_CELL_X(4) + 60;
    const int gp4_y = UI_BAND_H + OU_CELL_Y(4) + 26;
    tap(gp4_x, gp4_y);
    CHECK_EQ(s_out_applied, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);
    ui_router_render(&s_c, 0);
    outputs_screen_select(1u);
    tap(gp4_x, gp4_y);
    CHECK_EQ(s_out_applied, 1);
    tap(gp4_x, gp4_y);
    CHECK_EQ(s_out_applied, 2);
    CHECK(!settings_dirty());
    outputs_screen_select(0u);

    tap(home_x(), home_y());
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(!settings_screen_warning());
    tap(home_x(), home_y());
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!settings_screen_warning());
    CHECK(!settings_dirty());
    CHECK_EQ(s_out_applied, 2);

    /* The guard still holds a real change made after the visit. */
    ui_router_goto(SCREEN_SETUP);
    tap(tile_x(OUTPUTS_TILE), tile_y(OUTPUTS_TILE));
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    tap(home_x(), home_y());
    settings_set(SET_PACK_CELLS, 4.0f);
    tap(home_x(), home_y());
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    CHECK_EQ(warn_lines(), 1);
    CHECK_EQ(s_out_applied, 2);
    outputs_screen_set_apply(NULL);
}

int main(void)
{
    RUN(an_edit_back_to_the_stored_value_is_no_change);
    RUN(the_stored_copy_follows_a_successful_save_only);
    RUN(discard_puts_back_every_value_through_the_observer);
    RUN(discard_leaves_a_held_value);
    RUN(a_key_away_and_back_is_no_change_for_every_setting);
    RUN(a_save_asked_falls_when_every_value_is_back);
    RUN(discard_after_a_refused_save_asks_for_a_write);
    RUN(leaving_setup_with_nothing_changed_warns_nothing);
    RUN(leaving_with_one_change_warns_and_names_it);
    RUN(a_changed_string_is_named_in_the_warning);
    RUN(the_warning_names_a_pin_open_and_spaces_gpio);
    RUN(more_changes_than_fit_end_on_their_count);
    RUN(a_value_edited_back_holds_nothing);
    RUN(a_save_asked_holds_nothing_and_a_failed_one_warns);
    RUN(save_on_the_warning_asks_and_leaves);
    RUN(discard_on_the_warning_reverts_by_the_edit_path_and_leaves);
    RUN(stay_keeps_the_list_and_its_scroll);
    RUN(the_warning_covers_the_tiles);
    RUN(stop_while_the_warning_shows_stops_and_changes_nothing);
    RUN(discard_is_refused_while_armed);
    RUN(a_route_the_application_takes_is_never_held);
    RUN(a_tool_tile_is_no_way_out);
    RUN(the_outputs_overview_leaves_nothing_unsaved);
    return test_summary("settings_leave");
}
