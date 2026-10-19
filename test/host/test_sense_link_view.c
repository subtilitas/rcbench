/*
 * The panel's half of the SENSE, SERVO_SENSE and SERVO_WIN pages: what the
 * last reads hold and the accessors that hand it to the screens, the log
 * and the band (shared/bench/sense_link_view.c).  The write and event side
 * is test_sense_link.c.
 *
 * Under test: each servo window reaching the log once; a row unheld from
 * its edit to the acknowledgement, across a lost link and a restart; the
 * ESC's figures and the totals while the read is fresh; the store off said
 * once per link; and the output
 * encoder (protocol 4.9): ENABLE bit 2 and SENSE 26 to 31 sent only to an
 * IOmcu that names 4.9, the operator told once when one older is
 * asked for it, the status read taken to 31 only while the
 * page enables it, its angle handed over while the read is fresh and the
 * part answers, and its two events -- not answering, a magnet missing, weak
 * or strong -- each once.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sense_link_fix.h"

/* ------------------------------------------------------ the windows */

/*
 * SERVO_SENSE every poll: four 50 ms windows in a row each give one row
 * for the log, keyed by the window number, and a window read twice gives
 * none.  A clip in one window, gone in the next, is still said.
 */
TEST_CASE(every_window_reaches_the_log_once)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i3221 = true;
    want(&w);
    polls(14);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I3221_ONLINE);
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    unsigned rows = 0u;
    for (uint16_t win = 100u; win < 104u; ++win) {
        pg.servo[LINK_SS_WINDOW]   = win;
        pg.servo[LINK_SS_CH_FLAGS] = LINK_SS_CH_VALID(1);
        pg.servo[LINK_SS_CH_MEAN_MA] = (uint16_t)(400u + win);
        pg.servo[LINK_SS_CH_MAX_MA]  = 900u;
        pg.servo[LINK_SS_CH_MIN_MV]  = 5900u;
        poll_once();
        if (sense_link_take_window(&sl, &b)) {
            ++rows;
            CHECK(b.servo_new);
            CHECK_EQ(b.servo_window, win);
            CHECK_EQ(b.servo_ok, 0x01u);
            CHECK_EQ(b.servo_mean_ma[0], 400 + win);
            CHECK_EQ(b.servo_max_ma[0], 900);
            CHECK_EQ(b.servo_min_mv[0], 5900u);
            b.servo_new = false;
        }
        /* The same window again: no second row. */
        CHECK(!sense_link_take_window(&sl, &b));
        poll_once();
        CHECK(!sense_link_take_window(&sl, &b));
    }
    CHECK_EQ(rows, 4u);

    /* One window clipped, the next not: seen and said. */
    pg.servo[LINK_SS_WINDOW]   = 104u;
    pg.servo[LINK_SS_CH_FLAGS] = (uint16_t)(LINK_SS_CH_VALID(1)
                                            | LINK_SS_CH_CLIPPED(1));
    poll_once();
    pg.servo[LINK_SS_WINDOW]   = 105u;
    pg.servo[LINK_SS_CH_FLAGS] = LINK_SS_CH_VALID(1);
    poll_once();
    CHECK_EQ(sl.events, SENSE_LINK_EV_I3221_CLIPPED);

    /* A window with no readings is no row. */
    pg.servo[LINK_SS_WINDOW]   = 106u;
    pg.servo[LINK_SS_CH_FLAGS] = 0u;
    poll_once();
    CHECK(!sense_link_take_window(&sl, &b));
    CHECK(!sense_link_take_window(NULL, &b));
}

/* ------------------------------------------------- what the page holds */

#define ROWS_ALL 0x03FFu

/* An edit is not on the page until its frame is written and taken: it
 * rests SENSE_LINK_SETTLE_MS first. */
TEST_CASE(a_row_is_unheld_from_its_edit_to_the_acknowledgement)
{
    fresh(9u);
    CHECK_EQ(sense_link_unheld(&sl), 0u);       /* the page not read yet */
    polls(2);
    CHECK_EQ(sense_link_unheld(&sl), 0u);       /* read: it holds it all */

    static const struct { unsigned field; uint16_t row; } k[] = {
        { 0u, SENSE_LINK_ROW_I228 },       { 1u, SENSE_LINK_ROW_I3221 },
        { 2u, SENSE_LINK_ROW_ENC },        { 3u, SENSE_LINK_ROW_PINS },
        { 4u, SENSE_LINK_ROW_I228_ADDR },  { 5u, SENSE_LINK_ROW_I228_SHUNT },
        { 6u, SENSE_LINK_ROW_I228_MAX },   { 7u, SENSE_LINK_ROW_I3221_ADDR },
        { 8u, SENSE_LINK_ROW_I3221_SHUNT }, { 9u, SENSE_LINK_ROW_I3221_CH },
    };
    uint16_t seen = 0u;
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh(9u);
        polls(2);
        sense_setup_t w = setup_default();
        switch (k[i].field) {
        case 0u: w.i228 = true;         break;
        case 1u: w.i3221 = true;        break;
        case 2u: w.as5600 = true;       break;
        case 3u: w.sda = 18; w.scl = 19; break;
        case 4u: w.i228_addr = 0x44u;   break;
        case 5u: w.i228_uohm = 199u;    break;
        case 6u: w.i228_max_da = 2000u; break;
        case 7u: w.i3221_addr = 0x41u;  break;
        case 8u: w.i3221_dmohm = 999u;  break;
        default: w.i3221_ch = 0x07u;    break;
        }
        want(&w);
        CHECK_EQ(sense_link_unheld(&sl), k[i].row);
        /* Still resting 450 ms later, and written by 550 ms. */
        polls(9);
        CHECK_EQ(sense_link_unheld(&sl), k[i].row);
        polls(2);
        CHECK_EQ(sense_link_unheld(&sl), 0u);
        CHECK(sense_link_settled(&sl));
        seen |= k[i].row;
    }
    CHECK_EQ(seen, ROWS_ALL);
    CHECK_EQ(sense_link_unheld(NULL), 0u);
}

