/*
 * The unit the Hitec programmer's session backup is of (shared/bench/
 * hitec_prog_unit.c, the RESTORE gate in hitec_prog_write.c) on the whole
 * loopback of hitec_prog_rig.h:
 *
 *  - a second unit of one model on the port without LEAVE or CONNECT: a
 *    classic servo of another serial and a D485HW of another ID end the
 *    backup, and the read is the new unit's backup;
 *  - the same unit read again keeps its backup;
 *  - RESTORE of the session's backup goes for a classic servo whose read
 *    and backup hold one serial; for a D series servo and a classic servo
 *    with no serial it is refused, with nothing sent, until the operator
 *    confirms the unit (hitec_prog_restore_backup_unit()), and the second
 *    read still runs;
 *  - the backup's age across the 32-bit millisecond wrap;
 *  - the serial's limits: 7 ASCII digits '0' to '9', each cell read;
 *  - a swap with no read after it: RESTORE's second read reads the serial
 *    again and ends the write before a step;
 *  - the abort paths: a READ ALL aborted after a swap, ABORT and STOP
 *    during RESTORE's second read.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos these tests model.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_card.h"
#include "hitec_panel.h"
#include "ui_text.h"

/* ---------------------------------------------------------------- helpers */

/* An HS-5645MG read, its serial @p serial (7 cells; NULL: the fixture's),
 * the switch on, a card mounted. */
static void c_ready(const uint8_t *serial)
{
    rig();
    servo_hs5645();
    for (unsigned c = 0; serial != NULL && c < HITEC_C_SERIAL_LEN; ++c) {
        g_drv.eeprom[HITEC_C_SERIAL + c] = serial[c];
    }
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    g_card = true;
    prun(20u);
}

/* A D485HW read, the switch on, a card mounted. */
static void d_ready(void)
{
    rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    g_card = true;
    prun(20u);
}

/* The write runs to its end: held, nothing running. */
static void written(void)
{
    reread();
    for (unsigned i = 0; i < 60000u
                         && (pstate() == HITEC_PROG_BACKUP
                             || pstate() == HITEC_PROG_RECORDING
                             || pstate() == HITEC_PROG_WRITING);
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* READ ALL to its end. */
static void read_all(void)
{
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
}

/* Classic SPEED 50% written and verified: cell 0x06 changes. */
static void c_speed_written(void)
{
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK(g_drv.eeprom[0x06] != fix_hs5645()[0x06]);
}

/* D series SPEED 50% written and verified: register 0x54 = 10. */
static void d_speed_written(void)
{
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

/* The serial cells 0x80 to 0x86 on the port. */
static void put_serial(const char *s)
{
    for (unsigned c = 0; c < HITEC_C_SERIAL_LEN; ++c) {
        g_drv.eeprom[HITEC_C_SERIAL + c] = (uint8_t)s[c];
    }
}

/* RESTORE of the session's backup without the unit confirmed: refused,
 * nothing sent, the switch and the backup as they were. */
static void restore_needs_the_unit(void)
{
    const unsigned c_writes = g_drv.c_writes;
    const unsigned d_writes = g_drv.d_writes;
    const unsigned saves = g_files_saves;
    const bool on = hitec_prog_write_on(model());

    CHECK(hitec_prog_backup_differs(model()));
    CHECK(!hitec_prog_unit_told_apart(model()));
    CHECK_EQ(hitec_prog_write_gate(model(), (uint8_t)HITEC_PROG_WR_BACKUP),
             HITEC_PROG_MSG_BACKUP_UNIT);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_BACKUP_UNIT);
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_UNIT);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_WARN);
    prun(200u);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(g_drv.c_writes, c_writes);
    CHECK_EQ(g_drv.d_writes, d_writes);
    CHECK_EQ(g_files_saves, saves);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(hitec_prog_write_on(model()), on);
    CHECK(hitec_prog_backup_differs(model()));
    /* The confirmation is for one call: none is left over. */
    CHECK(!model()->bk_unit_ok);
}

