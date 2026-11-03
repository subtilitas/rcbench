/*
 * The Hitec programmer's writes (shared/bench/hitec_prog_write.c) on the
 * whole loopback of hitec_prog_rig.h:
 *
 *  - a D series write: the write, the save, the reboot and the read-back,
 *    the record on the card and cleared after;
 *  - a D series write the servo does not take: the read-back fails, the
 *    switch goes off, READ ALL compares the servo with the record;
 *  - a link lost before the save, after the save and before the reboot,
 *    and in the read-back: the record stands, READ ALL comes first, the
 *    next CONNECT compares, RESTORE writes the old values back;
 *  - a classic write: the cell, its balance, the read-back of 0x00 to 0x23;
 *    a link lost between the cell and the balance names the cell, and the
 *    next read shows the checksum WRONG;
 *  - REPAIR of a classic checksum;
 *  - the record across a panel restart, a card that refuses it and one
 *    that does not answer in 3000ms, ABORT while the write waits for it;
 *  - the record of another servo; FORGET;
 *  - a write refused ARMED, STOPPED, with STOP latched, without a read,
 *    with the switch off, without a change;
 *  - a write across the millisecond clock's wrap;
 *  - the edge paths: no servo or no entry, a raw value's confirmation, a
 *    confirmation after a stop, an arm request or a refused plan while the
 *    card takes the record, an empty RESTORE, ABORT before the first step,
 *    the record loaded;
 *  - the card's answer to a text of before the write that waits (an
 *    aborted write's record, a record's clear, a refusal, no answer within
 *    3000ms) releases and ends nothing;
 *  - the gate taken again at the card's answer: the switch off, a STOP or
 *    ARM pressed while the record goes to the card writes nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_card.h"

/* ---------------------------------------------------------------- helpers */

/* A D485HW read, the switch on. */
static void d_ready(void)
{
    rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
}

/* An HS-5645MG read, the switch on. */
static void c_ready(void)
{
    rig();
    servo_hs5645();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
}

