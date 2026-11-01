/*
 * The session's entry, reads, leave and refusals
 * (protocols/escwire/ew_session.c), against the simulated ESC of ew_sim.h.
 * Writes are in test_ew_session_write.c.
 *
 * Under test: the line through an entry; the hold to its limit and across the
 * clock's wrap; the first contact on its first and third try, silent, with a
 * wrong answer; every ESC identified; a refused ESC; reads on a first and third
 * try, each fault, none on 3; the frames of a read and a leave byte for byte;
 * a leave's low time; a driver that refuses or never ends; a guard that refuses
 * what the session built; abort and stop in every phase; every refusal.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "ew_sim.h"

static sim_t esc;
static ew_session_t s;

static void up_s(void)
{
    sim_init_efm8(&esc, 0xB2u, false);
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
}

static void up_bj(void)
{
    sim_init_efm8(&esc, 0xB2u, true);
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
}

static void up_am(uint8_t rev, uint8_t protocol)
{
    sim_init_am(&esc, rev, protocol);
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
}

TEST_CASE(init_needs_every_hook)
{
    sim_init_efm8(&esc, 0xB2u, false);
    ew_driver_t d = sim_driver(&esc);

    CHECK_EQ(ew_session_init(NULL, &d), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_init(&s, NULL), EW_SES_ERR_ARG);
    d.start = NULL;
    CHECK_EQ(ew_session_init(&s, &d), EW_SES_ERR_ARG);
    d = sim_driver(&esc);
    d.poll = NULL;
    CHECK_EQ(ew_session_init(&s, &d), EW_SES_ERR_ARG);
    d = sim_driver(&esc);
    d.abort = NULL;
    CHECK_EQ(ew_session_init(&s, &d), EW_SES_ERR_ARG);
    d = sim_driver(&esc);
    d.line = NULL;
    CHECK_EQ(ew_session_init(&s, &d), EW_SES_ERR_ARG);
    d = sim_driver(&esc);
    d.now_us = NULL;
    CHECK_EQ(ew_session_init(&s, &d), EW_SES_ERR_ARG);
    d = sim_driver(&esc);
    CHECK_EQ(ew_session_init(&s, &d), EW_SES_OK);
    CHECK_EQ(esc.line_calls, 0);
}

TEST_CASE(the_hold_to_its_limit_and_across_the_wrap)
{
    up_s();
    esc.now = 0xFFFFF000u;
    CHECK_EQ(ew_session_enter(&s, 3000u), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_hold(&s), EW_SES_OK);
    CHECK_EQ(esc.line, EW_LINE_HIGH);
    CHECK_EQ(ew_session_enter(&s, EW_HOLD_MIN_MS - 1u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_enter(&s, EW_HOLD_MAX_MS + 1u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_enter(NULL, 3000u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_enter(&s, EW_HOLD_MIN_MS), EW_SES_BUSY);
    /* A second one while it runs. */
    CHECK_EQ(ew_session_enter(&s, EW_HOLD_MIN_MS), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_hold(&s), EW_SES_ERR_STATE);
    /* Exactly the hold: nothing yet.  One tick more: the first contact. */
    const uint32_t t0 = esc.now;

    esc.now = t0 + EW_HOLD_MIN_MS * 1000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.starts, 0);
    CHECK_EQ(esc.line, EW_LINE_HIGH);
    esc.now++;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.starts, 1);
    CHECK(sim_sent(&esc, 0, "00 00 00 00 00 00 00 00 0D 42 4C 48 65 6C 69 F4 7D"));
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    /* The longest hold. */
    up_s();
    (void)ew_session_hold(&s);
    CHECK_EQ(ew_session_enter(&s, EW_HOLD_MAX_MS), EW_SES_BUSY);
    esc.now += EW_HOLD_MAX_MS * 1000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.starts, 0);
}

