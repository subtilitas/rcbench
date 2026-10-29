/*
 * The panel's end of the PROG_SUPPLY page (shared/bench/prog_supply_link.c,
 * link 4.32).
 *
 * Under test: nothing is written or read for an IOmcu without the page; an
 * error owes one OFF, OUTPUT 0, which a write the IOmcu answers ends, ACK
 * or NACK, and one it does not answer keeps; the page is read at once and
 * then every PROG_SUPPLY_LINK_READ_MS to the millisecond, across the
 * clock's wrap; the state SUPPLY shows: none without the switch, ON and OFF
 * as read, NOT READ after an unanswered read of a fitted switch; the
 * ports' own supply as the programmers' supply, on the board's word before
 * the first read; and as live for the install and flash gates while on, not
 * read, or held by a programmer's port; PANEL 1 written after the OFF
 * whenever a read finds it 0 on a board with the switch, once per read.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "prog_supply_link.h"

static prog_supply_link_t l;

/* A read of a page with these flags and PANEL @p panel, on GPIO 21. */
static void read_page(uint16_t flags, uint16_t panel, uint32_t now)
{
    const uint16_t regs[LINK_PS_COUNT] = { 0u, flags, 21u, 5000u, panel };
    prog_supply_link_read(&l, regs, now);
}

/* A read of a page with these flags, PANEL 1, on GPIO 21. */
static void read_flags(uint16_t flags, uint32_t now)
{
    read_page(flags, 1u, now);
}

TEST_CASE(an_iomcu_without_the_page_gets_nothing)
{
    prog_supply_link_init(&l, false);
    prog_supply_link_cut(&l);
    uint8_t off = 9u;
    uint16_t v = 9u;
    CHECK(!prog_supply_link_next(&l, &off, &v));
    CHECK(!prog_supply_link_read_due(&l, 0u));
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_NONE);
    CHECK_EQ(l.pin, LINK_PS_NO_PIN);

    /* NULL is nothing. */
    prog_supply_link_init(NULL, true);
    prog_supply_link_cut(NULL);
    CHECK(!prog_supply_link_next(NULL, &off, &v));
    prog_supply_link_written(NULL, PROG_SUPPLY_LINK_ACK);
    CHECK(!prog_supply_link_read_due(NULL, 0u));
    prog_supply_link_read(NULL, NULL, 0u);
    CHECK_EQ(prog_supply_link_state(NULL), PROG_SUPPLY_NONE);
}

TEST_CASE(an_error_owes_one_off_until_the_iomcu_answers)
{
    prog_supply_link_init(&l, true);
    uint8_t off = 9u;
    uint16_t v = 9u;
    CHECK(!prog_supply_link_next(&l, &off, &v));
    prog_supply_link_cut(&l);
    CHECK(prog_supply_link_next(&l, &off, &v));
    CHECK_EQ(off, LINK_PS_OUTPUT);
    CHECK_EQ(v, 0u);
    CHECK(prog_supply_link_next(&l, NULL, NULL));
    /* No answer: owed still. */
    prog_supply_link_written(&l, PROG_SUPPLY_LINK_NO_ANSWER);
    CHECK(prog_supply_link_next(&l, &off, &v));
    /* An ACK ends it. */
    prog_supply_link_written(&l, PROG_SUPPLY_LINK_ACK);
    CHECK(!prog_supply_link_next(&l, &off, &v));
    /* So does a NACK: the IOmcu answered. */
    prog_supply_link_cut(&l);
    prog_supply_link_cut(&l);               /* two errors, one OFF */
    prog_supply_link_written(&l, (int)LINK_NACK_BAD_VALUE);
    CHECK(!prog_supply_link_next(&l, &off, &v));
}

TEST_CASE(the_page_is_read_at_once_then_every_200ms)
{
    prog_supply_link_init(&l, true);
    CHECK(prog_supply_link_read_due(&l, 1234u));
    read_flags(LINK_PS_FITTED, 1000u);
    CHECK(!prog_supply_link_read_due(&l, 1000u));
    CHECK(!prog_supply_link_read_due(&l, 1000u + PROG_SUPPLY_LINK_READ_MS
                                             - 1u));
    CHECK(prog_supply_link_read_due(&l, 1000u + PROG_SUPPLY_LINK_READ_MS));
    /* Across the wrap of the clock. */
    read_flags(LINK_PS_FITTED, 0xFFFFFFFFu - 50u);
    CHECK(!prog_supply_link_read_due(&l, 100u));
    CHECK(prog_supply_link_read_due(&l, PROG_SUPPLY_LINK_READ_MS - 51u));
    /* An unanswered read counts as a read for the cadence. */
    prog_supply_link_read(&l, NULL, 5000u);
    CHECK(!prog_supply_link_read_due(&l, 5001u));
}

TEST_CASE(supply_shows_the_state_as_read)
{
    prog_supply_link_init(&l, true);
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_NONE);
    read_flags(0u, 0u);                     /* no switch on this board */
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_NONE);
    read_flags(LINK_PS_ON, 0u);             /* ON without FITTED: none */
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_NONE);
    read_flags(LINK_PS_FITTED, 0u);
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_OFF);
    CHECK_EQ(l.pin, 21u);
    read_flags(LINK_PS_FITTED | LINK_PS_ON | LINK_PS_PORT, 0u);
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_ON);
    /* A read unanswered: NOT READ, the pin kept. */
    prog_supply_link_read(&l, NULL, 0u);
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_UNKNOWN);
    CHECK_EQ(l.pin, 21u);
    read_flags(LINK_PS_FITTED, 0u);
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_OFF);
    /* The link lost: nothing again. */
    prog_supply_link_init(&l, false);
    CHECK_EQ(prog_supply_link_state(&l), PROG_SUPPLY_NONE);
}

