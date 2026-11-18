/*
 * The M5Stack Dial's 1s hold while RUDDER AUTO or a limit run is under way
 * on the SERVO screen (servo_screen_dial_hold(), servo_drive_ready.c): the
 * hold ends the run in every phase, the phases in which the run itself
 * holds the bench SAFE for its programmer session included, and the run
 * arms nothing afterwards.  The rig is servo_rauto_kst_rig.h: a KST servo
 * on the programming port behind a PD mini.
 *
 * Cases:
 * - a_dial_hold_ends_auto_in_every_phase
 * - a_dial_hold_ends_a_limit_run_in_every_phase
 * - a_dial_hold_takes_back_an_arm_on_its_way
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "dial_panel.h"
#include "ui_setlist.h"

/** @brief LIMIT RUN's list: the MODE row. */
#define LR_MODE   0
/** @brief LIMIT RUN's list: the SERVO PULSES row (servo_lists.c). */
#define LR_PULSES 2

/** @brief The phase asked of to_phase(): SAFE with the run's own DISARM
 *         posted and not yet taken by the bench. */
#define PH_SAFE_ASKED (-1)

/**
 * @brief Run the rig until the run is in @p want.
 *
 * For PH_SAFE_ASKED the frames run until the run's SAFE phase posts
 * DISARM; the command is taken here and not acted on, as a bench that has
 * not switched yet: the gate still reads ARMED.
 *
 * @param running whether the run is under way
 * @param phase   the run's phase
 * @param safe    the run's SAFE phase
 * @param want    the phase to reach, or PH_SAFE_ASKED
 * @return true when the run is there
 */
static bool to_phase(bool (*running)(void), int (*phase)(void), int safe,
                     int want)
{
    for (int i = 0; i < 4000000 && running(); ++i) {
        ra_ms(1u);
        if (want != PH_SAFE_ASKED) {
            if (phase() == want) {
                return true;
            }
            continue;
        }
        if (phase() != safe || g_rig.ms % 20u != 0u) {
            continue;
        }
        servo_cmd_t c;
        while (servo_screen_take(&c)) {
            if (c.kind == SERVO_CMD_DISARM) {
                return ra.armed;
            }
            if (c.kind == SERVO_CMD_POSITION) {
                ra.cmd = c.value_us;
            } else if (c.kind == SERVO_CMD_RELEASE) {
                ra.cmd = 1500u;
            }
        }
    }
    return false;
}

/**
 * @brief The Dial's HOLD through the frame's path, and what has to hold
 *        after it: the run ended at once as the operator's stop, no ARM in
 *        the 8s that follow, the bench SAFE, the pulse at rest.
 * @param running whether the run is under way
 * @param route   the screen's route
 */
static void hold_ends_it(bool (*running)(void), ui_screen_id_t route)
{
    const unsigned arms = ra.arms;
    const dial_input_t hold = { .holds = 1u };
    CHECK(running());
    CHECK(dial_gestures(route, &hold, false));
    CHECK(!running());
    ra_ms(8000u);
    CHECK(!running());
    CHECK_EQ(ra.arms, arms);
    CHECK(!ra.armed);
    CHECK(!servo_screen_arm_asked());
    CHECK_EQ(ra.cmd, 1500u);
    CHECK(strstr(ra.report, "ABORTED") != NULL);
    servo_screen_cancel_arm();
    ra_ms(3000u);
    CHECK(rig_settle());
}

/* RUDDER AUTO: the read session, a measure, the SAFE asked and not yet
 * read back, and the session of the first end point write with its ARM. */
TEST_CASE(a_dial_hold_ends_auto_in_every_phase)
{
    static const int k_ph[] = {
        SERVO_RAUTO_PH_READ, SERVO_RAUTO_PH_SETTLE, PH_SAFE_ASKED,
        SERVO_RAUTO_PH_CONNECT, SERVO_RAUTO_PH_WRITE, SERVO_RAUTO_PH_LEAVE,
        SERVO_RAUTO_PH_ARM,
    };
    for (size_t k = 0; k < sizeof(k_ph) / sizeof(k_ph[0]); ++k) {
        ra_setup(true, true);
        ra_hold_start();
        CHECK(servo_screen_rauto_running());
        const bool there = to_phase(servo_screen_rauto_running,
                                    servo_screen_rauto_phase,
                                    SERVO_RAUTO_PH_SAFE, k_ph[k]);
        if (!there) {
            printf("  AUTO phase %d not reached\n", k_ph[k]);
        }
        CHECK(there);
        if (k_ph[k] == SERVO_RAUTO_PH_READ
            || k_ph[k] == SERVO_RAUTO_PH_CONNECT
            || k_ph[k] == SERVO_RAUTO_PH_WRITE
            || k_ph[k] == SERVO_RAUTO_PH_LEAVE) {
            ra_ms(40u);
            CHECK(!ra.armed);       /* the run's own SAFE */
            CHECK_EQ(servo_screen_rauto_phase(), k_ph[k]);
        }
        hold_ends_it(servo_screen_rauto_running,
                     servo_fn_screen_id(SERVO_FN_RAUTO));
    }
}

