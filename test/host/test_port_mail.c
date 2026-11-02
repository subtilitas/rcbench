/*
 * The programming ports between the panel's control task and its UI task
 * (shared/ui/port_mail.c).
 *
 * Under test: nothing known before the first read; the page as the view
 * draws it; a torn pair of view words keeps the last view; an ask handed
 * on once and its outcome back under its number; a refused ask that stays
 * REFUSED; the link lost with an ask posted and not yet taken, and with
 * one handed on and waiting, both LOST and neither written after the link
 * comes back; an ask made on the view of a lost link LOST in the next
 * epoch; an ask in the new epoch handed on; ports past the count not
 * known; number 0 and NULL refused; a number reused after the wrap
 * never taking the outcome its last use left.
 * Both tasks run on one thread here, in the order the panel runs them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "link_pages.h"
#include "port_mail.h"
#include "port_view.h"
#include "prog_link.h"
#include "ui_text.h"

static port_mail_t g_m = PORT_MAIL_INIT;
static prog_link_t g_p;
static uint16_t g_page[LINK_PG_REGS];

static bool hook(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                 uint16_t value)
{
    return port_mail_post(&g_m, seq, port, kind, value);
}

/* A 4.26 IOmcu with 4 ports: port 0 on GP13 SERVO PWM and bound, port 1
 * on GP14 NONE and parked, ports 2 and 3 NONE without a pin. */
static void page_fresh(void)
{
    memset(g_page, 0, sizeof(g_page));
    g_page[LINK_PG_COUNT] = 4u;
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0) | LINK_PG_F_PARKED(1);
    g_page[LINK_PG_REG(0, LINK_PG_PIN)] = 13u;
    g_page[LINK_PG_REG(1, LINK_PG_PIN)] = 14u;
    g_page[LINK_PG_FUNC(1)] = (uint16_t)LINK_PG_FN_NONE;
    for (unsigned n = 2; n < LINK_PG_PORTS_MAX; ++n) {
        g_page[LINK_PG_REG(n, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
        g_page[LINK_PG_FUNC(n)] = (uint16_t)LINK_PG_FN_NONE;
    }
}

/* The link up and the page read, published and taken by the UI. */
static void up(void)
{
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    prog_link_read(&g_p, g_page, 0u);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
}

static void fresh(void)
{
    const port_mail_t init = PORT_MAIL_INIT;
    memcpy(&g_m, &init, sizeof(g_m));
    page_fresh();
    prog_link_init(&g_p);
    port_view_set_ask(hook);
    port_mail_to_view(&g_m);
}

/* The one write prog_link has due on a bench at rest, acked. */
static bool write_one(void)
{
    prog_link_write_t w;
    if (!prog_link_next(&g_p, true, &w)) {
        return false;
    }
    g_page[w.off] = w.value;
    prog_link_written(&g_p, &w, true);
    return true;
}

TEST_CASE(nothing_is_known_before_the_first_read)
{
    fresh();
    CHECK_EQ(port_view_get()->count, PORT_VIEW_UNKNOWN);
    CHECK(!port_view_has(0u));
    /* The link up and the page not read yet: still not known. */
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_get()->count, PORT_VIEW_UNKNOWN);
    CHECK(!port_view_ask(0u, PROG_LINK_ASK_PIN, 15u));
}

TEST_CASE(the_view_draws_the_page)
{
    fresh();
    up();
    const port_view_t *v = port_view_get();
    CHECK_EQ(v->count, 4u);
    CHECK(v->has_func);
    CHECK_EQ(v->pin[0], 13u);
    CHECK_EQ(v->func[0], (uint8_t)LINK_PG_FN_SERVO_PWM);
    CHECK_EQ(v->pin[1], 14u);
    CHECK_EQ(v->func[1], (uint8_t)LINK_PG_FN_NONE);
    CHECK_EQ(v->pin[3], PORT_VIEW_NO_PIN);
    CHECK_EQ(v->refused, 0u);
    /* A pin the IOmcu does not hold is refused. */
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0) | LINK_PG_F_REFUSED(1);
    prog_link_read(&g_p, g_page, 0u);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_get()->refused, 0x2u);
    /* A build with 2 ports: ports 3 and 4 have no pin and no function. */
    g_page[LINK_PG_COUNT] = 2u;
    prog_link_read(&g_p, g_page, 0u);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_get()->count, 2u);
    CHECK_EQ(port_view_get()->pin[2], PORT_VIEW_NO_PIN);
    CHECK_EQ(port_view_get()->func[3], PORT_VIEW_FN_UNKNOWN);
    CHECK(!port_view_has(2u));
}

