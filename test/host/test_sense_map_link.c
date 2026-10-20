/*
 * The panel's half of the SENSE_MAP page (sense_map_link.h), against a
 * model of the IOmcu's page as link_pages.h states it (protocol 4.25), and
 * SENSE's half with the INA3221 parts on the map (sense_link_set_map()).
 *
 * Under test: nothing sent to an IOmcu older than 4.25, whose fitted parts
 * but the mirror part say OLD; the page read whole before anything is
 * written; the map written whole, once rested and only while idle; a
 * refusal not written again until the map changes; a dark map and a part
 * on the INA228's address never written; SENSE read again after a write
 * is taken; the parts' states and the events a read raises, each once,
 * none for the mirror part; and SENSE, with the map in force, never
 * writing the INA3221's frame or ENABLE bit 1.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sense_link_fix.h"

#include "sense_map_link.h"

static sense_map_link_t ml;
/* The IOmcu's SENSE_MAP as link_pages.h states it. */
static uint16_t fm[LINK_SM_COUNT];
static bool     fm_drives;          /* the bank drives: every change refused */
static uint8_t  fm_i228;            /* the INA228's address while on, or 0 */
static unsigned fm_writes, fm_reads;

/* Whether the page takes map @p m: link_pages.h's rules. */
static bool fm_takes(const uint16_t *m)
{
    if (m[LINK_SM_ENABLE] > LINK_SM_FIT_ALL) {
        return false;
    }
    const uint16_t fast = m[LINK_SM_FAST];
    if (fast != LINK_SM_FAST_NONE
        && (fast >= LINK_SM_CHANNELS
            || (m[LINK_SM_ENABLE] & (1u << (fast / 3u))) == 0u
            || m[LINK_SM_SHUNT(fast)] == 0u)) {
        return false;
    }
    for (unsigned c = 0u; c < LINK_SM_CHANNELS; ++c) {
        const uint16_t d = m[LINK_SM_SHUNT(c)];
        const uint16_t w = m[LINK_SM_WATCH(c)];
        if (d != 0u && (d < LINK_SM_DMOHM_MIN || d > LINK_SM_DMOHM_MAX)) {
            return false;
        }
        if (!(w == LINK_SM_WATCH_NONE || w == LINK_SM_WATCH_SUPPLY
              || (w >= LINK_SM_WATCH_OUT(0) && w <= LINK_SM_WATCH_OUT(7))
              || (w >= LINK_SM_WATCH_PORT(0) && w <= LINK_SM_WATCH_PORT(3)))) {
            return false;
        }
    }
    for (unsigned a = 0u; a < LINK_SM_PARTS; ++a) {
        if ((m[LINK_SM_ENABLE] & (1u << a)) == 0u) {
            continue;
        }
        if (m[LINK_SM_SHUNT(3 * a)] == 0u && m[LINK_SM_SHUNT(3 * a + 1)] == 0u
            && m[LINK_SM_SHUNT(3 * a + 2)] == 0u) {
            return false;
        }
        if (fm_i228 == 0x40u + a) {
            return false;
        }
    }
    return true;
}

