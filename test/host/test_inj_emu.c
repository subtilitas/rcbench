/*
 * The touch injector's core (testbench/touch-injector/core): the GT911
 * register file and the command and gesture engine, driven as the RP2350
 * build drives them.  A simulated panel reads the emulator every 10 ms
 * exactly as firmware/panel/components/gt911/gt911.c does: the status
 * byte, the points when the status says ready, then the acknowledge.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "injector.h"

static injector_t inj;
static uint32_t   now;

/* What the simulated panel saw: one entry per frame it read. */
typedef struct {
    uint32_t t;
    uint8_t  n;
    uint16_t x, y;
    uint8_t  raw[8];
} seen_t;

static seen_t   seen[4096];
static int      nseen;
static bool     panel_on;
static uint32_t panel_period = 10;

/* -------------------------------------------------------- a GT911 master */

static void m_write(uint16_t reg, const uint8_t *data, int n)
{
    gt911_emu_rx(&inj.emu, (uint8_t)(reg >> 8));
    gt911_emu_rx(&inj.emu, (uint8_t)(reg & 0xFFu));
    for (int i = 0; i < n; ++i) {
        gt911_emu_rx(&inj.emu, data[i]);
    }
    gt911_emu_finish(&inj.emu);
}

static void m_read(uint16_t reg, uint8_t *out, int n)
{
    gt911_emu_rx(&inj.emu, (uint8_t)(reg >> 8));
    gt911_emu_rx(&inj.emu, (uint8_t)(reg & 0xFFu));
    gt911_emu_finish(&inj.emu);   /* the repeated START */
    for (int i = 0; i < n; ++i) {
        out[i] = gt911_emu_tx(&inj.emu);
    }
    gt911_emu_finish(&inj.emu);
}

/* gt911_read(): status, points if ready, acknowledge. */
static void panel_poll(void)
{
    uint8_t st = 0;
    m_read(GT911_EMU_REG_STATUS, &st, 1);
    if ((st & 0x80u) == 0u) {
        return;
    }
    const int n = st & 0x0F;
    seen_t *s = &seen[nseen < 4095 ? nseen++ : nseen];
    memset(s, 0, sizeof(*s));
    s->t = now;
    s->n = (uint8_t)n;
    if (n > 0) {
        m_read(GT911_EMU_REG_POINT1, s->raw, 8);
        s->x = (uint16_t)(s->raw[1] | (s->raw[2] << 8));
        s->y = (uint16_t)(s->raw[3] | (s->raw[4] << 8));
    }
    const uint8_t zero = 0;
    m_write(GT911_EMU_REG_STATUS, &zero, 1);
}

/* ------------------------------------------------------------ the host */

static char replies[64][INJ_REPLY_LEN];
static int  nreplies;

static void drain(void)
{
    char line[INJ_REPLY_LEN];
    while (injector_take_reply(&inj, line, sizeof(line))) {
        if (nreplies < 64) {
            snprintf(replies[nreplies++], INJ_REPLY_LEN, "%s", line);
        }
    }
}

static void send(const char *line)
{
    injector_input(&inj, line, strlen(line), now);
    injector_input(&inj, "\n", 1, now);
    drain();
}

static const char *last(void)
{
    return nreplies > 0 ? replies[nreplies - 1] : "";
}

/* Run @p ms milliseconds: the injector every 1 ms, the panel every
 * panel_period ms while it is on. */
static void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        ++now;
        if (panel_on && now % panel_period == 0u) {
            panel_poll();
        }
        injector_poll(&inj, now);
        drain();
    }
}

/* Run until a reply starts with @p prefix, for at most @p ms. */
static bool run_until(const char *prefix, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        for (int k = 0; k < nreplies; ++k) {
            if (strncmp(replies[k], prefix, strlen(prefix)) == 0) {
                return true;
            }
        }
        run(1);
    }
    return false;
}

static void fresh(uint32_t t0)
{
    injector_init(&inj);
    now = t0;
    nseen = 0;
    nreplies = 0;
    panel_on = false;
    panel_period = 10;
}

