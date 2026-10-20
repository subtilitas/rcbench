/*
 * The panel's side of the PROG page: a port's PIN and FUNC written on the
 * operator's ask (shared/bench/prog_link.c, protocol 4.26).
 *
 * Under test: an ask refused with no page, no such port, a FUNC an IOmcu
 * older than 4.26 does not have and a FUNC no write takes; the ask
 * written at once on a bench at rest; a port that is the target let go
 * (pulse off, TARGET 0), the write, then the target and its pulse back;
 * nothing but a pulse switched off while the bench is not at rest or the
 * programmer holds a port; a refused write; a later ask replacing one
 * waiting; the link lost, or coming up again, with an ask waiting; the
 * page re-read after a written ask; an older IOmcu read as 24 registers
 * with every port SERVO PWM; a port as the page holds it, and not known
 * before the first read.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_pages.h"
#include "prog_link.h"

static prog_link_t g_p;
static uint16_t g_page[LINK_PG_REGS];
/* Registers the "IOmcu" refuses, bit r for register r. */
static uint32_t g_refuse;

/* A 4.26 IOmcu with 4 ports: port 0 on GP13 SERVO PWM and bound, the
 * others NONE without a pin, target the standard outputs, no pulse. */
static void page_fresh(void)
{
    memset(g_page, 0, sizeof(g_page));
    g_page[LINK_PG_COUNT] = 4u;
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0);
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 13u;
    for (unsigned n = 1; n < LINK_PG_PORTS_MAX; ++n) {
        g_page[LINK_PG_REG(n, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
        g_page[LINK_PG_FUNC(n)] = (uint16_t)LINK_PG_FN_NONE;
    }
    g_refuse = 0u;
}

static void fresh(void)
{
    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    prog_link_read(&g_p, g_page, 0u);
}

/* FLAGS as the IOmcu sets them: a port with a pin held by its
 * function. */
static void flags(void)
{
    uint16_t f = 0u;
    for (unsigned n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        if (g_page[LINK_PG_REG(n, LINK_PG_PIN)] == LINK_PG_PIN_NONE) {
            continue;
        }
        f |= (g_page[LINK_PG_FUNC(n)] == (uint16_t)LINK_PG_FN_SERVO_PWM)
                 ? LINK_PG_F_BOUND(n) : LINK_PG_F_PARKED(n);
    }
    g_page[LINK_PG_FLAGS] = f;
}

/* Carry out every write that is due as the IOmcu would, each one's
 * register in @p log, the page read again whenever the panel asks; the
 * count. */
static int run(bool quiet, uint8_t *log)
{
    prog_link_write_t w;
    int n = 0;

    while (n < 10) {
        if (prog_link_read_due(&g_p, 0u) && g_p.reread) {
            prog_link_read(&g_p, g_page, 0u);
        }
        if (!prog_link_next(&g_p, quiet, &w)) {
            break;
        }
        const bool ack = (g_refuse & (1u << w.off)) == 0u;
        if (ack) {
            g_page[w.off] = w.value;
            flags();
        }
        if (log != NULL) {
            log[n] = w.off;
        }
        prog_link_written(&g_p, &w, ack);
        ++n;
        if (!ack) {
            break;
        }
    }
    return n;
}

TEST_CASE(an_ask_is_refused_where_nothing_could_take_it)
{
    prog_link_init(&g_p);
    CHECK(!prog_link_ask(&g_p, 0u, PROG_LINK_ASK_PIN, 14u));   /* no link */
    CHECK(!prog_link_ask(NULL, 0u, PROG_LINK_ASK_PIN, 14u));
    prog_link_came_up(&g_p, LINK_MINOR_PROG - 1u);
    CHECK(!prog_link_ask(&g_p, 0u, PROG_LINK_ASK_PIN, 14u));   /* no page */
    fresh();
    CHECK(!prog_link_ask(&g_p, 4u, PROG_LINK_ASK_PIN, 14u));
    CHECK(!prog_link_ask(&g_p, 1u, PROG_LINK_ASK_FUNC, 2u));   /* DSHOT150 */
    CHECK(!prog_link_ask(&g_p, 1u, PROG_LINK_ASK_FUNC, 0xFFFFu));
    CHECK(!prog_link_ask(&g_p, 1u, (prog_link_ask_kind_t)7, 0u));
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_IDLE);
    /* A build with 2 ports has no port 2. */
    g_page[LINK_PG_COUNT] = 2u;
    prog_link_read(&g_p, g_page, 1u);
    CHECK(!prog_link_ask(&g_p, 2u, PROG_LINK_ASK_PIN, 14u));
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, 14u));
    CHECK_EQ(prog_link_ask_state(NULL, NULL, NULL, NULL), PROG_LINK_ASK_IDLE);
}