/* RESTORE of the session's backup with the unit confirmed: written and
 * verified. */
static void restore_with_the_unit(void)
{
    CHECK_EQ(hitec_prog_restore_backup_unit(model()), HITEC_PROG_MSG_NONE);
    CHECK(!model()->bk_unit_ok);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK(!hitec_prog_backup_differs(model()));
}

/* ------------------------------------------------------- a second unit */

TEST_CASE(a_second_classic_unit_of_another_serial_takes_its_own_backup)
{
    hitec_image_t img;

    c_ready(NULL);
    c_speed_written();
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    /* B: the same model and date, the values A's last read found, another
     * serial. */
    put_serial("0001516");
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(!hitec_prog_backup_differs(model()));
    CHECK(hitec_prog_unit_told_apart(model()));
    CHECK(hitec_prog_backup_image(model(), &img));
    CHECK_EQ(hitec_image_cell(&img, HITEC_C_SERIAL + 6u), (uint8_t)'6');
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NO_CHANGES);
    /* B written: its first write saves B's own backup file, and RESTORE
     * brings B back as found, not A. */
    const uint8_t b06 = g_drv.eeprom[0x06];
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC002");
    CHECK(g_drv.eeprom[0x06] != b06);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK_EQ(g_drv.eeprom[0x06], b06);
    CHECK(g_drv.eeprom[0x06] != fix_hs5645()[0x06]);
}

TEST_CASE(a_second_d_series_unit_of_another_id_takes_its_own_backup)
{
    hitec_image_t img;

    d_ready();
    d_speed_written();
    /* B: A's last read but the ID. */
    g_drv.dreg[0x32] = 7u;
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_image(model(), &img));
    CHECK_EQ(hitec_image_reg(&img, 0x32), 7u);
    CHECK_EQ(hitec_image_reg(&img, 0x54), 10u);
    CHECK(!hitec_prog_backup_differs(model()));
    /* B's first write saves B's file; RESTORE is refused all the same. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC002");
    CHECK(files_image("HITEC002.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x32), 7u);
    CHECK_EQ(g_drv.dreg[0x54], 6u);
    /* Confirmed, RESTORE brings B back as found, not A. */
    restore_needs_the_unit();
    restore_with_the_unit();
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    CHECK_EQ(g_drv.dreg[0x32], 7u);
}

TEST_CASE(a_second_d_series_unit_of_another_id_breaks_an_unverified_write)
{
    d_ready();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    g_drv.d_drop = false;
    /* B holds the values before the write, the record's SAME, but its ID
     * is neither the one before nor the one the write left. */
    g_drv.dreg[0x32] = 3u;
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_OTHER);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_due(model()));
}

/* ------------------------------------------------------- the same unit */

TEST_CASE(the_same_classic_unit_keeps_its_backup_and_restore_goes)
{
    unsigned done = 0;
    unsigned total = 0;

    c_ready(NULL);
    c_speed_written();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(hitec_prog_backup_differs(model()));
    CHECK(hitec_prog_unit_told_apart(model()));
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    /* The second read: cells 0x00 to 0x23 and the serial 0x80 to 0x86. */
    CHECK(hitec_prog_rereading(model()));
    CHECK(hitec_prog_progress(model(), &done, &total));
    CHECK_EQ(total, HITEC_C_BLOCK_LEN + HITEC_C_SERIAL_LEN);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK_EQ(g_drv.eeprom[0x06], fix_hs5645()[0x06]);
    CHECK(!hitec_prog_backup_differs(model()));
}

