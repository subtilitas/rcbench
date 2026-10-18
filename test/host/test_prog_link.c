/*
 * The panel's side of the programming ports' page: what is read, what is
 * wanted and the one write between them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_pages.h"
#include "prog_link.h"

static prog_link_t g_p;
static uint16_t g_page[LINK_PG_REGS];

/* An IOmcu with 1 port on GP13, target the standard outputs, no
 * pulse, as it starts. */
static void page_fresh(void)
{
    memset(g_page, 0, sizeof(g_page));
    g_page[LINK_PG_COUNT] = 1u;
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0);
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 13u;
    for (unsigned n = 1; n < LINK_PG_PORTS_MAX; ++n) {
        g_page[LINK_PG_REG(n, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
    }
}

static void fresh(void)
{
    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_PROG);
}

static void read_at(uint32_t ms)
{
    prog_link_read(&g_p, g_page, ms);
}

/* Carry out every write that is due as the IOmcu would; the count. */
static int run(bool quiet)
{
    prog_link_write_t w;
    int n = 0;

    while (n < 10 && prog_link_next(&g_p, quiet, &w)) {
        g_page[w.off] = w.value;
        prog_link_written(&g_p, &w, true);
        ++n;
    }
    return n;
}

TEST_CASE(nothing_is_asked_of_a_coprocessor_without_the_page)
{
    prog_link_write_t w;

    prog_link_init(&g_p);
    CHECK(!prog_link_read_due(&g_p, 0u));
    CHECK(prog_link_settled(&g_p));
    prog_link_came_up(&g_p, LINK_MINOR_PROG - 1u);
    prog_link_want(&g_p, 1u, true);
    CHECK(!prog_link_read_due(&g_p, 1000u));
    page_fresh();
    prog_link_read(&g_p, g_page, 0u);
    CHECK(!prog_link_next(&g_p, true, &w));
    /* The tester has the standard outputs and may command them. */
    CHECK_EQ(prog_link_target(&g_p), 0u);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    CHECK(prog_link_settled(&g_p));
    CHECK(!prog_link_pending(&g_p));
    CHECK(!prog_link_pwm(&g_p));
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
}

TEST_CASE(nothing_is_written_before_the_page_is_read)
{
    prog_link_write_t w;

    fresh();
    prog_link_want(&g_p, 1u, true);
    CHECK(prog_link_read_due(&g_p, 0u));
    CHECK(!prog_link_next(&g_p, true, &w));
    CHECK(!prog_link_settled(&g_p));
    CHECK(!prog_link_pending(&g_p));
    CHECK_EQ(prog_link_target(&g_p), 0u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
    /* A read that is not answered changes nothing and is asked again. */
    prog_link_read(&g_p, NULL, 10u);
    CHECK(prog_link_read_due(&g_p, 11u));
    CHECK(!prog_link_next(&g_p, true, &w));
    read_at(20u);
    CHECK(prog_link_next(&g_p, true, &w));
}

TEST_CASE(the_page_is_read_every_500_ms_and_the_clock_may_wrap)
{
    fresh();
    read_at(0xFFFFFF00u);
    CHECK(!prog_link_read_due(&g_p, 0xFFFFFF00u));
    CHECK(!prog_link_read_due(&g_p, 0xFFFFFF00u + PROG_LINK_POLL_MS - 1u));
    /* 500 ms later the counter has wrapped. */
    CHECK(prog_link_read_due(&g_p, 0xFFFFFF00u + PROG_LINK_POLL_MS));
    CHECK_EQ((uint32_t)(0xFFFFFF00u + PROG_LINK_POLL_MS), 244u);
    read_at(244u);
    CHECK(!prog_link_read_due(&g_p, 300u));
    prog_link_stale(&g_p);
    CHECK(prog_link_read_due(&g_p, 300u));
}

TEST_CASE(the_port_becomes_the_target_and_then_renders_its_pulse)
{
    prog_link_write_t w;

    fresh();
    read_at(0u);
    CHECK(prog_link_settled(&g_p));
    prog_link_want(&g_p, 1u, true);
    CHECK(prog_link_pending(&g_p));
    CHECK(!prog_link_settled(&g_p));
    /* Not under an armed bank or a held surface. */
    CHECK(!prog_link_next(&g_p, false, &w));
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    CHECK_EQ(w.value, 1u);
    g_page[w.off] = w.value;
    prog_link_written(&g_p, &w, true);
    CHECK_EQ(prog_link_target(&g_p), 1u);
    CHECK(prog_link_settled(&g_p));
    CHECK(!prog_link_pwm(&g_p));
    /* The pulse after the target, and not under an armed bank either. */
    CHECK(!prog_link_next(&g_p, false, &w));
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_PWM);
    CHECK_EQ(w.value, 0x0001u);
    g_page[w.off] = w.value;
    prog_link_written(&g_p, &w, true);
    CHECK(prog_link_pwm(&g_p));
    CHECK(!prog_link_next(&g_p, true, &w));
}