static int fm_exchange(const sense_map_link_op_t *op, uint16_t *regs)
{
    if (minor < LINK_MINOR_SENSE_MAP) {
        return LINK_NACK_BAD_PAGE;
    }
    if ((unsigned)op->off + op->n > LINK_SM_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (op->write) {
        ++fm_writes;
        if ((unsigned)op->off + op->n > LINK_SM_KEPT) {
            return LINK_NACK_READ_ONLY;
        }
        uint16_t next[LINK_SM_COUNT];
        memcpy(next, fm, sizeof(next));
        memcpy(&next[op->off], op->regs, op->n * sizeof(uint16_t));
        if (fm_drives || !fm_takes(next)) {
            return LINK_NACK_BAD_VALUE;
        }
        memcpy(fm, next, sizeof(fm));
        fm[LINK_SM_FLAGS] = (uint16_t)((fm[LINK_SM_FLAGS] & 0xFFu)
                                       | LINK_SM_IN_FORCE);
        return SENSE_MAP_LINK_ACK;
    }
    ++fm_reads;
    memcpy(regs, &fm[op->off], op->n * sizeof(uint16_t));
    return SENSE_MAP_LINK_ACK;
}

static void map_poll(void)
{
    for (int k = 0; k < 4; ++k) {
        sense_map_link_op_t op;
        if (!sense_map_link_next(&ml, now, idle, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = fm_exchange(&op, regs);
        sense_map_link_done(&ml, result, op.write ? NULL : regs, now);
    }
    now += 50u;
}

static void map_polls(int n)
{
    for (int i = 0; i < n; ++i) {
        map_poll();
    }
}

/* A map: parts @p parts fitted, every shunt 100.0 mOhm, 0x40 CH1 watching
 * OUT 1 and FAST. */
static sense_map_want_t map_of(uint16_t parts)
{
    sense_map_want_t w;
    memset(&w, 0, sizeof(w));
    w.regs[LINK_SM_ENABLE] = parts;
    w.regs[LINK_SM_FAST] = ((parts & 1u) != 0u) ? 0u : LINK_SM_FAST_NONE;
    for (unsigned c = 0u; c < LINK_SM_CHANNELS; ++c) {
        w.regs[LINK_SM_SHUNT(c)] = 1000u;
    }
    w.regs[LINK_SM_WATCH(0)] = LINK_SM_WATCH_OUT(0);
    return w;
}

/* An IOmcu of minor @p m whose page holds the derived map of nothing. */
static void map_fresh(uint16_t m, uint16_t parts)
{
    fresh(m);
    memset(fm, 0, sizeof(fm));
    fm[LINK_SM_FAST] = LINK_SM_FAST_NONE;
    fm[LINK_SM_FLAGS] = LINK_SM_DERIVED;
    fm_drives = false;
    fm_i228 = 0u;
    fm_writes = fm_reads = 0u;
    sense_map_link_init(&ml);
    const sense_map_want_t w = map_of(parts);
    sense_map_link_want(&ml, &w, now);
    sense_map_link_came_up(&ml, m, now);
}

/* ------------------------------------------------------------ the version */

TEST_CASE(nothing_goes_to_an_iomcu_older_than_4_25)
{
    map_fresh(24u, 0x03u);
    map_polls(40);
    CHECK_EQ(fm_reads + fm_writes, 0u);
    CHECK(!sense_map_link_page(&ml));
    /* Its mirror part is SENSE's; every other fitted one is not read. */
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_OLD);
    CHECK(sense_map_link_part(&ml, &sl, 0u, 0u, now) != SENS_PART_OLD);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 2u, 0u, now), SENS_PART_UNKNOWN);
    /* With no link nothing is known. */
    sense_map_link_lost(&ml);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_UNKNOWN);
}

TEST_CASE(an_iomcu_that_refuses_the_page_is_left_alone)
{
    map_fresh(25u, 0x01u);
    sense_map_link_op_t op;
    CHECK(sense_map_link_next(&ml, now, true, &op));
    CHECK_EQ(op.kind, SENSE_MAP_LINK_OP_READ);
    sense_map_link_done(&ml, LINK_NACK_BAD_PAGE, NULL, now);
    CHECK(!sense_map_link_page(&ml));
    CHECK(!sense_map_link_next(&ml, now + 1000u, true, &op));
    /* Its parts but the mirror part are not read, as on 4.24. */
    const sense_map_want_t w = map_of(0x03u);
    sense_map_link_want(&ml, &w, now);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_OLD);
}

/* -------------------------------------------------------------- the map */

TEST_CASE(the_page_is_read_whole_then_the_map_written_whole)
{
    map_fresh(25u, 0x01u);
    sense_map_link_op_t op;
    CHECK(sense_map_link_next(&ml, now, true, &op));
    CHECK_EQ(op.kind, SENSE_MAP_LINK_OP_READ);
    CHECK_EQ(op.page, LINK_PAGE_SENSE_MAP);
    CHECK_EQ(op.off, 0u);
    CHECK_EQ(op.n, LINK_SM_COUNT);
    uint16_t regs[LINK_MAX_REGS];
    sense_map_link_done(&ml, fm_exchange(&op, regs), regs, now);
    /* The first map is due at once: one write of 0 to 25. */
    CHECK(sense_map_link_next(&ml, now, true, &op));
    CHECK_EQ(op.kind, SENSE_MAP_LINK_OP_WRITE);
    CHECK(op.write);
    CHECK_EQ(op.off, 0u);
    CHECK_EQ(op.n, LINK_SM_KEPT);
    CHECK_EQ(op.regs[LINK_SM_ENABLE], 0x01u);
    sense_map_link_done(&ml, fm_exchange(&op, regs), NULL, now);
    CHECK_EQ(fm[LINK_SM_ENABLE], 0x01u);
    CHECK((fm[LINK_SM_FLAGS] & LINK_SM_IN_FORCE) != 0u);
    /* SENSE is read again, once. */
    CHECK(sense_map_link_take_reread(&ml));
    CHECK(!sense_map_link_take_reread(&ml));
    /* Then IDs and FLAGS, every 500 ms, and nothing written again. */
    const unsigned w0 = fm_writes;
    const unsigned r0 = fm_reads;
    map_polls(40);                              /* 2 s */
    CHECK_EQ(fm_writes, w0);
    CHECK(fm_reads - r0 >= 4u && fm_reads - r0 <= 5u);
}

