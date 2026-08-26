/*
 * Renders real frames on the host so cachegrind can model the ESP32-S3's data
 * cache (64 KiB, 8-way, 64-byte lines).
 *
 * On the board the framebuffer lives in PSRAM behind that cache, so every D1
 * miss is a 64-byte fetch and every dirty eviction a 64-byte write back.
 * Counting misses counts the bus traffic a frame costs -- the quantity that
 * actually decides the frame rate, and an awkward one to measure on-device.
 *
 * Driven by tools/frame_cost.py.  Modes:
 *   frame     steady state: the band redrawn, screen chrome cached
 *   chrome    everything repainted every frame, i.e. what not caching costs
 *   overview  the menu, whose tiles are chrome and must never be in a frame
 *   clear     a full-screen clear, for reference
 *   vlines    17 full-height vertical lines, the pathological case
 *   hlines    the same pixel count as horizontal lines
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "overview_screen.h"
#include "splash_screen.h"
#include "stub_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t fb[W * H];

static const ui_bench_status_t k_status = {
    .link_up = true, .armed = true, .faults = 0,
    .run_seconds = 257, .mode = "DSHOT600",
};

/*
 * argv is (frames, mode), and the frame count matters: frame_cost.py measures
 * the *difference* between rendering zero frames and rendering N, so that the
 * process start-up, the first paint into each buffer and the cachegrind
 * harness itself all cancel out.  A harness that ignored the count would
 * report zero for everything -- which is exactly what the first version of
 * this file did.
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
        /* The same pixel count, drawn the other way.  The difference between
         * this and vlines is the whole argument for drawing row-major. */
        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < 17; ++i) {
                gfx_fill_rect(&c, 0, i * 28, H, 1, (gfx_color_t)(0x4321 + f));
            }
        }
        return 0;
    }

    ui_router_goto(strcmp(mode, "overview") == 0 ? SCREEN_OVERVIEW
                                                 : SCREEN_MOTOR);

    /* Warm both framebuffers the way the panel does, before measuring. */
    ui_router_render(&c, 0);
    ui_router_render(&c, 1);

    for (int i = 0; i < frames; ++i) {
        if (strcmp(mode, "chrome") == 0) {
            /* Nothing cached: what repainting the chrome every frame costs. */
            ui_router_invalidate();
        }
        ui_router_render(&c, i & 1);
    }
    return 0;
}