/*
 * The write that switches a port's pulse on names the port, so the panel
 * can state the port's range before it: an IOmcu from protocol 4.16
 * refuses the pulse while the range is longer than the frame.  The target
 * write, a write that switches the pulse off, a write naming no new port,
 * and any write on an unknown page name none.
 */
TEST_CASE(the_write_that_switches_a_pulse_on_names_its_port)
{
    prog_link_write_t w;

    fresh();
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    CHECK_EQ(prog_link_pulse_on(&g_p, &w), 0u);
    g_page[w.off] = w.value;
    prog_link_written(&g_p, &w, true);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_PWM);
    CHECK_EQ(prog_link_pulse_on(&g_p, &w), 1u);
    /* Port 2's bit alone names port 2. */
    const prog_link_write_t two = { .off = LINK_PG_PWM, .value = 0x0004u };
    CHECK_EQ(prog_link_pulse_on(&g_p, &two), 3u);
    g_page[w.off] = w.value;
    prog_link_written(&g_p, &w, true);
    /* A pulse already on is not switched on again, and off is not on. */
    CHECK_EQ(prog_link_pulse_on(&g_p, &w), 0u);
    const prog_link_write_t off = { .off = LINK_PG_PWM, .value = 0u };
    CHECK_EQ(prog_link_pulse_on(&g_p, &off), 0u);
    /* Unknown page, no write, no object: none. */
    prog_link_written(&g_p, &off, false);
    CHECK_EQ(prog_link_pulse_on(&g_p, &two), 0u);
    CHECK_EQ(prog_link_pulse_on(&g_p, NULL), 0u);
    CHECK_EQ(prog_link_pulse_on(NULL, &two), 0u);
}

TEST_CASE(the_pulse_goes_off_at_any_time_and_before_the_target_changes)
{
    prog_link_write_t w;

    fresh();
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(true), 2);
    /* The switch off, with the bank armed. */
    prog_link_want(&g_p, 1u, false);
    CHECK(prog_link_next(&g_p, false, &w));
    CHECK_EQ(w.off, LINK_PG_PWM);
    CHECK_EQ(w.value, 0u);
    CHECK_EQ(run(false), 1);
    CHECK(!prog_link_pwm(&g_p));
    CHECK(prog_link_settled(&g_p));
    /* On again waits for a quiet bench. */
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(false), 0);
    CHECK_EQ(run(true), 1);
    CHECK(prog_link_pwm(&g_p));
    /* Back to the standard outputs: the pulse off at once, the target
     * when the bench is quiet. */
    prog_link_want(&g_p, 0u, true);
    CHECK_EQ(run(false), 1);
    CHECK_EQ(g_page[LINK_PG_PWM], 0u);
    CHECK_EQ(g_page[LINK_PG_TARGET], 1u);
    CHECK(prog_link_pending(&g_p));
    CHECK(!prog_link_settled(&g_p));
    CHECK_EQ(run(true), 1);
    CHECK_EQ(g_page[LINK_PG_TARGET], 0u);
    CHECK(prog_link_settled(&g_p));
    CHECK(!prog_link_pwm(&g_p));
}

TEST_CASE(a_port_the_page_does_not_have_is_no_target)
{
    fresh();
    read_at(0u);
    /* Port 1 of a build with 1 port; port 4 of none. */
    prog_link_want(&g_p, 2u, true);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    CHECK_EQ(run(true), 0);
    CHECK(prog_link_settled(&g_p));
    prog_link_want(&g_p, LINK_PG_PORTS_MAX + 1u, true);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    prog_link_want(&g_p, 255u, true);
    CHECK_EQ(run(true), 0);
    /* The last port a page can have, without a pin: the IOmcu
     * refuses it as a target, so it is not asked. */
    g_page[LINK_PG_COUNT] = LINK_PG_PORTS_MAX;
    read_at(1u);
    prog_link_want(&g_p, LINK_PG_PORTS_MAX, true);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    CHECK_EQ(run(true), 0);
    CHECK(prog_link_settled(&g_p));
    g_page[LINK_PG_FLAGS] |= LINK_PG_F_REFUSED(LINK_PG_PORTS_MAX - 1u);
    read_at(2u);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    /* Bound. */
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0)
                            | LINK_PG_F_BOUND(LINK_PG_PORTS_MAX - 1u);
    read_at(3u);
    prog_link_want(&g_p, LINK_PG_PORTS_MAX, true);
    CHECK_EQ(prog_link_wanted(&g_p), LINK_PG_PORTS_MAX);
    CHECK_EQ(run(true), 2);
    CHECK_EQ(g_page[LINK_PG_TARGET], LINK_PG_PORTS_MAX);
    CHECK_EQ(g_page[LINK_PG_PWM], 0x0008u);
    CHECK(prog_link_pwm(&g_p));
    /* A page with no port at all. */
    fresh();
    g_page[LINK_PG_COUNT] = 0u;
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(true), 0);
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
}

