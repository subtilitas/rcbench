/**
 * @file test_esc_stick_panel_list.c
 * @brief ESC STICK's profile list: what it offers for a voltage and a cap, the
 * search and its keys, and the header at the registry's most.
 *
 * Cases:
 * - reset_and_exit_rows_offer_no_value
 * - the_list_follows_voltage_and_the_cap
 * - a_profile_over_the_supply_cap_is_refused_in_the_list
 * - the_search_filters_the_list_with_every_key
 * - ok_keeps_the_search_cancel_restores_it_and_empty_clears_it
 * - a_lost_touch_drops_a_search_key_under_way
 * - the_header_fits_at_the_registrys_most
 * - a_voltage_over_the_escs_rating_is_refused
 * - the_page_offers_only_the_models_items
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* The list follows VOLTAGE and the SUPPLY cap: its order and its count are
 * built again when either moves, not only when the session opens. */
TEST_CASE(the_list_follows_voltage_and_the_cap)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    settings_set(SET_SUPPLY_V_MAX, 7.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    /* Every hobbywing-flyfun-8item model needs 7.6 V or more now: its row
     * is a refused one and opens nothing. */
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK(esc_stick_panel_page() == NULL);
    /* The cap back up: it opens again. */
    settings_set(SET_SUPPLY_V_MAX, 21.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK(esc_stick_panel_page() != NULL);
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    CHECK_STR_EQ(esc_stick_panel_stick()->p->id, "hobbywing-flyfun-8item");
    scr->leave();
    /* And on the way back from a profile, and on opening the session
     * again after the screen was left. */
    scr->enter();
    open_stick();
    open_maker("Hobbywing");
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK(esc_stick_panel_page() != NULL);
    tap(BACK_X, BACK_Y);
    settings_set(SET_STICK_V, 20.0f);
    settings_set(SET_SUPPLY_V_MAX, 12.0f);
    supply_screen_limits_changed();
    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* Reset and exit are actions: their rows offer no value. */
TEST_CASE(reset_and_exit_rows_offer_no_value)
{
    fresh();
    descend_to_hobbywing();
    tap(PAGE_DN_X, PAGE_CY);                 /* items 2 to 8 */
    tap(STEP_UP_X, STEP_CY(5));              /* item 7: set all to default */
    tap(STEP_UP_X, STEP_CY(6));              /* item 8: exit */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
}

/* A profile the supply cannot give the voltage for is refused in the list,
 * with the voltage it needs. */
TEST_CASE(a_profile_over_the_supply_cap_is_refused_in_the_list)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 7.0f);
    supply_screen_limits_changed();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    /* Every model with a cell count needs 7.6 V or more: none opens. */
    open_model("hobbywing-flyfun-8item", NULL);
    tap(STEP_UP_X, STEP_CY(2));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    CHECK(esc_stick_panel_page() == NULL);
    /* DualSky's XController states none, and opens, its rating
     * unknown. */
    tap(BACK_X, BACK_Y);
    open_maker("Dualsky");
    open_model("dualsky-xcontroller", NULL);
    CHECK(esc_stick_panel_page() != NULL);
    tap(TIMING_X, TIMING_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* The page offers only the items on the model opened: FLYFUN V5's BEC
 * voltage is on its 60, 80 and 120 A models.  With no model, only the
 * items every model has. */
TEST_CASE(the_page_offers_only_the_models_items)
{
    const esc_profile_t *p = esc_profiles_find("hobbywing-flyfun-v5");
    CHECK(p != NULL);
    if (p == NULL) {
        return;
    }
    int bec = -1;
    for (unsigned i = 0; i < p->item_count; ++i) {
        bec = (p->items[i].applies_count > 0u) ? (int)i : bec;
    }
    CHECK(bec >= 0);
    static const struct { const char *model; bool has; } k[] = {
        { "FLYFUN 60A V5", true }, { "FLYFUN 80A OPTO V5", false },
    };
    for (size_t c = 0; c < 2; ++c) {
        fresh();
        esc_stick_panel_bench(0u, false, 0u, 0u, false);
        open_stick();
        open_maker("Hobbywing");
        open_model("hobbywing-flyfun-v5", k[c].model);
        CHECK(esc_stick_panel_page() != NULL);
        bool seen = false;
        int rows = 0;
        for (int i = 0; esc_stick_panel_item_at(i) >= 0; ++i) {
            seen = seen || esc_stick_panel_item_at(i) == bec;
            ++rows;
        }
        CHECK_EQ(seen, k[c].has);
        CHECK_EQ(rows, (int)p->item_count - (k[c].has ? 0 : 1));
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK(lit() > 20000);
        scr->leave();
    }
}

/* The model's own rating, not only the SUPPLY cap: FlyFun-6A is rated
 * 8.4 V and 6 A.  VOLTAGE 20 V is refused on its row and on RUN, 7.6 V
 * runs; the automatic voltage stays under the rating. */
TEST_CASE(a_voltage_over_the_escs_rating_is_refused)
{
    fresh();
    settings_set(SET_STICK_V, 20.0f);
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    int six = -1;
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        int m = -1;
        const esc_profile_t *p = esc_stick_panel_row(i, &m);
        if (p != NULL && strcmp(p->id, "hobbywing-flyfun-8item") == 0
            && strcmp(p->models[m].name, "FlyFun-6A") == 0) {
            six = i;
        }
    }
    CHECK(six >= 0);
    CHECK_STR_EQ(esc_stick_panel_row_why(six),
                 "20.0 V; ESC maximum 8.4 V");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(esc_stick_panel_row_why(six),
                 "20.0 V; ESC höchstens 8.4 V");
    ui_text_set_language(UI_LANG_EN);
    open_model("hobbywing-flyfun-8item", "FlyFun-6A");
    CHECK(esc_stick_panel_page() == NULL);   /* refused */

    /* Set while the page is open: RUN says why, and nothing starts. */
    settings_set(SET_STICK_V, 7.6f);
    scr->tick(0.02f);
    CHECK(esc_stick_panel_row_why(six) == NULL);
    open_model("hobbywing-flyfun-8item", "FlyFun-6A");
    CHECK(esc_stick_panel_page() != NULL);
    pick_cutoff();
    settings_set(SET_STICK_V, 20.0f);
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    CHECK_STR_EQ(esc_stick_panel_note(),
                 "VOLTAGE 20.0 V exceeds the ESC limit of 8.4 V.");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    settings_set(SET_STICK_V, 7.6f);
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    scr->leave();

    /* The automatic voltage: the model's 2 cells, 7.6 V, under 8.4 V. */
    fresh();
    settings_set(SET_STICK_V, 0.0f);
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    open_model("hobbywing-flyfun-8item", "FlyFun-6A");
    CHECK(esc_stick_panel_page() != NULL);
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    CHECK_EQ(esc_stick_panel_stick()->out.supply_mv, 7600u);
    scr->leave();

    /* CURRENT LIMIT over a rating a card profile states: a FlyFun-6A
     * card rated 1 A refuses 2 A, on its row and on RUN. */
    fresh();
    esc_profiles_clear_overrides();
    static esc_model_t k_one;
    static esc_profile_t card;
    card = *esc_profiles_find("hobbywing-flyfun-8item");
    for (unsigned m = 0; m < card.model_count; ++m) {
        if (strcmp(card.models[m].name, "FlyFun-6A") == 0) {
            k_one = card.models[m];
        }
    }
    k_one.current_a = 1u;
    card.id = "card-one-amp";
    card.models = &k_one;
    card.model_count = 1;
    CHECK(esc_profiles_override(&card, NULL));
    settings_set(SET_STICK_I, 2.0f);
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    int row = -1;
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        const esc_profile_t *q = esc_stick_panel_row(i, NULL);
        row = (q != NULL && strcmp(q->id, "card-one-amp") == 0) ? i : row;
    }
    const char *why = esc_stick_panel_row_why(row);
    CHECK(why != NULL && strcmp(why, "2.0 A; ESC maximum 1 A") == 0);
    settings_set(SET_STICK_I, 1.0f);
    scr->tick(0.02f);
    CHECK(esc_stick_panel_row_why(row) == NULL);
    open_model("card-one-amp", NULL);
    pick_cutoff();
    settings_set(SET_STICK_I, 2.0f);
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    CHECK_STR_EQ(esc_stick_panel_note(),
                 "CURRENT LIMIT 2.0 A exceeds the ESC rating of 1 A.");
    settings_set(SET_STICK_I, 1.0f);
    scr->leave();
    esc_profiles_clear_overrides();

    /* KOLIBRI-X 60 LV states no cell count and no rating: VOLTAGE 0 takes
     * the family's lowest cell count, opens, and says so. */
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Kontronik");
    int kx = -1;
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        int m = -1;
        const esc_profile_t *q = esc_stick_panel_row(i, &m);
        kx = (q != NULL && strcmp(q->models[m].name, "KOLIBRI-X 60 LV") == 0)
                 ? i : kx;
    }
    CHECK(kx >= 0);
    CHECK(esc_stick_panel_row_why(kx) == NULL);
    const char *warn = esc_stick_panel_row_warn(kx);
    CHECK(warn != NULL && strcmp(warn, "ESC voltage rating unknown.") == 0);
    ui_text_set_language(UI_LANG_DE);
    warn = esc_stick_panel_row_warn(kx);
    CHECK(warn != NULL && strcmp(warn, "Zulässige ESC-Spannung unbekannt.") == 0);
    ui_text_set_language(UI_LANG_EN);
    open_model("kontronik-kontrol-x", "KOLIBRI-X 60 LV");
    CHECK(esc_stick_panel_page() != NULL);
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    tap(STEP_UP_X, STEP_CY(0));
    supply_reads_off();
    scr->tick(0.02f);
    char want[96];
    const uint32_t mv = esc_stick_model_mv(esc_profiles_find(
                                               "kontronik-kontrol-x"), -1);
    snprintf(want, sizeof(want),
             "%u.%u V uses the family's lowest cell count. This ESC's "
             "rating is unknown.",
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u));
    CHECK_STR_EQ(esc_stick_panel_note(), want);
    scr->leave();
}