/* The write runs to its end: held, nothing running. */
static void written(void)
{
    reread();
    for (unsigned i = 0; i < 60000u
                         && (pstate() == HITEC_PROG_RECORDING
                             || pstate() == HITEC_PROG_WRITING);
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* Run the write until the plan's step out is of @p kind (0xFF: the
 * read-back). */
static void run_to(uint8_t kind)
{
    unsigned d = 0, t = 0;
    uint8_t k = 0;

    for (unsigned i = 0; i < 60000u; ++i) {
        if (hitec_prog_write_progress(model(), &d, &t, &k) && k == kind) {
            return;
        }
        pms();
    }
    T_FAIL("the write never reached step kind %u", (unsigned)kind);
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

/* The record's text on the card, parsed. */
static hitec_card_result_t card_record(hitec_card_record_t *r)
{
    return hitec_card_parse(g_card_text, g_card_len, r);
}

/* ----------------------------------------------------------------- D series */

TEST_CASE(a_d_write_writes_saves_reboots_reads_back_and_clears_the_record)
{
    hitec_card_record_t r;

    d_ready();
    card_in();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    /* The record goes to the card first. */
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK_EQ(g_card_writes, 0u);
    frame();
    CHECK_EQ(g_card_writes, 1u);
    CHECK_EQ(card_record(&r), HITEC_CARD_OK);
    CHECK_EQ(r.series, HITEC_D);
    CHECK_EQ(r.port, CH);
    CHECK_EQ(r.pin, PIN);
    CHECK_EQ(hitec_image_reg(&r.before, 0x54), 4095u);
    CHECK_EQ(hitec_image_reg(&r.target, 0x54), 10u);
    CHECK(hitec_prog_record(model()));
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(hitec_prog_message(model())->arg, 1u);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    CHECK_EQ(g_drv.d_writes, 1u);
    CHECK_EQ(g_drv.d_saves, 1u);
    CHECK_EQ(g_drv.d_reboots, 1u);
    CHECK_EQ(hitec_image_reg(hitec_prog_servo(model()), 0x54), 10u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK(hitec_prog_first_done(model()));
    /* Verified: the record is cleared, on the card as well; the switch
     * stays on. */
    CHECK(!hitec_prog_record(model()));
    frame();
    CHECK_EQ(g_card_writes, 2u);
    CHECK_EQ(card_record(&r), HITEC_CARD_NONE);
    CHECK(hitec_prog_write_on(model()));
}

TEST_CASE(a_d_write_the_servo_does_not_take_fails_its_check)
{
    d_ready();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    CHECK_EQ(hitec_prog_message(model())->arg, 1u);
    CHECK(!hitec_prog_write_on(model()));
    CHECK(hitec_prog_record(model()));
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_EDITS),
             HITEC_PROG_MSG_MUST_READ);
    /* READ ALL: the servo holds the values before the write. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_SAME);
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_SAME);
    /* A write waits until the record is restored or discarded. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_RECORD_HELD);
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    CHECK(!hitec_prog_record(model()));
    g_drv.d_drop = false;
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
}

TEST_CASE(a_link_lost_before_the_save_leaves_the_record_and_read_all_first)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    run_to(HITEC_STEP_D_SAVE);
    link_silent();
    prun(400u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    CHECK_EQ(hitec_prog_message(model())->reg, HITEC_STEP_D_SAVE);
    CHECK_EQ(hitec_prog_message(model())->arg, HITEC_D_REG_SAVE);
    CHECK_EQ(g_drv.d_writes, 1u);
    CHECK_EQ(g_drv.d_saves, 0u);
    CHECK(hitec_prog_record(model()));
    CHECK(!hitec_prog_write_on(model()));
    back_and_connect();
    /* The servo took the register and kept it: the new value. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_DONE);
    CHECK_EQ(hitec_prog_message(model())->arg, PIN);
    CHECK_EQ(hitec_prog_message(model())->reg, CH);
}

TEST_CASE(a_link_lost_after_the_save_before_the_reboot_then_restore)
{
    hitec_image_t img;

    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    run_to(HITEC_STEP_D_REBOOT);
    link_silent();
    prun(400u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    CHECK_EQ(hitec_prog_message(model())->reg, HITEC_STEP_D_REBOOT);
    CHECK_EQ(g_drv.d_saves, 1u);
    CHECK_EQ(g_drv.d_reboots, 0u);
    back_and_connect();
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_DONE);
    /* RESTORE: the value before the write, with the switch. */
    CHECK(hitec_prog_restore_image(model(), &img));
    CHECK_EQ(hitec_image_reg(&img, 0x54), 4095u);
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RESTORED);
    CHECK_EQ(g_drv.dreg[0x54], 4095u);
    CHECK(!hitec_prog_record(model()));
}

TEST_CASE(a_link_lost_after_the_reboot_in_the_read_back)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    run_to(0xFFu);
    link_silent();
    /* The reboot's 1000ms pass before the read-back's first step. */
    prun(1200u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    CHECK_EQ(hitec_prog_message(model())->reg, HITEC_PROG_CUT_CHECK);
    CHECK_EQ(g_drv.d_reboots, 1u);
    CHECK(hitec_prog_record(model()));
    back_and_connect();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_DONE);
}

TEST_CASE(a_stop_during_a_write_cuts_it_and_keeps_the_record)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    run_to(HITEC_STEP_D_SAVE);
    press_stop();
    prun(300u);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    CHECK(hitec_prog_record(model()));
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_write_enable(model(), true),
             HITEC_PROG_MSG_STOP_LATCHED);
}

/* ------------------------------------------------------------------ classic */

TEST_CASE(a_classic_write_sends_the_cell_its_balance_and_reads_back)
{
    hitec_image_t img;

    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.c_writes, 2u);
    CHECK_EQ(g_drv.eeprom[0x06], 0x1Cu);
    hitec_image_init(&img, HITEC_CLASSIC);
    for (unsigned c = 0; c <= HITEC_C_SUM_LAST; ++c) {
        (void)hitec_image_put_cell(&img, (uint8_t)c, g_drv.eeprom[c]);
    }
    CHECK_EQ(hitec_c_eeprom_sum(&img), 0u);
    CHECK(!hitec_prog_sum_bad(model()));
}

