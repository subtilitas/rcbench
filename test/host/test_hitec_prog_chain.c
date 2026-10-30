/*
 * The Hitec programmer's chain of custody (hitec_prog.c read_done(),
 * hitec_prog_file.c hpf_read_done()) on the whole loopback of
 * hitec_prog_rig.h: a D series read is of the servo read before it while
 * every writable register is as that read found it, or, after a write left
 * unverified, while the record's verdict is SAME, DONE or MIX.  Otherwise
 * the read is another servo's: it becomes the session's backup, its first
 * write saves a backup file of its own, and the message says so.
 *
 *  - a swap mid-session to a D485HW with other values, and to one with the
 *    values of the last read (not told apart);
 *  - a verified write, the ID among its values, keeps the chain;
 *  - a write left unverified: the verdicts SAME, DONE and MIX keep the
 *    backup, OTHER replaces it; a discarded record leaves the values to
 *    decide;
 *  - LEAVE and CONNECT, and a lost link: a new session, a new backup,
 *    no message of another servo;
 *  - a classic servo of another serial says the same message.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_card.h"

/* ---------------------------------------------------------------- helpers */

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

/* SPEED 50% written to the servo and verified: register 0x54 = 10, the
 * backup file HITEC001 saved first. */
static void speed_written(void)
{
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

/* SPEED 50% (and SOFT START 60% with @p two) written to a servo that takes
 * nothing: the check fails, the record stands, READ ALL is owed. */
static void write_unverified(bool two)
{
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    if (two) {
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
                 HITEC_PROG_EDIT_OK);
    }
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(hitec_prog_record(model()));
    g_drv.d_drop = false;
}

/* READ ALL to its end. */
static void read_all(void)
{
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
}

/* Register @p reg of the session's backup. */
static unsigned bk_reg(uint8_t reg)
{
    hitec_image_t img;

    CHECK(hitec_prog_backup_image(model(), &img));
    return hitec_image_reg(&img, reg);
}

/* The session's backup is the servo read, no file holds it, and the next
 * write saves one: RESTORE of the backup is not offered. */
static void backup_is_this_read(void)
{
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK(hitec_prog_backup_due(model()));
    CHECK(!hitec_prog_backup_differs(model()));
}

/* The backup still holds the first servo as found: register 0x54 = 4095,
 * its file HITEC001. */
static void backup_kept(void)
{
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK(!hitec_prog_backup_due(model()));
    CHECK_EQ(bk_reg(0x54), 4095u);
}

/* Another D485HW on the port, as found: the fixture with dead band 4. */
static void swap_to_b(void)
{
    servo_d485();
    g_drv.dreg[0x4E] = 4u;
}

/* After a lost link: back, RELEASE PORT, a switch to ARMED and back to
 * SAFE, CONNECT and the read. */
static void back_and_connect(void)
{
    prun(200u);
    link_back();
    until(HITEC_PROG_OTHER);
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK(arm());
    disarm();
    prun(50u);
    connected();
}

/* ------------------------------------------------------------- the swaps */

TEST_CASE(a_swap_to_a_servo_with_other_values_is_another_servo)
{
    hitec_image_t img;

    d_ready();
    speed_written();
    CHECK(hitec_prog_write_on(model()));
    /* B on the port: 0x54 = 4095 and dead band 4, A's last read 10 and 1. */
    swap_to_b();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    CHECK_EQ(hitec_prog_message(model())->level, HITEC_PROG_WARN);
    backup_is_this_read();
    CHECK_EQ(bk_reg(0x4E), 4u);
    CHECK(!hitec_prog_write_on(model()));
    /* READ ALL again: the same servo now. */
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    backup_is_this_read();
    /* B's first write saves B as found to a file of its own. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC002");
    CHECK(files_image("HITEC002.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
    CHECK_EQ(hitec_image_reg(&img, 0x4E), 4u);
    CHECK(files_image("HITEC001.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x4E), 1u);
    /* RESTORE of the backup gives B back its own value. */
    CHECK_EQ(bk_reg(0x54), 4095u);
}

TEST_CASE(a_swap_with_one_value_changed_breaks_the_chain)
{
    /* The least change: one register, one count, no write before. */
    d_ready();
    CHECK_EQ(g_drv.dreg[0x4E], 1u);
    g_drv.dreg[0x4E] = 2u;
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    CHECK_EQ(bk_reg(0x4E), 2u);
    CHECK(!hitec_prog_backup_differs(model()));
}

TEST_CASE(a_swap_with_the_values_of_the_last_read_is_not_told_apart)
{
    d_ready();
    speed_written();
    /* B holds what A's last read found, register for register. */
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    backup_kept();
    CHECK(hitec_prog_backup_differs(model()));
    /* B as found equal to A as found, A not written: the backup is B's
     * values as well. */
    d_ready();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK(!hitec_prog_backup_differs(model()));
}

TEST_CASE(a_verified_write_of_the_id_keeps_the_chain)
{
    d_ready();
    CHECK_EQ(g_drv.dreg[0x32], 0u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_ID, 5), HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x32], 5u);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_message(model())->reg, 5u);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK_EQ(bk_reg(0x32), 0u);
    CHECK(hitec_prog_backup_differs(model()));
    CHECK(hitec_prog_write_on(model()));
    /* A second write, verified: still the same servo. */
    speed_written();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC001");
    CHECK_EQ(bk_reg(0x54), 4095u);
}

