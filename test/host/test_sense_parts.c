/*
 * Up to four INA3221 on the sensor bus (sense_sched.h, sense_svc.h,
 * sense_page.h), against the parts of fake_ina.h and a clock the suite
 * moves 1ms a tick.
 *
 * Under test: with 4 parts and 12 channels, FAST read every tick, each
 * other channel 90 or 91 times a second, each bus voltage every 24ms and
 * each part's Mask/Enable and Configuration 25 times a second; every tick
 * inside 1ms of bus time, the longest the read-back read twice beside a
 * voltage, 742.5us; the windows of 0, 1 and 12 channels; a shunt for
 * each channel; FAST on another part than 0x40, and none, with its ring
 * and its capture; FAST on a channel that is off taken as none; one probe
 * a tick; a part missing beside three that read, a part reset found
 * within one rotation and only its windows emptied; the service's
 * snapshot of four parts and its scan; the SENSE page's one part as the
 * map, and the page showing the part SENSE names.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "link_pages.h"
#include "sense_page.h"
#include "sense_sched.h"
#include "sense_svc.h"

#define I228_ADDR  0x45u
#define I228_UOHM  200u
#define I228_MA    204800u
#define UOHM       100000u        /* the DAOKAI modules' R100 shunts */

static fake_bus_t    fb;
static fake_part_t  *i228;
static fake_part_t  *part[SENSE_I3221_PARTS];
static fake_part_t  *enc;
static sense_sched_t s;
static uint64_t      g_us;

static uint64_t now_us(void *ctx)
{
    (void)ctx;
    return g_us;
}

/* The bus with @p fitted INA3221 (bit a at 0x40 + a), the INA228 and the
 * encoder; nothing set up yet. */
static void bus(uint8_t fitted)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        part[a] = NULL;
    }
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        if ((fitted & (1u << a)) != 0u) {
            part[a] = fake_add(&fb, FAKE_INA3221, (uint8_t)(0x40u + a), 0.1);
            for (unsigned ch = 0; ch < 3u; ++ch) {
                part[a]->amps[ch]  = 0.01 * (double)(10u * a + ch + 1u);
                part[a]->volts[ch] = 5.0 + 0.1 * (double)a;
            }
        }
    }
    i228 = fake_add(&fb, FAKE_INA228, I228_ADDR, 200e-6);
    i228->volts[0] = 16.8;
    enc = fake_add(&fb, FAKE_AS5600, 0x36u, 0.0);
}

/* A map of @p fitted parts, every channel of them on its R100 shunt. */
static sense_i3221_cfg_t map_all(uint8_t fitted, uint8_t fast)
{
    sense_i3221_cfg_t m;
    memset(&m, 0, sizeof(m));
    m.fitted = fitted;
    m.fast   = fast;
    for (unsigned c = 0; c < SENSE_CHANNELS; ++c) {
        if ((fitted & (1u << (c / 3u))) != 0u) {
            m.shunt_uohm[c] = UOHM;
        }
    }
    return m;
}

static void start(const sense_i3221_cfg_t *m, bool with_228, bool with_enc)
{
    const sense_sched_io_t io = { { fake_read, fake_write, &fb }, now_us,
                                  NULL, NULL };
    sense_sched_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ina228_en         = with_228;
    cfg.ina228_addr       = I228_ADDR;
    cfg.ina228_shunt_uohm = I228_UOHM;
    cfg.ina228_max_ma     = I228_MA;
    cfg.i3221             = *m;
    cfg.as5600_en         = with_enc;
    g_us = 10000000u;
    sense_sched_init(&s, &io, &cfg);
}

static void tick(void)
{
    const uint64_t at = g_us;
    sense_sched_tick(&s);
    g_us = at + 1000u;
}

static void ticks(unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        tick();
    }
}

static void zero_counts(void)
{
    for (size_t i = 0; i < fb.n; ++i) {
        memset(fb.part[i].reads, 0, sizeof(fb.part[i].reads));
    }
}

