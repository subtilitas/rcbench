/*
 * The session's writes (protocols/escwire/ew_session.c) against the simulated
 * ESC of ew_sim.h.
 *
 * Under test: a write of each firmware, its frames in order and every other
 * byte kept; a draft with no change; every refusal; a page that changed before
 * the write; AM32's byte 2; each step failing, once and for good; a stop inside
 * the erase-to-program stretch; an abort and a power loss at every transaction;
 * what the ESC looks like at the next start; the restore of an erased AM32;
 * the whole write across the clock's wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "ew_sim.h"

static sim_t esc;
static ew_session_t s;
static uint8_t g_draft[EW_PAGE_MAX];
static uint8_t g_before[EW_PAGE_MAX];

static void attach(void)
{
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
}

/* Entered and read. */
static void ready(void)
{
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    memcpy(g_draft, s.orig, sizeof(g_draft));
    memcpy(g_before, s.orig, sizeof(g_before));
}

static void up_s(void)
{
    sim_init_efm8(&esc, 0xB2u, false);
    attach();
    ready();
}

static void up_bj(void)
{
    sim_init_efm8(&esc, 0xB2u, true);
    attach();
    ready();
}

static void up_am(void)
{
    sim_init_am(&esc, 4u, 2u);
    attach();
    ready();
}

