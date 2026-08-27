/*
 * Renders any screen to a PPM on the host.
 *
 * The UI is pure C over a gfx canvas, so the pixels this produces are the
 * pixels the panel shows -- same rasteriser, same fonts, same layout code,
 * same router and the same band.  That makes it possible to design and review
 * screens without a board, and it doubles as a golden-image check.
 *
 * Driven by tools/render_ui.py.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overview_screen.h"
#include "splash_screen.h"
#include "stub_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t fb[W * H];

/*
 * A fixed bench state, so the band shows the same thing every time.  Not all
 * zeroes: a band with nothing in it would make the golden image agree with a
 * band that had stopped working.
 */
static const ui_bench_status_t k_status = {
    .link_up     = true,
    .armed       = false,
    .faults      = 0,
    .run_seconds = 257,
    .mode        = "DSHOT600",
    .simulated   = false,
};

static void write_ppm(const char *path, const gfx_color_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        uint8_t r, g, b;
        gfx_unpack(pixels[i], &r, &g, &b);
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

static ui_screen_id_t id_of(const char *name)
{
    static const struct { const char *name; ui_screen_id_t id; } k[] = {
        { "splash",     SCREEN_SPLASH },
        { "overview",   SCREEN_OVERVIEW },
        { "motor",      SCREEN_MOTOR },
        { "servo",      SCREEN_SERVO },
        { "analyser",   SCREEN_ANALYSER },
        { "logs",       SCREEN_LOGS },
        { "setup",      SCREEN_SETUP },
        { "battery",    SCREEN_BATTERY },
        { "balance",    SCREEN_BALANCE },
        { "programmer", SCREEN_PROGRAMMER },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (strcmp(name, k[i].name) == 0) {
            return k[i].id;
        }
    }
    fprintf(stderr, "unknown screen: %s\n", name);
    exit(2);
}

/* The splash is a sequence, not a state, so it is posed rather than ticked:
 * every step answered, one of them warning, which is the interesting case. */
static void pose_splash(void)
{
    splash_screen_set(SPLASH_STEP_BOARD,    SPLASH_OK,   "CH422G");
    splash_screen_set(SPLASH_STEP_DISPLAY,  SPLASH_OK,   "800x480 39Hz");
    splash_screen_set(SPLASH_STEP_TOUCH,    SPLASH_OK,   "GT911 5pt");
    splash_screen_set(SPLASH_STEP_STORAGE,  SPLASH_WARN, "no card");
    splash_screen_set(SPLASH_STEP_SETTINGS, SPLASH_OK,   "NVS");
    splash_screen_set(SPLASH_STEP_LINK,     SPLASH_OK,   "256k 8N1");
    splash_screen_set(SPLASH_STEP_COPRO,    SPLASH_OK,   "proto 1.0 hw 0");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: render_screen OUT.ppm SCREEN [theme]\n");
        return 2;
    }
    const ui_screen_id_t id = id_of(argv[2]);
    const bool light = (argc > 3 && strcmp(argv[3], "light") == 0);
    const bool sim   = (argc > 3 && strcmp(argv[3], "sim") == 0);

    ui_theme_set(light ? UI_THEME_LIGHT : UI_THEME_DARK);
    ui_router_init();
    pose_splash();
    ui_bench_status_t st = k_status;
    st.simulated = sim;
    ui_router_set_status(&st);
    ui_router_goto(id);

    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, W, H, W);
    gfx_clear(&c, ui_theme_color(UI_C_BG));
    ui_router_render(&c, 0);

    write_ppm(argv[1], fb);
    return 0;
}