TEST_CASE(the_same_d_series_unit_keeps_its_backup_and_restore_asks_for_it)
{
    hitec_image_t img;

    d_ready();
    d_speed_written();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(hitec_prog_backup_image(model(), &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
    restore_needs_the_unit();
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    /* With the switch off the switch comes first, confirmed or not. */
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    const unsigned writes = g_drv.d_writes;
    CHECK_EQ(hitec_prog_write_gate(model(), (uint8_t)HITEC_PROG_WR_BACKUP),
             HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_restore_backup_unit(model()),
             HITEC_PROG_MSG_WRITE_OFF);
    CHECK(!model()->bk_unit_ok);
    /* A confirmed RESTORE refused by another gate leaves no confirmation:
     * an edit standing. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_restore_backup_unit(model()),
             HITEC_PROG_MSG_EDITS_PENDING);
    hitec_prog_discard(model());
    CHECK_EQ(g_drv.d_writes, writes);
    restore_needs_the_unit();
    /* Confirmed: the values of the session's start. */
    restore_with_the_unit();
    CHECK_EQ(g_drv.dreg[0x54], 4095u);
}

/* ---------------------------------------------------- no serial to read */

TEST_CASE(a_classic_unit_with_no_serial_keeps_its_backup_and_restore_asks_for_it)
{
    static const uint8_t k_blank[HITEC_C_SERIAL_LEN] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    c_ready(k_blank);
    c_speed_written();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    restore_needs_the_unit();
    CHECK(g_drv.eeprom[0x06] != fix_hs5645()[0x06]);
    restore_with_the_unit();
    CHECK_EQ(g_drv.eeprom[0x06], fix_hs5645()[0x06]);
}

TEST_CASE(a_second_classic_unit_with_no_serial_gets_nothing_of_the_first)
{
    static const uint8_t k_blank[HITEC_C_SERIAL_LEN] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    /* A written, then B with A's last values and no serial either: the
     * read does not tell them apart, and RESTORE sends nothing. */
    c_ready(k_blank);
    c_speed_written();
    const uint8_t written06 = g_drv.eeprom[0x06];
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    restore_needs_the_unit();
    CHECK_EQ(g_drv.eeprom[0x06], written06);
}

TEST_CASE(a_serial_is_7_ascii_digits_each_read)
{
    static const struct {
        const char *serial;
        bool        told;
    } k_case[] = {
        { "0000000", true },
        { "9999999", true },
        { "0825798", true },
        { "/000000", false },   /* '0' - 1 */
        { "000000:", false },   /* '9' + 1 */
        { "000 000", false },
        { "\0\0\0\0\0\0\0", false },
    };

    for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        c_ready((const uint8_t *)k_case[k].serial);
        CHECK_EQ(hitec_prog_unit_told_apart(model()), k_case[k].told);
    }
    /* A serial cell not read, in the read or in the backup. */
    c_ready(NULL);
    CHECK(hitec_prog_unit_told_apart(model()));
    model()->servo.known[(HITEC_C_SERIAL + 6u) / 8u] &=
        (uint8_t)~(1u << ((HITEC_C_SERIAL + 6u) % 8u));
    CHECK(!hitec_prog_unit_told_apart(model()));
    c_ready(NULL);
    model()->bk.known[HITEC_C_SERIAL / 8u] &=
        (uint8_t)~(1u << (HITEC_C_SERIAL % 8u));
    CHECK(!hitec_prog_unit_told_apart(model()));
    /* The backup's serial against the read's, cell by cell. */
    c_ready(NULL);
    (void)hitec_image_put_cell(&model()->bk, HITEC_C_SERIAL + 6u, '9');
    CHECK(!hitec_prog_unit_told_apart(model()));
    (void)hitec_image_put_cell(&model()->bk, HITEC_C_SERIAL + 6u,
                               hitec_image_cell(&model()->servo,
                                                HITEC_C_SERIAL + 6u));
    CHECK(hitec_prog_unit_told_apart(model()));
    /* Cell 0x87, past the 7 digits, is no part of the serial. */
    (void)hitec_image_put_cell(&model()->bk, HITEC_C_SERIAL + 7u, 'X');
    CHECK(hitec_image_cell(&model()->servo, HITEC_C_SERIAL + 7u) != 'X');
    CHECK(hitec_prog_unit_told_apart(model()));
    /* No backup, another series. */
    model()->have_bk = false;
    CHECK(!hitec_prog_unit_told_apart(model()));
    model()->have_bk = true;
    model()->bk.series = (uint8_t)HITEC_D;
    CHECK(!hitec_prog_unit_told_apart(model()));
    model()->bk.series = (uint8_t)HITEC_CLASSIC;
    model()->have_servo = false;
    CHECK(!hitec_prog_unit_told_apart(model()));
    d_ready();
    CHECK(!hitec_prog_unit_told_apart(model()));
}

/* ------------------------------------------------------- the abort paths */

TEST_CASE(an_aborted_read_after_a_swap_leaves_nothing_to_restore_from)
{
    unsigned done = 0;
    unsigned total = 0;

    c_ready(NULL);
    c_speed_written();
    /* B on the port with A's last values, another serial; its READ ALL
     * aborted part way: the backup stays A's, and no write plans from a
     * read cut short. */
    put_serial("0001516");
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    for (unsigned i = 0; i < 60000u
                         && (!hitec_prog_progress(model(), &done, &total)
                             || done < 10u);
         ++i) {
        pms();
    }
    hitec_prog_abort(model());
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    const unsigned writes = g_drv.c_writes;
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_MUST_READ);
    prun(200u);
    CHECK_EQ(g_drv.c_writes, writes);
    CHECK(!hitec_prog_record(model()));
    /* READ ALL: B is its own backup, and RESTORE has nothing to bring. */
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NO_CHANGES);
}

