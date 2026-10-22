/*
 * The limit run through the SERVO screen and the overlay of the programmer
 * (shared/ui/kst_panel.c, shared/servo/servo_limrun.c): the LIMITS page, the
 * refusals before a drive, and the sessions and backups around a run on the
 * programming port.  The bench is kst_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"
#include "settings_sensors.h"

/* ------------------------------------------------- the SERVO screen's limit run */

/*
 * The limit run on the rig: the servo on programming port 1 behind
 * servo_sim's current model, the horn where its registers put it for the
 * pulse (kst_sim.h sim_horn_us(): linear from NEUTRAL to PULSE LOWER and
 * PULSE UPPER, LEFT ANGLE and RIGHT ANGLE the full travel at 4 us a count;
 * the bench image's 150 counts are 600 us, past both stops at 1150 and
 * 1880 us); the INA3221's CH1 windows from it every 50 ms; and a bench
 * that goes SAFE and ARMED as the screen asks, an ARM dropped while the
 * programmer holds the port, as the panel's control task drops it.  The
 * SERVO screen's PULSE MIN and MAX are STANDARD PWM's, 1000 and 2000 us,
 * the bench image's PULSE LOWER and UPPER 900 and 2100 us: a run with
 * SERVO PULSES at KEEP is refused after the read, one at 1000-2000 runs.
 */
#define LR_MODE_X  100          /* the LIMIT RUN settings' left column */
#define LR_MODE_Y  SV_LIST_ROW_Y(0)   /* LIMIT RUN, across both columns */
#define LR_PULSE_Y SV_LIST_ROW_Y(1)   /* SERVO PULSES, the left column */

/* TEST, and the LIMIT RUN card's own settings opened. */
static void lr_run_list(void)
{
    screen_tap(SV_TAB_TEST_X, BTN_Y);
    screen_tap(SV_RUN_SET_X, SV_RUN_SET_Y(1));
}

static struct {
    servo_sim_t sv;
    uint16_t cmd;
    bool armed;
    bool ina;                   /* the windows arrive */
    bool pin;                   /* the target is the bench's GP2 */
    uint16_t rail_mv;           /* the windows' rail; 0 is 6000 mV */
    uint16_t win_no;
    uint32_t win_end;
    float sum;
    unsigned n;
    unsigned disarms, arms, dropped;
    unsigned opens_txt, ends;
    char report[8192];
    size_t len;
} lr;

static void lr_drain(void)
{
    const char *text = NULL;

    for (;;) {
        const servo_test_out_t o = servo_screen_test_peek(&text);

        if (o == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (o == SERVO_TEST_OUT_OPEN_TXT) {
            lr.opens_txt++;
        } else if (o == SERVO_TEST_OUT_END) {
            lr.ends++;
        } else if (o == SERVO_TEST_OUT_TXT) {
            const size_t n = strlen(text);

            if (lr.len + n + 2u < sizeof(lr.report)) {
                memcpy(lr.report + lr.len, text, n);
                lr.len += n;
                lr.report[lr.len++] = '\n';
                lr.report[lr.len] = '\0';
            }
        }
        servo_screen_test_pop();
    }
}

/* What the panel does with the screen's commands. */
static void lr_commands(void)
{
    servo_cmd_t c;

    while (servo_screen_take(&c)) {
        if (c.kind == SERVO_CMD_POSITION) {
            lr.cmd = c.value_us;
        } else if (c.kind == SERVO_CMD_RELEASE) {
            lr.cmd = 1500u;
        } else if (c.kind == SERVO_CMD_DISARM) {
            lr.disarms++;
            lr.armed = false;
        } else if (c.kind == SERVO_CMD_ARM) {
            if (kst_panel_holds_port()
                || kst_prog_state(M) != KST_PROG_PWM) {
                lr.dropped++;
            } else {
                lr.arms++;
                lr.armed = true;
            }
        }
    }
    g_rig.safe = !lr.armed;
    servo_screen_set_armed(lr.armed);
}

static void lr_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        float amps;

        rig_frame();
        amps = servo_sim_step(&lr.sv,
                              sim_horn_cmd(g_rig.sim.reg, lr.cmd, 4.0f,
                                           false),
                              g_rig.ms);
        lr.sum += amps;
        lr.n++;
        if ((int32_t)(g_rig.ms - lr.win_end) >= 0) {
            const int ma = (int)(lr.sum / (float)lr.n * 1000.0f + 0.5f);
            const sense_link_win_t w = {
                .number = ++lr.win_no, .current = true, .voltage = true,
                .mean_ma = (int16_t)ma, .max_ma = (int16_t)ma,
                .min_ma = (int16_t)ma,
                .mean_mv = lr.rail_mv != 0u ? lr.rail_mv : 6000u,
                .min_mv = lr.rail_mv != 0u ? lr.rail_mv : 6000u,
                .taken_ms = g_rig.ms, .stamped = true,
            };

            lr.win_end += 50u;
            lr.sum = 0.0f;
            lr.n = 0u;
            if (lr.ina) {
                servo_screen_window(&w, SERVO_SOURCE_INA3221, 2u);
            }
        }
        if (g_rig.ms % 20u == 0u) {
            servo_screen_clock(g_rig.ms);
            if (lr.pin) {
                servo_screen_set_drive(0u, 2u, true);
            } else {
                servo_screen_set_drive(1u + RIG_PORT, RIG_PIN,
                                       kst_prog_state(M) == KST_PROG_PWM
                                           && !kst_panel_holds_port());
            }
            lr_commands();
            scr->tick(0.02f);
            servo_screen_service();
            lr_drain();
        }
    }
}

