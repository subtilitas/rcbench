/*
 * Binding writes beside a programming session on the remote servo node's
 * common rail (shared/outputs/rail_bind.c, pass_page.c), wired as
 * firmware/iomcu/src/iomcu_pages.c and iomcu_pass.c wire them: the lock is
 * rail_bind_lock() of the POWER page, whether a session is open and the
 * session's output, taken at each write; an OUTPUTS write is judged by
 * rail_bind_slots() of the pages before and after it, a PROG write by
 * rail_bind_prog() of the ports before and after it (prog_port_revert()
 * on a refusal), a PASS write by pass_ctx_t.locked.
 *
 * Under test, for each activation kind (a slot bound on an output, a
 * programming port given a function on an output, a port with a function
 * moved to another output, a SOURCE set to PASS-THROUGH) and both FITTED
 * values: before CONNECT taken, while the session is open refused
 * (LINK_NACK_BUSY) without the switches and taken with them, after the
 * session ends taken; the session's own output never locked; with the
 * session's output not known every output locked; a write that leaves the
 * channels as they are or switches one off taken during the session; NULL
 * arguments.  And a port on whose output a taken write makes a throttle
 * (a DShot slot, a PWM slot whose channel is a throttle, a DShot
 * programming port) switched off in that write, with and without the
 * switches; a surface bind leaving the port on; a PASS-THROUGH throttle
 * channel refused during a session at FITTED 0, and once taken counted as
 * a throttle port whose rail goes off in the write.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"
#include "pass_page.h"
#include "power_page.h"
#include "prog_port.h"
#include "rail_bind.h"
#include "rsn_pins.h"

#define K     4u         /* the session's output, S5 */
#define OTHER 2u         /* another output, S3 */

static power_page_t g_pg;
static power_ctx_t  g_ctx;
static bool         g_open;  /* a session is open */

static const outbind_roles_t *roles(void)
{
    return outbind_roles((uint16_t)OUTBIND_BOARD_RSN);
}

static void start(bool switches)
{
    power_init(&g_pg, switches, true);
    g_ctx.beat_alive  = true;
    g_ctx.limits_held = true;
    g_ctx.throttle    = 0u;
    g_open = false;
}

/* The lock as rsn_lock() takes it at a write. */
static uint16_t lock(void)
{
    return rail_bind_lock(&g_pg, g_open, (int)K);
}

/* ------------------------------------------------------------ the lock */

TEST_CASE(the_lock_is_every_other_output_without_the_switches)
{
    start(false);
    CHECK_EQ(rail_bind_lock(&g_pg, false, (int)K), 0u);
    CHECK_EQ(rail_bind_lock(&g_pg, true, (int)K), 0x01EFu);
    CHECK_EQ(rail_bind_lock(&g_pg, true, 0), 0x01FEu);
    CHECK_EQ(rail_bind_lock(&g_pg, true, 8), 0x00FFu);
    /* The session's output not known: all 9. */
    CHECK_EQ(rail_bind_lock(&g_pg, true, -1), 0x01FFu);
    CHECK_EQ(rail_bind_lock(&g_pg, true, 9), 0x01FFu);
    CHECK_EQ(rail_bind_lock(NULL, true, (int)K), 0u);
    start(true);
    CHECK_EQ(rail_bind_lock(&g_pg, false, (int)K), 0u);
    CHECK_EQ(rail_bind_lock(&g_pg, true, (int)K), 0u);
    CHECK_EQ(rail_bind_lock(&g_pg, true, -1), 0u);
}

TEST_CASE(only_a_new_locked_output_refuses)
{
    CHECK(!rail_bind_refuses(0x01EFu, 0u, 0u));
    CHECK(rail_bind_refuses(0x01EFu, 0u, 0x0004u));
    /* Already active, written again: taken. */
    CHECK(!rail_bind_refuses(0x01EFu, 0x0004u, 0x0004u));
    /* Switched off: taken. */
    CHECK(!rail_bind_refuses(0x01EFu, 0x0004u, 0u));
    /* The session's own output: not locked. */
    CHECK(!rail_bind_refuses(0x01EFu, 0u, 0x0010u));
    CHECK(!rail_bind_refuses(0u, 0u, 0x01FFu));
}

/* ------------------------------------------------------------ the slots */

static uint16_t g_slots[LINK_OS_ALL];

/* An OUTPUTS write as slots_take() judges it: slot @p s set to @p drv on
 * output @p k's pin.  Taken: 0 and the pages changed; refused: BUSY. */
