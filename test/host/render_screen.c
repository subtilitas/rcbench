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
#include <math.h>
#include <string.h>

#include "log_viewer_screen.h"
#include "settings.h"
#include "analyser_screen.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "overview_screen.h"
#include "telemetry_sim.h"
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
/* A card that does not exist, so the log screenshots are of states the UI
 * can actually reach rather than of an empty browser. */
static char g_csv[32768];
static log_mem_ctx_t g_csv_ctx;

static void make_log_csv(void)
{
    /*
     * A run written the way the logger will write one: units in the header,
     * which is the shape the fixtures use.
     *
     * Worth a look later -- a *separate* units row is read for its units and
     * then counted as a data row as well, so a file in that shape reports its
     * first row as unreadable cells.  Both cannot be right.
     */
    telemetry_sim_t sim;
    bench_state_t   b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);

    size_t n = (size_t)snprintf(g_csv, sizeof(g_csv),
                                "time (s);voltage (V);current (A);"
                                "power (W);rpm (rpm)\n");
    for (int i = 0; i < 320 && n + 80u < sizeof(g_csv); ++i) {
        const float t = (float)i * 0.05f;
        float th = 0.0f;
        if (t >= 1.0f && t < 5.0f)  { th = 24.0f; }
        else if (t < 9.0f)          { th = 58.0f; }
        else if (t < 11.0f)         { th = 92.0f; }
        else if (t >= 11.0f)        { th = 40.0f; }
        telemetry_sim_step(&sim, th, 0.05f, &b);
        n += (size_t)snprintf(g_csv + n, sizeof(g_csv) - n,
                              "%.2f;%.2f;%.2f;%.0f;%.0f\n",
                              (double)t, (double)b.voltage, (double)b.current,
                              (double)b.power, (double)b.rpm);
    }
}

static const struct {
    const char *name;
    uint32_t size;
} k_card[] = {
    { "BENCH_2026-08-22_1.CSV", 18422 },
    { "BENCH_2026-08-22_2.CSV", 9210 },
    { "LOG00015.BFL", 1048576 },
    { "SWEEP_920KV.CSV", 4096 },
};

static int fake_list(log_viewer_file_t *out, int max_entries, void *ctx)
{
    (void)ctx;
    int n = 0;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]) && n < max_entries;
         ++i) {
        snprintf(out[n].name, sizeof(out[n].name), "%s", k_card[i].name);
        out[n].size = k_card[i].size;
        out[n].is_dir = false;
        ++n;
    }
    return n;
}

static bool fake_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    (void)name;
    log_source_memory(src, &g_csv_ctx, g_csv, strlen(g_csv));
    return true;
}

static const char *fake_volume(void *ctx)
{
    (void)ctx;
    return "BENCH SD  14.7 GB FREE";
}

static const log_viewer_io_t k_io = {
    .list = fake_list,
    .open = fake_open,
    .close = NULL,
    .volume = fake_volume,
    .ctx = NULL,
};


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

/* IM_BTN_Y from log_viewer_screen.c: if the layout moves this moves
 * with it, and render_ui --check is what notices. */
#define IM_BTN_Y_LOCAL 368