/* The SERVO screen on the rig, armed, the INA3221 the rail's meter, the
 * port in PWM with the servo on it, REVERSION as @p reversion. */
static void lr_fresh(bool reversion)
{
    servo_sim_cfg_t sc;
    kst_image_t img;

    fresh_screen();
    memset(&lr, 0, sizeof(lr));
    img = rig_servo();
    (void)kst_field_set(&img, KST_F_REVERSION, reversion ? 1u : 0u);
    memcpy(g_rig.sim.reg, img.r, sizeof(img.r));
    servo_sim_defaults(&sc);                /* stops at 1150 and 1880 us */
    servo_sim_init(&lr.sv, &sc);
    lr.sv.position_us = 1500.0f;
    lr.cmd = 1500u;
    lr.ina = true;
    lr.armed = true;
    settings_set(SET_INA_FIT(0), 1.0f);
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    link_up();
    lr.win_end = g_rig.ms + 50u;
    lr_ms(400u);
}

/* TEST, and START LIMIT RUN on its card held for 2.3 s. */
static void lr_hold_start(void)
{
    screen_tap(SV_TAB_TEST_X, BTN_Y);
    ev(SV_START_X, SV_START_Y(1), TOUCH_EVENT_DOWN, 1);
    lr_ms(2300u);
    ev(SV_START_X, SV_START_Y(1), TOUCH_EVENT_UP, 1);
    lr_ms(20u);
}

/* The LIMIT RUN settings: LIMIT RUN to SEARCH+PROG, SERVO PULSES to
 * 1000-2000 where @p pulses; the home tag back to TEST, and START LIMIT
 * RUN held for 2.3 s. */
static void lr_start_with(bool pulses)
{
    lr_run_list();
    screen_tap(LR_MODE_X, LR_MODE_Y);
    if (pulses) {
        screen_tap(LR_MODE_X, LR_PULSE_Y);
    }
    CHECK(scr->back());
    lr_hold_start();
}

static void lr_start(void)
{
    lr_start_with(true);
}

static void lr_run_out(void)
{
    for (int i = 0; i < 1200 && servo_screen_limit_running(); ++i) {
        lr_ms(500u);
    }
    lr_ms(2000u);
}

/* SEARCH+PROG with SERVO PULSES at 1000-2000, REVERSION off and on: PULSE
 * LOWER and PULSE UPPER at 1000.0 and 2000.0 us, both ends found bound,
 * LEFT ANGLE and RIGHT ANGLE lowered so neither binds, the servo's full
 * travel inside both stops, the bench back to ARMED after every session,
 * one backup file, PASS in the report with the writes made, and RESTORE
 * putting the servo back as the run found it, 900.0 and 2100.0 us. */
