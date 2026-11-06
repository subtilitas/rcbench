/*
 * Screens that repaint a part of themselves for what changed on it.
 *
 * The IMU screen repaints the card lines a new reading changes and the
 * calibration's button while its hold fills; SUPPLY repaints neither
 * PLOT | TABLE nor its heading row for a sample; WI-FI repaints nothing
 * for an idle second inside the minute it shows.  Each is held against
 * the same frames drawn whole: both buffers, pixel for pixel, in every
 * frame.  And each part costs less than the whole: the visits of the
 * rasteriser (gfx_work()) in the frames that repaint a part.  The KST and
 * Hitec pages count a PD mini reading only as their state line writes it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "gfx_work.h"
#include "hitec_panel.h"
#include "imu_screen.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "supply.h"
#include "supply_screen.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "wifi_screen.h"
#include "wifi_view.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
/** @brief The frames of a run: about 2.6s at the panel's 39 frames a
 *         second, past a 2s hold and its flash. */
#define FRAMES 100
/** @brief The frame time, s. */
#define DT 0.026f

static gfx_color_t fb[2][W * H];
static gfx_canvas_t cv[2];

/** @brief What a run of frames feeds. */
typedef enum {
    RUN_IMU,       /**< a new angle frame in every frame */
    RUN_IMU_HOLD,  /**< a finger on CALIBRATE ACCELEROMETER's hold, lifted
                        at frame 60, a reading on show */
    RUN_IMU_CAL,   /**< the same hold to its end and its flash, lifted at
                        frame 95, no reading */
    RUN_SUPPLY,    /**< a sample in every frame, the supply silent for 10 */
    RUN_WIFI,      /**< the idle time 1s shorter in every frame */
} run_t;

/** @brief A frame's picture, as a 64-bit FNV-1a hash of its buffer. */
static uint64_t picture_hash(const gfx_color_t *px)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t *b = (const uint8_t *)px;
    for (size_t i = 0; i < sizeof(fb[0]); ++i) {
        h = (h ^ b[i]) * 1099511628211ull;
    }
    return h;
}

/** @brief The IMU's view of frame @p i: X 0.1° further every frame but
 *         every fifth, which brings the frame's time alone; ZERO taken at
 *         frame 40, the battery lower from frame 50, the reading gone at
 *         frames 60 to 64. */
static imu_view_t imu_view(int i)
{
    imu_view_t v;
    memset(&v, 0, sizeof(v));
    v.link = IMU_LINK_CONNECTED;
    snprintf(v.bound_addr, sizeof(v.bound_addr), "98D3:31:F5A1B2");
    snprintf(v.bound_name, sizeof(v.bound_name), "HC-06");
    v.r.valid = i < 60 || i > 64;
    v.r.taken_ms = 1000u + 20u * (uint32_t)i;
    const int step = i - i / 5;
    v.r.angle[0] = 10.7f + 0.1f * (float)(step % 30);
    v.r.angle[1] = 0.3f;
    v.r.angle[2] = 87.2f;
    v.r.zeroed = i >= 40;
    v.r.rate_dhz = (uint16_t)(500u + (unsigned)(i / 25) * 10u);
    v.r.battery_v = (i < 50) ? 3.95f : 3.91f;
    v.r.temp_c = NAN;
    v.r.frames = 100u + 3u * (uint32_t)i;
    return v;
}

/** @brief The supply's sample of frame @p i: the output on, the current
 *         moving; the supply silent at frames 30 to 39. */
static supply_state_t supply_at(int i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = i < 30 || i > 39;
    st.output = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.mode = SUPPLY_MODE_CV;
    st.v = 6.00f;
    st.i = 0.40f + 0.01f * (float)(i % 13);
    st.p = st.v * st.i;
    st.set_v = 6.0f;
    st.set_i = 2.0f;
    st.samples = (uint16_t)i;
    return st;
}

/** @brief WI-FI's view with the access point up and @p idle_s left. */
static wifi_view_t wifi_at(uint32_t idle_s)
{
    wifi_view_t v;
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    snprintf(v.name, sizeof(v.name), "rcbench-3F7A");
    snprintf(v.pass, sizeof(v.pass), "k7mqx3ha");
    snprintf(v.code, sizeof(v.code), "r4tcw9pe");
    v.radio = WIFI_RADIO_AP;
    v.channel = 6;
    v.idle_left_s = idle_s;
    return v;
}

/** @brief A contact on (@p x, @p y) of the body, through the router. */
static void touch(touch_event_type_t type, int x, int y)
{
    const touch_event_t e = {
        .type = type,
        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)(UI_BAND_H + y),
                   .strength = 40 } };
    ui_router_event(&e);
}