static uint64_t all_clocks(void)
{
    uint64_t c = 0u;
    for (size_t i = 0; i < fb.n; ++i) {
        c += fb.part[i].clocks;
    }
    return c;
}

static bool all_online(void)
{
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        if (part[a] != NULL
            && ina3221_state(&s.i3221[a]) != SENSE_PART_ONLINE) {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- rates */

TEST_CASE(twelve_channels_are_read_at_their_rates)
{
    bus(0x0Fu);
    const sense_i3221_cfg_t m = map_all(0x0Fu, (uint8_t)SENSE_CH(1u, 2u));
    start(&m, true, true);
    ticks(4u);                              /* one probe a tick */
    CHECK(all_online());
    CHECK_EQ(s.n_others, 11u);
    zero_counts();
    ticks(1000u);
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        for (unsigned ch = 1u; ch <= 3u; ++ch) {
            const unsigned c = SENSE_CH(a, ch);
            const unsigned n_i = part[a]->reads[INA3221_SHUNT1 + 2u * (ch - 1u)];
            const unsigned n_v = part[a]->reads[INA3221_BUS1 + 2u * (ch - 1u)];
            if (c == m.fast) {
                CHECK_EQ(n_i, 1000u);
            } else {
                /* 1000 / 11 a second. */
                CHECK(n_i == 90u || n_i == 91u);
            }
            CHECK(n_v == 41u || n_v == 42u);
        }
        CHECK_EQ(part[a]->reads[INA3221_MASK_ENABLE], 25u);
        CHECK_EQ(part[a]->reads[INA3221_CONFIG], 25u);
    }
    CHECK_EQ(i228->reads[INA228_CURRENT], 500u);
    CHECK_EQ(i228->reads[INA228_VBUS], 500u);
    CHECK_EQ(i228->reads[INA228_DIAG_ALRT], 50u);
    CHECK_EQ(i228->reads[INA228_ENERGY], 50u);
    CHECK_EQ(i228->reads[INA228_CHARGE], 50u);
    CHECK_EQ(i228->reads[INA228_DIETEMP], 25u);
    CHECK_EQ(i228->reads[INA228_ADC_CONFIG], 25u);
    CHECK_EQ(enc->reads[AS5600_REG_RAW_ANGLE], 500u);
    CHECK_EQ(s.have_flags, 0x0Fu);
    CHECK_EQ(fb.bad_width, 0u);
}

/* Each tick's bus time in clocks of 2.5us: the longest even tick reads
 * FAST, another channel, the INA228's current (57), a voltage and a
 * read-back read twice: 48 + 48 + 57 + 48 + 96 = 297, 742.5us; the
 * longest odd one the encoder's field slot, 48 + 48 + 57 + 135 = 288. */
TEST_CASE(every_tick_with_twelve_channels_fits_a_millisecond)
{
    bus(0x0Fu);
    const sense_i3221_cfg_t m = map_all(0x0Fu, 0u);
    start(&m, true, true);
    ticks(10u);
    CHECK(all_online());
    unsigned worst_even = 0u;
    unsigned worst_odd = 0u;
    /* 60 even ticks bring every voltage turn to every rotation slot. */
    for (unsigned k = 0; k < 2400u; ++k) {
        if ((s.ticks & 1u) == 0u && (s.rot % SENSE_ROTATION) % 10u == 9u) {
            /* A Configuration slot comes: read back wrong once. */
            for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
                part[a]->glitch_reg   = INA3221_CONFIG;
                part[a]->glitch_value = INA3221_CONFIG_RESET;
                part[a]->glitch_n     = 1u;
            }
        }
        const bool even = (s.ticks & 1u) == 0u;
        const uint64_t c0 = all_clocks();
        tick();
        const unsigned c = (unsigned)(all_clocks() - c0);
        if (even && c > worst_even) {
            worst_even = c;
        }
        if (!even && c > worst_odd) {
            worst_odd = c;
        }
        for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
            part[a]->glitch_n = 0u;
        }
    }
    CHECK_EQ(worst_even, 297u);
    CHECK_EQ(worst_odd, 288u);
    CHECK(297u * 25u < 10000u);
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        CHECK_EQ(s.i3221_resets[a], 0u);    /* one wrong read is no reset */
    }
}

