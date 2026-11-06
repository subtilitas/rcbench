/**
 * @file test_kst_panel_expert.c
 * @brief KST EXPERT MODE: what the KST and Chaservo programmer hides with
 * the mode OFF, the SERVO > SETTINGS > DUT row that switches it, and the
 * pages that name a hidden value a write or a restore changes.
 *
 * Cases:
 * - a_fresh_panel_starts_with_the_mode_off_and_the_expert_rows_absent
 * - with_the_mode_on_every_row_sits_at_its_own_place
 * - with_the_mode_off_tune_closes_up_and_the_strip_holds_four_tabs
 * - more_offers_release_pairing_only_with_the_mode_on
 * - no_press_on_tune_reaches_a_hidden_row
 * - the_locked_tab_never_edits_in_either_mode
 * - the_mode_kept_by_the_settings_shows_after_a_restart
 * - the_settings_row_turns_on_after_a_2s_hold_and_off_with_a_tap
 * - a_short_press_on_the_row_gives_the_band_hint_and_changes_nothing
 * - a_hold_that_leaves_the_row_or_loses_its_press_changes_nothing
 * - a_draft_keeps_an_expert_edit_and_the_write_page_names_it
 * - a_change_of_the_mode_closes_the_editor_open
 * - a_file_load_with_the_mode_off_names_each_hidden_value_it_changes
 * - a_file_check_page_names_the_hidden_values_first
 * - a_restore_with_the_mode_off_names_each_hidden_value_it_changes
 * - a_restore_of_an_unknown_servo_names_the_hidden_values_on_one_line
 * - a_step_key_held_as_the_mode_turns_off_stops_its_repeat
 * - the_fullest_pages_with_the_mode_off_end_above_their_buttons
 * - the_active_output_and_the_write_switch_are_the_same_in_both_modes
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"

#include "kst_file.h"
#include "servo_spots.h"
#include "splash_screen.h"

/* A store in memory: what a save wrote, a restart loads. */
static float s_kept[SETTING_COUNT];
static bool  s_have_kept;

static bool kept_load(float *values, int count)
{
    if (!s_have_kept) {
        return false;
    }
    memcpy(values, s_kept, (size_t)count * sizeof(float));
    return true;
}

static bool kept_save(const float *values, int count)
{
    memcpy(s_kept, values, (size_t)count * sizeof(float));
    s_have_kept = true;
    return true;
}

static const settings_store_t k_kept = { kept_load, kept_save, NULL, NULL };

/** @brief KST EXPERT MODE @p on, as SETTINGS leaves it, and a frame. */
static void mode(bool on)
{
    settings_set(SET_KST_EXPERT, on ? 1.0f : 0.0f);
    tick(0.02f);
    show();
}

/** @brief The overlay alone with the mode OFF. */
static void fresh_off(void)
{
    fresh();
    mode(false);
}