TEST_CASE(a_write_of_family_s)
{
    up_s();
    g_draft[0x15] = 5u;
    g_draft[0x0B] = 2u;
    const int at = esc.starts;

    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.erases, 1);
    CHECK_EQ(esc.programs, 1);
    /* The re-read, then the procedure of the specification, then the read. */
    CHECK(sim_sent(&esc, at + 0, "FF 00 1A 00 3B 74"));
    CHECK(sim_sent(&esc, at + 1, "03 70 01 14"));
    CHECK(sim_sent(&esc, at + 2, "FF 00 1A 00 3B 74"));
    CHECK(sim_sent(&esc, at + 3, "02 01 C0 A0"));
    CHECK(sim_sent(&esc, at + 4, "FF 00 1A 00 3B 74"));
    CHECK_EQ(esc.log[at + 5].n, 6u + 0x70u + 2u);
    CHECK_EQ(esc.log[at + 5].b[0], 0xFEu);
    CHECK(sim_sent(&esc, at + 6, "01 01 C0 50"));
    CHECK(sim_sent(&esc, at + 7, "FF 00 1A 00 3B 74"));
    CHECK(sim_sent(&esc, at + 8, "03 70 01 14"));
    CHECK_EQ(esc.starts, at + 9);
    /* The flash: the two bytes changed, nothing else in what was written. */
    for (unsigned i = 0u; i < 0x70u; ++i) {
        const uint8_t want = i == 0x15u ? 5u : (i == 0x0Bu ? 2u : g_before[i]);

        CHECK_EQ(esc.flash[esc.base + i], want);
    }
    CHECK(memcmp(s.orig, esc.flash + esc.base, 0x70u) == 0);
    CHECK(!s.dirty);
    CHECK(s.guard.approved == NULL);
    CHECK_EQ(s.attempts, 1u);
    CHECK_EQ(esc.invalid, 0);
    /* A leave is free again. */
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(a_write_of_bluejay_keeps_its_melody)
{
    up_bj();
    g_draft[0x10] = 128u;
    CHECK_EQ(ew_session_write(&s, g_draft, 255u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.flash[esc.base + 0x10], 128u);
    for (unsigned i = 0x70u; i <= 0xFEu; ++i) {
        CHECK_EQ(esc.flash[esc.base + i], g_before[i]);
    }
    /* The safety arm byte stays as read. */
    memcpy(g_draft, s.orig, 255u);
    g_draft[0x2A] = 1u;
    CHECK_EQ(ew_session_write(&s, g_draft, 255u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_CHANGED_BYTE);
    CHECK_EQ(s.bad_off, 0x2Au);
}

TEST_CASE(a_write_of_am32_has_no_erase)
{
    up_am();
    g_draft[0x18] = 48u;
    const int at = esc.starts;

    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.erases, 0);
    for (int i = at; i < esc.starts; ++i) {
        CHECK(esc.log[i].b[0] != EW_A_CMD_ERASE);
    }
    CHECK(sim_sent(&esc, at + 2, "FF 00 00 20 31 CC"));
    CHECK_EQ(esc.log[at + 3].n, 6u + 192u + 2u);
    CHECK(sim_sent(&esc, at + 4, "01 01 C0 50"));
    CHECK_EQ(esc.am[0x18], 48u);
    CHECK_EQ(esc.am[0], 1u);
    /* The tune as it was. */
    CHECK(memcmp(esc.am + 0x30, g_before + 0x30, 0x90u) == 0);
    /* The rest of the erase unit is the ESC's. */
    CHECK_EQ(esc.am[192], 0xFFu);
}

TEST_CASE(a_pause_between_announcement_and_data)
{
    up_am();
    g_draft[0x18] = 48u;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    /* The transaction with the data is the fourth of the write; */
    const int at = esc.starts - 1;

    for (int i = 0; i < 100000 && esc.starts < at + 4; ++i) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
    CHECK_EQ(s.x.n_head, 6u);
    CHECK_EQ(s.x.pause_bits, EW_SES_PAUSE_BITS);
    CHECK_EQ(s.x.n_tx, 6u + 192u + 2u);
    CHECK(s.x.pause_bits * EW_BIT_A_NS < 260000u);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(a_draft_with_no_change_sends_nothing)
{
    up_s();
    const int at = esc.starts;

    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_OK);
    CHECK_EQ(esc.starts, at);
    CHECK_EQ(esc.erases, 0);
}

TEST_CASE(every_refusal_of_a_write)
{
    up_s();
    const int at = esc.starts;

    /* The length. */
    CHECK_EQ(ew_session_write(&s, g_draft, 0x6Fu), EW_SES_ERR_ARG);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x71u), EW_SES_ERR_ARG);
    /* A tag, the marker. */
    g_draft[0x60] = 0x41u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_CHANGED_BYTE);
    CHECK_EQ(s.bad_off, 0x60u);
    memcpy(g_draft, s.orig, 0x70u);
    g_draft[0x0D] = 0u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_REFUSED);
    /* A value. */
    memcpy(g_draft, s.orig, 0x70u);
    g_draft[0x15] = 6u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_VALUE);
    CHECK_EQ(esc.starts, at);
    /* No read yet. */
    sim_init_efm8(&esc, 0xB2u, false);
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_NO_READ);
    /* A layout shown only. */
    sim_init_efm8(&esc, 0xB2u, false);
    esc.flash[esc.base + 2u] = 32u;
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.why, EW_WHY_OLD_LAYOUT);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_NOT_WRITABLE);
    /* A reset page. */
    sim_init_efm8(&esc, 0xB2u, false);
    memset(esc.flash + esc.base + 0x40, 0xFF, 48u);
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.id.why, EW_WHY_RESET_PAGE);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_NOT_WRITABLE);
    CHECK_EQ(esc.erases, 0);
    /* While an operation runs. */
    up_s();
    g_draft[0x15] = 5u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_STATE);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    /* A revision below 3. */
    sim_init_am(&esc, 2u, 2u);
    attach();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_write(&s, s.orig, 192u), EW_SES_ERR_NOT_WRITABLE);
    /* 0x0D of a revision 3 page. */
    sim_init_am(&esc, 3u, 2u);
    attach();
    ready();
    g_draft[0x0D] = 2u;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_CHANGED_BYTE);
}