TEST_CASE(nothing_is_written_that_the_page_holds_and_nothing_fitted_reads_little)
{
    map_fresh(25u, 0x00u);
    /* The page holds a map of nothing fitted, as asked but for the
     * channels' values. */
    sense_map_want_t w = map_of(0u);
    memcpy(fm, w.regs, sizeof(w.regs));
    sense_map_link_want(&ml, &w, now);
    map_polls(40);
    CHECK_EQ(fm_writes, 0u);
    CHECK_EQ(fm_reads, 1u);                     /* the first read only */
}

TEST_CASE(a_map_waits_for_an_idle_bank_and_its_rest)
{
    map_fresh(25u, 0x01u);
    map_polls(4);
    const unsigned w0 = fm_writes;
    idle = false;
    sense_map_want_t w = map_of(0x03u);
    sense_map_link_want(&ml, &w, now);
    map_polls(40);
    CHECK_EQ(fm_writes, w0);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_PENDING);
    idle = true;
    /* An edit restarts the rest: written SENSE_MAP_LINK_SETTLE_MS after. */
    w.regs[LINK_SM_SHUNT(4)] = 500u;
    sense_map_link_want(&ml, &w, now);
    map_polls(9);                               /* 450 ms */
    CHECK_EQ(fm_writes, w0);
    map_polls(2);
    CHECK_EQ(fm_writes, w0 + 1u);
    CHECK_EQ(fm[LINK_SM_SHUNT(4)], 500u);
}

TEST_CASE(a_refused_map_is_said_once_and_asked_again_after_an_edit)
{
    map_fresh(25u, 0x01u);
    fm_drives = true;
    map_polls(4);
    const unsigned w0 = fm_writes;
    CHECK_EQ(w0, 1u);
    uint8_t part = 9u;
    uint16_t id = 1u;
    CHECK_EQ(sense_map_link_event(&ml, &part, &id), SENSE_MAP_LINK_EV_REFUSED);
    CHECK_EQ(part, 0u);
    CHECK_EQ(sense_map_link_event(&ml, &part, &id), 0u);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 0u, 0u, now), SENS_PART_REFUSED);
    map_polls(40);
    CHECK_EQ(fm_writes, w0);
    /* Put back unseen: handed out again. */
    sense_map_link_event_back(&ml, SENSE_MAP_LINK_EV_REFUSED, 0u);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), SENSE_MAP_LINK_EV_REFUSED);
    /* The same map again is no edit. */
    sense_map_want_t w = map_of(0x01u);
    sense_map_link_want(&ml, &w, now);
    map_polls(20);
    CHECK_EQ(fm_writes, w0);
    fm_drives = false;
    w.regs[LINK_SM_SHUNT(1)] = 0u;
    sense_map_link_want(&ml, &w, now);
    map_polls(20);
    CHECK_EQ(fm_writes, w0 + 1u);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 0u, 0u, now), SENS_PART_SEARCHING);
}

TEST_CASE(a_dark_part_is_said_and_no_map_is_written)
{
    map_fresh(25u, 0x01u);
    map_polls(4);
    const unsigned w0 = fm_writes;
    sense_map_want_t w = map_of(0x05u);
    w.regs[LINK_SM_SHUNT(6)] = 0u;
    w.regs[LINK_SM_SHUNT(7)] = 0u;
    w.regs[LINK_SM_SHUNT(8)] = 0u;
    w.dark = 0x04u | 0x02u;                     /* 0x41 is not fitted */
    sense_map_link_want(&ml, &w, now);
    uint8_t part = 0u;
    CHECK_EQ(sense_map_link_event(&ml, &part, NULL), SENSE_MAP_LINK_EV_DARK);
    CHECK_EQ(part, 2u);
    CHECK_EQ(ml.dark, 0x04u);
    map_polls(40);
    CHECK_EQ(fm_writes, w0);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 2u, 0u, now), SENS_PART_DARK);
    /* Said once per edit: the same map says nothing more. */
    sense_map_link_want(&ml, &w, now);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    /* A channel on again: written. */
    w.regs[LINK_SM_SHUNT(8)] = 50u;
    w.dark = 0x02u;
    sense_map_link_want(&ml, &w, now);
    map_polls(20);
    CHECK_EQ(fm_writes, w0 + 1u);
    CHECK_EQ(fm[LINK_SM_ENABLE], 0x05u);
}