/* A write waits for an idle bank, and the row with it. */
TEST_CASE(a_row_stays_unheld_while_the_bank_drives)
{
    fresh(7u);
    polls(2);
    idle = false;
    sense_setup_t w = setup_default();
    w.i3221_dmohm = 999u;
    want(&w);
    polls(60);
    CHECK_EQ(sense_link_unheld(&sl), SENSE_LINK_ROW_I3221_SHUNT);
    idle = true;
    polls(2);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
}

/*
 * Pins the page refuses stay unheld, with the part that is off for them,
 * for as long as they are asked: across the retries, a link that goes and
 * comes back and an IOmcu that starts again.  No IOmcu
 * answering, nothing is known and nothing is marked.
 */
TEST_CASE(refused_pins_stay_unheld_across_a_lost_link_and_a_restart)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i3221 = true;
    w.sda = 2;            /* GP3 is reserved */
    w.scl = 3;
    want(&w);
    const uint16_t refused = SENSE_LINK_ROW_PINS | SENSE_LINK_ROW_I3221;
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_BUS_REFUSED);
    CHECK_EQ(sense_link_unheld(&sl), refused);
    polls((int)(2u * SENSE_LINK_BUS_RETRY_MS / 50u));
    CHECK_EQ(sense_link_unheld(&sl), refused);

    sense_link_lost(&sl);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    polls(20);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    sense_link_came_up(&sl, minor, now);
    CHECK_EQ(sense_link_unheld(&sl), 0u);       /* not read yet */
    polls(1);
    CHECK_EQ(sense_link_unheld(&sl), refused);  /* read, written, refused */
    polls(20);
    CHECK_EQ(sense_link_unheld(&sl), refused);

    /* The IOmcu starts again at its defaults. */
    sense_page_init(&pg);
    sense_link_lost(&sl);
    sense_link_came_up(&sl, minor, now);
    polls(20);
    CHECK_EQ(sense_link_unheld(&sl), refused);

    /* Pins it takes: both rows are held. */
    w.sda = 18;
    w.scl = 19;
    want(&w);
    CHECK_EQ(sense_link_unheld(&sl), refused);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 18u);

    /* A restart that loses that: unheld from the read to the write. */
    sense_page_init(&pg);
    sense_link_lost(&sl);
    sense_link_came_up(&sl, minor, now);
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_READ_SETUP);
    uint16_t regs[LINK_MAX_REGS];
    CHECK_EQ(far_exchange(&op, regs), SENSE_LINK_ACK);
    sense_link_done(&sl, SENSE_LINK_ACK, regs, now);
    CHECK_EQ(sense_link_unheld(&sl), refused);
    polls(4);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I3221);
}

/* A part's frame the page refuses: its values, and the part left off. */
TEST_CASE(a_refused_part_frame_leaves_its_rows_and_its_switch_unheld)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    w.i228_uohm = 20000u;
    w.i228_max_da = 3000u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl),
             SENSE_LINK_ROW_I228 | SENSE_LINK_ROW_I228_SHUNT
                 | SENSE_LINK_ROW_I228_MAX);
    w.i228_max_da = 80u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
}

/* A part that is on in the settings and that the page cannot have on. */
TEST_CASE(a_part_the_page_cannot_enable_is_unheld)
{
    /* One address for both monitors: neither is enabled. */
    fresh(9u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    w.i228_addr = 0x40u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl),
             SENSE_LINK_ROW_I228 | SENSE_LINK_ROW_I3221);

    /* The pins not set: the part is off, and the pins are written. */
    fresh(9u);
    w = setup_default();
    w.i3221 = true;
    w.sda = -1;
    w.scl = -1;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl), SENSE_LINK_ROW_I3221);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 0u);
    CHECK_EQ(pg.sense[LINK_SN_SCL_PIN], 0u);
    w.i3221 = false;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl), 0u);

    /* A 4.8 IOmcu has no encoder; a 4.6 one no page. */
    fresh(8u);
    w = setup_default();
    w.as5600 = true;
    w.i228 = true;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl), SENSE_LINK_ROW_ENC);
    fresh(6u);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    w.i3221_dmohm = 999u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_unheld(&sl),
             SENSE_LINK_ROW_I228 | SENSE_LINK_ROW_ENC);
    CHECK_EQ(to_sense, 0u);
}