/* 4.28: each port's function crosses whole, DSHOT150 to DSHOT600 beside
 * SERVO PWM and NONE, a refused DShot port refused; the DShot functions
 * are asked of a 4.28 IOmcu only. */
TEST_CASE(the_view_carries_five_functions_and_whether_dshot_is_taken)
{
    fresh();
    up();
    CHECK(!port_view_get()->has_dshot);
    CHECK(!port_view_ask(2u, PROG_LINK_ASK_FUNC, LINK_PG_FN_DSHOT300));
    g_page[LINK_PG_REG(2, LINK_PG_PIN)] = 15u;
    g_page[LINK_PG_REG(3, LINK_PG_PIN)] = 16u;
    g_page[LINK_PG_FUNC(1)] = (uint16_t)LINK_PG_FN_DSHOT150;
    g_page[LINK_PG_FUNC(2)] = (uint16_t)LINK_PG_FN_DSHOT300;
    g_page[LINK_PG_FUNC(3)] = (uint16_t)LINK_PG_FN_DSHOT600;
    g_page[LINK_PG_FLAGS] = LINK_PG_F_BOUND(0) | LINK_PG_F_DSHOT(1)
                            | LINK_PG_F_DSHOT(2) | LINK_PG_F_REFUSED(3);
    prog_link_came_up(&g_p, LINK_MINOR_PROG_DSHOT);
    prog_link_read(&g_p, g_page, 0u);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    const port_view_t *v = port_view_get();
    CHECK(v->has_func);
    CHECK(v->has_dshot);
    CHECK_EQ(v->func[0], (uint8_t)LINK_PG_FN_SERVO_PWM);
    CHECK_EQ(v->func[1], (uint8_t)LINK_PG_FN_DSHOT150);
    CHECK_EQ(v->func[2], (uint8_t)LINK_PG_FN_DSHOT300);
    CHECK_EQ(v->func[3], (uint8_t)LINK_PG_FN_DSHOT600);
    CHECK_EQ(v->pin[3], 16u);
    CHECK_EQ(v->refused, 0x8u);
    CHECK(port_view_dshot(1u));
    CHECK(!port_view_pwm(1u));
    CHECK(port_view_pwm(0u));
    CHECK(!port_view_dshot(0u));
    CHECK(port_view_pwm(LINK_PG_PORTS_MAX));     /* not known: stricter */
    CHECK(!port_view_dshot(LINK_PG_PORTS_MAX));
    CHECK_EQ(strcmp(port_view_fn_name(LINK_PG_FN_DSHOT600), "DSHOT600"), 0);
    CHECK_EQ(strcmp(port_view_fn_name(LINK_PG_FN_DSHOT150), "DSHOT150"), 0);
    CHECK_EQ(strcmp(port_view_fn_name(LINK_PG_FN_COUNT), "?"), 0);
    CHECK(port_view_ask(2u, PROG_LINK_ASK_FUNC, LINK_PG_FN_DSHOT600));
    CHECK(!port_view_ask(2u, PROG_LINK_ASK_FUNC, LINK_PG_FN_COUNT));
    /* Lost: nothing known, DShot with it. */
    prog_link_lost(&g_p);
    port_mail_lost(&g_m);
    port_mail_to_view(&g_m);
    CHECK(!port_view_get()->has_dshot);
    CHECK_EQ(port_view_get()->count, PORT_VIEW_UNKNOWN);
}