static void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
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
    /* argv[4] is the golden's name; several share one screen. */
    const char *view = (argc > 4) ? argv[4] : argv[2];
    const bool sim   = (argc > 3 && strcmp(argv[3], "sim") == 0);

    ui_theme_set(light ? UI_THEME_LIGHT : UI_THEME_DARK);
    settings_init();
    ui_router_init();
    pose_splash();
    /*
     * A scripted run, so the bench screenshot always shows the same
     * interesting moment: spin up, hold, a burst, then settle.  Pushed one
     * sample per plot column at the rate the panel polls.
     */
    if (id == SCREEN_MOTOR) {
        telemetry_sim_t sim;
        bench_state_t bench;
        memset(&bench, 0, sizeof(bench));
        telemetry_sim_init(&sim, NULL);
        for (int i = 0; i < 780; ++i) {
            const float t = (float)i * 0.05f;
            float th = 0.0f;
            if (t < 4.0f)                { th = 0.0f; }
            else if (t < 12.0f)          { th = 22.0f; }
            else if (t < 20.0f)          { th = 46.0f; }
            else if (t < 24.0f)          { th = 88.0f; }
            else if (t < 30.0f)          { th = 30.0f; }
            else                         { th = 64.0f; }
            telemetry_sim_step(&sim, th, 0.05f, &bench);
            motor_screen_push(&bench);
        }
        motor_screen_set_throttle(64.0f);
        motor_screen_set_armed(true);
    }

    if (id == SCREEN_PROGRAMMER && strcmp(view, "programmer") != 0) {
        /*
         * Walked down the hierarchy by pressing, not posed by setting flags,
         * so each screenshot goes through the same path a finger does -- a
         * mock that poses its own state can drift from what the buttons
         * actually do.
         *
         * Geometry from programmer_screen.c: if those move, these move.
         */
        ui_router_goto(SCREEN_PROGRAMMER);
        tap(210, UI_BAND_H + 180);              /* the ESC tile */
        if (strcmp(view, "programmer-protocols") != 0) {
            /* Slot two is AM32, whose timing is degrees rather than named
             * steps -- the same renderer, a different kind. */
            tap(400, UI_BAND_H + (strcmp(view, "programmer-am32") == 0
                                  ? 92 + 2 * 68 : 92));
            if (strcmp(view, "programmer-idle") != 0) {
                tap(698, UI_BAND_H + 70);       /* CONNECT */
                if (strcmp(view, "programmer-dirty") == 0) {
                    /* Two staged edits, so the screenshot holds the state
                     * every configurator distinguishes and this one nearly
                     * did not: changed, but not yet written. */
                    tap(765, UI_BAND_H + 132 + 1 * 30 + 10);
                    tap(765, UI_BAND_H + 132 + 3 * 30 + 10);
                }
            }
        }
    }

    if (id == SCREEN_ANALYSER) {
        /*
         * Fed through the real decoder frame by frame, with the channels
         * moving, because a lane view of a still frame shows sixteen
         * straight lines and proves nothing about what it is for.
         */
        sbus_decoder_t dec;
        sbus_decoder_reset(&dec);
        uint32_t us = 100000u;

        for (unsigned t = 0; t < 132u; ++t) {
            uint16_t ch[SBUS_CHANNELS];
            for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
                ch[i] = 1024;                       /* neutral */
            }
            /* A stick swept across the window. */
            ch[1] = (uint16_t)(1024 + (int)(600.0f
                        * sinf((float)t * 0.06f)));
            /* A switch thrown once, a third of the way in. */
            ch[4] = (t > 44u) ? 1811 : 172;
            /* A knob wound slowly. */
            ch[7] = (uint16_t)(400 + t * 8u);
            /* One frame with a spike on it: the glitch a lane is meant to
             * make findable. */
            if (t == 96u) {
                ch[10] = 1811;
            }

            uint8_t raw[SBUS_FRAME_BYTES];
            memset(raw, 0, sizeof(raw));
            raw[0] = SBUS_HEADER;
            for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
                const unsigned bit = i * 11u;
                for (unsigned b = 0; b < 11u; ++b) {
                    if (ch[i] & (1u << b)) {
                        const unsigned at = bit + b;
                        raw[1u + at / 8u] |= (uint8_t)(1u << (at % 8u));
                    }
                }
            }
            raw[23] = SBUS_FLAG_CH17;
            /*
             * The state this screen exists for gets a golden of its own.  A
             * receiver in failsafe sends well-formed numbers it was told to
             * invent, and how that is presented is the thing most worth
             * holding still between releases.
             */
            if (strcmp(view, "analyser-failsafe") == 0) {
                raw[23] |= SBUS_FLAG_FAILSAFE;
            }

            sbus_frame_t frame;
            bool got = false;
            us += SBUS_GAP_US * 2u;          /* the gap that starts a frame */
            for (unsigned i = 0; i < SBUS_FRAME_BYTES; ++i) {
                if (sbus_decode_byte(&dec, raw[i], us, &frame)) {
                    got = true;
                }
                us += 120u;
            }
            if (got) {
                analyser_screen_push(&frame, &dec, raw, SBUS_FRAME_BYTES,
                                     t * 14u);
            }
        }
    }

    if (id == SCREEN_SERVO) {
        /* Driven by the same model the limit finder is tested against, so
         * the screenshot shows a servo that lags its command and draws
         * current for doing so, rather than four dashes. */
        servo_sim_t ss;
        servo_sim_cfg_t cfg;
        servo_sim_defaults(&cfg);
        servo_sim_init(&ss, &cfg);
        servo_screen_set_commanded(38.0f);
        for (int i = 0; i < 200; ++i) {
            const float a = servo_sim_step(&ss, servo_screen_commanded(),
                                           (uint32_t)(i * 10));
            servo_screen_feedback((uint16_t)ss.position_us, a, true);
            ui_router_tick(0.01f);
        }
    }

    ui_bench_status_t st = k_status;
    st.simulated = sim || (id == SCREEN_MOTOR);
    st.armed     = (id == SCREEN_MOTOR);
    ui_router_set_status(&st);
    ui_router_goto(id);

    if (id == SCREEN_LOGS) {
        /* Driven the way a person would, so the shots are of reachable
         * states.  Taps are in panel coordinates; the router removes the
         * band before the screen sees them. */
        make_log_csv();
        log_viewer_set_io(&k_io);
        log_viewer_refresh();
        /*
         * BR_LIST starts at y = 76 in screen coordinates and rows are 44 px,
         * so the first row's middle is 76 + 22.  The band offset is added
         * here because ui_router_event takes panel coordinates and strips it
         * again on the way in.
         */
        if (strcmp(view, "logs") != 0) {
            /* Rows begin at BR_LIST.y + 30, not at BR_LIST.y: the panel
             * has a header. */
            tap(400, UI_BAND_H + 36 + 30 + 22);   /* select */
            tap(400, UI_BAND_H + 36 + 30 + 22);   /* open   */
        }
        if (strcmp(view, "logs-plot") == 0) {
            tap(690, UI_BAND_H + IM_BTN_Y_LOCAL + 23);   /* PLOT */
        }
    }


    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, W, H, W);
    gfx_clear(&c, ui_theme_color(UI_C_BG));
    ui_router_render(&c, 0);

    write_ppm(argv[1], fb);
    return 0;
}