TEST_CASE(a_swap_with_no_read_after_it_ends_restore_at_the_second_read)
{
    c_ready(NULL);
    c_speed_written();
    /* B on the port with A's last values, another serial, and no READ ALL:
     * the gate sees A's read; the second read finds B's serial. */
    put_serial("0001516");
    CHECK(hitec_prog_unit_told_apart(model()));
    const unsigned writes = g_drv.c_writes;
    const unsigned saves = g_files_saves;
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READS_DIFFER);
    CHECK_EQ(hitec_prog_message(model())->arg, HITEC_C_SERIAL + 6u);
    CHECK_EQ(g_drv.c_writes, writes);
    CHECK_EQ(g_files_saves, saves);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(model()->ses.plan.n, 0u);
    /* The first serial cell as well. */
    c_ready(NULL);
    c_speed_written();
    g_drv.eeprom[HITEC_C_SERIAL] = (uint8_t)'9';
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READS_DIFFER);
    CHECK_EQ(hitec_prog_message(model())->arg, HITEC_C_SERIAL);
}

TEST_CASE(abort_and_stop_during_restores_second_read_send_nothing)
{
    for (unsigned k = 0; k < 2u; ++k) {
        unsigned done = 0;
        unsigned total = 0;

        c_ready(NULL);
        c_speed_written();
        const unsigned writes = g_drv.c_writes;
        const uint8_t written06 = g_drv.eeprom[0x06];
        CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
        for (unsigned i = 0; i < 60000u
                             && (!hitec_prog_progress(model(), &done, &total)
                                 || done < 4u);
             ++i) {
            pms();
        }
        CHECK(hitec_prog_rereading(model()));
        if (k == 0u) {
            hitec_prog_abort(model());
        } else {
            press_stop();
        }
        for (unsigned i = 0; i < 60000u && hitec_prog_rereading(model());
             ++i) {
            pms();
        }
        prun(50u);
        CHECK_EQ(msg(), k == 0u ? HITEC_PROG_MSG_ABORTED
                                : HITEC_PROG_MSG_STOPPED);
        CHECK_EQ(g_drv.c_writes, writes);
        CHECK_EQ(g_drv.eeprom[0x06], written06);
        CHECK(!hitec_prog_record(model()));
        CHECK_EQ(model()->ses.plan.n, 0u);
        CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
        CHECK(hitec_prog_backup_differs(model()));
        CHECK(!hitec_prog_write_on(model()) || k == 0u);
    }
    /* After the ABORT the backup still goes back. */
    c_ready(NULL);
    c_speed_written();
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    hitec_prog_abort(model());
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BACKUP_RESTORED);
    CHECK_EQ(g_drv.eeprom[0x06], fix_hs5645()[0x06]);
}