TEST_CASE(a_torn_pair_keeps_the_last_view)
{
    fresh();
    up();
    const uint32_t rev = port_view_revision();
    /* The control task stopped between its two stores: the count odd, a
     * new PIN word beside the old functions. */
    atomic_fetch_add(&g_m.gen, 1u);
    atomic_store(&g_m.pins, 0xFFFFFF0Fu);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_get()->pin[0], 13u);
    CHECK_EQ(port_view_revision(), rev);
    /* The second store done and the count even: the new pair. */
    atomic_store(&g_m.meta, atomic_load(&g_m.meta) | 0x1u);
    atomic_fetch_add(&g_m.gen, 1u);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_get()->pin[0], 15u);
    CHECK_EQ(port_view_get()->func[0], (uint8_t)LINK_PG_FN_NONE);
}

TEST_CASE(an_ask_is_handed_on_once_and_its_outcome_comes_back)
{
    fresh();
    up();
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_WAITING);
    port_mail_publish(&g_m, &g_p);
    uint8_t port = 9u;
    CHECK_EQ(prog_link_ask_state(&g_p, &port, NULL, NULL),
             PROG_LINK_ASK_WAITING);
    CHECK_EQ(port, 2u);
    CHECK(write_one());
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_DONE);
    /* Not handed on twice: prog_link has nothing more to write. */
    port_mail_publish(&g_m, &g_p);
    CHECK(!write_one());
    /* A FUNC the same way. */
    prog_link_read(&g_p, g_page, 0u);
    CHECK(port_view_ask(2u, PROG_LINK_ASK_FUNC, LINK_PG_FN_SERVO_PWM));
    port_mail_publish(&g_m, &g_p);
    CHECK(write_one());
    CHECK_EQ(g_page[LINK_PG_FUNC(2u)], (uint16_t)LINK_PG_FN_SERVO_PWM);
}

TEST_CASE(a_refused_ask_stays_refused)
{
    fresh();
    up();
    /* One ask done, then one prog_link refuses: FUNC 7 is no function. */
    CHECK(port_view_ask(1u, PROG_LINK_ASK_PIN, 15u));
    port_mail_publish(&g_m, &g_p);
    CHECK(write_one());
    port_mail_publish(&g_m, &g_p);
    CHECK(port_mail_post(&g_m, (uint8_t)(port_view_ask_seq() + 1u), 1u,
                         PROG_LINK_ASK_FUNC, 7u));
    port_mail_publish(&g_m, &g_p);
    unsigned st = atomic_load(&g_m.ask_state);
    CHECK_EQ(st & 0xFFu, (unsigned)PROG_LINK_ASK_REFUSED);
    /* prog_link still says DONE for the older ask; the word keeps
     * REFUSED. */
    port_mail_publish(&g_m, &g_p);
    st = atomic_load(&g_m.ask_state);
    CHECK_EQ(st & 0xFFu, (unsigned)PROG_LINK_ASK_REFUSED);
}

TEST_CASE(an_ask_posted_and_not_taken_is_lost_with_the_link)
{
    fresh();
    up();
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    /* The link goes before the control task takes the ask. */
    prog_link_lost(&g_p);
    port_mail_lost(&g_m);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_LOST);
    CHECK_EQ(port_view_get()->count, PORT_VIEW_UNKNOWN);
    /* Another IOmcu answers: the ask is not written to it. */
    up();
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_IDLE);
    CHECK(!write_one());
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_LOST);
    CHECK_EQ(g_page[LINK_PG_REG(2u, LINK_PG_PIN)], LINK_PG_PIN_NONE);
}

TEST_CASE(an_ask_handed_on_and_waiting_is_lost_with_the_link)
{
    fresh();
    up();
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    port_mail_publish(&g_m, &g_p);
    prog_link_lost(&g_p);
    port_mail_lost(&g_m);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_LOST);
    up();
    CHECK(!write_one());
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_LOST);
}