/*
 * The rest after an edit and the retry of refused pins, each with the
 * 2^32 ms tick wrapping inside it: the mark stays and goes at the same
 * poll as away from the wrap.
 */
TEST_CASE(a_row_is_unheld_for_the_same_time_across_the_tick_wrap)
{
    /* An edit 250 ms before the wrap: resting at 450 ms, written by 550. */
    fresh(9u);
    polls(2);
    now = 0xFFFFFFFFu - 249u;
    sense_setup_t w = setup_default();
    w.sda = 18;
    w.scl = 19;
    want(&w);
    CHECK_EQ(sense_link_unheld(&sl), SENSE_LINK_ROW_PINS);
    polls(9);
    CHECK_EQ(now, 200u);
    CHECK_EQ(sense_link_unheld(&sl), SENSE_LINK_ROW_PINS);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 16u);
    polls(2);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 18u);

    /* Pins an output holds, refused 1000 ms before the wrap: marked until
     * the retry SENSE_LINK_BUS_RETRY_MS later, which is past it, finds
     * the pin free. */
    fresh(9u);
    polls(2);
    o.slot[0].driver = OUT_DRIVER_PWM;
    o.slot[0].pin    = 18u;
    now = 0xFFFFFFFFu - 1499u;
    w.i3221 = true;
    want(&w);
    const uint16_t refused = SENSE_LINK_ROW_PINS | SENSE_LINK_ROW_I3221;
    polls(10);
    CHECK_EQ(sense_link_events(&sl), 0u);       /* resting: not written */
    CHECK_EQ(now, 0xFFFFFFFFu - 999u);
    polls(1);                                   /* written and refused */
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_BUS_REFUSED);
    CHECK_EQ(sense_link_unheld(&sl), refused);
    o.slot[0].driver = OUT_DRIVER_NONE;
    const unsigned before = writes;
    polls((int)(SENSE_LINK_BUS_RETRY_MS / 50u) - 1);
    CHECK_EQ(now, 4000u);                       /* the tick has wrapped */
    CHECK_EQ(writes, before);                   /* 4950 ms: not offered */
    CHECK_EQ(sense_link_unheld(&sl), refused);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 16u);
    polls(1);                                   /* 5000 ms: offered, taken */
    CHECK_EQ(writes, before + 1u);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 18u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I3221);
}

/*
 * Pins an output holds: the bus frame is refused and said once, and
 * offered again every SENSE_LINK_BUS_RETRY_MS, so freeing the pin on
 * OUTPUTS lets it through without an edit.
 */
TEST_CASE(a_bus_refused_for_a_held_pin_goes_through_once_it_is_free)
{
    fresh(7u);
    o.slot[0].driver = OUT_DRIVER_PWM;
    o.slot[0].pin    = 16u;
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_BUS_REFUSED);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 0u);
    const unsigned before = writes;
    polls((int)(SENSE_LINK_BUS_RETRY_MS / 50u) + 2);
    CHECK_EQ(writes, before + 1u);             /* tried again, once */
    CHECK_EQ(sense_link_events(&sl), 0u);      /* and not said again */

    o.slot[0].driver = OUT_DRIVER_NONE;
    polls((int)(SENSE_LINK_BUS_RETRY_MS / 50u) + 2);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
    CHECK(sense_link_settled(&sl));
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(the_store_off_is_said_once_per_link)
{
    fresh(7u);
    sense_link_faults(&sl, 0u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    sense_link_faults(&sl, LINK_FAULT_STORE_OFF);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_STORE_OFF);
    sense_link_faults(&sl, LINK_FAULT_STORE_OFF | LINK_FAULT_HEARTBEAT);
    CHECK_EQ(sense_link_events(&sl), 0u);
    sense_link_lost(&sl);
    sense_link_came_up(&sl, 7u, now);
    sense_link_faults(&sl, LINK_FAULT_STORE_OFF);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_STORE_OFF);
}

