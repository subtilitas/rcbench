/*
 * The second read before a Hitec write's plan (shared/bench/hitec_prog_write.c)
 * on the whole loopback of hitec_prog_rig.h:
 *
 *  - the reads agree: the write plans and goes on, after a second read of
 *    cells 0x00 to 0x23 (classic), of the factory block as well for FACTORY
 *    DEFAULT, or of the 12 registers a D series plan writes;
 *  - the reads differ in one cell, the first or the last of the set: the
 *    write is refused naming it, nothing is planned, recorded, saved or
 *    sent, the image is the first read, and the edits stay;
 *  - a cell outside the set that differs is not read again;
 *  - tried again while the servo answers the other value, refused again;
 *    READ ALL takes a new first read, and the write goes;
 *  - ABORT and STOP during the second read: nothing planned, the first read
 *    kept, the next write goes;
 *  - the second read's time on the host model, at a frame of 16ms and of
 *    33ms (docs/HitecProgrammer.md, "Writing").
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos these tests model.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

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
    for (unsigned i = 0; i < 60000u
                         && (hitec_prog_rereading(model())
                             || pstate() == HITEC_PROG_BACKUP
                             || pstate() == HITEC_PROG_RECORDING
                             || pstate() == HITEC_PROG_WRITING);
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* The second read's addresses, as the progress says while it runs. */
static unsigned reread_total(void)
{
    unsigned done = 0;
    unsigned total = 0;

    CHECK(hitec_prog_rereading(model()));
    CHECK(hitec_prog_progress(model(), &done, &total));
    return total;
}

/* Refused at @p addr, with nothing of the write done. */
static void refused_at(unsigned addr, unsigned c_writes, unsigned d_writes)
{
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK(!hitec_prog_rereading(model()));
    CHECK_EQ(msg(), HITEC_PROG_MSG_READS_DIFFER);
    CHECK_EQ(hitec_prog_message(model())->arg, addr);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_WARN);
    CHECK_EQ(g_drv.c_writes, c_writes);
    CHECK_EQ(g_drv.d_writes, d_writes);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(model()->ses.plan.n, 0u);
}

/* --------------------------------------------------------------- classic */

/* The reads agree: 36 cells read again, then the write as before. */
TEST_CASE(classic_reads_that_agree_let_the_write_go)
{
    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    CHECK_EQ(reread_total(), 0x24u);
    /* Nothing is planned before the second read ends. */
    CHECK_EQ(g_drv.c_writes, 0u);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK(g_drv.c_writes > 0u);
}

/* The station's case: the second read of 0x17 answers 0x04 where the first
 * read 0x05.  Refused, nothing sent; the edit stays. */
TEST_CASE(classic_reads_that_differ_in_one_cell_refuse_the_write)
{
    static const uint8_t k_cell[] = { 0x17u, 0x00u, 0x23u };

    for (size_t k = 0; k < sizeof(k_cell) / sizeof(k_cell[0]); ++k) {
        const uint8_t a = k_cell[k];
        uint8_t first;

        c_ready();
        card_in();
        g_drv.eeprom[a] = 0x05u;
        CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
        until(HITEC_PROG_HELD);
        CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
        first = hitec_image_cell(hitec_prog_servo(model()), a);
        CHECK_EQ(first, 0x05u);
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
                 HITEC_PROG_EDIT_OK);
        const unsigned saves = g_files_saves;
        const unsigned cards = g_card_writes;
        const unsigned changes = hitec_prog_changes(model());
        CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
        g_drv.eeprom[a] = 0x04u;
        written();
        refused_at(a, 0u, 0u);
        CHECK_EQ(g_files_saves, saves);
        CHECK_EQ(g_card_writes, cards);
        CHECK_EQ(hitec_image_cell(&model()->ses.image, a), first);
        CHECK_EQ(hitec_image_cell(hitec_prog_servo(model()), a), first);
        CHECK_EQ(hitec_prog_changes(model()), changes);
        CHECK(hitec_prog_write_on(model()));
    }
}