TEST_CASE(a_classic_link_lost_between_cell_and_balance_names_the_cell)
{
    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    run_to(HITEC_STEP_C_SUM);
    link_silent();
    prun(400u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    CHECK_EQ(hitec_prog_message(model())->arg, 0x06u);
    CHECK_EQ(hitec_prog_message(model())->reg, HITEC_STEP_C_SUM);
    CHECK_EQ(g_drv.c_writes, 1u);
    back_and_connect();
    /* The cell holds the new value and the balance the old one. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_DONE);
    CHECK(hitec_prog_sum_bad(model()));
    /* REPAIR waits for the record; RESTORE puts both cells right. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_repair(model()), HITEC_PROG_MSG_RECORD_HELD);
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RESTORED);
    CHECK_EQ(g_drv.eeprom[0x06], 0x40u);
    CHECK(!hitec_prog_sum_bad(model()));
    CHECK(!hitec_prog_record(model()));
}

TEST_CASE(repair_writes_the_balance_alone)
{
    rig();
    servo_hs5645();
    g_drv.eeprom[HITEC_C_SUM_LAST] ^= 0x01u;
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK(hitec_prog_sum_bad(model()));
    CHECK_EQ(hitec_prog_repair(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    /* Not with an edit standing: WRITE n VALUES balances it too. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_repair(model()), HITEC_PROG_MSG_EDITS_PENDING);
    hitec_prog_discard(model());
    CHECK_EQ(hitec_prog_repair(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REPAIRED);
    CHECK_EQ(g_drv.c_writes, 1u);
    CHECK_EQ(g_drv.eeprom[0x06], 0x40u);
    CHECK(!hitec_prog_sum_bad(model()));
    /* A right sum has nothing to repair. */
    CHECK_EQ(hitec_prog_repair(model()), HITEC_PROG_MSG_SUM_OK);
}

TEST_CASE(a_classic_write_without_its_answer_names_the_cell)
{
    c_ready();
    g_drv.c_mute = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_CUT);
    CHECK_EQ(hitec_prog_message(model())->arg, 0x06u);
    CHECK_EQ(hitec_prog_message(model())->reg, HITEC_STEP_C_CELL);
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_EDITS),
             HITEC_PROG_MSG_MUST_READ);
    g_drv.c_mute = false;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_SAME);
}

/* ------------------------------------------------------------------- record */

TEST_CASE(the_record_stands_across_a_panel_restart)
{
    d_ready();
    card_in();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    run_to(HITEC_STEP_D_SAVE);
    panel_restart();
    frame();
    CHECK(hitec_prog_record(model()));
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_NONE);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD);
    CHECK_EQ(hitec_prog_message(model())->arg, PIN);
    until(HITEC_PROG_OTHER);
    /* No servo compared: no restore. */
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_HELD_OTHER);
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK(arm());
    disarm();
    prun(50u);
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_DONE);
}

TEST_CASE(a_card_that_refuses_the_record_keeps_the_write_from_going)
{
    d_ready();
    card_in();
    g_card_fails = true;
    prun(20u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD_FAILED);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(g_drv.d_saves, 0u);
    CHECK(!hitec_prog_record(model()));
    CHECK(hitec_prog_write_on(model()));
}

TEST_CASE(a_card_that_does_not_answer_in_3000ms_ends_the_write)
{
    uint32_t t0;

    d_ready();
    card_in();
    g_card_slow = true;
    prun(20u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    t0 = g_ms;
    while (g_ms - t0 < HITEC_PROG_CARD_MS - g_frame_ms) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    written();
    CHECK(g_ms - t0 >= HITEC_PROG_CARD_MS);
    CHECK(g_ms - t0 <= HITEC_PROG_CARD_MS + g_frame_ms);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD_FAILED);
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(abort_while_the_write_waits_for_the_card_writes_nothing)
{
    d_ready();
    card_in();
    g_card_slow = true;
    prun(20u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    prun(100u);
    hitec_prog_abort(model());
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK(!hitec_prog_record(model()));
    prun(4000u);
    CHECK_EQ(g_drv.d_writes, 0u);
    /* The edit stands for the next try. */
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

TEST_CASE(without_a_card_the_record_is_kept_in_memory_only)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_WRITING);
    CHECK(hitec_prog_record(model()));
    CHECK(hitec_prog_file_due(model()));
    written();
    CHECK_EQ(g_card_writes, 0u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
}

TEST_CASE(a_record_of_another_servo_is_not_restored_and_forget_ends_it)
{
    d_ready();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    /* A dead band the record knows neither way: another servo. */
    g_drv.dreg[0x4E] = 4u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_OTHER);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_REC_OTHER);
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    CHECK(!hitec_prog_record(model()));
    /* And a forget with none says nothing. */
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_NONE);
}

