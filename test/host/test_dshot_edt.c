/*
 * When the bench asks an ESC (electronic speed controller) for extended
 * telemetry, held against a model of the ESC that has to accept it.
 *
 * The model is AM32 2.21's command path, cut down to what decides whether
 * command 13 is taken:
 *
 *   - a command acts only while the ESC is armed and its motor is stopped,
 *     and only on its sixth identical frame in a row (Src/dshot.c:157-166);
 *   - a zero-throttle frame clears the repeat count (Src/dshot.c:152-154);
 *   - the ESC arms itself after a second of zero input, counted from the
 *     first frame it hears (Src/main.c:1353-1356);
 *   - it restarts, forgetting everything, after 0.5 s without a frame while
 *     armed and 2 s while not (Src/main.c:1977-2002);
 *   - taking command 13 sends the status frame 0xE00 once, then voltage
 *     frames interleaved with speed (Src/dshot.c:271, 258-260).
 *
 * The bench side is dshot_edt_*(), driven the way outputs_hw.c drives it:
 * one frame a millisecond while armed, dshot_edt_idle() on every service
 * while not, and each reply heard before the next frame goes out.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "dshot.h"

/* -------------------------------------------------------- the ESC model */

typedef struct {
    bool     armed;
    bool     running;
    unsigned zero_ms;        /* zero input while not armed            */
    unsigned silent_ms;      /* since the last frame                  */
    uint16_t last_cmd;
    unsigned count;          /* identical commands in a row           */
    bool     edt;            /* extended telemetry on                 */
    bool     init_pending;   /* the 0xE00 status frame still to send  */
    unsigned frames;         /* replies since extended telemetry came on */
    unsigned voltage_every;  /* one voltage frame per this many replies */
} am32_t;

static void am32_restart(am32_t *m)
{
    const unsigned every = m->voltage_every;
    memset(m, 0, sizeof(*m));
    m->voltage_every = every;
}

/* One frame in; the reply's kind out. */
static dshot_telem_kind_t am32_frame(am32_t *m, uint16_t value)
{
    m->silent_ms = 0u;
    const bool zero_input = (value <= DSHOT_CMD_MAX);
    if (value == 0u) {
        m->count = 0u;
    }
    if (zero_input) {
        m->running = false;
        if (!m->armed && ++m->zero_ms > 1000u) {
            m->armed = true;
        }
    } else {
        m->zero_ms = 0u;
        if (m->armed) {
            m->running = true;
        }
    }
    if (value >= 1u && value <= DSHOT_CMD_MAX && m->armed && !m->running) {
        if (value != m->last_cmd) {
            m->last_cmd = value;
            m->count = 0u;
        }
        if (++m->count >= 6u) {
            m->count = 0u;
            if (value == DSHOT_CMD_EDT_ENABLE) {
                m->edt = true;
                m->init_pending = true;
            }
        }
    }

    if (m->init_pending) {
        m->init_pending = false;
        return DSHOT_TELEM_STATUS;
    }
    if (m->edt && m->voltage_every > 0u
        && ++m->frames % m->voltage_every == 0u) {
        return DSHOT_TELEM_VOLTAGE;
    }
    return DSHOT_TELEM_ERPM;
}

/* A millisecond with nothing on the wire. */
static void am32_silence(am32_t *m)
{
    ++m->silent_ms;
    if ((m->armed && m->silent_ms > 500u) || m->silent_ms > 2000u) {
        am32_restart(m);
    }
}

/* ------------------------------------------------------------ the bench */

typedef struct {
    dshot_edt_t edt;
    am32_t      esc;
    uint32_t    now_ms;
    unsigned    commands;        /* command 13 frames sent             */
    unsigned    late_commands;   /* sent after the answer was heard    */
    unsigned    held_throttle;   /* throttle asked for, command sent   */
    uint32_t    answered_ms;     /* when the bench heard the answer    */
    unsigned    starts_after_on; /* asks begun after the ESC took one  */
    uint32_t    ask_start[64];
    unsigned    n_starts;
    bool        in_ask;
} bench_t;