/* The INA228 on at 0x40 and 0x40 fitted: the part leaves the map, with
 * FAST, and says why; the rest is written. */
TEST_CASE(a_part_on_the_ina228s_address_is_left_out)
{
    map_fresh(25u, 0x01u);
    map_polls(4);
    sense_map_want_t w = map_of(0x03u);
    w.i228_addr = 0x40u;
    fm_i228 = 0x40u;
    sense_map_link_want(&ml, &w, now);
    map_polls(20);
    CHECK_EQ(fm[LINK_SM_ENABLE], 0x02u);
    CHECK_EQ(fm[LINK_SM_FAST], LINK_SM_FAST_NONE);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 0u, 0u, now), SENS_PART_SAME_ADDR);
    /* An INA228 off the parts' addresses leaves them all in. */
    w.i228_addr = 0x45u;
    fm_i228 = 0x45u;
    sense_map_link_want(&ml, &w, now);
    map_polls(20);
    CHECK_EQ(fm[LINK_SM_ENABLE], 0x03u);
    CHECK_EQ(fm[LINK_SM_FAST], 0u);
}

TEST_CASE(an_unanswered_exchange_is_asked_again)
{
    map_fresh(25u, 0x01u);
    sense_map_link_op_t op;
    CHECK(sense_map_link_next(&ml, now, true, &op));
    sense_map_link_done(&ml, SENSE_MAP_LINK_NO_ANSWER, NULL, now);
    CHECK(sense_map_link_next(&ml, now, true, &op));
    CHECK_EQ(op.kind, SENSE_MAP_LINK_OP_READ);
    uint16_t regs[LINK_MAX_REGS];
    sense_map_link_done(&ml, fm_exchange(&op, regs), regs, now);
    CHECK(sense_map_link_next(&ml, now, true, &op));
    CHECK_EQ(op.kind, SENSE_MAP_LINK_OP_WRITE);
    sense_map_link_done(&ml, SENSE_MAP_LINK_NO_ANSWER, NULL, now);
    CHECK(!sense_map_link_take_reread(&ml));
    CHECK(sense_map_link_next(&ml, now, true, &op));
    CHECK_EQ(op.kind, SENSE_MAP_LINK_OP_WRITE);
    /* A done with nothing pending moves nothing. */
    sense_map_link_done(&ml, SENSE_MAP_LINK_ACK, NULL, now);
    sense_map_link_done(&ml, SENSE_MAP_LINK_ACK, NULL, now);
    CHECK(!sense_map_link_next(NULL, now, true, &op));
    CHECK(!sense_map_link_next(&ml, now, true, NULL));
}

/* ------------------------------------------------------ the parts' states */

/* 0x40 the mirror (FAST's), 0x41 and 0x43 fitted: 0x41 answers late and
 * then stops, 0x43 answers as something else.  Each said once; nothing for
 * 0x40, which SENSE reports. */
