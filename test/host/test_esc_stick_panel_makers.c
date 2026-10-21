/**
 * @file test_esc_stick_panel_makers.c
 * @brief ESC STICK's maker and model lists: their order, the family a model
 * opens, the search on both levels and the entry a run waits for.
 *
 * Cases:
 * - makers_are_alphabetical_and_count_their_models
 * - models_go_by_current_then_voltage_then_name
 * - a_model_row_opens_its_family_at_its_own_voltage
 * - the_search_finds_on_both_levels_and_back_keeps_it
 * - a_card_profile_joins_its_maker
 * - a_maker_at_its_most_models_lists_every_one
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* ------------------------------------------------- makers and models */

/* The first level: every maker once, alphabetical with case folded, its
 * models counted, those that run among them. */
TEST_CASE(makers_are_alphabetical_and_count_their_models)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK_EQ(listed(), 20);
    for (int i = 1; i < listed(); ++i) {
        const char *a = esc_stick_panel_maker_at(i - 1);
        const char *b = esc_stick_panel_maker_at(i);
        char x[48], y[48];
        snprintf(x, sizeof(x), "%s", a);
        snprintf(y, sizeof(y), "%s", b);
        for (char *c = x; *c != '\0'; ++c) {
            *c = (*c >= 'a' && *c <= 'z') ? (char)(*c - 32) : *c;
        }
        for (char *c = y; *c != '\0'; ++c) {
            *c = (*c >= 'a' && *c <= 'z') ? (char)(*c - 32) : *c;
        }
        if (strcmp(x, y) >= 0) {
            T_FAIL("%s listed before %s", a, b);
        }
    }
    CHECK_STR_EQ(esc_stick_panel_maker_at(0), "Align");
    CHECK(esc_stick_panel_maker_at(20) == NULL);
    /* Kontronik's models: every model of its 22 families. */
    open_maker("Kontronik");
    CHECK_EQ(esc_stick_panel_level(), 1);
    unsigned models = 0;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        models += same_name(p->brand, "Kontronik") ? p->model_count : 0u;
    }
    CHECK_EQ(listed(), (int)models);
    CHECK(esc_stick_panel_maker_at(0) == NULL);
    CHECK(esc_stick_panel_row(listed(), NULL) == NULL);
    draws();
    /* The second level's BACK keeps the first's page. */
    tap(BACK_X, BACK_Y);
    tap(LIST_DN_X, LIST_CY);
    open_maker("Robbe");
    tap(BACK_X, BACK_Y);
    int top = 0;
    (void)esc_stick_panel_listed(&top);
    CHECK_EQ(top, 9);
    CHECK_STR_EQ(esc_stick_panel_maker(), "");
}

/* One key of the order: smaller first, 0 -- not stated -- last. */
static int key_order(uint32_t a, uint32_t b)
{
    if (a == b) {
        return 0;
    }
    if (a == 0u || b == 0u) {
        return (a == 0u) ? 1 : -1;
    }
    return (a < b) ? -1 : 1;
}

/* The second level: one row a model, by current, then voltage, then name,
 * a value a model does not state last on its key. */