TEST_CASE(a_page_that_changed_since_the_read)
{
    up_s();
    g_draft[0x15] = 5u;
    esc.flash[esc.base + 0x1Bu] = 41u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_CHANGED);
    CHECK_EQ(esc.erases, 0);
    CHECK_EQ(esc.programs, 0);
    CHECK(!s.dirty);
    CHECK(s.guard.approved == NULL);
    /* The re-read itself failing: nothing was erased. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_SILENT, 0, 3, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_READ);
    CHECK_EQ(esc.erases, 0);
    CHECK(!s.dirty);
    CHECK(s.guard.approved == NULL);
}

TEST_CASE(am32_byte_2_may_differ)
{
    /* At the re-read: taken, the write goes on. */
    up_am();
    g_draft[0x18] = 48u;
    esc.am[2] = 7u;
    esc.am_boot_version = 7u;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.orig[2], 7u);
    /* At the read-back: the bootloader sets it. */
    up_am();
    g_draft[0x18] = 48u;
    esc.am_boot_version = 9u;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.am[2], 9u);
    CHECK_EQ(s.orig[2], 9u);
    CHECK_EQ(s.attempts, 1u);
    /* Only byte 2. */
    up_am();
    g_draft[0x18] = 48u;
    esc.am[3] = 99u;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_CHANGED);
}

TEST_CASE(an_efm8_byte_2_may_not)
{
    up_s();
    g_draft[0x15] = 5u;
    esc.flash[esc.base + 2u] = 32u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_CHANGED);
}

/* The index, from the write's first transaction, of each step on an EFM8. */
enum { I_ERASE = 3, I_SET2 = 4, I_DATA = 5, I_PROGRAM = 6, I_SET3 = 7,
       I_READBACK = 8 };

TEST_CASE(each_step_failing_once)
{
    /* An erase whose answer is lost: read, and the procedure again. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_HEARD_SILENT, I_ERASE, 1, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.attempts, 2u);
    CHECK_EQ(esc.erases, 2);
    CHECK_EQ(esc.flash[esc.base + 0x15u], 5u);
    /* A program's answer lost, the flash taken: the read says it is done. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_HEARD_SILENT, I_PROGRAM, 1, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.attempts, 1u);
    CHECK_EQ(esc.erases, 1);
    CHECK_EQ(esc.programs, 1);
    /* A buffer with a bad CRC: the same buffer again, no second erase. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_BYTE, I_DATA, 1, EW_A_REF_CRC);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.erases, 1);
    /* The address between: silent twice. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_HEARD_SILENT, I_SET2, 2, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.erases, 1);
    /* An erase refused once. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_BYTE, I_ERASE, 1, EW_A_REF_UNKNOWN);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.attempts, 2u);
    /* A program refused by the bootloader. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_BYTE, I_PROGRAM, 1, EW_A_REF_PROGRAM);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    /* The read-back fails once: its address goes again. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_BAD_CRC, I_READBACK, 1, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.attempts, 1u);
    /* A data buffer that never goes through on AM32: nothing has happened. */
    up_am();
    g_draft[0x18] = 48u;
    sim_script(&esc, F_BAD_BUFFER, 0, 100, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_WRITE_FAILED);
    CHECK(!s.dirty);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK_EQ(esc.programs, 0);
    CHECK_EQ(esc.am[0x18], 24u);
}