TEST_CASE(a_part_not_answering_or_answering_wrong_is_said_once)
{
    map_fresh(25u, 0x0Bu);
    map_polls(4);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_SEARCHING);
    fm[LINK_SM_FLAGS] = (uint16_t)(LINK_SM_IN_FORCE | LINK_SM_ID_WRONG(3));
    fm[LINK_SM_ID(3)] = 0x2260u;
    map_polls(12);
    uint8_t part = 0u;
    uint16_t id = 0u;
    CHECK_EQ(sense_map_link_event(&ml, &part, &id), SENSE_MAP_LINK_EV_WRONG);
    CHECK_EQ(part, 3u);
    CHECK_EQ(id, 0x2260u);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 3u, 0u, now), SENS_PART_WRONG);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    /* After SENSE_LINK_GRACE_MS: 0x41 and 0x40 not online.  0x41 said. */
    map_polls(50);
    CHECK_EQ(sense_map_link_event(&ml, &part, NULL), SENSE_MAP_LINK_EV_SILENT);
    CHECK_EQ(part, 1u);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_SILENT);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 0u, 0u, now), SENS_PART_SILENT);
    /* Online: the state says so, and stopping again is said again. */
    fm[LINK_SM_FLAGS] = (uint16_t)(LINK_SM_IN_FORCE | LINK_SM_ONLINE(0)
                                   | LINK_SM_ONLINE(1) | LINK_SM_ONLINE(3));
    map_polls(12);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 1u, 0u, now), SENS_PART_ONLINE);
    CHECK_EQ(sense_map_link_part(&ml, &sl, 3u, 0u, now), SENS_PART_ONLINE);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    fm[LINK_SM_FLAGS] = (uint16_t)(LINK_SM_IN_FORCE | LINK_SM_ONLINE(3));
    map_polls(12);
    CHECK_EQ(sense_map_link_event(&ml, &part, NULL), SENSE_MAP_LINK_EV_SILENT);
    CHECK_EQ(part, 1u);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    /* The status word: 4 bits a part. */
    const uint16_t word = sense_map_link_status(&ml, &sl, 0u, now);
    CHECK_EQ(SENS_PART_OF(word, 1u), SENS_PART_SILENT);
    CHECK_EQ(SENS_PART_OF(word, 2u), SENS_PART_UNKNOWN);
    CHECK_EQ(SENS_PART_OF(word, 3u), SENS_PART_ONLINE);
}

/* A part said not to answer that answers before the band shows it: over. */
TEST_CASE(a_silent_part_that_answers_before_the_band_is_not_said)
{
    map_fresh(25u, 0x03u);
    map_polls(60);                              /* past the grace */
    fm[LINK_SM_FLAGS] = (uint16_t)(LINK_SM_IN_FORCE | LINK_SM_ONLINE(1));
    map_polls(12);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    /* A link lost drops what it said; one put back after is not kept. */
    sense_map_link_lost(&ml);
    sense_map_link_event_back(&ml, SENSE_MAP_LINK_EV_SILENT, 1u);
    CHECK_EQ(sense_map_link_event(&ml, NULL, NULL), 0u);
    CHECK_EQ(sense_map_link_status(&ml, &sl, 0u, now), 0u);
}

/* ------------------------------------------- SENSE with the map in force */

/* The map in force: SENSE's INA3221 frame and ENABLE bit 1 are the
 * IOmcu's mirror.  An edit of the one-part frame writes nothing there, and
 * the bus frame carries bit 1 as the page holds it. */
TEST_CASE(sense_never_writes_the_mirror_with_the_map_in_force)
{
    fresh(25u);
    sense_link_set_map(&sl, true);
    /* The page mirrors a part at 0x42 with all three channels. */
    pg.sense[LINK_SN_ENABLE] = LINK_SN_EN_I3221;
    pg.sense[LINK_SN_SDA_PIN] = 16u;
    pg.sense[LINK_SN_SCL_PIN] = 17u;
    pg.sense[LINK_SN_KHZ] = LINK_SN_KHZ_BUS;
    pg.sense[LINK_SN_I3221_ADDR] = 0x42u;
    pg.sense[LINK_SN_I3221_SHUNT_DMOHM] = 500u;
    pg.sense[LINK_SN_I3221_CHANNELS] = 0x07u;
    sense_setup_t w = setup_default();
    w.i3221 = true;
    w.i3221_addr = 0x40u;
    w.i3221_dmohm = 1000u;
    w.i3221_ch = 0x01u;
    w.i3221_parts = 0x05u;
    want(&w);
    polls(20);
    CHECK_EQ(writes, 0u);
    CHECK_EQ(pg.sense[LINK_SN_I3221_ADDR], 0x42u);
    CHECK_EQ(sense_link_unheld(&sl) & (SENSE_LINK_ROW_I3221
                                       | SENSE_LINK_ROW_I3221_ADDR
                                       | SENSE_LINK_ROW_I3221_SHUNT
                                       | SENSE_LINK_ROW_I3221_CH), 0u);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_I3221_REFUSED));
    /* The INA228 on: the bus frame goes with bit 1 as held. */
    w.i228 = true;
    want(&w);
    polls(20);
    CHECK(writes > 0u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228 | LINK_SN_EN_I3221);
    CHECK_EQ(pg.sense[LINK_SN_I3221_ADDR], 0x42u);
    /* Without the map the same edit is the one-part frame. */
    sense_link_set_map(&sl, false);
    want(&w);
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_I3221_ADDR], 0x40u);
    sense_link_set_map(NULL, true);
}