TEST_CASE(a_record_on_another_model_is_another_servo)
{
    d_ready();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    g_drv.dreg[0x00] = 34645u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_OTHER);
}

TEST_CASE(a_record_with_a_mix_of_old_and_new_values)
{
    d_ready();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_changes(model()), 2u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    CHECK_EQ(hitec_prog_message(model())->arg, 2u);
    /* One of the two took. */
    g_drv.dreg[0x54] = 10u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_MIX);
    g_drv.d_drop = false;
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RESTORED);
    CHECK_EQ(hitec_prog_message(model())->arg, 1u);
    CHECK_EQ(g_drv.dreg[0x54], 4095u);
}

/* ---------------------------------------------------------------- refusals */

TEST_CASE(a_write_is_refused_armed_stopped_unread_off_and_unchanged)
{
    unsigned starts;

    rig();
    servo_d485();
    /* No servo read: nothing held. */
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NOT_IN_MODE);
    connected();
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NO_CHANGES);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    starts = g_drv.starts;
    /* Asked to arm. */
    hitec_prog_bench_armed(model(), true);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NEED_DISARM);
    hitec_prog_bench_armed(model(), false);
    /* A switch to SAFE at the bench alone: the port STOPPED. */
    disarm();
    prun(100u);
    CHECK(hitec_prog_stopped(model()));
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_STOPPED);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(g_drv.starts, starts);
    /* STOP latched. */
    press_stop();
    prun(100u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_STOP_LATCHED);
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(a_write_without_a_read_is_refused)
{
    rig();
    servo_d485();
    connected();
    /* The read lost: no servo held. */
    model()->have_servo = false;
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NOT_READ);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NOT_READ);
    CHECK(hitec_prog_draft(model()) == NULL);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NOT_READ);
}

TEST_CASE(a_write_runs_across_the_clocks_wrap)
{
    rig_from(0xFFFFFFFFu - 2600u, SUPPLY_ON, false);
    servo_d485();
    connected();
    CHECK(g_ms > 0x80000000u);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK(g_ms < 0x10000000u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

TEST_CASE(rules_edits_and_writes_need_a_servo_in_programming_mode)
{
    rig();
    servo_d485();
    /* No servo read: no rule asks. */
    CHECK_EQ(hitec_prog_rules(model(), HITEC_P_D_SPEED, 10), 0u);
    connected();
    CHECK(hitec_prog_rules(model(), HITEC_P_D_SPEED, 10) != 0u);
    /* A parameter of the other series has no rule here. */
    CHECK_EQ(hitec_prog_rules(model(), HITEC_P_C_SPEED, 10), 0u);
    /* Held, the entry not done: nothing is edited or written. */
    model()->ses.in_mode = 0u;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_write_enable(model(), true),
             HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_EDITS),
             HITEC_PROG_MSG_NOT_IN_MODE);
}