/* Every key filters the list at once, and the list stays in view. */
TEST_CASE(the_search_filters_the_list_with_every_key)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK_EQ(listed(), 20);                  /* the makers */
    CHECK_STR_EQ(esc_stick_panel_search(), "");
    tap(LIST_DN_X, LIST_CY);                 /* a page down first */
    int top = 0;
    (void)esc_stick_panel_listed(&top);
    CHECK_EQ(top, 9);

    tap(FIND_X, FIND_Y);
    CHECK(esc_stick_panel_typing());
    find_key('S');
    CHECK_STR_EQ(esc_stick_panel_search(), "S");
    (void)esc_stick_panel_listed(&top);
    CHECK_EQ(top, 0);                        /* back to the top */
    const int after_s = listed();
    CHECK(after_s > 3 && after_s < 20);
    find_type("KY*V2");
    CHECK_STR_EQ(esc_stick_panel_search(), "SKY*V2");
    CHECK_EQ(listed(), 1);                   /* Hobbywing alone */
    find_key('<');                           /* DEL: "SKY*V" */
    CHECK(listed() >= 1);
    find_key('2');
    CHECK_EQ(listed(), 1);
    /* Drawn with the keyboard over the right of the list. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);

    /* The maker tapped while typing opens its models the search finds; no
     * maker opens by itself. */
    CHECK_EQ(esc_stick_panel_level(), 0);
    tap(100, SP_ROW_CY(0));
    CHECK(!esc_stick_panel_typing());
    CHECK_EQ(esc_stick_panel_level(), 1);
    CHECK_STR_EQ(esc_stick_panel_maker(), "Hobbywing");
    CHECK_EQ(listed(), 14);
    for (int i = 0; i < listed(); ++i) {
        int m = -1;
        const esc_profile_t *p = esc_stick_panel_row(i, &m);
        CHECK(p != NULL && esc_model_matches(p, (unsigned)m, "SKY*V2"));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 1);
    CHECK(lit() > 20000);
    /* Typing on the models filters them as well. */
    tap(FIND_X, FIND_Y);
    find_type("*MINI");
    CHECK(listed() > 0 && listed() < 14);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    find_key('~');                           /* CANCEL: SKY*V2 */
    CHECK_EQ(listed(), 14);

    /* A model opens its family's page; BACK returns with the search. */
    tap(ROW_CX, SP_ROW_CY(0));
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL);
    CHECK(esc_profile_matches(p, "SKY*V2"));
    tap(BACK_X, BACK_Y);
    CHECK(esc_stick_panel_page() == NULL);
    CHECK_EQ(esc_stick_panel_level(), 1);
    CHECK_STR_EQ(esc_stick_panel_search(), "SKY*V2");
    CHECK_EQ(listed(), 14);
    tap(BACK_X, BACK_Y);
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK_EQ(listed(), 1);

    /* X clears it. */
    tap(FIND_CLR_X, FIND_Y);
    CHECK_STR_EQ(esc_stick_panel_search(), "");
    CHECK_EQ(listed(), 20);
    CHECK(!esc_stick_panel_typing());
}