static void confirm_ok(void)
{
    inj_line_result_t r = { .verdict = INJ_WIRING_OK, .tx = 50,
                            .agree_pct = 100, .end_state = 3,
                            .rate_ksps = 24500 };
    send("wiring");
    CHECK(injector_take_wiring(&inj));
    injector_wiring_done(&inj, &r);
    drain();
    send("confirm scl=GP5");
    CHECK_STR_EQ(last(), "ok confirm");
    CHECK(injector_take_attach(&inj));
    nreplies = 0;
}

/* An active injector with the panel polling every 10 ms. */
static void active(uint32_t t0)
{
    fresh(t0);
    confirm_ok();
    panel_on = true;
}

/* ------------------------------------------------------- the register file */

TEST_CASE(the_register_file_answers_as_a_gt911)
{
    fresh(0);
    uint8_t id[6] = { 0 };
    m_read(GT911_EMU_REG_PRODUCT, id, 6);
    CHECK_EQ(id[0], '9');
    CHECK_EQ(id[1], '1');
    CHECK_EQ(id[2], '1');
    CHECK_EQ(id[3], 0);
    CHECK_EQ(id[4], 0x01);
    CHECK_EQ(id[5], 0xE0);
    CHECK_EQ(inj.emu.product_reads, 1);

    uint8_t res[4] = { 0 };
    m_read(0x8048, res, 4);
    CHECK_EQ(res[0] | (res[1] << 8), 800);
    CHECK_EQ(res[2] | (res[3] << 8), 480);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8146) | (gt911_emu_peek(&inj.emu, 0x8147) << 8), 800);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8148) | (gt911_emu_peek(&inj.emu, 0x8149) << 8), 480);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8047), 0x41);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x804C), 5);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8157), 0);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0xFFFF), 0);

    /* The command registers keep what was written; others ignore it. */
    const uint8_t cmd[2] = { 0xAA, 0x55 };
    m_write(0x8040, cmd, 2);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8040), 0xAA);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8041), 0x55);
    const uint8_t junk = 0x77;
    m_write(0x8047, &junk, 1);
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8047), 0x41);

    /* A read with no pointer write continues from the pointer. */
    m_read(0x8140, id, 2);
    id[0] = gt911_emu_tx(&inj.emu);
    gt911_emu_finish(&inj.emu);
    CHECK_EQ(id[0], '1');
}

TEST_CASE(a_frame_waits_for_its_acknowledge)
{
    fresh(0);
    const gt911_emu_frame_t down = { .down = true, .x = 83, .y = 370 };
    const gt911_emu_frame_t up = { .down = false };
    CHECK(gt911_emu_publish(&inj.emu, &down));
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0x81);
    /* Not acknowledged: the next is refused. */
    CHECK(!gt911_emu_publish(&inj.emu, &up));

    uint8_t p[8];
    m_read(GT911_EMU_REG_POINT1, p, 8);
    const uint8_t want[8] = { 0x00, 0x53, 0x00, 0x72, 0x01, 0x18, 0x00, 0x00 };
    CHECK(memcmp(p, want, 8) == 0);

    /* Any value written to the status acknowledges. */
    const uint8_t v = 0x5A;
    m_write(GT911_EMU_REG_STATUS, &v, 1);
    CHECK_EQ(inj.emu.acks, 1);
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0x00);
    CHECK(gt911_emu_publish(&inj.emu, &up));
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0x80);
}

TEST_CASE(nothing_is_published_or_withdrawn_mid_transfer)
{
    fresh(0);
    const gt911_emu_frame_t down = { .down = true, .x = 1, .y = 2 };
    gt911_emu_rx(&inj.emu, 0x81);   /* a transfer has started */
    CHECK(inj.emu.busy);
    CHECK(!gt911_emu_publish(&inj.emu, &down));
    CHECK(!gt911_emu_withdraw(&inj.emu));
    gt911_emu_finish(&inj.emu);
    CHECK(gt911_emu_publish(&inj.emu, &down));
    CHECK(gt911_emu_withdraw(&inj.emu));
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
}