TEST_CASE(a_limit_run_programs_the_servo_on_the_programming_port)
{
    for (int rev = 0; rev < 2; ++rev) {
        int32_t d = 0;

        lr_fresh(rev != 0);
        lr_start();
        CHECK(servo_screen_limit_running());
        lr_run_out();
        CHECK(!servo_screen_limit_running());
        CHECK(strstr(lr.report, "RCBENCH SERVO LIMIT REPORT\n") == lr.report);
        CHECK(strstr(lr.report, "PASS") != NULL);
        CHECK(strstr(lr.report, "Writes:") != NULL);
        CHECK_EQ(lr.opens_txt, 1u);
        CHECK_EQ(lr.ends, 1u);
        CHECK(strstr(lr.report, "Pulse write:    PULSE LOWER 900.0 -> 1000.0 "
                                "us, PULSE UPPER 2100.0 -> 2000.0 us") != NULL);
        CHECK(servo(KST_F_LEFT_ANGLE) < 150u);
        CHECK(servo(KST_F_RIGHT_ANGLE) < 150u);
        CHECK(kst_field_display(KST_F_PULSE_LOWER, servo(KST_F_PULSE_LOWER),
                                &d));
        CHECK_EQ(d, 10000);
        CHECK(kst_field_display(KST_F_PULSE_UPPER, servo(KST_F_PULSE_UPPER),
                                &d));
        CHECK_EQ(d, 20000);
        /* The full travel, at the pulse limits, inside both stops by the
         * model's resolution: a push of 3.0 us past a stop draws less
         * than the +0.15 A a bind needs, and the command rounds to 0.5 us
         * (test_servo_limrun.c, every_stop_position_passes). */
        CHECK(sim_horn_us(g_rig.sim.reg, 1000u, 4.0f, false) > 1150.0f - 3.5f);
        CHECK(sim_horn_us(g_rig.sim.reg, 2000u, 4.0f, false) < 1880.0f + 3.5f);
        /* Every other register as it was. */
        {
            static const kst_field_id_t k_run[] = {
                KST_F_LEFT_ANGLE, KST_F_RIGHT_ANGLE, KST_F_PULSE_LOWER,
                KST_F_PULSE_UPPER,
            };
            kst_image_t was = sim_bench_image(), now = rig_servo();

            (void)kst_field_set(&was, KST_F_REVERSION, rev ? 1u : 0u);
            for (size_t k = 0; k < 4u; ++k) {
                (void)kst_field_set(&was, k_run[k], servo(k_run[k]));
            }
            CHECK(memcmp(was.r, now.r, sizeof(was.r)) == 0);
        }
        CHECK(lr.disarms >= 1u);
        CHECK_EQ(lr.dropped, 0u);
        CHECK(lr.armed);
        CHECK_EQ(g_rig.fc.saves, 1u);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        /* SAFE, PROGRAM, CONNECT: RESTORE offers the servo as the run found
         * it, and puts it back. */
        lr.armed = false;
        lr_ms(40u);
        program_open();
        CHECK(kst_panel_is_open());
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_restore_offered(M, KST_PROG_FROM_RUN));
        if (!kst_prog_write_enabled(M)) {
            enable();
        }
        tap(val(PG_SERVO, 2, 0));
        tap(kst_panel_choice_rect(choice(TR(KP_FROM_RUN))));
        CHECK_EQ(kst_panel_editor(), 3);
        for (int k = 0; k < 4 && kst_panel_editor() == 3; ++k) {
            hold(2.1f);
        }
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
        {
            kst_image_t was = sim_bench_image(), now = rig_servo();

            (void)kst_field_set(&was, KST_F_REVERSION, rev ? 1u : 0u);
            CHECK(memcmp(was.r, now.r, sizeof(was.r)) == 0);
        }
        CHECK(kst_field_display(KST_F_PULSE_LOWER, servo(KST_F_PULSE_LOWER),
                                &d));
        CHECK_EQ(d, 9000);
        CHECK(kst_field_display(KST_F_PULSE_UPPER, servo(KST_F_PULSE_UPPER),
                                &d));
        CHECK_EQ(d, 21000);
    }
}

/* SERVO PULSES at KEEP: the bench's PULSE MIN and MAX, 1000 and 2000 us,
 * do not hold the servo's 900 and 2100 us.  The run reads the servo,
 * gives the port back and ends as FAIL with the bench SAFE: nothing
 * driven, nothing written, both ranges in the report. */