TEST_CASE(a_port_that_loses_its_pin_gives_the_target_back)
{
    fresh();
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(true), 2);
    CHECK_EQ(prog_link_target(&g_p), 1u);
    /* A slot takes GP13: the port has no pin, and the standard outputs are
     * the target the screen can have.  The pulse bit goes first. */
    g_page[LINK_PG_FLAGS] = 0u;
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
    read_at(500u);
    CHECK_EQ(prog_link_wanted(&g_p), 0u);
    CHECK(prog_link_pending(&g_p));
    CHECK(!prog_link_settled(&g_p));
    CHECK_EQ(run(false), 1);
    CHECK_EQ(g_page[LINK_PG_PWM], 0u);
    CHECK_EQ(g_page[LINK_PG_TARGET], 1u);
    CHECK_EQ(run(true), 1);
    CHECK_EQ(g_page[LINK_PG_TARGET], 0u);
    CHECK(prog_link_settled(&g_p));
}

TEST_CASE(a_release_waits_for_a_read_and_not_for_the_target)
{
    /* No page: the standard outputs, at once. */
    prog_link_init(&g_p);
    CHECK(prog_link_ready(&g_p));
    prog_link_came_up(&g_p, LINK_MINOR_PROG - 1u);
    CHECK(prog_link_ready(&g_p));
    CHECK(prog_link_ready(NULL));
    /* A page not yet read: whose pulse it is is not known. */
    fresh();
    CHECK(!prog_link_ready(&g_p));
    read_at(0u);
    CHECK(prog_link_ready(&g_p));
    /* A target change that waits does not hold a release. */
    prog_link_want(&g_p, 1u, true);
    CHECK(!prog_link_settled(&g_p));
    CHECK(prog_link_ready(&g_p));
    /* A failed read, a link lost and found. */
    prog_link_read(&g_p, NULL, 1u);
    CHECK(!prog_link_ready(&g_p));
    prog_link_lost(&g_p);
    CHECK(prog_link_ready(&g_p));
    prog_link_came_up(&g_p, LINK_MINOR_PROG);
    CHECK(!prog_link_ready(&g_p));
}

TEST_CASE(a_coprocessor_that_started_again_is_brought_back)
{
    prog_link_write_t w;

    fresh();
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK_EQ(run(true), 2);
    /* It restarts: target 0, no pulse.  The next read shows it. */
    page_fresh();
    read_at(PROG_LINK_POLL_MS);
    CHECK_EQ(prog_link_target(&g_p), 0u);
    CHECK(!prog_link_settled(&g_p));
    CHECK(prog_link_pending(&g_p));
    CHECK_EQ(run(false), 0);
    CHECK_EQ(run(true), 2);
    CHECK(prog_link_settled(&g_p));
    /* Pulse bits nobody wants beside the wanted one. */
    g_page[LINK_PG_PWM] = 0x0006u | 0x0001u;
    read_at(2u * PROG_LINK_POLL_MS);
    CHECK(prog_link_next(&g_p, false, &w));
    CHECK_EQ(w.off, LINK_PG_PWM);
    CHECK_EQ(w.value, 0x0001u);
    CHECK_EQ(run(false), 1);
    /* A target past the ports. */
    g_page[LINK_PG_TARGET] = 9u;
    read_at(3u * PROG_LINK_POLL_MS);
    CHECK_EQ(prog_link_target(&g_p), 0u);
    CHECK(!prog_link_settled(&g_p));
    CHECK(!prog_link_pwm(&g_p));
    CHECK_EQ(run(true), 3);
    CHECK_EQ(g_page[LINK_PG_TARGET], 1u);
    CHECK_EQ(g_page[LINK_PG_PWM], 0x0001u);
}

TEST_CASE(a_write_that_is_not_acknowledged_makes_the_page_unknown)
{
    prog_link_write_t w;

    fresh();
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK(prog_link_next(&g_p, true, &w));
    prog_link_written(&g_p, &w, false);
    CHECK(!prog_link_settled(&g_p));
    CHECK(!prog_link_next(&g_p, true, &w));
    CHECK(prog_link_read_due(&g_p, 1u));
    CHECK_EQ(prog_link_target(&g_p), 0u);
    /* Read again, the write is due again. */
    read_at(2u);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    /* The link goes: no page, and what is wanted stays for the next. */
    prog_link_lost(&g_p);
    CHECK(!prog_link_read_due(&g_p, 3u));
    CHECK(!prog_link_next(&g_p, true, &w));
    CHECK(prog_link_settled(&g_p));
    prog_link_written(&g_p, &w, true);
    prog_link_read(&g_p, g_page, 4u);
    CHECK_EQ(prog_link_target(&g_p), 0u);
    prog_link_came_up(&g_p, LINK_MINOR_PROG);
    CHECK(prog_link_read_due(&g_p, 5u));
    read_at(5u);
    CHECK_EQ(run(true), 2);
    CHECK_EQ(g_page[LINK_PG_TARGET], 1u);
}

