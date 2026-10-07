/*
 * What the programmer decides: that you say what you are programming before
 * anything is read, that stepping up a level cannot leave a stale connection
 * behind, and that a stepper stops rather than wraps.
 *
 * And for the ESC STICK class: that a run starts only from the warning held
 * for its two seconds, that it is refused rather than adjusted when the
 * supply cannot give what it needs, and that leaving, ABORT and STOP each
 * end it with the bench let go.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_sim.h"
#include "programmer_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_textkey.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* Mirrored from programmer_screen.c; if the layout moves these move with it. */
#define TILE_CX(i) (18 + (i) * 260 + 122)
#define TILE_CY    180
#define BACK_X     66
#define BACK_Y     27
#define ROW_CX     400
#define ROW_CY(n)  (64 + (n) * 68 + 29)
#define CONNECT_X  698
#define CONNECT_Y  70
#define STEP_DN_X  653
#define STEP_UP_X  765
#define STEP_CY(i) (132 + (i) * 30 + 10)
#define READ_X     516
#define WRITE_X    698
#define BTN_CY     407
#define PAGE_UP_X  723
#define PAGE_DN_X  761
#define PAGE_CY    116

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_init();
    supply_screen()->reset();
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    supply_screen_set_caps(&caps);
    supply_screen_settings_loaded();
    scr = programmer_screen();
    scr->reset();
}

static void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
}

static int lit(void)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != ui_theme_color(UI_C_BG)) { ++n; }
    }
    return n;
}

/* ESC (electronic speed controller) then BLHeli_S then CONNECT: the whole
 * descent. */
static void descend_to_blheli(void)
{
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    tap(CONNECT_X, CONNECT_Y);
}

/*
 * Pins: the protocol rows accept a press before the page is rendered.  Hit
 * rectangles are laid out independently of drawing, so the press here comes
 * with no render at all.
 */
TEST_CASE(the_protocol_list_is_pressable_before_it_is_painted)
{
    fresh();
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(1));                 /* AM32, never rendered */
    tap(CONNECT_X, CONNECT_Y);
    CHECK(programmer_screen_connected());
    CHECK_EQ(programmer_screen_protocol(), 1);
}

/* A class shows its own protocols and no others. */
TEST_CASE(a_class_shows_only_its_own_protocols)
{
    fresh();
    tap(TILE_CX(1), TILE_CY);               /* SERVO: one protocol */
    /* The fourth row does not exist in this class, so pressing where it
     * would be in the other one must do nothing. */
    tap(ROW_CX, ROW_CY(3));
    CHECK(!programmer_screen_connected());
    tap(CONNECT_X, CONNECT_Y);
    CHECK(!programmer_screen_connected());  /* still on the list */

    tap(ROW_CX, ROW_CY(0));                 /* Hitec, the only servo one */
    CHECK_EQ(programmer_screen_protocol(), 4);
}

/*
 * The reason for the hierarchy: the device that answered a one-wire
 * bootloader is not the device that answers a CLI (command-line interface),
 * and the screen must never show one identity above the other's parameters.
 * Stepping back up drops the connection.
 */
TEST_CASE(stepping_back_up_drops_the_connection)
{
    fresh();
    descend_to_blheli();
    CHECK(programmer_screen_connected());

    tap(BACK_X, BACK_Y);                    /* to the protocol list */
    CHECK(!programmer_screen_connected());

    tap(ROW_CX, ROW_CY(2));                 /* AM32 */
    CHECK_EQ(programmer_screen_protocol(), 2);
    CHECK(!programmer_screen_connected());
}

/* Back is a step up the hierarchy, not out of the screen: two presses reach
 * the class picker rather than doing nothing the second time. */
TEST_CASE(back_climbs_one_level_at_a_time)
{
    fresh();
    descend_to_blheli();
    scr->render(&cv, 0);
    const int at_device = lit();

    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int at_list = lit();

    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int at_class = lit();

    CHECK(at_device > 0 && at_list > 0 && at_class > 0);
    if (at_class == at_list || at_list == at_device) {
        T_FAIL("levels drew the same: %d, %d, %d",
               at_class, at_list, at_device);
    }
    /*
     * And from the top it does nothing rather than leaving the screen.
     *
     * Asserted by descending again rather than by comparing pixels: pressing
     * BACK at the class picker changes nothing, so the redraw gate paints
     * nothing, and a test that cleared the buffer first measures its own
     * memset.
     */
    tap(BACK_X, BACK_Y);
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    tap(CONNECT_X, CONNECT_Y);
    CHECK(programmer_screen_connected());
}

/* Each protocol brings its own parameters and its own defaults. */
TEST_CASE(each_protocol_starts_from_its_own_defaults)
{
    fresh();
    descend_to_blheli();
    CHECK_EQ(programmer_screen_value(2), 0);   /* 24 kHz */
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(programmer_screen_value(2), 1);
    /* And that shows as an unwritten change rather than blending in. */
    CHECK_EQ(programmer_screen_dirty(), 1);

    tap(BACK_X, BACK_Y);
    tap(ROW_CX, ROW_CY(1));                     /* AM32 */
    tap(CONNECT_X, CONNECT_Y);
    /* AM32's third row starts at 48 kHz on its own account, not because
     * BLHeli_S was left on index one. */
    CHECK_EQ(programmer_screen_value(2), 1);
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(programmer_screen_value(2), 2);
}