TEST_CASE(an_ask_made_on_a_lost_links_view_is_lost_in_the_next_epoch)
{
    fresh();
    up();
    /* The loss and the new link's read happen before the UI takes a
     * view: the screen still draws the old one and asks on it. */
    prog_link_lost(&g_p);
    port_mail_lost(&g_m);
    prog_link_came_up(&g_p, LINK_MINOR_PROG_FUNC);
    prog_link_read(&g_p, g_page, 0u);
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    port_mail_publish(&g_m, &g_p);
    CHECK_EQ(prog_link_ask_state(&g_p, NULL, NULL, NULL), PROG_LINK_ASK_IDLE);
    CHECK(!write_one());
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_LOST);
    /* On the new view the same ask goes through. */
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    port_mail_publish(&g_m, &g_p);
    CHECK(write_one());
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_DONE);
    CHECK_EQ(g_page[LINK_PG_REG(2u, LINK_PG_PIN)], 15u);
}

TEST_CASE(the_epoch_wraps_past_eight_losses)
{
    fresh();
    for (int k = 0; k < 9; ++k) {
        up();
        prog_link_lost(&g_p);
        port_mail_lost(&g_m);
    }
    up();
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    port_mail_publish(&g_m, &g_p);
    CHECK(write_one());
}

TEST_CASE(number_0_and_null_are_refused)
{
    fresh();
    CHECK(!port_mail_post(&g_m, 0u, 0u, PROG_LINK_ASK_PIN, 15u));
    CHECK(!port_mail_post(NULL, 1u, 0u, PROG_LINK_ASK_PIN, 15u));
    port_mail_publish(NULL, &g_p);
    port_mail_publish(&g_m, NULL);
    port_mail_lost(NULL);
    port_mail_to_view(NULL);
    /* A loss with no ask ever posted says nothing about one. */
    port_mail_lost(&g_m);
    CHECK_EQ(atomic_load(&g_m.ask_state), 0u);
}

TEST_CASE(a_reused_number_does_not_take_its_old_outcome)
{
    fresh();
    up();
    /* The word still holds the outcome of the ask the next number named
     * 255 asks ago: REFUSED. */
    const uint8_t next = (uint8_t)(port_view_ask_seq() == 0xFFu
                                       ? 1u : port_view_ask_seq() + 1u);
    atomic_store(&g_m.ask_state,
                 ((unsigned)next << 8) | (unsigned)PROG_LINK_ASK_REFUSED);
    CHECK(port_view_ask(2u, PROG_LINK_ASK_PIN, 15u));
    CHECK_EQ(port_view_ask_seq(), next);
    /* Before the control task takes it, and after: WAITING. */
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_WAITING);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_WAITING);
    /* What holds it reaches the view. */
    prog_link_hold_by(&g_p, PROG_LINK_WAIT_ESC);
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    {
        char line[64];

        CHECK(port_view_ask_line(2u, line, sizeof(line)));
        CHECK_STR_EQ(line, ui_tr(TX_OUT_PORT_WAIT_ESC));
    }
    prog_link_hold_by(&g_p, PROG_LINK_WAIT_NONE);
    /* And its own outcome. */
    CHECK(write_one());
    port_mail_publish(&g_m, &g_p);
    port_mail_to_view(&g_m);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_DONE);
}

int main(void)
{
    RUN(nothing_is_known_before_the_first_read);
    RUN(the_view_draws_the_page);
    RUN(the_view_carries_five_functions_and_whether_dshot_is_taken);
    RUN(a_torn_pair_keeps_the_last_view);
    RUN(an_ask_is_handed_on_once_and_its_outcome_comes_back);
    RUN(a_refused_ask_stays_refused);
    RUN(an_ask_posted_and_not_taken_is_lost_with_the_link);
    RUN(an_ask_handed_on_and_waiting_is_lost_with_the_link);
    RUN(an_ask_made_on_a_lost_links_view_is_lost_in_the_next_epoch);
    RUN(the_epoch_wraps_past_eight_losses);
    RUN(number_0_and_null_are_refused);
    RUN(a_reused_number_does_not_take_its_old_outcome);
    return test_summary("port_mail");
}