static void bench_init(bench_t *b, uint32_t now_ms)
{
    memset(b, 0, sizeof(*b));
    b->now_ms = now_ms;
    b->esc.voltage_every = 50u;
    dshot_edt_bind(&b->edt, now_ms);
}

/* Disarmed for @p ms: nothing sent, the ask owed again. */
static void bench_idle(bench_t *b, unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) {
        dshot_edt_idle(&b->edt, b->now_ms);
        am32_silence(&b->esc);
        b->in_ask = false;
        ++b->now_ms;
    }
}

/*
 * Armed for @p ms at @p throttle (a DShot value, 0 or 48..2047).  The reply
 * is read as the decoder would: as a speed unless an ask has started.
 */
static void bench_drive(bench_t *b, unsigned ms, uint16_t throttle)
{
    for (unsigned i = 0; i < ms; ++i) {
        const bool was_answered = b->edt.answered;
        const bool ask = dshot_edt_frame(&b->edt, throttle == 0u, b->now_ms);
        if (ask) {
            ++b->commands;
            if (was_answered) {
                ++b->late_commands;
            }
            if (throttle != 0u) {
                ++b->held_throttle;
            }
            if (!b->in_ask && b->n_starts < 64u) {
                b->ask_start[b->n_starts++] = b->now_ms;
            }
            if (!b->in_ask && b->esc.edt) {
                ++b->starts_after_on;
            }
        }
        b->in_ask = ask && b->edt.left != 0u;
        dshot_telem_kind_t kind =
            am32_frame(&b->esc, ask ? (uint16_t)DSHOT_CMD_EDT_ENABLE : throttle);
        if (!b->edt.asked) {
            kind = DSHOT_TELEM_ERPM;
        }
        dshot_edt_heard(&b->edt, kind);
        if (!was_answered && b->edt.answered) {
            b->answered_ms = b->now_ms;
        }
        ++b->now_ms;
    }
}

/* ---------------------------------------------------------------- cases */

TEST_CASE(one_ask_at_the_start_of_a_run_never_reaches_am32)
{
    /*
     * What the bench did before: DSHOT_CMD_REPEATS frames of command 13 on
     * the edge into driving, then throttle, from a line that was silent.
     * AM32 has not armed 10 ms after the line came alive, so it drops them,
     * and nothing asks again.
     */
    am32_t esc;
    memset(&esc, 0, sizeof(esc));
    esc.voltage_every = 50u;
    for (unsigned ms = 0; ms < 5000u; ++ms) {
        const uint16_t v = (ms < DSHOT_CMD_REPEATS)
                               ? (uint16_t)DSHOT_CMD_EDT_ENABLE : 0u;
        (void)am32_frame(&esc, v);
    }
    CHECK(esc.armed);
    CHECK(!esc.edt);
}

TEST_CASE(am32_takes_the_repeated_ask_once_it_has_armed)
{
    bench_t b;
    bench_init(&b, 1000u);
    bench_idle(&b, 3000u);                  /* the ESC restarts in here */
    bench_drive(&b, 5000u, 0u);

    CHECK(b.esc.edt);
    CHECK(b.edt.answered);
    /* AM32 arms on its 1001st millisecond of zero input; the ask that is
     * due after that is the one it takes. */
    CHECK(b.answered_ms - 4000u <= 1001u + DSHOT_EDT_RETRY_MS
                                   + DSHOT_CMD_REPEATS);
    CHECK_EQ(b.late_commands, 0u);
    CHECK_EQ(b.starts_after_on, 0u);
}

