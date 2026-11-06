/**
 * @file test_esc_stick_panel_steps.c
 * @brief ESC STICK's stack light and manual steps: the green light, the prompt
 * for a manual step, DONE, and what the supply does until it.
 *
 * Cases:
 * - a_result_of_many_changes_counts_the_rest
 * - the_green_light_follows_the_beeps
 * - manual_steps_show_once_by_themselves_and_on_their_button
 * - a_row_that_does_not_run_shows_its_manual_steps
 * - a_run_asks_for_its_manual_step_and_goes_on_with_done
 * - abort_on_the_prompt_ends_the_run
 * - the_supply_stays_on_until_done_after_the_store
 * - a_manual_step_shows_in_the_language_showing
 * - a_family_shows_in_the_language_showing
 * - a_crumb_never_cuts_inside_a_word_of_the_family
 * - an_unsure_selection_is_not_counted_as_made
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* ------------------------------------------------------- the stack light */

/* Green lights with the detector's beeps during a run, both ways. */
TEST_CASE(the_green_light_follows_the_beeps)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    unsigned on = 0u, off = 0u, rises = 0u;
    bool was = false, red = true;
    for (int i = 0; i < 240000 && esc_stick_running(esc_stick_panel_stick());
         ++i) {
        rig_step(&r);
        bool g = false;
        esc_stick_panel_lights(&red, &g);
        CHECK(!red);
        if (esc_stick_panel_stick()->det.high) {
            CHECK(g);                        /* never dark in a beep */
        }
        on += g ? 1u : 0u;
        off += g ? 0u : 1u;
        rises += (g && !was) ? 1u : 0u;
        was = g;
        if ((i & 4095) == 0) {
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, i & 1);
        }
    }
    rig_run(&r, 10u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);
    CHECK(on > 1000u && off > 1000u);
    CHECK(rises > 5u);
    bool g = true;
    esc_stick_panel_lights(&red, &g);
    CHECK(!red && !g);                       /* DONE: both dark */
}

/* Past five changes the result says how many more there were. */
TEST_CASE(a_result_of_many_changes_counts_the_rest)
{
    fresh();
    /* hobbywing-flyfun-v5: twelve items. */
    open_profile("hobbywing-flyfun-v5");
    for (int i = 0; i < ST_PARM_ROWS; ++i) {
        tap(STEP_UP_X, STEP_CY(i));          /* items 1 to 6 */
    }
    tap(PAGE_DN_X, PAGE_CY);
    tap(STEP_UP_X, STEP_CY(ST_PARM_ROWS - 1)); /* item 7 */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    CHECK_EQ(esc_stick_panel_stick()->n, 7);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* The steps show by themselves the first time the profile opens, under
 * its red button every time, and cover the page while they show. */
TEST_CASE(manual_steps_show_once_by_themselves_and_on_their_button)
{
    fresh();
    descend_to_jazz();
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL);
    if (p == NULL) {
        return;
    }
    CHECK_STR_EQ(p->id, "kontronik-jazz");
    CHECK(esc_stick_panel_hand_shown());
    draws();
    tap(WRITE_X, BTN_CY);                    /* RUN's place: covered */
    tap(BACK_X, BACK_Y);
    CHECK(esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_page() == p);
    tap(CANCEL_X, HOLD_Y);                   /* OK */
    CHECK(!esc_stick_panel_hand_shown());
    draws();

    /* Opened again, it stays closed; the button opens it. */
    tap(BACK_X, BACK_Y);
    open_model("kontronik-jazz", NULL);
    CHECK(esc_stick_panel_page() == p);
    CHECK(!esc_stick_panel_hand_shown());
    tap(HAND_X, HAND_Y);
    CHECK(esc_stick_panel_hand_shown());
    tap(CANCEL_X, HOLD_Y);
    CHECK(!esc_stick_panel_hand_shown());

    /* A profile without steps has no button and no pop-up. */
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);                     /* the makers */
    open_maker("Hobbywing");
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK_STR_EQ(esc_stick_panel_page()->id,
                 "hobbywing-flyfun-8item");
    CHECK(!esc_stick_panel_hand_shown());
    tap(HAND_X, HAND_Y);
    CHECK(!esc_stick_panel_hand_shown());

    /* Leaving the screen closes it. */
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);
    open_maker("Kontronik");
    open_model("kontronik-jazz", NULL);
    tap(HAND_X, HAND_Y);
    CHECK(esc_stick_panel_hand_shown());
    scr->leave();
    CHECK(!esc_stick_panel_hand_shown());
}