TEST_CASE(a_confirmation_of_a_raw_value_holds_for_that_value_only)
{
    rig();
    servo_d485();
    g_drv.dreg[0x54] = 7u;   /* fits no speed */
    connected();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 20),
             HITEC_PROG_EDIT_CHECK);
    CHECK_EQ(hitec_prog_confirm(model()), HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 20),
             HITEC_PROG_EDIT_OK);
    /* No servo value to measure "nearer" from: 10% asks again. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 10),
             HITEC_PROG_EDIT_CHECK);
}

TEST_CASE(a_confirmation_after_the_port_stopped_is_refused)
{
    rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 20),
             HITEC_PROG_EDIT_CHECK);
    disarm();
    prun(100u);
    CHECK(hitec_prog_stopped(model()));
    CHECK_EQ(hitec_prog_confirm(model()), HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

TEST_CASE(restore_without_a_compared_record_asks_for_read_all)
{
    d_ready();
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_MUST_READ);
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_RESTORE),
             HITEC_PROG_MSG_MUST_READ);
}

TEST_CASE(an_arm_request_while_the_card_takes_the_record_writes_nothing)
{
    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    /* FORGET waits for the write. */
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_BUSY);
    hitec_prog_bench_armed(model(), true);
    g_card_slow = false;
    frame();
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NEED_DISARM);
    CHECK(!hitec_prog_record(model()));
    hitec_prog_bench_armed(model(), false);
    prun(100u);
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(a_restore_to_a_servo_with_the_old_values_sends_nothing)
{
    unsigned writes;

    d_ready();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_SAME);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    writes = g_drv.d_writes;
    /* The plan is empty: done at once, the record cleared. */
    CHECK_EQ(hitec_prog_restore(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RESTORED);
    CHECK(!hitec_prog_record(model()));
    prun(100u);
    CHECK_EQ(g_drv.d_writes, writes);
}

TEST_CASE(a_write_the_session_cannot_plan_sends_nothing)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    /* The servo's speed not known to the session: no plan. */
    CHECK(hitec_image_forget(&model()->ses.image, 0x54));
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_MUST_READ);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

TEST_CASE(a_write_the_session_refuses_after_the_card_sends_nothing)
{
    d_ready();
    card_in();
    /* A first write, so the session's count of steps sent is 1. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    /* Its plan refused when the card has taken the record. */
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK(hitec_image_forget(&model()->ses.image, 0x54));
    g_card_slow = false;
    frame();
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MUST_READ);
    CHECK(!hitec_prog_record(model()));
    CHECK(!hitec_prog_write_on(model()));
    /* The entry gone when the card has taken it. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 60),
             HITEC_PROG_EDIT_OK);
    g_card_slow = true;
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    model()->ses.in_mode = 0u;
    g_card_slow = false;
    frame();
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

TEST_CASE(abort_before_the_first_step_sends_nothing_and_drops_the_record)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_WRITING);
    CHECK(hitec_prog_record(model()));
    hitec_prog_abort(model());
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK(!hitec_prog_record(model()));
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(g_drv.d_writes, 0u);
    /* Nothing reached the servo: no READ ALL owed. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
}

TEST_CASE(the_record_loaded_from_the_card)
{
    hitec_card_record_t r;
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    d_ready();
    /* A text with no record, and one that is not a record. */
    len = hitec_card_text(NULL, text, sizeof(text));
    CHECK(hitec_prog_file_load(model(), text, len));
    CHECK(!hitec_prog_record(model()));
    CHECK(!hitec_prog_file_load(model(), "RCBENCH HITEC 9\n", 16u));
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD_BAD);
    CHECK(!hitec_prog_record(model()));
    /* No text taken: a card's answer changes nothing. */
    hitec_prog_file_done(model(), true);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    /* A record of this servo, loaded with it read: compared at once. */
    r.series = HITEC_D;
    r.port = CH;
    r.pin = PIN;
    r.before = *hitec_prog_servo(model());
    r.target = r.before;
    (void)hitec_image_put_reg(&r.target, 0x54, 10u);
    len = hitec_card_text(&r, text, sizeof(text));
    CHECK(len > 0u);
    CHECK(hitec_prog_file_load(model(), text, len));
    CHECK(hitec_prog_record(model()));
    CHECK_EQ(hitec_prog_record_verdict(model()), HITEC_PROG_REC_SAME);
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_SAME);
}

/* The card by hand: the text the model hands over, kept in @p r. */
static size_t card_take(hitec_card_record_t *r)
{
    char text[HITEC_CARD_TEXT_MAX];
    const size_t len = hitec_prog_file_take(model(), text, sizeof(text));

    if (len != 0u) {
        CHECK(hitec_card_parse(text, len, r) <= HITEC_CARD_NONE);
    }
    return len;
}