/*
 * Clamped, not wrapped.  A parameter that rolls from its last value round to
 * its first is set to the wrong end by one press too many, and on an ESC the
 * wrong end is a direction.
 */
TEST_CASE(a_stepper_stops_at_its_ends)
{
    fresh();
    descend_to_blheli();

    /* Row 0 is an enum of three; row 3 is a number bounded 25..150 by 25.
     * Both are steppers to the finger and both must stop. */
    for (int i = 0; i < 12; ++i) { tap(STEP_DN_X, STEP_CY(0)); }
    CHECK_EQ(programmer_screen_value(0), 0);
    for (int i = 0; i < 12; ++i) { tap(STEP_UP_X, STEP_CY(0)); }
    CHECK_EQ(programmer_screen_value(0), 2);

    for (int i = 0; i < 20; ++i) { tap(STEP_UP_X, STEP_CY(3)); }
    CHECK_EQ(programmer_screen_value(3), 150);
    for (int i = 0; i < 20; ++i) { tap(STEP_DN_X, STEP_CY(3)); }
    CHECK_EQ(programmer_screen_value(3), 25);
}

/* Nothing is editable until something has answered. */
TEST_CASE(no_device_means_no_editing)
{
    fresh();
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    const int before = programmer_screen_value(0);
    tap(STEP_UP_X, STEP_CY(0));
    CHECK_EQ(programmer_screen_value(0), before);

    tap(CONNECT_X, CONNECT_Y);
    tap(STEP_UP_X, STEP_CY(0));
    CHECK(programmer_screen_value(0) != before);
}

/* Eight parameters in six rows: the other two have to be reachable. */
TEST_CASE(paging_reaches_the_rows_that_do_not_fit)
{
    fresh();
    descend_to_blheli();
    scr->render(&cv, 0);
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_DN_X, PAGE_CY);
    scr->render(&cv, 0);
    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != first[i]) { ++differ; }
    }
    if (differ < 2000) {
        T_FAIL("paging changed only %d pixels", differ);
    }

    for (int i = 0; i < 10; ++i) { tap(PAGE_DN_X, PAGE_CY); }
    scr->render(&cv, 0);
    gfx_color_t *bottom = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(bottom, fb, (size_t)W * H * sizeof(gfx_color_t));
    tap(PAGE_DN_X, PAGE_CY);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(bottom, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(first);
    free(bottom);
}

/* Every protocol reaches its parameters and draws them. */
TEST_CASE(every_protocol_draws_its_parameters)
{
    /* Four ESC protocols and one servo; BLHeli_32 is not one of them, see
     * docs/BLHeli32.md. */
    const int klass[] = { 0, 0, 0, 0, 1 };
    const int slot[]  = { 0, 1, 2, 3, 0 };
    for (int p = 0; p < (int)(sizeof(klass) / sizeof(klass[0])); ++p) {
        fresh();
        tap(TILE_CX(klass[p]), TILE_CY);
        tap(ROW_CX, ROW_CY(slot[p]));
        tap(CONNECT_X, CONNECT_Y);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK_EQ(programmer_screen_protocol(), p);
        if (lit() < 20000) {
            T_FAIL("protocol %d drew only %d pixels", p, lit());
        }
    }
}


/*
 * Until a read or a write happens, a staged edit and the device's value
 * disagree, and the screen shows the difference: an edited value must not
 * look like a value read off the hardware.
 */
TEST_CASE(a_change_is_unwritten_until_it_is_written)
{
    fresh();
    descend_to_blheli();
    CHECK_EQ(programmer_screen_dirty(), 0);

    tap(STEP_UP_X, STEP_CY(1));
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(programmer_screen_dirty(), 2);

    /* Writing makes the device agree with the screen. */
    tap(WRITE_X, BTN_CY);                   /* WRITE */
    CHECK_EQ(programmer_screen_dirty(), 0);

    /* Reading throws staged edits away, which is what reading means. */
    const int was = programmer_screen_value(1);
    tap(STEP_UP_X, STEP_CY(1));
    CHECK_EQ(programmer_screen_dirty(), 1);
    tap(READ_X, BTN_CY);                    /* READ */
    CHECK_EQ(programmer_screen_dirty(), 0);
    CHECK_EQ(programmer_screen_value(1), was);
}

/* Three kinds of parameter, and the stepper is the same gesture for all of
 * them: a switch has two values, a choice has its list, a number has its
 * range.  The widget differs; the way you move it does not. */
TEST_CASE(every_kind_steps)
{
    fresh();
    descend_to_blheli();
    /* 0 enum, 3 number, 5 boolean. */
    const int rows[3] = { 0, 3, 5 };
    for (int k = 0; k < 3; ++k) {
        const int before = programmer_screen_value(rows[k]);
        tap(STEP_UP_X, STEP_CY(rows[k]));
        if (programmer_screen_value(rows[k]) == before) {
            T_FAIL("row %d did not step from %d", rows[k], before);
        }
    }
}

/* ------------------------------------------------------- ESC STICK */

/* Mirrored from programmer_screen.c, as the rest are. */
#define SP_ROW_CY(n) (52 + (n) * 36 + 16)
#define LIST_DN_X    761
#define LIST_CY      27
#define TIMING_X     698
#define TIMING_Y     70
#define HOLD_X       156
#define HOLD_Y       378
#define CANCEL_X     684
#define HOLD_ID      7