/* ------------------------------------------------------------- LISTEN */

TEST_CASE(a_reset_listens_and_refuses_gestures)
{
    fresh(0);
    send("ping");
    CHECK_STR_EQ(last(), "pong rcbench-touch 1 addr=0x14 res=800x480 state=listen");
    send("tap 10 10");
    CHECK_STR_EQ(last(), "err unconfirmed");
    send("hold 10 10 500");
    CHECK_STR_EQ(last(), "err unconfirmed");
    send("swipe 0 0 10 10 100");
    CHECK_STR_EQ(last(), "err unconfirmed");
    send("status");
    CHECK_STR_EQ(last(), "status listen reads=0 acks=0 last_read_ms=none down=0 x=0 y=0");
    CHECK(!injector_take_attach(&inj));
    CHECK_EQ(injector_led(&inj, 0), INJ_LED_LISTEN);
    /* Releasing nothing is fine. */
    send("release");
    CHECK_STR_EQ(last(), "ok release");
}

TEST_CASE(confirm_needs_an_ok_verdict)
{
    fresh(0);
    send("confirm scl=GP5");
    CHECK_STR_EQ(last(), "err noverdict");
    send("confirm scl=GP4");
    CHECK_STR_EQ(last(), "err syntax confirm");
    send("confirm");
    CHECK_STR_EQ(last(), "err syntax confirm");

    const inj_wiring_t bad[] = { INJ_WIRING_IDLE, INJ_WIRING_LOW,
                                 INJ_WIRING_NOISE, INJ_WIRING_SWAPPED };
    const char *want[] = { "err noverdict", "err noverdict", "err noverdict",
                           "err swapped" };
    for (int i = 0; i < 4; ++i) {
        send("wiring");
        CHECK_STR_EQ(last(), "ok wiring");
        CHECK(injector_take_wiring(&inj));
        CHECK(!injector_take_wiring(&inj));   /* once */
        /* A confirm while the check runs is refused. */
        send("confirm scl=GP5");
        CHECK_STR_EQ(last(), "err busy");
        send("wiring");
        CHECK_STR_EQ(last(), "err busy");
        const inj_line_result_t r = { .verdict = bad[i], .tx = 3,
                                      .end_state = 1 };
        injector_wiring_done(&inj, &r);
        drain();
        send("confirm scl=GP5");
        CHECK_STR_EQ(last(), want[i]);
        CHECK(!injector_take_attach(&inj));
        if (bad[i] != INJ_WIRING_IDLE) {
            CHECK_EQ(injector_led(&inj, 0), INJ_LED_WIRING_BAD);
        }
    }
    confirm_ok();
    send("ping");
    CHECK_STR_EQ(last(), "pong rcbench-touch 1 addr=0x14 res=800x480 state=active");
    send("confirm scl=GP5");
    CHECK_STR_EQ(last(), "err state");
    send("wiring");
    CHECK_STR_EQ(last(), "err state");

    /* A reset forgets the confirmation. */
    injector_init(&inj);
    send("tap 1 1");
    CHECK_STR_EQ(last(), "err unconfirmed");
}

