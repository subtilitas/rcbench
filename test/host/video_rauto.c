/*
 * A RUDDER AUTO run as a video shows it: the run of test_servo_rauto_kst.c
 * on the rig of servo_rauto_kst_rig.h, a KST servo behind a PD mini with
 * the IMU on the surface, from the screen at rest through the hold, the
 * read session, every measure and write and the verify pass to PASS.
 * Every 500ms of the rig's time the screen is drawn under the status band
 * and written as a PPM file; every 100ms a line of the track names the
 * frame on show, the time, the surface's angle and whether the run runs.
 *
 *   video_rauto DIR      writes DIR/rauto-NNNN.ppm and DIR/track.csv
 *   video_rauto DIR de   the same with the screen in German
 *
 * tools/make_video.py does not build this; a video's generator runs it
 * and turns the frames into the stills its storyboard names.
 *
 * SPDX-License-Identifier: MIT
 */
/* The linkage of tools/video/player/rudder3d.js lifts the trailing edge
 * 27 degrees at most: 600us of the horn are 25.2 degrees here. */
#define DEG_PER_US 0.042f
/* The meter's readings as a servo on a 5V rail gives them, in place of
 * the model's two levels: ra_video_amps() and ra_video_mv() below. */
#define RA_VIDEO_METER
#include <stdint.h>
static float ra_video_amps(float amps);
static uint16_t ra_video_mv(uint16_t mv, int ma);
#include "servo_rauto_kst_rig.h"
#include "ui_band.h"

#define STEP_MS 100u
#define FRAME_EVERY 5u

/* ---------------------------------------------------- the meter's readings */

/** @brief A number of -1 to 1 from a fixed sequence: the same run draws
 *         the same frames.  @return the number */
static float jitter(void)
{
    static uint32_t seed = 0x2F6E2B1u;
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) / 8388608.0f - 1.0f;
}

/**
 * @brief The servo's current of this millisecond.
 *
 * The model gives 0.12A at rest and 0.95A while the horn travels, two
 * levels that draw as a square wave.  Here the current is a smooth one:
 * 28mA at rest, 4mA more a degree the surface is held out, 0.35A more
 * while the horn travels, followed with a time constant of 250ms, so a
 * search step of 10us shows as a ripple of some mA and a move across the
 * travel as a rounded rise.  White noise of 1mA lies on it.  A rail that
 * is off gives nothing, and a current the model raises against a stop is
 * kept.
 *
 * @param amps the model's current, A
 * @return the current drawn, A
 */
static float ra_video_amps(float amps)
{
    static float out;
    if (!ra_rail_on()) {
        out *= 0.95f;
        return out;
    }
    const bool moving = amps > 0.9f && amps < 1.0f;
    const bool bound = amps > 0.13f && !moving;
    const float defl = fabsf(ra.sv.position_us - 1500.0f) * DEG_PER_US;
    const float want = bound ? amps
        : 0.028f + 0.004f * defl + (moving ? 0.35f : 0.0f);
    out += (want - out) / 250.0f;
    return out + 0.001f * jitter();
}

/**
 * @brief The rail of a 50ms window.
 *
 * The model gives 6000mV or nothing.  The rail is the run's 5.0V: it
 * rises and falls over some windows, sags 0.12V an ampere and carries
 * white noise of 2mV.
 *
 * @param mv the model's rail, mV
 * @param ma the window's mean current, mA
 * @return the rail read, mV
 */
static uint16_t ra_video_mv(uint16_t mv, int ma)
{
    static float rail;
    const float want = mv > 0u ? 5020.0f - 0.12f * (float)ma : 0.0f;
    rail += (want - rail) * (want > rail ? 0.45f : 0.30f);
    const float read = rail + (rail > 100.0f ? 2.0f * jitter() : 0.0f);
    return (uint16_t)(read < 0.0f ? 0.0f : read + 0.5f);
}

static const char *g_dir;
static unsigned g_n;
static FILE *g_track;
static const char *g_title = "RUDDER AUTO";

static void frame(void)
{
    char path[512];
    static unsigned ticks;
    if (ticks++ % FRAME_EVERY == 0u) {
        ui_bench_status_t st = *ui_router_status();
        st.link_up = true;
        st.armed = ra.armed;
        st.armed_roles = ra.armed ? UI_ARMED_SERVO : 0u;
        /* The screen's body under the band, as the router lays it out. */
        gfx_canvas_t body;
        gfx_canvas_init(&body, fb + (size_t)W * UI_BAND_H, W, H - UI_BAND_H,
                        W);
        scr->render(&body, 0);
        ui_band_render(&cv, g_title, true, &st, NULL, false,
                       UI_BAND_GUIDE_SHOWN);
        ++g_n;
        snprintf(path, sizeof(path), "%s/rauto-%04u.ppm", g_dir, g_n);
        FILE *f = fopen(path, "wb");
        if (f == NULL) {
            perror(path);
            exit(1);
        }
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; ++i) {
            uint8_t r, g, b;
            gfx_unpack(fb[i], &r, &g, &b);
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
        fclose(f);
    }
    fprintf(g_track, "%u,%u,%.3f,%d,%d\n", g_n, (unsigned)g_rig.ms,
            (double)((ra.sv.position_us - 1500.0f) * DEG_PER_US),
            servo_screen_rauto_running() ? 1 : 0, ra.armed ? 1 : 0);
}

static void run_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; k += STEP_MS) {
        ra_ms(STEP_MS);
        frame();
    }
}

int main(int argc, char **argv)
{
    char path[512];
    const bool de = argc == 3 && strcmp(argv[2], "de") == 0;
    if (argc != 2 && !de) {
        fprintf(stderr, "usage: video_rauto DIR [de]\n");
        return 2;
    }
    t_install_crash_handler();
    g_dir = argv[1];
    snprintf(path, sizeof(path), "%s/track.csv", g_dir);
    g_track = fopen(path, "w");
    if (g_track == NULL) {
        perror(path);
        return 1;
    }
    fprintf(g_track, "frame,ms,deg,running,armed\n");
    ra_setup_on(true, true, true);
    /* The bench SAFE before the hold: the hold arms. */
    ra.armed = false;
    servo_screen_set_armed(false);
    ra_ms(200u);
    if (de) {
        /* The rig sets the screen up in English; the run is drawn in
         * German from its first frame. */
        ui_text_set_language(UI_LANG_DE);
        g_title = ui_router_title(SCREEN_SERVO_RAUTO);
    }
    /* A whole first frame: nothing of the set-up's drawing stays. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->enter();
    run_ms(2000u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    run_ms(2400u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    for (int i = 0; i < 30000 && servo_screen_rauto_running(); ++i) {
        run_ms(STEP_MS);
    }
    run_ms(4000u);
    fclose(g_track);
    /* The report names the verdict in the screen's language. */
    const bool pass = strstr(ra.report, ui_servo_str(SERVO_STR_PASS)) != NULL;
    printf("%u frames, %s\n", g_n, pass ? "PASS" : "no PASS");
    return test_summary("video_rauto") != 0 || !pass;
}