/* ------------------------------------------------------------- windows */

TEST_CASE(windows_of_none_one_and_twelve_channels)
{
    sense_window_t w;
    /* None: the INA228 alone; every channel's window empty, the ring's
     * entries closed and empty. */
    bus(0x0Fu);
    sense_i3221_cfg_t m = map_all(0u, SENSE_FAST_NONE);
    start(&m, true, false);
    ticks(101u);
    for (unsigned c = 0; c < SENSE_CHANNELS; ++c) {
        CHECK(sense_sched_window(&s, c, &w));
        CHECK_EQ(w.n_i, 0u);
        CHECK_EQ(w.n_v, 0u);
    }
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_EQ(w.n_i, 25u);
    sense_ring_win_t r;
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 0u);
    CHECK(sense_sched_fast_part(&s) == NULL);
    for (size_t i = 0; i < fb.n; ++i) {
        if (fb.part[i].kind == FAKE_INA3221) {
            CHECK_EQ(fb.part[i].clocks, 0u);    /* never addressed */
        }
    }

    /* One: 0x42's CH3 alone, FAST. */
    bus(0x0Fu);
    memset(&m, 0, sizeof(m));
    m.fitted = 0x04u;
    m.shunt_uohm[SENSE_CH(2u, 3u)] = UOHM;
    m.fast = (uint8_t)SENSE_CH(2u, 3u);
    start(&m, false, false);
    ticks(101u);
    CHECK(sense_sched_window(&s, SENSE_CH(2u, 3u), &w));
    CHECK_EQ(w.n_i, 50u);
    CHECK_EQ(w.i_mean_ua, 230000);
    CHECK(w.n_v >= 2u && w.n_v <= 3u);
    CHECK_EQ(w.v_mean_uv, 5200000);
    CHECK_EQ(s.n_others, 0u);
    for (unsigned c = 0; c < SENSE_CHANNELS; ++c) {
        if (c != SENSE_CH(2u, 3u)) {
            CHECK(sense_sched_window(&s, c, &w));
            CHECK_EQ(w.n_i + w.n_v, 0u);
        }
    }
    CHECK_EQ(part[0]->clocks + part[1]->clocks + part[3]->clocks, 0u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK_EQ(r.i_mean_ua, 230000);

    /* Twelve: each with its own current; FAST 50 samples, the others 4 or
     * 5 a window. */
    bus(0x0Fu);
    m = map_all(0x0Fu, 0u);
    start(&m, false, false);
    ticks(201u);
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        for (unsigned ch = 1u; ch <= 3u; ++ch) {
            CHECK(sense_sched_window(&s, SENSE_CH(a, ch), &w));
            if (SENSE_CH(a, ch) == 0u) {
                CHECK_EQ(w.n_i, 50u);
            } else {
                CHECK(w.n_i >= 4u && w.n_i <= 5u);
            }
            CHECK_EQ(w.i_mean_ua, (int32_t)(10000u * (10u * a + ch)));
            CHECK(w.n_v >= 2u && w.n_v <= 3u);
        }
    }
}

/* Each channel's shunt is its own: a part with R100 shunts read as 50mOhm
 * on CH2 shows twice the current there, and its clip ends follow. */