TEST_CASE(the_wiring_verdicts_read_as_documented)
{
    fresh(0);
    struct {
        inj_line_result_t r;
        const char *line;
    } cases[] = {
        { { INJ_WIRING_OK, 63, 100, 1000, 1500, 3, 24500, 58 },
          "wiring ok scl=GP5 sda=GP4 tx=63 named=58 agree=100 rate=24.5" },
        { { INJ_WIRING_SWAPPED, 40, 97, 1, 1, 3, 9999, 40 },
          "wiring swapped scl=GP4 sda=GP5 tx=40 named=40 agree=97"
          " rate=9.9" },
        { { INJ_WIRING_IDLE, 0, 0, 0, 0, 3, 24000, 0 },
          "wiring idle gp4=1 gp5=1" },
        { { INJ_WIRING_LOW, 0, 0, 0, 0, 1, 24000, 0 },
          "wiring low gp4=1 gp5=0" },
        { { INJ_WIRING_NOISE, 12, 50, 300, 280, 3, 24000, 10 },
          "wiring noise edges4=300 edges5=280 tx=12 named=10 agree=50" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        send("wiring");
        (void)injector_take_wiring(&inj);
        injector_wiring_done(&inj, &cases[i].r);
        drain();
        CHECK_STR_EQ(last(), cases[i].line);
    }
}

/* ------------------------------------------------------------ gestures */

/* The SETUP tile: a tap at (83, 370) reaches the panel as these bytes. */
TEST_CASE(a_tap_presses_for_its_length_and_releases)
{
    active(1000);
    send("tap 83 370");
    CHECK_STR_EQ(last(), "ok tap");
    CHECK(run_until("done tap", 400));
    CHECK_EQ(nseen, 2);   /* one frame puts it down, one lifts it */
    const uint8_t want[8] = { 0x00, 0x53, 0x00, 0x72, 0x01, 0x18, 0x00, 0x00 };
    CHECK(memcmp(seen[0].raw, want, 8) == 0);
    /* Every frame but the last is the contact; the last lifts it. */
    for (int i = 0; i + 1 < nseen; ++i) {
        CHECK_EQ(seen[i].n, 1);
    }
    CHECK_EQ(seen[nseen - 1].n, 0);
    /* Down from the first read to the release read: 100 ms to 110 ms. */
    const uint32_t held = seen[nseen - 1].t - seen[0].t;
    CHECK(held >= 100u && held <= 110u);

    unsigned frames = 0, ms = 0;
    CHECK_EQ(sscanf(last(), "done tap frames=%u ms=%u", &frames, &ms), 2);
    CHECK_EQ(frames, (unsigned)nseen);
    CHECK(ms >= 100u && ms <= 110u);
    send("status");
    CHECK(strncmp(last(), "status idle", 11) == 0);
}

TEST_CASE(a_hold_lasts_its_ms_on_the_panel)
{
    const uint32_t lens[] = { 1, 1900, 2100, 30000 };
    for (size_t k = 0; k < 4; ++k) {
        active(5000);
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "hold 400 240 %lu", (unsigned long)lens[k]);
        send(cmd);
        CHECK_STR_EQ(last(), "ok hold");
        run(lens[k] / 2u);
        send("status");
        if (lens[k] > 20u) {
            CHECK(strstr(last(), "status hold") != NULL);
            CHECK(strstr(last(), "down=1 x=400 y=240") != NULL);
            CHECK_EQ(injector_led(&inj, now), INJ_LED_DOWN);
        }
        CHECK(run_until("done hold", lens[k] + 100u));
        const uint32_t held = seen[nseen - 1].t - seen[0].t;
        /* One poll at least, and never more than a poll late. */
        const uint32_t floor = lens[k] < 10u ? 10u : lens[k];
        if (held < floor || held > floor + 10u) {
            T_FAIL("hold %lu held %lu ms", (unsigned long)lens[k],
                   (unsigned long)held);
        }
    }
}