/** @brief Any line of the open warning page starts with @p s. */
static bool warns_from(const char *s)
{
    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        if (strncmp(kst_panel_warning_line(i), s, strlen(s)) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief The line a hidden value of @p name starts with on a page. */
static const char *hidden_line(const char *name)
{
    static char t[KST_PANEL_LINE];

    snprintf(t, sizeof(t), "%s (KST EXPERT MODE): ", name);
    return t;
}

/** @brief The tabs in the strip. */
static int tabs(void)
{
    int n = 0;

    for (int p = 0; p < 5; ++p) {
        n += kst_panel_tab_rect(p).w > 0 ? 1 : 0;
    }
    return n;
}

/** @brief The rows of the five pages that KST EXPERT MODE OFF hides: DEAD
 *         BAND, BOOST, STRETCH and the LOCKED tab's ten. */
static bool hidden_place(int page, int row, int col)
{
    return page == PG_LOCKED
        || (page == PG_TUNE && ((row == 0 && col == 0)
                                || (row == 1 && col == 1)
                                || (row == 2 && col == 0)));
}

TEST_CASE(a_fresh_panel_starts_with_the_mode_off_and_the_expert_rows_absent)
{
    canvas();
    scr = NULL;
    settings_set_store(NULL);
    settings_init();
    rig_fresh();
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    CHECK(!settings_def(SET_KST_EXPERT)->def);
    kst_panel_init(AREA);
    kst_panel_open();
    show();
    connected();
    CHECK_EQ(tabs(), 4);
    for (int page = PG_SERVO; page <= PG_LOCKED; ++page) {
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 2; ++col) {
                if (!hidden_place(page, row, col)) {
                    continue;
                }
                CHECK_EQ(val(page, row, col).w, 0);
                CHECK(kst_panel_label(page, row, col) == NULL);
                CHECK(!kst_panel_row_live(page, row, col));
            }
        }
    }
    CHECK_EQ(kst_panel_step_rect(PG_TUNE, 0, true).w, 0);
    CHECK_EQ(kst_panel_step_rect(PG_TUNE, 0, false).w, 0);
    CHECK_EQ(kst_panel_tab_rect(PG_LOCKED).w, 0);
    /* A tab no page has is the first. */
    CHECK_EQ(kst_panel_tab_rect(5).x, kst_panel_tab_rect(PG_SERVO).x);
    CHECK_EQ(kst_panel_tab_rect(-1).x, kst_panel_tab_rect(PG_SERVO).x);
    CHECK(kst_panel_tab_rect(5).w > 0);
    /* The rows the owner keeps by default are there and take a tap. */
    CHECK_STR_EQ(kst_panel_label(PG_TUNE, 1, 0), "MAX. DUTY");
    CHECK_STR_EQ(kst_panel_label(PG_TUNE, 2, 1), "SOFT START");
    CHECK(kst_panel_row_live(PG_TUNE, 1, 0));
    CHECK(kst_panel_row_live(PG_PULSE, 0, 0));
    CHECK(kst_panel_row_live(PG_FAILSAFE, 0, 0));
}

TEST_CASE(with_the_mode_on_every_row_sits_at_its_own_place)
{
    fresh();
    connected();
    CHECK_EQ(tabs(), 5);
    for (int page = PG_SERVO; page <= PG_LOCKED; ++page) {
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 2; ++col) {
                const gfx_rect_t v = val(page, row, col);

                if (v.w == 0) {
                    continue;
                }
                /* ROW0 64px under the area's top, a 56px pitch. */
                CHECK_EQ(v.y, AREA.y + 64 + row * 56);
                CHECK(kst_panel_label(page, row, col) != NULL);
            }
        }
    }
    CHECK_STR_EQ(kst_panel_label(PG_TUNE, 0, 0), "DEAD BAND");
    CHECK_STR_EQ(kst_panel_label(PG_TUNE, 1, 1), "BOOST");
    CHECK_STR_EQ(kst_panel_label(PG_TUNE, 2, 0), "STRETCH");
    CHECK_STR_EQ(kst_panel_label(PG_LOCKED, 0, 0), "20K SEL");
    CHECK(kst_panel_row_live(PG_TUNE, 0, 0));
    CHECK(kst_panel_step_rect(PG_TUNE, 0, true).w > 0);
}