TEST_CASE(models_go_by_current_then_voltage_then_name)
{
    static const char *const k[] = { "Kontronik", "YGE", "Hobbywing",
                                     "Dualsky" };
    for (size_t b = 0; b < sizeof(k) / sizeof(k[0]); ++b) {
        fresh();
        esc_stick_panel_bench(0u, false, 0u, 0u, false);
        open_stick();
        open_maker(k[b]);
        bool missing = false;
        for (int i = 1; i < listed(); ++i) {
            int ma = -1, mb = -1;
            const esc_profile_t *pa = esc_stick_panel_row(i - 1, &ma);
            const esc_profile_t *pb = esc_stick_panel_row(i, &mb);
            const esc_model_t *x = &pa->models[ma], *y = &pb->models[mb];
            missing = missing || x->current_a == 0u || x->v_max_mv == 0u
                      || y->current_a == 0u || y->v_max_mv == 0u;
            int c = key_order(x->current_a, y->current_a);
            if (c == 0) {
                c = key_order(x->v_max_mv, y->v_max_mv);
            }
            if (c == 0) {
                char nx[64], ny[64];
                snprintf(nx, sizeof(nx), "%s", x->name);
                snprintf(ny, sizeof(ny), "%s", y->name);
                for (char *s1 = nx; *s1 != '\0'; ++s1) {
                    *s1 = (*s1 >= 'a' && *s1 <= 'z') ? (char)(*s1 - 32) : *s1;
                }
                for (char *s1 = ny; *s1 != '\0'; ++s1) {
                    *s1 = (*s1 >= 'a' && *s1 <= 'z') ? (char)(*s1 - 32) : *s1;
                }
                c = (strcmp(nx, ny) > 0) ? 1 : 0;
            }
            if (c > 0) {
                T_FAIL("%s: %s before %s", k[b], x->name, y->name);
            }
        }
        /* Kontronik, YGE and Dualsky each list models stating no current or
         * no voltage: they are last on that key. */
        if (b != 2u) {
            CHECK(missing);
        }
    }
    /* Kontronik's first: the smallest current it lists. */
    int m = -1;
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Kontronik");
    const esc_profile_t *p = esc_stick_panel_row(0, &m);
    uint16_t least = 0xFFFFu;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *q = esc_profiles_at(i);
        for (unsigned j = 0; same_name(q->brand, "Kontronik")
                             && j < q->model_count; ++j) {
            if (q->models[j].current_a != 0u
                && q->models[j].current_a < least) {
                least = q->models[j].current_a;
            }
        }
    }
    CHECK(p != NULL && p->models[m].current_a == least);
}

/* A model row opens its family's page, named with the model; the supply
 * takes the model's own cell count where it states one. */
TEST_CASE(a_model_row_opens_its_family_at_its_own_voltage)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Kontronik");
    open_model("kontronik-jazz", "JAZZ 55 LV");
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL && strcmp(p->id, "kontronik-jazz") == 0);
    const int m = esc_stick_panel_model();
    CHECK(m >= 0 && strcmp(p->models[m].name, "JAZZ 55 LV") == 0);
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    draws();

    /* hobbywing-flyfun-hv-9item: a model with more cells than the family's
     * fewest runs at its own voltage. */
    fresh();
    const esc_profile_t *q = esc_profiles_find("hobbywing-flyfun-hv-9item");
    CHECK(q != NULL);
    if (q == NULL) {
        return;
    }
    int most = 0;
    for (unsigned i = 1; i < q->model_count; ++i) {
        if (q->models[i].cells_min > q->models[most].cells_min) {
            most = (int)i;
        }
    }
    CHECK(esc_stick_model_mv(q, most) >= esc_stick_profile_mv(q));
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    open_model(q->id, q->models[most].name);
    if (esc_stick_panel_page() == q) {
        tap(STEP_UP_X, STEP_CY(0));
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        CHECK_EQ(esc_stick_panel_runs(), 1u);
        CHECK_EQ(esc_stick_panel_stick()->out.supply_mv,
                 esc_stick_model_mv(q, most));
        scr->leave();
    } else {
        /* Over the cap at its own voltage: refused on its row. */
        CHECK(esc_stick_model_mv(q, most) > 21000u);
    }
    CHECK_EQ(esc_stick_model_mv(q, -1), esc_stick_profile_mv(q));
}

/* The owner's example: *KONTR*JAZZ*55* finds Kontronik on the first level
 * and JAZZ 55 LV on the second; BACK keeps the search on both. */
TEST_CASE(the_search_finds_on_both_levels_and_back_keeps_it)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    tap(FIND_X, FIND_Y);
    find_type("*KONTR*JAZZ*55*");
    CHECK_EQ(listed(), 1);
    CHECK_STR_EQ(esc_stick_panel_maker_at(0), "Kontronik");
    draws();                                 /* docked, the makers */
    find_key('\n');
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK_EQ(esc_stick_panel_level(), 1);
    CHECK_EQ(listed(), 1);
    int m = -1;
    const esc_profile_t *p = esc_stick_panel_row(0, &m);
    CHECK(p != NULL && strcmp(p->models[m].name, "JAZZ 55 LV") == 0);
    tap(FIND_X, FIND_Y);
    draws();                                 /* docked, the models */
    find_key('~');
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(esc_stick_panel_page() == p);
    tap(CANCEL_X, HOLD_Y);
    tap(BACK_X, BACK_Y);
    CHECK_EQ(listed(), 1);
    tap(BACK_X, BACK_Y);
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK_STR_EQ(esc_stick_panel_search(), "*KONTR*JAZZ*55*");
    CHECK_EQ(listed(), 1);
}