TEST_CASE(a_write_that_does_not_take)
{
    /* AM32: answered 0x30, the page as it was, 3 times: as read. */
    up_am();
    g_draft[0x18] = 48u;
    const int at = esc.starts;

    esc.fail = F_NO_TAKE;
    esc.fail_at = at;
    esc.fail_n = 100;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_WRITE_FAILED);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK(!s.dirty);
    CHECK(s.guard.approved == NULL);
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);

    /* An EFM8: erased and not programmed: torn. */
    up_s();
    g_draft[0x15] = 5u;
    esc.fail = F_NO_TAKE;
    esc.fail_at = esc.starts;
    esc.fail_n = 100;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_TORN);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK(s.dirty);
    CHECK(s.reread);
    /* Torn: no leave; no other draft; the intended one or the first page. */
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    {
        uint8_t other[EW_PAGE_MAX];

        memcpy(other, g_draft, sizeof(other));
        other[0x0B] = 2u;
        CHECK_EQ(ew_session_write(&s, other, 0x70u), EW_SES_ERR_DIRTY);
    }
    esc.fail = F_NONE;
    CHECK_EQ(ew_session_write(&s, g_before, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(memcmp(esc.flash + esc.base, g_before, 0x70u) == 0);
    CHECK(!s.dirty);
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(a_stored_byte_that_is_wrong)
{
    /* Wrong once: the procedure runs again and takes. */
    up_s();
    g_draft[0x15] = 5u;
    sim_script(&esc, F_CORRUPT, I_PROGRAM, 1, 0x09u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(s.attempts, 2u);
    CHECK_EQ(esc.flash[esc.base + 0x09u], g_before[0x09u]);
    /* Wrong every time on AM32. */
    up_am();
    g_draft[0x18] = 48u;
    sim_script(&esc, F_CORRUPT, 0, 100, 0x1Bu);
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_TORN);
    CHECK_EQ(esc.programs, (int)EW_SES_ATTEMPTS);
    /* After it: a good write of the same draft. */
    esc.fail = F_NONE;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(an_esc_lost_inside_a_write)
{
    up_s();
    g_draft[0x15] = 5u;
    /* From the data on: gone, the page erased. */
    sim_script(&esc, F_DEAD, I_DATA, 1, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_LOST);
    CHECK(s.dirty);
    CHECK(!s.reread);
    /* A read first, before any write. */
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_ERR_MUST_READ);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
    /* The ESC answers again: the read keeps the first page and tells the
     * write it may go. */
    esc.dead = false;
    esc.contacted = true;
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(s.reread);
    CHECK_EQ(s.page[0], 0xFFu);
    CHECK_EQ(s.orig[0], 16u);
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.flash[esc.base + 0x15u], 5u);
    CHECK_EQ(esc.flash[esc.base + 0x40u], g_before[0x40u]);
}

TEST_CASE(a_stop_in_a_write)
{
    /* Before the first erase: at once, nothing erased. */
    up_s();
    g_draft[0x15] = 5u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    CHECK(!ew_session_writing(&s));
    ew_session_stop(&s);
    CHECK_EQ(s.result, EW_SES_ERR_STOPPED);
    CHECK_EQ(esc.erases, 0);
    CHECK_EQ(esc.line, EW_LINE_PARK);

    /* During the erase: the write goes to its end and verifies. */
    up_s();
    g_draft[0x15] = 5u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    while (esc.erases == 0) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
    CHECK(ew_session_writing(&s));
    ew_session_stop(&s);
    CHECK(s.stop_wanted);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.programs, 1);
    CHECK(!s.dirty);
    CHECK(s.stopped);
    CHECK(!s.in_mode);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!ew_session_writing(&s));

    /* A stop and a failure: no second procedure. */
    up_s();
    g_draft[0x15] = 5u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    while (esc.erases == 0) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
    ew_session_stop(&s);
    esc.fail = F_NO_TAKE;
    esc.fail_at = esc.starts;
    esc.fail_n = 100;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_TORN);
    CHECK_EQ(esc.erases, 1);
    CHECK(s.dirty);
    CHECK_EQ(esc.line, EW_LINE_PARK);
}

TEST_CASE(an_abort_at_every_transaction)
{
    for (int fw = 0; fw < 2; ++fw) {
        for (int i = 0; i < 12; ++i) {
            if (fw == 0) {
                up_s();
                g_draft[0x15] = 5u;
            } else {
                up_am();
                g_draft[0x18] = 48u;
            }
            const uint16_t len = s.id.len;
            const int at = esc.starts;

            CHECK_EQ(ew_session_write(&s, g_draft, len), EW_SES_BUSY);
            ew_ses_t r = EW_SES_BUSY;

            while (r == EW_SES_BUSY && esc.starts < at + 1 + i) {
                esc.now += 500u;
                r = ew_session_step(&s);
            }
            const int starts = esc.starts;

            ew_session_abort(&s);
            CHECK_EQ(esc.line, EW_LINE_PARK);
            CHECK(!s.in_mode);
            CHECK(!esc.running);
            esc.now += 5000000u;
            (void)ew_session_step(&s);
            CHECK_EQ(esc.starts, starts);
            /* Nothing runs after it; a new start takes the ESC as it is. */
            CHECK_EQ(ew_session_read(&s), EW_SES_ERR_STATE);
            CHECK_EQ(ew_session_hold(&s), EW_SES_OK);
            CHECK(!s.dirty);
        }
    }
}