TEST_CASE(an_entry_of_each_firmware)
{
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.fw, EW_FW_S);
    CHECK(s.id.writable);
    CHECK(s.in_mode);
    CHECK_EQ(esc.line, EW_LINE_UP);
    CHECK(sim_sent(&esc, 1, "FF 00 1A 00 3B 74"));
    CHECK(sim_sent(&esc, 2, "03 70 01 14"));
    CHECK_EQ(esc.invalid, 0);

    up_bj();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.fw, EW_FW_BJ);
    CHECK_EQ(s.id.len, 255u);

    sim_init_efm8(&esc, 0xB5u, false);
    {
        const ew_driver_t d = sim_driver(&esc);

        (void)ew_session_init(&s, &d);
    }
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.fw, EW_FW_S);
    CHECK_EQ(esc.log[1].b[2], 0x30u);
    CHECK_EQ(esc.log[1].b[3], 0x00u);

    up_am(4u, 2u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.fw, EW_FW_AM);
    CHECK(s.id.writable);
    CHECK_STR_EQ(s.id.name, "BOARD_F421");
    CHECK(sim_sent(&esc, 1, "FF 00 00 20 31 CC"));
    CHECK(sim_sent(&esc, 2, "03 C0 00 A0"));
    CHECK_EQ(esc.starts, 5); /* contact, settings, name */

    up_am(4u, 3u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.starts, 7); /* and the record */
    CHECK(s.id.writable);
    /* A CAN build. */
    up_am(4u, 3u);
    esc.am_record[19] = 0u;
    esc.am_record[20] = 0x40u;
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(!s.id.writable);
    CHECK_EQ(s.id.why, EW_WHY_CAN_BUILD);
}

TEST_CASE(the_first_contact_on_its_third_try_and_none)
{
    up_s();
    sim_script(&esc, F_SILENT, 0, 2, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(sim_sent(&esc, 2, "00 00 00 00 00 00 00 00 0D 42 4C 48 65 6C 69 F4 7D"));

    up_s();
    sim_script(&esc, F_SILENT, 0, 3, 0u);
    (void)ew_session_hold(&s);
    (void)ew_session_enter(&s, 3000u);
    const uint32_t t0 = esc.now;

    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_SILENT);
    /* 3 windows of 100ms after the hold, 17 bytes each, then the hold for
     * the text interface and 3 windows of `info`, 5 characters each. */
    const uint32_t spent = 3000000u + 3u * (100000u + 170u * 52u)
                           + EW_SES_TEXT_HOLD_MS * 1000u
                           + 3u * (EW_TEXT_FIRST_US + 50u * 26u);

    CHECK(esc.now - t0 >= spent);
    CHECK(esc.now - t0 <= spent + 5000u);
    CHECK_EQ(esc.starts, 6);
    CHECK(sim_sent_line(&esc, 3, "info\n"));
    CHECK(!s.in_mode);

    /* Bytes that are no answer. */
    up_s();
    esc.answer[0] = 0x35u;
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK(!s.in_mode);
    up_s();
    sim_script(&esc, F_BYTE, 0, 3, 0x12u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK_EQ(s.last_ans, EW_ANS_LENGTH);
    CHECK(!s.in_mode);
    /* A contact whose bytes are all there and wrong. */
    up_s();
    sim_script(&esc, F_FRAMING, 0, 3, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_CONTACT);
    CHECK_EQ(s.last_ans, EW_ANS_FRAMING);
}

TEST_CASE(an_esc_that_is_refused)
{
    /* A signature no part has: nothing more goes out. */
    up_s();
    esc.answer[5] = 0xB3u;
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_NOT_KNOWN);
    CHECK_EQ(s.id.why, EW_WHY_SIGNATURE);
    CHECK(!s.in_mode);
    CHECK_EQ(esc.starts, 1);
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_STATE);
    /* A protocol not known: no read, a leave of its kind. */
    up_am(4u, 1u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_NOT_KNOWN);
    CHECK_EQ(s.id.why, EW_WHY_PROTOCOL);
    CHECK(s.in_mode);
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(sim_sent(&esc, 1, "00 00 00 00"));
    /* A page that is nothing known: read, and nothing written. */
    up_s();
    esc.flash[esc.base] = 15u;
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_NOT_KNOWN);
    CHECK_EQ(s.id.fw, EW_FW_NONE);
    CHECK(s.in_mode);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_write(&s, s.orig, s.id.len), EW_SES_ERR_NOT_WRITABLE);
    CHECK_EQ(esc.erases, 0);
}

