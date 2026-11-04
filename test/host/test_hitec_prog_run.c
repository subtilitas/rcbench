/*
 * The Hitec programmer's run seam (shared/bench/hitec_prog_run.c) on the
 * whole loopback of hitec_prog_rig.h:
 *
 *  - travel units: D series counts from NEUTRAL 0xC2 both ways, classic
 *    the raw cells by DIRECTION, CW and CCW; the conversions at their
 *    limits;
 *  - a run's write: both end points edited, no CHECK THIS CHANGE, the
 *    write with its save, reboot and read-back;
 *  - outside a run, and for the operator's own edit inside one, the end
 *    rule still asks;
 *  - the values after a write come from its read-back, a refused one's
 *    included;
 *  - the backup file: the run's first write saves it, a later session of
 *    the run does not, an ordinary write after the run does;
 *  - the run's end switches the write off;
 *  - a value past lo or hi is refused, the exact limits taken;
 *  - the gates: no servo read, ARMED, a write that changes nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_run.h"

#include "hitec_prog_rig.h"

#include "hitec_card.h"

/* The D485HW fixture's NEUTRAL, LEFT END and RIGHT END. */
#define D_MID   8192u
#define D_LEFT  3351u
#define D_RIGHT 13003u

/* The HS-5645MG fixture's cells 0x13 and 0x14. */
#define C_13 0xC2u
#define C_14 0xA2u

/* ---------------------------------------------------------------- helpers */

/* A D485HW read. */
static void d_read(void)
{
    rig();
    servo_d485();
    connected();
}

/* An HS-5645MG read; @p ccw clears DIRECTION (0x1B bit 0) and balances the
 * cells again. */
static void c_read(bool ccw)
{
    hitec_image_t img;

    rig();
    servo_hs5645();
    if (ccw) {
        hs5645(&img);
        CHECK(hitec_image_put_cell(&img, 0x1Bu,
                                   (uint8_t)(hitec_image_cell(&img, 0x1Bu)
                                             & ~1u)));
        CHECK(hitec_c_rebalance(&img));
        for (unsigned c = 0; c < 0x24u; ++c) {
            g_drv.eeprom[c] = hitec_image_cell(&img, (uint8_t)c);
        }
    }
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
}

/* The servo of the run. */
static hitec_run_servo_t servo(void)
{
    hitec_run_servo_t sv;

    (void)hitec_prog_run_servo(model(), &sv);
    return sv;
}