TEST_CASE(the_status_frame_alone_is_heard_as_the_answer)
{
    /* No voltage frames at all: the 0xE00 AM32 sends once when it takes the
     * command is the only extended frame, and it must not be read as a
     * speed. */
    bench_t b;
    bench_init(&b, 0u);
    b.esc.voltage_every = 0u;
    bench_drive(&b, 3000u, 0u);
    CHECK(b.esc.edt);
    CHECK(b.edt.answered);
    CHECK_EQ(b.late_commands, 0u);
    /* Heard on the ask AM32 took, not on a later one: a status frame read as
     * a speed is lost, and only the next ask would bring another. */
    CHECK_EQ(b.starts_after_on, 0u);
}

TEST_CASE(an_esc_that_takes_the_first_ask_is_heard_on_it)
{
    /* An ESC already armed when the run starts -- Bluejay, which takes
     * commands without arming, or an AM32 the bench left for less than its
     * 0.5 s restart -- takes the first ask on its sixth repeat.  Its status
     * frame arrives before that ask has finished. */
    bench_t b;
    bench_init(&b, 0u);
    b.esc.voltage_every = 0u;
    b.esc.armed = true;
    bench_drive(&b, 2000u, 0u);
    CHECK(b.esc.edt);
    CHECK(b.edt.answered);
    CHECK_EQ(b.commands, 6u);
    CHECK_EQ(b.starts_after_on, 0u);
}

TEST_CASE(the_first_frame_of_a_run_at_zero_throttle_is_the_ask)
{
    /* An ESC that takes commands at once (Bluejay, an AM32 still armed)
     * gets the ask on the first frame, as before. */
    dshot_edt_t e;
    dshot_edt_bind(&e, 0u);
    for (unsigned i = 0; i < DSHOT_CMD_REPEATS; ++i) {
        CHECK(dshot_edt_frame(&e, true, i));
    }
    CHECK(!dshot_edt_frame(&e, true, DSHOT_CMD_REPEATS));
    CHECK(e.asked);
    CHECK_EQ(e.asks, 1u);
}

TEST_CASE(a_throttle_is_never_held_back_by_an_ask)
{
    bench_t b;
    bench_init(&b, 0u);
    bench_drive(&b, 4u, 0u);                /* four repeats of the first ask */
    bench_drive(&b, 2000u, 600u);           /* the operator opens the throttle */
    CHECK_EQ(b.held_throttle, 0u);
    CHECK_EQ(b.commands, 4u);

    /* Back at zero, the interrupted ask starts again from its first repeat:
     * the four that went out were followed by throttle, which an ESC does not
     * count as part of a run of commands. */
    dshot_edt_t *e = &b.edt;
    CHECK_EQ(e->asks, 0u);
    CHECK(dshot_edt_frame(e, true, b.now_ms));
    CHECK_EQ(e->left, DSHOT_CMD_REPEATS - 1u);
}

TEST_CASE(a_run_armed_with_the_throttle_open_asks_once_it_closes)
{
    /* The arming frame carries a throttle, so a run can start above zero.
     * AM32 does not arm on that, and the bench does not ask into it. */
    bench_t b;
    bench_init(&b, 0u);
    bench_drive(&b, 2000u, 400u);
    CHECK_EQ(b.commands, 0u);
    CHECK(!b.esc.armed);
    bench_drive(&b, 3000u, 0u);
    CHECK(b.esc.edt);
    CHECK(b.edt.answered);
}