TEST_CASE(a_limit_run_refuses_a_servo_whose_pulses_lie_outside_min_and_max)
{
    lr_fresh(false);
    lr_start_with(false);
    CHECK(servo_screen_limit_running());
    lr_run_out();
    CHECK(!servo_screen_limit_running());
    CHECK(strstr(lr.report, "Result:         FAIL - servo pulses outside "
                            "MIN/MAX\n") != NULL);
    CHECK(strstr(lr.report, "Searched:") == NULL);
    /* The run's LEAVE gives the port back: the box stays on the left
     * card, the programmer does not open over it. */
    CHECK(!kst_panel_holds_port());
    CHECK(!kst_panel_is_open());
    CHECK(strstr(lr.report, "Range:          servo pulses 900.0-2100.0 us "
                            "outside PULSE MIN/MAX 1000-2000 us") != NULL);
    CHECK_EQ(lr.disarms, 1u);
    CHECK_EQ(lr.arms, 0u);
    CHECK(!lr.armed);
    CHECK_EQ(g_rig.fc.saves, 0u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    {
        kst_image_t was = sim_bench_image(), now = rig_servo();

        CHECK(memcmp(was.r, now.r, sizeof(was.r)) == 0);
    }
    CHECK_EQ(lr.cmd, 1500u);
}

/* SEARCH ONLY on a servo that nothing answers on the programming port, a
 * Hitec servo for one: the first CONNECT finds no servo, and the run ends
 * as FAIL with the reason of a read that is no KST or Chaservo servo, in
 * both languages, and not with the session's failure.  Nothing is driven
 * or written. */
TEST_CASE(a_limit_run_on_a_servo_that_does_not_answer_names_the_missing_kst_servo)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        lr_fresh(false);
        ui_text_set_language((ui_lang_t)lang);
        g_rig.sim.powered = false;
        lr_hold_start();
        CHECK(servo_screen_limit_running());
        lr_run_out();
        CHECK(!servo_screen_limit_running());
        CHECK(strstr(lr.report, servo_str_in(ui_servo_table(), SERVO_STR_L_WHY_NOT_KST)) != NULL);
        CHECK(strstr(lr.report, servo_str_in(ui_servo_table(), SERVO_STR_L_WHY_PROG)) == NULL);
        CHECK(strstr(lr.report, "Searched:") == NULL);
        CHECK(!kst_panel_holds_port());
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_EQ(g_rig.fc.saves, 0u);
        CHECK_EQ(lr.cmd, 1500u);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The same run where the wire's driver refuses the CONNECT's frames: a
 * failure of the first CONNECT that is not a missing servo is the
 * session's, in both languages. */
TEST_CASE(a_limit_run_whose_first_connect_fails_otherwise_names_the_session)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        lr_fresh(false);
        ui_text_set_language((ui_lang_t)lang);
        sim_add_fault(&g_rig.sim, SIM_ANY, -1, 0, 1000, SIM_F_START_FAIL, 0);
        lr_hold_start();
        CHECK(servo_screen_limit_running());
        lr_run_out();
        CHECK(!servo_screen_limit_running());
        CHECK(!kst_panel_run_no_servo());
        CHECK(strstr(lr.report, servo_str_in(ui_servo_table(),
                                             SERVO_STR_L_WHY_PROG)) != NULL);
        CHECK(strstr(lr.report, servo_str_in(ui_servo_table(),
                                             SERVO_STR_L_WHY_NOT_KST))
              == NULL);
        CHECK(!kst_panel_holds_port());
        CHECK_EQ(g_rig.fc.saves, 0u);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A run that ends inside a session gives the port back with a LEAVE and
 * ends the programmer's run once that LEAVE is over: through the LEAVE the
 * programmer's run still holds it (its poll says BUSY), after it the run
 * is over (NONE), and the programmer does not open over the box.  A LEAVE
 * that fails leaves the port in programming mode and opens the
 * programmer. */
TEST_CASE(a_run_ended_in_a_session_ends_the_programmers_run_after_its_leave)
{
    bool leaving_seen = false;

    lr_fresh(false);
    lr_start_with(false);
    CHECK(servo_screen_limit_running());
    for (int i = 0; i < 200000 && servo_screen_limit_running(); ++i) {
        lr_ms(1u);
    }
    CHECK(!servo_screen_limit_running());
    for (int i = 0; i < 20000 && kst_prog_state(M) != KST_PROG_PWM; ++i) {
        if (kst_prog_state(M) == KST_PROG_LEAVING) {
            leaving_seen = true;
            CHECK_EQ(kst_panel_run_poll(), KST_PANEL_RUN_BUSY);
            CHECK(!kst_panel_is_open());
        }
        lr_ms(1u);
    }
    CHECK(leaving_seen);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    lr_ms(40u);
    CHECK_EQ(kst_panel_run_poll(), KST_PANEL_RUN_NONE);
    CHECK(!kst_panel_is_open());
    CHECK(strstr(lr.report, "Result:         FAIL - servo pulses outside "
                            "MIN/MAX\n") != NULL);

    /* A LEAVE the control task never takes: the port stays in programming
     * mode, the programmer's run ends, and the programmer opens. */
    lr_fresh(false);
    lr_start_with(false);
    for (int i = 0; i < 200000 && servo_screen_limit_running(); ++i) {
        lr_ms(1u);
    }
    CHECK(!servo_screen_limit_running());
    g_rig.deaf = true;
    for (int i = 0; i < 400 && kst_prog_state(M) == KST_PROG_LEAVING; ++i) {
        CHECK(!kst_panel_is_open());
        lr_ms(500u);
    }
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    lr_ms(40u);
    CHECK_EQ(kst_panel_run_poll(), KST_PANEL_RUN_NONE);
    CHECK(kst_panel_is_open());
    g_rig.deaf = false;
}

/* With no PD mini to set, the servo rail as the INA3221 reads it holds
 * the programmer's 4.8 V: 4799 mV refuses every run on the programming
 * port before anything is driven, 4800 mV starts one. */
TEST_CASE(a_run_on_the_programming_port_needs_4_8_v_on_the_rail)
{
    lr_fresh(false);
    lr.rail_mv = KST_PORT_SUPPLY_MIN_MV - 1u;
    lr_ms(200u);
    lr_start();
    CHECK(!servo_screen_limit_running());
    CHECK_EQ(lr.disarms, 0u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(lr.cmd, 1500u);

    lr_fresh(false);
    lr.rail_mv = KST_PORT_SUPPLY_MIN_MV;
    lr_ms(200u);
    lr_start();
    CHECK(servo_screen_limit_running());
    lr_run_out();
    CHECK(strstr(lr.report, "Result:         PASS") != NULL);
}

/* A PD mini that answers with its output off, the rail fed from
 * elsewhere: the read session's ENTER would switch it on beside that
 * supply, so no run on the programming port starts, and the page says why.
 * With its output on, the run starts. */
TEST_CASE(a_run_on_the_programming_port_needs_the_pd_mini_on)
{
    for (int on = 0; on < 2; ++on) {
        lr_fresh(false);
        CHECK(!supply_screen_model());
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.output = on != 0;
        st.set_v = supply_screen_set_v();
        st.set_i = supply_screen_set_i();
        st.v = st.set_v;
        st.samples = 1u;
        st.taken_ms = g_rig.ms;
        st.stamped = true;
        servo_screen_supply(&st);
        lr_ms(200u);
        lr_start();
        CHECK_EQ(servo_screen_limit_running(), on != 0);
        if (on == 0) {
            CHECK_EQ(lr.disarms, 0u);
            CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
            CHECK_EQ(lr.cmd, 1500u);
        } else {
            servo_screen_set_link(false);
            lr_ms(200u);
            CHECK(!servo_screen_limit_running());
            servo_screen_set_link(true);
        }
    }
}

/* One reading of a PD mini that answers, its output on or off. */
static void lr_pd_reading(bool on)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = on;
    st.set_v = supply_screen_set_v();
    st.set_i = supply_screen_set_i();
    st.v = on ? st.set_v : 0.0f;
    st.samples = 1u;
    st.taken_ms = g_rig.ms;
    st.stamped = true;
    servo_screen_supply(&st);
}

/* A PD mini that is not the supply the run started with, or whose output
 * went off, heard inside the read session with no reading after it: the
 * next CONNECT is not sent, as it would switch that PD mini on or set it
 * beside the rail's feed.  No supply is asked after the read session, and
 * the run ends as a reading outside a session ends it: a PD mini heard
 * first after the start, its output on or off, as a changed supply; the
 * run's own PD mini, its output gone off, as `supply output went off`. */
TEST_CASE(a_pd_mini_heard_first_in_a_session_gets_no_connect)
{
    for (int k = 0; k < 3; ++k) {
        const bool started_with_pd = k == 2;
        const bool on_in_session   = k == 1;

        lr_fresh(false);
        if (started_with_pd) {
            g_rig.rail = true;
            lr_pd_reading(true);
            lr_ms(200u);
        }
        lr_start();
        CHECK(servo_screen_limit_running());
        for (int i = 0; i < 200000 && servo_screen_limit_running()
                        && kst_prog_state(M) != KST_PROG_PROGRAMMING; ++i) {
            lr_ms(1u);
        }
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        lr_pd_reading(on_in_session);
        for (int i = 0; i < 200000 && servo_screen_limit_running()
                        && kst_prog_state(M) != KST_PROG_PWM; ++i) {
            lr_ms(1u);
        }
        const unsigned sets = g_rig.sup_sets;
        CHECK(servo_screen_limit_running());
        lr_run_out();
        CHECK(!servo_screen_limit_running());
        CHECK_EQ(g_rig.sup_sets, sets);
        if (started_with_pd) {
            CHECK(strstr(lr.report, "Result:         ABORTED - supply "
                                    "output went off\n") != NULL);
        } else {
            CHECK(!g_rig.sup_on);
            CHECK(strstr(lr.report, "Result:         ABORTED - supply or "
                                    "rail voltage changed\n") != NULL);
        }
        g_rig.rail = false;
    }
}

/* The bench's own PWM output as the target: SERVO PULSES is not on the
 * page and a tap where it stands on the programming port changes nothing;
 * the run searches out to PULSE MIN and PULSE MAX with no session. */
TEST_CASE(servo_pulses_is_not_offered_for_a_pwm_pin)
{
    lr_fresh(false);
    lr.pin = true;
    servo_screen_set_pins((uint64_t)1u << 2, true);
    settings_set(SET_SERVO_TARGET, 0.0f);
    lr_ms(40u);
    lr_run_list();
    screen_tap(LR_MODE_X, LR_PULSE_Y);      /* where SERVO PULSES would be */
    CHECK(scr->back());
    lr_hold_start();
    CHECK(servo_screen_limit_running());
    lr_run_out();
    CHECK(strstr(lr.report, "search only, nothing") != NULL);
    CHECK(strstr(lr.report, "Pulse write") == NULL);
    CHECK(strstr(lr.report, "Searched:       from 1500 us out to 1000 us "
                            "and 2000 us") != NULL);
    CHECK_EQ(lr.disarms, 0u);
    {
        kst_image_t was = sim_bench_image(), now = rig_servo();

        CHECK(memcmp(was.r, now.r, sizeof(was.r)) == 0);
    }
}

/* SEARCH+PROG ended in a session: STOP, the link cut under a write, a
 * write the servo refuses.  ABORTED or FAIL, the servo let go, and once a
 * value was written the report names RESTORE and the programmer offers the
 * servo as the run found it. */
TEST_CASE(a_limit_run_ended_in_a_session_offers_restore)
{
    for (int how = 0; how < 2; ++how) {
        bool wrote = false, back = false, again = false;

        lr_fresh(false);
        lr_start();
        CHECK(servo_screen_limit_running());
        /* Until the second session holds the port, after a write. */
        for (int i = 0; i < 400000 && servo_screen_limit_running(); ++i) {
            kst_prog_state_t st;

            lr_ms(1u);
            st = kst_prog_state(M);
            wrote = wrote || servo(KST_F_LEFT_ANGLE) != 150u
                    || servo(KST_F_RIGHT_ANGLE) != 150u;
            back = back || (wrote && st == KST_PROG_PWM);
            if (back && st == KST_PROG_PROGRAMMING) {
                again = true;
                break;
            }
        }
        CHECK(again);
        if (how == 0) {
            rig_stop(true);
            servo_screen_cancel_arm();
        } else {
            g_rig.cut = true;
            /* What a session's write sends goes nowhere. */
        }
        for (int i = 0; i < 400 && servo_screen_limit_running(); ++i) {
            lr_ms(500u);
        }
        CHECK(!servo_screen_limit_running());
        lr_ms(1000u);
        CHECK(strstr(lr.report, "ABORTED") != NULL
              || strstr(lr.report, "FAIL") != NULL);
        CHECK(strstr(lr.report, "RESTORE") != NULL);
        rig_stop(false);
        g_rig.cut = false;
        CHECK(rig_settle());
    }
}

/* A run after a run: its report names the file its own first write saved,
 * also when that write is sent and lost on the line, never the earlier
 * run's file. */
TEST_CASE(a_later_run_names_its_own_backup_when_its_first_write_is_lost)
{
    char first[sizeof(g_rig.fc.file[0].name)];

    lr_fresh(false);
    lr_start();
    CHECK(servo_screen_limit_running());
    lr_run_out();
    CHECK(strstr(lr.report, "PASS") != NULL);
    CHECK_EQ(g_rig.fc.saves, 1u);
    snprintf(first, sizeof(first), "%s", g_rig.fc.file[0].name);
    CHECK(first[0] != '\0' && strstr(lr.report, first) != NULL);

    /* The servo set back by hand: the second run binds and writes again,
     * its first write lost on the line once sent. */
    {
        kst_image_t img = rig_servo();

        (void)kst_field_set(&img, KST_F_LEFT_ANGLE, 150u);
        (void)kst_field_set(&img, KST_F_RIGHT_ANGLE, 150u);
        memcpy(g_rig.sim.reg, img.r, sizeof(img.r));
    }
    memset(lr.report, 0, sizeof(lr.report));
    lr.len = 0u;
    lr_ms(1000u);
    /* SEARCH+PROG stays chosen from the first run. */
    lr_hold_start();
    CHECK(servo_screen_limit_running());
    bool cut = false;
    for (int i = 0; i < 400000 && servo_screen_limit_running(); ++i) {
        lr_ms(1u);
        /* The file saved before the write goes out: the line cut. */
        if (g_rig.fc.saves == 2u) {
            g_rig.cut = true;
            cut = true;
            break;
        }
    }
    CHECK(cut);
    for (int i = 0; i < 400 && servo_screen_limit_running(); ++i) {
        lr_ms(500u);
    }
    CHECK(!servo_screen_limit_running());
    lr_ms(1000u);
    CHECK_EQ(g_rig.fc.saves, 2u);
    CHECK(strstr(lr.report, "RESTORE") != NULL);
    CHECK(strstr(lr.report, "Backup:") != NULL);
    CHECK(strstr(lr.report, g_rig.fc.file[1].name) != NULL);
    CHECK(strstr(lr.report, first) == NULL);
    g_rig.cut = false;
    rig_stop(false);
    CHECK(rig_settle());
}

/* After a limit run that passed, an ordinary session is the operator's: its
 * first write saves the servo as that session found it, as every session's
 * first write does, also to a field the run never wrote.  RESTORE still
 * offers the servo as the run found it until then. */
TEST_CASE(a_session_after_a_limit_run_saves_its_own_backup)
{
    uint16_t left, right;

    lr_fresh(false);
    lr_start();
    CHECK(servo_screen_limit_running());
    lr_run_out();
    CHECK(strstr(lr.report, "PASS") != NULL);
    CHECK_EQ(g_rig.fc.saves, 1u);
    left = servo(KST_F_LEFT_ANGLE);
    right = servo(KST_F_RIGHT_ANGLE);
    CHECK(left < 150u && right < 150u);
    lr.armed = false;
    lr_ms(40u);
    program_open();
    CHECK(kst_panel_is_open());
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_restore_offered(M, KST_PROG_FROM_RUN));
    CHECK(kst_prog_save_first(M));
    enable();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 2u);
    /* The file holds the servo as this session found it: the run's
     * values. */
    {
        kst_image_t was = rig_servo();

        (void)kst_field_set(&was, KST_F_BOOST,
                            (uint16_t)(servo(KST_F_BOOST) + 1u));
        CHECK(memcmp(g_rig.fc.file[1].img.r, was.r, sizeof(was.r)) == 0);
        CHECK_EQ(kst_field_get(&g_rig.fc.file[1].img, KST_F_LEFT_ANGLE),
                 left);
        CHECK_EQ(kst_field_get(&g_rig.fc.file[1].img, KST_F_RIGHT_ANGLE),
                 right);
    }
}