TEST_CASE(gestures_check_every_field_at_its_limits)
{
    active(0);
    struct {
        const char *cmd;
        const char *reply;
    } cases[] = {
        { "tap 800 0", "err range x" },
        { "tap 0 480", "err range y" },
        { "tap -1 0", "err range x" },
        { "tap 0 0 0", "err range ms" },
        { "tap 0 0 30001", "err range ms" },
        { "tap 0 0 99999999999", "err range ms" },
        { "hold 0 0 30001", "err range ms" },
        { "hold 0 0", "err syntax hold" },
        { "tap 0", "err syntax tap" },
        { "tap 0 0 1 2", "err syntax tap" },
        { "tap x 0", "err syntax tap" },
        { "tap 1.5 0", "err syntax tap" },
        { "tap - 0", "err syntax tap" },
        { "swipe 0 0 800 0 10", "err range x" },
        { "swipe 0 0 0 480 10", "err range y" },
        { "swipe 0 0 1 1", "err syntax swipe" },
        { "swipe 0 0 1 1 1 1", "err syntax swipe" },
        { "release now", "err syntax release" },
        { "wiring now", "err syntax wiring" },
        { "wiring", "err state" },
        { "jump 1 2", "err unknown jump" },
        { "a b c d e f g h", "err syntax a" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        send(cases[i].cmd);
        if (strcmp(last(), cases[i].reply) != 0) {
            T_FAIL("%s: got \"%s\", want \"%s\"", cases[i].cmd, last(),
                   cases[i].reply);
        }
    }
    /* The far corners and the longest gesture are taken. */
    send("tap 799 479 30000");
    CHECK_STR_EQ(last(), "ok tap");
    send("release");
    send("swipe 0 0 799 479 1");
    CHECK_STR_EQ(last(), "ok swipe");
}

TEST_CASE(lines_are_bounded_and_crlf_is_taken)
{
    active(0);
    char line[80];
    /* 63 characters run, 64 do not. */
    memset(line, ' ', sizeof(line));
    memcpy(line, "ping", 4);
    line[63] = '\0';
    send(line);
    CHECK(strncmp(last(), "pong", 4) == 0);
    memset(line, ' ', sizeof(line));
    memcpy(line, "ping", 4);
    line[64] = '\0';
    send(line);
    CHECK_STR_EQ(last(), "err toolong");
    /* The overflow is dropped up to the LF, and the next line is clean. */
    injector_input(&inj, "ping\r\n", 6, now);
    drain();
    CHECK(strncmp(last(), "pong", 4) == 0);
    const int before = nreplies;
    send("");
    send("   ");
    CHECK_EQ(nreplies, before);
}

TEST_CASE(one_gesture_at_a_time)
{
    active(0);
    send("hold 10 10 500");
    send("tap 20 20");
    CHECK_STR_EQ(last(), "err busy");
    send("swipe 0 0 1 1 10");
    CHECK_STR_EQ(last(), "err busy");
    send("status");
    CHECK(strncmp(last(), "status hold", 11) == 0);
    send("ping");
    CHECK(strncmp(last(), "pong", 4) == 0);
    CHECK(run_until("done hold", 700));
    send("tap 20 20");
    CHECK_STR_EQ(last(), "ok tap");
}

/* A swipe never moves more than 100 px between two frames the panel reads,
 * so the panel's 120 px jump split never cuts it. */
TEST_CASE(a_fast_swipe_is_slowed_under_the_jump_split)
{
    active(0);
    send("swipe 0 100 700 400 50");
    CHECK(run_until("done swipe", 2000));
    CHECK(strstr(last(), "slowed=1") != NULL);
    int downs = 0;
    for (int i = 0; i + 1 < nseen; ++i) {
        CHECK_EQ(seen[i].n, 1);
        ++downs;
        if (i > 0) {
            const int d = abs((int)seen[i].x - (int)seen[i - 1].x)
                          + abs((int)seen[i].y - (int)seen[i - 1].y);
            if (d > 100) {
                T_FAIL("frame %d moved %d px", i, d);
            }
        }
    }
    CHECK(downs >= 10);
    CHECK_EQ(seen[0].x, 0);
    CHECK_EQ(seen[0].y, 100);
    CHECK_EQ(seen[nseen - 2].x, 700);
    CHECK_EQ(seen[nseen - 2].y, 400);
    CHECK_EQ(seen[nseen - 1].n, 0);
}

TEST_CASE(a_slow_swipe_follows_the_clock)
{
    active(0);
    send("swipe 100 200 300 200 1000");
    CHECK(run_until("done swipe", 2000));
    CHECK(strstr(last(), "slowed=0") != NULL);
    unsigned frames = 0, ms = 0, slowed = 9;
    CHECK_EQ(sscanf(last(), "done swipe frames=%u ms=%u slowed=%u", &frames,
                    &ms, &slowed), 3);
    CHECK(ms >= 1000u && ms <= 1030u);
    /* Monotonic, and at the halfway frame about halfway. */
    for (int i = 1; i + 1 < nseen; ++i) {
        CHECK(seen[i].x >= seen[i - 1].x);
    }
    CHECK_EQ(seen[nseen - 2].x, 300);
}

/* ------------------------------------------------------------- aborts */

TEST_CASE(no_panel_reading_fails_the_gesture_after_100_ms)
{
    active(0);
    panel_on = false;
    send("tap 10 10");
    run(99);
    CHECK_EQ(nreplies, 1);
    run(2);
    CHECK_STR_EQ(last(), "fail tap no_master");
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
    CHECK_EQ(injector_led(&inj, now), INJ_LED_FAILED);
    send("status");
    CHECK(strncmp(last(), "status idle", 11) == 0);
    /* A new command clears the red. */
    send("tap 10 10");
    CHECK_STR_EQ(last(), "ok tap");
    CHECK(injector_led(&inj, now) != INJ_LED_FAILED);
}

TEST_CASE(a_panel_that_stops_reading_mid_hold_fails_it)
{
    active(0);
    send("hold 10 10 2000");
    run(500);
    panel_on = false;
    run(150);
    CHECK_STR_EQ(last(), "fail hold no_master");
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
}

/* A panel that starts again reads the product ID; it must not boot into a
 * contact that is already down. */
TEST_CASE(a_panel_restart_aborts_the_gesture)
{
    active(0);
    send("hold 300 300 5000");
    run(200);
    uint8_t id[6];
    m_read(GT911_EMU_REG_PRODUCT, id, 6);
    run(1);
    CHECK_STR_EQ(last(), "fail hold panel_reset");
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
    const int before = nseen;
    run(100);
    CHECK_EQ(nseen, before);   /* nothing more published */
}

/* The restart read arrives while a transfer is still open: the frame is
 * withdrawn as soon as it closes. */
TEST_CASE(a_restart_mid_transfer_withdraws_when_it_closes)
{
    active(0);
    send("hold 300 300 5000");
    run(15);
    gt911_emu_rx(&inj.emu, 0x81);
    gt911_emu_rx(&inj.emu, 0x40);
    gt911_emu_finish(&inj.emu);
    (void)gt911_emu_tx(&inj.emu);   /* reading 0x8140, still open */
    injector_poll(&inj, ++now);
    drain();
    CHECK_STR_EQ(last(), "fail hold panel_reset");
    gt911_emu_finish(&inj.emu);
    injector_poll(&inj, ++now);
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
}

TEST_CASE(release_preempts_and_lifts_the_contact)
{
    active(0);
    send("hold 50 60 5000");
    run(300);
    send("release");
    CHECK_STR_EQ(replies[nreplies - 2], "fail hold released");
    CHECK_STR_EQ(last(), "ok release");
    const int n_before = nreplies;
    run(50);
    CHECK_EQ(seen[nseen - 1].n, 0);       /* the panel saw the lift */
    CHECK_EQ(nreplies, n_before);         /* and nothing more is said */
    send("status");
    CHECK(strncmp(last(), "status idle", 11) == 0);
    send("tap 1 1");
    CHECK_STR_EQ(last(), "ok tap");
}

TEST_CASE(release_before_the_first_frame_publishes_nothing)
{
    active(0);
    panel_on = false;
    send("tap 5 5");
    send("release");
    CHECK_STR_EQ(last(), "ok release");
    injector_poll(&inj, now);
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
}

TEST_CASE(a_closed_port_lifts_the_contact_silently)
{
    active(0);
    send("hold 50 60 5000");
    run(300);
    nreplies = 0;
    injector_host_closed(&inj);
    run(50);
    CHECK_EQ(nreplies, 0);
    CHECK_EQ(seen[nseen - 1].n, 0);
    CHECK_EQ(seen[nseen - 2].n, 1);
}

/* The release frame itself not read: the release ends after 100 ms and
 * the frame is withdrawn. */
TEST_CASE(an_unread_release_frame_is_withdrawn)
{
    active(0);
    send("hold 50 60 5000");
    run(300);
    panel_on = false;
    run(150);   /* the hold fails no_master */
    CHECK_STR_EQ(last(), "fail hold no_master");
    panel_on = true;
    send("hold 50 60 5000");
    run(300);
    panel_on = false;
    send("release");
    run(150);
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
    send("status");
    CHECK(strncmp(last(), "status idle", 11) == 0);
}

/* ---------------------------------------------------------- the rest */

TEST_CASE(status_reports_the_reads)
{
    active(0);
    run(95);
    send("status");
    CHECK_STR_EQ(last(), "status idle reads=9 acks=0 last_read_ms=5 down=0 x=0 y=0");
    CHECK_EQ(injector_led(&inj, now), INJ_LED_POLLED);
    panel_on = false;
    run(200);
    CHECK_EQ(injector_led(&inj, now), INJ_LED_OFF);
}

TEST_CASE(a_full_reply_queue_keeps_the_newest)
{
    fresh(0);
    for (int i = 0; i < 12; ++i) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "w%d", i);
        injector_input(&inj, cmd, strlen(cmd), 0);
        injector_input(&inj, "\n", 1, 0);
    }
    char line[INJ_REPLY_LEN];
    int n = 0;
    char first[INJ_REPLY_LEN] = "";
    while (injector_take_reply(&inj, line, sizeof(line))) {
        if (n++ == 0) {
            snprintf(first, sizeof(first), "%s", line);
        }
    }
    CHECK_EQ(n, (int)INJ_REPLIES);
    CHECK_STR_EQ(first, "err unknown w4");
    CHECK_STR_EQ(line, "err unknown w11");
}