static void press(touch_event_type_t type, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = HOLD_ID, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}

static void hold_for(float seconds)
{
    press(TOUCH_EVENT_DOWN, HOLD_X, HOLD_Y);
    for (float t = 0.0f; t < seconds; t += 0.25f) {
        scr->tick(0.25f);
    }
    press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
}

/* The stick class, then hobbywing-flyfun-8item: the third that runs. */
static void descend_to_hobbywing(void)
{
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(ROW_CX, SP_ROW_CY(2));
}

/* Cutoff mode (item 3) to hard cutoff, its second value. */
static void pick_cutoff(void)
{
    tap(STEP_UP_X, STEP_CY(2));
    tap(STEP_UP_X, STEP_CY(2));
}

static int drain_cmds(motor_cmd_kind_t want)
{
    int n = 0;
    motor_cmd_t mc;
    while (programmer_screen_poll_cmd(&mc)) {
        n += (mc.kind == want) ? 1 : 0;
    }
    return n;
}

/* The modelled bench a run goes to, as the panel's without a coprocessor:
 * the arm and the supply follow what is asked, the supply is read every
 * 50 ms, and the simulated ESC draws the current. */
typedef struct {
    esc_sim_t sim;
    bool      armed, on;
    float     pct;
    uint32_t  now, next, seq, stops;
    int       arms, disarms;
    /* What can go wrong with the bench. */
    bool      no_arm, no_power, silent, offline, link_lost, report_off;
    bool      module_on;      /* what the supply itself reads         */
    uint32_t  off_at, off_lag; /* it goes off this much after the OFF */
    uint32_t  moved_while_on;
    float     last_pct;
    uint32_t  every;          /* ms between readings; 0 is 50 */
    int32_t   extra_ma;
} rig_t;

static void rig_step(rig_t *r)
{
    motor_cmd_t mc;
    while (programmer_screen_poll_cmd(&mc)) {
        if (mc.kind == MOTOR_CMD_ARM) {
            r->armed = !r->no_arm;
            r->arms++;
        } else if (mc.kind == MOTOR_CMD_DISARM) {
            r->armed = false;
            r->pct = 0.0f;
            r->disarms++;
        } else if (mc.kind == MOTOR_CMD_THROTTLE) {
            r->pct = mc.value;
        }
    }
    supply_cmd_t sc;
    if (supply_screen_poll_cmd(&sc)) {
        if (sc.off && r->on) {
            r->off_at = r->now;
        }
        r->on = sc.off ? false : (sc.on ? !r->no_power : r->on);
        if (r->on) {
            r->module_on = true;
        }
        supply_screen_set_output(r->on);
        supply_screen_set_on_coming(false);
    }
    /* The panel's flag follows its request; the module follows it off_lag
     * later, as the PD mini does. */
    if (!r->on && r->module_on && r->now - r->off_at >= r->off_lag) {
        r->module_on = false;
    }
    const float eff = r->armed ? r->pct : -1.0f;
    if (eff != r->last_pct && r->module_on && !r->on) {
        r->moved_while_on++;
    }
    r->last_pct = eff;
    programmer_screen_bench(r->now, r->armed, r->stops, !r->link_lost);
    const int32_t ma = esc_sim_step(&r->sim, r->now, r->module_on,
                                    r->armed ? r->pct : -1.0f);
    if (r->now >= r->next && !r->silent) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = (uint16_t)++r->seq;
        st.taken_ms = r->now;
        st.i = r->module_on ? (float)(ma + r->extra_ma) / 1000.0f : 0.0f;
        st.output = r->on;                              /* asked */
        st.mode = (r->module_on && !r->report_off) ? SUPPLY_MODE_CV
                                                   : SUPPLY_MODE_OFF;
        st.online = !r->offline;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        programmer_screen_supply(&st);
        r->next = r->now + ((r->every != 0u) ? r->every : 50u);
    }
    scr->tick(0.001f);
    r->now++;
}

static void rig_start(rig_t *r)
{
    memset(r, 0, sizeof(*r));
    r->last_pct = -1.0f;
    esc_sim_init(&r->sim, programmer_screen_stick()->p, NULL);
}

static void rig_run(rig_t *r, uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(programmer_screen_stick());
         ++i) {
        rig_step(r);
    }
    for (int i = 0; i < 5; ++i) {
        rig_step(r);            /* what the end asked for, taken */
    }
}

/* The list puts what can run first; a row that cannot run says why and goes
 * no further. */
TEST_CASE(a_profile_the_engine_cannot_run_goes_no_further)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(LIST_DN_X, LIST_CY);
    tap(LIST_DN_X, LIST_CY);                 /* rows 18 to 26 */
    tap(ROW_CX, SP_ROW_CY(5));               /* the 24th: cannot run */
    tap(WRITE_X, BTN_CY);                    /* where RUN would be */
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    CHECK_EQ(drain_cmds(MOTOR_CMD_ARM), 0);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