TEST_CASE(the_esc_figures_and_the_totals_come_from_the_last_read)
{
    fresh(7u);
    bool v_ok = true;
    bool i_ok = true;
    float v = 1.0f;
    float a = 1.0f;
    int32_t mah = 0;
    uint32_t wh = 0u;
    CHECK(!sense_link_esc(&sl, now, &v_ok, &v, &i_ok, &a));
    CHECK(!v_ok);
    CHECK(!i_ok);
    CHECK(!sense_link_totals(&sl, now, &mah, &wh));

    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(14);
    sense_page_esc(&pg, true, 24.37f, true, 31.5f);
    pg.sense[LINK_SN_I228_CHARGE_LO] = 0x2345u;   /* -0x10000 + ... */
    pg.sense[LINK_SN_I228_CHARGE_HI] = 0xFFFFu;
    pg.sense[LINK_SN_I228_ENERGY_LO] = 0x0002u;
    pg.sense[LINK_SN_I228_ENERGY_HI] = 0x0001u;
    const uint32_t reads_before = sense_link_reads(&sl);
    polls(1);
    CHECK_EQ(sense_link_reads(&sl), reads_before + 1u);
    CHECK(sense_link_esc(&sl, now, &v_ok, &v, &i_ok, &a));
    CHECK(v_ok);
    CHECK(i_ok);
    CHECK_NEAR(v, 24.37f, 0.006f);
    CHECK_NEAR(a, 31.5f, 0.006f);
    CHECK(sense_link_totals(&sl, now, &mah, &wh));
    /* Two polls with no read: BENCH's totals are used instead. */
    CHECK(!sense_link_totals(&sl, now + SENSE_LINK_TOTALS_MS, &mah, &wh));
    CHECK_EQ(mah, (int32_t)0xFFFF2345u);
    CHECK_EQ(wh, 0x00010002u);

    /* The ESC's voltage alone. */
    sense_page_esc(&pg, true, 24.0f, false, 0.0f);
    polls(1);
    CHECK(sense_link_esc(&sl, now, &v_ok, &v, &i_ok, &a));
    CHECK(v_ok);
    CHECK(!i_ok);

    /* A read that stopped answering ages out. */
    CHECK(!sense_link_esc(&sl, now + SENSE_LINK_STALE_MS, &v_ok, &v, &i_ok,
                          &a));
    CHECK(!v_ok);
}

/* ---------------------------------------------------- the output encoder */

TEST_CASE(the_encoder_is_enabled_only_on_a_4_9_coprocessor)
{
    CHECK_EQ(SENSE_LINK_ENC_MINOR, 9u);
    /* 4.8: the operator is told once, the bit is not sent, the status read
     * stops at ESC_FLAGS. */
    fresh(8u);
    sense_setup_t w = setup_default();
    w.as5600 = true;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_OLD);
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 0u);
    CHECK(!sense_link_enc_on(&sl));
    sense_link_came_up(&sl, 8u, now);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_OLD);
    polls(20);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK(sense_link_settled(&sl));

    /* With the INA228 beside it: the part is enabled, the encoder is not. */
    w.i228 = true;
    want(&w);
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], (uint16_t)LINK_SN_EN_I228);
    CHECK_EQ(last_status_n, SENSE_LINK_STATUS_COUNT_V48);
    CHECK_EQ(SENSE_LINK_STATUS_COUNT_V48, 14u);

    /* 4.6: nothing is sent, and the encoder is the reason it says. */
    fresh(6u);
    w = setup_default();
    w.as5600 = true;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_OLD);
    polls(40);
    CHECK_EQ(to_sense, 0u);

    /* 4.9: the bit goes, and the status read takes the encoder's six. */
    enc_wanted(9u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], (uint16_t)LINK_SN_EN_AS5600);
    CHECK(sense_link_enc_on(&sl));
    CHECK_EQ(last_status_n, SENSE_LINK_STATUS_COUNT);
    CHECK_EQ(SENSE_LINK_STATUS_COUNT, 20u);
    CHECK(sense_link_settled(&sl));
}

TEST_CASE(the_encoder_and_the_parts_enable_in_any_order)
{
    enc_wanted(9u);
    sense_setup_t w = setup_default();
    w.as5600 = true;
    w.i228 = true;
    w.i3221 = true;
    want(&w);
    polls(40);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 7u);
    /* A part's frame changes with all three on: the page takes it, and the
     * encoder is back on afterwards. */
    w.i228_uohm = 250u;
    want(&w);
    polls(40);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 250u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 7u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Off again. */
    w.as5600 = false;
    want(&w);
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 3u);
    CHECK_EQ(last_status_n, SENSE_LINK_STATUS_COUNT_V48);
}