TEST_CASE(an_ask_on_a_free_port_is_written_at_once_at_rest)
{
    uint8_t log[10];

    fresh();
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, 14u));
    uint8_t port = 9u, kind = 9u;
    uint16_t value = 0u;
    CHECK_EQ(prog_link_ask_state(&g_p, &port, &kind, &value),
             PROG_LINK_ASK_WAITING);
    CHECK_EQ(port, 1u);
    CHECK_EQ(kind, (uint8_t)PROG_LINK_ASK_PIN);
    CHECK_EQ(value, 14u);
    prog_link_write_t w;
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_REG(1u, LINK_PG_PIN));
    CHECK_EQ(w.value, 14u);
    g_page[w.off] = w.value;
    prog_link_written(&g_p, &w, true);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_DONE);
    /* FLAGS moves with it: the page is read again now, and nothing is
     * written before, not even a target that waits. */
    prog_link_want(&g_p, 1u, false);
    CHECK(prog_link_read_due(&g_p, 1u));
    CHECK(!prog_link_next(&g_p, true, &w));
    prog_link_read(&g_p, g_page, 1u);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    prog_link_want(&g_p, 0u, false);
    CHECK_EQ(run(true, log), 0);
    /* A FUNC the same way, and a pin taken back to none. */
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_FUNC, LINK_PG_FN_SERVO_PWM));
    CHECK_EQ(run(true, log), 1);
    CHECK_EQ(log[0], LINK_PG_FUNC(1u));
    CHECK_EQ(g_page[LINK_PG_FUNC(1u)], (uint16_t)LINK_PG_FN_SERVO_PWM);
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, LINK_PG_PIN_NONE));
    CHECK_EQ(run(true, log), 1);
    CHECK_EQ(g_page[LINK_PG_REG(1u, LINK_PG_PIN)], LINK_PG_PIN_NONE);
    CHECK_EQ(run(true, log), 0);
}

TEST_CASE(the_target_port_is_let_go_for_the_write_and_comes_back)
{
    uint8_t log[10];

    fresh();
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(true, log), 2);           /* TARGET, then PWM */
    CHECK_EQ(g_page[LINK_PG_TARGET], 1u);
    CHECK_EQ(g_page[LINK_PG_PWM], 1u);
    CHECK(prog_link_ask(&g_p, 0u, PROG_LINK_ASK_PIN, 14u));
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    CHECK(!prog_link_settled(&g_p));
    CHECK_EQ(run(true, log), 5);
    CHECK_EQ(log[0], LINK_PG_PWM);         /* pulse off */
    CHECK_EQ(log[1], LINK_PG_TARGET);      /* target let go */
    CHECK_EQ(log[2], LINK_PG_REG(0u, LINK_PG_PIN));
    CHECK_EQ(log[3], LINK_PG_TARGET);      /* back */
    CHECK_EQ(log[4], LINK_PG_PWM);         /* and its pulse */
    CHECK_EQ(g_page[LINK_PG_REG(0u, LINK_PG_PIN)], 14u);
    CHECK_EQ(g_page[LINK_PG_TARGET], 1u);
    CHECK_EQ(g_page[LINK_PG_PWM], 1u);
    CHECK(prog_link_settled(&g_p));
}