/* A row the bench does not run opens nothing, unless it has manual steps:
 * then they show, over the list, with why it does not run. */
TEST_CASE(a_row_that_does_not_run_shows_its_manual_steps)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    tap(FIND_X, FIND_Y);
    find_type("KOSMIK\n");
    CHECK_EQ(listed(), 1);                   /* one maker */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK_EQ(listed(), 12);                  /* its KOSMIK models */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(esc_stick_panel_page() == NULL);
    CHECK(esc_stick_panel_hand_shown());
    draws();
    tap(CANCEL_X, HOLD_Y);
    CHECK(!esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_page() == NULL);

    /* With the keyboard open, the tap closes it first. */
    tap(FIND_X, FIND_Y);
    CHECK(esc_stick_panel_typing());
    tap(100, SP_ROW_CY(0));
    CHECK(!esc_stick_panel_typing());
    CHECK(esc_stick_panel_hand_shown());
    tap(CANCEL_X, HOLD_Y);

    /* graupner-brushless-control-t: a person reads its LEDs; no steps. */
    tap(FIND_CLR_X, FIND_Y);
    tap(BACK_X, BACK_Y);                     /* the makers */
    open_maker("Graupner");
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(!esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_page() == NULL);
}

/* A whole JAZZ run: the warning holds the jumper step, the run stops after
 * the entry with a prompt, DONE counts only after a second, and the mode
 * is stored. */
TEST_CASE(a_run_asks_for_its_manual_step_and_goes_on_with_done)
{
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* KEEP, 1, 2, 3 */
    }
    supply_reads_off();                      /* the jumper on */
    tap(WRITE_X, BTN_CY);
    draws();                                 /* the warning, with its step */
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);                   /* asked, and listening */
    CHECK(r.on);
    CHECK(r.armed);
    CHECK(r.pct == 0.0f);
    draws();                                 /* the prompt */
    tap(HOLD_X, HOLD_Y);                     /* DONE, too soon */
    for (int i = 0; i < 5; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);
    tap(WRITE_X, BTN_CY);                    /* ABORT's place: covered */
    CHECK(esc_stick_running(run));
    for (uint32_t i = 0; i < ESC_STICK_HAND_MIN_MS; ++i) {
        rig_step(&r);
    }
    draws();
    esc_sim_hand(&r.sim, r.now);             /* the jumper, pulled */
    tap(HOLD_X, HOLD_Y);                     /* DONE */
    rig_step(&r);
    CHECK(!run->hand_menu);
    CHECK_EQ(run->phase, ESC_STICK_VALUES);
    rig_run(&r, 300000u);
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK(!r.armed);
    CHECK(!r.on);
    draws();
}

/* A step shows in German where the profile gives it and German shows,
 * and in English otherwise; every step of record has its German. */