TEST_CASE(the_encoders_angle_is_handed_over_while_the_read_is_fresh)
{
    enc_wanted(9u);
    sense_link_enc_t e;
    /* Before any read. */
    CHECK(!sense_link_enc(&sl, now, &e));
    far_enc(ENC_OK, 2345u, 10u, 120u);
    polls(1);
    CHECK(sense_link_enc(&sl, now, &e));
    CHECK_EQ(e.raw, 2345u);
    CHECK_EQ(e.samples, 10u);
    CHECK_EQ(e.still_ms, 120u);
    CHECK(!e.weak && !e.strong);
    CHECK_EQ(e.taken_ms, now - 50u);
    /* A read older than SENSE_LINK_STALE_MS gives nothing. */
    CHECK(!sense_link_enc(&sl, e.taken_ms + SENSE_LINK_STALE_MS, &e));
    /* The next read updates it. */
    far_enc(ENC_OK | LINK_SN_ENC_MH, 100u, 30u, 0u);
    polls(1);
    CHECK(sense_link_enc(&sl, now, &e));
    CHECK_EQ(e.raw, 100u);
    CHECK(e.strong);
    /* A part that stopped answering, or an angle never read: none. */
    far_enc(LINK_SN_ENC_VALID, 100u, 30u, 0u);
    polls(1);
    CHECK(!sense_link_enc(&sl, now, &e));
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_MD, 0u, 0u, 0u);
    polls(1);
    CHECK(!sense_link_enc(&sl, now, &e));
    CHECK(!sense_link_enc(&sl, now, NULL));
    CHECK(!sense_link_enc(NULL, now, &e));
    /* Read every 40 ms: two polls of 50 ms never go by unread. */
    far_enc(ENC_OK, 7u, 1u, 1u);
    uint32_t last = 0u;
    for (int k = 0; k < 10; ++k) {
        polls(1);
        CHECK(sense_link_enc(&sl, now, &e));
        CHECK(e.taken_ms != last);
        last = e.taken_ms;
    }
}

TEST_CASE(the_link_going_down_takes_the_angle_with_it)
{
    enc_wanted(9u);
    far_enc(ENC_OK, 1u, 1u, 1u);
    polls(1);
    sense_link_enc_t e;
    CHECK(sense_link_enc(&sl, now, &e));
    sense_link_lost(&sl);
    CHECK(!sense_link_enc(&sl, now, &e));
    CHECK(!sense_link_enc_on(&sl));
}

TEST_CASE(an_encoder_that_does_not_answer_is_said_once)
{
    enc_wanted(9u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Never online: said after the grace, once. */
    far_enc(0u, 0u, 0u, 0u);
    polls((int)(SENSE_LINK_GRACE_MS / 50u) + 2);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_SILENT);
    polls(40);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Answering: nothing to say; stopping again is said again. */
    far_enc(ENC_OK, 5u, 1u, 1u);
    polls(2);
    CHECK_EQ(sense_link_events(&sl), 0u);
    far_enc(LINK_SN_ENC_VALID, 5u, 1u, 1u);
    polls(2);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_SILENT);
    polls(10);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Something else at 0x36 is no answer. */
    far_enc(LINK_SN_ENC_WRONG, 0u, 0u, 0u);
    polls(10);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(a_missing_weak_or_strong_magnet_is_said_once_until_it_is_right)
{
    enc_wanted(9u);
    far_enc(ENC_OK, 5u, 1u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Too weak. */
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_MD | LINK_SN_ENC_ML
            | LINK_SN_ENC_VALID, 5u, 2u, 1u);
    polls(4);
    CHECK_EQ(sense_link_event(&sl, now), SENSE_LINK_EV_ENC_MAGNET);
    CHECK(sense_link_enc_magnet(&sl) & LINK_SN_ENC_ML);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Right again, then not detected: said again. */
    far_enc(ENC_OK, 5u, 3u, 1u);
    polls(4);
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 5u, 4u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    CHECK_EQ(sense_link_enc_magnet(&sl) & LINK_SN_ENC_MD, 0u);
    /* Too strong. */
    far_enc(ENC_OK, 5u, 5u, 1u);
    polls(4);
    far_enc(ENC_OK | LINK_SN_ENC_MH, 5u, 6u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    CHECK(sense_link_enc_magnet(&sl) & LINK_SN_ENC_MH);
    CHECK_EQ(sense_link_enc_magnet(NULL), 0u);
}

/* STATUS MD clear: the part answers and sees no magnet.  Its count is not
 * a position and is not handed out, whatever else the read says.  ML and MH
 * beside MD leave it a position, handed out and marked. */
TEST_CASE(an_angle_read_without_a_magnet_is_not_handed_out)
{
    enc_wanted(9u);
    sense_link_enc_t e, before;
    memset(&e, 0x5A, sizeof(e));
    memcpy(&before, &e, sizeof(e));
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 1234u, 7u, 300u);
    polls(1);
    CHECK(!sense_link_enc(&sl, now, &e));
    CHECK_EQ(memcmp(&e, &before, sizeof(e)), 0);    /* untouched */
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID | LINK_SN_ENC_ML,
            1234u, 8u, 300u);
    polls(1);
    CHECK(!sense_link_enc(&sl, now, &e));
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID | LINK_SN_ENC_MH,
            1234u, 9u, 300u);
    polls(1);
    CHECK(!sense_link_enc(&sl, now, &e));
    /* The magnet back, too weak: a position again. */
    far_enc(ENC_OK | LINK_SN_ENC_ML, 1240u, 10u, 0u);
    polls(1);
    CHECK(sense_link_enc(&sl, now, &e));
    CHECK_EQ(e.raw, 1240u);
    CHECK(e.weak && !e.strong);
}