TEST_CASE(a_port_names_its_pin_unless_it_has_none_or_was_refused)
{
    fresh();
    read_at(0u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), 13u);
    CHECK_EQ(prog_link_pin(&g_p, 1u), PROG_LINK_NO_PIN);
    CHECK_EQ(prog_link_pin(&g_p, LINK_PG_PORTS_MAX), PROG_LINK_NO_PIN);
    CHECK_EQ(prog_link_pin(&g_p, 255u), PROG_LINK_NO_PIN);
    /* A stored slot on the pin won at start: the port reads refused. */
    g_page[LINK_PG_FLAGS] = LINK_PG_F_REFUSED(0);
    read_at(1u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
    g_page[LINK_PG_FLAGS] = 0u;
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
    read_at(2u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 0u;
    read_at(3u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), 0u);
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 300u;
    read_at(4u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
    /* A register above one byte never truncates into a pin. */
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 0x0100u;
    read_at(5u);
    CHECK_EQ(prog_link_pin(&g_p, 0u), PROG_LINK_NO_PIN);
}

/* A malformed page: a port count above the ports the page has room for
 * names no pin for the port past the last one, whose registers lie beyond
 * the page. */
TEST_CASE(a_count_above_the_ports_the_page_holds_names_no_pin)
{
    fresh();
    g_page[LINK_PG_COUNT] = LINK_PG_PORTS_MAX + 1u;
    read_at(0u);
    CHECK_EQ(prog_link_pin(&g_p, LINK_PG_PORTS_MAX), PROG_LINK_NO_PIN);
}

/* While the servo programmer holds the port, the drive does not change: it
 * keeps the target the IOmcu holds, a port or the standard outputs;
 * the programmer drives the port and the tester sends nothing. */
TEST_CASE(the_drive_keeps_its_target_while_the_programmer_holds_the_port)
{
    prog_link_drive_t d;

    fresh();
    g_page[LINK_PG_TARGET] = 1u;
    read_at(1u);
    prog_link_hold(&g_p, true);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, 13u);
    prog_link_hold(&g_p, false);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, 13u);

    /* Held with the standard outputs as target: still target 0, no pin. */
    fresh();
    g_page[LINK_PG_TARGET] = 0u;
    read_at(1u);
    prog_link_hold(&g_p, true);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 0u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    CHECK(!d.pwm);
}

/* What the tester drives: the target the commands use, that port's pin and
 * its pulse.  A port target without a usable pin stays a port target, with
 * no pin.  Before the page is known, and with no link, where the commands
 * would go is not known and the tester sends none: neither is the outputs. */
TEST_CASE(the_drive_names_the_target_and_the_port_pin)
{
    prog_link_drive_t d;

    d = prog_link_drive(NULL);
    CHECK_EQ(d.target, 0u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    CHECK(!d.pwm);
    /* No IOmcu has answered yet: no link. */
    prog_link_init(&g_p);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, PROG_LINK_TARGET_NO_LINK);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    /* One without the page: the standard outputs, commanded at once. */
    prog_link_came_up(&g_p, LINK_MINOR_PROG - 1u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 0u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    CHECK(prog_link_command(&g_p, false));
    fresh();
    /* Not read yet: not known, and no command goes out. */
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, PROG_LINK_TARGET_UNKNOWN);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    CHECK(!d.pwm);
    CHECK(!prog_link_command(&g_p, false));
    CHECK(!prog_link_command(&g_p, true));
    /* Target 0: the outputs, and no port pin although port 0 has GP13. */
    read_at(0u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 0u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    /* Target 1 is port 0 on GP13, its pulse off. */
    g_page[LINK_PG_TARGET] = 1u;
    read_at(1u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, 13u);
    CHECK(!d.pwm);
    /* And on. */
    g_page[LINK_PG_PWM] = 1u;
    read_at(1u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, 13u);
    CHECK(d.pwm);
    /* Another port's pulse bit is not the target's. */
    g_page[LINK_PG_PWM] = 2u;
    read_at(1u);
    CHECK(!prog_link_drive(&g_p).pwm);
    g_page[LINK_PG_PWM] = 0u;
    /* Port 0 without a pin. */
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
    read_at(2u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    /* Port 0 refused. */
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 13u;
    g_page[LINK_PG_FLAGS] = LINK_PG_F_REFUSED(0);
    read_at(3u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    /* Target 2 names port 1, which a page of 1 port does not have. */
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0);
    g_page[LINK_PG_TARGET] = 2u;
    g_page[LINK_PG_REG(1, LINK_PG_PIN)] = 14u;
    read_at(4u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 2u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    /* The same target with 2 ports names port 1's pin. */
    g_page[LINK_PG_COUNT] = 2u;
    read_at(5u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 2u);
    CHECK_EQ(d.pin, 14u);
    /* A target above LINK_PG_PORTS_MAX is the outputs. */
    g_page[LINK_PG_TARGET] = LINK_PG_PORTS_MAX + 1u;
    read_at(6u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 0u);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    /* The link lost with port 0 the target: no link, not the outputs. */
    g_page[LINK_PG_TARGET] = 1u;
    g_page[LINK_PG_PWM] = 1u;
    read_at(7u);
    prog_link_lost(&g_p);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, PROG_LINK_TARGET_NO_LINK);
    CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
    CHECK(!d.pwm);
    /* Back up against an IOmcu that still holds the port: not known
     * until read, then the port. */
    prog_link_came_up(&g_p, LINK_MINOR_PROG);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, PROG_LINK_TARGET_UNKNOWN);
    read_at(8u);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, 13u);
    CHECK(d.pwm);
    /* A read not answered: not known. */
    prog_link_read(&g_p, NULL, 9u);
    CHECK_EQ(prog_link_drive(&g_p).target, PROG_LINK_TARGET_UNKNOWN);
}