TEST_CASE(a_power_loss_at_every_transaction)
{
    /* An EFM8: the page is the old one, the new one, or, after the ESC's own
     * reset, its defaults with tags of 0xFF, shown read-only. */
    for (int i = 0; i < 12; ++i) {
        up_s();
        g_draft[0x15] = 5u;
        sim_script(&esc, F_DEAD, i, 1, 0u);
        CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
        const ew_ses_t r = sim_run(&s, &esc);

        CHECK(r == EW_SES_OK || r == EW_SES_ERR_READ || r == EW_SES_ERR_LOST);
        sim_power_cycle(&esc);
        CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
        CHECK_EQ(s.id.fw, EW_FW_S);
        if (i >= I_ERASE + 1 && i <= I_PROGRAM) {
            CHECK_EQ(s.id.why, EW_WHY_RESET_PAGE);
            CHECK(!s.id.writable);
            CHECK_EQ(esc.flash[esc.base + 0x60u], 0xFFu);
            CHECK_EQ(esc.flash[esc.base + 0x02u], 33u);
            CHECK_EQ(esc.flash[esc.base + 0x0Du], 0x55u);
        } else {
            CHECK(s.id.writable);
        }
    }
    /* AM32 at every transaction of a write. */
    for (int i = 0; i < 9; ++i) {
        up_am();
        g_draft[0x18] = 48u;
        sim_script(&esc, F_DEAD, i, 1, 0u);
        CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
        const ew_ses_t r = sim_run(&s, &esc);

        CHECK(r == EW_SES_OK || r == EW_SES_ERR_READ || r == EW_SES_ERR_LOST);
        sim_power_cycle(&esc);
        CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
        CHECK(s.id.writable);
    }
}

/* An AM32 that lost power between the erase and the program. */
static void erased_am(void)
{
    up_am();
    g_draft[0x18] = 48u;
    sim_script(&esc, F_ERASE_DEAD, 0, 100, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_LOST);
    sim_power_cycle(&esc);
}

TEST_CASE(an_erased_am32_takes_only_its_backup)
{
    ew_backup_id_t from;

    erased_am();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(s.id.erased);
    CHECK(!s.id.writable);
    CHECK_EQ(s.id.why, EW_WHY_ERASED);
    /* Nothing built here, no leave: the ESC could not start. */
    CHECK_EQ(ew_session_write(&s, g_before, 192u), EW_SES_ERR_ERASED);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_ERASED);
    /* The backup of another ESC. */
    ew_session_backup_id(&s, &from);
    CHECK_STR_EQ(from.name, "BOARD_F421");
    CHECK_EQ(from.pin_code, 0x14u);
    CHECK_EQ(from.flash_code, 0x35u);
    CHECK_EQ(from.protocol, 2u);
    from.pin_code = 0x02u;
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_BACKUP);
    ew_session_backup_id(&s, &from);
    from.flash_code = 0x1Fu;
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_BACKUP);
    ew_session_backup_id(&s, &from);
    from.protocol = 3u;
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_BACKUP);
    ew_session_backup_id(&s, &from);
    from.name[0] = 'X';
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_BACKUP);
    /* A backup that breaks a rule. */
    ew_session_backup_id(&s, &from);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 191u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_AM_LENGTH);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, EW_PAGE_MAX + 1u),
             EW_SES_ERR_ARG);
    {
        uint8_t bad[192];

        memcpy(bad, g_before, sizeof(bad));
        bad[0] = 0xFFu;
        CHECK_EQ(ew_session_restore(&s, &from, bad, 192u), EW_SES_ERR_REFUSED);
        CHECK_EQ(s.refuse, EW_REF_AM_BYTE0);
        bad[0] = 1u;
        bad[1] = 2u;
        CHECK_EQ(ew_session_restore(&s, &from, bad, 192u), EW_SES_ERR_REFUSED);
        CHECK_EQ(s.refuse, EW_REF_AM_REVISION);
    }
    /* The backup. */
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.am[0], 1u);
    CHECK(memcmp(esc.am + 3, g_before + 3, 189u) == 0);
    CHECK(!s.id.erased);
    CHECK(s.id.writable);
    CHECK_EQ(s.id.layout, 4u);
    CHECK_EQ(s.id.why, EW_WHY_NONE);
    /* And on from there. */
    memcpy(g_draft, s.orig, 192u);
    g_draft[0x18] = 30u;
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_leave(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

TEST_CASE(a_restore_refused_or_failing)
{
    ew_backup_id_t from;

    /* A page that is not erased. */
    up_am();
    ew_session_backup_id(&s, &from);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_STATE);
    /* Byte 0 is 0xFF and the rest is not. */
    erased_am();
    esc.am[191] = 0u;
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(s.id.erased);
    ew_session_backup_id(&s, &from);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_RESTORE);
    /* A program that does not take: still erased. */
    erased_am();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    ew_session_backup_id(&s, &from);
    esc.fail = F_NO_TAKE;
    esc.fail_at = esc.starts;
    esc.fail_n = 100;
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_WRITE_FAILED);
    CHECK(s.id.erased);
    CHECK_EQ(esc.programs, 0);
    CHECK_EQ(s.attempts, EW_SES_ATTEMPTS);
    CHECK(!s.restoring);
    /* A build for CAN boards: erased, and its backup refused. */
    sim_init_am(&esc, 4u, 3u);
    attach();
    ready();
    g_draft[0x18] = 48u;
    esc.am_record[19] = 0u;
    esc.am_record[20] = 0x40u;
    memset(esc.am, 0xFF, sizeof(esc.am));
    sim_power_cycle(&esc);
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK(s.id.erased);
    CHECK_EQ(s.id.why, EW_WHY_CAN_BUILD);
    ew_session_backup_id(&s, &from);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_RESTORE);
    CHECK_EQ(esc.programs, 0);
}