TEST_CASE(a_run_needs_a_change_and_the_warning_held_two_seconds)
{
    fresh();
    descend_to_hobbywing();
    tap(WRITE_X, BTN_CY);                    /* nothing picked: no RUN */
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);

    pick_cutoff();
    tap(WRITE_X, BTN_CY);                    /* RUN: the warning */
    hold_for(1.5f);                          /* let go early */
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    tap(CANCEL_X, HOLD_Y);                   /* and closed */
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);

    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    CHECK(esc_stick_running(programmer_screen_stick()));
    /* It begins by arming, with the throttle at rest and the supply off. */
    motor_cmd_t mc;
    CHECK(programmer_screen_poll_cmd(&mc));
    CHECK_EQ(mc.kind, MOTOR_CMD_ARM);
    CHECK(!programmer_screen_poll_cmd(&mc));
    CHECK(!supply_screen_output_live());
}

/* The hold is the finger on the button: off it, touch lost, or a STOP, and
 * the hold is gone; a new one is a new decision. */
TEST_CASE(a_hold_that_is_interrupted_starts_nothing)
{
    for (int k = 0; k < 3; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        press(TOUCH_EVENT_DOWN, HOLD_X, HOLD_Y);
        scr->tick(0.25f);
        scr->tick(0.25f);
        if (k == 0) {
            press(TOUCH_EVENT_MOVE, HOLD_X, 100);   /* off the button */
        } else if (k == 1) {
            scr->cancel();                          /* touch went missing */
        } else {
            programmer_screen_bench(0u, false, 1u, false);  /* STOP */
        }
        for (int i = 0; i < 12; ++i) {
            scr->tick(0.25f);
        }
        press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
        if (programmer_screen_stick_runs() != 0u) {
            T_FAIL("interruption %d still started a run", k);
        }
    }
}

TEST_CASE(a_whole_run_on_the_modelled_bench_stores_and_lets_go)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    rig_run(&r, 300000u);
    const esc_stick_t *run = programmer_screen_stick();
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
    CHECK_EQ(r.sim.stores, 1u);
    CHECK_EQ(r.arms, 1);
    CHECK_EQ(r.disarms, 1);
    CHECK(!r.armed);
    CHECK(!r.on);
    CHECK(r.pct == 0.0f);
    /* The supply ran at the profile's two cells. */
    CHECK_NEAR(supply_screen_set_v(), 7.6f, 0.001f);
    CHECK(programmer_screen_stick_profile() == NULL);

    /* The result stands until OK, and the menu comes back after it. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int result = lit();
    tap(WRITE_X, BTN_CY);                    /* OK */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    if (lit() == result) {
        T_FAIL("OK left the result showing");
    }
}

/* Leaving the screen, ABORT and STOP each end a run with the bench let go:
 * a DISARM, and the supply's OFF. */
TEST_CASE(leaving_abort_and_stop_end_a_run_and_let_go)
{
    for (int k = 0; k < 3; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        static rig_t r;
        rig_start(&r);
        for (int i = 0; i < 12000; ++i) {
            rig_step(&r);                    /* powered, in the menu */
        }
        CHECK(r.on && r.armed);
        CHECK(programmer_screen_stick_profile() != NULL);
        esc_stick_reason_t want = ESC_STICK_R_LEFT;
        if (k == 0) {
            scr->leave();
        } else if (k == 1) {
            tap(BACK_X, BACK_Y);             /* not while it runs */
            CHECK(esc_stick_running(programmer_screen_stick()));
            tap(WRITE_X, BTN_CY);            /* ABORT */
            want = ESC_STICK_R_USER;
        } else {
            r.stops++;
            want = ESC_STICK_R_STOP;
        }
        rig_run(&r, 1000u);
        const esc_stick_t *run = programmer_screen_stick();
        CHECK_EQ(run->phase, ESC_STICK_ABORTED);
        CHECK_EQ(run->reason, want);
        /* An end the operator chose lights no red. */
        bool red = true, green = true;
        programmer_screen_stick_lights(&red, &green);
        CHECK(!red && !green);
        CHECK(!r.armed);
        CHECK(!r.on);
        CHECK_EQ(r.sim.stores, 0u);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK(lit() > 20000);
    }
}

/* Refused, not adjusted: a voltage over the SUPPLY cap, or a live output,
 * keeps RUN from opening the warning. */
TEST_CASE(a_run_the_supply_cannot_give_is_refused)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 5.0f);
    supply_screen_limits_changed();
    descend_to_hobbywing();
    pick_cutoff();
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);

    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_screen_ask_on();                 /* the output on its way */
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);

    /* A current limit over the SUPPLY cap. */
    fresh();
    settings_set(SET_STICK_I, 3.0f);
    settings_set(SET_SUPPLY_I_MAX, 2.0f);
    supply_screen_limits_changed();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
}

TEST_CASE(timing_steps_the_settings_and_closing_asks_for_a_save)
{
    fresh();
    descend_to_hobbywing();
    tap(TIMING_X, TIMING_Y);
    tap(STEP_UP_X, STEP_CY(2));              /* BEEP MIN */
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    tap(STEP_UP_X, STEP_CY(0));              /* VOLTAGE off FROM PROFILE */
    CHECK_NEAR(settings_get(SET_STICK_V), 0.1f, 0.001f);
    for (int i = 0; i < 10; ++i) {
        tap(PAGE_DN_X, PAGE_CY);
    }
    tap(STEP_DN_X, STEP_CY(6));              /* the last: HYSTERESIS */
    CHECK_EQ(settings_get_int(SET_STICK_HYST), 30);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    tap(READ_X, BTN_CY);                     /* DEFAULTS */
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);
    CHECK_EQ(settings_get_int(SET_STICK_HYST), 40);
    tap(WRITE_X, BTN_CY);                    /* CLOSE */
    CHECK(settings_save_asked());
    settings_cancel_save();
}

