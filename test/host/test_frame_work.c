/*
 * What a frame costs to draw, ARMED against SAFE.
 *
 * The screens are fed the way the panel's frame loop feeds them
 * (firmware/panel/main/panel_frame.c): every frame the bench's state, the
 * drive and the pins; a supply sample every 100ms and a bench sample every
 * 50ms whichever state the bench is in; the band's status with its run
 * clock.  Each frame is drawn into one of two buffers in turn, through the
 * router, and its work is what the rasteriser's loops visited
 * (gfx_work()): a clipped repaint that still walks a whole card pays for
 * the card.
 *
 * Held here:
 * - ARMED at rest draws no more than SAFE at rest, plus the band once a
 *   second in each buffer for its run clock;
 * - a held position, whose grip breathes, draws about the grip's box;
 * - a moving arm, swept or dragged, stays within its budget, and so does
 *   MOTOR & ESC LIVE ARMED at rest, whose plot runs;
 * - every frame drawn the screens' way is, in both buffers, pixel for pixel
 *   the frame drawn whole: the band with its ARMED chip, its run clock, a
 *   FAULT chip and STOP pressed, DISARM, and the pin SERVO drives.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "gfx_work.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "supply.h"
#include "supply_screen.h"
#include "telemetry_sim.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"

#include "servo_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

/** @brief The panel's refresh, Hz, as the station measured it. */
#define PANEL_HZ 60.4f
/** @brief Frames before the measured ones: the arm's flash (150ms), both
 *         buffers painted whole, a motion under way for 30 frames. */
#define WARM 60
/** @brief The frames measured: three seconds at PANEL_HZ. */
#define FRAMES 182
/** @brief The frame of a run's tap, CENTRE or SWEEP, or a drag's start. */
#define MOVE_AT (WARM - 30)

/** @brief The grip's box, px a side: the ring of 36px radius round the
 *         tip and its centre (servo_screen_draw.c). */
#define GRIP_SIDE 73
/** @brief A moving arm's budget: visits a frame, half what a moving arm
 *         drew before its boxes were drawn by what lies in them. */
#define MOVING_BUDGET 140000ul
/** @brief MOTOR & ESC LIVE ARMED at rest: visits a frame, the plot and
 *         the readings of each sample in both buffers. */
#define MOTOR_BUDGET 300000ul

/** @brief What moves the screen during a run. */
typedef enum {
    MO_REST,    /**< nothing: the bench as it is */
    MO_HELD,    /**< CENTRE tapped: a position held, the grip breathing */
    MO_SWEEP,   /**< SWEEP tapped and acknowledged */
    MO_DRAG,    /**< a finger round the dial and back */
    MO_BAND,    /**< the band: a FAULT chip comes and goes, STOP pressed */
} motion_t;

/** @brief One run of frames. */
typedef struct {
    ui_screen_id_t screen; /**< the screen on top */
    bool armed;            /**< the bench ARMED for the screen's role */
    motion_t motion;       /**< what moves */
    float hz;              /**< the frame rate */
} run_t;

/** @brief What a run measured. */
typedef struct {
    unsigned long mean;   /**< visits a frame, over the measured frames */
    unsigned long most;   /**< the most in one of them */
} work_t;

static gfx_color_t *fb[2];
static gfx_canvas_t cv[2];

/** @brief The frames' pictures, the screens' way and whole. */
static uint64_t pic[2][FRAMES];