static uint8_t slot_put(unsigned s, uint16_t drv, unsigned k)
{
    uint16_t next[LINK_OS_ALL];
    memcpy(next, g_slots, sizeof(next));
    next[s * LINK_OS_STRIDE + LINK_OS_DRIVER] = drv;
    next[s * LINK_OS_STRIDE + LINK_OS_PIN]    = (uint16_t)RSN_PIN_OUT(k);
    if (rail_bind_refuses(lock(), rail_bind_slots(g_slots, roles()),
                          rail_bind_slots(next, roles()))) {
        return (uint8_t)LINK_NACK_BUSY;
    }
    memcpy(g_slots, next, sizeof(next));
    return 0u;
}

TEST_CASE(the_slots_read_the_outputs_they_drive)
{
    memset(g_slots, 0, sizeof(g_slots));
    CHECK_EQ(rail_bind_slots(g_slots, roles()), 0u);
    g_slots[LINK_OS_DRIVER] = (uint16_t)LINK_DRIVER_PWM;
    g_slots[LINK_OS_PIN]    = (uint16_t)RSN_PIN_OUT(0);
    /* Slot 9 on OUTPUTS_HI, the last one, on S9. */
    const unsigned last = (LINK_OUT_SLOTS_ALL - 1u) * LINK_OS_STRIDE;
    g_slots[last + LINK_OS_DRIVER] = (uint16_t)LINK_DRIVER_DSHOT;
    g_slots[last + LINK_OS_PIN]    = (uint16_t)RSN_PIN_OUT(8);
    CHECK_EQ(rail_bind_slots(g_slots, roles()), 0x0101u);
    /* NONE on a pin: not active.  A pin off the outputs: no bit. */
    g_slots[LINK_OS_DRIVER] = (uint16_t)LINK_DRIVER_NONE;
    CHECK_EQ(rail_bind_slots(g_slots, roles()), 0x0100u);
    g_slots[last + LINK_OS_PIN] = 2u;
    CHECK_EQ(rail_bind_slots(g_slots, roles()), 0u);
    CHECK_EQ(rail_bind_slots(NULL, roles()), 0u);
    CHECK_EQ(rail_bind_slots(g_slots, NULL), 0u);
}

static void slot_phases(bool switches)
{
    start(switches);
    memset(g_slots, 0, sizeof(g_slots));
    /* Before CONNECT: taken, then off again. */
    CHECK_EQ(slot_put(0u, LINK_DRIVER_PWM, OTHER), 0u);
    CHECK_EQ(slot_put(0u, LINK_DRIVER_NONE, OTHER), 0u);
    /* The session open. */
    g_open = true;
    const uint8_t want = switches ? 0u : (uint8_t)LINK_NACK_BUSY;
    CHECK_EQ(slot_put(0u, LINK_DRIVER_PWM, OTHER), want);
    CHECK_EQ(slot_put(1u, LINK_DRIVER_DSHOT, 8u), want);
    CHECK_EQ(slot_put(1u, LINK_DRIVER_PPM, 0u), want);
    CHECK_EQ(rail_bind_slots(g_slots, roles()),
             switches ? 0x0005u : 0u);
    /* The session ends. */
    g_open = false;
    CHECK_EQ(slot_put(2u, LINK_DRIVER_PWM, 6u), 0u);
    CHECK_EQ(rail_bind_slots(g_slots, roles()),
             switches ? 0x0045u : 0x0040u);
}

TEST_CASE(a_slot_bind_is_refused_while_a_session_is_open_without_switches)
{
    slot_phases(false);
}

TEST_CASE(a_slot_bind_is_taken_during_a_session_with_switches)
{
    slot_phases(true);
}