/* The write started runs to its end: held again. */
static void done(void)
{
    reread();
    for (unsigned i = 0; i < 60000u && pstate() != HITEC_PROG_HELD; ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* LEAVE, then CONNECT and the read again. */
static void again(void)
{
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    connected();
}

/* ------------------------------------------------------------ travel units */

TEST_CASE(d_series_travel_units_are_counts_from_0xc2_both_ways)
{
    hitec_run_servo_t sv;

    d_read();
    sv = servo();
    CHECK(sv.ok);
    CHECK_EQ(sv.series, HITEC_D);
    CHECK_EQ(sv.mid, D_MID);
    CHECK_EQ(sv.value[0], D_MID - D_LEFT);
    CHECK_EQ(sv.value[1], D_RIGHT - D_MID);
    CHECK(sv.cw);
    /* One count from NEUTRAL at the least; 8191 keeps 0xB2 at 1 and 0xB0
     * at 16383. */
    CHECK_EQ(sv.lo, 1u);
    CHECK_EQ(sv.hi, 8191u);
    CHECK_STR_EQ(sv.backup, "");
    CHECK_EQ(hitec_run_to_reg(&sv, 0, sv.value[0]), D_LEFT);
    CHECK_EQ(hitec_run_to_reg(&sv, 1, sv.value[1]), D_RIGHT);
    CHECK_EQ(hitec_run_to_travel(&sv, 0, D_LEFT), sv.value[0]);
    CHECK_EQ(hitec_run_to_travel(&sv, 1, D_RIGHT), sv.value[1]);
    CHECK_EQ(hitec_run_to_reg(&sv, 0, sv.hi), 1u);
    CHECK_EQ(hitec_run_to_reg(&sv, 1, sv.hi), 16383u);
    /* Past NEUTRAL a register is no travel of its side, and a travel past
     * the register's range saturates. */
    CHECK_EQ(hitec_run_to_travel(&sv, 0, D_MID), 0u);
    CHECK_EQ(hitec_run_to_travel(&sv, 0, D_MID + 1u), 0u);
    CHECK_EQ(hitec_run_to_travel(&sv, 1, D_MID - 1u), 0u);
    CHECK_EQ(hitec_run_to_reg(&sv, 0, D_MID), 0u);
    CHECK_EQ(hitec_run_to_reg(&sv, 0, D_MID + 1u), 0u);
    CHECK_EQ(hitec_run_to_reg(&sv, 1, 65535u - D_MID), 65535u);
    CHECK_EQ(hitec_run_to_reg(&sv, 1, 65535u - D_MID + 1u), 65535u);

    /* A CCW servo: the same units, from the registers as read. */
    rig();
    servo_d485();
    g_drv.dreg[0x5E] = 1u;
    connected();
    sv = servo();
    CHECK(sv.ok);
    CHECK(!sv.cw);
    CHECK_EQ(sv.value[0], D_MID - D_LEFT);
}

TEST_CASE(classic_travel_units_are_the_raw_cells_by_direction)
{
    hitec_run_servo_t sv;

    /* CW: left 0x14, right 0x13. */
    c_read(false);
    sv = servo();
    CHECK(sv.ok);
    CHECK_EQ(sv.series, HITEC_CLASSIC);
    CHECK(sv.cw);
    CHECK_EQ(sv.mid, 0u);
    CHECK_EQ(sv.value[0], C_14);
    CHECK_EQ(sv.value[1], C_13);
    CHECK_EQ(sv.lo, 0u);
    CHECK_EQ(sv.hi, 255u);
    CHECK_EQ(hitec_run_to_reg(&sv, 0, 0x90u), 0x90u);
    CHECK_EQ(hitec_run_to_reg(&sv, 1, 0x91u), 0x91u);
    CHECK_EQ(hitec_run_to_travel(&sv, 1, 0x92u), 0x92u);
    /* CCW: swapped. */
    c_read(true);
    sv = servo();
    CHECK(sv.ok);
    CHECK(!sv.cw);
    CHECK_EQ(sv.value[0], C_13);
    CHECK_EQ(sv.value[1], C_14);
}

TEST_CASE(no_servo_read_is_no_run_servo)
{
    hitec_run_servo_t sv;
    uint16_t v[2] = { 7u, 7u };

    rig();
    CHECK(!hitec_prog_run_servo(model(), &sv));
    CHECK(!sv.ok);
    CHECK_EQ(sv.value[0], 0u);
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_run_write(model(), 100u, 100u),
             HITEC_PROG_MSG_NOT_READ);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NOT_READ);
    CHECK(!hitec_prog_run_values(model(), v));
    CHECK_EQ(v[0], 7u);
    /* A NEUTRAL outside the end points is no run servo either. */
    rig();
    servo_d485();
    g_drv.dreg[0xC2] = D_LEFT;
    connected();
    CHECK(!servo().ok);
    CHECK_EQ(servo().series, HITEC_D);
    rig();
    servo_d485();
    g_drv.dreg[0xC2] = D_RIGHT;
    connected();
    CHECK(!servo().ok);
    /* Nor an end point that decodes raw (above 16383). */
    rig();
    servo_d485();
    g_drv.dreg[0xB0] = 20000u;
    connected();
    CHECK(!servo().ok);
}

/* ------------------------------------------------------------------ writes */

TEST_CASE(run_write_edits_both_ends_and_starts_the_write_without_a_confirmation)
{
    uint16_t v[2];

    d_read();
    hitec_prog_run_begin(model());
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u),
             HITEC_PROG_MSG_NONE);
    /* No CHECK THIS CHANGE waits, and the draft holds both. */
    CHECK_EQ(hitec_prog_pending(model(), NULL, NULL), 0u);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0xB2), D_MID - 4000u);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0xB0), D_MID + 4500u);
    CHECK(hitec_prog_write_on(model()));
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(hitec_prog_message(model())->arg, 2u);
    CHECK_EQ(g_drv.dreg[0xB2], D_MID - 4000u);
    CHECK_EQ(g_drv.dreg[0xB0], D_MID + 4500u);
    CHECK_EQ(g_drv.dreg[0xC2], D_MID);
    /* The two registers and nothing else, then the save and the reboot. */
    CHECK_EQ(g_drv.d_writes, 2u);
    CHECK_EQ(g_drv.d_saves, 1u);
    CHECK_EQ(g_drv.d_reboots, 1u);
    CHECK(hitec_prog_run_values(model(), v));
    CHECK_EQ(v[0], 4000u);
    CHECK_EQ(v[1], 4500u);

    /* A classic servo: the cells by DIRECTION and the balance. */
    c_read(false);
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_run_write(model(), 0x90u, 0xB0u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.eeprom[0x14], 0x90u);
    CHECK_EQ(g_drv.eeprom[0x13], 0xB0u);
    CHECK(!hitec_prog_sum_bad(model()));
}