/* An item steps from KEEP through its values and stops at either end. */
TEST_CASE(an_item_steps_from_keep_and_stops_at_its_ends)
{
    fresh();
    descend_to_hobbywing();
    for (int i = 0; i < 5; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* brake: off, on */
    }
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    const esc_stick_t *run = programmer_screen_stick();
    CHECK_EQ(run->n, 1);
    CHECK_EQ(run->ch[0].item, 0);
    CHECK_EQ(run->ch[0].value, 1);           /* the last value, "on" */
    scr->leave();

    fresh();
    descend_to_hobbywing();
    tap(STEP_UP_X, STEP_CY(0));
    for (int i = 0; i < 5; ++i) {
        tap(STEP_DN_X, STEP_CY(0));          /* back to KEEP, and no lower */
    }
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    /* Paging reaches item 8, and BACK climbs to the list. */
    tap(PAGE_DN_X, PAGE_CY);
    tap(STEP_UP_X, STEP_CY(6));
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 5000);
}

/* Run to the end, drawing the page at every phase it passes through;
 * returns the phases seen, one bit each. */
static unsigned run_drawing(rig_t *r, uint32_t ms)
{
    unsigned seen = 1u << programmer_screen_stick()->phase;
    int last = (int)programmer_screen_stick()->phase;
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    for (uint32_t i = 0; i < ms; ++i) {
        rig_step(r);
        const esc_stick_t *run = programmer_screen_stick();
        if ((int)run->phase != last) {
            last = (int)run->phase;
            seen |= 1u << run->phase;
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, (int)(i & 1u));
            if (lit() < 20000) {
                T_FAIL("phase %s drew only %d pixels",
                       esc_stick_phase_text(run->phase), lit());
            }
        }
        if (!esc_stick_running(run)) {
            break;
        }
    }
    return seen;
}

/* Every phase a run passes through draws: a two-stage menu's items and
 * values, and a one-stage menu's power cycle between its two changes. */
TEST_CASE(every_phase_of_a_run_draws)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    unsigned seen = run_drawing(&r, 300000u);
    const unsigned two = (1u << ESC_STICK_ARMING) | (1u << ESC_STICK_SIGNAL)
                         | (1u << ESC_STICK_POWER) | (1u << ESC_STICK_ENTRY)
                         | (1u << ESC_STICK_ITEMS) | (1u << ESC_STICK_VALUES)
                         | (1u << ESC_STICK_STORE) | (1u << ESC_STICK_DONE);
    CHECK_EQ(seen & two, two);

    /* sunrise-pro, the tenth that runs: page two, row one.  The list
     * refuses hobbywing-skywalker-v2-hv-opto at 22.8 V before it. */
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(LIST_DN_X, LIST_CY);
    tap(ROW_CX, SP_ROW_CY(0));
    tap(STEP_UP_X, STEP_CY(0));
    tap(STEP_UP_X, STEP_CY(0));
    tap(STEP_UP_X, STEP_CY(0));              /* battery: LiFe */
    tap(STEP_UP_X, STEP_CY(1));              /* timing: automatic */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    rig_start(&r);
    seen = run_drawing(&r, 300000u);
    CHECK(seen & (1u << ESC_STICK_CYCLE));
    CHECK_EQ(programmer_screen_stick()->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);
}

/* Each way a run can end draws its result with its reason. */
TEST_CASE(every_end_draws_its_reason)
{
    static const esc_stick_reason_t want[] = {
        ESC_STICK_R_NOT_ARMED, ESC_STICK_R_NO_POWER, ESC_STICK_R_STALE,
        ESC_STICK_R_RATE, ESC_STICK_R_TIMEOUT, ESC_STICK_R_NO_BEEPS,
        ESC_STICK_R_HIGH, ESC_STICK_R_SUPPLY_OFF, ESC_STICK_R_SUPPLY_LOST,
        ESC_STICK_R_DISARMED, ESC_STICK_R_LINK,
    };
    for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); ++k) {
        fresh();
        settings_set(SET_STICK_TIMEOUT, 20000.0f);
        descend_to_hobbywing();
        if (want[k] == ESC_STICK_R_TIMEOUT) {
            /* Brake is item 1, reached only by the wrap from the item
             * the profile ends on; one item more and it never is. */
            tap(STEP_UP_X, STEP_CY(0));
        } else {
            pick_cutoff();
        }
        tap(WRITE_X, BTN_CY);
        static rig_t r;
        memset(&r, 0, sizeof(r));
        /* The link answers at the start for LINK LOST, and only then. */
        programmer_screen_bench(0u, false, 0u, want[k] == ESC_STICK_R_LINK);
        hold_for(2.25f);
        esc_sim_init(&r.sim, programmer_screen_stick()->p, NULL);
        switch (want[k]) {
        case ESC_STICK_R_NOT_ARMED: r.no_arm = true;          break;
        case ESC_STICK_R_NO_POWER:  r.no_power = true;        break;
        case ESC_STICK_R_TIMEOUT:   r.sim.c.extra = 1u;       break;
        case ESC_STICK_R_NO_BEEPS:  r.sim.c.mute = true;      break;
        case ESC_STICK_R_RATE:      r.every = 400u;           break;
        default: break;
        }
        for (int i = 0; i < 12000; ++i) {
            rig_step(&r);
        }
        switch (want[k]) {
        case ESC_STICK_R_STALE:       r.silent = true;          break;
        case ESC_STICK_R_HIGH:        r.extra_ma = 2000;        break;
        case ESC_STICK_R_SUPPLY_OFF:  r.report_off = true;      break;
        case ESC_STICK_R_SUPPLY_LOST: r.offline = true;         break;
        case ESC_STICK_R_DISARMED:    r.no_arm = true; r.armed = false; break;
        case ESC_STICK_R_LINK:        r.link_lost = true;       break;
        default: break;
        }
        rig_run(&r, 200000u);
        const esc_stick_t *run = programmer_screen_stick();
        if (run->reason != want[k]) {
            T_FAIL("case %u ended %s", (unsigned)k,
                   esc_stick_reason_text(run->reason));
        }
        CHECK(!r.on);
        /* Every one of these is an end nobody chose: red, until OK. */
        bool red = false, green = true;
        programmer_screen_stick_lights(&red, &green);
        CHECK(red && !green);
        scr->tick(0.25f);
        programmer_screen_stick_lights(&red, NULL);
        CHECK(red);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK(lit() > 20000);
        /* BACK from the result goes to the list. */
        tap(BACK_X, BACK_Y);
        programmer_screen_stick_lights(&red, NULL);
        CHECK(!red);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 1);
        CHECK(lit() > 20000);
    }
}