/* During the session a slot written again, or unbound, is taken. */
TEST_CASE(a_slot_kept_or_unbound_during_a_session_is_taken)
{
    start(false);
    memset(g_slots, 0, sizeof(g_slots));
    CHECK_EQ(slot_put(0u, LINK_DRIVER_PWM, OTHER), 0u);
    g_open = true;
    /* Written again with another driver on the same output. */
    CHECK_EQ(slot_put(0u, LINK_DRIVER_PWM, OTHER), 0u);
    CHECK_EQ(slot_put(0u, LINK_DRIVER_DSHOT, OTHER), 0u);
    /* Moved to another output: that one comes on, refused. */
    CHECK_EQ(slot_put(0u, LINK_DRIVER_DSHOT, 3u), (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(slot_put(0u, LINK_DRIVER_NONE, OTHER), 0u);
    CHECK_EQ(rail_bind_slots(g_slots, roles()), 0u);
}

/* ---------------------------------------------- the programming ports */

static prog_port_t g_pp;
static outputs_t   g_out;

/* A PROG write of one register as prog_write() judges it. */
static uint8_t pg_put(unsigned off, uint16_t v)
{
    const prog_port_t was = g_pp;
    const uint8_t nack = prog_port_write(&g_pp, (uint8_t)off, 1u, &v, &g_out,
                                         0u, false, 0u);
    if (nack != 0u) {
        return nack;
    }
    if (rail_bind_refuses(lock(), rail_bind_prog(&was, roles()),
                          rail_bind_prog(&g_pp, roles()))) {
        prog_port_revert(&g_pp, &was, &g_out);
        return (uint8_t)LINK_NACK_BUSY;
    }
    return 0u;
}

static void prog_start(bool switches)
{
    start(switches);
    outputs_init(&g_out, 0u);
    prog_port_init(&g_pp, LINK_PG_PORTS_MAX);
    /* Port 1 parked on S3 without a function. */
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_NONE), 0u);
    CHECK_EQ(pg_put(LINK_PG_REG(1u, LINK_PG_PIN),
                    (uint16_t)RSN_PIN_OUT(OTHER)), 0u);
}

TEST_CASE(the_ports_read_the_outputs_their_function_holds)
{
    prog_start(false);
    CHECK_EQ(rail_bind_prog(&g_pp, roles()), 0u);
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_DSHOT300), 0u);
    CHECK_EQ(rail_bind_prog(&g_pp, roles()), 0x0004u);
    CHECK_EQ(rail_bind_prog(NULL, roles()), 0u);
    CHECK_EQ(rail_bind_prog(&g_pp, NULL), 0u);
}

static void prog_phases(bool switches)
{
    prog_start(switches);
    /* Before CONNECT: a function on S3 taken, and back to NONE. */
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_SERVO_PWM), 0u);
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_NONE), 0u);
    g_open = true;
    const uint8_t want = switches ? 0u : (uint8_t)LINK_NACK_BUSY;
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_SERVO_PWM), want);
    CHECK_EQ(prog_port_func(&g_pp, 1u),
             switches ? (uint8_t)LINK_PG_FN_SERVO_PWM
                      : (uint8_t)LINK_PG_FN_NONE);
    CHECK_EQ(rail_bind_prog(&g_pp, roles()), switches ? 0x0004u : 0u);
    g_open = false;
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_DSHOT600), 0u);
    CHECK_EQ(rail_bind_prog(&g_pp, roles()), 0x0004u);
}

TEST_CASE(a_port_function_is_refused_while_a_session_is_open_without_switches)
{
    prog_phases(false);
}

TEST_CASE(a_port_function_is_taken_during_a_session_with_switches)
{
    prog_phases(true);
}

/* A port with a function moved to another output during the session:
 * refused without the switches, the pin as it was; a function taken off
 * is taken. */
TEST_CASE(a_port_moved_to_another_output_during_a_session_is_refused)
{
    prog_start(false);
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_SERVO_PWM), 0u);
    g_open = true;
    CHECK_EQ(pg_put(LINK_PG_REG(1u, LINK_PG_PIN), (uint16_t)RSN_PIN_OUT(6)),
             (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(prog_port_pin(&g_pp, 1u), (uint8_t)RSN_PIN_OUT(OTHER));
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_NONE), 0u);
    CHECK_EQ(rail_bind_prog(&g_pp, roles()), 0u);
    /* With the switches the move is taken. */
    prog_start(true);
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_SERVO_PWM), 0u);
    g_open = true;
    CHECK_EQ(pg_put(LINK_PG_REG(1u, LINK_PG_PIN), (uint16_t)RSN_PIN_OUT(6)),
             0u);
    CHECK_EQ(rail_bind_prog(&g_pp, roles()), 0x0040u);
}

/* -------------------------------------------------------- PASS-THROUGH */

static pass_page_t g_pa;

/* A SOURCE write as thru_write() judges it. */
static uint8_t src(unsigned k, uint16_t v)
{
    const pass_ctx_t c = { .armed = false, .busy = 0u, .locked = lock() };
    return pass_page_write(&g_pa, (uint8_t)LINK_PA_SOURCE(k), 1u, &v, &c);
}