/* A card profile joins its maker's models; one of a maker the set does
 * not have adds the maker in its alphabetical place. */
TEST_CASE(a_card_profile_joins_its_maker)
{
    fresh();
    esc_profiles_clear_overrides();
    esc_profile_t card = *esc_profiles_find("kontronik-jazz");
    card.id = "card-jazz";
    CHECK(esc_profiles_override(&card, NULL));
    esc_profile_t other = *esc_profiles_find("sunrise-pro");
    other.id = "card-other";
    other.brand = "Bench Test";
    CHECK(esc_profiles_override(&other, NULL));
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    CHECK_EQ(listed(), 21);
    CHECK_STR_EQ(esc_stick_panel_maker_at(1), "Bench Test");
    open_maker("Kontronik");
    unsigned models = 0;
    bool found = false;
    for (size_t i = 0; i < esc_profiles_builtin_count; ++i) {
        const esc_profile_t *p = &esc_profiles_builtin[i];
        models += same_name(p->brand, "Kontronik") ? p->model_count : 0u;
    }
    CHECK_EQ(listed(), (int)(models + card.model_count));
    for (int i = 0; i < listed(); ++i) {
        const esc_profile_t *p = esc_stick_panel_row(i, NULL);
        found = found || (p != NULL && strcmp(p->id, "card-jazz") == 0);
    }
    CHECK(found);
    draws();
    esc_profiles_clear_overrides();
}

/* A maker at ESC_MAKER_MODELS_MAX models, most from the card: every one
 * has its row, and the rows are the count. */
TEST_CASE(a_maker_at_its_most_models_lists_every_one)
{
    static esc_model_t many[ESC_MAKER_MODELS_MAX];
    static char names[ESC_MAKER_MODELS_MAX][8];
    for (unsigned i = 0; i < ESC_MAKER_MODELS_MAX; ++i) {
        (void)snprintf(names[i], sizeof(names[i]), "M%03u", i);
        many[i] = (esc_model_t){ names[i], 2u, 3u, false, 12600u,
                                 (uint16_t)(i + 1u), 0u };
    }
    fresh();
    esc_profiles_clear_overrides();
    unsigned have = 0;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        if (esc_brand_same(esc_profiles_at(i)->brand, "Kontronik")) {
            have += esc_profiles_at(i)->model_count;
        }
    }
    esc_profile_t p = *esc_profiles_find("kontronik-jazz");
    p.id = "card-many";
    p.models = many;
    p.model_count = (uint16_t)(ESC_MAKER_MODELS_MAX - have);
    CHECK(esc_profiles_override(&p, NULL));
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Kontronik");
    CHECK_EQ(listed(), (int)ESC_MAKER_MODELS_MAX);
    int m = -1;
    CHECK(esc_stick_panel_row((int)ESC_MAKER_MODELS_MAX - 1, &m)
          != NULL);
    CHECK(esc_stick_panel_row((int)ESC_MAKER_MODELS_MAX, &m) == NULL);
    draws();
    for (int i = 0; i < 60; ++i) {
        tap(LIST_DN_X, LIST_CY);             /* to the last page */
    }
    draws();
    esc_profiles_clear_overrides();
}

int main(void)
{
    RUN(makers_are_alphabetical_and_count_their_models);
    RUN(models_go_by_current_then_voltage_then_name);
    RUN(a_model_row_opens_its_family_at_its_own_voltage);
    RUN(the_search_finds_on_both_levels_and_back_keeps_it);
    RUN(a_card_profile_joins_its_maker);
    RUN(a_maker_at_its_most_models_lists_every_one);
    return test_summary("esc_stick_panel_makers");
}