TEST_CASE(with_the_mode_off_tune_closes_up_and_the_strip_holds_four_tabs)
{
    gfx_rect_t five[5];

    fresh();
    connected();
    for (int t = 0; t < 5; ++t) {
        five[t] = kst_panel_tab_rect(t);
    }
    mode(false);
    CHECK_EQ(tabs(), 4);
    CHECK_EQ(kst_panel_tab_rect(PG_LOCKED).w, 0);
    /* Four tabs over the row five took: no gap where LOCKED was. */
    CHECK_EQ(kst_panel_tab_rect(PG_SERVO).x, five[0].x);
    CHECK(abs((kst_panel_tab_rect(PG_FAILSAFE).x
               + kst_panel_tab_rect(PG_FAILSAFE).w)
              - (five[4].x + five[4].w)) <= 3);
    CHECK(kst_panel_tab_rect(PG_TUNE).w > five[1].w);
    /* TUNE: MAX. DUTY and SOFT START on the first line, SPEED LIMIT and
     * SPEED on the second, nothing under them. */
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    CHECK_EQ(val(PG_TUNE, 1, 0).y, AREA.y + 64);
    CHECK_EQ(val(PG_TUNE, 2, 1).y, AREA.y + 64);
    CHECK(val(PG_TUNE, 2, 1).x > val(PG_TUNE, 1, 0).x);
    CHECK_EQ(val(PG_TUNE, 3, 0).y, AREA.y + 64 + 56);
    CHECK_EQ(val(PG_TUNE, 3, 1).y, AREA.y + 64 + 56);
    CHECK_EQ(val(PG_TUNE, 3, 0).x, val(PG_TUNE, 1, 0).x);
    /* PULSE and FAILSAFE lose nothing. */
    CHECK_EQ(val(PG_PULSE, 3, 0).y, AREA.y + 64 + 3 * 56);
    CHECK_EQ(val(PG_FAILSAFE, 1, 1).y, AREA.y + 64 + 56);
    /* The page shown stays; LOCKED shown goes back to SERVO. */
    mode(true);
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    tap(kst_panel_tab_rect(PG_LOCKED));
    CHECK_EQ(kst_panel_page(), PG_LOCKED);
    mode(false);
    CHECK_EQ(kst_panel_page(), PG_SERVO);
    /* The tab strip draws four labels: the last tab is FAILSAFE. */
    tap(kst_panel_tab_rect(PG_FAILSAFE));
    CHECK_EQ(kst_panel_page(), PG_FAILSAFE);
}

TEST_CASE(more_offers_release_pairing_only_with_the_mode_on)
{
    fresh_off();
    connected();
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 2);
    CHECK_EQ(choice(TR(KP_VERIFY)), 0);
    CHECK_EQ(choice(TR(KP_DISCARD)), 1);
    CHECK_EQ(choice(TR(KP_RELEASE_TITLE)), -1);
    /* The place the third entry had takes nothing. */
    tap(kst_panel_choice_rect(2));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
    mode(true);
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(choices(), 3);
    CHECK_EQ(choice(TR(KP_RELEASE_TITLE)), 2);
}

TEST_CASE(no_press_on_tune_reaches_a_hidden_row)
{
    uint16_t was[3];

    fresh_off();
    connected();
    was[0] = draft(KST_F_DEAD_BAND);
    was[1] = draft(KST_F_BOOST);
    was[2] = draft(KST_F_STRETCH);
    tap(kst_panel_tab_rect(PG_TUNE));
    /* Every 8px of the rows the TUNE page has with the mode ON. */
    for (int y = AREA.y + 64; y < AREA.y + 64 + 4 * 56; y += 8) {
        for (int x = AREA.x; x < AREA.x + AREA.w; x += 8) {
            ev(x, y, TOUCH_EVENT_DOWN, 1);
            ev(x, y, TOUCH_EVENT_UP, 1);
            show();
            if (kst_panel_editor() == 1) {
                CHECK(strcmp(kst_panel_keypad()->title, "DEAD BAND") != 0);
                CHECK(strcmp(kst_panel_keypad()->title, "BOOST") != 0);
            }
            if (kst_panel_editor() == 2) {
                CHECK(strcmp(kst_panel_choice_title(), "STRETCH") != 0);
            }
            if (kst_panel_editor() == 3) {
                hold(0.1f);
            }
            if (kst_panel_editor() != 0) {
                kst_panel_back();
            }
            CHECK_EQ(kst_panel_editor(), 0);
        }
    }
    CHECK_EQ(draft(KST_F_DEAD_BAND), was[0]);
    CHECK_EQ(draft(KST_F_BOOST), was[1]);
    CHECK_EQ(draft(KST_F_STRETCH), was[2]);
    /* The place STRETCH had, now under the rows, takes nothing. */
    CHECK_EQ(kst_panel_editor(), 0);
    tap((gfx_rect_t){ (int16_t)(AREA.x + 160), (int16_t)(AREA.y + 64 + 112),
                      150, 48 });
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(the_locked_tab_never_edits_in_either_mode)
{
    for (int on = 1; on >= 0; --on) {
        fresh();
        connected();
        const kst_image_t before = *kst_prog_draft(M);

        mode(on != 0);
        if (on != 0) {
            tap(kst_panel_tab_rect(PG_LOCKED));
            CHECK_EQ(kst_panel_page(), PG_LOCKED);
        }
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 2; ++col) {
                CHECK(!kst_panel_row_live(PG_LOCKED, row, col));
                if (val(PG_LOCKED, row, col).w > 0) {
                    tap(val(PG_LOCKED, row, col));
                    CHECK_EQ(kst_panel_editor(), 0);
                }
            }
        }
        CHECK_EQ(memcmp(kst_prog_draft(M), &before, sizeof(before)), 0);
        CHECK_EQ(kst_prog_changes(M), 0);
    }
}