/** @brief One step of row @p i of LIMIT RUN's list, on its ">" key; MODE
 *         takes a tap on its value. */
static void lr_step(int i)
{
    ui_setlist_t *sl = (ui_setlist_t *)servo_screen_list();
    ui_setlist_scroll_to(sl, i);
    const gfx_rect_t r = ui_setlist_row_part(
        sl, i, (i == LR_MODE) ? UI_SL_PART_VALUE : UI_SL_PART_PLUS);
    screen_tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief A limit run at SEARCH+PROG with SERVO PULSES 1000-2000 started
 *         from SAFE on the programming port behind a PD mini, the servo's
 *         stops at 1150 and 1880us inside its travel. */
static void lim_start(void)
{
    servo_sim_cfg_t sc;
    ra_setup(true, false);
    servo_sim_defaults(&sc);
    servo_sim_init(&ra.sv, &sc);
    ra.sv.position_us = 1500.0f;
    ra.armed = false;
    servo_screen_set_armed(false);
    ra_ms(200u);
    settings_request_save();
    go(SERVO_FN_LIMRUN);
    screen_tap(SV_FOOT_SET_X, SV_FOOT_Y);
    CHECK(servo_screen_list()->open);
    lr_step(LR_MODE);
    lr_step(LR_PULSES);
    CHECK(scr->back());
    go(SERVO_FN_LIMRUN);
    ra_ms(100u);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2300u);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 1);
    for (int i = 0; i < 200 && !servo_screen_limit_running(); ++i) {
        ra_ms(20u);
    }
}

/* The limit run: the read session, a search, the SAFE asked and not yet
 * read back, and the session of the first write with its ARM. */
TEST_CASE(a_dial_hold_ends_a_limit_run_in_every_phase)
{
    static const int k_ph[] = {
        SERVO_LIMRUN_PH_CONNECT, SERVO_LIMRUN_PH_SEARCH, PH_SAFE_ASKED,
        SERVO_LIMRUN_PH_WRITE, SERVO_LIMRUN_PH_LEAVE, SERVO_LIMRUN_PH_ARM,
    };
    for (size_t k = 0; k < sizeof(k_ph) / sizeof(k_ph[0]); ++k) {
        lim_start();
        CHECK(servo_screen_limit_running());
        bool there = to_phase(servo_screen_limit_running,
                              servo_screen_limit_phase,
                              SERVO_LIMRUN_PH_SAFE, k_ph[k]);
        if (there && k_ph[k] == PH_SAFE_ASKED) {
            /* The SAFE asked ahead of a write session, not the one the
             * read session began with: the horn has searched by then. */
            there = ra.arms > 0u;
        }
        if (!there) {
            printf("  limit run phase %d not reached\n", k_ph[k]);
        }
        CHECK(there);
        if (k_ph[k] == SERVO_LIMRUN_PH_CONNECT
            || k_ph[k] == SERVO_LIMRUN_PH_WRITE
            || k_ph[k] == SERVO_LIMRUN_PH_LEAVE) {
            ra_ms(40u);
            CHECK(!ra.armed);       /* the run's own SAFE */
            CHECK_EQ(servo_screen_limit_phase(), k_ph[k]);
        }
        hold_ends_it(servo_screen_limit_running,
                     servo_fn_screen_id(SERVO_FN_LIMRUN));
    }
}

/* No run: ARM asked and not yet read back counts as ARMED for the hold,
 * which posts DISARM; a SAFE bench with nothing asked takes nothing. */
TEST_CASE(a_dial_hold_takes_back_an_arm_on_its_way)
{
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    ra_ms(200u);
    go(SERVO_FN_LIVE);
    ra_ms(100u);
    CHECK(!servo_screen_dial_hold());
    ra.no_arm = true;           /* the bench has not answered the ARM */
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2300u);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 1);
    CHECK(servo_screen_arm_asked());
    const unsigned disarms = ra.disarms;
    CHECK(servo_screen_dial_hold());
    ra.no_arm = false;
    ra_ms(100u);
    CHECK(ra.disarms > disarms);
    CHECK(!servo_screen_arm_asked());
    CHECK(!ra.armed);
    ra_ms(3000u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!ra.armed);
}

int main(void)
{
    RUN(a_dial_hold_ends_auto_in_every_phase);
    RUN(a_dial_hold_ends_a_limit_run_in_every_phase);
    RUN(a_dial_hold_takes_back_an_arm_on_its_way);
    return test_summary("servo_dial_runs");
}