static void pass_phases(bool switches)
{
    start(switches);
    pass_page_init(&g_pa, true, 0u);
    /* Before CONNECT: taken, and back to BENCH. */
    CHECK_EQ(src(OTHER, LINK_PA_SRC_PASS), 0u);
    CHECK_EQ(pass_page_active(&g_pa), 0x0004u);
    CHECK_EQ(src(OTHER, LINK_PA_SRC_BENCH), 0u);
    g_open = true;
    const uint8_t want = switches ? 0u : (uint8_t)LINK_NACK_BUSY;
    CHECK_EQ(src(OTHER, LINK_PA_SRC_PASS), want);
    CHECK_EQ(src(8u, LINK_PA_SRC_PASS), want);
    CHECK_EQ(pass_page_active(&g_pa), switches ? 0x0104u : 0u);
    /* OFF holds the pin low and is no activation: taken. */
    CHECK_EQ(src(0u, LINK_PA_SRC_OFF), 0u);
    g_open = false;
    CHECK_EQ(src(0u, LINK_PA_SRC_PASS), 0u);
    CHECK_EQ(pass_page_active(&g_pa), switches ? 0x0105u : 0x0001u);
}

TEST_CASE(pass_through_is_refused_while_a_session_is_open_without_switches)
{
    pass_phases(false);
}

TEST_CASE(pass_through_is_taken_during_a_session_with_switches)
{
    pass_phases(true);
}

/* A write of several SOURCEs with one locked PASS-THROUGH is refused
 * whole; one that keeps a PASS-THROUGH in force, or ends it, is taken. */