TEST_CASE(a_manual_step_shows_in_the_language_showing)
{
    const esc_profile_t *p = esc_profiles_find("kontronik-jazz");
    CHECK(p != NULL && p->manual_count == 3u);
    if (p == NULL || p->manual_count < 2u) {
        return;
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(esc_stick_panel_step_text(&p->manual[1]),
                 p->manual[1].action);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(esc_stick_panel_step_text(&p->manual[1]),
                 "Nach 2s oder der Tonfolge: JAZZ: Ziehe den Jumper ab. "
                 "MINIJAZZ: Dr\xC3\xBC" "cke den Taster.");
    /* No German in the step: the English. */
    esc_manual_t m = p->manual[0];
    m.action_de = "";
    CHECK_STR_EQ(esc_stick_panel_step_text(&m), m.action);
    m.action_de = NULL;
    CHECK_STR_EQ(esc_stick_panel_step_text(&m), m.action);
    CHECK_STR_EQ(esc_stick_panel_step_text(NULL), "");
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *q = esc_profiles_at(i);
        for (unsigned k = 0; k < q->manual_count; ++k) {
            if (q->manual[k].action_de == NULL
                || q->manual[k].action_de[0] == '\0') {
                T_FAIL("%s step %u has no German", q->id, k + 1u);
            }
        }
    }
    /* Drawn in German: the pop-up of a profile opened for the first
     * time. */
    fresh();
    ui_text_set_language(UI_LANG_DE);
    descend_to_jazz();
    CHECK(esc_stick_panel_hand_shown());
    draws();
    ui_text_set_language(UI_LANG_EN);
}

/* The pixels of the crumb's text, right of BACK and left of TIMING, as
 * one number. */
static uint32_t crumb_look(void)
{
    uint32_t h = 2166136261u;
    stick_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    for (int y = ST_CRUMB_CY - 22; y < ST_CRUMB_CY + 22; ++y) {
        for (int x = 170; x < 820; ++x) {
            h = (h ^ (uint32_t)fb[y * W + x]) * 16777619u;
        }
    }
    return h;
}

/* A family shows in German where the profile gives family_de and German
 * shows, and in English otherwise; the page's crumb drawn changes with the
 * language for a profile with a family_de and does not for one without. */