TEST_CASE(a_restore_left_half_done_takes_its_backup_again)
{
    ew_backup_id_t from;
    static uint8_t other[EW_PAGE_MAX];

    /* A wrong byte every time: neither erased nor the backup. */
    erased_am();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    ew_session_backup_id(&s, &from);
    esc.programs = 0;
    sim_script(&esc, F_CORRUPT, 0, 100, 0x1Bu);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_TORN);
    CHECK(s.dirty);
    CHECK(s.id.erased);
    /* Only that backup; no write of its own, and no leave. */
    memcpy(other, g_before, sizeof(other));
    other[0x18] = 30u;
    CHECK_EQ(ew_session_restore(&s, &from, other, 192u), EW_SES_ERR_DIRTY);
    CHECK_EQ(ew_session_write(&s, g_before, 192u), EW_SES_ERR_ERASED);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_ERASED);
    /* The fault gone: it goes in, with no compare against the erased page. */
    esc.fail = F_NONE;
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(memcmp(esc.am + 3, g_before + 3, 189u) == 0);
    CHECK(!s.id.erased);
    CHECK(!s.dirty);
    CHECK(s.id.writable);
    /* The ESC gone in the program: a read comes first. */
    erased_am();
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    ew_session_backup_id(&s, &from);
    sim_script(&esc, F_ERASE_DEAD, 0, 100, 0u);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_LOST);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_ERR_MUST_READ);
    esc.dead = false;
    esc.fail = F_NONE;
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_restore(&s, &from, g_before, 192u), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK(!s.id.erased);
}

/* Step until transaction @p n of the write runs. */
static void run_to(int at, int n)
{
    for (int i = 0; i < 100000 && esc.starts <= at + n; ++i) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
}