TEST_CASE(no_magnet_is_the_reason_only_while_the_part_says_so)
{
    enc_wanted(9u);
    CHECK(!sense_link_enc_no_magnet(&sl, now));     /* no read yet */
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 5u, 1u, 1u);
    polls(1);
    CHECK(sense_link_enc_no_magnet(&sl, now));
    /* A read too old says nothing about the magnet now. */
    CHECK(!sense_link_enc_no_magnet(&sl, now + SENSE_LINK_STALE_MS));
    /* A magnet, weak or not: an angle, so no reason for none. */
    far_enc(ENC_OK, 5u, 2u, 1u);
    polls(1);
    CHECK(!sense_link_enc_no_magnet(&sl, now));
    far_enc(ENC_OK | LINK_SN_ENC_ML, 5u, 3u, 1u);
    polls(1);
    CHECK(!sense_link_enc_no_magnet(&sl, now));
    /* A part that does not answer, and one with no STATUS read yet, have
     * no magnet bits to go by. */
    far_enc(LINK_SN_ENC_VALID, 5u, 3u, 1u);
    polls(1);
    CHECK(!sense_link_enc_no_magnet(&sl, now));
    far_enc(LINK_SN_ENC_ONLINE, 0u, 0u, 0u);
    polls(1);
    CHECK(!sense_link_enc_no_magnet(&sl, now));
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 5u, 4u, 1u);
    polls(1);
    CHECK(sense_link_enc_no_magnet(&sl, now));
    sense_link_lost(&sl);
    CHECK(!sense_link_enc_no_magnet(&sl, now));
    CHECK(!sense_link_enc_no_magnet(NULL, now));
}

/* "Too weak" was said and the angle went on; the magnet then goes missing
 * and the angle stops.  That is said, and so is its return. */
