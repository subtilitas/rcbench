/*
 * Random runs of the automatic servo test and of the stick programming
 * engine, with what a caller relies on checked at every step.
 *
 * The runs are drawn from a 32-bit xorshift generator.  Each case seeds it
 * from a fixed seed and its own number, so the suite is the same every time
 * and a case that fails is run again alone from the two numbers its message
 * states (one_servo_run() and one_stick_run() take them).  The counts are
 * fixed: FUZZ_SERVO_RUNS and FUZZ_STICK_RUNS for each of the seeds.
 *
 * Under test, servo_test: random configurations on each of the three
 * meters, with every text field full; readings stamped either side of the
 * frame time, with counts that step by up to 70000, a supply that drops
 * out, trips, reports another set point or goes off; windows that repeat,
 * skip and come without samples; the meter changing under a run; encoder
 * readings; a cap that moves, disarms and aborts; the clock started
 * anywhere in 2^32 ms.  Checked: the step that sees the run over hands off
 * and release together; nothing is asked on, set or commanded after it; a
 * set point is at most the cap and at least 3.0 V; a command is the centre
 * or an end; step and step_count stay inside the slots; every CSV row has
 * the header's number of fields and neither nan nor inf; no line fills its
 * buffer or holds a newline; the outbox drains.
 *
 * Under test, esc_stick: every built-in profile the engine takes, with
 * random changes and timings, a bench that arms late, a supply that follows
 * late, stops, disarms, link loss, supply loss, late, skipped and
 * current-less readings, noise in five shapes and a person tapping DONE at
 * random.  Checked: the throttle is 0, 50 or 100 %; the stick leaves 0 %
 * only on an armed bench; a stick move with the supply asked off happens
 * only once the supply read off; the supply is asked on only on entering
 * POWER; a run that is over has the arm off, the supply off and the
 * throttle at 0 %; every run ends within 1200 s.
 *
 * Not checked: whether a run stores the value asked.  The simulated menu
 * here loses readings on purpose, and what the engine does then is the
 * subject of test_esc_stick.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"
#include "esc_stick.h"
#include "servo_test.h"

#define FUZZ_SERVO_RUNS 200u
#define FUZZ_STICK_RUNS 800u

static const uint32_t k_seeds[] = { 12345u, 0x2545F491u, 0x9E3779B9u };

/* ------------------------------------------------------------ the draws */

static uint32_t rs;

static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static uint32_t upto(uint32_t n)
{
    return rnd() % n;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd() & 0xFFFFu) / 65535.0f;
}

/* The generator for case @p id of @p seed: never 0, which xorshift keeps. */
static void seed_case(uint32_t seed, unsigned id)
{
    rs = seed ^ ((uint32_t)id * 2654435761u);
    if (rs == 0u) {
        rs = 1u;
    }
    for (int k = 0; k < 8; ++k) {
        (void)rnd();
    }
}

/* What a broken rule says, with the two numbers that run the case again.
 * The first 8 of a suite are printed; every one fails its case. */
static unsigned g_said;
static uint32_t g_seed;
static unsigned g_id;

