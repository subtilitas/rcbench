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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_sim.h"
#include "gfx.h"
#include "programmer_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
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

/* The supply reading off for 300 ms: its own state off, the current at
 * 0, as the warning needs before it asks for a step at the ESC. */
static void supply_reads_off(void)
{
    for (uint32_t t = 0; t <= 300u; t += 100u) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = (uint16_t)(t / 100u + 1u);
        st.taken_ms = t;
        st.mode = SUPPLY_MODE_OFF;
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        programmer_screen_supply(&st);
    }
    programmer_screen_bench(300u, false, 0u, 0u, false);
    scr->tick(0.02f);
}

/* @p a and @p b alike with case folded, as the list groups makers. */
static bool same_name(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        x -= (x >= 'a' && x <= 'z') ? 32 : 0;
        y -= (y >= 'a' && y <= 'z') ? 32 : 0;
        if (x != y) {
            return false;
        }
        if (x == 0) {
            return true;
        }
    }
}

/* Tap row @p at of the level showing, paging to it first. */
static void tap_listed(int at)
{
    int top = 0;
    (void)programmer_screen_stick_listed(&top);
    for (int g = 0; g < 64 && at < top; ++g) {
        tap(723, LIST_CY);
        (void)programmer_screen_stick_listed(&top);
    }
    for (int g = 0; g < 64 && at >= top + 9; ++g) {
        tap(LIST_DN_X, LIST_CY);
        (void)programmer_screen_stick_listed(&top);
    }
    tap(ROW_CX, SP_ROW_CY(at - top));
}

/* On the makers: open the one named @p name. */
static void open_maker(const char *name)
{
    for (int i = 0; i < programmer_screen_stick_listed(NULL); ++i) {
        const char *m = programmer_screen_stick_maker_at(i);
        if (m != NULL && same_name(m, name)) {
            tap_listed(i);
            return;
        }
    }
    T_FAIL("no maker %s listed", name);
}

/* On a maker's models: open the first of profile @p id, or the one named
 * @p model. */
static void open_model(const char *id, const char *model)
{
    for (int i = 0; i < programmer_screen_stick_listed(NULL); ++i) {
        int m = -1;
        const esc_profile_t *p = programmer_screen_stick_row(i, &m);
        if (p != NULL && strcmp(p->id, id) == 0
            && (model == NULL || strcmp(p->models[m].name, model) == 0)) {
            tap_listed(i);
            supply_reads_off();          /* a panel's readings, off */
            return;
        }
    }
    T_FAIL("no model of %s listed", id);
}

/* The stick class, the profile's maker, and the profile's first model. */
static void open_profile(const char *id)
{
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    const esc_profile_t *p = esc_profiles_find(id);
    if (p == NULL) {
        T_FAIL("no profile %s", id);
        return;
    }
    open_maker(p->brand);
    open_model(id, NULL);
}