TEST_CASE(each_channel_has_its_own_shunt)
{
    bus(0x01u);
    sense_i3221_cfg_t m = map_all(0x01u, 0u);
    m.shunt_uohm[1] = UOHM / 2u;
    start(&m, false, false);
    ticks(101u);
    sense_window_t w;
    CHECK(sense_sched_window(&s, 0u, &w));
    CHECK_EQ(w.i_mean_ua, 10000);
    CHECK(sense_sched_window(&s, 1u, &w));
    CHECK_EQ(w.i_mean_ua, 40000);           /* 20mA through 0.1Ohm */
    CHECK_EQ(s.ch_top_ua[0], 1638000);
    CHECK_EQ(s.ch_top_ua[1], 3276000);
    CHECK_EQ(s.ch_top_ua[3], 0);            /* not fitted */
    /* The driver itself: an enabled channel with no shunt is refused. */
    ina3221_t d;
    sense_bus_t b;
    sense_bus_init(&b, &(sense_i2c_t){ fake_read, fake_write, &fb });
    const uint32_t three[3] = { UOHM, 0u, UOHM };
    CHECK_EQ(ina3221_init_shunts(&d, &b, 0x40u, three,
                                 INA3221_CONFIG_BENCH_ALL),
             INA3221_SETUP_NO_SHUNT);
    CHECK_EQ(ina3221_init_shunts(&d, &b, 0x40u, three, 0x5007u),
             INA3221_SETUP_OK);
    CHECK_EQ(d.shunt_uohm[1], 0u);
    /* One shunt for all three: one too small is said before no channel. */
    CHECK_EQ(ina3221_init(&d, &b, 0x40u, INA3221_SHUNT_MIN_UOHM - 1u, 0x0007u),
             INA3221_SETUP_NO_SHUNT);
    CHECK_EQ(ina3221_init(&d, &b, 0x40u, UOHM, 0x0007u),
             INA3221_SETUP_NO_CHANNEL);
}

/* ---------------------------------------------------------------- FAST */

TEST_CASE(fast_on_another_part_carries_the_ring_and_the_capture)
{
    bus(0x0Fu);
    const sense_i3221_cfg_t m = map_all(0x0Fu, (uint8_t)SENSE_CH(3u, 2u));
    start(&m, false, false);
    ticks(10u);
    CHECK(sense_sched_fast_part(&s) == &s.i3221[3]);
    part[3]->amps[1] = 0.3;
    ticks(200u);
    sense_ring_win_t r;
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK_EQ(r.i_mean_ua, 300000);
    /* A capture arms on FAST's part online, and is lost with it. */
    const sense_cap_arm_t lv = { SENSE_CAP_RISE_AUTO, 300000, 100000, 50000 };
    CHECK(sense_sched_cap_arm(&s, &lv));
    CHECK_EQ(s.cap.state, SENSE_CAP_ARMED);
    part[0]->present = false;               /* another part: nothing */
    ticks(5u);
    CHECK_EQ(s.cap.state, SENSE_CAP_ARMED);
    part[3]->present = false;
    ticks(5u);
    CHECK_EQ(s.cap.state, SENSE_CAP_LOST);
    CHECK(!sense_sched_cap_arm(&s, &lv));
}

TEST_CASE(fast_none_or_on_a_channel_off_reads_no_channel_every_tick)
{
    bus(0x03u);
    sense_i3221_cfg_t m = map_all(0x03u, SENSE_FAST_NONE);
    start(&m, false, false);
    CHECK_EQ(s.n_others, 6u);
    ticks(4u);
    zero_counts();
    ticks(600u);
    for (unsigned a = 0; a < 2u; ++a) {
        for (unsigned ch = 0; ch < 3u; ++ch) {
            CHECK_EQ(part[a]->reads[INA3221_SHUNT1 + 2u * ch], 100u);
        }
    }
    const sense_cap_arm_t lv = { 0, 300000, 100000, 50000 };
    CHECK(!sense_sched_cap_arm(&s, &lv));
    /* FAST on a channel whose shunt is 0, on a part not fitted, or past
     * 11: none. */
    m.shunt_uohm[2] = 0u;
    m.fast = 2u;
    start(&m, false, false);
    CHECK_EQ(s.cfg.i3221.fast, SENSE_FAST_NONE);
    CHECK_EQ(s.n_others, 5u);
    m.fast = (uint8_t)SENSE_CH(3u, 1u);
    start(&m, false, false);
    CHECK_EQ(s.cfg.i3221.fast, SENSE_FAST_NONE);
    m.fast = 12u;
    start(&m, false, false);
    CHECK_EQ(s.cfg.i3221.fast, SENSE_FAST_NONE);
}

