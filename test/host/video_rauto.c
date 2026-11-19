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
 *
 * tools/make_video.py does not build this; a video's generator runs it
 * and turns the frames into the stills its storyboard names.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "ui_band.h"

#define STEP_MS 100u
#define FRAME_EVERY 5u

static const char *g_dir;
static unsigned g_n;
static FILE *g_track;

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
        ui_band_render(&cv, "RUDDER AUTO", true, &st, NULL, false,
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
    if (argc != 2) {
        fprintf(stderr, "usage: video_rauto DIR\n");
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
    printf("%u frames, %s\n", g_n,
           strstr(ra.report, "PASS") != NULL ? "PASS" : "no PASS");
    return test_summary("video_rauto") != 0 || strstr(ra.report, "PASS") == NULL;
}