/* The stick class, then hobbywing-flyfun-8item. */
static void descend_to_hobbywing(void)
{
    open_profile("hobbywing-flyfun-8item");
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
    uint32_t  now, next, seq, stops, pressed;
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
    programmer_screen_bench(r->now, r->armed, r->stops, r->pressed,
                            !r->link_lost);
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
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.wait_hand = true;     /* a before_menu menu starts at the action */
    esc_sim_init(&r->sim, programmer_screen_stick()->p, &c);
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Castle Creations");          /* a yes/no menu: cannot run */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(programmer_screen_stick_page() == NULL);
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
            programmer_screen_bench(0u, false, 1u, 1u, false);  /* STOP */
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
    for (int k = 0; k < 4; ++k) {
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
        } else if (k == 2) {
            r.stops++;                       /* STOP pressed */
            r.pressed++;
            want = ESC_STICK_R_STOP;
        } else {
            r.stops++;                       /* the bench's own: touch */
            want = ESC_STICK_R_BENCH_STOP;
        }
        rig_run(&r, 1000u);
        const esc_stick_t *run = programmer_screen_stick();
        CHECK_EQ(run->phase, ESC_STICK_ABORTED);
        CHECK_EQ(run->reason, want);
        /* An end the operator chose lights no red; the bench's own does. */
        bool red = true, green = true;
        programmer_screen_stick_lights(&red, &green);
        CHECK_EQ(red, want == ESC_STICK_R_BENCH_STOP);
        CHECK(!green);
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

    /* sunrise-pro: two changes, one a power-up. */
    fresh();
    open_profile("sunrise-pro");
    CHECK_STR_EQ(programmer_screen_stick_page()->id, "sunrise-pro");
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
        programmer_screen_bench(0u, false, 0u, 0u, want[k] == ESC_STICK_R_LINK);
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
    open_profile("dualsky-xcontroller");     /* no cell count */
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
            programmer_screen_bench(10u, false, 1u, 1u, false);   /* STOP */
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Hobbywing");
    settings_set(SET_SUPPLY_V_MAX, 7.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    /* Every hobbywing-flyfun-8item model needs 7.6 V or more now: its row
     * is a refused one and opens nothing. */
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK(programmer_screen_stick_page() == NULL);
    /* The cap back up: it opens again. */
    settings_set(SET_SUPPLY_V_MAX, 21.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK(programmer_screen_stick_page() != NULL);
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Hobbywing");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    /* Every model with a cell count needs 7.6 V or more: none opens. */
    open_model("hobbywing-flyfun-8item", NULL);
    tap(STEP_UP_X, STEP_CY(2));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    CHECK(programmer_screen_stick_page() == NULL);
    /* DualSky's XController states none, and opens. */
    tap(BACK_X, BACK_Y);
    open_maker("Dualsky");
    open_model("dualsky-xcontroller", NULL);
    CHECK(programmer_screen_stick_page() != NULL);
    tap(TIMING_X, TIMING_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* ------------------------------------------------------------ the search */

/* Mirrored from programmer_screen.c: the field, its X, and the keyboard
 * docked right of SP_DOCK_X below the crumb row. */
#define FIND_X     600
#define FIND_CLR_X 682
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    CHECK_EQ(programmer_screen_stick_level(), 0);
    CHECK_EQ(listed(), 20);                  /* the makers */
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
    CHECK(after_s > 3 && after_s < 20);
    find_type("KY*V2");
    CHECK_STR_EQ(programmer_screen_stick_search(), "SKY*V2");
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
    CHECK_EQ(programmer_screen_stick_level(), 0);
    tap(100, SP_ROW_CY(0));
    CHECK(!programmer_screen_stick_typing());
    CHECK_EQ(programmer_screen_stick_level(), 1);
    CHECK_STR_EQ(programmer_screen_stick_maker(), "Hobbywing");
    CHECK_EQ(listed(), 14);
    for (int i = 0; i < listed(); ++i) {
        int m = -1;
        const esc_profile_t *p = programmer_screen_stick_row(i, &m);
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
    const esc_profile_t *p = programmer_screen_stick_page();
    CHECK(p != NULL);
    CHECK(esc_profile_matches(p, "SKY*V2"));
    tap(BACK_X, BACK_Y);
    CHECK(programmer_screen_stick_page() == NULL);
    CHECK_EQ(programmer_screen_stick_level(), 1);
    CHECK_STR_EQ(programmer_screen_stick_search(), "SKY*V2");
    CHECK_EQ(listed(), 14);
    tap(BACK_X, BACK_Y);
    CHECK_EQ(programmer_screen_stick_level(), 0);
    CHECK_EQ(listed(), 1);

    /* X clears it. */
    tap(FIND_CLR_X, FIND_Y);
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
    CHECK_EQ(listed(), 20);
    CHECK(!programmer_screen_stick_typing());
}

/* OK keeps what was typed, CANCEL goes back to what the keyboard opened
 * on, and an empty entry is no search. */
TEST_CASE(ok_keeps_the_search_cancel_restores_it_and_empty_clears_it)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(FIND_X, FIND_Y);
    find_type("FLYFUN\n");
    CHECK(!programmer_screen_stick_typing());
    CHECK_STR_EQ(programmer_screen_stick_search(), "FLYFUN");
    CHECK_EQ(listed(), 1);

    tap(FIND_X, FIND_Y);
    find_type("#ZZZ");
    CHECK_EQ(listed(), 0);                   /* nothing found */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    find_key('~');                           /* CANCEL */
    CHECK(!programmer_screen_stick_typing());
    CHECK_STR_EQ(programmer_screen_stick_search(), "FLYFUN");
    CHECK_EQ(listed(), 1);

    tap(FIND_X, FIND_Y);
    find_type("#\n");                        /* CLR, OK: empty */
    CHECK_STR_EQ(programmer_screen_stick_search(), "");
    CHECK_EQ(listed(), 20);

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
    programmer_screen_bench(0u, false, 0u, 0u, false);
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

/* The footer's count, mirrored from programmer_screen.c: SP_COUNT_W, 360
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(FIND_X, FIND_Y);
    find_type("*");
    const int makers = listed();
    CHECK_EQ(makers, 20 + (int)ESC_PROFILE_MAX_OVERRIDES);
    for (int i = 0; i < 20; ++i) {
        tap(LIST_DN_X, LIST_CY);
    }
    int top = 0;
    (void)programmer_screen_stick_listed(&top);
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
        programmer_invalidate();
        scr->render(&cv, l & 1);
        CHECK(lit() > 20000);
    }
    CHECK(COUNT_DOCK_CELLS <= COUNT_CELLS);
    ui_text_set_language(UI_LANG_EN);
    find_key('~');
    esc_profiles_clear_overrides();
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
    /* hobbywing-flyfun-v5: twelve items. */
    open_profile("hobbywing-flyfun-v5");
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

/* ------------------------------------------------------ manual steps */

/* MANUAL INTERVENTION REQUIRED, in the item list's header. */
#define HAND_X 264
#define HAND_Y 115

/* The stick class, page two, row five: kontronik-jazz, the fifth of the
 * Kontronik profiles that run. */
static void descend_to_jazz(void)
{
    open_profile("kontronik-jazz");
}

static void draws(void)
{
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
    const esc_profile_t *p = programmer_screen_stick_page();
    CHECK(p != NULL);
    if (p == NULL) {
        return;
    }
    CHECK_STR_EQ(p->id, "kontronik-jazz");
    CHECK(programmer_screen_stick_hand_shown());
    draws();
    tap(WRITE_X, BTN_CY);                    /* RUN's place: covered */
    tap(BACK_X, BACK_Y);
    CHECK(programmer_screen_stick_hand_shown());
    CHECK(programmer_screen_stick_page() == p);
    tap(CANCEL_X, HOLD_Y);                   /* OK */
    CHECK(!programmer_screen_stick_hand_shown());
    draws();

    /* Opened again, it stays closed; the button opens it. */
    tap(BACK_X, BACK_Y);
    open_model("kontronik-jazz", NULL);
    CHECK(programmer_screen_stick_page() == p);
    CHECK(!programmer_screen_stick_hand_shown());
    tap(HAND_X, HAND_Y);
    CHECK(programmer_screen_stick_hand_shown());
    tap(CANCEL_X, HOLD_Y);
    CHECK(!programmer_screen_stick_hand_shown());

    /* A profile without steps has no button and no pop-up. */
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);                     /* the makers */
    open_maker("Hobbywing");
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK_STR_EQ(programmer_screen_stick_page()->id,
                 "hobbywing-flyfun-8item");
    CHECK(!programmer_screen_stick_hand_shown());
    tap(HAND_X, HAND_Y);
    CHECK(!programmer_screen_stick_hand_shown());

    /* Leaving the screen closes it. */
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);
    open_maker("Kontronik");
    open_model("kontronik-jazz", NULL);
    tap(HAND_X, HAND_Y);
    CHECK(programmer_screen_stick_hand_shown());
    scr->leave();
    CHECK(!programmer_screen_stick_hand_shown());
}

/* A row the bench does not run opens nothing, unless it has manual steps:
 * then they show, over the list, with why it does not run. */
TEST_CASE(a_row_that_does_not_run_shows_its_manual_steps)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(FIND_X, FIND_Y);
    find_type("KOSMIK\n");
    CHECK_EQ(listed(), 1);                   /* one maker */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK_EQ(listed(), 12);                  /* its KOSMIK models */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(programmer_screen_stick_page() == NULL);
    CHECK(programmer_screen_stick_hand_shown());
    draws();
    tap(CANCEL_X, HOLD_Y);
    CHECK(!programmer_screen_stick_hand_shown());
    CHECK(programmer_screen_stick_page() == NULL);

    /* With the keyboard open, the tap closes it first. */
    tap(FIND_X, FIND_Y);
    CHECK(programmer_screen_stick_typing());
    tap(100, SP_ROW_CY(0));
    CHECK(!programmer_screen_stick_typing());
    CHECK(programmer_screen_stick_hand_shown());
    tap(CANCEL_X, HOLD_Y);

    /* graupner-brushless-control-t: a person reads its LEDs; no steps. */
    tap(FIND_CLR_X, FIND_Y);
    tap(BACK_X, BACK_Y);                     /* the makers */
    open_maker("Graupner");
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(!programmer_screen_stick_hand_shown());
    CHECK(programmer_screen_stick_page() == NULL);
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
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = programmer_screen_stick();
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
    CHECK(p != NULL && p->manual_count == 2u);
    if (p == NULL || p->manual_count < 2u) {
        return;
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(programmer_screen_step_text(&p->manual[1]),
                 p->manual[1].action);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(programmer_screen_step_text(&p->manual[1]),
                 "Nach 2 s oder der Tonfolge: JAZZ: Jumper abziehen. "
                 "MINIJAZZ: Taster dr\xC3\xBC" "cken.");
    /* No German in the step: the English. */
    esc_manual_t m = p->manual[0];
    m.action_de = "";
    CHECK_STR_EQ(programmer_screen_step_text(&m), m.action);
    m.action_de = NULL;
    CHECK_STR_EQ(programmer_screen_step_text(&m), m.action);
    CHECK_STR_EQ(programmer_screen_step_text(NULL), "");
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
    CHECK(programmer_screen_stick_hand_shown());
    draws();
    ui_text_set_language(UI_LANG_EN);
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
    const esc_stick_t *run = programmer_screen_stick();
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

/* ------------------------------------------------- makers and models */

/* The first level: every maker once, alphabetical with case folded, its
 * models counted, those that run among them. */
TEST_CASE(makers_are_alphabetical_and_count_their_models)
{
    fresh();
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    CHECK_EQ(programmer_screen_stick_level(), 0);
    CHECK_EQ(listed(), 20);
    for (int i = 1; i < listed(); ++i) {
        const char *a = programmer_screen_stick_maker_at(i - 1);
        const char *b = programmer_screen_stick_maker_at(i);
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
    CHECK_STR_EQ(programmer_screen_stick_maker_at(0), "Align");
    CHECK(programmer_screen_stick_maker_at(20) == NULL);
    /* Kontronik's models: every model of its 22 families. */
    open_maker("Kontronik");
    CHECK_EQ(programmer_screen_stick_level(), 1);
    unsigned models = 0;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        models += same_name(p->brand, "Kontronik") ? p->model_count : 0u;
    }
    CHECK_EQ(listed(), (int)models);
    CHECK(programmer_screen_stick_maker_at(0) == NULL);
    CHECK(programmer_screen_stick_row(listed(), NULL) == NULL);
    draws();
    /* The second level's BACK keeps the first's page. */
    tap(BACK_X, BACK_Y);
    tap(LIST_DN_X, LIST_CY);
    open_maker("Robbe");
    tap(BACK_X, BACK_Y);
    int top = 0;
    (void)programmer_screen_stick_listed(&top);
    CHECK_EQ(top, 9);
    CHECK_STR_EQ(programmer_screen_stick_maker(), "");
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
        programmer_screen_bench(0u, false, 0u, 0u, false);
        tap(TILE_CX(2), TILE_CY);
        open_maker(k[b]);
        bool missing = false;
        for (int i = 1; i < listed(); ++i) {
            int ma = -1, mb = -1;
            const esc_profile_t *pa = programmer_screen_stick_row(i - 1, &ma);
            const esc_profile_t *pb = programmer_screen_stick_row(i, &mb);
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Kontronik");
    const esc_profile_t *p = programmer_screen_stick_row(0, &m);
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Kontronik");
    open_model("kontronik-jazz", "JAZZ 55 LV");
    const esc_profile_t *p = programmer_screen_stick_page();
    CHECK(p != NULL && strcmp(p->id, "kontronik-jazz") == 0);
    const int m = programmer_screen_stick_model();
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Hobbywing");
    open_model(q->id, q->models[most].name);
    if (programmer_screen_stick_page() == q) {
        tap(STEP_UP_X, STEP_CY(0));
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        CHECK_EQ(programmer_screen_stick_runs(), 1u);
        CHECK_EQ(programmer_screen_stick()->out.supply_mv,
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    tap(FIND_X, FIND_Y);
    find_type("*KONTR*JAZZ*55*");
    CHECK_EQ(listed(), 1);
    CHECK_STR_EQ(programmer_screen_stick_maker_at(0), "Kontronik");
    draws();                                 /* docked, the makers */
    find_key('\n');
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK_EQ(programmer_screen_stick_level(), 1);
    CHECK_EQ(listed(), 1);
    int m = -1;
    const esc_profile_t *p = programmer_screen_stick_row(0, &m);
    CHECK(p != NULL && strcmp(p->models[m].name, "JAZZ 55 LV") == 0);
    tap(FIND_X, FIND_Y);
    draws();                                 /* docked, the models */
    find_key('~');
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(programmer_screen_stick_page() == p);
    tap(CANCEL_X, HOLD_Y);
    tap(BACK_X, BACK_Y);
    CHECK_EQ(listed(), 1);
    tap(BACK_X, BACK_Y);
    CHECK_EQ(programmer_screen_stick_level(), 0);
    CHECK_STR_EQ(programmer_screen_stick_search(), "*KONTR*JAZZ*55*");
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    CHECK_EQ(listed(), 21);
    CHECK_STR_EQ(programmer_screen_stick_maker_at(1), "Bench Test");
    open_maker("Kontronik");
    unsigned models = 0;
    bool found = false;
    for (size_t i = 0; i < esc_profiles_builtin_count; ++i) {
        const esc_profile_t *p = &esc_profiles_builtin[i];
        models += same_name(p->brand, "Kontronik") ? p->model_count : 0u;
    }
    CHECK_EQ(listed(), (int)(models + card.model_count));
    for (int i = 0; i < listed(); ++i) {
        const esc_profile_t *p = programmer_screen_stick_row(i, NULL);
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
                                 (uint16_t)(i + 1u) };
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
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Kontronik");
    CHECK_EQ(listed(), (int)ESC_MAKER_MODELS_MAX);
    int m = -1;
    CHECK(programmer_screen_stick_row((int)ESC_MAKER_MODELS_MAX - 1, &m)
          != NULL);
    CHECK(programmer_screen_stick_row((int)ESC_MAKER_MODELS_MAX, &m) == NULL);
    draws();
    for (int i = 0; i < 60; ++i) {
        tap(LIST_DN_X, LIST_CY);             /* to the last page */
    }
    draws();
    esc_profiles_clear_overrides();
}

/* SUN PLUS mode 4 waits 5 s from power-on, the profile's entry 2 s: the
 * page shows 5 s when mode 4 is picked, through the run, and on the result
 * after the change is made. */
TEST_CASE(the_page_shows_the_entry_the_run_waits)
{
    fresh();
    open_profile("kontronik-sun-plus");
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    CHECK_EQ(programmer_screen_stick_entry_shown(), 2000u);
    for (int i = 0; i < 4; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* KEEP, 1, 2, 3, 4 */
    }
    CHECK_EQ(programmer_screen_stick_entry_shown(), 5000u);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = programmer_screen_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK_EQ(programmer_screen_stick_entry_shown(), 5000u);
    esc_sim_hand(&r.sim, r.now);
    for (uint32_t i = 0; i < 240000u && run->phase != ESC_STICK_STORE;
         ++i) {
        rig_step(&r);
    }
    CHECK_EQ(run->phase, ESC_STICK_STORE);
    CHECK_EQ(esc_stick_entry_ms(run), 2000u);    /* the next change's */
    CHECK_EQ(programmer_screen_stick_entry_shown(), 5000u);
    rig_run(&r, 300000u);
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 4);
    CHECK_EQ(programmer_screen_stick_entry_shown(), 5000u);
    draws();
}

/* ALL STEPS, between HOLD TO RUN and CANCEL on the warning. */
#define STEPS_X 437

/* Four long steps before the power-up do not fit the warning: it shows
 * none cut, HOLD TO RUN does not count until ALL STEPS has been opened and
 * closed over it, and then it does.  The steps are read again for the next
 * warning. */
TEST_CASE(the_hold_needs_every_pre_power_step_read)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_POWER,
          "Fit the programming jumper on the two gold contacts beside the "
          "motor leads, with the receiver lead plugged in first.", 0u,
          NULL, false },
        { ESC_MANUAL_BEFORE_POWER,
          "Set the transmitter's throttle trim to its middle and its "
          "throttle curve to linear before the ESC is powered at all.", 0u,
          NULL, false },
        { ESC_MANUAL_BEFORE_POWER,
          "Connect the motor and fix it to the bench so that it cannot "
          "turn its leads off the contacts if it starts by itself.", 0u,
          NULL, false },
        { ESC_MANUAL_BEFORE_POWER,
          "Take any propeller or pinion off the motor shaft and keep "
          "hands and tools clear of the motor for the whole run.", 0u,
          NULL, false },
    };
    fresh();
    esc_profiles_clear_overrides();
    esc_profile_t card = *esc_profiles_find("sunrise-pro");
    card.id = "card-steps";
    card.automatable = ESC_AUTO_ASSISTED;
    card.automatable_note = "Steps before power.";
    card.manual = k;
    card.manual_count = 4;
    CHECK(esc_profiles_override(&card, NULL));
    open_profile("card-steps");
    tap(CANCEL_X, HOLD_Y);                   /* the first opening */
    tap(STEP_UP_X, STEP_CY(1));
    tap(WRITE_X, BTN_CY);                    /* the warning */
    draws();
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);      /* not yet */
    tap(STEPS_X, HOLD_Y);                    /* ALL STEPS */
    CHECK(programmer_screen_stick_hand_shown());
    draws();
    hold_for(2.25f);                         /* under the pop-up: nothing */
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    tap(CANCEL_X, HOLD_Y);                   /* OK: back to the warning */
    CHECK(!programmer_screen_stick_hand_shown());
    draws();
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    scr->leave();

    /* A new warning asks for them again. */
    fresh();
    open_profile("card-steps");
    tap(CANCEL_X, HOLD_Y);                   /* the first opening */
    tap(STEP_UP_X, STEP_CY(1));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    tap(CANCEL_X, HOLD_Y);                   /* CANCEL the warning */
    tap(WRITE_X, BTN_CY);
    tap(STEPS_X, HOLD_Y);
    tap(CANCEL_X, HOLD_Y);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    scr->leave();
    esc_profiles_clear_overrides();

    /* kontronik-jazz's one short step fits: no ALL STEPS needed. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    tap(WRITE_X, BTN_CY);
    tap(STEPS_X, HOLD_Y);                    /* nothing there */
    CHECK(!programmer_screen_stick_hand_shown());
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
}

/* A family whose models need different voltages, with the SUPPLY cap
 * between them: 3SL's 6-cell models (7.2 V) run, its 14-cell ones
 * (16.8 V) do not.  The row, the pop-up, the page and RUN agree for the
 * model tapped, not for the family's lowest. */
TEST_CASE(the_model_tapped_is_the_one_judged)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 12.0f);
    supply_screen_limits_changed();
    programmer_screen_bench(0u, false, 0u, 0u, false);
    tap(TILE_CX(2), TILE_CY);
    open_maker("Kontronik");
    /* The 14-cell model: refused on its row, and the steps say why. */
    open_model("kontronik-3sl", "3SL 25-14-32");
    CHECK(programmer_screen_stick_page() == NULL);
    CHECK(programmer_screen_stick_hand_shown());
    const char *why = programmer_screen_stick_hand_why();
    CHECK(why != NULL && strstr(why, "16.8") != NULL);
    draws();
    tap(CANCEL_X, HOLD_Y);
    /* A 6-cell model: opens, its steps say the run asks for them, and RUN
     * runs it at its own 7.2 V. */
    open_model("kontronik-3sl", "3SL 25-6-18");
    const esc_profile_t *p = programmer_screen_stick_page();
    CHECK(p != NULL && strcmp(p->id, "kontronik-3sl") == 0);
    CHECK(programmer_screen_stick_hand_shown());
    CHECK(programmer_screen_stick_hand_why() == NULL);
    tap(CANCEL_X, HOLD_Y);
    tap(HAND_X, HAND_Y);                     /* the button: the same model */
    CHECK(programmer_screen_stick_hand_shown());
    CHECK(programmer_screen_stick_hand_why() == NULL);
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));              /* mode 1 */
    supply_reads_off();                      /* the jumper on */
    tap(WRITE_X, BTN_CY);
    draws();                                 /* the warning */
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    CHECK_EQ(programmer_screen_stick()->out.supply_mv, 7200u);
    scr->leave();
}