/* The INA228 on any fitted part's address is one address: neither on. */
TEST_CASE(the_ina228_on_any_fitted_parts_address_is_one_address)
{
    fresh(25u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i228_addr = 0x43u;
    w.i3221 = true;
    w.i3221_addr = 0x40u;
    w.i3221_parts = 0x09u;
    want(&w);
    CHECK((sense_link_events(&sl) & SENSE_LINK_EV_SAME_ADDR) != 0u);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_SAME_ADDR));
    /* Neither monitor on the page's ENABLE. */
    CHECK_EQ(sl.want[LINK_SN_ENABLE] & (LINK_SN_EN_I228 | LINK_SN_EN_I3221),
             0u);
    w.i3221_parts = 0x01u;
    want(&w);
    CHECK_EQ(sense_link_events(&sl) & SENSE_LINK_EV_SAME_ADDR, 0u);
}

/* A SENSE_MAP write taken: SENSE's set-up is read again, and the identity
 * after it. */
TEST_CASE(a_reread_reads_the_setup_and_the_identity_again)
{
    fresh(25u);
    polls(4);
    const unsigned r0 = reads;
    const unsigned i0 = idents;
    sense_link_reread(&sl);
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_READ_SETUP);
    polls(2);
    CHECK(reads > r0);
    CHECK_EQ(idents, i0 + 1u);
    sense_link_reread(NULL);
    /* Not to an IOmcu without SENSE. */
    fresh(6u);
    sense_link_reread(&sl);
    CHECK(!sense_link_next(&sl, now, true, &op));
}

/* The meter and the ring with the map in force: FAST, whichever channel of
 * the mirror part it is; without it, CH1.  No channel watching the target
 * is no meter either way. */
TEST_CASE(the_meter_follows_fast_with_the_map_and_ch1_without)
{
    fresh(25u);
    sense_link_set_map(&sl, true);
    sense_setup_t w = setup_default();
    w.i3221 = true;
    w.i3221_ch = 0x02u;                     /* CH2 alone */
    w.i3221_parts = 0x01u;
    want(&w);
    sense_link_meter_t m;
    sense_link_meter(&sl, &m);
    CHECK(m.wanted);
    w.i3221_unwatched = true;
    want(&w);
    sense_link_meter(&sl, &m);
    CHECK(!m.wanted);
    CHECK(!sense_link_win_on(&sl));
    /* Without the map: CH1 of the one part, and still a watching channel. */
    sense_link_set_map(&sl, false);
    w.i3221_unwatched = false;
    want(&w);
    sense_link_meter(&sl, &m);
    CHECK(!m.wanted);
    w.i3221_ch = 0x01u;
    want(&w);
    sense_link_meter(&sl, &m);
    CHECK(m.wanted);
    w.i3221_unwatched = true;
    want(&w);
    sense_link_meter(&sl, &m);
    CHECK(!m.wanted);
}

int main(void)
{
    RUN(nothing_goes_to_an_iomcu_older_than_4_25);
    RUN(an_iomcu_that_refuses_the_page_is_left_alone);
    RUN(the_page_is_read_whole_then_the_map_written_whole);
    RUN(nothing_is_written_that_the_page_holds_and_nothing_fitted_reads_little);
    RUN(a_map_waits_for_an_idle_bank_and_its_rest);
    RUN(a_refused_map_is_said_once_and_asked_again_after_an_edit);
    RUN(a_dark_part_is_said_and_no_map_is_written);
    RUN(a_part_on_the_ina228s_address_is_left_out);
    RUN(an_unanswered_exchange_is_asked_again);
    RUN(a_part_not_answering_or_answering_wrong_is_said_once);
    RUN(a_silent_part_that_answers_before_the_band_is_not_said);
    RUN(sense_never_writes_the_mirror_with_the_map_in_force);
    RUN(the_ina228_on_any_fitted_parts_address_is_one_address);
    RUN(a_reread_reads_the_setup_and_the_identity_again);
    RUN(the_meter_follows_fast_with_the_map_and_ch1_without);
    return test_summary("sense_map_link");
}