/* OK keeps what was typed, CANCEL goes back to what the keyboard opened
 * on, and an empty entry is no search. */
TEST_CASE(ok_keeps_the_search_cancel_restores_it_and_empty_clears_it)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    tap(FIND_X, FIND_Y);
    find_type("FLYFUN\n");
    CHECK(!esc_stick_panel_typing());
    CHECK_STR_EQ(esc_stick_panel_search(), "FLYFUN");
    CHECK_EQ(listed(), 1);

    tap(FIND_X, FIND_Y);
    find_type("#ZZZ");
    CHECK_EQ(listed(), 0);                   /* nothing found */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    find_key('~');                           /* CANCEL */
    CHECK(!esc_stick_panel_typing());
    CHECK_STR_EQ(esc_stick_panel_search(), "FLYFUN");
    CHECK_EQ(listed(), 1);

    tap(FIND_X, FIND_Y);
    find_type("#\n");                        /* CLR, OK: empty */
    CHECK_STR_EQ(esc_stick_panel_search(), "");
    CHECK_EQ(listed(), 20);

    /* The search holds its most, sixteen. */
    tap(FIND_X, FIND_Y);
    find_type("ABCDEFGHIJKLMNOPQRS");
    CHECK_EQ((int)strlen(esc_stick_panel_search()), 16);
    /* BACK and leaving the screen close the keyboard, the search kept. */
    tap(BACK_X, BACK_Y);
    CHECK(!esc_stick_panel_typing());
    open_stick();
    CHECK_EQ((int)strlen(esc_stick_panel_search()), 16);
    tap(FIND_X, FIND_Y);
    CHECK(esc_stick_panel_typing());
    scr->leave();
    CHECK(!esc_stick_panel_typing());
    CHECK_EQ(listed(), 0);
}

