/*
 * Renders real frames on the host so cachegrind can model the ESP32-S3's data
 * cache (64 KiB, 8-way, 64-byte lines).
 *
 * On the board the framebuffer lives in PSRAM (pseudo-static random-access
 * memory) behind that cache, so every D1 (level-1 data cache) miss is a
 * 64-byte fetch and every dirty eviction a 64-byte write back.  The miss
 * count is the bus traffic a frame costs, which is what sets the frame rate.
 *
 * Driven by tools/frame_cost.py.  Modes:
 *   frame     steady state: the band redrawn, screen chrome cached
 *   chrome    everything repainted every frame, i.e. the cost of not caching
 *   overview  the menu, whose tiles are chrome and are never in a frame
 *   servo     the servo card, whose arm and grip are drawn from coverage
 *   servo-grip  the same, repainting only the breathing grip
 *   sim       the same steady state with the SIMULATION watermark over it,
 *             the only mode that blends rather than writes
 *   frame-idle the bench between samples: the panel refreshes at 39 Hz and
 *             samples arrive at 20 Hz, so about half of all frames have
 *             nothing new to plot.  `frame` pushes one sample per frame and
 *             is the worst case, which is what the ceiling guards.
 *   clear     a full-screen clear, for reference
 *   vlines    17 full-height vertical lines, the pathological case
 *   hlines    the same pixel count as horizontal lines
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "gfx.h"
#include "overview_screen.h"
#include "servo_screen.h"
#include "splash_screen.h"
#include "stub_screen.h"
#include "telemetry_sim.h"
#include "ui_screen.h"
#include "motor_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t fb[W * H];

static const ui_bench_status_t k_status = {
    .link_up = true, .armed = true, .faults = 0,
    .run_seconds = 257, .mode = "DSHOT600",
    /* Not read in this harness; the strip prints "--". */
    .mcu_temp_c = NAN,
};

/*
 * argv is (frames, mode).  frame_cost.py measures the difference between a
 * 1-frame run and an 11-frame run, so process start-up, the first paint into
 * each buffer and the cachegrind harness cancel out.  The frame count is
 * therefore honoured exactly.
 */
int main(int argc, char **argv)
{
    const int   frames = (argc > 1) ? atoi(argv[1]) : 1;
    const char *mode   = (argc > 2) ? argv[2] : "frame";

    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    ui_router_set_status(&k_status);

    if (strcmp(mode, "clear") == 0) {
        for (int i = 0; i < frames; ++i) {
            gfx_clear(&c, (gfx_color_t)(0x1234 + i));
        }
        return 0;
    }
    if (strcmp(mode, "vlines") == 0) {
        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < 17; ++i) {
                gfx_fill_rect(&c, i * 47, 0, 1, H, (gfx_color_t)(0x4321 + f));
            }
        }
        return 0;
    }
    if (strcmp(mode, "hlines") == 0) {
        /* The same pixel count as vlines, drawn as rows.  The difference is
         * the cost of column-major drawing. */
        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < 17; ++i) {
                gfx_fill_rect(&c, 0, i * 28, H, 1, (gfx_color_t)(0x4321 + f));
            }
        }
        return 0;
    }

    const size_t mode_len = strlen(mode);
    const bool want_sim =
        (strcmp(mode, "sim") == 0)
        || (mode_len > 4 && strcmp(mode + mode_len - 4, "-sim") == 0);
    if (want_sim) {
        ui_bench_status_t sim = k_status;
        sim.simulated = true;
        ui_router_set_status(&sim);
    }

    const bool servo = (strncmp(mode, "servo", 5) == 0);
    ui_screen_id_t start = SCREEN_MOTOR;
    if (strcmp(mode, "overview") == 0) {
        start = SCREEN_OVERVIEW;
    } else if (servo) {
        start = SCREEN_SERVO;
        servo_screen_set_commanded(38.0f);
    }
    /* The screens that carry no mode of their own: what one steady frame of
     * each costs once both framebuffers hold its chrome. */
    static const struct { const char *name; ui_screen_id_t id; } k_plain[] = {
        { "analyser",   SCREEN_ANALYSER },
        { "logs",       SCREEN_LOGS },
        { "settings",   SCREEN_SETUP },
        { "battery",    SCREEN_BATTERY },
        { "balance",    SCREEN_BALANCE },
        { "programmer", SCREEN_PROGRAMMER },
    };
    bool plain = false;
    bool plain_chrome = false;
    for (size_t k = 0; k < sizeof(k_plain) / sizeof(k_plain[0]); ++k) {
        const char *n = k_plain[k].name;
        const size_t len = strlen(n);
        if (strcmp(mode, n) == 0) {
            start = k_plain[k].id;
            plain = true;
            break;
        }
        /* "<screen>-chrome": the same screen repainting in full every frame,
         * which is what an invalidation on every frame costs. */
        if (strncmp(mode, n, len) == 0 && strcmp(mode + len, "-chrome") == 0) {
            start = k_plain[k].id;
            plain = true;
            plain_chrome = true;
            break;
        }
        /* "<screen>-sim": the same screen with the SIMULATION watermark, which
         * the panel paints over the whole canvas on every frame. */
        if (strncmp(mode, n, len) == 0 && strcmp(mode + len, "-sim") == 0) {
            start = k_plain[k].id;
            plain = true;
            break;
        }
    }
    ui_router_goto(start);

    /* Warm both framebuffers the way the panel does, before measuring. */
    ui_router_render(&c, 0);
    ui_router_render(&c, 1);

    telemetry_sim_t sim;
    bench_state_t bench;
    memset(&bench, 0, sizeof(bench));
    telemetry_sim_init(&sim, NULL);
    const bool feed = (strcmp(mode, "frame-idle") != 0);

    for (int i = 0; i < frames; ++i) {
        if (plain) {
            if (plain_chrome) {
                ui_router_invalidate();
            }
            ui_router_tick(0.026f);
            ui_router_render(&c, i & 1);
            continue;
        }
        if (servo) {
            /* "servo" moves the arm every frame: the drag case, and the
             * worst one.  "servo-grip" holds it still, so only the grip
             * breathes, which is the frame the clipped repaint exists for. */
            if (strcmp(mode, "servo") == 0) {
                servo_screen_set_commanded(38.0f + (float)((i % 20) - 10));
            }
            servo_screen_feedback(servo_screen_commanded(), 0.4f, true);
            ui_router_tick(0.026f);
            ui_router_render(&c, i & 1);
            continue;
        }
        if (feed) {
            telemetry_sim_step(&sim, 60.0f, 0.05f, &bench);
            motor_screen_push(&bench);
        }
        if (strcmp(mode, "throttle") == 0) {
            /* A finger on the throttle: the control revision moves on every
             * frame, which is the drag case and the screen's worst one. */
            motor_screen_set_throttle((float)(i % 100));
        }
        if (strcmp(mode, "chrome") == 0) {
            /* Nothing cached: what repainting the chrome every frame costs. */
            ui_router_invalidate();
        }
        ui_router_render(&c, i & 1);
    }
    return 0;
}