TEST_CASE(a_pass_write_is_judged_whole_during_a_session)
{
    start(false);
    pass_page_init(&g_pa, true, 0u);
    CHECK_EQ(src(1u, LINK_PA_SRC_PASS), 0u);
    g_open = true;
    uint16_t v[3] = { LINK_PA_SRC_PASS, LINK_PA_SRC_OFF, LINK_PA_SRC_PASS };
    const pass_ctx_t c = { .armed = false, .busy = 0u, .locked = lock() };
    CHECK_EQ(pass_page_write(&g_pa, (uint8_t)LINK_PA_SOURCE(1u), 3u, v, &c),
             (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(pass_page_active(&g_pa), 0x0002u);
    v[2] = LINK_PA_SRC_BENCH;
    CHECK_EQ(pass_page_write(&g_pa, (uint8_t)LINK_PA_SOURCE(1u), 3u, v, &c),
             0u);
    CHECK_EQ(src(1u, LINK_PA_SRC_BENCH), 0u);
    CHECK_EQ(pass_page_active(&g_pa), 0u);
}

/* --------------------------------------------- a throttle at the bind */

/* Output @p k on beside the ones on: PORT_WANT bit k added with the
 * switches, RAIL 1. */
static void port_on(unsigned k)
{
    uint16_t v = 0u;
    power_read(&g_pg, (uint8_t)LINK_PW_PORT_WANT, 1u, &v);
    v |= (uint16_t)(1u << k);
    CHECK_EQ(power_write(&g_pg, (uint8_t)LINK_PW_PORT_WANT, 1u, &v, &g_ctx),
             0u);
    v = 1u;
    CHECK_EQ(power_write(&g_pg, (uint8_t)LINK_PW_RAIL, 1u, &v, &g_ctx), 0u);
    CHECK((power_port_on(&g_pg) & (1u << k)) != 0u);
}

TEST_CASE(a_dshot_bind_switches_its_powered_port_off_in_the_write)
{
    for (unsigned sw = 0u; sw < 2u; ++sw) {
        start(sw != 0u);
        outputs_t o;
        outputs_init(&o, 0u);
        port_on(OTHER);
        /* A surface on S3: the port stays on. */
        const out_slot_t pwm = { .driver = OUT_DRIVER_PWM,
                                 .first_channel = 0u, .channels = 1u,
                                 .pin = RSN_PIN_OUT(OTHER), .rate_hz = 50u };
        CHECK(outputs_configure(&o, 0u, &pwm));
        CHECK_EQ(rail_bind_hold(&g_pg, &o, NULL, roles()), 0u);
        CHECK((power_port_on(&g_pg) & 0x0004u) != 0u);
        /* DShot on S3: off in this call, no pass between. */
        const out_slot_t d = { .driver = OUT_DRIVER_DSHOT,
                               .first_channel = 0u, .channels = 1u,
                               .pin = RSN_PIN_OUT(OTHER), .rate_hz = 600u };
        CHECK(outputs_configure(&o, 0u, &d));
        CHECK_EQ(rail_bind_hold(&g_pg, &o, NULL, roles()), 0x0004u);
        CHECK_EQ(power_port_on(&g_pg), 0u);
        CHECK_EQ(power_rail(&g_pg), sw != 0u);
        CHECK_EQ(power_faults(&g_pg), 0u);
    }
}

TEST_CASE(a_throttle_role_switches_its_powered_port_off_in_the_write)
{
    start(true);
    outputs_t o;
    outputs_init(&o, 0u);
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 3u,
                             .channels = 1u, .pin = RSN_PIN_OUT(OTHER),
                             .rate_hz = 50u };
    CHECK(outputs_configure(&o, 0u, &pwm));
    port_on(OTHER);
    port_on(6u);
    /* MOTOR PWM: the channel becomes a throttle (a CHAN_CFG write). */
    CHECK(outputs_set_role(&o, 3u, OUT_ROLE_THROTTLE));
    CHECK_EQ(rail_bind_hold(&g_pg, &o, NULL, roles()), 0x0004u);
    /* Only that port: S7 stays on. */
    CHECK_EQ(power_port_on(&g_pg), 0x0040u);
}

/* A receiver's throttle on PASS-THROUGH without the switches: refused by
 * the lock while a session is open; taken before it, it is a throttle
 * port, and the rail goes off in the PASS write (passing_follow()). */
TEST_CASE(a_pass_through_throttle_is_locked_and_counted_as_a_throttle)
{
    start(false);
    pass_page_init(&g_pa, true, 0u);
    outputs_t o;
    outputs_init(&o, 0u);
    CHECK(outputs_set_role(&o, (uint8_t)OTHER, OUT_ROLE_THROTTLE));
    port_on(K);
    g_open = true;
    CHECK_EQ(src(OTHER, LINK_PA_SRC_PASS), (uint8_t)LINK_NACK_BUSY);
    outputs_set_passing(&o, pass_page_active(&g_pa));
    CHECK_EQ(power_throttle(&o, NULL, roles()), 0u);
    CHECK_EQ(rail_bind_hold(&g_pg, &o, NULL, roles()), 0u);
    CHECK(power_rail(&g_pg));
    /* With the session ended the write is taken: S3 a throttle port, the
     * rail off in that write. */
    g_open = false;
    CHECK_EQ(src(OTHER, LINK_PA_SRC_PASS), 0u);
    outputs_set_passing(&o, pass_page_active(&g_pa));
    CHECK_EQ(power_throttle(&o, NULL, roles()), 0x0004u);
    CHECK_EQ(rail_bind_hold(&g_pg, &o, NULL, roles()), 0x0004u);
    CHECK(!power_rail(&g_pg));
    CHECK_EQ(power_port_on(&g_pg), 0u);
}

TEST_CASE(a_dshot_port_function_switches_its_powered_port_off_in_the_write)
{
    prog_start(false);
    port_on(OTHER);
    CHECK_EQ(rail_bind_hold(&g_pg, &g_out, &g_pp, roles()), 0u);
    CHECK_EQ(pg_put(LINK_PG_FUNC(1u), (uint16_t)LINK_PG_FN_DSHOT150), 0u);
    CHECK_EQ(rail_bind_hold(&g_pg, &g_out, &g_pp, roles()), 0x0004u);
    CHECK(!power_rail(&g_pg));
    CHECK_EQ(power_port_on(&g_pg), 0u);
    CHECK_EQ(rail_bind_hold(NULL, &g_out, &g_pp, roles()), 0u);
    CHECK_EQ(rail_bind_hold(&g_pg, NULL, &g_pp, roles()), 0u);
    CHECK_EQ(rail_bind_hold(&g_pg, &g_out, &g_pp, NULL), 0u);
}

int main(void)
{
    RUN(the_lock_is_every_other_output_without_the_switches);
    RUN(only_a_new_locked_output_refuses);
    RUN(the_slots_read_the_outputs_they_drive);
    RUN(a_slot_bind_is_refused_while_a_session_is_open_without_switches);
    RUN(a_slot_bind_is_taken_during_a_session_with_switches);
    RUN(a_slot_kept_or_unbound_during_a_session_is_taken);
    RUN(the_ports_read_the_outputs_their_function_holds);
    RUN(a_port_function_is_refused_while_a_session_is_open_without_switches);
    RUN(a_port_function_is_taken_during_a_session_with_switches);
    RUN(a_port_moved_to_another_output_during_a_session_is_refused);
    RUN(pass_through_is_refused_while_a_session_is_open_without_switches);
    RUN(pass_through_is_taken_during_a_session_with_switches);
    RUN(a_pass_write_is_judged_whole_during_a_session);
    RUN(a_dshot_bind_switches_its_powered_port_off_in_the_write);
    RUN(a_throttle_role_switches_its_powered_port_off_in_the_write);
    RUN(a_dshot_port_function_switches_its_powered_port_off_in_the_write);
    RUN(a_pass_through_throttle_is_locked_and_counted_as_a_throttle);
    return test_summary("rail_bind");
}