/* A run's write that is refused before it is sent -- a value the model does
 * not take, or nothing to write -- leaves write enable off: the session the
 * run gives up holds no enabled write for the next edit. */
TEST_CASE(a_run_write_refused_before_it_is_sent_switches_write_enable_off)
{
    uint16_t lo = 0u, hi = 0u;

    lr_fresh(false);
    lr.armed = false;
    lr_ms(40u);
    program_open();
    CHECK(kst_panel_is_open());
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_range(M, KST_F_LEFT_ANGLE, &lo, &hi));
    kst_panel_run_begin();
    /* Past the hard rules: the model refuses the edit. */
    CHECK(kst_panel_run_write((uint16_t)(hi + 1u), servo(KST_F_RIGHT_ANGLE))
          != KST_PROG_MSG_NONE);
    CHECK(!kst_prog_write_enabled(M));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* The servo's own values: nothing to write. */
    CHECK(kst_panel_run_write(servo(KST_F_LEFT_ANGLE),
                              servo(KST_F_RIGHT_ANGLE))
          != KST_PROG_MSG_NONE);
    CHECK(!kst_prog_write_enabled(M));
    CHECK_EQ(kst_panel_run_poll(), KST_PANEL_RUN_NONE);
    kst_panel_run_end();
}

/* The run aborted in the frame its ARM was asked, before the panel took
 * it: the command waiting is SAFE, and the bench is not armed after the
 * end. */