/* ------------------------------------------------------- parts missing */

TEST_CASE(parts_are_probed_one_a_tick_and_a_missing_one_leaves_the_rest)
{
    bus(0x0Bu);                             /* 0x42 is not on the bus */
    const sense_i3221_cfg_t m = map_all(0x0Fu, 0u);
    start(&m, false, false);
    tick();
    CHECK_EQ(ina3221_state(&s.i3221[0]), SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_state(&s.i3221[1]), SENSE_PART_UNPROBED);
    tick();
    CHECK_EQ(ina3221_state(&s.i3221[1]), SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_state(&s.i3221[2]), SENSE_PART_UNPROBED);
    tick();
    CHECK_EQ(ina3221_state(&s.i3221[2]), SENSE_PART_ABSENT);
    CHECK_EQ(ina3221_state(&s.i3221[3]), SENSE_PART_UNPROBED);
    tick();
    CHECK_EQ(ina3221_state(&s.i3221[3]), SENSE_PART_ONLINE);
    ticks(200u);
    sense_window_t w;
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        CHECK(sense_sched_window(&s, SENSE_CH(a, 1u), &w));
        if (a == 2u) {
            CHECK_EQ(w.n_i + w.n_v, 0u);
        } else {
            CHECK(w.n_i > 0u && w.n_v > 0u);
        }
    }
    /* Online mid-rotation: 0x41 lost, three reads on. */
    part[1]->present = false;
    ticks(100u);
    CHECK(ina3221_state(&s.i3221[1]) != SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_state(&s.i3221[0]), SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_state(&s.i3221[3]), SENSE_PART_ONLINE);
    CHECK(sense_sched_window(&s, SENSE_CH(1u, 2u), &w));
    CHECK_EQ(w.n_i + w.n_v, 0u);
    CHECK(sense_sched_window(&s, SENSE_CH(3u, 2u), &w));
    CHECK(w.n_i >= 4u);
    /* And back after SENSE_RETRY_MS. */
    part[1]->present = true;
    ticks(SENSE_RETRY_MS + 100u);
    CHECK_EQ(ina3221_state(&s.i3221[1]), SENSE_PART_ONLINE);
}

TEST_CASE(a_reset_part_is_found_within_a_rotation_and_alone_emptied)
{
    bus(0x0Fu);
    const sense_i3221_cfg_t m = map_all(0x0Fu, 0u);
    start(&m, false, false);
    ticks(30u);
    CHECK(all_online());
    /* 0x43 resets itself: its Configuration reads the power-on value. */
    part[3]->reg[INA3221_CONFIG] = INA3221_CONFIG_RESET;
    unsigned found = 0u;
    for (unsigned k = 0; k < 41u && found == 0u; ++k) {
        tick();
        if (ina3221_state(&s.i3221[3]) != SENSE_PART_ONLINE) {
            found = k + 1u;
        }
    }
    CHECK(found > 0u && found <= 40u);
    CHECK_EQ(s.i3221_resets[3], 1u);
    CHECK_EQ(s.i3221_resets[0] + s.i3221_resets[1] + s.i3221_resets[2], 0u);
    CHECK_EQ(s.acc[SENSE_CH(3u, 1u)].n_i + s.acc[SENSE_CH(3u, 2u)].n_i
             + s.acc[SENSE_CH(3u, 3u)].n_i, 0u);
    CHECK(s.acc[0].n_i > 0u || s.acc[1].n_i > 0u);
    ticks(SENSE_RETRY_MS + 50u);
    CHECK(all_online());
}