/* Review round 1 on #613: the answer to the record of an aborted write
 * released the next write before the card held that write's record. */
TEST_CASE(the_answer_to_an_aborted_writes_record_releases_no_later_write)
{
    hitec_card_record_t r;

    d_ready();
    card_in();
    g_card_slow = true;   /* the rig's card half takes nothing */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK(card_take(&r) > 0u);
    CHECK_EQ(hitec_image_reg(&r.target, 0x60), 1u);
    hitec_prog_abort(model());
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    /* Another target, another write: it waits for its own record. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK(!hitec_prog_file_due(model()));   /* the card is busy */
    hitec_prog_file_done(model(), true);    /* the first record's answer */
    prun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK_EQ(g_drv.d_writes, 0u);
    /* The new record goes next, with both edits. */
    CHECK(card_take(&r) > 0u);
    CHECK_EQ(hitec_image_reg(&r.target, 0x60), 6u);
    CHECK_EQ(hitec_image_reg(&r.target, 0x54), 10u);
    hitec_prog_file_done(model(), true);
    CHECK_EQ(pstate(), HITEC_PROG_WRITING);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x60], 6u);
}

TEST_CASE(a_refusal_of_an_older_text_ends_no_later_write)
{
    hitec_card_record_t r;

    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK(card_take(&r) > 0u);
    hitec_prog_abort(model());
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    hitec_prog_file_done(model(), false);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK(hitec_prog_record(model()));
    /* Its own record refused: the write ends, nothing sent. */
    CHECK(card_take(&r) > 0u);
    hitec_prog_file_done(model(), false);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD_FAILED);
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(the_answer_to_a_record_clear_releases_no_later_write)
{
    hitec_card_record_t r;

    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK(card_take(&r) > 0u);
    hitec_prog_file_done(model(), true);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    /* The text that clears the record goes out; the next write starts
     * before the card answers it. */
    CHECK_EQ(card_take(&r), (size_t)27u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    hitec_prog_file_done(model(), true);
    prun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK_EQ(g_drv.d_writes, 1u);
    CHECK(card_take(&r) > 0u);
    CHECK_EQ(hitec_image_reg(&r.target, 0x60), 6u);
    hitec_prog_file_done(model(), true);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.d_writes, 2u);
}

TEST_CASE(an_older_text_never_answered_ends_the_write_at_3000ms)
{
    hitec_card_record_t r;
    uint32_t t0;

    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK(card_take(&r) > 0u);
    hitec_prog_abort(model());
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    t0 = g_ms;
    while (g_ms - t0 < HITEC_PROG_CARD_MS - g_frame_ms) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD_FAILED);
    CHECK_EQ(g_drv.d_writes, 0u);
    /* The late answer to the old text starts nothing. */
    hitec_prog_file_done(model(), true);
    prun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(g_drv.d_writes, 0u);
}

/* Review round 2 on #613: the card's answer started the write with the
 * switch off.  The gate is taken again when the card has the record. */

/* A D485HW read with the switch on and a card that answers by hand; a
 * write of SPEED 50% waits for its record, which the card has taken. */
static void write_waits_for_the_card(void)
{
    hitec_card_record_t r;

    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK(card_take(&r) > 0u);
}

/* The write ended at the card's answer with @p m: no servo exchange, the
 * record cleared, on the card too. */