/* A card profile with four long steps after programming: they cannot fit
 * the result's two lines, which count them; the steps open by themselves
 * when the run ends, list every one, and reopen from MANUAL INTERVENTION
 * REQUIRED on the result. */
TEST_CASE(every_step_after_programming_is_shown)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Remove the programming jumper from the two gold contacts before "
          "the model is flown, or the ESC enters its menu again.", 0u,
          NULL, false },
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Refit the heat shrink over the programming contacts and the "
          "button so that no conductive dirt reaches them in use.", 0u,
          NULL, false },
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Disconnect the bench supply, then reconnect the flight battery "
          "and check the stored mode on the ESC's start-up tones.", 0u,
          NULL, false },
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Run the motor without a propeller at low throttle once and check "
          "the direction and the brake before the first flight.", 0u,
          NULL, false },
    };
    for (size_t i = 0; i < 4; ++i) {
        CHECK(strlen(k[i].action) > 100u
              && strlen(k[i].action) <= ESC_MANUAL_ACTION_MAX);
    }
    fresh();
    esc_profiles_clear_overrides();
    esc_profile_t card = *esc_profiles_find("sunrise-pro");
    card.id = "card-after";
    card.automatable = ESC_AUTO_ASSISTED;
    card.automatable_note = "Steps after programming.";
    card.manual = k;
    card.manual_count = 4;
    CHECK(esc_profiles_override(&card, NULL));
    open_profile("card-after");
    const esc_profile_t *p = programmer_screen_stick_page();
    CHECK(p != NULL && strcmp(p->id, "card-after") == 0);
    CHECK(programmer_screen_stick_hand_shown());   /* the first opening */
    draws();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(1));              /* timing: automatic */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    rig_run(&r, 300000u);
    CHECK_EQ(programmer_screen_stick()->phase, ESC_STICK_DONE);
    /* Ended: every step after programming shows by itself. */
    CHECK(programmer_screen_stick_hand_shown());
    draws();
    tap(CANCEL_X, HOLD_Y);                   /* OK: the result */
    CHECK(!programmer_screen_stick_hand_shown());
    draws();                                 /* the count, and the button */
    tap(HAND_X, HAND_Y);
    CHECK(programmer_screen_stick_hand_shown());
    tap(CANCEL_X, HOLD_Y);
    tap(WRITE_X, BTN_CY);                    /* OK on the result */
    CHECK(!programmer_screen_stick_hand_shown());
    esc_profiles_clear_overrides();

    /* A profile without such steps: no pop-up when the run ends. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    rig_start(&r);
    rig_run(&r, 300000u);
    CHECK_EQ(programmer_screen_stick()->phase, ESC_STICK_DONE);
    CHECK(!programmer_screen_stick_hand_shown());
}

/* A supply that reads live -- its output on, its own state on, or current
 * through it -- refuses RUN, manual steps or not; the warning asks a step
 * at an unpowered ESC, and HOLD TO RUN counts, only while it reads off. */