/* Tried again while the servo answers the other value: refused again, as
 * the same first read is compared.  READ ALL takes 0x04 as the first read,
 * and the write goes. */
TEST_CASE(read_all_takes_a_new_first_read_and_the_write_goes)
{
    c_ready();
    g_drv.eeprom[0x17] = 0x05u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    g_drv.eeprom[0x17] = 0x04u;
    written();
    refused_at(0x17u, 0u, 0u);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    refused_at(0x17u, 0u, 0u);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(hitec_image_cell(hitec_prog_servo(model()), 0x17), 0x04u);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
}

/* A factory cell no edit's plan depends on is not read again; FACTORY
 * DEFAULT reads the factory block 0x30 to 0x53 too, and a difference at
 * its first or its last cell refuses it. */
TEST_CASE(the_factory_block_is_read_again_for_factory_default_only)
{
    static const uint8_t k_cell[] = { 0x30u, 0x53u };

    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    g_drv.eeprom[0x30] ^= 0x01u;
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);

    for (size_t k = 0; k < sizeof(k_cell) / sizeof(k_cell[0]); ++k) {
        c_ready();
        /* Cell 0x08 off its factory value: FACTORY DEFAULT has a write. */
        g_drv.eeprom[0x08] ^= 0x01u;
        g_drv.eeprom[HITEC_C_SUM_LAST] =
            (uint8_t)(g_drv.eeprom[HITEC_C_SUM_LAST] ^ 0x01u);
        CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
        until(HITEC_PROG_HELD);
        CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
        CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NONE);
        CHECK_EQ(reread_total(), 2u * 0x24u);
        g_drv.eeprom[k_cell[k]] ^= 0x01u;
        written();
        refused_at(k_cell[k], 0u, 0u);
    }
}

/* ------------------------------------------------------------- D series */

/* A register a plan writes is read again, one it does not write is not. */
TEST_CASE(d_series_reads_again_the_registers_a_plan_writes)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(reread_total(), 12u);
    g_drv.dreg[0x54] ^= 0x0001u;
    written();
    refused_at(0x54u, 0u, 0u);
    CHECK_EQ(g_drv.d_saves, 0u);
    CHECK_EQ(hitec_image_reg(hitec_prog_servo(model()), 0x54), 4095u);

    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    g_drv.dreg[0x00] ^= 0x0001u;            /* the model: not written */
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK_EQ(g_drv.dreg[0x54], 10u);
}

/* ------------------------------------------------------- abort and stop */

/* ABORT part way through the second read: nothing planned, the first read
 * kept whole, and the next write goes. */
TEST_CASE(abort_during_the_second_read_plans_nothing)
{
    unsigned done = 0;
    unsigned total = 0;

    c_ready();
    card_in();
    const unsigned cards = g_card_writes;
    const hitec_image_t first = *hitec_prog_servo(model());
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    /* The servo answers other values from here: the read part way through
     * has taken some of them into the image. */
    for (unsigned c = 0; c < HITEC_C_BLOCK_LEN; ++c) {
        g_drv.eeprom[c] ^= 0x01u;
    }
    for (unsigned i = 0; i < 60000u
                         && (!hitec_prog_progress(model(), &done, &total)
                             || done < 10u);
         ++i) {
        pms();
    }
    CHECK(hitec_prog_rereading(model()));
    CHECK(done >= 10u && done < total);
    hitec_prog_abort(model());
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK_EQ(g_drv.c_writes, 0u);
    CHECK_EQ(g_card_writes, cards);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(model()->ses.plan.n, 0u);
    CHECK(memcmp(model()->ses.image.mem, first.mem, sizeof(first.mem)) == 0);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    /* The servo as it was: the next write goes. */
    for (unsigned c = 0; c < HITEC_C_BLOCK_LEN; ++c) {
        g_drv.eeprom[c] ^= 0x01u;
    }
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    written();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
}

/* STOP part way through the second read: the read is cut, nothing planned,
 * the first read kept. */