/* A press on a key the touch stream lost the release of types nothing. */
TEST_CASE(a_lost_touch_drops_a_search_key_under_way)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    tap(FIND_X, FIND_Y);
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_search(&k, k_dock, "", "", 16);
    const gfx_rect_t r = ui_textkey_key_rect(&k, 10);    /* Q */
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 3, .x = (int16_t)(r.x + 4),
                                   .y = (int16_t)(r.y + 4),
                                   .strength = 40 } };
    scr->event(&e);
    scr->cancel();
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
    CHECK_STR_EQ(esc_stick_panel_search(), "");
    CHECK(esc_stick_panel_typing());
    /* A key slid off is no key either. */
    e.type = TOUCH_EVENT_DOWN;
    scr->event(&e);
    e.type = TOUCH_EVENT_MOVE;
    e.point.x = 10;
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
    CHECK_STR_EQ(esc_stick_panel_search(), "");
}

/* The footer's count, mirrored from esc_stick_panel.c: SP_COUNT_W, 360
 * px, and beside the docked keyboard from PAD + 12 to SP_DOCK_X - 8, 326
 * px, of 8 px cells. */
#define COUNT_CELLS      45

#define COUNT_DOCK_CELLS 40

/*
 * The registry at its most -- every built-in profile and
 * ESC_PROFILE_MAX_OVERRIDES card ones, each card one a maker of its own --
 * found whole by "*", on the last page of each level: the widest count
 * either language can draw, with every row counted as one that runs, still
 * fits beside the docked keyboard.
 */