/* A write the IOmcu does not acknowledge leaves the page unknown, and
 * the drive with it: the port it holds is not named the outputs while the
 * tester sends nothing, however often the write is refused. */
TEST_CASE(a_write_not_acknowledged_leaves_the_drive_unknown)
{
    prog_link_write_t w;
    prog_link_drive_t d;

    fresh();
    g_page[LINK_PG_TARGET] = 1u;
    prog_link_want(&g_p, 1u, true);
    for (uint32_t pass = 0u; pass < 3u; ++pass) {
        read_at(pass * PROG_LINK_POLL_MS);
        d = prog_link_drive(&g_p);
        CHECK_EQ(d.target, 1u);
        CHECK_EQ(d.pin, 13u);
        CHECK(!d.pwm);
        /* The pulse switched on, refused (BAD_VALUE). */
        CHECK(prog_link_next(&g_p, true, &w));
        CHECK_EQ(w.off, (uint8_t)LINK_PG_PWM);
        CHECK_EQ(w.value, 1u);
        prog_link_written(&g_p, &w, false);
        d = prog_link_drive(&g_p);
        CHECK_EQ(d.target, PROG_LINK_TARGET_UNKNOWN);
        CHECK_EQ(d.pin, PROG_LINK_NO_PIN);
        CHECK(!d.pwm);
        CHECK(!prog_link_command(&g_p, false));
    }
    /* Acknowledged: the port, its pulse on. */
    read_at(3u * PROG_LINK_POLL_MS);
    CHECK(prog_link_next(&g_p, true, &w));
    prog_link_written(&g_p, &w, true);
    d = prog_link_drive(&g_p);
    CHECK_EQ(d.target, 1u);
    CHECK_EQ(d.pin, 13u);
    CHECK(d.pwm);
}

TEST_CASE(a_channel_configuration_maps_onto_a_port)
{
    uint16_t cc[LINK_CC_STRIDE];
    uint16_t out[PROG_LINK_CFG_REGS] = { 1u, 2u, 3u };

    memset(cc, 0, sizeof(cc));
    cc[LINK_CC_SLEW]   = 250u;
    cc[LINK_CC_MIN_US] = 660u;
    cc[LINK_CC_MAX_US] = 860u;
    prog_link_cfg(NULL, out);
    CHECK_EQ(out[0], 1u);
    prog_link_cfg(cc, NULL);
    prog_link_cfg(cc, out);
    /* cppcheck-suppress duplicateExpression
     * the page's offset from itself: the first register */
    CHECK_EQ(out[LINK_PG_SLEW - LINK_PG_SLEW], 250u);
    CHECK_EQ(out[LINK_PG_MIN_US - LINK_PG_SLEW], 660u);
    CHECK_EQ(out[LINK_PG_MAX_US - LINK_PG_SLEW], 860u);
    CHECK_EQ(LINK_PG_MAX_US - LINK_PG_SLEW + 1, (int)PROG_LINK_CFG_REGS);
}