TEST_CASE(the_board_or_a_read_makes_the_switch_the_ports_supply)
{
    prog_supply_link_init(&l, false);
    CHECK(!prog_supply_link_owns(&l, true));     /* no page: the PD mini */
    CHECK(!prog_supply_link_owns(NULL, true));
    prog_supply_link_init(&l, true);
    CHECK(!prog_supply_link_owns(&l, false));    /* a board without it */
    CHECK(prog_supply_link_owns(&l, true));      /* the carrier, unread */
    read_flags(LINK_PS_FITTED, 0u);
    CHECK(prog_supply_link_owns(&l, false));     /* read fitted */
    read_flags(0u, 0u);
    CHECK(!prog_supply_link_owns(&l, false));    /* read without */
}

TEST_CASE(the_gates_count_it_live_until_read_off_and_free)
{
    prog_supply_link_init(&l, true);
    /* Not read yet on the carrier: live. */
    CHECK(prog_supply_link_live(&l, true, false));
    /* Never on a board without the switch, whatever a port holds. */
    CHECK(!prog_supply_link_live(&l, false, true));
    CHECK(!prog_supply_link_live(NULL, true, true));
    read_flags(LINK_PS_FITTED, 0u);
    CHECK(!prog_supply_link_live(&l, true, false));   /* read off, free */
    CHECK(prog_supply_link_live(&l, true, true));     /* a port holds it */
    read_flags(LINK_PS_FITTED | LINK_PS_ON, 0u);
    CHECK(prog_supply_link_live(&l, true, false));    /* read on */
    read_flags(LINK_PS_FITTED, 0u);
    prog_supply_link_read(&l, NULL, 0u);
    CHECK(prog_supply_link_live(&l, true, false));    /* read unanswered */
    read_flags(LINK_PS_FITTED, 0u);
    CHECK(!prog_supply_link_live(&l, true, false));
    /* The link lost: no page, nothing live here; the IOmcu cuts it. */
    prog_supply_link_init(&l, false);
    CHECK(!prog_supply_link_live(&l, true, true));
}

TEST_CASE(the_panel_gives_its_word_when_a_read_finds_none)
{
    uint8_t off = 9u;
    uint16_t v = 9u;
    prog_supply_link_init(&l, true);
    CHECK(!prog_supply_link_next(&l, &off, &v));    /* nothing read yet */
    /* A board without the switch: no word. */
    read_page(0u, 0u, 0u);
    CHECK(!prog_supply_link_next(&l, &off, &v));
    /* The switch, PANEL 0: PANEL 1 is written. */
    read_page(LINK_PS_FITTED, 0u, 0u);
    CHECK(prog_supply_link_next(&l, &off, &v));
    CHECK_EQ(off, LINK_PS_PANEL);
    CHECK_EQ(v, 1u);
    /* Unanswered: again; answered, ACK or NACK: not before the next read. */
    prog_supply_link_written(&l, PROG_SUPPLY_LINK_NO_ANSWER);
    CHECK(prog_supply_link_next(&l, &off, &v));
    prog_supply_link_written(&l, (int)LINK_NACK_NOT_ARMED);
    CHECK(!prog_supply_link_next(&l, &off, &v));
    read_page(LINK_PS_FITTED, 0u, 200u);
    CHECK(prog_supply_link_next(&l, &off, &v));
    prog_supply_link_written(&l, PROG_SUPPLY_LINK_ACK);
    read_page(LINK_PS_FITTED, 1u, 400u);
    CHECK(!prog_supply_link_next(&l, &off, &v));
    /* An unanswered read keeps what was read. */
    prog_supply_link_read(&l, NULL, 600u);
    CHECK(!prog_supply_link_next(&l, &off, &v));
    /* The OFF an error owes goes first, then the word. */
    read_page(LINK_PS_FITTED, 0u, 800u);
    prog_supply_link_cut(&l);
    CHECK(prog_supply_link_next(&l, &off, &v));
    CHECK_EQ(off, LINK_PS_OUTPUT);
    CHECK_EQ(v, 0u);
    prog_supply_link_written(&l, PROG_SUPPLY_LINK_ACK);
    CHECK(prog_supply_link_next(&l, &off, &v));
    CHECK_EQ(off, LINK_PS_PANEL);
    CHECK_EQ(v, 1u);
}

int main(void)
{
    RUN(an_iomcu_without_the_page_gets_nothing);
    RUN(an_error_owes_one_off_until_the_iomcu_answers);
    RUN(the_page_is_read_at_once_then_every_200ms);
    RUN(supply_shows_the_state_as_read);
    RUN(the_board_or_a_read_makes_the_switch_the_ports_supply);
    RUN(the_gates_count_it_live_until_read_off_and_free);
    RUN(the_panel_gives_its_word_when_a_read_finds_none);
    return test_summary("prog_supply_link");
}