TEST_CASE(a_limit_run_aborted_behind_its_arm_leaves_the_bench_safe)
{
    bool seen = false;

    lr_fresh(false);
    lr_start();
    CHECK(servo_screen_limit_running());
    for (int i = 0; i < 4000000 && servo_screen_limit_running(); ++i) {
        lr_ms(1u);
        if (g_rig.ms % 20u == 0u && servo_screen_limit_running()
            && servo_screen_limit_phase() == SERVO_LIMRUN_PH_ARM
            && !lr.armed) {
            seen = true;
            break;
        }
    }
    CHECK(seen);
    {
        const unsigned arms = lr.arms;

        /* The meter changes under it in that frame. */
        lr.ina = false;
        servo_screen_source(SERVO_SOURCE_PDMINI, 3u);
        CHECK(!servo_screen_limit_running());
        lr_ms(2000u);
        CHECK_EQ(lr.arms, arms);
        CHECK(!lr.armed);
        CHECK(strstr(lr.report, "ABORTED") != NULL);
    }
}

int main(void)
{
    RUN(a_limit_run_programs_the_servo_on_the_programming_port);
    RUN(a_limit_run_refuses_a_servo_whose_pulses_lie_outside_min_and_max);
    RUN(a_limit_run_on_a_servo_that_does_not_answer_names_the_missing_kst_servo);
    RUN(a_limit_run_whose_first_connect_fails_otherwise_names_the_session);
    RUN(a_run_ended_in_a_session_ends_the_programmers_run_after_its_leave);
    RUN(a_run_on_the_programming_port_needs_4_8_v_on_the_rail);
    RUN(a_run_on_the_programming_port_needs_the_pd_mini_on);
    RUN(a_pd_mini_heard_first_in_a_session_gets_no_connect);
    RUN(servo_pulses_is_not_offered_for_a_pwm_pin);
    RUN(a_limit_run_ended_in_a_session_offers_restore);
    RUN(a_later_run_names_its_own_backup_when_its_first_write_is_lost);
    RUN(a_session_after_a_limit_run_saves_its_own_backup);
    RUN(a_run_write_refused_before_it_is_sent_switches_write_enable_off);
    RUN(a_limit_run_aborted_behind_its_arm_leaves_the_bench_safe);
    return test_summary("kst_panel_limrun");
}