TEST_CASE(a_family_shows_in_the_language_showing)
{
    const esc_profile_t *p = esc_profiles_find("jeti-spin-3p");
    CHECK(p != NULL && p->family_de != NULL);
    if (p == NULL || p->family_de == NULL) {
        return;
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(esc_stick_panel_family_text(p), "Spin / Master / Advance / JES 3P series");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(esc_stick_panel_family_text(p), "Serie Spin / Master / Advance / JES 3P");
    /* No German in the profile: the English. */
    esc_profile_t q = *p;
    q.family_de = "";
    CHECK_STR_EQ(esc_stick_panel_family_text(&q), q.family);
    q.family_de = NULL;
    CHECK_STR_EQ(esc_stick_panel_family_text(&q), q.family);
    CHECK_STR_EQ(esc_stick_panel_family_text(NULL), "");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(esc_stick_panel_family_text(p), p->family);

    /* The names-only families carry none, and read the same in both. */
    const esc_profile_t *n = esc_profiles_find("hobbywing-platinum-v3");
    CHECK(n != NULL && (n->family_de == NULL || n->family_de[0] == '\0'));
    ui_text_set_language(UI_LANG_DE);
    if (n != NULL) {
        CHECK_STR_EQ(esc_stick_panel_family_text(n), n->family);
    }

    /* Drawn: the crumb of the page. */
    fresh();
    ui_text_set_language(UI_LANG_EN);
    open_profile("jeti-spin-3p");
    const uint32_t en = crumb_look();
    ui_text_set_language(UI_LANG_DE);
    const uint32_t de = crumb_look();
    CHECK(en != de);
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(crumb_look(), en);
    fresh();
    open_profile("hobbywing-platinum-v3");
    const uint32_t n_en = crumb_look();
    ui_text_set_language(UI_LANG_DE);
    CHECK_EQ(crumb_look(), n_en);
    ui_text_set_language(UI_LANG_EN);
}

/*
 * @p crumb against the rule of esc_stick_panel_crumb(): at most @p cells;
 * the brand and model, then the family whole, its whole words and " ..",
 * or none.  Names what is wrong, NULL for a crumb that keeps the rule.
 */
static const char *crumb_wrong(const esc_profile_t *p, int model, int cells,
                               const char *crumb)
{
    char head[160];
    const char *fam = esc_stick_panel_family_text(p);
    const bool named = model >= 0;

    if (gfx_text_cells(crumb) > cells) {
        return "wider than its room";
    }
    if (named) {
        snprintf(head, sizeof(head), "ESC STICK  >  %s  >  %s", p->brand,
                 p->models[model].name);
    } else {
        snprintf(head, sizeof(head), "ESC STICK  >  %s", p->brand);
    }
    if (gfx_text_cells(head) > cells) {
        /* The name cut: no family after it. */
        const size_t at = gfx_text_prefix(head, cells - 2);
        return (strlen(crumb) == at + 2u && strncmp(crumb, head, at) == 0
                && strcmp(crumb + at, "..") == 0)
                   ? NULL : "a cut name with more after it";
    }
    const size_t hn = strlen(head);
    if (strncmp(crumb, head, hn) != 0) {
        return "no brand and model at its start";
    }
    const char *rest = crumb + hn;
    if (*rest == '\0') {
        return NULL;
    }
    const char *open = named ? "  (" : " ";
    const char *close = named ? ")" : "";
    if (strncmp(rest, open, strlen(open)) != 0) {
        return "no family opening after the model";
    }
    rest += strlen(open);
    const size_t rn = strlen(rest);
    if (rn < strlen(close) || strcmp(rest + rn - strlen(close), close) != 0) {
        return "no family closing";
    }
    const size_t body = rn - strlen(close);
    if (body == strlen(fam) && strncmp(rest, fam, body) == 0) {
        return NULL;
    }
    /* The whole family fits: it shows whole. */
    if (gfx_text_cells(head) + gfx_text_cells(open) + gfx_text_cells(fam)
            + gfx_text_cells(close) <= cells) {
        return "a family cut that fits whole";
    }
    /* Part of the family: whole words of it, then " ..". */
    if (body < 4u || strncmp(rest + body - 3u, " ..", 3) != 0) {
        return "part of the family without \" ..\"";
    }
    const size_t kept = body - 3u;
    if (strncmp(rest, fam, kept) != 0) {
        return "a family that is not the profile's";
    }
    const char next = fam[kept];
    const bool mark = next == ',' || next == ';' || next == ':';
    if (next != ' '
        && !(mark && (fam[kept + 1u] == ' ' || fam[kept + 1u] == '\0'))) {
        return "a family cut inside a word";
    }
    /* As many words as fit: the next one with " .." does not. */
    size_t more = kept + 1u;
    while (fam[more] == ' ') {
        ++more;
    }
    while (fam[more] != '\0' && fam[more] != ' ') {
        ++more;
    }
    size_t end = more;
    while (end > kept && (fam[end - 1u] == ',' || fam[end - 1u] == ';'
                          || fam[end - 1u] == ':')) {
        --end;
    }
    char longer[400];
    snprintf(longer, sizeof(longer), "%s%s%.*s ..%s", head, open, (int)end,
             fam, close);
    if (fam[more] != '\0' && end > kept && gfx_text_cells(longer) <= cells) {
        return "fewer words than fit";
    }
    return NULL;
}

/*
 * The 0.23.9 walk found the German crumb of XC-12-Lite cut inside its
 * family ("XController Lit..").  Every profile, with each model and with
 * none, in both languages, at every room from 20 to 80 cells: the family
 * shows whole, as whole words and " ..", or not at all, and the crumb fits.
 */
TEST_CASE(a_crumb_never_cuts_inside_a_word_of_the_family)
{
    char crumb[160];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < esc_profiles_count(); ++i) {
            const esc_profile_t *p = esc_profiles_at(i);
            for (int m = -1; m < (int)p->model_count; ++m) {
                for (int cells = 20; cells <= 80; ++cells) {
                    esc_stick_panel_crumb(p, m, cells, crumb, sizeof(crumb));
                    const char *why = crumb_wrong(p, m, cells, crumb);
                    if (why != NULL) {
                        T_FAIL("%s model %d, language %d, %d cells: %s: "
                               "\"%s\"", p->id, m, lang, cells, why, crumb);
                        ui_text_set_language(UI_LANG_EN);
                        return;
                    }
                }
            }
        }
    }
    /* The walk's crumb, at the profile page's 56 cells beside TIMING. */
    const esc_profile_t *p = esc_profiles_find("dualsky-xcontroller-lite");
    CHECK(p != NULL && p->model_count > 0u);
    if (p == NULL || p->model_count == 0u) {
        ui_text_set_language(UI_LANG_EN);
        return;
    }
    ui_text_set_language(UI_LANG_DE);
    esc_stick_panel_crumb(p, 0, 56, crumb, sizeof(crumb));
    CHECK(strstr(crumb, "Lit..") == NULL);
    CHECK(strstr(crumb, "  (XController") != NULL);
    /* Room for all of it: the whole family. */
    esc_stick_panel_crumb(p, 0, 200, crumb, sizeof(crumb));
    CHECK(strstr(crumb, esc_stick_panel_family_text(p)) != NULL);
    /* Too little room for the family's first word: none of it. */
    esc_stick_panel_crumb(p, 0, 42, crumb, sizeof(crumb));
    CHECK(strchr(crumb, '(') == NULL);
    /* No profile, no buffer: nothing, and nothing written. */
    esc_stick_panel_crumb(NULL, 0, 56, crumb, sizeof(crumb));
    CHECK_STR_EQ(crumb, "");
    esc_stick_panel_crumb(p, 0, 56, NULL, 0u);
    crumb[0] = 'x';
    esc_stick_panel_crumb(p, 0, 56, crumb, 1u);
    CHECK_STR_EQ(crumb, "");
    /* A model index past the last is no model. */
    char none[160];
    esc_stick_panel_crumb(p, -1, 200, none, sizeof(none));
    esc_stick_panel_crumb(p, (int)p->model_count, 200, crumb, sizeof(crumb));
    CHECK_STR_EQ(crumb, none);
    /* A buffer one byte short of the whole crumb: the name alone, and not
     * a byte past the buffer. */
    char whole[160];
    esc_stick_panel_crumb(p, 0, 200, whole, sizeof(whole));
    char head[160];
    snprintf(head, sizeof(head), "ESC STICK  >  %s  >  %s", p->brand,
             p->models[0].name);
    memset(crumb, '#', sizeof(crumb));
    esc_stick_panel_crumb(p, 0, 200, crumb, strlen(whole));
    CHECK_STR_EQ(crumb, head);
    CHECK_EQ(crumb[strlen(whole)], '#');
    esc_stick_panel_crumb(p, 0, 200, crumb, strlen(whole) + 1u);
    CHECK_STR_EQ(crumb, whole);
    ui_text_set_language(UI_LANG_EN);
}