/** @brief A panel restart: the settings loaded, the overlay made and
 *         opened. */
static void panel_restart(void)
{
    settings_init();
    kst_panel_init(AREA);
    kst_panel_open();
    show();
}

TEST_CASE(the_mode_kept_by_the_settings_shows_after_a_restart)
{
    s_have_kept = false;
    fresh();
    settings_set_store(&k_kept);
    panel_restart();
    /* Nothing stored: OFF. */
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    CHECK_EQ(tabs(), 4);
    settings_set(SET_KST_EXPERT, 1.0f);
    CHECK(settings_save());
    panel_restart();
    CHECK(settings_get_bool(SET_KST_EXPERT));
    CHECK_EQ(tabs(), 5);
    CHECK(kst_panel_label(PG_TUNE, 0, 0) != NULL);
    /* OFF, kept the same way. */
    settings_set(SET_KST_EXPERT, 0.0f);
    CHECK(settings_save());
    panel_restart();
    CHECK_EQ(tabs(), 4);
    CHECK(kst_panel_label(PG_TUNE, 0, 0) == NULL);
    settings_set_store(NULL);
    settings_init();
}

/* ------------------------------------------------- SERVO > SETTINGS > DUT */

/* The KST EXPERT MODE row: DUT's right column, its second row. */
#define KX_X 470
#define KX_Y SV_ROW_Y(1)

/** @brief The SERVO screen with the router on it, SETTINGS > DUT shown,
 *         the mode OFF. */
static void on_dut(void)
{
    /* The router on SERVO first: the band's hint needs it. */
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_SERVO);
    fresh_screen();
    settings_set(SET_KST_EXPERT, 0.0f);
    ui_router_set_alert(NULL);
    screen_tap(SETB_X, BTN_Y);
    screen_tap(SV_PAGE_X(2), SV_PAGE_TAB_Y);
}

/** @brief A press on the row held @p secs and let go on it. */
static void kx_hold(float secs)
{
    ev(KX_X, KX_Y, TOUCH_EVENT_DOWN, 1);
    ticks(secs);
    scr->render(&cv, 0);
    ev(KX_X, KX_Y, TOUCH_EVENT_UP, 1);
    tick(0.02f);
    scr->render(&cv, 0);
}

TEST_CASE(the_settings_row_turns_on_after_a_2s_hold_and_off_with_a_tap)
{
    on_dut();
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    /* 1.9s: still OFF. */
    kx_hold(1.9f);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    /* 2.1s: ON, asked to be saved, and no hint on the band. */
    ui_router_set_alert(NULL);
    kx_hold(2.1f);
    CHECK(settings_get_bool(SET_KST_EXPERT));
    CHECK(settings_save_asked());
    CHECK(ui_router_alert() == NULL);
    /* A tap turns it OFF. */
    screen_tap(KX_X, KX_Y);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    /* A tap on OFF is a short press: it stays OFF. */
    screen_tap(KX_X, KX_Y);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    /* The overlay follows: four tabs on PROGRAM. */
    screen_tap(PROG_X, BTN_Y);
    program_open();
    CHECK(kst_panel_is_open());
    CHECK_EQ(tabs(), 4);
    program_close();
    CHECK(!kst_panel_is_open());
    screen_tap(SETB_X, BTN_Y);
    screen_tap(SV_PAGE_X(2), SV_PAGE_TAB_Y);
    kx_hold(2.1f);
    CHECK(settings_get_bool(SET_KST_EXPERT));
    program_open();
    CHECK(kst_panel_is_open());
    CHECK_EQ(tabs(), 5);
}