TEST_CASE(no_object_is_no_fault)
{
    prog_link_write_t w = { 0 };

    prog_link_init(NULL);
    prog_link_came_up(NULL, LINK_MINOR_PROG);
    prog_link_lost(NULL);
    prog_link_want(NULL, 1u, true);
    prog_link_read(NULL, g_page, 0u);
    prog_link_stale(NULL);
    prog_link_written(NULL, &w, true);
    CHECK(!prog_link_read_due(NULL, 0u));
    CHECK(!prog_link_next(NULL, true, &w));
    CHECK(prog_link_settled(NULL));
    CHECK(!prog_link_pending(NULL));
    CHECK(!prog_link_pwm(NULL));
    CHECK_EQ(prog_link_target(NULL), 0u);
    CHECK_EQ(prog_link_wanted(NULL), 0u);
    CHECK_EQ(prog_link_pin(NULL, 0u), PROG_LINK_NO_PIN);
    fresh();
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK(!prog_link_next(&g_p, true, NULL));
    prog_link_written(&g_p, NULL, true);
    CHECK(prog_link_next(&g_p, true, &w));
    /* A write past the page is no write of this module. */
    w.off = LINK_PG_REGS;
    prog_link_written(&g_p, &w, true);
    CHECK(prog_link_read_due(&g_p, 1u));
}


/* The page as the IOmcu holds it differs from what is wanted in
 * every way a write could mend: nothing is written while the programmer
 * has a port, on a quiet bench or not, and no pulse is switched off. */
TEST_CASE(nothing_is_written_while_the_programmer_has_a_port)
{
    prog_link_write_t w;

    for (unsigned c = 0; c < 16u; ++c) {
        /* What the page holds and what is wanted, each 0 or 1. */
        const uint16_t has_target = (uint16_t)(c & 1u);
        const uint16_t has_pwm    = (uint16_t)((c >> 1) & 1u);
        const uint8_t  want_t     = (uint8_t)((c >> 2) & 1u);
        const bool     want_p     = ((c >> 3) & 1u) != 0u;

        fresh();
        g_page[LINK_PG_TARGET] = has_target;
        g_page[LINK_PG_PWM]    = has_pwm;
        read_at(0u);
        prog_link_want(&g_p, want_t, want_p);
        const bool due_quiet = prog_link_next(&g_p, true, &w);
        const bool due_busy  = prog_link_next(&g_p, false, &w);
        const bool pending   = prog_link_pending(&g_p);
        CHECK(!prog_link_held(&g_p));
        prog_link_hold(&g_p, true);
        CHECK(prog_link_held(&g_p));
        CHECK(!prog_link_next(&g_p, true, &w));
        CHECK(!prog_link_next(&g_p, false, &w));
        CHECK(!prog_link_pending(&g_p));
        CHECK_EQ(run(true), 0);
        /* What the screen asks for meanwhile is not taken. */
        prog_link_want(&g_p, (uint8_t)(1u - want_t), !want_p);
        CHECK_EQ(g_p.want_target, want_t);
        CHECK_EQ(g_p.want_pwm, want_p);
        CHECK(!prog_link_next(&g_p, true, &w));
        /* A read under the hold changes nothing of it. */
        read_at(PROG_LINK_POLL_MS);
        CHECK(!prog_link_next(&g_p, true, &w));
        CHECK_EQ(g_page[LINK_PG_TARGET], has_target);
        CHECK_EQ(g_page[LINK_PG_PWM], has_pwm);
        /* The tester: no command and no release to a port, every command
         * to the standard outputs it is settled on. */
        CHECK_EQ(prog_link_command(&g_p, true), has_target == 0u);
        CHECK_EQ(prog_link_command(&g_p, false),
                 has_target == 0u && prog_link_settled(&g_p));
        /* The hold over: what was due is due again. */
        prog_link_hold(&g_p, false);
        CHECK_EQ(prog_link_next(&g_p, true, &w), due_quiet);
        CHECK_EQ(prog_link_next(&g_p, false, &w), due_busy);
        CHECK_EQ(prog_link_pending(&g_p), pending);
        CHECK(prog_link_command(&g_p, true));
        CHECK_EQ(prog_link_command(&g_p, false), prog_link_settled(&g_p));
        /* And what the screen asks for is taken. */
        prog_link_want(&g_p, (uint8_t)(1u - want_t), !want_p);
        CHECK_EQ(g_p.want_target, (uint8_t)(1u - want_t));
    }
    /* A page not read takes no command, held or not; no page takes all. */
    fresh();
    CHECK(!prog_link_command(&g_p, true));
    CHECK(!prog_link_command(&g_p, false));
    prog_link_init(&g_p);
    prog_link_hold(&g_p, true);
    CHECK(prog_link_command(&g_p, true));
    CHECK(prog_link_command(&g_p, false));
    /* The link lost and back: the hold is the caller's to say again. */
    fresh();
    prog_link_hold(&g_p, true);
    prog_link_lost(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_PROG);
    read_at(0u);
    prog_link_want(&g_p, 1u, true);
    CHECK(prog_link_held(&g_p));
    CHECK(!prog_link_next(&g_p, true, &w));
    prog_link_hold(NULL, true);
    CHECK(!prog_link_held(NULL));
    CHECK(prog_link_command(NULL, false));
}