/* ------------------------------------------------------------- service */

static unsigned g_opens;

static bool svc_open(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    ++g_opens;
    return true;
}

static void svc_close(void *ctx)
{
    (void)ctx;
}

static sense_err_t svc_ask(void *ctx, uint8_t addr)
{
    (void)ctx;
    return (fake_find(&fb, addr) != NULL) ? SENSE_OK : SENSE_NACK;
}

TEST_CASE(the_snapshot_carries_four_parts)
{
    bus(0x0Du);                             /* 0x41 missing */
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &fb }, now_us, NULL, NULL },
        .open = svc_open, .close = svc_close, .ask = svc_ask,
    };
    static sense_svc_t v;
    sense_svc_init(&v, &io);
    sense_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cfg_gen = 1u;
    cmd.sda = 16u;
    cmd.scl = 17u;
    cmd.parts.i3221 = map_all(0x0Fu, (uint8_t)SENSE_CH(2u, 1u));
    sense_snap_t snap;
    g_us = 10000000u;
    for (unsigned k = 0; k < 120u; ++k) {
        sense_svc_step(&v, &cmd, &snap);
        g_us += 1000u;
    }
    CHECK(snap.open);
    CHECK_EQ(snap.fast, (uint8_t)SENSE_CH(2u, 1u));
    CHECK_EQ(snap.i3221[0], SENSE_PART_ONLINE);
    CHECK_EQ(snap.i3221[1], SENSE_PART_ABSENT);
    CHECK_EQ(snap.i3221[2], SENSE_PART_ONLINE);
    CHECK_EQ(snap.i3221[3], SENSE_PART_ONLINE);
    CHECK_EQ(snap.i3221_die[3], 0x3220u);
    CHECK_EQ(snap.i3221_maker[2], 0x5449u);
    /* PRESENT: the three parts and the INA228 at 0x45, which answers the
     * scan though not enabled; the encoder lies outside it. */
    CHECK_EQ(snap.present, 0x002Du);
    CHECK(snap.have_win);
    CHECK_EQ(snap.win[SENSE_CH(2u, 1u)].n_i, 50u);
    CHECK_EQ(snap.win[SENSE_CH(2u, 1u)].i_mean_ua, 210000);
    CHECK(snap.win[SENSE_CH(3u, 3u)].n_i >= 5u);
    CHECK_EQ(snap.win[SENSE_CH(1u, 1u)].n_i, 0u);
    CHECK(snap.ring[0].closed && snap.ring[0].n_i == 50u);
    /* Nothing fitted: the bus closes, no FAST. */
    cmd.cfg_gen = 2u;
    memset(&cmd.parts.i3221, 0, sizeof(cmd.parts.i3221));
    sense_svc_step(&v, &cmd, &snap);
    CHECK(!snap.open);
    CHECK_EQ(snap.fast, SENSE_FAST_NONE);
    CHECK_EQ(snap.i3221[2], SENSE_PART_UNSET);
}

/* ---------------------------------------------------------------- page */