TEST_CASE(an_identifying_read_that_fails)
{
    up_s();
    sim_script(&esc, F_SILENT, 1, 3, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_READ);
    up_am(4u, 3u);
    sim_script(&esc, F_SILENT, 3, 20, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_READ);
    up_am(4u, 3u);
    sim_script(&esc, F_BAD_CRC, 6, 6, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_READ);
    CHECK_EQ(s.last_ans, EW_ANS_CRC);
}

TEST_CASE(a_read_on_its_first_and_third_try)
{
    up_bj();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    int at = esc.starts;

    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(s.have_read);
    CHECK(memcmp(s.orig, esc.flash + esc.base, 255u) == 0);
    CHECK(memcmp(s.page, esc.flash + esc.base, 255u) == 0);
    CHECK_EQ(esc.starts, at + 2);

    /* Each way a read answer can fail, twice; the address goes again. */
    static const uint8_t k_faults[] = { F_SILENT, F_BAD_CRC, F_FRAMING,
                                        F_OVERFLOW, F_ABORTED, F_HEARD_SILENT };
    for (size_t i = 0; i < sizeof(k_faults); ++i) {
        up_am(4u, 2u);
        CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
        at = esc.starts;
        esc.fail = k_faults[i];
        esc.fail_at = at + 1;
        esc.fail_n = 1;
        CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
        CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
        CHECK_EQ(esc.starts, at + 4);
        CHECK(sim_sent(&esc, at + 2, "FF 00 00 20 31 CC"));
        CHECK(sim_sent(&esc, at + 3, "03 C0 00 A0"));
    }
    /* An answer of one byte. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    sim_script(&esc, F_BYTE, 1, 1, EW_A_REF_UNKNOWN);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    /* An address answered badly, twice. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    sim_script(&esc, F_BYTE, 0, 2, EW_A_REF_CRC);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(a_read_with_no_answer_on_3_tries)
{
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    /* The address stands and the read fails. */
    int at = esc.starts;

    esc.fail = F_BAD_CRC;
    esc.fail_at = at;
    esc.fail_n = 12;
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_READ);
    CHECK_EQ(esc.starts, at + 6);
    CHECK(!s.have_read);
    CHECK(s.in_mode);
    /* The address itself. */
    at = esc.starts;
    sim_script(&esc, F_SILENT, 0, 3, 0u);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_READ);
    CHECK_EQ(esc.starts, at + 3);
    CHECK_EQ(s.last_ans, EW_ANS_SILENT);
}

TEST_CASE(a_leave_of_each_firmware_and_its_low_time)
{
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    const int at = esc.starts;

    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK(sim_sent(&esc, at, "00 01 C1 C0"));
    /* The frame's 40 bits, then low: not parked before EW_LEAVE_LOW_MS. */
    esc.now += 40u * 52u + 1u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.line, EW_LINE_LOW);
    CHECK(!s.in_mode);
    esc.now += EW_LEAVE_LOW_MS * 1000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.line, EW_LINE_LOW);
    esc.now++;
    CHECK_EQ(ew_session_step(&s), EW_SES_OK);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);

    up_am(4u, 2u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(sim_sent(&esc, esc.starts - 1, "00 00 00 00"));
    CHECK_EQ(esc.line, EW_LINE_PARK);
}

TEST_CASE(a_driver_that_refuses_or_does_not_end)
{
    /* start() false, at the contact and later. */
    up_s();
    sim_script(&esc, F_START_FALSE, 0, 1, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_DRIVER);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!s.in_mode);
    up_s();
    sim_script(&esc, F_START_FALSE, 2, 1, 0u);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_ERR_DRIVER);
    /* A poll that never ends: the longest time and the grace, then out. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    sim_script(&esc, F_STUCK, 0, 1, 0u);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    const uint32_t t0 = esc.now;
    const uint32_t wait = s.wait_us;

    CHECK_EQ(wait, ew_xfer_span_us(&s.x) + EW_SES_GRACE_US);
    esc.now = t0 + wait;
    CHECK_EQ(ew_session_step(&s), EW_SES_BUSY);
    CHECK_EQ(esc.aborts, 0);
    esc.now++;
    CHECK_EQ(ew_session_step(&s), EW_SES_ERR_DRIVER);
    CHECK_EQ(esc.aborts, 1);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!s.in_mode);
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
}

TEST_CASE(what_the_guard_refuses_reaches_no_driver)
{
    /* The session is told to talk to no contact. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    int at = esc.starts;

    s.guard.cpu = EW_CPU_NONE;
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_NO_CONTACT);
    CHECK_EQ(esc.starts, at);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!s.in_mode);
    /* A leave. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    at = esc.starts;
    s.guard.cpu = EW_CPU_ARM;
    s.contact.cpu = EW_CPU_EFM8;
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_LEAVE_ARG);
    CHECK_EQ(esc.starts, at);
    /* A read whose address is taken from a memory that lost it. */
    up_s();
    s.guard.cpu = EW_CPU_ARM;
    CHECK_EQ(ew_session_hold(&s), EW_SES_OK);
    CHECK_EQ(s.guard.cpu, EW_CPU_NONE);
    /* A first contact while the guard holds another target. */
    (void)ew_session_enter(&s, 3000u);
    s.guard.cpu = EW_CPU_EFM8;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_FORM);
    CHECK_EQ(esc.starts, 0);
}