TEST_CASE(an_ask_waits_for_a_bench_at_rest_and_a_free_port)
{
    uint8_t log[10];

    fresh();
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(true, log), 2);
    CHECK(prog_link_ask(&g_p, 0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_NONE));
    /* Not at rest: the pulse goes off and nothing else. */
    CHECK_EQ(run(false, log), 1);
    CHECK_EQ(log[0], LINK_PG_PWM);
    CHECK_EQ(run(false, log), 0);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL),
             PROG_LINK_ASK_WAITING);
    /* The programmer holds a port: nothing at all. */
    prog_link_hold(&g_p, true);
    CHECK_EQ(run(true, log), 0);
    prog_link_hold(&g_p, false);
    /* Not known: nothing. */
    prog_link_stale(&g_p);
    CHECK_EQ(run(true, log), 0);
    prog_link_read(&g_p, g_page, 5u);
    CHECK_EQ(run(true, log), 2);           /* TARGET 0, then FUNC */
    CHECK_EQ(log[1], LINK_PG_FUNC(0u));
    /* The port is NONE now: the IOmcu reports it parked, not bound, so it
     * is no target the screen can have, and nothing more is written. */
    CHECK_EQ(g_page[LINK_PG_FLAGS], LINK_PG_F_PARKED(0));
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    CHECK_EQ(run(true, log), 0);
}

TEST_CASE(a_refused_ask_says_so_and_makes_the_page_unknown)
{
    uint8_t log[10];

    fresh();
    g_refuse = 1u << LINK_PG_REG(2u, LINK_PG_PIN);
    CHECK(prog_link_ask(&g_p, 2u, PROG_LINK_ASK_PIN, 4u));
    CHECK_EQ(run(true, log), 1);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL),
             PROG_LINK_ASK_REFUSED);
    CHECK_EQ(prog_link_count(&g_p), PROG_LINK_COUNT_UNKNOWN);
    /* Read again: nothing more is written for it. */
    prog_link_read(&g_p, g_page, 1u);
    CHECK_EQ(run(true, log), 0);
}

TEST_CASE(a_later_ask_replaces_one_waiting)
{
    uint8_t log[10];

    fresh();
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, 14u));
    CHECK(prog_link_ask(&g_p, 2u, PROG_LINK_ASK_PIN, 15u));
    CHECK_EQ(run(true, log), 1);
    CHECK_EQ(log[0], LINK_PG_REG(2u, LINK_PG_PIN));
    CHECK_EQ(g_page[LINK_PG_REG(1u, LINK_PG_PIN)], LINK_PG_PIN_NONE);
}

TEST_CASE(an_ask_waiting_is_lost_with_the_link)
{
    fresh();
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, 14u));
    prog_link_lost(&g_p);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_LOST);
    /* Nor carried to the IOmcu that answers next. */
    fresh();
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, 14u));
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_LOST);
    prog_link_read(&g_p, g_page, 1u);
    CHECK_EQ(run(true, NULL), 0);
    /* A finished ask stays as it was. */
    CHECK(prog_link_ask(&g_p, 1u, PROG_LINK_ASK_PIN, 14u));
    CHECK_EQ(run(true, NULL), 1);
    prog_link_lost(&g_p);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_DONE);
}