TEST_CASE(a_weak_magnet_that_goes_missing_is_said_again)
{
    enc_wanted(9u);
    far_enc(ENC_OK, 5u, 1u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);
    far_enc(ENC_OK | LINK_SN_ENC_ML, 5u, 2u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID | LINK_SN_ENC_ML,
            5u, 3u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    CHECK_EQ(sense_link_enc_magnet(&sl) & LINK_SN_ENC_MD, 0u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);           /* once */
    far_enc(ENC_OK | LINK_SN_ENC_ML, 5u, 4u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    CHECK(sense_link_enc_magnet(&sl) & LINK_SN_ENC_MD);
    /* Weak to strong with the magnet detected throughout: the angle is
     * there before and after, and the magnet is said again, as strong. */
    far_enc(ENC_OK | LINK_SN_ENC_MH, 5u, 5u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    CHECK(sense_link_enc_magnet(&sl) & LINK_SN_ENC_MH);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);           /* once */
}

/* The bench's walk: a weak magnet, said once, and then edits of other rows
 * on SETUP -- the INA228's shunt, the INA3221 on, the sensor bus moved to
 * GP14 and back -- each a set-up the page takes and starts its reads
 * again under, the encoder offline until the IOmcu has found it again.
 * The magnet bits the same throughout: not said again. */
TEST_CASE(a_weak_magnet_is_not_said_again_for_an_edit_of_another_row)
{
    enc_wanted(9u);
    const uint16_t weak = ENC_OK | LINK_SN_ENC_ML;
    far_enc(weak, 5u, 1u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    sense_setup_t w = setup_default();
    w.as5600 = true;
    for (int edit = 0; edit < 4; ++edit) {
        switch (edit) {
        case 0:  w.i228_uohm = 250u; break;
        case 1:  w.i3221 = true;     break;
        case 2:  w.sda = 14; w.scl = 15; break;
        default: w.sda = 16; w.scl = 17; break;
        }
        const uint32_t reads_before = sense_link_reads(&sl);
        want(&w);
        polls(14);                      /* rested, written, read offline */
        CHECK(sense_link_reads(&sl) != reads_before);
        CHECK_EQ(pg.sense[LINK_SN_AS5600_FLAGS], 0u);
        far_enc(weak, 5u, (uint16_t)(2u + (unsigned)edit), 1u);
        polls(6);
        CHECK_EQ(sense_link_events(&sl), 0u);
    }
    /* The bits change: said again. */
    far_enc(ENC_OK | LINK_SN_ENC_MH, 5u, 9u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    /* Right, then weak again: said again. */
    far_enc(ENC_OK, 5u, 10u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);
    far_enc(weak, 5u, 11u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
}

/* An encoder said not to answer -- past the grace after a set-up -- has
 * its magnet said again when it answers; a link that comes up again too. */
TEST_CASE(a_weak_magnet_is_said_again_after_the_encoder_was_lost)
{
    enc_wanted(9u);
    const uint16_t weak = ENC_OK | LINK_SN_ENC_ML;
    far_enc(weak, 5u, 1u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    /* Offline, briefly: under the grace after a set-up nothing is said. */
    sense_setup_t w = setup_default();
    w.as5600 = true;
    w.i228_uohm = 250u;
    want(&w);
    polls(14);
    polls((int)(SENSE_LINK_GRACE_MS / 50u) - 14);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* Past it: not answering, said; back weak: the magnet said again. */
    polls(16);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_SILENT);
    far_enc(weak, 5u, 2u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
    /* A new link: news again. */
    sense_link_lost(&sl);
    sense_link_came_up(&sl, 9u, now);
    polls(6);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
}

/* A magnet event raised and not yet shown when the set-up changes is
 * dropped with the reads of that set-up; the next read under the new one
 * says it, so it is never lost. */
TEST_CASE(a_magnet_event_not_yet_shown_is_said_under_the_next_set_up)
{
    enc_wanted(9u);
    const uint16_t weak = ENC_OK | LINK_SN_ENC_ML;
    far_enc(weak, 5u, 1u, 1u);
    polls(4);                           /* raised, not taken */
    sense_setup_t w = setup_default();
    w.as5600 = true;
    w.i228_uohm = 250u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), 0u);      /* dropped, read offline */
    far_enc(weak, 5u, 2u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
}

TEST_CASE(a_magnet_is_judged_only_from_a_read_angle)
{
    enc_wanted(9u);
    /* Online with no angle read yet: the status bits are still zero, and
     * zero is not "no magnet". */
    far_enc(LINK_SN_ENC_ONLINE, 0u, 0u, 0u);
    polls(6);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* The angle read with the magnet detected: nothing to say. */
    far_enc(ENC_OK, 5u, 1u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* The angle read with the magnet not detected: said. */
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 5u, 2u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_ENC_MAGNET);
}

TEST_CASE(a_magnet_event_waiting_survives_a_current_monitor_answering)
{
    /* The magnet event is bit 16.  A read that finds a current monitor
     * answering clears that monitor's "not answering" and "another
     * identity" events with a mask of the event word's full width; a mask
     * of 16 bits would clear the magnet event with them. */
    fresh(9u);
    sense_setup_t w = setup_default();
    w.i228  = true;
    w.as5600 = true;
    want(&w);
    polls(20);
    far_flags(LINK_SN_I228_ONLINE);
    far_enc(ENC_OK, 5u, 1u, 1u);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* A weak magnet, while the caller's alert slot is busy: the event
     * waits. */
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_MD | LINK_SN_ENC_ML
            | LINK_SN_ENC_VALID, 5u, 2u, 1u);
    polls(4);
    /* More status reads with the monitor answering. */
    polls(10);
    CHECK_EQ(sense_link_event(&sl, now), SENSE_LINK_EV_ENC_MAGNET);
    CHECK_EQ(sense_link_event(&sl, now + 10000u), 0u);

    /* The same, with another event handed out first: the magnet event is
     * held back by the gap and comes with the next call after it. */
    far_enc(ENC_OK, 5u, 3u, 1u);
    polls(4);
    far_flags(LINK_SN_I228_ONLINE | LINK_SN_BUS_STUCK);
    polls(2);
    const uint32_t t0 = now + 20000u;
    CHECK_EQ(sense_link_event(&sl, t0), SENSE_LINK_EV_STUCK);
    far_flags(LINK_SN_I228_ONLINE);
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 5u, 4u, 1u);
    polls(1);
    CHECK_EQ(sense_link_event(&sl, t0 + 100u), 0u);   /* inside the gap */
    polls(3);
    CHECK_EQ(sense_link_event(&sl, t0 + 10000u), SENSE_LINK_EV_ENC_MAGNET);
}

TEST_CASE(a_magnet_event_waiting_goes_when_the_encoder_is_switched_off)
{
    enc_wanted(9u);
    far_enc(LINK_SN_ENC_ONLINE | LINK_SN_ENC_VALID, 5u, 1u, 1u);
    polls(4);
    sense_setup_t w = setup_default();
    w.as5600 = false;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(encoder_events_come_after_the_current_monitors)
{
    enc_wanted(9u);
    sense_setup_t w = setup_default();
    w.as5600 = true;
    w.i228 = true;
    want(&w);
    polls(20);
    far_enc(LINK_SN_ENC_VALID, 5u, 1u, 1u);
    far_flags(LINK_SN_I228_ID_WRONG);
    pg.sense[LINK_SN_I228_ID] = 0x1234u;
    polls(2);
    /* The monitor's identity first, the encoder's silence after. */
    CHECK_EQ(sense_link_event(&sl, now), SENSE_LINK_EV_I228_WRONG);
    polls((int)(SENSE_LINK_GRACE_MS / 50u) + 2);
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_ENC_SILENT);
}

/* The ESC's telemetry is handed over for SENSE_LINK_STALE_MS less 1 ms
 * after the read that brought it and the totals for SENSE_LINK_TOTALS_MS
 * less 1 ms, wherever on the clock that read was. */
static void a_status_read_is_fresh_for_its_time(uint32_t t0)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(14);
    sense_page_esc(&pg, true, 24.37f, true, 31.5f);
    pg.sense[LINK_SN_I228_CHARGE_LO] = 0x2345u;
    pg.sense[LINK_SN_I228_ENERGY_LO] = 0x0002u;
    now = t0;                                   /* the read is taken here */
    const uint32_t reads_before = sense_link_reads(&sl);
    polls(1);
    CHECK_EQ(sense_link_reads(&sl), reads_before + 1u);
    bool v_ok = false;
    bool i_ok = false;
    float v = 0.0f;
    float a = 0.0f;
    CHECK(sense_link_esc(&sl, t0 + 1u, &v_ok, &v, &i_ok, &a));
    CHECK(sense_link_esc(&sl, t0 + SENSE_LINK_STALE_MS - 1u, &v_ok, &v,
                         &i_ok, &a));
    CHECK_NEAR(v, 24.37f, 0.006f);
    CHECK(!sense_link_esc(&sl, t0 + SENSE_LINK_STALE_MS, &v_ok, &v, &i_ok,
                          &a));
    int32_t mah = 0;
    uint32_t wh = 0u;
    CHECK(sense_link_totals(&sl, t0 + 1u, &mah, &wh));
    CHECK(sense_link_totals(&sl, t0 + SENSE_LINK_TOTALS_MS - 1u, &mah, &wh));
    CHECK_EQ(wh, 2u);
    CHECK(!sense_link_totals(&sl, t0 + SENSE_LINK_TOTALS_MS, &mah, &wh));
}

TEST_CASE(a_status_read_is_fresh_for_its_time_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(a_status_read_is_fresh_for_its_time,
                                  SENSE_LINK_TOTALS_MS / 2u);
    at_tick_0_and_before_the_wrap(a_status_read_is_fresh_for_its_time,
                                  SENSE_LINK_STALE_MS / 2u);
}

/* The encoder's angle the same: handed over for SENSE_LINK_STALE_MS less
 * 1 ms after its read. */
static void an_angle_is_fresh_for_its_time(uint32_t t0)
{
    enc_wanted(9u);
    far_enc(ENC_OK, 2345u, 10u, 120u);
    now = t0;
    polls(1);
    sense_link_enc_t e;
    CHECK(sense_link_enc(&sl, t0 + 1u, &e));
    CHECK_EQ(e.taken_ms, t0);
    CHECK(sense_link_enc(&sl, t0 + SENSE_LINK_STALE_MS - 1u, &e));
    CHECK_EQ(e.raw, 2345u);
    CHECK(!sense_link_enc(&sl, t0 + SENSE_LINK_STALE_MS, &e));
}

TEST_CASE(an_angle_is_fresh_for_its_time_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(an_angle_is_fresh_for_its_time,
                                  SENSE_LINK_STALE_MS / 2u);
}

int main(void)
{
    RUN(the_encoder_is_enabled_only_on_a_4_9_coprocessor);
    RUN(the_encoder_and_the_parts_enable_in_any_order);
    RUN(the_encoders_angle_is_handed_over_while_the_read_is_fresh);
    RUN(the_link_going_down_takes_the_angle_with_it);
    RUN(an_encoder_that_does_not_answer_is_said_once);
    RUN(a_missing_weak_or_strong_magnet_is_said_once_until_it_is_right);
    RUN(an_angle_read_without_a_magnet_is_not_handed_out);
    RUN(no_magnet_is_the_reason_only_while_the_part_says_so);
    RUN(a_weak_magnet_that_goes_missing_is_said_again);
    RUN(a_weak_magnet_is_not_said_again_for_an_edit_of_another_row);
    RUN(a_weak_magnet_is_said_again_after_the_encoder_was_lost);
    RUN(a_magnet_event_not_yet_shown_is_said_under_the_next_set_up);
    RUN(a_magnet_is_judged_only_from_a_read_angle);
    RUN(a_magnet_event_waiting_survives_a_current_monitor_answering);
    RUN(a_magnet_event_waiting_goes_when_the_encoder_is_switched_off);
    RUN(encoder_events_come_after_the_current_monitors);
    RUN(every_window_reaches_the_log_once);
    RUN(a_row_is_unheld_from_its_edit_to_the_acknowledgement);
    RUN(a_row_stays_unheld_while_the_bank_drives);
    RUN(refused_pins_stay_unheld_across_a_lost_link_and_a_restart);
    RUN(a_refused_part_frame_leaves_its_rows_and_its_switch_unheld);
    RUN(a_part_the_page_cannot_enable_is_unheld);
    RUN(a_row_is_unheld_for_the_same_time_across_the_tick_wrap);
    RUN(a_bus_refused_for_a_held_pin_goes_through_once_it_is_free);
    RUN(the_store_off_is_said_once_per_link);
    RUN(the_esc_figures_and_the_totals_come_from_the_last_read);
    RUN(a_status_read_is_fresh_for_its_time_across_the_tick_wrap);
    RUN(an_angle_is_fresh_for_its_time_across_the_tick_wrap);
    return test_summary("sense_link_view");
}