TEST_CASE(a_stop_or_abort_in_each_phase)
{
    /* The line held, no operation. */
    up_s();
    (void)ew_session_hold(&s);
    ew_session_abort(&s);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(ew_session_enter(&s, 3000u), EW_SES_ERR_STATE);
    /* The hold. */
    up_s();
    (void)ew_session_hold(&s);
    (void)ew_session_enter(&s, 3000u);
    ew_session_stop(&s);
    CHECK_EQ(s.result, EW_SES_ERR_STOPPED);
    CHECK(s.stopped);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(esc.aborts, 0);
    esc.now += 4000000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_ERR_STOPPED);
    CHECK_EQ(esc.starts, 0);
    /* A transaction: abort. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    ew_session_abort(&s);
    CHECK_EQ(s.result, EW_SES_ERR_ABORTED);
    CHECK_EQ(esc.aborts, 1);
    CHECK(!s.in_mode);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    const int at = esc.starts;

    esc.now += 1000000u;
    CHECK_EQ(ew_session_step(&s), EW_SES_ERR_ABORTED);
    CHECK_EQ(esc.starts, at);
    /* A leave's low time. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    esc.now += 40u * 52u + 1u;
    (void)ew_session_step(&s);
    CHECK_EQ(esc.line, EW_LINE_LOW);
    ew_session_abort(&s);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    /* Idle: the result stays, and the ESC is gone. */
    up_s();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    ew_session_stop(&s);
    CHECK_EQ(s.result, EW_SES_OK);
    CHECK(s.stopped);
    CHECK(!s.in_mode);
    /* Over again from the hold. */
    sim_power_cycle(&esc);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(!s.stopped);
    ew_session_stop(NULL);
    ew_session_abort(NULL);
    CHECK(!ew_session_writing(NULL));
}

TEST_CASE(null_arguments)
{
    up_s();
    const uint8_t p[192] = { 0 };
    ew_backup_id_t id;

    memset(&id, 0, sizeof(id));
    CHECK_EQ(ew_session_hold(NULL), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_read(NULL), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_write(NULL, p, 192u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_write(&s, NULL, 192u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_restore(NULL, &id, p, 192u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_restore(&s, NULL, p, 192u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_restore(&s, &id, NULL, 192u), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_leave(NULL), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_step(NULL), EW_SES_ERR_ARG);
    /* Nothing started. */
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_write(&s, p, 192u), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_restore(&s, &id, p, 192u), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_step(&s), EW_SES_OK);
    /* One operation at a time. */
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_STATE);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.line_in_xfer, 0);
    CHECK_EQ(esc.parked_tx, 0);
}

int main(void)
{
    RUN(init_needs_every_hook);
    RUN(the_hold_to_its_limit_and_across_the_wrap);
    RUN(an_entry_of_each_firmware);
    RUN(the_first_contact_on_its_third_try_and_none);
    RUN(an_esc_that_is_refused);
    RUN(an_identifying_read_that_fails);
    RUN(a_read_on_its_first_and_third_try);
    RUN(a_read_with_no_answer_on_3_tries);
    RUN(a_leave_of_each_firmware_and_its_low_time);
    RUN(a_driver_that_refuses_or_does_not_end);
    RUN(what_the_guard_refuses_reaches_no_driver);
    RUN(a_stop_or_abort_in_each_phase);
    RUN(null_arguments);
    return test_summary("ew_session");
}