/* KONTROL-X mode 2 to the step before the supply goes off: the button
 * pressed when asked, the ESC powered and the stick at MAX after the
 * store. */
static const esc_stick_t *run_to_end_step(rig_t *r)
{
    fresh();
    open_profile("kontronik-kontrol-x");
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    tap(STEP_UP_X, STEP_CY(0));              /* KEEP, 1 */
    tap(STEP_UP_X, STEP_CY(0));              /* 2 */
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    rig_start(r);
    r->no_watch = true;
    const esc_stick_t *run = esc_stick_panel_stick();
    bool pressed = false;
    for (uint32_t i = 0; i < 240000u && esc_stick_running(run)
                         && run->phase != ESC_STICK_HAND_END; ++i) {
        if (run->hand_menu && !pressed) {
            esc_sim_hand(&r->sim, r->now);   /* the button */
            pressed = true;
        }
        rig_step(r);
    }
    CHECK_EQ(run->phase, ESC_STICK_HAND_END);
    return run;
}

/* The run holds the ESC powered after the store until DONE, with the
 * prompt up; DONE switches the supply off.  No DONE: the run ends with
 * NOT CONFIRMED and says the ESC may be locked. */
TEST_CASE(the_supply_stays_on_until_done_after_the_store)
{
    static rig_t r;
    const esc_stick_t *run = run_to_end_step(&r);
    CHECK(r.on);
    CHECK(r.armed);
    CHECK(r.pct == ESC_STICK_PCT_MAX);
    CHECK(esc_stick_panel_hand_shown() == false);
    CHECK_STR_EQ(esc_stick_panel_left_text(),
                 "Without DONE within %us, the run ends and power switches "
                 "off. The ESC may lock itself.");
    draws();                                 /* the prompt */
    for (uint32_t i = 0; i < ESC_STICK_HAND_MIN_MS; ++i) {
        rig_step(&r);
    }
    CHECK(r.on);
    tap(HOLD_X, HOLD_Y);                     /* DONE */
    rig_step(&r);
    CHECK_EQ(run->phase, ESC_STICK_OFF);
    rig_run(&r, 60000u);
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK(!esc_stick_lock_risk(run));
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
    CHECK(!r.on);
    draws();

    run = run_to_end_step(&r);
    rig_run(&r, ESC_STICK_HAND_WAIT_MS + 1000u);
    CHECK_EQ(run->phase, ESC_STICK_ABORTED);
    CHECK_EQ(run->reason, ESC_STICK_R_HAND);
    CHECK(esc_stick_lock_risk(run));
    CHECK(!r.on);
    CHECK(!r.armed);
    draws();                                 /* the result, with the lock */
    ui_text_set_language(UI_LANG_DE);
    draws();
    ui_text_set_language(UI_LANG_EN);

    /* JAZZ repeats its mode with no lock named: the prompt and the result
     * say the mode may not be stored. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* KEEP, 1, 2, 3 */
    }
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    rig_start(&r);
    r.no_watch = true;
    run = esc_stick_panel_stick();
    bool pulled = false;
    for (uint32_t i = 0; i < 240000u && esc_stick_running(run)
                         && run->phase != ESC_STICK_HAND_END; ++i) {
        if (run->hand_menu && !pulled) {
            esc_sim_hand(&r.sim, r.now);     /* the jumper */
            pulled = true;
        }
        rig_step(&r);
    }
    CHECK_EQ(run->phase, ESC_STICK_HAND_END);
    CHECK(esc_stick_hand(run) != NULL && !esc_stick_hand(run)->locks);
    CHECK_STR_EQ(esc_stick_panel_left_text(),
                 "Without DONE within %us, the run ends and power switches "
                 "off. The mode may not be saved.");
    draws();                                 /* the prompt */
    rig_run(&r, ESC_STICK_HAND_WAIT_MS + 1000u);
    CHECK_EQ(run->reason, ESC_STICK_R_HAND);
    CHECK(esc_stick_cut_short(run));
    CHECK(!esc_stick_lock_risk(run));
    draws();                                 /* the result */
    ui_text_set_language(UI_LANG_DE);
    draws();
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A selection cut short while the ESC confirms it is not counted as made:
 * six changes on a two-stage card profile with a step before the
 * power-off, aborted while that step is asked: 5 of 6 made.  One being
 * confirmed past the four rows shown is named unsure on the summary line.
 * A 1-of-1 run cut the same way says 0 of 1.
 */