static void ended_unsent(unsigned m)
{
    hitec_card_record_t r;

    hitec_prog_file_done(model(), true);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK_EQ(msg(), m);
    prun(100u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(g_drv.d_saves, 0u);
    CHECK_EQ(g_drv.d_reboots, 0u);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(card_take(&r), (size_t)27u);   /* the text with no record */
    hitec_prog_file_done(model(), true);
}

TEST_CASE(the_switch_off_while_the_card_takes_the_record_writes_nothing)
{
    write_waits_for_the_card();
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    ended_unsent(HITEC_PROG_MSG_WRITE_OFF);
    /* The edit stands for the next try. */
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

TEST_CASE(a_stop_while_the_card_takes_the_record_writes_nothing)
{
    write_waits_for_the_card();
    press_stop();
    prun(100u);
    CHECK(!hitec_prog_write_on(model()));
    ended_unsent(HITEC_PROG_MSG_STOP_LATCHED);
}

TEST_CASE(arm_pressed_while_the_card_takes_the_record_writes_nothing)
{
    write_waits_for_the_card();
    hitec_prog_bench_armed(model(), true);   /* the finger on ARM */
    ended_unsent(HITEC_PROG_MSG_NEED_DISARM);
    hitec_prog_bench_armed(model(), false);
}

TEST_CASE(the_switch_stays_off_until_the_answer_ends_the_write)
{
    write_waits_for_the_card();
    CHECK_EQ(hitec_prog_write_enable(model(), false), HITEC_PROG_MSG_NONE);
    /* On again while the write waits: refused as busy, as KST refuses
     * its switch while a write waits for the card. */
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_BUSY);
    CHECK(!hitec_prog_write_on(model()));
    hitec_prog_file_done(model(), true);
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(g_drv.d_writes, 0u);
}

int main(void)
{
    RUN(a_d_write_writes_saves_reboots_reads_back_and_clears_the_record);
    RUN(a_d_write_the_servo_does_not_take_fails_its_check);
    RUN(a_link_lost_before_the_save_leaves_the_record_and_read_all_first);
    RUN(a_link_lost_after_the_save_before_the_reboot_then_restore);
    RUN(a_link_lost_after_the_reboot_in_the_read_back);
    RUN(a_stop_during_a_write_cuts_it_and_keeps_the_record);
    RUN(a_classic_write_sends_the_cell_its_balance_and_reads_back);
    RUN(a_classic_link_lost_between_cell_and_balance_names_the_cell);
    RUN(repair_writes_the_balance_alone);
    RUN(a_classic_write_without_its_answer_names_the_cell);
    RUN(the_record_stands_across_a_panel_restart);
    RUN(a_card_that_refuses_the_record_keeps_the_write_from_going);
    RUN(a_card_that_does_not_answer_in_3000ms_ends_the_write);
    RUN(abort_while_the_write_waits_for_the_card_writes_nothing);
    RUN(without_a_card_the_record_is_kept_in_memory_only);
    RUN(a_record_of_another_servo_is_not_restored_and_forget_ends_it);
    RUN(a_record_on_another_model_is_another_servo);
    RUN(a_record_with_a_mix_of_old_and_new_values);
    RUN(a_write_is_refused_armed_stopped_unread_off_and_unchanged);
    RUN(a_write_without_a_read_is_refused);
    RUN(a_write_runs_across_the_clocks_wrap);
    RUN(rules_edits_and_writes_need_a_servo_in_programming_mode);
    RUN(a_confirmation_of_a_raw_value_holds_for_that_value_only);
    RUN(a_confirmation_after_the_port_stopped_is_refused);
    RUN(restore_without_a_compared_record_asks_for_read_all);
    RUN(an_arm_request_while_the_card_takes_the_record_writes_nothing);
    RUN(a_restore_to_a_servo_with_the_old_values_sends_nothing);
    RUN(a_write_the_session_cannot_plan_sends_nothing);
    RUN(a_write_the_session_refuses_after_the_card_sends_nothing);
    RUN(abort_before_the_first_step_sends_nothing_and_drops_the_record);
    RUN(the_record_loaded_from_the_card);
    RUN(the_answer_to_an_aborted_writes_record_releases_no_later_write);
    RUN(a_refusal_of_an_older_text_ends_no_later_write);
    RUN(the_answer_to_a_record_clear_releases_no_later_write);
    RUN(an_older_text_never_answered_ends_the_write_at_3000ms);
    RUN(the_switch_off_while_the_card_takes_the_record_writes_nothing);
    RUN(a_stop_while_the_card_takes_the_record_writes_nothing);
    RUN(arm_pressed_while_the_card_takes_the_record_writes_nothing);
    RUN(the_switch_stays_off_until_the_answer_ends_the_write);
    return test_summary("hitec_prog_write");
}
