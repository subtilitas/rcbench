/*
 * ESC STICK's pre-power steps and the run page: the hold that needs every
 * step read, the steps after programming and the phase tap.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* SUN PLUS mode 4 waits 5 s from power-on, the profile's entry 2 s: the
 * page shows 5 s when mode 4 is picked, through the run, and on the result
 * after the change is made. */
TEST_CASE(the_page_shows_the_entry_the_run_waits)
{
    fresh();
    open_profile("kontronik-sun-plus");
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    CHECK_EQ(esc_stick_panel_entry_shown(), 2000u);
    for (int i = 0; i < 4; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* KEEP, 1, 2, 3, 4 */
    }
    CHECK_EQ(esc_stick_panel_entry_shown(), 5000u);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK_EQ(esc_stick_panel_entry_shown(), 5000u);
    esc_sim_hand(&r.sim, r.now);
    for (uint32_t i = 0; i < 240000u && run->phase != ESC_STICK_STORE;
         ++i) {
        rig_step(&r);
    }
    CHECK_EQ(run->phase, ESC_STICK_STORE);
    CHECK_EQ(esc_stick_entry_ms(run), 2000u);    /* the next change's */
    CHECK_EQ(esc_stick_panel_entry_shown(), 5000u);
    rig_run(&r, 300000u);
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 4);
    CHECK_EQ(esc_stick_panel_entry_shown(), 5000u);
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
          NULL, false, false },
        { ESC_MANUAL_BEFORE_POWER,
          "Set the transmitter's throttle trim to its middle and its "
          "throttle curve to linear before the ESC is powered at all.", 0u,
          NULL, false, false },
        { ESC_MANUAL_BEFORE_POWER,
          "Connect the motor and fix it to the bench so that it cannot "
          "turn its leads off the contacts if it starts by itself.", 0u,
          NULL, false, false },
        { ESC_MANUAL_BEFORE_POWER,
          "Take any propeller or pinion off the motor shaft and keep "
          "hands and tools clear of the motor for the whole run.", 0u,
          NULL, false, false },
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
    CHECK_EQ(esc_stick_panel_runs(), 0u);      /* not yet */
    tap(STEPS_X, HOLD_Y);                    /* ALL STEPS */
    CHECK(esc_stick_panel_hand_shown());
    draws();
    hold_for(2.25f);                         /* under the pop-up: nothing */
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    tap(CANCEL_X, HOLD_Y);                   /* OK: back to the warning */
    CHECK(!esc_stick_panel_hand_shown());
    draws();
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    scr->leave();

    /* A new warning asks for them again. */
    fresh();
    open_profile("card-steps");
    tap(CANCEL_X, HOLD_Y);                   /* the first opening */
    tap(STEP_UP_X, STEP_CY(1));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    tap(CANCEL_X, HOLD_Y);                   /* CANCEL the warning */
    tap(WRITE_X, BTN_CY);
    tap(STEPS_X, HOLD_Y);
    tap(CANCEL_X, HOLD_Y);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    scr->leave();
    esc_profiles_clear_overrides();

    /* kontronik-jazz's one short step fits: no ALL STEPS needed. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    tap(WRITE_X, BTN_CY);
    tap(STEPS_X, HOLD_Y);                    /* nothing there */
    CHECK(!esc_stick_panel_hand_shown());
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
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
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Kontronik");
    /* The 14-cell model: refused on its row, and the steps say why. */
    open_model("kontronik-3sl", "3SL 25-14-32");
    CHECK(esc_stick_panel_page() == NULL);
    CHECK(esc_stick_panel_hand_shown());
    const char *why = esc_stick_panel_hand_why();
    CHECK(why != NULL && strstr(why, "16.8") != NULL);
    draws();
    tap(CANCEL_X, HOLD_Y);
    /* A 6-cell model: opens, its steps say the run asks for them, and RUN
     * runs it at its own 7.2 V. */
    open_model("kontronik-3sl", "3SL 25-6-18");
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL && strcmp(p->id, "kontronik-3sl") == 0);
    CHECK(esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_hand_why() == NULL);
    tap(CANCEL_X, HOLD_Y);
    tap(HAND_X, HAND_Y);                     /* the button: the same model */
    CHECK(esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_hand_why() == NULL);
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));              /* mode 1 */
    supply_reads_off();                      /* the jumper on */
    tap(WRITE_X, BTN_CY);
    draws();                                 /* the warning */
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    CHECK_EQ(esc_stick_panel_stick()->out.supply_mv, 7200u);
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
          NULL, false, false },
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Refit the heat shrink over the programming contacts and the "
          "button so that no conductive dirt reaches them in use.", 0u,
          NULL, false, false },
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Disconnect the bench supply, then reconnect the flight battery "
          "and check the stored mode on the ESC's start-up tones.", 0u,
          NULL, false, false },
        { ESC_MANUAL_AFTER_PROGRAMMING,
          "Run the motor without a propeller at low throttle once and check "
          "the direction and the brake before the first flight.", 0u,
          NULL, false, false },
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
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL && strcmp(p->id, "card-after") == 0);
    CHECK(esc_stick_panel_hand_shown());   /* the first opening */
    draws();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(1));              /* timing: automatic */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    rig_run(&r, 300000u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);
    /* Ended: every step after programming shows by itself. */
    CHECK(esc_stick_panel_hand_shown());
    draws();
    tap(CANCEL_X, HOLD_Y);                   /* OK: the result */
    CHECK(!esc_stick_panel_hand_shown());
    draws();                                 /* the count, and the button */
    tap(HAND_X, HAND_Y);
    CHECK(esc_stick_panel_hand_shown());
    tap(CANCEL_X, HOLD_Y);
    tap(WRITE_X, BTN_CY);                    /* OK on the result */
    CHECK(!esc_stick_panel_hand_shown());
    esc_profiles_clear_overrides();

    /* The run ends on a supply sample, and a tap queued in the same
     * frame lands on ABORT's place -- the result's OK -- before the tick:
     * the steps still open by themselves. */
    fresh();
    CHECK(esc_profiles_override(&card, NULL));
    open_profile("card-after");
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(1));
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 300000u && run->phase != ESC_STICK_VALUES
                         && esc_stick_running(run); ++i) {
        rig_step(&r);
    }
    CHECK_EQ(run->phase, ESC_STICK_VALUES);
    CHECK(!esc_stick_panel_hand_shown());
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.samples = (uint16_t)++r.seq;
    st.taken_ms = r.now;
    st.stamped = true;
    st.output = true;
    st.mode = SUPPLY_MODE_OFF;               /* the module went off */
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    esc_stick_panel_supply(&st);
    CHECK(!esc_stick_running(run));
    tap(WRITE_X, BTN_CY);                    /* meant for ABORT: OK now */
    scr->tick(0.001f);
    CHECK(esc_stick_panel_hand_shown());
    tap(CANCEL_X, HOLD_Y);
    esc_profiles_clear_overrides();

    /* A profile without such steps: no pop-up when the run ends. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    rig_start(&r);
    rig_run(&r, 300000u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);
    CHECK(!esc_stick_panel_hand_shown());
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
        esc_stick_panel_supply(&st);
        scr->tick(0.02f);
        tap(WRITE_X, BTN_CY);                /* RUN: refused */
        hold_for(2.25f);
        CHECK_EQ(esc_stick_panel_runs(), 0u);
        draws();                             /* the note says why */
    }

    /* kontronik-jazz: the jumper is fitted on the warning.  No fresh
     * reading: RUN is refused, the warning never shows its step. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    esc_stick_panel_bench(300u + ESC_STICK_STALE_MS + 1u, false, 0u, 0u,
                            false);
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    draws();                                 /* the note says why */
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    /* Reading off: RUN, the step shows and the hold runs it. */
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    draws();
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
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
    on.stamped = true;
    on.mode = SUPPLY_MODE_CV;
    on.online = true;
    on.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    esc_stick_panel_supply(&on);
    for (int i = 0; i < 10; ++i) {
        scr->tick(0.25f);
    }
    press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
    CHECK_EQ(esc_stick_panel_runs(), 0u);

    /* A run that ends with the supply still on: the result and the steps
     * after it say not to touch the ESC. */
    fresh();
    esc_profiles_clear_overrides();
    static const esc_manual_t after[] = {
        { ESC_MANUAL_AFTER_PROGRAMMING, "Remove the jumper.", 0u, NULL,
          false, false },
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
    CHECK(!esc_stick_running(esc_stick_panel_stick()));
    CHECK(esc_stick_panel_hand_shown());
    CHECK(!esc_stick_panel_supply_reads_off());
    draws();
    r.off_lag = 0u;                          /* off now */
    for (int i = 0; i < 1000; ++i) {
        rig_step(&r);
    }
    CHECK(esc_stick_panel_supply_reads_off());
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
    gone.stamped = true;
    gone.online = false;
    gone.mode = SUPPLY_MODE_OFF;
    esc_stick_panel_supply(&gone);
    scr->tick(0.02f);
    CHECK(!esc_stick_panel_supply_reads_off());
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    draws();

    /* A reading too old to say the supply is off now. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    tap(STEP_UP_X, STEP_CY(0));
    supply_reads_off();
    esc_stick_panel_bench(300u + ESC_STICK_STALE_MS + 1u, false, 0u, 0u,
                            false);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
}

/* The frame drawn with the tap off, to count what the readout adds. */
static gfx_color_t *tone_base;

/* Pixels of the run page's rows below its current line, left of the stack
 * light -- where the phase tap's readout is drawn -- that differ from the
 * frame drawn with the tap off. */
static int tone_rows_lit(void)
{
    int n = 0;
    for (int y = 284; y < 352; ++y) {
        for (int x = 0; x < 700; ++x) {
            n += (fb[y * W + x] != tone_base[y * W + x]) ? 1 : 0;
        }
    }
    return n;
}

/* The same rows as one number, to tell two frames apart or alike. */
static uint32_t tone_rows_hash(void)
{
    uint32_t h = 2166136261u;
    for (int y = 284; y < 352; ++y) {
        for (int x = 0; x < 700; ++x) {
            h = (h ^ (uint32_t)fb[y * W + x]) * 16777619u;
        }
    }
    return h;
}

/* Pixels of the same rows in the warning colour. */
static int tone_rows_warn(void)
{
    const gfx_color_t warn = ui_theme_color(UI_C_WARN);
    int n = 0;
    for (int y = 284; y < 352; ++y) {
        for (int x = 0; x < 700; ++x) {
            n += (fb[y * W + x] == warn) ? 1 : 0;
        }
    }
    return n;
}

/* The tap's readout fed to the screen and one frame drawn after it. */
static int tone_drawn(rig_t *r, const tone_readout_t *t, int buf)
{
    esc_stick_panel_tone(t);
    rig_run(r, 2u);                 /* a tick: the page repaints on a change */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, buf);
    if (tone_base == NULL) {
        tone_base = malloc((size_t)W * H * sizeof(gfx_color_t));
        REQUIRE_ALLOC(tone_base);
        memcpy(tone_base, fb, (size_t)W * H * sizeof(gfx_color_t));
    }
    return tone_rows_lit();
}