/** @brief A frame's picture, as a 64-bit FNV-1a hash of its 64-bit words. */
static uint64_t picture_hash(const gfx_color_t *px)
{
    uint64_t h = 1469598103934665603ull;
    const size_t words = (size_t)W * H * sizeof(gfx_color_t) / 8u;
    for (size_t i = 0; i < words; ++i) {
        uint64_t v;
        memcpy(&v, (const uint8_t *)px + i * 8u, sizeof(v));
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

/** @brief A touch at (@p x, @p y) of the panel, through the router. */
static void touch_at(int x, int y, touch_event_type_t type)
{
    const touch_event_t e = {
        .type = type,
        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y,
                   .strength = 40 },
    };
    ui_router_event(&e);
}

/** @brief A tap at (@p x, @p y) of the body. */
static void tap(int x, int y)
{
    touch_at(x, UI_BAND_H + y, TOUCH_EVENT_DOWN);
    touch_at(x, UI_BAND_H + y, TOUCH_EVENT_UP);
}

/** @brief The finger of a drag at frame @p i: round the dial and back. */
static void drag_at(int i)
{
    const float k = 3.14159265358979f / 180.0f;
    const float deg = -70.0f + 140.0f * fabsf(sinf((float)i * 0.035f));
    const int x = SV_SHAFT_X + (int)(110.0f * cosf(deg * k) + 0.5f);
    const int y = SV_SHAFT_Y - (int)(110.0f * sinf(deg * k) + 0.5f);
    touch_at(x, UI_BAND_H + y, (i == 0) ? TOUCH_EVENT_DOWN : TOUCH_EVENT_MOVE);
}

/** @brief A fresh panel: the router, the settings, the buffers. */
static void fresh(void)
{
    for (int b = 0; b < 2; ++b) {
        if (fb[b] == NULL) {
            fb[b] = calloc((size_t)W * H, sizeof(gfx_color_t));
            REQUIRE_ALLOC(fb[b]);
        }
        memset(fb[b], 0, (size_t)W * H * sizeof(gfx_color_t));
        gfx_canvas_init(&cv[b], fb[b], W, H, W);
    }
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    supply_screen_settings_loaded();
}

/** @brief The bench's status for frame @p i of @p r at @p ms. */
static ui_bench_status_t status_of(const run_t *r, int i, uint32_t ms)
{
    const bool motor = r->screen == SCREEN_MOTOR;
    ui_bench_status_t st = {
        .link_up      = true,
        .armed        = r->armed,
        .armed_roles  = (uint8_t)(!r->armed ? 0u
                                  : motor ? UI_ARMED_MOTOR : UI_ARMED_SERVO),
        .run_seconds  = r->armed ? (ms - 1000u) / 1000u + 1u : 0u,
        .mode         = "BENCH",
        .capabilities = LINK_CAP_SERVO_PWM,
        .mcu_temp_c   = NAN,
    };
    if (r->motion == MO_BAND && i >= WARM + 20 && i < WARM + 90) {
        st.faults = 1u;
    }
    return st;
}

/**
 * @brief The frame loop's feed for frame @p i of @p r, as panel_frame.c
 *        gives it.
 * @param r the run
 * @param i the frame's number from the run's start
 * @param ms the frame's clock
 */
static void feed(const run_t *r, int i, uint32_t ms)
{
    static telemetry_sim_t sim;
    static bench_state_t bench;
    static supply_sim_t sup_sim;
    static supply_state_t sup;
    static uint32_t next_sup, next_bench;
    if (i == 0) {
        memset(&bench, 0, sizeof(bench));
        memset(&sup, 0, sizeof(sup));
        telemetry_sim_init(&sim, NULL);
        supply_sim_init(&sup_sim);
        next_sup = 0u;
        next_bench = 0u;
    }
    const bool motor = r->screen == SCREEN_MOTOR;
    const bool sv_armed = r->armed && !motor;
    const bool mo_armed = r->armed && motor;
    servo_screen_clock(ms);
    servo_screen_set_armed(sv_armed);
    servo_screen_set_bench_armed(r->armed);
    motor_screen_set_armed(mo_armed);
    servo_screen_rate(SERVO_RATE_IN_FORCE, 50u);
    servo_screen_set_drive(1u, 12u, true);
    servo_screen_set_sweep(true);
    servo_screen_set_surfaces(true);
    servo_screen_set_pins(1ull << 12, true);
    servo_screen_set_enc_held(false);
    servo_screen_set_link(true);
    servo_screen_source(SERVO_SOURCE_MODEL, 0u);
    if (ms >= next_bench) {
        next_bench = ms + 50u;
        telemetry_sim_step(&sim, mo_armed ? 30.0f : 0.0f, 0.05f, &bench);
        motor_screen_push(&bench);
    }
    if (ms >= next_sup) {
        next_sup = ms + 100u;
        supply_sim_step(&sup_sim, 0.1f, &sup);
        supply_screen_push(&sup);
        servo_screen_supply_at(&sup, SERVO_SOURCE_MODEL, 0u);
    }
    if (sv_armed && i == MOVE_AT) {
        if (r->motion == MO_HELD) {
            tap(SV_CENTRE_X, SV_BTN_Y);
        } else if (r->motion == MO_SWEEP) {
            tap(SV_SWEEP_X, SV_BTN_Y);
        }
    }
    if (sv_armed && r->motion == MO_DRAG && i >= MOVE_AT) {
        drag_at(i - MOVE_AT);
    }
    if (r->motion == MO_BAND && (i == WARM + 40 || i == WARM + 50)) {
        /* STOP pressed for ten frames and let go off it, so it does not
         * latch. */
        const gfx_rect_t stop = ui_band_stop_rect();
        const int sx = stop.x + stop.w / 2, sy = stop.y + stop.h / 2;
        if (i == WARM + 40) {
            touch_at(sx, sy, TOUCH_EVENT_DOWN);
        } else {
            touch_at(sx, H / 2, TOUCH_EVENT_MOVE);
            touch_at(sx, H / 2, TOUCH_EVENT_UP);
        }
    }
    servo_cmd_t cmd;
    while (servo_screen_take(&cmd)) {
        if (cmd.kind == SERVO_CMD_SWEEP) {
            servo_screen_sweep_started(cmd.start_seq, 0u,
                                       SERVO_SWEEP_FROM_REST, 0u);
        }
    }
    motor_cmd_t mc;
    while (motor_screen_poll_cmd(&mc)) { }
    (void)ui_router_take_stop();
    const ui_bench_status_t st = status_of(r, i, ms);
    ui_router_set_status(&st);
}

/**
 * @brief Run @p r for WARM + FRAMES frames, each drawn into buffer i & 1
 *        the screens' way, or whole when @p whole.
 * @param r the run
 * @param whole every frame drawn whole
 * @return the work of the measured frames
 */
static work_t run(const run_t *r, bool whole)
{
    fresh();
    ui_router_goto(r->screen);
    work_t w = { 0u, 0u };
    unsigned long sum = 0u;
    for (int i = 0; i < WARM + FRAMES; ++i) {
        const uint32_t ms =
            1000u + (uint32_t)lroundf((float)i * 1000.0f / r->hz);
        feed(r, i, ms);
        ui_router_tick(1.0f / r->hz);
        if (whole) {
            ui_router_invalidate();
        }
        const int b = i & 1;
        const unsigned long before = gfx_work();
        ui_router_render(&cv[b], b);
        const unsigned long spent = gfx_work() - before;
        if (i >= WARM) {
            sum += spent;
            w.most = (spent > w.most) ? spent : w.most;
            pic[whole ? 1 : 0][i - WARM] = picture_hash(fb[b]);
        }
    }
    w.mean = sum / FRAMES;
    return w;
}

/** @brief @p r drawn the screens' way is @p r drawn whole, every frame. */
static bool same_as_whole(const run_t *r)
{
    (void)run(r, false);
    (void)run(r, true);
    for (int i = 0; i < FRAMES; ++i) {
        if (pic[0][i] != pic[1][i]) {
            printf("    screen %d armed %d motion %d differs at frame %d\n",
                   (int)r->screen, (int)r->armed, (int)r->motion, i);
            return false;
        }
    }
    return true;
}

/** @brief The work of @p r, printed for the record. */
static work_t measured(const char *name, const run_t *r)
{
    const work_t w = run(r, false);
    printf("    %-26s %7lu visits a frame, at most %7lu\n", name, w.mean,
           w.most);
    return w;
}

/** @brief The work of the band drawn whole with @p st, as the router draws
 *         it on SERVO. */
static unsigned long band_work(const ui_bench_status_t *st)
{
    const unsigned long before = gfx_work();
    ui_band_render(&cv[0], "SERVO", true, st, false, false);
    return gfx_work() - before;
}

/* ------------------------------------------------------------------ cases */

/* SERVO LIVE and OVERVIEW at rest: ARMED draws what SAFE draws, and the
 * band once a second in each buffer for its run clock. */
TEST_CASE(armed_at_rest_draws_what_safe_at_rest_draws)
{
    static const struct {
        const char *name;
        ui_screen_id_t id;
    } k_screens[] = {
        { "SERVO LIVE", SCREEN_SERVO },
        { "OVERVIEW", SCREEN_OVERVIEW },
    };
    for (size_t k = 0; k < sizeof(k_screens) / sizeof(k_screens[0]); ++k) {
        const run_t safe  = { k_screens[k].id, false, MO_REST, PANEL_HZ };
        const run_t armed = { k_screens[k].id, true, MO_REST, PANEL_HZ };
        char n[48];
        snprintf(n, sizeof(n), "%s SAFE", k_screens[k].name);
        const work_t s = measured(n, &safe);
        snprintf(n, sizeof(n), "%s ARMED", k_screens[k].name);
        const work_t a = measured(n, &armed);
        /* The seconds the measured frames reach into, each the band in
         * both buffers. */
        const ui_bench_status_t st = status_of(&armed, WARM, 1000u);
        const unsigned long secs =
            (unsigned long)ceilf((float)FRAMES / PANEL_HZ) + 1ul;
        const unsigned long clock = 2ul * secs * band_work(&st) / FRAMES;
        if (a.mean > s.mean + clock) {
            T_FAIL("%s at rest: ARMED %lu visits a frame, SAFE %lu, the "
                   "clock %lu", k_screens[k].name, a.mean, s.mean, clock);
        }
    }
}

/* A held position breathes its grip: the frame draws about the grip's box,
 * six layers deep, and not the card it lies on. */
TEST_CASE(a_held_position_draws_the_grip_alone)
{
    const run_t safe = { SCREEN_SERVO, false, MO_REST, PANEL_HZ };
    const run_t held = { SCREEN_SERVO, true, MO_HELD, PANEL_HZ };
    const work_t s = measured("SERVO LIVE SAFE", &safe);
    const work_t h = measured("SERVO LIVE ARMED HELD", &held);
    const unsigned long grip = 6ul * GRIP_SIDE * GRIP_SIDE;
    if (h.mean > s.mean + grip) {
        T_FAIL("held: %lu visits a frame, SAFE %lu, the grip %lu", h.mean,
               s.mean, grip);
    }
}

/* A moving arm, swept or dragged, repaints the horn's boxes and the
 * numerals that follow it, within MOVING_BUDGET. */
TEST_CASE(a_moving_arm_stays_within_its_budget)
{
    const run_t sweep = { SCREEN_SERVO, true, MO_SWEEP, PANEL_HZ };
    const run_t drag  = { SCREEN_SERVO, true, MO_DRAG, PANEL_HZ };
    const work_t s = measured("SERVO LIVE ARMED SWEEP", &sweep);
    const work_t d = measured("SERVO LIVE ARMED DRAG", &drag);
    if (s.mean > MOVING_BUDGET || d.mean > MOVING_BUDGET) {
        T_FAIL("sweep %lu, drag %lu visits a frame; budget %lu", s.mean,
               d.mean, MOVING_BUDGET);
    }
}

/* MOTOR & ESC LIVE ARMED at rest: its plot runs and each sample repaints
 * the plot and the readings in both buffers, within MOTOR_BUDGET. */
TEST_CASE(motor_armed_at_rest_stays_within_its_budget)
{
    const run_t safe  = { SCREEN_MOTOR, false, MO_REST, PANEL_HZ };
    const run_t armed = { SCREEN_MOTOR, true, MO_REST, PANEL_HZ };
    (void)measured("MOTOR & ESC LIVE SAFE", &safe);
    const work_t a = measured("MOTOR & ESC LIVE ARMED", &armed);
    if (a.mean > MOTOR_BUDGET) {
        T_FAIL("MOTOR & ESC LIVE ARMED: %lu visits a frame; budget %lu",
               a.mean, MOTOR_BUDGET);
    }
}

/* Every frame of every run, in both buffers, is the frame drawn whole.  A
 * held position at 25Hz, where every frame takes the grip's breath on by
 * an eighth: at 60Hz a frame may show it an eighth behind by design
 * (test_servo_repaint). */
TEST_CASE(every_frame_is_the_frame_drawn_whole)
{
    static const run_t k_runs[] = {
        { SCREEN_SERVO, false, MO_REST, PANEL_HZ },
        { SCREEN_SERVO, true, MO_REST, PANEL_HZ },
        { SCREEN_SERVO, true, MO_HELD, 25.0f },
        { SCREEN_SERVO, true, MO_SWEEP, PANEL_HZ },
        { SCREEN_SERVO, true, MO_DRAG, PANEL_HZ },
        { SCREEN_SERVO, true, MO_BAND, PANEL_HZ },
        { SCREEN_OVERVIEW, true, MO_BAND, PANEL_HZ },
        { SCREEN_MOTOR, true, MO_REST, PANEL_HZ },
    };
    for (size_t k = 0; k < sizeof(k_runs) / sizeof(k_runs[0]); ++k) {
        CHECK(same_as_whole(&k_runs[k]));
    }
}

/* ARMED stays on the band in both buffers: the chip in the danger red,
 * with the run's clock beside it, three seconds into a run at rest. */
TEST_CASE(the_band_says_armed_in_both_buffers)
{
    const run_t armed = { SCREEN_SERVO, true, MO_REST, PANEL_HZ };
    (void)run(&armed, false);
    gfx_rect_t chip;
    CHECK(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_ARMED, &chip));
    CHECK(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_CLOCK, NULL));
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    for (int b = 0; b < 2; ++b) {
        int n = 0;
        for (int y = chip.y; y < chip.y + chip.h; ++y) {
            for (int x = chip.x; x < chip.x + chip.w; ++x) {
                n += (fb[b][(size_t)y * W + (size_t)x] == red) ? 1 : 0;
            }
        }
        if (n < chip.w * chip.h / 2) {
            T_FAIL("buffer %d: %d of the chip's %d pixels red", b, n,
                   chip.w * chip.h);
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(armed_at_rest_draws_what_safe_at_rest_draws);
    RUN(a_held_position_draws_the_grip_alone);
    RUN(a_moving_arm_stays_within_its_budget);
    RUN(motor_armed_at_rest_stays_within_its_budget);
    RUN(every_frame_is_the_frame_drawn_whole);
    RUN(the_band_says_armed_in_both_buffers);
    for (int b = 0; b < 2; ++b) {
        free(fb[b]);
    }
    return test_summary("frame_work");
}