TEST_CASE(a_short_press_on_the_row_gives_the_band_hint_and_changes_nothing)
{
    on_dut();
    kx_hold(0.5f);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), ui_tr(TX_HOLD_HINT_KST_EXPERT));
    /* At 1.95s, the same. */
    ui_router_set_alert(NULL);
    kx_hold(1.95f);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    CHECK_STR_EQ(ui_router_alert(), ui_tr(TX_HOLD_HINT_KST_EXPERT));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_tr(TX_HOLD_HINT_KST_EXPERT),
                 "Halte 2s, um KST EXPERT MODE einzuschalten.");
    CHECK_STR_EQ(ui_tr(TX_SV_ROW_KST_EXPERT), "KST EXPERT MODE");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_hold_that_leaves_the_row_or_loses_its_press_changes_nothing)
{
    on_dut();
    /* A finger that slides off at 1s. */
    ev(KX_X, KX_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    ev(KX_X, KX_Y + 80, TOUCH_EVENT_MOVE, 1);
    ticks(1.5f);
    ev(KX_X, KX_Y + 80, TOUCH_EVENT_UP, 1);
    tick(0.02f);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    /* A press the screen is told is stale (a lost touch). */
    ev(KX_X, KX_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    scr->cancel();
    ticks(1.5f);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    /* A task left under the hold. */
    ev(KX_X, KX_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    ev(KX_X, KX_Y, TOUCH_EVENT_UP, 1);
    screen_tap(LIVE_X, BTN_Y);
    ticks(1.5f);
    CHECK(!settings_get_bool(SET_KST_EXPERT));
}

/* ------------------------------------------- hidden values on the pages */

TEST_CASE(a_draft_keeps_an_expert_edit_and_the_write_page_names_it)
{
    fresh();
    connected();
    enable();
    edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
    edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 2u));
    mode(false);
    /* The rule: the draft keeps the edit. */
    CHECK_EQ(draft(KST_F_DEAD_BAND), servo(KST_F_DEAD_BAND) + 2u);
    CHECK_EQ(kst_prog_changes(M), 2);
    tap(val(PG_SERVO, 4, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns_from(hidden_line("DEAD BAND")));
    CHECK(warns_from("MAX. DUTY: "));
    /* The hidden one first, in the warning colour. */
    CHECK(strncmp(kst_panel_warning_line(0), hidden_line("DEAD BAND"),
                  strlen(hidden_line("DEAD BAND"))) == 0);
    CHECK_EQ(kst_panel_warning_level(0), (int)KST_PROG_WARN);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo(KST_F_DEAD_BAND), draft(KST_F_DEAD_BAND));
    /* With the mode ON the same write names it as any value. */
    fresh();
    connected();
    enable();
    edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
    tap(val(PG_SERVO, 4, 1));
    CHECK(warns_from("DEAD BAND: "));
    CHECK(!warns_from(hidden_line("DEAD BAND")));
}

TEST_CASE(a_change_of_the_mode_closes_the_editor_open)
{
    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 1));                /* BOOST's keypad */
    CHECK_EQ(kst_panel_editor(), 1);
    mode(false);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* RELEASE PAIRING's page, open as the mode goes OFF, sends nothing. */
    mode(true);
    enable();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    CHECK_EQ(kst_panel_editor(), 3);
    mode(false);
    CHECK_EQ(kst_panel_editor(), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

/** @brief A file of the bench servo with @p n fields changed. */
static void file_of(const char *name, const kst_field_id_t *id,
                    const uint16_t *raw, int n)
{
    kst_image_t img = sim_bench_image();

    for (int i = 0; i < n; ++i) {
        CHECK(kst_field_edit(&img, id[i], raw[i]));
    }
    (void)fake_card_put(&g_rig.fc, name, &img);
}

/** @brief FILES, the file @p name, and what the card answers. */
static void load(const char *name)
{
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    for (int i = 1; kst_panel_choice_label(i) != NULL; ++i) {
        if (strncmp(kst_panel_choice_label(i), name, strlen(name)) == 0) {
            tap(kst_panel_choice_rect(i));
            break;
        }
    }
    settle();
}

TEST_CASE(a_file_load_with_the_mode_off_names_each_hidden_value_it_changes)
{
    fresh_off();
    connected();
    {
        const kst_field_id_t id[] = { KST_F_BOOST, KST_F_DEAD_BAND,
                                      KST_F_DUTY };
        const uint16_t raw[] = {
            (uint16_t)(servo(KST_F_BOOST) - 3u),
            (uint16_t)(servo(KST_F_DEAD_BAND) - 2u),    /* wider */
            (uint16_t)(servo(KST_F_DUTY) - 1u),
        };

        file_of("EXP", id, raw, 3);
    }
    load("EXP");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    /* The whole image: the hidden values too. */
    CHECK_EQ(kst_prog_changes(M), 3);
    enable();
    tap(val(PG_SERVO, 4, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns_from(hidden_line("DEAD BAND")));
    CHECK(warns_from(hidden_line("BOOST")));
    CHECK(warns_from("MAX. DUTY: "));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo(KST_F_BOOST), draft(KST_F_BOOST));
    CHECK_EQ(servo(KST_F_DEAD_BAND), draft(KST_F_DEAD_BAND));
}

TEST_CASE(a_file_check_page_names_the_hidden_values_first)
{
    fresh_off();
    connected();
    {
        /* The three hidden fields, BOOST raised, which asks a second look,
         * and four values a tab shows: the page has four lines for
         * values. */
        const kst_field_id_t id[] = { KST_F_BOOST, KST_F_DEAD_BAND,
                                      KST_F_STRETCH, KST_F_DUTY,
                                      KST_F_SOFT_START, KST_F_LEFT_ANGLE,
                                      KST_F_UNCONT_TIME };
        const uint16_t raw[] = {
            (uint16_t)(servo(KST_F_BOOST) + 3u),
            (uint16_t)(servo(KST_F_DEAD_BAND) - 2u),
            (uint16_t)(servo(KST_F_STRETCH) != 0u ? 0u : 1u),
            (uint16_t)(servo(KST_F_DUTY) - 1u),
            (uint16_t)(1u - servo(KST_F_SOFT_START)),
            (uint16_t)(servo(KST_F_LEFT_ANGLE) - 1u),
            (uint16_t)(servo(KST_F_UNCONT_TIME) + 1u),
        };
        char more[KST_PANEL_LINE];

        file_of("SOFT", id, raw, 7);
        snprintf(more, sizeof(more), TR(KP_VALUES_MORE), 4u);
        load("SOFT");
        CHECK_EQ(kst_panel_editor(), 3);
        /* Each hidden one named; the count is of the four a tab shows. */
        CHECK(warns_from(hidden_line("DEAD BAND")));
        CHECK(warns_from(hidden_line("BOOST")));
        CHECK(warns_from(hidden_line("STRETCH")));
        CHECK(warns(more));
        CHECK(!warns_from("MAX. DUTY: "));
    }
    CHECK(warns(TR(KP_W_BOOST_RAISED)));
    hold(2.1f);
    CHECK_EQ(draft(KST_F_BOOST), servo(KST_F_BOOST) + 3u);
}

/** @brief RESTORE, this session's backup, and the check page before the
 *         restore's page where the backup asks one. */
static void restore_session(void)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK(choice(TR(KP_FROM_SESSION)) >= 0);
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 3);
    if (warns(TR(KP_CHECK_1))) {
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 3);
    }
}