TEST_CASE(an_older_iomcu_is_read_as_24_registers_all_servo_pwm)
{
    uint8_t log[10];

    prog_link_init(&g_p);
    CHECK_EQ(prog_link_regs(&g_p), LINK_PG_REGS_V424);
    CHECK(!prog_link_has_func(&g_p));
    page_fresh();
    g_page[LINK_PG_COUNT] = 1u;
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC - 1u);
    CHECK_EQ(prog_link_regs(&g_p), LINK_PG_REGS_V424);
    CHECK(!prog_link_has_func(&g_p));
    /* What lies past register 23 is not the page's. */
    g_page[LINK_PG_FUNC(0u)] = 0x77u;
    prog_link_read(&g_p, g_page, 0u);
    prog_link_port_t pt;
    CHECK(prog_link_port(&g_p, 0u, &pt));
    CHECK_EQ(pt.func, (uint8_t)LINK_PG_FN_SERVO_PWM);
    CHECK_EQ(pt.pin, 13u);
    CHECK(!pt.refused);
    CHECK(!prog_link_port(&g_p, 1u, &pt));
    /* No FUNC is asked of it; a PIN is, from 4.13. */
    CHECK(!prog_link_ask(&g_p, 0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_NONE));
    CHECK(prog_link_ask(&g_p, 0u, PROG_LINK_ASK_PIN, 14u));
    CHECK_EQ(run(true, log), 1);
    CHECK_EQ(log[0], LINK_PG_REG(0u, LINK_PG_PIN));
    CHECK_EQ(prog_link_regs(&g_p), LINK_PG_REGS_V424);
    /* A 4.26 IOmcu reads all 28. */
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    CHECK_EQ(prog_link_regs(&g_p), LINK_PG_REGS);
    CHECK(prog_link_has_func(&g_p));
}

TEST_CASE(a_port_is_unknown_before_the_first_read_then_as_the_page_holds_it)
{
    prog_link_port_t pt = { 0xAAu, 0xAAu, true };

    prog_link_init(&g_p);
    page_fresh();
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    /* Up, not read: no port is known, none reads as free. */
    CHECK(!prog_link_port(&g_p, 0u, &pt));
    CHECK_EQ(pt.pin, 0xAAu);
    CHECK_EQ(prog_link_count(&g_p), PROG_LINK_COUNT_UNKNOWN);
    g_page[LINK_PG_REG(1u, LINK_PG_PIN)] = 48u;
    g_page[LINK_PG_REG(2u, LINK_PG_PIN)] = 4u;
    g_page[LINK_PG_FLAGS] |= LINK_PG_F_PARKED(1u) | LINK_PG_F_REFUSED(2u);
    g_page[LINK_PG_FUNC(3u)] = 9u;
    prog_link_read(&g_p, g_page, 0u);
    CHECK(prog_link_port(&g_p, 0u, &pt));
    CHECK_EQ(pt.pin, 13u);
    CHECK_EQ(pt.func, (uint8_t)LINK_PG_FN_SERVO_PWM);
    CHECK(prog_link_port(&g_p, 1u, &pt));
    CHECK_EQ(pt.pin, 48u);
    CHECK_EQ(pt.func, (uint8_t)LINK_PG_FN_NONE);
    CHECK(!pt.refused);
    CHECK_EQ(prog_link_pin(&g_p, 1u), 48u);
    CHECK(prog_link_port(&g_p, 2u, &pt));
    CHECK_EQ(pt.pin, 4u);
    CHECK(pt.refused);
    CHECK_EQ(prog_link_pin(&g_p, 2u), PROG_LINK_NO_PIN);
    CHECK(prog_link_port(&g_p, 3u, &pt));
    CHECK_EQ(pt.pin, PROG_LINK_NO_PIN);
    CHECK_EQ(pt.func, (uint8_t)LINK_PG_FN_NONE);   /* 9 is no function */
    CHECK(!prog_link_port(&g_p, 4u, &pt));
    CHECK(!prog_link_port(&g_p, 0u, NULL));
    CHECK(!prog_link_port(NULL, 0u, &pt));
    /* A NONE port is no target. */
    prog_link_want(&g_p, 2u, true);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
}

int main(void)
{
    RUN(an_ask_is_refused_where_nothing_could_take_it);
    RUN(an_ask_on_a_free_port_is_written_at_once_at_rest);
    RUN(the_target_port_is_let_go_for_the_write_and_comes_back);
    RUN(an_ask_waits_for_a_bench_at_rest_and_a_free_port);
    RUN(a_refused_ask_says_so_and_makes_the_page_unknown);
    RUN(a_later_ask_replaces_one_waiting);
    RUN(an_ask_waiting_is_lost_with_the_link);
    RUN(an_older_iomcu_is_read_as_24_registers_all_servo_pwm);
    RUN(a_port_is_unknown_before_the_first_read_then_as_the_page_holds_it);
    return test_summary("prog_link_ask");
}