/* ------------------------------------------------ a write left unverified */

TEST_CASE(an_unverified_write_and_the_old_values_keep_the_backup)
{
    d_ready();
    write_unverified(false);
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_SAME);
    backup_kept();
}

TEST_CASE(an_unverified_write_and_the_new_values_keep_the_backup)
{
    d_ready();
    write_unverified(false);
    /* The servo took it after all; the read-back did not see it. */
    g_drv.dreg[0x54] = 10u;
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_DONE);
    backup_kept();
    CHECK(hitec_prog_backup_differs(model()));
}

TEST_CASE(an_unverified_write_and_a_mix_keep_the_backup)
{
    d_ready();
    write_unverified(true);
    g_drv.dreg[0x54] = 10u;
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_MIX);
    backup_kept();
}

TEST_CASE(an_unverified_write_and_another_servo_replace_the_backup)
{
    d_ready();
    write_unverified(false);
    swap_to_b();
    read_all();
    /* The record's line stays: it says the more. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_OTHER);
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_OTHER);
    backup_is_this_read();
    CHECK_EQ(bk_reg(0x4E), 4u);
    /* The record discarded, a write saves B's own backup file. */
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "HITEC002");
}

TEST_CASE(a_discarded_record_leaves_the_values_to_decide)
{
    /* The servo as the failed check read it back: the same servo. */
    d_ready();
    write_unverified(false);
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    backup_kept();
    /* Another servo after a discarded record: its own backup. */
    d_ready();
    write_unverified(false);
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    swap_to_b();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    backup_is_this_read();
}

TEST_CASE(a_record_compared_once_leaves_the_next_read_to_the_values)
{
    /* READ ALL after the failed check clears the debt; the record stays
     * until it is restored or discarded. */
    d_ready();
    write_unverified(false);
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_SAME);
    swap_to_b();
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_OTHER);
    backup_is_this_read();
}

/* ------------------------------------------------------ the session ends */

TEST_CASE(leave_and_connect_start_a_new_backup_without_the_message)
{
    d_ready();
    speed_written();
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    swap_to_b();
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    backup_is_this_read();
    CHECK_EQ(bk_reg(0x4E), 4u);
    /* Its next read is chained to this one. */
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    backup_is_this_read();
}

TEST_CASE(a_lost_link_starts_a_new_session_for_the_servo_found)
{
    hitec_image_t img;

    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    for (unsigned i = 0; i < 60000u && g_drv.d_saves == 0u; ++i) {
        pms();
    }
    link_silent();
    prun(400u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    swap_to_b();
    back_and_connect();
    /* The first read of a session: the backup, the record's line. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_OTHER);
    backup_is_this_read();
    CHECK_EQ(bk_reg(0x4E), 4u);
    /* The same servo again within the session. */
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_OTHER);
    backup_is_this_read();
    /* The old session's file holds A as found. */
    CHECK(files_image("HITEC001.TXT", &img));
    CHECK_EQ(hitec_image_reg(&img, 0x4E), 1u);
}

/* ---------------------------------------------------------------- classic */

TEST_CASE(a_classic_servo_of_another_serial_says_so)
{
    rig();
    servo_hs5645();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    g_drv.eeprom[HITEC_C_SERIAL] = (uint8_t)'9';
    read_all();
    CHECK_EQ(msg(), HITEC_PROG_MSG_OTHER_SERVO);
    CHECK(!hitec_prog_backup_differs(model()));
}

/* ------------------------------------------------------------- the panel */

TEST_CASE(the_message_has_two_lines_in_both_languages)
{
    const hitec_prog_msg_t m = { .id = HITEC_PROG_MSG_OTHER_SERVO };
    char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

    CHECK_EQ(hitec_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[0], "Other values than the last read: taken as another "
                       "servo.");
    CHECK_STR_EQ(t[1], "Its backup is this read.");
    ui_text_set_language(UI_LANG_DE);
    CHECK_EQ(hitec_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[1], "Sein Backup sind die jetzt ausgelesenen Werte.");
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_swap_to_a_servo_with_other_values_is_another_servo);
    RUN(a_swap_with_one_value_changed_breaks_the_chain);
    RUN(a_swap_with_the_values_of_the_last_read_is_not_told_apart);
    RUN(a_verified_write_of_the_id_keeps_the_chain);
    RUN(an_unverified_write_and_the_old_values_keep_the_backup);
    RUN(an_unverified_write_and_the_new_values_keep_the_backup);
    RUN(an_unverified_write_and_a_mix_keep_the_backup);
    RUN(an_unverified_write_and_another_servo_replace_the_backup);
    RUN(a_discarded_record_leaves_the_values_to_decide);
    RUN(a_record_compared_once_leaves_the_next_read_to_the_values);
    RUN(leave_and_connect_start_a_new_backup_without_the_message);
    RUN(a_lost_link_starts_a_new_session_for_the_servo_found);
    RUN(a_classic_servo_of_another_serial_says_so);
    RUN(the_message_has_two_lines_in_both_languages);
    return test_summary("hitec_prog_chain");
}