TEST_CASE(an_unsure_selection_is_not_counted_as_made)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_POWER_OFF, "Wait for the confirmation tones.",
          0u, NULL, false, false },
    };
    fresh();
    esc_profiles_clear_overrides();
    esc_profile_t card = *esc_profiles_find("hobbywing-flyfun-8item");
    card.id = "card-confirm";
    card.automatable = ESC_AUTO_ASSISTED;
    card.automatable_note = "Confirmation before power-off.";
    card.manual = k;
    card.manual_count = 1;
    CHECK(esc_profiles_override(&card, NULL));
    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        open_profile("card-confirm");
        tap(CANCEL_X, HOLD_Y);               /* the first opening */
        const int picks = (pass == 0) ? 6 : 1;
        for (int i = 0; i < picks; ++i) {
            tap(STEP_UP_X, STEP_CY(i));
        }
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        static rig_t r;
        rig_start(&r);
        r.no_watch = true;
        const esc_stick_t *run = esc_stick_panel_stick();
        CHECK_EQ(run->n, (uint8_t)picks);
        for (uint32_t i = 0; i < 900000u && esc_stick_running(run)
                             && run->phase != ESC_STICK_HAND_END; ++i) {
            rig_step(&r);
        }
        CHECK_EQ(run->phase, ESC_STICK_HAND_END);
        CHECK_EQ(esc_stick_done_count(run), (unsigned)picks);
        tap(CANCEL_X, HOLD_Y);               /* ABORT on the prompt */
        rig_run(&r, 1000u);
        CHECK(esc_stick_cut_short(run));
        CHECK_EQ(esc_stick_made_count(run), (unsigned)picks - 1u);
        if (pass == 0) {
            CHECK_STR_EQ(esc_stick_panel_result_head(),
                         "5 of 6 selections completed.");
            /* The ESC's loop decides which selection comes last; here the
             * one being confirmed is shown on a row, and the line for the
             * two past the fourth counts both made. */
            CHECK(run->active < 4u && esc_stick_unsure(run, run->active));
            CHECK_STR_EQ(esc_stick_panel_result_more(),
                         "AND 2 MORE, 2 COMPLETED");
            /* The same end with the one being confirmed past the fourth
             * row: the line names it unsure (the run's state moved to
             * that change, as a loop that sounds it last leaves it). */
            /* NOLINTNEXTLINE(performance-no-int-to-ptr): drops const to move the state under test */
            esc_stick_t *moved = (esc_stick_t *)(uintptr_t)run;
            moved->active = 5u;
            CHECK(esc_stick_unsure(run, 5u));
            CHECK_STR_EQ(esc_stick_panel_result_head(),
                         "5 of 6 selections completed.");
            CHECK_STR_EQ(esc_stick_panel_result_more(),
                         "AND 2 MORE, 1 COMPLETED, 1 UNCERTAIN");
            ui_text_set_language(UI_LANG_DE);
            CHECK_STR_EQ(esc_stick_panel_result_more(),
                         "UND 2 WEITERE, 1 AUSGEFÜHRT, 1 UNKLAR");
            draws();
            ui_text_set_language(UI_LANG_EN);
        } else {
            CHECK_STR_EQ(esc_stick_panel_result_head(),
                         "0 of 1 selections completed.");
            CHECK_STR_EQ(esc_stick_panel_result_more(), "");
        }
        draws();
        scr->leave();
    }
    esc_profiles_clear_overrides();
}