TEST_CASE(a_stop_during_the_second_read_plans_nothing)
{
    unsigned done = 0;
    unsigned total = 0;

    d_ready();
    const hitec_image_t first = *hitec_prog_servo(model());
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    for (unsigned i = 0; i < 60000u
                         && (!hitec_prog_progress(model(), &done, &total)
                             || done < 4u);
         ++i) {
        pms();
    }
    CHECK(hitec_prog_rereading(model()));
    press_stop();
    for (unsigned i = 0; i < 60000u && hitec_prog_rereading(model()); ++i) {
        pms();
    }
    prun(50u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(g_drv.d_saves, 0u);
    CHECK(!hitec_prog_record(model()));
    CHECK_EQ(model()->ses.plan.n, 0u);
    CHECK(memcmp(model()->ses.image.mem, first.mem, sizeof(first.mem)) == 0);
}

/* ------------------------------------------------------------- the time */

/* From the write's call to the end of the second read, ms. */
static uint32_t reread_ms(uint8_t series, bool factory, unsigned frame_ms)
{
    uint32_t t0;

    rig();
    g_frame_ms = frame_ms;
    if (series == (uint8_t)HITEC_CLASSIC) {
        servo_hs5645();
        CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    } else {
        servo_d485();
    }
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    if (factory) {
        g_drv.eeprom[0x08] ^= 0x01u;
        g_drv.eeprom[HITEC_C_SUM_LAST] ^= 0x01u;
        CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
        until(HITEC_PROG_HELD);
        CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
        prun(frame_ms - g_ms % frame_ms);
        t0 = g_ms;
        CHECK_EQ(hitec_prog_factory(model()), HITEC_PROG_MSG_NONE);
    } else {
        CHECK_EQ(hitec_prog_edit(model(),
                                 series == (uint8_t)HITEC_CLASSIC
                                     ? HITEC_P_C_SPEED : HITEC_P_D_SPEED,
                                 50),
                 HITEC_PROG_EDIT_OK);
        prun(frame_ms - g_ms % frame_ms);
        t0 = g_ms;
        CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    }
    for (unsigned i = 0; i < 60000u && hitec_prog_rereading(model()); ++i) {
        pms();
    }
    return g_ms - t0;
}

/* The figures docs/HitecProgrammer.md states, within one frame. */
TEST_CASE(the_second_read_takes_the_time_the_docs_state)
{
    static const struct {
        uint8_t  series;
        bool     factory;
        unsigned frame_ms;
        uint32_t ms;
    } k_case[] = {
        { HITEC_CLASSIC, false, 16u, 1152u },
        { HITEC_CLASSIC, true,  16u, 2304u },
        { HITEC_D,       false, 16u, 704u },
        { HITEC_CLASSIC, false, 33u, 2376u },
        { HITEC_CLASSIC, true,  33u, 4752u },
        { HITEC_D,       false, 33u, 792u },
    };

    for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        const uint32_t took = reread_ms(k_case[k].series, k_case[k].factory,
                                        k_case[k].frame_ms);

        if (took + k_case[k].frame_ms < k_case[k].ms
            || took > k_case[k].ms + k_case[k].frame_ms) {
            T_FAIL("case %zu: %ums, want %u +- %u", k, (unsigned)took,
                   (unsigned)k_case[k].ms, k_case[k].frame_ms);
        }
    }
}

int main(void)
{
    RUN(classic_reads_that_agree_let_the_write_go);
    RUN(classic_reads_that_differ_in_one_cell_refuse_the_write);
    RUN(read_all_takes_a_new_first_read_and_the_write_goes);
    RUN(the_factory_block_is_read_again_for_factory_default_only);
    RUN(d_series_reads_again_the_registers_a_plan_writes);
    RUN(abort_during_the_second_read_plans_nothing);
    RUN(a_stop_during_the_second_read_plans_nothing);
    RUN(the_second_read_takes_the_time_the_docs_state);
    return test_summary("hitec_prog_reread");
}