/** @brief The last frame RUN_IMU hands a view in. */
#define IMU_LAST 80

/** @brief Frame @p i's input of @p run. */
static void feed(run_t run, int i)
{
    if (run == RUN_IMU && i <= IMU_LAST) {
        const imu_view_t v = imu_view(i);
        imu_screen_set(&v);
    } else if (run == RUN_SUPPLY) {
        const supply_state_t st = supply_at(i);
        supply_screen_push(&st);
    } else if (run == RUN_WIFI) {
        const wifi_view_t v = wifi_at(605u - (uint32_t)i);
        wifi_screen_set(&v);
    }
}

/**
 * @brief @p run's frames, each drawn into buffer i & 1 the screen's way, or
 *        whole when @p whole.
 * @param run   what is fed
 * @param whole every frame drawn whole
 * @param hash  each frame's picture
 * @param work  each frame's visits
 */
static void draw_run(run_t run, bool whole, uint64_t *hash,
                     unsigned long *work)
{
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    for (int b = 0; b < 2; ++b) {
        gfx_canvas_init(&cv[b], fb[b], W, H, W);
        memset(fb[b], 0, sizeof(fb[b]));
    }
    switch (run) {
    case RUN_IMU:
    case RUN_IMU_HOLD:
    case RUN_IMU_CAL:
        ui_router_goto(SCREEN_IMU);
        break;
    case RUN_SUPPLY:
        ui_router_goto(SCREEN_SUPPLY);
        supply_screen_set_model(true);
        supply_screen_set_output(true);
        break;
    case RUN_WIFI:
        ui_router_goto(SCREEN_WIFI);
        break;
    }
    feed(run, 0);
    const bool hold = run == RUN_IMU_HOLD || run == RUN_IMU_CAL;
    const gfx_rect_t r = imu_screen_hold_rect();
    if (hold) {
        /* RUN_IMU_CAL's view has the reading gone: its age is not shown. */
        const imu_view_t v = imu_view(run == RUN_IMU_CAL ? 60 : 0);
        imu_screen_set(&v);
        touch(TOUCH_EVENT_DOWN, r.x + r.w / 2, r.y + r.h / 2);
    }
    ui_router_render(&cv[0], 0);
    ui_router_render(&cv[1], 1);
    for (int i = 0; i < FRAMES; ++i) {
        feed(run, i + 1);
        if (hold && i == (run == RUN_IMU_CAL ? 95 : 60)) {
            touch(TOUCH_EVENT_UP, r.x + r.w / 2, r.y + r.h / 2);
        }
        ui_router_tick(DT);
        if (whole) {
            ui_router_invalidate();
        }
        const unsigned long was = gfx_work();
        ui_router_render(&cv[i & 1], i & 1);
        work[i] = gfx_work() - was;
        hash[i] = picture_hash(fb[i & 1]);
    }
}

/**
 * @brief Whether @p run drawn the screen's way is the run drawn whole in
 *        each of its first @p upto frames.  A frame that visits under half
 *        the whole frame's pixels repainted a part, and a part visits at
 *        most @p share of them.
 * @param run   what is fed
 * @param upto  the frames compared
 * @param share the most a part may visit, of the whole frame
 * @param parts takes the frames that repainted a part
 * @return true when both hold
 */