TEST_CASE(a_restore_with_the_mode_off_names_each_hidden_value_it_changes)
{
    uint16_t boost0;

    fresh();
    g_rig.sim.reg[0x1D] = 0x5Au;
    connected();
    boost0 = servo(KST_F_BOOST);
    enable();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 3u));
    edit(KST_F_STRETCH, (uint16_t)(servo(KST_F_STRETCH) != 0u ? 0u : 1u));
    tap(val(PG_SERVO, 4, 1));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    /* RELEASE PAIRING: register 1D to 00. */
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    hold(2.1f);
    settle();
    CHECK_EQ(g_rig.sim.reg[0x1D], 0x00u);
    /* With the mode ON the restore's page lists no values. */
    restore_session();
    CHECK(!warns_from(hidden_line("BOOST")));
    tap(kst_panel_cancel_rect());
    mode(false);
    restore_session();
    CHECK(warns_from(hidden_line("BOOST")));
    CHECK(warns_from(hidden_line("STRETCH")));
    CHECK(warns("Pairing (KST EXPERT MODE): 00 -> 5A"));
    CHECK(!warns_from(hidden_line("DEAD BAND")));
    hold(2.1f);
    settle();
    CHECK_EQ(g_rig.sim.reg[0x1D], 0x5Au);
    CHECK_EQ(servo(KST_F_BOOST), boost0);
}