TEST_CASE(the_header_fits_at_the_registrys_most)
{
    fresh();
    esc_profiles_clear_overrides();
    static char ids[ESC_PROFILE_MAX_OVERRIDES][16];
    static char brands[ESC_PROFILE_MAX_OVERRIDES][16];
    for (unsigned i = 0; i < ESC_PROFILE_MAX_OVERRIDES; ++i) {
        esc_profile_t p = esc_profiles_builtin[i % esc_profiles_builtin_count];
        snprintf(ids[i], sizeof(ids[i]), "card-%u", i);
        snprintf(brands[i], sizeof(brands[i]), "Card %02u", i);
        p.id = ids[i];
        p.brand = brands[i];
        CHECK(esc_profiles_override(&p, NULL));
    }
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    tap(FIND_X, FIND_Y);
    find_type("*");
    const int makers = listed();
    CHECK_EQ(makers, 20 + (int)ESC_PROFILE_MAX_OVERRIDES);
    for (int i = 0; i < 20; ++i) {
        tap(LIST_DN_X, LIST_CY);
    }
    int top = 0;
    (void)esc_stick_panel_listed(&top);
    CHECK(top + 9 >= makers && top < makers);
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        static const ui_text_id_t k[] = {
            TX_SP_MAKERS_FOUND, TX_SP_MAKERS_COUNT, TX_SP_LIST_FOUND,
            TX_SP_LIST_COUNT,
        };
        for (size_t f = 0; f < sizeof(k) / sizeof(k[0]); ++f) {
            /* Makers at their most, models at a maker's list's most. */
            const int n = (f < 2u) ? makers : 512;
            char line[96];
            snprintf(line, sizeof(line), ui_tr_in((ui_lang_t)l, k[f]),
                     n - 8, n, n, n);
            if (gfx_text_cells(line) > COUNT_DOCK_CELLS) {
                T_FAIL("language %d: \"%s\" is %d cells, the box %d", l,
                       line, gfx_text_cells(line), COUNT_DOCK_CELLS);
            }
        }
        ui_text_set_language((ui_lang_t)l);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        stick_invalidate();
        scr->render(&cv, l & 1);
        CHECK(lit() > 20000);
    }
    CHECK(COUNT_DOCK_CELLS <= COUNT_CELLS);
    ui_text_set_language(UI_LANG_EN);
    find_key('~');
    esc_profiles_clear_overrides();
}

int main(void)
{
    RUN(reset_and_exit_rows_offer_no_value);
    RUN(the_list_follows_voltage_and_the_cap);
    RUN(a_profile_over_the_supply_cap_is_refused_in_the_list);
    RUN(the_search_filters_the_list_with_every_key);
    RUN(ok_keeps_the_search_cancel_restores_it_and_empty_clears_it);
    RUN(a_lost_touch_drops_a_search_key_under_way);
    RUN(the_header_fits_at_the_registrys_most);
    RUN(a_voltage_over_the_escs_rating_is_refused);
    RUN(the_page_offers_only_the_models_items);
    return test_summary("esc_stick_panel_list");
}