/* ABORT on the prompt ends the run as ABORT does: disarmed, supply off. */
TEST_CASE(abort_on_the_prompt_ends_the_run)
{
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));
    }
    supply_reads_off();                      /* the jumper on */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);
    tap(CANCEL_X, HOLD_Y);                   /* ABORT */
    rig_run(&r, 1000u);
    CHECK_EQ(run->phase, ESC_STICK_ABORTED);
    CHECK_EQ(run->reason, ESC_STICK_R_USER);
    CHECK(!r.armed);
    CHECK(!r.on);
    draws();                                 /* the result */
}

int main(void)
{
    RUN(a_result_of_many_changes_counts_the_rest);
    RUN(the_green_light_follows_the_beeps);
    RUN(manual_steps_show_once_by_themselves_and_on_their_button);
    RUN(a_row_that_does_not_run_shows_its_manual_steps);
    RUN(a_run_asks_for_its_manual_step_and_goes_on_with_done);
    RUN(abort_on_the_prompt_ends_the_run);
    RUN(the_supply_stays_on_until_done_after_the_store);
    RUN(a_manual_step_shows_in_the_language_showing);
    RUN(a_family_shows_in_the_language_showing);
    RUN(a_crumb_never_cuts_inside_a_word_of_the_family);
    RUN(an_unsure_selection_is_not_counted_as_made);
    return test_summary("esc_stick_panel_steps");
}