/* What RUN says when it cannot start: no cell count to take a voltage
 * from, a voltage over the cap, and a supply switched on in the middle of
 * the warning. */
TEST_CASE(a_run_that_cannot_start_says_why)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(ROW_CX, SP_ROW_CY(0));               /* dualsky: no cell count */
    tap(STEP_UP_X, STEP_CY(0));
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    settings_set(SET_STICK_V, 7.4f);         /* and with one it runs */
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    scr->leave();

    /* VOLTAGE over the cap, set where a profile ran: refused on RUN. */
    fresh();
    descend_to_hobbywing();
    tap(STEP_UP_X, STEP_CY(0));
    settings_set(SET_STICK_V, 20.0f);
    settings_set(SET_SUPPLY_V_MAX, 12.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    /* And the list pages back as well as forward. */
    tap(BACK_X, BACK_Y);
    tap(LIST_DN_X, LIST_CY);
    tap(LIST_DN_X, LIST_CY);
    tap(723, LIST_CY);                       /* ^: back a page */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);

    /* The supply switched on while the warning is up: the hold completes
     * and the run is still refused. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    supply_screen_ask_on();
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
}

/* The pages step back as well as forward, and leaving with TIMING open
 * closes it and asks for the save. */
TEST_CASE(pages_step_back_and_leaving_closes_timing)
{
    fresh();
    descend_to_hobbywing();
    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_UP_X, PAGE_CY);
    tap(TIMING_X, TIMING_Y);
    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_UP_X, PAGE_CY);
    tap(80, STEP_CY(1));                     /* a row's name: its help */
    tap(STEP_UP_X, STEP_CY(1));
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    scr->leave();
    CHECK(settings_save_asked());
    settings_cancel_save();
    /* Back on the screen, TIMING is closed. */
    tap(80, STEP_CY(0));
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/*
 * The hold completes in the frame's tick, after the frame handed its
 * commands over, so its ARM waits a frame in the screen.  A stop or a
 * touch loss in between must take it away: the next frame would stamp it
 * with a fresh stop count and touch number, and it would clear the stop.
 * Both orderings: the interruption after the hold completed, and during
 * it.
 */
TEST_CASE(a_stop_or_a_lost_touch_takes_a_queued_arm_away)
{
    for (int k = 0; k < 2; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);                     /* ARM queued, not taken */
        CHECK_EQ(programmer_screen_stick_runs(), 1u);
        if (k == 0) {
            programmer_screen_bench(10u, false, 1u, false);   /* STOP */
        } else {
            scr->cancel();                   /* touch events lost */
        }
        motor_cmd_t mc;
        int arms = 0;
        while (programmer_screen_poll_cmd(&mc)) {
            arms += (mc.kind == MOTOR_CMD_ARM) ? 1 : 0;
        }
        CHECK_EQ(arms, 0);
        const esc_stick_t *run = programmer_screen_stick();
        CHECK(!esc_stick_running(run));
        CHECK_EQ(run->reason, (k == 0) ? ESC_STICK_R_STOP : ESC_STICK_R_TOUCH);
        CHECK(!supply_screen_output_live());
    }

    /* The ARM already taken, the bench not yet armed: a lost touch still
     * ends the run, with a DISARM. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(drain_cmds(MOTOR_CMD_ARM), 1);
    scr->cancel();
    CHECK_EQ(programmer_screen_stick()->reason, ESC_STICK_R_TOUCH);
    CHECK_EQ(drain_cmds(MOTOR_CMD_DISARM), 1);

    /* Armed and running, a lost touch is the arm watch's to judge. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    for (int i = 0; i < 3000; ++i) {
        rig_step(&r);
    }
    scr->cancel();
    CHECK(esc_stick_running(programmer_screen_stick()));
    scr->leave();
}

/* The reviewers' proof through the screen: the SUPPLY flag goes off with
 * the request, the module 300 ms later.  The run waits for the module. */