/** @brief A panel that starts under a locked session, its backup fetched
 *         and no servo read (test_kst_panel.c's case of the same name). */
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
    CHECK(!kst_prog_have_servo(M));
}

TEST_CASE(a_restore_of_an_unknown_servo_names_the_hidden_values_on_one_line)
{
    for (int on = 1; on >= 0; --on) {
        restart_locked();
        mode(on != 0);
        tap(val(PG_SERVO, 3, 0));           /* the write switch */
        hold(2.1f);
        CHECK(kst_prog_write_enabled(M));
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 2);
        CHECK(choice(TR(KP_FROM_SESSION)) >= 0);
        tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(warns(TR(KP_RESTORE_ALL)));
        CHECK_EQ(warns(TR(KP_HIDDEN_UNKNOWN)), on == 0);
    }
    CHECK(strstr(TR(KP_HIDDEN_UNKNOWN), "DEAD BAND") != NULL);
    CHECK(strstr(TR(KP_HIDDEN_UNKNOWN), "BOOST") != NULL);
    CHECK(strstr(TR(KP_HIDDEN_UNKNOWN), "STRETCH") != NULL);
    CHECK(strstr(TR(KP_HIDDEN_UNKNOWN), "LOCKED") != NULL);
    CHECK(strstr(TR(KP_HIDDEN_UNKNOWN), "Pairing") != NULL);
}

TEST_CASE(a_step_key_held_as_the_mode_turns_off_stops_its_repeat)
{
    uint16_t at;

    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    {
        const gfx_rect_t plus = kst_panel_step_rect(PG_TUNE, 0, true);

        ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
        ticks(1.0f);
        CHECK(kst_prog_changes(M) > 0u);
        at = draft(KST_F_DEAD_BAND);
        const uint32_t rev = kst_panel_revision();
        mode(false);
        CHECK(kst_panel_revision() != rev);
        ticks(1.5f);
        ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    }
    CHECK_EQ(draft(KST_F_DEAD_BAND), at);
    CHECK_EQ(kst_panel_editor(), 0);
}

/** @brief The rows the open warning page's lines take. */
static int page_rows(void)
{
    int rows = 0;

    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        rows += kst_panel_text_rows(kst_panel_warning_line(i));
    }
    return rows;
}

/** @brief The open page's lines end 8px above its buttons: WARN_TOP, 64px,
 *         and LINE_H, 20px, of kst_panel_int.h. */
static bool page_fits(void)
{
    return AREA.y + 64 + page_rows() * 20 + 8 <= kst_panel_apply_rect().y;
}