TEST_CASE(outside_a_run_the_end_rule_still_asks)
{
    d_read();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_LEFT_END, 4000),
             HITEC_PROG_EDIT_CHECK);
    CHECK_EQ(hitec_prog_pending(model(), NULL, NULL),
             HITEC_RULE_BIT(HITEC_RULE_END));
    hitec_prog_cancel(model());
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_NEUTRAL, 8000),
             HITEC_PROG_EDIT_CHECK);
    hitec_prog_cancel(model());
    /* The seam's write is a run's only, and changes nothing outside one. */
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u),
             HITEC_PROG_MSG_WRITE_OFF);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* Inside a run the operator's own edit still asks. */
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_RIGHT_END, 13000),
             HITEC_PROG_EDIT_CHECK);
    hitec_prog_cancel(model());
    /* After the run, as before it. */
    hitec_prog_run_end(model());
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_LEFT_END, 4000),
             HITEC_PROG_EDIT_CHECK);
    hitec_prog_cancel(model());
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u),
             HITEC_PROG_MSG_WRITE_OFF);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

TEST_CASE(run_values_come_from_the_read_back)
{
    uint16_t v[2] = { 0u, 0u };

    d_read();
    hitec_prog_run_begin(model());
    /* No write yet: no read-back. */
    CHECK(!hitec_prog_run_values(model(), v));
    /* The servo does not take the write: the read-back holds the values
     * before it, and so do the run's values. */
    g_drv.d_drop = true;
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    CHECK(hitec_prog_run_values(model(), v));
    CHECK_EQ(v[0], D_MID - D_LEFT);
    CHECK_EQ(v[1], D_RIGHT - D_MID);
    CHECK(!hitec_prog_write_on(model()));
    /* READ ALL: a read, not a read-back. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_REC_SAME);
    CHECK(!hitec_prog_run_values(model(), v));
    /* The record stands: the run's next write waits for it. */
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u),
             HITEC_PROG_MSG_RECORD_HELD);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    g_drv.d_drop = false;
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK(hitec_prog_run_values(model(), v));
    CHECK_EQ(v[0], 4000u);
    CHECK_EQ(v[1], 4500u);
    /* After the run no read-back is the run's. */
    hitec_prog_run_end(model());
    CHECK(!hitec_prog_run_values(model(), v));
}

TEST_CASE(the_first_write_of_a_run_saves_the_backup_file_and_later_ones_do_not)
{
    hitec_image_t bk;

    rig();
    servo_d485();
    g_card = true;
    connected();
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 1u);
    CHECK_STR_EQ(servo().backup, "HITEC001");
    /* The file holds the servo as the run found it. */
    CHECK(files_image("HITEC001.TXT", &bk));
    CHECK_EQ(hitec_image_reg(&bk, 0xB2), D_LEFT);
    CHECK_EQ(hitec_image_reg(&bk, 0xB0), D_RIGHT);
    /* The same session: no second file. */
    CHECK_EQ(hitec_prog_run_write(model(), 3000u, 3500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 1u);
    /* A later session of the run: none either. */
    again();
    CHECK_STR_EQ(servo().backup, "");
    CHECK_EQ(hitec_prog_run_write(model(), 2000u, 2500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 1u);
    CHECK_EQ(g_drv.dreg[0xB2], D_MID - 2000u);
    /* After the run a session's first write saves its backup again. */
    hitec_prog_run_end(model());
    again();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_files_saves, 2u);
}

TEST_CASE(a_run_whose_first_write_sent_nothing_saves_the_backup_later)
{
    rig();
    servo_d485();
    connected();
    hitec_prog_run_begin(model());
    /* The second read differs: the write ends with nothing sent. */
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    g_drv.dreg[0x54] = 10u;
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READS_DIFFER);
    CHECK(!model()->run_wrote);
    g_drv.dreg[0x54] = fix_d485()[0x54 / 2];
    /* A card in a later session: the run's first write that goes out
     * saves its file. */
    g_card = true;
    again();
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK(model()->run_wrote);
    CHECK_EQ(g_files_saves, 1u);
}