TEST_CASE(the_screen_passes_the_supplys_own_state)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    r.off_lag = 300u;
    rig_run(&r, 300000u);
    CHECK_EQ(programmer_screen_stick()->phase, ESC_STICK_DONE);
    CHECK_EQ(r.moved_while_on, 0u);
    CHECK(!r.module_on);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
}

/* The list follows VOLTAGE and the SUPPLY cap: its order and its count are
 * built again when either moves, not only when the class is opened. */
TEST_CASE(the_list_follows_voltage_and_the_cap)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    settings_set(SET_SUPPLY_V_MAX, 7.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    /* Only DualSky, with no cell count, still runs; the third row is now
     * a refused one and opens nothing. */
    tap(ROW_CX, SP_ROW_CY(2));
    tap(STEP_UP_X, STEP_CY(2));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    /* The cap back up: hobbywing-flyfun-8item is the third row again. */
    settings_set(SET_SUPPLY_V_MAX, 21.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    tap(ROW_CX, SP_ROW_CY(2));
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    CHECK_STR_EQ(programmer_screen_stick()->p->id, "hobbywing-flyfun-8item");
    scr->leave();
    /* And on the way back from a profile, and on entering the screen. */
    scr->enter();
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
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
}

/* A profile the supply cannot give the voltage for is refused in the list,
 * with the voltage it needs. */
TEST_CASE(a_profile_over_the_supply_cap_is_refused_in_the_list)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 7.0f);
    supply_screen_limits_changed();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    /* Every profile with a cell count needs 7.6 V or more: only DualSky's,
     * which states none, is left to open, and the third row opens nothing. */
    tap(ROW_CX, SP_ROW_CY(2));
    tap(STEP_UP_X, STEP_CY(2));
    tap(STEP_UP_X, STEP_CY(2));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    /* The first row does open. */
    tap(BACK_X, BACK_Y);
    tap(TILE_CX(2), TILE_CY);
    tap(ROW_CX, SP_ROW_CY(0));
    tap(TIMING_X, TIMING_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* ------------------------------------------------------------ the search */

/* Mirrored from programmer_screen.c: the field, its X, and the keyboard
 * docked right of SP_DOCK_X below the crumb row. */
#define FIND_X     260
#define FIND_CLR_X 370
#define FIND_Y     27
static const gfx_rect_t k_dock = { 352, 50, 442, 376 };

/* One key of the docked search keyboard; "\n" is OK, "~" CANCEL, "<" DEL,
 * "#" CLR. */
static void find_key(char ch)
{
    static const char keys[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM*.";
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_search(&k, k_dock, "", "", 16);
    const char *at = strchr(keys, ch);
    const int key = (ch == '\n') ? UI_TK_OK
                  : (ch == '~')  ? UI_TK_CANCEL
                  : (ch == '<')  ? UI_TK_DEL
                  : (ch == '#')  ? UI_TK_CLR
                  : (ch == ' ')  ? UI_TK_SPACE
                                 : (int)(at - keys);
    const gfx_rect_t r = ui_textkey_key_rect(&k, key);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

static void find_type(const char *s)
{
    for (; *s != '\0'; ++s) {
        find_key(*s);
    }
}

static int listed(void)
{
    return programmer_screen_stick_listed(NULL);
}

/* Every key filters the list at once, and the list stays in view. */
TEST_CASE(the_search_filters_the_list_with_every_key)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    CHECK_EQ(listed(), (int)esc_profiles_count());
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
    tap(LIST_DN_X, LIST_CY);                 /* a page down first */
    int top = 0;
    (void)programmer_screen_stick_listed(&top);
    CHECK_EQ(top, 9);

    tap(FIND_X, FIND_Y);
    CHECK(programmer_screen_stick_typing());
    find_key('S');
    CHECK_STR_EQ(programmer_screen_stick_search(), "S");
    (void)programmer_screen_stick_listed(&top);
    CHECK_EQ(top, 0);                        /* back to the top */
    const int after_s = listed();
    CHECK(after_s > 3 && after_s < (int)esc_profiles_count());
    find_type("KY*V2");
    CHECK_STR_EQ(programmer_screen_stick_search(), "SKY*V2");
    CHECK_EQ(listed(), 3);
    find_key('<');                           /* DEL: "SKY*V" */
    CHECK(listed() >= 3);
    find_key('2');
    CHECK_EQ(listed(), 3);
    /* Drawn with the keyboard over the right of the list. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);

    /* A row tapped while typing opens its profile; the search stays. */
    tap(100, SP_ROW_CY(0));
    CHECK(!programmer_screen_stick_typing());
    const esc_profile_t *p = programmer_screen_stick_page();
    CHECK(p != NULL);
    CHECK(esc_profile_matches(p, "SKY*V2"));
    tap(BACK_X, BACK_Y);
    CHECK(programmer_screen_stick_page() == NULL);
    CHECK_STR_EQ(programmer_screen_stick_search(), "SKY*V2");
    CHECK_EQ(listed(), 3);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 1);
    CHECK(lit() > 20000);

    /* X clears it. */
    tap(FIND_CLR_X, FIND_Y);
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
    CHECK_EQ(listed(), (int)esc_profiles_count());
    CHECK(!programmer_screen_stick_typing());
}

/* OK keeps what was typed, CANCEL goes back to what the keyboard opened
 * on, and an empty entry is no search. */
TEST_CASE(ok_keeps_the_search_cancel_restores_it_and_empty_clears_it)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(FIND_X, FIND_Y);
    find_type("FLYFUN\n");
    CHECK(!programmer_screen_stick_typing());
    CHECK_STR_EQ(programmer_screen_stick_search(), "FLYFUN");
    CHECK_EQ(listed(), 3);

    tap(FIND_X, FIND_Y);
    find_type("#ZZZ");
    CHECK_EQ(listed(), 0);                   /* nothing found */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    find_key('~');                           /* CANCEL */
    CHECK(!programmer_screen_stick_typing());
    CHECK_STR_EQ(programmer_screen_stick_search(), "FLYFUN");
    CHECK_EQ(listed(), 3);

    tap(FIND_X, FIND_Y);
    find_type("#\n");                        /* CLR, OK: empty */
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
    CHECK_EQ(listed(), (int)esc_profiles_count());

    /* The search holds its most, sixteen. */
    tap(FIND_X, FIND_Y);
    find_type("ABCDEFGHIJKLMNOPQRS");
    CHECK_EQ((int)strlen(programmer_screen_stick_search()), 16);
    /* BACK and leaving the screen close the keyboard, the search kept. */
    tap(BACK_X, BACK_Y);
    CHECK(!programmer_screen_stick_typing());
    tap(TILE_CX(2), TILE_CY);
    CHECK_EQ((int)strlen(programmer_screen_stick_search()), 16);
    tap(FIND_X, FIND_Y);
    CHECK(programmer_screen_stick_typing());
    scr->leave();
    CHECK(!programmer_screen_stick_typing());
    CHECK_EQ(listed(), 0);
}