#define RULE(cond, ...)                                                       \
    do {                                                                      \
        if (!(cond)) {                                                        \
            t_case_failed = 1;                                                \
            if (g_said < 8u) {                                                \
                ++g_said;                                                     \
                printf("      %s:%d: seed 0x%08lX case %u: ", __FILE__,       \
                       __LINE__, (unsigned long)g_seed, g_id);                \
                printf(__VA_ARGS__);                                          \
                printf("\n");                                                 \
            }                                                                 \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------ servo_test */

static servo_test_t st;

static unsigned fields_of(const char *row)
{
    unsigned n = 1u;
    for (const char *p = row; *p != '\0'; ++p) {
        n += (*p == ';') ? 1u : 0u;
    }
    return n;
}

static void one_servo_run(uint32_t seed, unsigned id)
{
    g_seed = seed;
    g_id   = id;
    seed_case(seed, id);

    servo_test_cfg_t c;
    memset(&c, 0, sizeof(c));
    static const float k_v[] = { 4.8f, 6.0f, 7.4f, 8.4f };
    c.step_count = (uint8_t)upto(5u);
    for (unsigned k = 0; k < c.step_count && k < 4u; ++k) {
        c.steps_v[k] = k_v[k];
    }
    c.brownout = upto(2u) != 0u;
    c.i_limit = frand(0.5f, 5.0f);
    c.centre_us = 1500u;
    c.end_lo_us = (uint16_t)(1100u + upto(300u));
    c.end_hi_us = (uint16_t)(1600u + upto(300u));
    c.settle_ms = (uint16_t)upto(2000u);
    c.dwell_ms = (uint16_t)upto(1500u);
    c.by_moves = upto(2u) != 0u;
    c.moves = (uint16_t)(1u + upto(6u));
    c.time_s = (uint16_t)(5u + upto(10u));
    c.idle_max_a = upto(2u) ? frand(0.0f, 0.2f) : 0.0f;
    c.hold_max_a = upto(2u) ? frand(0.0f, 0.5f) : 0.0f;
    c.travel_max_ms = (uint16_t)(upto(2u) ? upto(2000u) : 0u);
    c.stall_a = frand(0.1f, 5.0f);
    c.report = upto(4u) != 0u;
    memset(c.dut, 'W', sizeof(c.dut));          /* no terminator: the widest */
    memset(c.type, 'W', sizeof(c.type));
    memset(c.danger, 'W', sizeof(c.danger));
    memset(c.firmware, 'W', sizeof(c.firmware));
    const unsigned meter = upto(4u);            /* 3: none stated */
    switch (meter) {
    case 0: servo_test_meter_pdmini(&c.meter); break;
    case 1: servo_test_meter_model(&c.meter); c.model = true; break;
    case 2: servo_test_meter_ina3221(&c.meter, (uint16_t)(upto(3u) * 500u));
            break;
    default: break;
    }
    const bool on_windows = meter == 2u;
    c.meter_changes = rnd();
    c.ina_why = (uint8_t)upto(SERVO_TEST_INA_COUNT);
    c.enc_on = upto(3u) == 0u;
    c.enc_centre = (uint16_t)upto(4096u);
    c.enc_cmd_deg[0] = frand(-90.0f, 0.0f);
    c.enc_cmd_deg[1] = frand(0.0f, 90.0f);
    c.min_us = 65535u;
    c.max_us = 65535u;
    c.frame_hz = 65535u;
    c.trim_us = -32768;
    c.travel_deg = 255u;
    c.range_pct = 255u;
    c.reverse = true;
    c.hv = true;

    uint32_t now = rnd();                       /* anywhere on the clock */
    const float v_min = frand(1.0f, 3.5f);
    const float v_max0 = frand(4.0f, 21.0f);
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = upto(8u) != 0u;
    last.output = upto(3u) == 0u;
    servo_test_init(&st);
    if (servo_test_start(&st, &c, now, &last, v_min, v_max0)
        != SERVO_TEST_START_OK) {
        return;
    }
    bool on = last.output;
    float set_v = 5.0f;
    uint16_t samples = (uint16_t)rnd();
    uint16_t win_number = (uint16_t)rnd();
    uint32_t win_next = now;
    uint16_t enc_raw = c.enc_centre;
    uint16_t cmd = 1500u;
    uint32_t cmd_at = now;
    bool was_running = true;
    unsigned offs = 0u, releases = 0u, csv_fields = 0u;
    const unsigned mode = upto(6u);
    for (unsigned f = 0; f < 400000u && !servo_test_drained(&st); ++f) {
        now += 20u;
        const uint32_t since = now - cmd_at;
        float i = 0.03f;
        switch (mode) {
        case 0: i = (cmd != 1500u && since > 200u && since < 600u) ? 0.5f
                                                                 : 0.03f;
                break;
        case 1: i = frand(0.0f, 0.3f); break;
        case 2: i = (since < 5000u) ? 0.5f : 0.03f; break;
        case 3: i = 0.0f; break;
        case 4: i = frand(-1.0f, 6.0f); break;
        default: i = (cmd > 1500u) ? 0.4f : 0.02f; break;
        }
        if (upto(5u) == 0u || mode == 5u) {
            servo_test_reading_t r;
            memset(&r, 0, sizeof(r));
            r.online = upto(4000u) != 0u;
            r.output = on && upto(6000u) != 0u;
            r.ok = upto(200u) != 0u;
            r.trip = (uint8_t)(upto(5000u) == 0u);
            r.set_v = (upto(300u) == 0u) ? set_v + 1.0f : set_v;
            r.set_i = c.i_limit;
            r.i = on ? i : 0.0f;
            r.v = on ? set_v : 0.0f;
            r.mode = (uint8_t)upto(4u);
            samples = (uint16_t)(samples + ((upto(50u) == 0u) ? upto(70000u)
                                                             : 1u));
            r.samples = samples;
            r.taken_ms = now - upto(3u) * 10u + upto(2u) * 15u; /* either side */
            servo_test_reading(&st, &r, (uint16_t)(upto(2u) ? 0u : rnd()));
        }
        /* A window every 50 ms, each number once; now and then one twice,
         * a number left out, one without samples, or none for a while. */
        while (on_windows && (int32_t)(now - win_next) >= 0) {
            win_next += 50u;
            if (upto(400u) == 0u) {
                win_next += upto(900u);         /* the part goes quiet */
            }
            servo_test_win_t w;
            memset(&w, 0, sizeof(w));
            if (upto(100u) != 0u) {
                win_number = (uint16_t)(win_number + 1u + (upto(60u) == 0u));
            }
            w.number = win_number;
            w.current = upto(300u) != 0u;
            w.voltage = upto(300u) != 0u;
            w.clipped = (uint8_t)((upto(40u) == 0u) ? upto(51u) : 0u);
            const float a = on ? i : 0.0f;
            const float lim = (a > 1.638f) ? 1.638f : (a < -1.638f) ? -1.638f : a;
            w.mean_ma = (int16_t)lroundf(lim * 1000.0f);
            w.max_ma = (int16_t)(w.mean_ma + (int16_t)upto(200u));
            w.min_ma = (int16_t)(w.mean_ma - (int16_t)upto(200u));
            w.mean_mv = (uint16_t)lroundf((on ? set_v : 0.0f) * 1000.0f);
            w.min_mv = (uint16_t)(w.mean_mv - upto(w.mean_mv / 4u + 1u));
            w.taken_ms = now - upto(2u) * 50u;
            servo_test_window(&st, &w, (uint16_t)(upto(2u) ? 0u : rnd()));
        }
        if (c.enc_on && upto(2u) == 0u) {
            servo_test_enc_t e;
            memset(&e, 0, sizeof(e));
            enc_raw = (uint16_t)((enc_raw + upto(9u) + ((cmd > 1500u) ? 40u : 0u)
                                  + 4092u) & 4095u);
            e.valid = upto(60u) != 0u;
            e.no_magnet = !e.valid && upto(2u) != 0u;
            e.raw = enc_raw;
            e.still_ms = (uint16_t)upto(2000u);
            e.taken_ms = now - upto(3u) * 10u;
            e.gap = upto(80u) == 0u;
            e.weak = upto(30u) == 0u;
            e.strong = upto(30u) == 0u;
            servo_test_encoder(&st, &e);
        }
        if (upto(3u) == 0u) {
            servo_test_meter_now_t m;
            memset(&m, 0, sizeof(m));
            const bool drop = upto(20000u) == 0u;
            m.ina3221 = on_windows && !drop;
            m.changes = c.meter_changes + (drop ? 1u : 0u);
            m.dropped = drop ? (servo_test_abort_t)(SERVO_TEST_AB_INA_RESET
                                                    + upto(5u))
                             : SERVO_TEST_AB_NONE;
            m.dropped_at = m.changes;
            servo_test_meter_now(&st, &m, now);
        }
        const servo_test_in_t in = { upto(40000u) != 0u,
                                     (upto(3000u) == 0u) ? 4.0f : v_max0 };
        if (upto(60000u) == 0u) {
            servo_test_abort(&st, (servo_test_abort_t)(1u + upto(7u)), now);
        }
        servo_test_do_t d;
        servo_test_step(&st, now, &in, &d);
        const bool running = servo_test_running(&st);
        if (d.set) {
            RULE(d.set_v <= in.v_max + 0.001f || !running,
                 "set %.2f V above the cap %.2f V", (double)d.set_v,
                 (double)in.v_max);
            RULE(d.set_v >= 2.99f, "set %.2f V", (double)d.set_v);
            set_v = d.set_v;
        }
        if (d.on) {
            on = true;
        }
        if (d.off) {
            on = false;
            ++offs;
        }
        if (d.release) {
            ++releases;
        }
        if (d.command) {
            RULE(d.cmd_us == c.centre_us || d.cmd_us == c.end_lo_us
                 || d.cmd_us == c.end_hi_us, "command %u us",
                 (unsigned)d.cmd_us);
            cmd = d.cmd_us;
            cmd_at = now;
        }
        if (!running) {
            RULE(!d.on && !d.set && !d.command,
                 "a run that is over asks on, set or command");
        }
        if (was_running && !running) {
            RULE(d.off && d.release, "the end without off and release");
        }
        was_running = running;
        RULE(st.step_count <= SERVO_TEST_STEP_SLOTS
             && st.step < SERVO_TEST_STEP_SLOTS, "step %u of %u",
             (unsigned)st.step, (unsigned)st.step_count);
        const char *text = NULL;
        servo_test_out_t k;
        while ((k = servo_test_peek(&st, &text)) != SERVO_TEST_OUT_NONE) {
            if (k == SERVO_TEST_OUT_CSV || k == SERVO_TEST_OUT_TXT) {
                const size_t n = strlen(text);
                RULE(n + 1u < SERVO_TEST_LINE_MAX,
                     "a %s line fills the buffer (%lu bytes): %.60s...",
                     k == SERVO_TEST_OUT_CSV ? "CSV" : "TXT",
                     (unsigned long)n, text);
                RULE(strchr(text, '\n') == NULL, "a newline in a line");
            }
            if (k == SERVO_TEST_OUT_CSV) {
                if (csv_fields == 0u) {
                    csv_fields = fields_of(text);       /* the header */
                    RULE(csv_fields >= 13u, "a header of %u fields",
                         csv_fields);
                }
                RULE(fields_of(text) == csv_fields,
                     "a CSV row of %u fields under a header of %u: %s",
                     fields_of(text), csv_fields, text);
                RULE(strstr(text, "nan") == NULL
                     && strstr(text, "inf") == NULL, "nan or inf in %s",
                     text);
            }
            servo_test_pop(&st);
        }
    }
    RULE(servo_test_drained(&st),
         "not drained after 8000 s (mode %u, phase %d)", mode, (int)st.phase);
    RULE(offs >= 1u && releases >= 1u, "%u offs, %u releases", offs,
         releases);
}

/* ------------------------------------------------------------- esc_stick */

static esc_stick_t es;

/* Whether the run reached DONE: counted, so the suite shows that runs get
 * that far and are not all turned away at their start. */
static bool one_stick_run(uint32_t seed, unsigned id)
{
    g_seed = seed;
    g_id   = id;
    seed_case(seed, id);

    const esc_profile_t *p = NULL;
    for (unsigned tries = 0; tries < 200u; ++tries) {
        const esc_profile_t *q =
            &esc_profiles_builtin[upto((uint32_t)esc_profiles_builtin_count)];
        if (esc_stick_kind(q, NULL) != ESC_STICK_KIND_NONE) {
            p = q;
            break;
        }
    }
    if (p == NULL) {
        return false;
    }
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    if (p->entry_hold_ms != 0u) {
        t.entry_ms = p->entry_hold_ms;
    }
    if (upto(3u) == 0u) {
        t.store_ms = upto(3u) * 250u;
        t.off_ms = 500u + upto(3u) * 500u;
        t.silence_ms = 1000u + upto(5u) * 1000u;
        t.timeout_ms = 5000u + upto(10u) * 5000u;
    }
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n = 0u;
    for (unsigned i = 0; i < p->item_count && n < 3u; ++i) {
        if (esc_stick_not_offered(&p->items[i]) == NULL && upto(2u)) {
            ch[n].item = (uint8_t)i;
            ch[n].value = (uint8_t)upto(p->items[i].value_count);
            ++n;
        }
    }
    uint32_t now = rnd();
    esc_stick_bench_t b = { now, false, rnd(), true, rnd() };
    const char *why = NULL;
    if (!esc_stick_start(&es, p, ch, n, &t, 7600u, 1000u, &b, &why)) {
        return false;
    }
    const uint32_t stops0 = b.stops;
    const uint32_t pressed0 = b.pressed;
    uint32_t seq = rnd() & 0xFFFFu;
    bool supply = false;            /* the module's own state */
    uint32_t supply_change_at = now;
    bool want = false;
    float last_pct = 0.0f;
    bool armed = false;
    const unsigned mode = upto(5u);
    const unsigned loss_1_in = 2u + upto(40u);
    const unsigned skip_1_in = upto(3u) ? 20u + upto(100u) : 0u;
    unsigned tone_n = 1u;
    uint32_t tone_t = 0u;
    unsigned f;
    for (f = 0; f < 60000u && esc_stick_running(&es); ++f) {
        now += 20u;
        const esc_stick_out_t *o = esc_stick_out(&es);
        /* The bench follows what the run asks, a little late. */
        if (o->arm && upto(4u) == 0u) {
            armed = true;
        }
        if (!o->arm) {
            armed = false;
        }
        if (o->supply_on != want) {
            want = o->supply_on;
            supply_change_at = now + 100u + upto(200u);
        }
        if ((int32_t)(now - supply_change_at) >= 0) {
            supply = want;
        }
        if (upto(20000u) == 0u) {
            b.stops++;
            if (upto(2u)) {
                b.pressed++;
            }
        }
        if (upto(30000u) == 0u) {
            armed = false;
        }
        if (upto(30000u) == 0u) {
            b.link_up = false;
        }
        if (f % 5u == 0u && (skip_1_in == 0u || upto(skip_1_in) != 0u)) {
            esc_stick_sample_t s;
            memset(&s, 0, sizeof(s));
            seq = (seq + ((upto(300u) == 0u) ? 2u : 1u)) & 0xFFFFu;
            s.seq = seq;
            s.at_ms = now - upto(2u) * 7u;
            s.online = upto(20000u) != 0u;
            s.output = want;
            s.reported_on = supply && upto(20000u) != 0u;
            s.current_ok = upto(400u) != 0u;
            int32_t ma = supply ? 60 : (int32_t)upto(15u);
            if (supply) {
                /* A menu: groups of 1 to 5 beeps, 250 ms on, 250 ms off,
                 * 1.5 s between groups; noise by mode. */
                tone_t += 100u;
                const uint32_t group_ms = tone_n * 500u + 1500u;
                if (tone_t >= group_ms) {
                    tone_t = 0u;
                    tone_n = (tone_n % 5u) + 1u;
                }
                const bool beep = tone_t < tone_n * 500u
                                  && (tone_t % 500u) < 250u;
                switch (mode) {
                case 0: ma += beep ? 300 : 0; break;
                case 1: ma += (int32_t)upto(400u); break;
                case 2: ma += 500; break;
                case 3: ma += beep ? 300 : (int32_t)upto(90u); break;
                default: ma += (beep && upto(loss_1_in) != 0u) ? 300 : 0;
                         break;
                }
            }
            s.ma = ma;
            esc_stick_sample(&es, &s);
        }
        if (esc_stick_hand(&es) != NULL && upto(100u) == 0u) {
            (void)esc_stick_confirm(&es);
        }
        b.now_ms = now;
        b.armed = armed;
        const esc_stick_phase_t ph0 = es.phase;
        const bool sup0 = es.out.supply_on;
        esc_stick_step(&es, &b);
        o = esc_stick_out(&es);
        RULE(o->throttle_pct == 0.0f || o->throttle_pct == 50.0f
             || o->throttle_pct == 100.0f, "%s: throttle %.1f", p->id,
             (double)o->throttle_pct);
        if (o->throttle_pct != ESC_STICK_PCT_MIN) {
            RULE(o->arm && es.armed_seen, "%s: stick off MIN without the arm",
                 p->id);
        }
        /* A planned move with the supply asked off waits for the supply
         * read off: the stick changes in an off phase only with off_seen. */
        if (esc_stick_running(&es) && o->throttle_pct != last_pct
            && !o->supply_on && !sup0) {
            RULE(es.off_seen,
                 "%s: stick moved %.0f to %.0f in %s with the supply not "
                 "read off", p->id, (double)last_pct,
                 (double)o->throttle_pct, esc_stick_phase_text(ph0));
        }
        if (o->supply_on && !sup0) {
            RULE(es.phase == ESC_STICK_POWER, "%s: supply on in %s", p->id,
                 esc_stick_phase_text(es.phase));
        }
        last_pct = o->throttle_pct;
        if (!esc_stick_running(&es)) {
            RULE(!o->arm && !o->supply_on
                 && o->throttle_pct == ESC_STICK_PCT_MIN,
                 "%s: ended %s with arm %d supply %d throttle %.0f", p->id,
                 esc_stick_phase_text(es.phase), o->arm, o->supply_on,
                 (double)o->throttle_pct);
            if (b.stops != stops0) {
                RULE(es.reason == esc_stick_stop_reason(b.stops - stops0,
                                                        b.pressed - pressed0)
                     || es.phase == ESC_STICK_ABORTED, "%s: stop reason",
                     p->id);
            }
        }
        RULE(es.active < ESC_STICK_MAX_CHANGES, "%s: active %u", p->id,
             (unsigned)es.active);
    }
    RULE(!esc_stick_running(&es), "%s still running after 1200 s in %s "
         "(mode %u)", p->id, esc_stick_phase_text(es.phase), mode);
    return es.phase == ESC_STICK_DONE;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(random_servo_runs_keep_the_rules_a_caller_relies_on)
{
    unsigned finished = 0u;
    for (size_t s = 0; s < sizeof(k_seeds) / sizeof(k_seeds[0]); ++s) {
        for (unsigned id = 0; id < FUZZ_SERVO_RUNS; ++id) {
            one_servo_run(k_seeds[s], id);
            finished += (servo_test_verdict(&st) != SERVO_TEST_ABORTED
                         && st.state == SERVO_TEST_DONE) ? 1u : 0u;
        }
    }
    /* Runs that go to their end are among them: the draws are not all
     * turned away at the start or aborted. */
    CHECK(finished >= 20u);
}

TEST_CASE(random_stick_runs_keep_the_rules_a_caller_relies_on)
{
    unsigned done = 0u;
    for (size_t s = 0; s < sizeof(k_seeds) / sizeof(k_seeds[0]); ++s) {
        for (unsigned id = 0; id < FUZZ_STICK_RUNS; ++id) {
            done += one_stick_run(k_seeds[s], id) ? 1u : 0u;
        }
    }
    CHECK(done >= 20u);
}

int main(void)
{
    RUN(random_servo_runs_keep_the_rules_a_caller_relies_on);
    RUN(random_stick_runs_keep_the_rules_a_caller_relies_on);
    return test_summary("fuzz_engines");
}