TEST_CASE(an_esc_that_never_answers_is_asked_a_bounded_number_of_times)
{
    /* An ESC that ignores command 13 answers every frame with a speed. */
    bench_t b;
    bench_init(&b, 0u);
    for (unsigned ms = 0; ms < 60000u; ++ms) {
        const bool ask = dshot_edt_frame(&b.edt, true, b.now_ms);
        if (ask) {
            ++b.commands;
            if (!b.in_ask && b.n_starts < 64u) {
                b.ask_start[b.n_starts++] = b.now_ms;
            }
        }
        b.in_ask = ask && b.edt.left != 0u;
        dshot_edt_heard(&b.edt, DSHOT_TELEM_ERPM);
        ++b.now_ms;
    }
    CHECK_EQ(b.commands, DSHOT_EDT_ASKS * DSHOT_CMD_REPEATS);
    CHECK_EQ(b.n_starts, DSHOT_EDT_ASKS);
    for (unsigned i = 1; i < b.n_starts; ++i) {
        /* Due DSHOT_EDT_RETRY_MS after the last repeat of the ask before. */
        CHECK_EQ(b.ask_start[i] - b.ask_start[i - 1],
                 DSHOT_EDT_RETRY_MS + DSHOT_CMD_REPEATS - 1u);
    }
    CHECK(!b.edt.answered);
}

TEST_CASE(every_run_asks_again_and_the_binding_keeps_reading_extended)
{
    bench_t b;
    bench_init(&b, 0u);
    bench_drive(&b, 3000u, 0u);
    CHECK(b.edt.answered);

    /* Disarmed long enough for AM32 to restart and forget. */
    bench_idle(&b, 1000u);
    CHECK(!b.esc.edt);
    CHECK(!b.edt.answered);
    CHECK(b.edt.asked);                     /* replies still read extended */

    const unsigned before = b.commands;
    bench_drive(&b, 3000u, 0u);
    CHECK(b.commands > before);
    CHECK(b.esc.edt);
    CHECK(b.edt.answered);

    /* A new binding is a new ESC as far as anyone knows. */
    dshot_edt_bind(&b.edt, b.now_ms);
    CHECK(!b.edt.asked);
    CHECK(!b.edt.answered);
}

TEST_CASE(the_retry_clock_survives_the_millisecond_wrap)
{
    bench_t b;
    bench_init(&b, 0xFFFFFFFFu - 700u);
    unsigned asks_seen = 0u;
    for (unsigned ms = 0; ms < 3000u; ++ms) {
        if (dshot_edt_frame(&b.edt, true, b.now_ms) && b.edt.left == 0u) {
            ++asks_seen;
        }
        ++b.now_ms;
    }
    /* Asks start at 0, 509, 1018, 1527, 2036, 2545 ms after the bind; the
     * wrap falls between the second and the third. */
    CHECK_EQ(asks_seen, 6u);
}

TEST_CASE(a_null_state_is_refused_everywhere)
{
    dshot_edt_bind(NULL, 0u);
    dshot_edt_idle(NULL, 0u);
    dshot_edt_heard(NULL, DSHOT_TELEM_STATUS);
    CHECK(!dshot_edt_frame(NULL, true, 0u));
}

TEST_CASE(an_extended_kind_before_any_ask_is_not_an_answer)
{
    dshot_edt_t e;
    dshot_edt_bind(&e, 0u);
    dshot_edt_heard(&e, DSHOT_TELEM_VOLTAGE);
    CHECK(!e.answered);
    CHECK(dshot_edt_frame(&e, true, 0u));
}

int main(void)
{
    RUN(one_ask_at_the_start_of_a_run_never_reaches_am32);
    RUN(am32_takes_the_repeated_ask_once_it_has_armed);
    RUN(the_status_frame_alone_is_heard_as_the_answer);
    RUN(an_esc_that_takes_the_first_ask_is_heard_on_it);
    RUN(the_first_frame_of_a_run_at_zero_throttle_is_the_ask);
    RUN(a_throttle_is_never_held_back_by_an_ask);
    RUN(a_run_armed_with_the_throttle_open_asks_once_it_closes);
    RUN(an_esc_that_never_answers_is_asked_a_bounded_number_of_times);
    RUN(every_run_asks_again_and_the_binding_keeps_reading_extended);
    RUN(the_retry_clock_survives_the_millisecond_wrap);
    RUN(a_null_state_is_refused_everywhere);
    RUN(an_extended_kind_before_any_ask_is_not_an_answer);
    return test_summary("dshot_edt");
}