/* A press on a key the touch stream lost the release of types nothing. */
TEST_CASE(a_lost_touch_drops_a_search_key_under_way)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
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
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
    CHECK(programmer_screen_stick_typing());
    /* A key slid off is no key either. */
    e.type = TOUCH_EVENT_DOWN;
    scr->event(&e);
    e.type = TOUCH_EVENT_MOVE;
    e.point.x = 10;
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
}

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
    for (int i = 0; i < 240000 && esc_stick_running(programmer_screen_stick());
         ++i) {
        rig_step(&r);
        bool g = false;
        programmer_screen_stick_lights(&red, &g);
        CHECK(!red);
        if (programmer_screen_stick()->det.high) {
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
    CHECK_EQ(programmer_screen_stick()->phase, ESC_STICK_DONE);
    CHECK(on > 1000u && off > 1000u);
    CHECK(rises > 5u);
    bool g = true;
    programmer_screen_stick_lights(&red, &g);
    CHECK(!red && !g);                       /* DONE: both dark */
}

/* Past five changes the result says how many more there were. */
TEST_CASE(a_result_of_many_changes_counts_the_rest)
{
    fresh();
    /* hobbywing-flyfun-v5, the fifth that runs: twelve items. */
    programmer_screen_bench(0u, false, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(ROW_CX, SP_ROW_CY(4));
    for (int i = 0; i < 7; ++i) {
        tap(STEP_UP_X, STEP_CY(i));
    }
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    CHECK_EQ(programmer_screen_stick()->n, 7);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

int main(void)
{
    RUN(the_protocol_list_is_pressable_before_it_is_painted);
    RUN(a_class_shows_only_its_own_protocols);
    RUN(stepping_back_up_drops_the_connection);
    RUN(back_climbs_one_level_at_a_time);
    RUN(each_protocol_starts_from_its_own_defaults);
    RUN(a_stepper_stops_at_its_ends);
    RUN(no_device_means_no_editing);
    RUN(paging_reaches_the_rows_that_do_not_fit);
    RUN(a_change_is_unwritten_until_it_is_written);
    RUN(every_kind_steps);
    RUN(every_protocol_draws_its_parameters);
    RUN(a_profile_the_engine_cannot_run_goes_no_further);
    RUN(a_run_needs_a_change_and_the_warning_held_two_seconds);
    RUN(a_hold_that_is_interrupted_starts_nothing);
    RUN(a_whole_run_on_the_modelled_bench_stores_and_lets_go);
    RUN(leaving_abort_and_stop_end_a_run_and_let_go);
    RUN(a_run_the_supply_cannot_give_is_refused);
    RUN(timing_steps_the_settings_and_closing_asks_for_a_save);
    RUN(an_item_steps_from_keep_and_stops_at_its_ends);
    RUN(every_phase_of_a_run_draws);
    RUN(every_end_draws_its_reason);
    RUN(a_run_that_cannot_start_says_why);
    RUN(pages_step_back_and_leaving_closes_timing);
    RUN(a_stop_or_a_lost_touch_takes_a_queued_arm_away);
    RUN(reset_and_exit_rows_offer_no_value);
    RUN(the_screen_passes_the_supplys_own_state);
    RUN(the_list_follows_voltage_and_the_cap);
    RUN(a_profile_over_the_supply_cap_is_refused_in_the_list);
    RUN(a_result_of_many_changes_counts_the_rest);
    RUN(the_search_filters_the_list_with_every_key);
    RUN(ok_keeps_the_search_cancel_restores_it_and_empty_clears_it);
    RUN(a_lost_touch_drops_a_search_key_under_way);
    RUN(the_green_light_follows_the_beeps);
    return test_summary("programmer");
}