/* The millisecond clock wraps after 49.7 days; a gesture across the wrap
 * keeps its length. */
TEST_CASE(a_gesture_across_the_clock_wrap)
{
    active(UINT32_MAX - 150u);
    send("hold 10 10 300");
    CHECK(run_until("done hold", 600));
    unsigned frames = 0, ms = 0;
    CHECK_EQ(sscanf(last(), "done hold frames=%u ms=%u", &frames, &ms), 2);
    if (ms < 300u || ms > 310u) {
        T_FAIL("%s", last());
    }
}

TEST_CASE(gesture_names)
{
    CHECK_STR_EQ(inj_gesture_name(INJ_G_TAP), "tap");
    CHECK_STR_EQ(inj_gesture_name(INJ_G_HOLD), "hold");
    CHECK_STR_EQ(inj_gesture_name(INJ_G_SWIPE), "swipe");
    CHECK_STR_EQ(inj_gesture_name(INJ_G_RELEASE), "release");
    CHECK_STR_EQ(inj_gesture_name(INJ_G_NONE), "idle");
}

int main(void)
{
    RUN(the_register_file_answers_as_a_gt911);
    RUN(a_frame_waits_for_its_acknowledge);
    RUN(nothing_is_published_or_withdrawn_mid_transfer);
    RUN(a_reset_listens_and_refuses_gestures);
    RUN(confirm_needs_an_ok_verdict);
    RUN(the_wiring_verdicts_read_as_documented);
    RUN(a_tap_presses_for_its_length_and_releases);
    RUN(a_hold_lasts_its_ms_on_the_panel);
    RUN(gestures_check_every_field_at_its_limits);
    RUN(lines_are_bounded_and_crlf_is_taken);
    RUN(one_gesture_at_a_time);
    RUN(a_fast_swipe_is_slowed_under_the_jump_split);
    RUN(a_slow_swipe_follows_the_clock);
    RUN(no_panel_reading_fails_the_gesture_after_100_ms);
    RUN(a_panel_that_stops_reading_mid_hold_fails_it);
    RUN(a_panel_restart_aborts_the_gesture);
    RUN(a_restart_mid_transfer_withdraws_when_it_closes);
    RUN(release_preempts_and_lifts_the_contact);
    RUN(release_before_the_first_frame_publishes_nothing);
    RUN(a_closed_port_lifts_the_contact_silently);
    RUN(an_unread_release_frame_is_withdrawn);
    RUN(status_reports_the_reads);
    RUN(a_full_reply_queue_keeps_the_newest);
    RUN(a_gesture_across_the_clock_wrap);
    RUN(gesture_names);
    return test_summary("inj_emu");
}