TEST_CASE(no_step_at_the_esc_while_the_supply_reads_live)
{
    static const struct { bool output; supply_mode_t mode; float i; } k[] = {
        { true,  SUPPLY_MODE_CV,  0.0f },    /* on, as asked          */
        { false, SUPPLY_MODE_CV,  0.0f },    /* off asked, module on  */
        { false, SUPPLY_MODE_OFF, 0.05f },   /* 50 mA through it      */
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = 1u;
        st.output = k[i].output;
        st.mode = k[i].mode;
        st.i = k[i].i;
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        programmer_screen_supply(&st);
        scr->tick(0.02f);
        tap(WRITE_X, BTN_CY);                /* RUN: refused */
        hold_for(2.25f);
        CHECK_EQ(programmer_screen_stick_runs(), 0u);
        draws();                             /* the note says why */
    }

    /* kontronik-jazz: the jumper is fitted on the warning.  No fresh
     * reading: RUN is refused, the warning never shows its step. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    programmer_screen_bench(300u + ESC_STICK_STALE_MS + 1u, false, 0u, 0u,
                            false);
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    draws();                                 /* the note says why */
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    /* Reading off: RUN, the step shows and the hold runs it. */
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    draws();
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 1u);
    scr->leave();

    /* Off, then live again during the hold: the hold ends, no run. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    press(TOUCH_EVENT_DOWN, HOLD_X, HOLD_Y);
    scr->tick(0.5f);
    supply_state_t on;
    memset(&on, 0, sizeof(on));
    on.samples = 9u;
    on.taken_ms = 350u;
    on.mode = SUPPLY_MODE_CV;
    on.online = true;
    on.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    programmer_screen_supply(&on);
    for (int i = 0; i < 10; ++i) {
        scr->tick(0.25f);
    }
    press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);

    /* A run that ends with the supply still on: the result and the steps
     * after it say not to touch the ESC. */
    fresh();
    esc_profiles_clear_overrides();
    static const esc_manual_t after[] = {
        { ESC_MANUAL_AFTER_PROGRAMMING, "Remove the jumper.", 0u, NULL,
          false },
    };
    esc_profile_t card = *esc_profiles_find("sunrise-pro");
    card.id = "card-live";
    card.automatable = ESC_AUTO_ASSISTED;
    card.automatable_note = "A step after programming.";
    card.manual = after;
    card.manual_count = 1;
    CHECK(esc_profiles_override(&card, NULL));
    open_profile("card-live");
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(1));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    for (int i = 0; i < 20000; ++i) {
        rig_step(&r);
    }
    r.off_lag = 100000u;                     /* the module stays on */
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    for (int i = 0; i < 1000; ++i) {
        rig_step(&r);
    }
    CHECK(!esc_stick_running(programmer_screen_stick()));
    CHECK(programmer_screen_stick_hand_shown());
    CHECK(!programmer_screen_stick_supply_reads_off());
    draws();
    r.off_lag = 0u;                          /* off now */
    for (int i = 0; i < 1000; ++i) {
        rig_step(&r);
    }
    CHECK(programmer_screen_stick_supply_reads_off());
    draws();
    esc_profiles_clear_overrides();

    /* A supply that does not answer is not known off: RUN refused, and no
     * stick moves on a module that may be on. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_state_t gone;
    memset(&gone, 0, sizeof(gone));
    gone.samples = 9u;
    gone.taken_ms = 300u;
    gone.online = false;
    gone.mode = SUPPLY_MODE_OFF;
    programmer_screen_supply(&gone);
    scr->tick(0.02f);
    CHECK(!programmer_screen_stick_supply_reads_off());
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
    draws();

    /* A reading too old to say the supply is off now. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    supply_reads_off();
    programmer_screen_bench(300u + ESC_STICK_STALE_MS + 1u, false, 0u, 0u,
                            false);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(programmer_screen_stick_runs(), 0u);
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
    RUN(the_header_fits_at_the_registrys_most);
    RUN(manual_steps_show_once_by_themselves_and_on_their_button);
    RUN(a_row_that_does_not_run_shows_its_manual_steps);
    RUN(a_run_asks_for_its_manual_step_and_goes_on_with_done);
    RUN(abort_on_the_prompt_ends_the_run);
    RUN(a_manual_step_shows_in_the_language_showing);
    RUN(makers_are_alphabetical_and_count_their_models);
    RUN(models_go_by_current_then_voltage_then_name);
    RUN(a_model_row_opens_its_family_at_its_own_voltage);
    RUN(the_search_finds_on_both_levels_and_back_keeps_it);
    RUN(a_card_profile_joins_its_maker);
    RUN(the_model_tapped_is_the_one_judged);
    RUN(the_page_shows_the_entry_the_run_waits);
    RUN(the_hold_needs_every_pre_power_step_read);
    RUN(a_maker_at_its_most_models_lists_every_one);
    RUN(every_step_after_programming_is_shown);
    RUN(no_step_at_the_esc_while_the_supply_reads_live);
    return test_summary("programmer");
}