TEST_CASE(the_fullest_pages_with_the_mode_off_end_above_their_buttons)
{
    for (int lang = 0; lang < 2; ++lang) {
        /* Ten values on the write page, three of them hidden. */
        fresh_off();
        ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
        connected();
        enable();
        edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
        edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 3u));
        edit(KST_F_STRETCH,
             (uint16_t)(servo(KST_F_STRETCH) != 0u ? 0u : 1u));
        edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 2u));
        edit(KST_F_SOFT_START, (uint16_t)(1u - servo(KST_F_SOFT_START)));
        edit(KST_F_NEUTRAL, (uint16_t)(servo(KST_F_NEUTRAL) + 5u));
        edit(KST_F_PULSE_LOWER, (uint16_t)(servo(KST_F_PULSE_LOWER) + 10u));
        edit(KST_F_PULSE_UPPER, (uint16_t)(servo(KST_F_PULSE_UPPER) - 10u));
        edit(KST_F_LEFT_ANGLE, (uint16_t)(servo(KST_F_LEFT_ANGLE) - 1u));
        edit(KST_F_REVERSION, (uint16_t)(1u - servo(KST_F_REVERSION)));
        tap(val(PG_SERVO, 4, 1));
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(warns_from(hidden_line("DEAD BAND")));
        CHECK(warns_from(hidden_line("BOOST")));
        CHECK(warns_from(hidden_line("STRETCH")));
        CHECK(page_fits());
        /* The locked session's restore with no card. */
        restart_locked();
        ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
        mode(false);
        card_out();
        tick(0.02f);
        tap(val(PG_SERVO, 3, 0));
        hold(2.1f);
        tap(val(PG_SERVO, 4, 0));
        tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(warns(TR(KP_HIDDEN_UNKNOWN)));
        CHECK(page_fits());
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_active_output_and_the_write_switch_are_the_same_in_both_modes)
{
    char port[2][64];
    char enable_label[2][64];

    for (int on = 1; on >= 0; --on) {
        fresh();
        mode(on != 0);
        link_up();
        snprintf(port[on], sizeof(port[on]), "%s",
                 kst_panel_label(PG_SERVO, 0, 0));
        connected();
        snprintf(enable_label[on], sizeof(enable_label[on]), "%s",
                 kst_panel_button(PG_SERVO, 3, 0));
        CHECK(!kst_prog_write_enabled(M));
        enable();
        CHECK(kst_prog_write_enabled(M));
        CHECK_EQ(kst_panel_pin(), RIG_PIN);
    }
    CHECK_STR_EQ(port[0], port[1]);
    CHECK_STR_EQ(enable_label[0], enable_label[1]);
}

int main(void)
{
    RUN(a_fresh_panel_starts_with_the_mode_off_and_the_expert_rows_absent);
    RUN(with_the_mode_on_every_row_sits_at_its_own_place);
    RUN(with_the_mode_off_tune_closes_up_and_the_strip_holds_four_tabs);
    RUN(more_offers_release_pairing_only_with_the_mode_on);
    RUN(no_press_on_tune_reaches_a_hidden_row);
    RUN(the_locked_tab_never_edits_in_either_mode);
    RUN(the_mode_kept_by_the_settings_shows_after_a_restart);
    RUN(the_settings_row_turns_on_after_a_2s_hold_and_off_with_a_tap);
    RUN(a_short_press_on_the_row_gives_the_band_hint_and_changes_nothing);
    RUN(a_hold_that_leaves_the_row_or_loses_its_press_changes_nothing);
    RUN(a_draft_keeps_an_expert_edit_and_the_write_page_names_it);
    RUN(a_change_of_the_mode_closes_the_editor_open);
    RUN(a_file_load_with_the_mode_off_names_each_hidden_value_it_changes);
    RUN(a_file_check_page_names_the_hidden_values_first);
    RUN(a_restore_with_the_mode_off_names_each_hidden_value_it_changes);
    RUN(a_restore_of_an_unknown_servo_names_the_hidden_values_on_one_line);
    RUN(a_step_key_held_as_the_mode_turns_off_stops_its_repeat);
    RUN(the_fullest_pages_with_the_mode_off_end_above_their_buttons);
    RUN(the_active_output_and_the_write_switch_are_the_same_in_both_modes);
    return test_summary("kst_panel_expert");
}