static bool same_as_whole(run_t run, int upto, double share, int *parts)
{
    static uint64_t part[FRAMES], full[FRAMES];
    static unsigned long part_work[FRAMES], full_work[FRAMES];
    draw_run(run, false, part, part_work);
    draw_run(run, true, full, full_work);
    *parts = 0;
    for (int i = 0; i < upto; ++i) {
        if (part[i] != full[i]) {
            printf("    run %d differs at frame %d\n", (int)run, i);
            return false;
        }
        const double w = (double)part_work[i], f = (double)full_work[i];
        if (part_work[i] == 0u || w >= f / 2.0) {
            continue;
        }
        ++*parts;
        if (w > share * f) {
            printf("    run %d: frame %d visits %lu of %lu\n", (int)run, i,
                   part_work[i], full_work[i]);
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------ the frames */

/* A new angle repaints its line, and the battery's change its own, two
 * lines at frame 49; the frame's time alone repaints nothing;
 * ZERO taken and the reading gone change the rows and repaint whole.  Once
 * the views stop, nothing repaints and RATE's age stands, as before a
 * reading came: compared up to the frame after the last view, and nothing
 * drawn after it.  Every card line is drawn in its box. */
TEST_CASE(a_new_imu_reading_repaints_its_lines_as_the_whole_frame)
{
    static uint64_t hash[FRAMES];
    static unsigned long work[FRAMES];
    int parts = 0;
    CHECK(same_as_whole(RUN_IMU, IMU_LAST + 1, 0.15, &parts));
    CHECK(parts > FRAMES / 2);
    draw_run(RUN_IMU, false, hash, work);
    for (int i = IMU_LAST + 1; i < FRAMES; ++i) {
        CHECK_EQ(work[i], 0ul);
    }
    for (int k = 0; k < 9; ++k) {
        const gfx_rect_t r = imu_screen_line_rect(k);
        const gfx_color_t *px = fb[0] + (size_t)(UI_BAND_H + r.y) * W + r.x;
        int inked = 0, accent = 0;
        for (int y = 0; y < r.h; ++y) {
            for (int x = 0; x < r.w; ++x) {
                inked += px[(size_t)y * W + x] != px[0];
                accent += px[(size_t)y * W + x] == UI_ACCENT;
            }
        }
        CHECK(inked > 20);
        /* X, the axis the runs read by default, in the accent alone. */
        CHECK_EQ(accent > 0, k == 4);
    }
}

/* The hold's fill repaints the button, and the reading's age on RATE as
 * it counts on; the release repaints whole.  After it nothing repaints, and
 * the age stands, as before the hold: compared up to the release's two
 * frames. */
TEST_CASE(the_calibration_hold_repaints_its_button_as_the_whole_frame)
{
    int parts = 0;
    CHECK(same_as_whole(RUN_IMU_HOLD, 62, 0.1, &parts));
    CHECK(parts > 50);
}

/* The hold to its end: the fill, the flash, then nothing until the
 * release repaints whole. */
TEST_CASE(the_calibration_hold_and_its_flash_are_the_whole_frame)
{
    int parts = 0;
    CHECK(same_as_whole(RUN_IMU_CAL, FRAMES, 0.1, &parts));
    CHECK(parts > 70);
    /* The release at frame 95 repaints both buffers whole, and nothing
     * after them. */
    static uint64_t hash[FRAMES];
    static unsigned long work[FRAMES];
    draw_run(RUN_IMU_CAL, false, hash, work);
    CHECK(work[95] > 0ul && work[96] > 0ul);
    for (int i = 97; i < FRAMES; ++i) {
        CHECK_EQ(work[i], 0ul);
    }
    imu_cmd_t cmd = { IMU_CMD_NONE, 0u };
    CHECK(imu_screen_take(&cmd));
    CHECK_EQ(cmd.kind, IMU_CMD_CALIBRATE);
}

/* A sample repaints the plot and the cards; the supply falling silent and
 * answering again repaints the heading row too. */
TEST_CASE(a_supply_sample_repaints_what_it_shows_as_the_whole_frame)
{
    int parts = 0;
    CHECK(same_as_whole(RUN_SUPPLY, FRAMES, 0.5, &parts));
    CHECK(parts > FRAMES / 2);
}

/* 605s to 506s left: 11 minutes, 10 from 600s, 9 from 540s. */
TEST_CASE(an_idle_second_repaints_wifi_only_when_its_minute_changes)
{
    static uint64_t part[FRAMES];
    static unsigned long work[FRAMES];
    int parts = 0;
    CHECK(same_as_whole(RUN_WIFI, FRAMES, 1.0, &parts));
    draw_run(RUN_WIFI, false, part, work);
    int drawn = 0;
    for (int i = 0; i < FRAMES; ++i) {
        drawn += (work[i] > 0u) ? 1 : 0;
    }
    /* 600s at frame 4 and 540s at frame 64, each into both buffers. */
    CHECK_EQ(drawn, 4);
    CHECK(work[4] > 0u && work[5] > 0u && work[64] > 0u && work[65] > 0u);
}

/* --------------------------------------------------- the programmers */

/** @brief The KST programmer fed the port in @p port and the PD mini on,
 *         reading @p mv and @p ma. */
static void kst_fed(link_kst_state_t port, uint16_t mv, uint16_t ma)
{
    static kst_prog_feed_t f;
    memset(&f, 0, sizeof(f));
    f.now_ms = 1000u;
    f.rd.phase = KST_LINK_READY;
    f.rd.known = true;
    f.rd.port = port;
    f.target = 1u;
    f.port_pin[0] = 13u;
    f.port_pin_known = 1u;
    f.supply_read = true;
    f.out_mv = mv;
    f.out_ma = ma;
    kst_panel_feed(&f);
}

/* In PWM the state line shows the set point; in programming the reading
 * in 10mV and 1mA. */
TEST_CASE(a_supply_reading_moves_the_kst_page_only_as_its_state_line_shows_it)
{
    kst_panel_init(gfx_rect_make(0, 0, W, H - UI_BAND_H));
    kst_fed(LINK_KST_PWM, 5000u, 37u);
    uint32_t rev = kst_panel_revision();
    kst_fed(LINK_KST_PWM, 5003u, 38u);
    CHECK_EQ(kst_panel_revision(), rev);
    kst_fed(LINK_KST_PROGRAMMING, 5003u, 38u);
    CHECK(kst_panel_revision() != rev);
    rev = kst_panel_revision();
    kst_fed(LINK_KST_PROGRAMMING, 5009u, 38u);   /* 5.00V as before */
    CHECK_EQ(kst_panel_revision(), rev);
    kst_fed(LINK_KST_PROGRAMMING, 5010u, 38u);   /* 5.01V */
    CHECK(kst_panel_revision() != rev);
    rev = kst_panel_revision();
    kst_fed(LINK_KST_PROGRAMMING, 5010u, 39u);
    CHECK(kst_panel_revision() != rev);
}

/** @brief The Hitec programmer fed its page, read when @p known, and the
 *         PD mini on, reading @p mv and @p ma. */
static void hitec_fed(bool known, uint16_t mv, uint16_t ma)
{
    static hitec_prog_feed_t f;
    memset(&f, 0, sizeof(f));
    f.now_ms = 1000u;
    f.rd.phase = HITEC_LINK_READY;
    f.rd.known = known;
    f.target = 1u;
    f.port_pin[0] = 13u;
    f.port_pin_known = 1u;
    f.supply_read = true;
    f.out_mv = mv;
    f.out_ma = ma;
    hitec_panel_feed(&f);
}

/* The port in PWM: the state line shows the set point, not the reading. */
TEST_CASE(a_supply_reading_the_hitec_page_does_not_show_moves_nothing)
{
    hitec_panel_init(gfx_rect_make(0, 0, W, H - UI_BAND_H));
    hitec_fed(true, 5000u, 37u);
    const uint32_t rev = hitec_panel_revision();
    hitec_fed(true, 5043u, 38u);
    hitec_fed(true, 4987u, 120u);
    CHECK_EQ(hitec_panel_revision(), rev);
    hitec_fed(false, 4987u, 120u);               /* SYNC: another line */
    CHECK(hitec_panel_revision() != rev);
}

/* ------------------------------------------------------- WI-FI's minute */

TEST_CASE(the_idle_minute_is_rounded_up_and_does_not_wrap)
{
    CHECK_EQ(wifi_view_idle_min(0u), 0u);
    CHECK_EQ(wifi_view_idle_min(1u), 1u);
    CHECK_EQ(wifi_view_idle_min(59u), 1u);
    CHECK_EQ(wifi_view_idle_min(60u), 1u);
    CHECK_EQ(wifi_view_idle_min(61u), 2u);
    CHECK_EQ(wifi_view_idle_min(UINT32_MAX), 71582789u);
}

TEST_CASE(a_view_is_kept_and_drawn_only_when_what_it_shows_changes)
{
    wifi_view_t have = wifi_at(600u);
    wifi_view_t v = have;
    CHECK(!wifi_view_take(&have, NULL));
    CHECK(!wifi_view_take(NULL, &v));
    CHECK(!wifi_view_take(&have, &v));          /* the same view */
    v.idle_left_s = 541u;                       /* 10 minutes, as 600s */
    CHECK(!wifi_view_take(&have, &v));
    CHECK_EQ(have.idle_left_s, 541u);           /* kept all the same */
    v.idle_left_s = 540u;                       /* 9 minutes */
    CHECK(wifi_view_take(&have, &v));
    v.channel = 11;                             /* a member before it */
    CHECK(wifi_view_take(&have, &v));
    CHECK_EQ(have.channel, 11);
    v.scanned = true;                           /* and one after it */
    CHECK(wifi_view_take(&have, &v));
    v.scanned = true;
    v.idle_left_s = 539u;
    CHECK(!wifi_view_take(&have, &v));
    CHECK(memcmp(&have, &v, sizeof(v)) == 0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(a_new_imu_reading_repaints_its_lines_as_the_whole_frame);
    RUN(the_calibration_hold_repaints_its_button_as_the_whole_frame);
    RUN(the_calibration_hold_and_its_flash_are_the_whole_frame);
    RUN(a_supply_sample_repaints_what_it_shows_as_the_whole_frame);
    RUN(an_idle_second_repaints_wifi_only_when_its_minute_changes);
    RUN(a_supply_reading_moves_the_kst_page_only_as_its_state_line_shows_it);
    RUN(a_supply_reading_the_hitec_page_does_not_show_moves_nothing);
    RUN(the_idle_minute_is_rounded_up_and_does_not_wrap);
    RUN(a_view_is_kept_and_drawn_only_when_what_it_shows_changes);
    return test_summary("repaint_parts");
}