TEST_CASE(run_end_switches_write_off)
{
    d_read();
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    /* A verified write leaves the switch on, as any write does. */
    CHECK(hitec_prog_write_on(model()));
    hitec_prog_run_end(model());
    CHECK(!hitec_prog_write_on(model()));
    CHECK(!model()->run);
    CHECK(!model()->run_wrote);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NO_CHANGES);
    /* The end of a run with a draft of the operator's drops the draft. */
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    hitec_prog_run_end(model());
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

TEST_CASE(a_value_past_lo_or_hi_is_refused_as_the_hard_rules_say)
{
    d_read();
    hitec_prog_run_begin(model());
    /* hi + 1 on either side, and 0, below lo. */
    CHECK_EQ(hitec_prog_run_write(model(), 8192u, 4500u),
             HITEC_PROG_MSG_EDIT_REFUSED);
    CHECK_EQ(hitec_prog_message(model())->arg, HITEC_SET_ERR_RANGE);
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 8192u),
             HITEC_PROG_MSG_EDIT_REFUSED);
    CHECK_EQ(hitec_prog_run_write(model(), 0u, 4500u),
             HITEC_PROG_MSG_EDIT_REFUSED);
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 0u),
             HITEC_PROG_MSG_EDIT_REFUSED);
    /* Refused, not clamped: nothing edited, nothing switched on. */
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    /* lo and hi themselves are taken: 0xB2 = 1, 0xB0 = 16383. */
    CHECK_EQ(hitec_prog_run_write(model(), 8191u, 1u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0xB2], 1u);
    CHECK_EQ(g_drv.dreg[0xB0], D_MID + 1u);
    CHECK_EQ(hitec_prog_run_write(model(), 1u, 8191u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(g_drv.dreg[0xB2], D_MID - 1u);
    CHECK_EQ(g_drv.dreg[0xB0], 16383u);

    /* A classic cell: 255 is the last. */
    c_read(false);
    hitec_prog_run_begin(model());
    CHECK_EQ(hitec_prog_run_write(model(), 256u, 0xB0u),
             HITEC_PROG_MSG_EDIT_REFUSED);
    CHECK_EQ(hitec_prog_run_write(model(), 0x90u, 256u),
             HITEC_PROG_MSG_EDIT_REFUSED);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(hitec_prog_run_write(model(), 0u, 255u), HITEC_PROG_MSG_NONE);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.eeprom[0x14], 0u);
    CHECK_EQ(g_drv.eeprom[0x13], 255u);
}

TEST_CASE(a_run_write_is_refused_armed_and_unchanged_with_the_switch_off)
{
    d_read();
    hitec_prog_run_begin(model());
    /* ARMED: the switch is refused, nothing edited. */
    hitec_prog_bench_armed(model(), true);
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u),
             HITEC_PROG_MSG_NEED_DISARM);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    hitec_prog_bench_armed(model(), false);
    /* The servo's own values: no change, the switch off again. */
    CHECK_EQ(hitec_prog_run_write(model(), D_MID - D_LEFT, D_RIGHT - D_MID),
             HITEC_PROG_MSG_NO_CHANGES);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* A write that runs: a second one is BUSY and leaves it as it is. */
    CHECK_EQ(hitec_prog_run_write(model(), 4000u, 4500u), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_run_write(model(), 3000u, 3500u), HITEC_PROG_MSG_BUSY);
    done();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0xB2], D_MID - 4000u);
}

int main(void)
{
    RUN(d_series_travel_units_are_counts_from_0xc2_both_ways);
    RUN(classic_travel_units_are_the_raw_cells_by_direction);
    RUN(no_servo_read_is_no_run_servo);
    RUN(run_write_edits_both_ends_and_starts_the_write_without_a_confirmation);
    RUN(outside_a_run_the_end_rule_still_asks);
    RUN(run_values_come_from_the_read_back);
    RUN(the_first_write_of_a_run_saves_the_backup_file_and_later_ones_do_not);
    RUN(a_run_whose_first_write_sent_nothing_saves_the_backup_later);
    RUN(run_end_switches_write_off);
    RUN(a_value_past_lo_or_hi_is_refused_as_the_hard_rules_say);
    RUN(a_run_write_is_refused_armed_and_unchanged_with_the_switch_off);
    return test_summary("hitec_prog_run");
}