TEST_CASE(a_frame_the_guard_refuses_inside_a_write)
{
    static uint8_t other[EW_PAGE_MAX];
    int at;

    /* The read after an address that is gone. */
    up_s();
    g_draft[0x15] = 5u;
    at = esc.starts;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    s.guard.addr_set = false;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_NO_ADDRESS);
    CHECK_EQ(esc.starts, at + 1);
    CHECK(!s.in_mode);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    /* The erase. */
    up_s();
    g_draft[0x15] = 5u;
    at = esc.starts;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    run_to(at, 2);
    s.guard.addr = 0x1C00u;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_BOOTLOADER);
    CHECK_EQ(esc.erases, 0);
    /* The announcement. */
    up_s();
    g_draft[0x15] = 5u;
    at = esc.starts;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    run_to(at, 4);
    s.guard.len = 0x6Fu;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_COUNT);
    CHECK_EQ(esc.programs, 0);
    /* The data: not the page that was judged. */
    up_s();
    g_draft[0x15] = 5u;
    at = esc.starts;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    run_to(at, 4);
    memcpy(other, s.target, sizeof(other));
    other[0x60] ^= 1u;
    s.guard.approved = other;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_DATA);
    CHECK_EQ(esc.programs, 0);
    /* A program in a state with no data. */
    up_s();
    g_draft[0x15] = 5u;
    at = esc.starts;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    run_to(at, 5);
    s.guard.data_sent = false;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_REFUSED);
    CHECK_EQ(s.refuse, EW_REF_NO_DATA);
    CHECK_EQ(esc.programs, 0);
}

TEST_CASE(a_page_that_moved_during_a_write_that_never_programmed)
{
    /* Every buffer refused, and the page changes behind the re-read. */
    up_am();
    g_draft[0x18] = 48u;
    const int at = esc.starts;

    sim_script(&esc, F_BAD_BUFFER, 0, 100, 0u);
    CHECK_EQ(ew_session_write(&s, g_draft, 192u), EW_SES_BUSY);
    run_to(at, 2);
    esc.am[0x19] = 99u;
    CHECK_EQ(sim_run(&s, &esc), EW_SES_ERR_TORN);
    CHECK_EQ(esc.programs, 0);
    /* Neither page: the same rules as after a program. */
    CHECK(s.dirty);
    CHECK(s.reread);
    CHECK_EQ(ew_session_leave(&s), EW_SES_ERR_DIRTY);
}

TEST_CASE(a_whole_write_across_the_wrap)
{
    sim_init_efm8(&esc, 0xB2u, false);
    esc.now = 0xFFFFFF00u - 3000000u;
    attach();
    ready();
    g_draft[0x15] = 5u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    /* The ESC takes its time at the longest wait. */
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
    CHECK_EQ(esc.flash[esc.base + 0x15u], 5u);
    CHECK_EQ(esc.line_in_xfer, 0);
    CHECK_EQ(esc.parked_tx, 0);
    CHECK_EQ(esc.invalid, 0);
}

TEST_CASE(the_waits_of_an_erase_and_a_program)
{
    up_s();
    g_draft[0x15] = 5u;
    CHECK_EQ(ew_session_write(&s, g_draft, 0x70u), EW_SES_BUSY);
    while (esc.erases == 0) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
    CHECK_EQ(s.x.first_us, EW_A_ERASE_US);
    while (esc.programs == 0) {
        esc.now += 500u;
        (void)ew_session_step(&s);
    }
    CHECK_EQ(s.x.first_us, EW_A_PROGRAM_US);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

int main(void)
{
    RUN(a_write_of_family_s);
    RUN(a_write_of_bluejay_keeps_its_melody);
    RUN(a_write_of_am32_has_no_erase);
    RUN(a_pause_between_announcement_and_data);
    RUN(a_draft_with_no_change_sends_nothing);
    RUN(every_refusal_of_a_write);
    RUN(a_page_that_changed_since_the_read);
    RUN(am32_byte_2_may_differ);
    RUN(an_efm8_byte_2_may_not);
    RUN(each_step_failing_once);
    RUN(a_write_that_does_not_take);
    RUN(a_stored_byte_that_is_wrong);
    RUN(an_esc_lost_inside_a_write);
    RUN(a_stop_in_a_write);
    RUN(an_abort_at_every_transaction);
    RUN(a_power_loss_at_every_transaction);
    RUN(an_erased_am32_takes_only_its_backup);
    RUN(a_restore_refused_or_failing);
    RUN(a_restore_left_half_done_takes_its_backup_again);
    RUN(a_frame_the_guard_refuses_inside_a_write);
    RUN(a_page_that_moved_during_a_write_that_never_programmed);
    RUN(a_whole_write_across_the_wrap);
    RUN(the_waits_of_an_erase_and_a_program);
    return test_summary("ew_session_write");
}