/*
 * Protocol 4.19: a TARGET change returns the SERVO rate to 0.  An
 * acknowledged TARGET write that changes it, and a read that finds it
 * changed, say RESET once; an unanswered TARGET write says UNKNOWN; the
 * stronger of the two is kept until asked.  The first TARGET known after a
 * link-up, a write of the TARGET in force and an IOmcu below 4.19 say
 * KEPT; below 4.19 a changed TARGET read and an unanswered TARGET write
 * too.
 */
TEST_CASE(a_target_change_returns_the_servo_rate_to_0_from_4_19)
{
    prog_link_write_t w;

    page_fresh();
    prog_link_init(&g_p);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(0u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* The port becomes the target: one RESET, then KEPT. */
    prog_link_want(&g_p, 1u, false);
    CHECK_EQ(run(true), 1);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_RESET);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    read_at(500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* The IOmcu gives the target back itself (the port lost its
     * pin): the read says RESET. */
    g_page[LINK_PG_TARGET] = 0u;
    g_page[LINK_PG_FLAGS]  = 0u;
    read_at(1000u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_RESET);
    /* A TARGET write nobody answered: UNKNOWN, and a read after it that
     * finds the TARGET changed keeps UNKNOWN until asked. */
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0);
    read_at(1500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    prog_link_written(&g_p, &w, false);
    g_page[LINK_PG_TARGET] = 1u;
    read_at(2000u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_UNKNOWN);
    read_at(2500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* A refused write of the same kind is no answer either. */
    prog_link_want(&g_p, 0u, false);
    CHECK(prog_link_next(&g_p, true, &w));
    prog_link_written(&g_p, &w, false);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_UNKNOWN);
    /* A link-up forgets what was seen and owed. */
    g_page[LINK_PG_TARGET] = 1u;
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(3000u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    g_page[LINK_PG_TARGET] = 0u;
    prog_link_lost(&g_p);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* Below 4.19 the rate does not follow the target. */
    page_fresh();
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE - 1u);
    read_at(4000u);
    prog_link_want(&g_p, 1u, false);
    CHECK_EQ(run(true), 1);
    CHECK(prog_link_next(&g_p, true, &w) == false);
    g_page[LINK_PG_TARGET] = 0u;
    read_at(4500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* Nor a read that finds TARGET changed, 0 then 1, nor a TARGET write
     * nobody answered. */
    g_page[LINK_PG_TARGET] = 1u;
    read_at(5000u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    prog_link_want(&g_p, 0u, false);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    prog_link_written(&g_p, &w, false);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    CHECK_EQ(prog_link_rate(NULL), PROG_LINK_RATE_KEPT);
}

/*
 * An unanswered TARGET write may still have landed.  It leaves no TARGET
 * known, so the first read after it, once its UNKNOWN has been asked for,
 * finds nothing to compare against and says KEPT: the panel already
 * counts the rate as unknown, and a RESET there would count 50 Hz while
 * the new TARGET runs at the rate written since.
 */
TEST_CASE(a_read_after_an_unanswered_target_write_compares_against_nothing)
{
    prog_link_write_t w;

    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(0u);
    prog_link_want(&g_p, 1u, false);
    CHECK(prog_link_next(&g_p, true, &w));
    CHECK_EQ(w.off, LINK_PG_TARGET);
    prog_link_written(&g_p, &w, false);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_UNKNOWN);
    /* The write had landed: the read finds the port as the TARGET. */
    g_page[LINK_PG_TARGET] = 1u;
    read_at(500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
}

/*
 * A link-up starts the TARGET record over even without a link loss before
 * it: the first TARGET read after it says KEPT, and an UNKNOWN owed from
 * before it is forgotten.  An UNKNOWN not yet asked for is not lowered to
 * RESET by a read that finds the TARGET changed.
 */
TEST_CASE(a_link_up_starts_the_target_record_over)
{
    prog_link_write_t w;

    /* Seen 0, a link-up, then 1: the first TARGET after it. */
    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(0u);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    g_page[LINK_PG_TARGET] = 1u;
    read_at(500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* An UNKNOWN not asked for before a link-up is not owed after it. */
    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(1000u);
    prog_link_want(&g_p, 1u, false);
    CHECK(prog_link_next(&g_p, true, &w));
    prog_link_written(&g_p, &w, false);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(1500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* UNKNOWN, then reads of 0 and 1: still UNKNOWN when asked. */
    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(2000u);
    prog_link_want(&g_p, 1u, false);
    CHECK(prog_link_next(&g_p, true, &w));
    prog_link_written(&g_p, &w, false);
    read_at(2500u);
    g_page[LINK_PG_TARGET] = 1u;
    read_at(3000u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_UNKNOWN);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
}

/*
 * Only a TARGET write feeds the TARGET record: a port's command or the pulse
 * register written, answered or not, leaves the rate KEPT and the TARGET
 * seen, so a read that then finds the TARGET changed still says RESET.
 */
TEST_CASE(a_write_to_another_register_leaves_the_target_record)
{
    prog_link_write_t w;

    page_fresh();
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_SERVO_TARGET_RATE);
    read_at(0u);
    w.off   = (uint8_t)LINK_PG_REG(0, LINK_PG_CMD);
    w.value = 5u;
    prog_link_written(&g_p, &w, true);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    w.off   = (uint8_t)LINK_PG_PWM;
    w.value = 1u;
    prog_link_written(&g_p, &w, true);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    w.off   = (uint8_t)LINK_PG_REG(0, LINK_PG_CMD);
    w.value = 7u;
    prog_link_written(&g_p, &w, false);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_KEPT);
    /* The TARGET seen before them is still the one compared against. */
    g_page[LINK_PG_TARGET] = 1u;
    read_at(500u);
    CHECK_EQ(prog_link_rate(&g_p), PROG_LINK_RATE_RESET);
}

TEST_CASE(the_port_count_is_the_page_s_up_to_the_ports_it_holds)
{
    /* Nothing known: no page, no read, a lost link, an old IOmcu. */
    CHECK_EQ(prog_link_count(NULL), 0u);
    fresh();
    CHECK_EQ(prog_link_count(&g_p), 0u);
    read_at(0u);
    CHECK_EQ(prog_link_count(&g_p), 1u);
    g_page[LINK_PG_COUNT] = 0u;
    read_at(1u);
    CHECK_EQ(prog_link_count(&g_p), 0u);
    g_page[LINK_PG_COUNT] = LINK_PG_PORTS_MAX;
    read_at(2u);
    CHECK_EQ(prog_link_count(&g_p), LINK_PG_PORTS_MAX);
    /* A count past the page's ports is the ports the page holds. */
    g_page[LINK_PG_COUNT] = LINK_PG_PORTS_MAX + 1u;
    read_at(3u);
    CHECK_EQ(prog_link_count(&g_p), LINK_PG_PORTS_MAX);
    g_page[LINK_PG_COUNT] = 0xFFFFu;
    read_at(4u);
    CHECK_EQ(prog_link_count(&g_p), LINK_PG_PORTS_MAX);
    prog_link_stale(&g_p);
    CHECK_EQ(prog_link_count(&g_p), 0u);
    read_at(5u);
    prog_link_lost(&g_p);
    CHECK_EQ(prog_link_count(&g_p), 0u);
    prog_link_init(&g_p);
    prog_link_came_up(&g_p, LINK_MINOR_PROG - 1u);
    page_fresh();
    read_at(6u);
    CHECK_EQ(prog_link_count(&g_p), 0u);
}

int main(void)
{
    RUN(nothing_is_written_while_the_programmer_has_a_port);
    RUN(nothing_is_asked_of_a_coprocessor_without_the_page);
    RUN(nothing_is_written_before_the_page_is_read);
    RUN(the_page_is_read_every_500_ms_and_the_clock_may_wrap);
    RUN(the_port_becomes_the_target_and_then_renders_its_pulse);
    RUN(the_write_that_switches_a_pulse_on_names_its_port);
    RUN(the_pulse_goes_off_at_any_time_and_before_the_target_changes);
    RUN(a_port_the_page_does_not_have_is_no_target);
    RUN(a_port_that_loses_its_pin_gives_the_target_back);
    RUN(a_release_waits_for_a_read_and_not_for_the_target);
    RUN(a_coprocessor_that_started_again_is_brought_back);
    RUN(a_write_that_is_not_acknowledged_makes_the_page_unknown);
    RUN(a_port_names_its_pin_unless_it_has_none_or_was_refused);
    RUN(a_count_above_the_ports_the_page_holds_names_no_pin);
    RUN(the_port_count_is_the_page_s_up_to_the_ports_it_holds);
    RUN(the_drive_keeps_its_target_while_the_programmer_holds_the_port);
    RUN(the_drive_names_the_target_and_the_port_pin);
    RUN(a_write_not_acknowledged_leaves_the_drive_unknown);
    RUN(a_channel_configuration_maps_onto_a_port);
    RUN(a_target_change_returns_the_servo_rate_to_0_from_4_19);
    RUN(a_read_after_an_unanswered_target_write_compares_against_nothing);
    RUN(a_link_up_starts_the_target_record_over);
    RUN(a_write_to_another_register_leaves_the_target_record);
    RUN(no_object_is_no_fault);
    return test_summary("prog_link");
}