/* A run's page shows the phase tap while the tap is enabled on SETUP, in
 * each state it can be in, and nothing in its place while it is not. */
TEST_CASE(the_run_page_shows_the_phase_tap_while_it_is_enabled)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    for (int i = 0; i < 300000
         && esc_stick_panel_stick()->phase != ESC_STICK_ITEMS; ++i) {
        rig_step(&r);
    }
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_ITEMS);

    tone_readout_t t;
    memset(&t, 0, sizeof(t));
    int buf = 0;
    const int off = tone_drawn(&r, &t, buf++ & 1);
    CHECK_EQ(off, 0);

    /* Every state with no beep yet draws its head and "none yet". */
    static const tone_state_t k_states[] = {
        TONE_STATE_NO_PAGE, TONE_STATE_WAITING, TONE_STATE_RUNNING,
        TONE_STATE_REFUSED, TONE_STATE_STOPPED,
    };
    for (size_t i = 0; i < sizeof(k_states) / sizeof(k_states[0]); ++i) {
        t.state = k_states[i];
        const int last = tone_drawn(&r, &t, buf++ & 1);
        CHECK(last > off + 200);
    }
    /* The head is in the warning colour for a state the tap does not run
     * in and for an overrun a read gives, and in no other. */
    static const struct {
        tone_state_t state;
        bool read, overrun, warn;
    } k_warn[] = {
        { TONE_STATE_NO_PAGE, false, false, true  },
        { TONE_STATE_WAITING, false, false, false },
        { TONE_STATE_WAITING, false, true,  false },
        { TONE_STATE_RUNNING, true,  false, false },
        { TONE_STATE_RUNNING, true,  true,  true  },
        { TONE_STATE_RUNNING, false, true,  false },
        { TONE_STATE_REFUSED, true,  false, true  },
        { TONE_STATE_STOPPED, true,  false, true  },
        { TONE_STATE_STOPPED, false, false, true  },
    };
    for (size_t i = 0; i < sizeof(k_warn) / sizeof(k_warn[0]); ++i) {
        tone_readout_t w;
        memset(&w, 0, sizeof(w));
        w.state = k_warn[i].state;
        w.read = k_warn[i].read;
        w.overrun = k_warn[i].overrun;
        (void)tone_drawn(&r, &w, buf++ & 1);
        CHECK_EQ(tone_rows_warn() > 0, k_warn[i].warn);
    }
    /* Running with the capture overrun says so, in the warning colour,
     * while a status read speaks for it; an overrun no read speaks for
     * draws nothing. */
    t.state = TONE_STATE_RUNNING;
    t.read = true;
    (void)tone_drawn(&r, &t, buf++ & 1);
    const uint32_t running = tone_rows_hash();
    t.overrun = true;
    CHECK(tone_drawn(&r, &t, buf++ & 1) > off + 200);
    CHECK(tone_rows_hash() != running);
    t.read = false;
    t.overrun = false;
    (void)tone_drawn(&r, &t, buf++ & 1);
    const uint32_t unread = tone_rows_hash();
    t.overrun = true;
    (void)tone_drawn(&r, &t, buf++ & 1);
    CHECK_EQ(tone_rows_hash(), unread);
    t.read = true;
    t.overrun = false;

    /* The window's pitch, the counts and four beeps add to it, and a new
     * reading repaints. */
    const int bare = tone_drawn(&r, &t, buf++ & 1);
    t.win_freq_dhz = 15234u;
    t.lost = 12u;
    t.missed = 6u;
    t.n = TONE_LINK_SHOWN;
    for (unsigned i = 0u; i < TONE_LINK_SHOWN; ++i) {
        t.beeps[i].seq = (uint16_t)(65535u - i);
        t.beeps[i].len_dms = 6553u;
        t.beeps[i].freq_dhz = 6553u;
    }
    CHECK(tone_drawn(&r, &t, buf++ & 1) > bare);

    /* A null readout changes nothing. */
    esc_stick_panel_tone(NULL);
    CHECK(tone_drawn(&r, &t, buf++ & 1) > bare);

    /* No status read speaks for the page: a readout that still carries an
     * overrun, a pitch and a lost count draws as one that carries none --
     * the pitch and the lost count read "--", and the head is not in the
     * warning colour.  The missed count and the beeps are the panel's own
     * and stay. */
    tone_readout_t none = t;
    none.state = TONE_STATE_WAITING;
    none.read = false;
    none.overrun = false;
    none.tone = false;
    none.beep = false;
    none.win_freq_dhz = 0u;
    none.win_periods = 0u;
    none.lost = 0u;
    none.glitches = 0u;
    (void)tone_drawn(&r, &none, buf++ & 1);
    const uint32_t waiting = tone_rows_hash();
    tone_readout_t left = none;
    left.overrun = true;
    left.tone = true;
    left.beep = true;
    left.win_freq_dhz = 15234u;
    left.win_periods = 12u;
    left.lost = 7u;
    left.glitches = 5u;
    (void)tone_drawn(&r, &left, buf++ & 1);
    CHECK_EQ(tone_rows_hash(), waiting);
    /* The same values with a read behind them are drawn. */
    left.read = true;
    (void)tone_drawn(&r, &left, buf++ & 1);
    CHECK(tone_rows_hash() != waiting);
    /* A lost count of 0 that a read gives is not the "--" of none. */
    none.read = true;
    (void)tone_drawn(&r, &none, buf++ & 1);
    CHECK(tone_rows_hash() != waiting);
    /* A read pitch of 0 is "--", one of 0.1 Hz is "0.1 Hz". */
    tone_readout_t p;
    memset(&p, 0, sizeof(p));
    p.state = TONE_STATE_RUNNING;
    p.read = true;
    const int dashes = tone_drawn(&r, &p, buf++ & 1);
    p.win_freq_dhz = 1u;
    CHECK(tone_drawn(&r, &p, buf++ & 1) > dashes);

    /* Off again: nothing in its place. */
    memset(&t, 0, sizeof(t));
    CHECK_EQ(tone_drawn(&r, &t, buf++ & 1), 0);
    rig_run(&r, 300000u);
    free(tone_base);
    tone_base = NULL;
}

int main(void)
{
    RUN(the_model_tapped_is_the_one_judged);
    RUN(the_page_shows_the_entry_the_run_waits);
    RUN(the_hold_needs_every_pre_power_step_read);
    RUN(every_step_after_programming_is_shown);
    RUN(no_step_at_the_esc_while_the_supply_reads_live);
    RUN(the_run_page_shows_the_phase_tap_while_it_is_enabled);
    return test_summary("esc_stick_panel_hold");
}