TEST_CASE(the_sense_page_sets_up_its_part_as_the_map_and_shows_it)
{
    sense_page_t pg;
    sense_page_init(&pg);
    pg.sense[LINK_SN_ENABLE] = LINK_SN_EN_I3221;
    pg.sense[LINK_SN_I3221_ADDR] = 0x42u;
    pg.sense[LINK_SN_I3221_SHUNT_DMOHM] = 500u;        /* 50mOhm */
    pg.sense[LINK_SN_I3221_CHANNELS] = 0x06u;          /* CH2, CH3 */
    sense_cmd_t c;
    memset(&c, 0, sizeof(c));
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.parts.i3221.fitted, 0x04u);
    for (unsigned k = 0; k < SENSE_CHANNELS; ++k) {
        const bool on = k == SENSE_CH(2u, 2u) || k == SENSE_CH(2u, 3u);
        CHECK_EQ(c.parts.i3221.shunt_uohm[k], on ? 50000u : 0u);
    }
    CHECK_EQ(c.parts.i3221.fast, SENSE_FAST_NONE);     /* CH1 is off */
    pg.sense[LINK_SN_I3221_CHANNELS] = 0x07u;
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.parts.i3221.fast, (uint8_t)SENSE_CH(2u, 1u));
    pg.sense[LINK_SN_ENABLE] = 0u;
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.parts.i3221.fitted, 0u);
    CHECK_EQ(c.parts.i3221.fast, SENSE_FAST_NONE);
    /* An address past 0x43 fits nothing. */
    sense_i3221_one(&c.parts.i3221, true, 0x44u, UOHM, 7u);
    CHECK_EQ(c.parts.i3221.fitted, 0u);
    CHECK_EQ(c.parts.i3221.shunt_uohm[0], 0u);
    CHECK_EQ(c.parts.i3221.fast, SENSE_FAST_NONE);

    /* What the page shows is the part SENSE names, 0x42, whatever the
     * others read. */
    pg.sense[LINK_SN_ENABLE] = LINK_SN_EN_I3221;
    sense_snap_t sn;
    memset(&sn, 0, sizeof(sn));
    sn.cfg_gen = pg.cfg_gen;
    sn.open = true;
    sn.have_win = true;
    sn.i3221[0] = SENSE_PART_ONLINE;
    sn.i3221_maker[0] = 0x5449u;
    sn.i3221_die[0] = 0x3220u;
    sn.i3221_resets[0] = 9u;
    sn.i3221[2] = SENSE_PART_WRONG_ID;
    sn.i3221_maker[2] = 0x1408u;
    sn.i3221_die[2] = 0x0000u;
    sn.i3221_resets[2] = 3u;
    for (unsigned k = 0; k < SENSE_SRC_COUNT; ++k) {
        sn.win[k].number = 77u;
        sn.win[k].n_i = 1u;
        sn.win[k].i_mean_ua = (int32_t)(1000u * (k + 1u));
    }
    sense_page_publish(&pg, &sn, 0u);
    CHECK_EQ(pg.sense[LINK_SN_FLAGS] & (LINK_SN_I3221_ONLINE
                                        | LINK_SN_I3221_ID_OK
                                        | LINK_SN_I3221_ID_WRONG),
             (uint16_t)LINK_SN_I3221_ID_WRONG);
    CHECK_EQ(pg.sense[LINK_SN_I3221_ID], 0u);
    CHECK_EQ(LINK_SN_RESETS_I3221(pg.sense[LINK_SN_RESETS]), 3u);
    for (unsigned ch = 1u; ch <= 3u; ++ch) {
        CHECK_EQ(pg.servo[(ch - 1u) * LINK_SS_CH_STRIDE + LINK_SS_CH_MEAN_MA],
                 (uint16_t)(SENSE_CH(2u, ch) + 1u));
    }
    CHECK_EQ(pg.servo[LINK_SS_WINDOW], 77u);
}

int main(void)
{
    RUN(twelve_channels_are_read_at_their_rates);
    RUN(every_tick_with_twelve_channels_fits_a_millisecond);
    RUN(windows_of_none_one_and_twelve_channels);
    RUN(each_channel_has_its_own_shunt);
    RUN(fast_on_another_part_carries_the_ring_and_the_capture);
    RUN(fast_none_or_on_a_channel_off_reads_no_channel_every_tick);
    RUN(parts_are_probed_one_a_tick_and_a_missing_one_leaves_the_rest);
    RUN(a_reset_part_is_found_within_a_rotation_and_alone_emptied);
    RUN(the_snapshot_carries_four_parts);
    RUN(the_sense_page_sets_up_its_part_as_the_map_and_shows_it);
    return test_summary("sense_parts");
}