TEST_CASE(a_confirmed_d_series_restore_still_reads_the_servo_again)
{
    /* B, with another ID, on the port after the last READ ALL: the
     * operator's confirmation does not pass the second read. */
    d_ready();
    d_speed_written();
    g_drv.dreg[0x32] = 9u;
    const unsigned writes = g_drv.d_writes;
    CHECK_EQ(hitec_prog_restore_backup_unit(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READS_DIFFER);
    CHECK_EQ(hitec_prog_message(model())->arg, 0x32u);
    CHECK_EQ(g_drv.d_writes, writes);
    CHECK(!hitec_prog_record(model()));
}

TEST_CASE(the_backup_age_counts_across_the_clock_wrap)
{
    d_ready();
    CHECK(hitec_prog_backup_age_ms(model()) < 1000u);
    model()->bk_ms = 0xFFFFFF00u;
    model()->f.now_ms = 0x00000100u;
    CHECK_EQ(hitec_prog_backup_age_ms(model()), 0x200u);
    model()->have_bk = false;
    CHECK_EQ(hitec_prog_backup_age_ms(model()), 0u);
}

TEST_CASE(the_session_end_takes_the_refusal_with_it)
{
    d_ready();
    d_speed_written();
    CHECK_EQ(hitec_prog_restore_backup(model()), HITEC_PROG_MSG_BACKUP_UNIT);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK(msg() != HITEC_PROG_MSG_BACKUP_UNIT);
    CHECK(!hitec_prog_unit_told_apart(model()));
}

/* ------------------------------------------------------------- the panel */

TEST_CASE(the_refusal_has_two_lines_in_both_languages)
{
    const hitec_prog_msg_t m = { .id = HITEC_PROG_MSG_BACKUP_UNIT };
    char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

    CHECK_EQ(hitec_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[0], "RESTORE not started: the servo is not confirmed as "
                       "the backup's servo.");
    CHECK_STR_EQ(t[1], "Confirm it with the 2s hold on the RESTORE page.");
    ui_text_set_language(UI_LANG_DE);
    CHECK_EQ(hitec_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[1], "Bestätige es mit 2s Halten auf der Seite "
                       "ZURÜCKSPIELEN.");
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_second_classic_unit_of_another_serial_takes_its_own_backup);
    RUN(a_second_d_series_unit_of_another_id_takes_its_own_backup);
    RUN(a_second_d_series_unit_of_another_id_breaks_an_unverified_write);
    RUN(the_same_classic_unit_keeps_its_backup_and_restore_goes);
    RUN(the_same_d_series_unit_keeps_its_backup_and_restore_asks_for_it);
    RUN(a_classic_unit_with_no_serial_keeps_its_backup_and_restore_asks_for_it);
    RUN(a_second_classic_unit_with_no_serial_gets_nothing_of_the_first);
    RUN(a_serial_is_7_ascii_digits_each_read);
    RUN(an_aborted_read_after_a_swap_leaves_nothing_to_restore_from);
    RUN(a_swap_with_no_read_after_it_ends_restore_at_the_second_read);
    RUN(abort_and_stop_during_restores_second_read_send_nothing);
    RUN(a_confirmed_d_series_restore_still_reads_the_servo_again);
    RUN(the_backup_age_counts_across_the_clock_wrap);
    RUN(the_session_end_takes_the_refusal_with_it);
    RUN(the_refusal_has_two_lines_in_both_languages);
    return test_summary("hitec_prog_unit");
}
