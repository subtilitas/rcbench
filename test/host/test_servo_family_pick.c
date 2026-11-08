/**
 * @file test_servo_family_pick.c
 * @brief The programmer each run writes with (servo_pick.c): LIMIT RUN's
 *        and RUDDER AUTO's SERVO, picked on the READY card's SERVO row
 *        from the choice list over the left column, each screen's own and
 *        saved.  A pick survives a restart and a visit to the KST and HITEC
 *        tiles; a pick on one screen leaves the other's; the HITEC page's
 *        series moves neither; a pick is refused ARMED and off a
 *        programming port.  The pick reaching a run, and the refusal while
 *        a run holds the screen: test_servo_limit_hitec.c and
 *        test_servo_rauto_hitec.c.
 *
 * Cases:
 * - the_family_of_each_screen_survives_a_restart_and_the_programmer_tiles
 * - a_pick_on_one_screen_leaves_the_others
 * - the_hitec_pages_series_moves_no_screens_family
 * - a_pick_is_refused_armed_and_off_a_programming_port
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "hitec_panel.h"
#include "kst_panel.h"
#include "servo_limrun.h"
#include "servo_ready_int.h"
#include "servo_rudder_auto.h"
#include "ui_text.h"

/* A store in memory: what a save wrote, a restart loads. */
static float s_kept[SETTING_COUNT];
static bool  s_have_kept;

/** @brief The store's load. @param values takes them @param count their
 *  count @return whether a save was made before */
static bool kept_load(float *values, int count)
{
    if (!s_have_kept) {
        return false;
    }
    memcpy(values, s_kept, (size_t)count * sizeof(float));
    return true;
}

/** @brief The store's save. @param values the values @param count their
 *  count @return true */
static bool kept_save(const float *values, int count)
{
    memcpy(s_kept, values, (size_t)count * sizeof(float));
    s_have_kept = true;
    return true;
}

/** @brief The store in memory. */
static const settings_store_t k_kept = { kept_load, kept_save, NULL, NULL };

/** @brief A SAFE bench on function @p fn, the tester on PROG PORT 1.
 *  @param fn the function */
static void on_port(servo_fn_t fn)
{
    fresh();
    scr->enter();
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, true);
    go(fn);
    ui_router_set_alert(NULL);
}

/** @brief The card's SERVO row on function @p fn tapped and entry @p k of
 *  the list it opens picked.  @param fn LIMIT RUN or RUDDER AUTO
 *  @param k the family */
static void card_pick(servo_fn_t fn, int k)
{
    go(fn);
    tap(SV_RDY_X, SV_RDY_ROW_Y(fn == SERVO_FN_RAUTO ? (int)RAR_SERVO
                                                    : (int)LRR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), (int)SERVO_LIMRUN_FAM_COUNT);
    ov_choose(k);
    CHECK_EQ(servo_screen_family_choices(), -1);
}

/** @brief A few frames of the screen shown. */
static void settle_frames(void)
{
    for (int i = 0; i < 5; ++i) {
        scr->tick(0.02f);
    }
}

/* Each pick is saved on its own key; a restart loads them back, and the
 * KST and HITEC tiles, opened and left, change neither. */
TEST_CASE(the_family_of_each_screen_survives_a_restart_and_the_programmer_tiles)
{
    s_have_kept = false;
    on_port(SERVO_FN_LIMRUN);
    settings_set_store(&k_kept);
    card_pick(SERVO_FN_LIMRUN, SERVO_LIMRUN_FAM_HITEC_C);
    card_pick(SERVO_FN_RAUTO, SERVO_RAUTO_FAM_HITEC_D);
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    CHECK_EQ((int)s_kept[SET_LIMIT_FAMILY], (int)SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ((int)s_kept[SET_RAUTO_FAMILY], (int)SERVO_RAUTO_FAM_HITEC_D);

    /* The restart: the settings loaded from the store, the screen reset. */
    settings_init();
    scr->reset();
    scr->enter();
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);

    /* The programmers' tiles, opened and left. */
    go(SERVO_FN_KST);
    settle_frames();
    CHECK(kst_panel_is_open());
    go(SERVO_FN_HITEC);
    settle_frames();
    CHECK(hitec_panel_is_open());
    go(SERVO_FN_LIMRUN);
    settle_frames();
    go(SERVO_FN_RAUTO);
    settle_frames();
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);
    settings_set_store(NULL);
}

/* RUDDER AUTO's pick leaves LIMIT RUN's, and the other way round; each
 * card reads its own. */
TEST_CASE(a_pick_on_one_screen_leaves_the_others)
{
    on_port(SERVO_FN_RAUTO);
    card_pick(SERVO_FN_RAUTO, SERVO_RAUTO_FAM_HITEC_D);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    card_pick(SERVO_FN_LIMRUN, SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);

    rdy_row_t r;
    go(SERVO_FN_LIMRUN);
    CHECK(lim_ready_row(LRR_SERVO, &r));
    CHECK_STR_EQ(r.value, "HITEC CLASSIC");
    go(SERVO_FN_RAUTO);
    CHECK(rauto_ready_row(RAR_SERVO, &r));
    CHECK_STR_EQ(r.value, "HITEC D SERIES");
    /* German: the series in German, HITEC as it is. */
    ui_text_set_language(UI_LANG_DE);
    CHECK(rauto_ready_row(RAR_SERVO, &r));
    CHECK_STR_EQ(r.value, "HITEC D-SERIE");
    ui_text_set_language(UI_LANG_EN);
    settings_cancel_save();
}

/* D SERIES or CLASSIC picked on a card stays the screen's family, whatever
 * the HITEC page's series reads. */
TEST_CASE(the_hitec_pages_series_moves_no_screens_family)
{
    on_port(SERVO_FN_LIMRUN);
    settings_set(SET_HITEC_SERIES, 1.0f);              /* D SERIES */
    card_pick(SERVO_FN_LIMRUN, SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_C);
    settings_set(SET_HITEC_SERIES, 0.0f);              /* CLASSIC */
    card_pick(SERVO_FN_RAUTO, SERVO_RAUTO_FAM_HITEC_D);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_C);
    settings_set(SET_HITEC_SERIES, 1.0f);
    settings_cancel_save();
}

/* ARMED, the card's SERVO opens no list; off a PROG PORT neither, and the
 * reason names the port.  The family stays. */
TEST_CASE(a_pick_is_refused_armed_and_off_a_programming_port)
{
    on_port(SERVO_FN_LIMRUN);
    servo_screen_set_armed(true);
    scr->tick(0.02f);
    CHECK_STR_EQ(servo_screen_family_refused(), TR(FOOT_SETTINGS_SAFE));
    tap(SV_RDY_X, SV_RDY_ROW_Y(LRR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    go(SERVO_FN_RAUTO);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    servo_screen_set_armed(false);
    scr->tick(0.02f);
    CHECK(servo_screen_family_refused() == NULL);

    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    CHECK_STR_EQ(servo_screen_family_refused(), TR(SVL_PORT_ONLY));
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    go(SERVO_FN_LIMRUN);
    tap(SV_RDY_X, SV_RDY_ROW_Y(LRR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_KST);
}

int main(void)
{
    RUN(the_family_of_each_screen_survives_a_restart_and_the_programmer_tiles);
    RUN(a_pick_on_one_screen_leaves_the_others);
    RUN(the_hitec_pages_series_moves_no_screens_family);
    RUN(a_pick_is_refused_armed_and_off_a_programming_port);
    return test_summary("servo_family_pick");
}
