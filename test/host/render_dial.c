/**
 * @file
 * @brief Renders a Dial face from its fixed model (dial_models.h) to a PPM
 *        (portable pixmap) image: the pixels the Dial's frame holds, drawn
 *        by the code the Dial runs.  Driven by tools/render_dial.py, whose
 *        committed images CI compares with the current render.
 *
 *     render_dial <out.ppm> <name> <en|de>
 *     render_dial <out.ppm> live-motor <en|de> <numbers>
 *     render_dial <out.ppm> live-list <en|de> <numbers> <texts>
 *     render_dial <out.ppm> live-value <en|de> <numbers> <texts>
 *
 * The live faces are for tools/video, which shows the Dial beside the
 * panel: live-motor takes the flags (1 ARMED), then throttle 0.01%, rpm,
 * mV, 10mA, 0.1W, the ESC's degrees C, the run's seconds and the speed's
 * full scale, separated by commas.  live-list takes the focused row's
 * index and the count, and as texts the title and the rows before, at and
 * after the focus, separated by `|`.  live-value takes the decimals, the
 * minimum, the maximum, the step, the value, the value on entry and the
 * flags (1 SET, 2 at a limit), and as texts the title and the unit.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dial_face.h"
#include "dial_models.h"

/** @brief The frame. */
static gfx_color_t s_fb[DIAL_FACE_SIZE * DIAL_FACE_SIZE];
/** @brief The renderer. */
static dial_render_t s_r;

/**
 * @brief Write the frame as a binary PPM.
 * @param path the file
 * @return 0, or 1 when it cannot be written
 */
static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", DIAL_FACE_SIZE, DIAL_FACE_SIZE);
    for (int i = 0; i < DIAL_FACE_SIZE * DIAL_FACE_SIZE; ++i) {
        uint8_t rgb[3];
        gfx_unpack(s_fb[i], &rgb[0], &rgb[1], &rgb[2]);
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0 ? 0 : 1;
}

/**
 * @brief Split a text at `|` into at most 4 parts, in place.
 * @param text the text; its `|` become ends of strings
 * @param part takes the parts; the ones the text lacks stay ""
 */
static void split(char *text, const char *part[4])
{
    int n = 0;
    part[0] = part[1] = part[2] = part[3] = "";
    for (char *p = text; n < 4; ++n) {
        part[n] = p;
        p = strchr(p, '|');
        if (p == NULL) {
            break;
        }
        *p++ = '\0';
    }
}

/**
 * @brief Build a live face's model from the command line.
 * @param m    takes the model
 * @param name live-motor, live-list or live-value
 * @param lang DK_LANG_EN or DK_LANG_DE
 * @param nums the numbers, separated by commas
 * @param text the texts, separated by `|`; changed in place
 * @return false on another name
 */
static bool live_model(dial_model_t *m, const char *name, uint8_t lang,
                       const char *nums, char *text)
{
    long v[9] = { 0 };
    const char *part[4];
    int n = 0;
    for (const char *p = nums; n < 9 && *p != '\0'; ++n) {
        char *end = NULL;
        v[n] = strtol(p, &end, 10);
        p = (*end == ',') ? end + 1 : end;
    }
    split(text, part);
    dial_model(m, DM_MOTOR_SAFE, lang);
    dk_face_init(&m->face);
    m->seq = 1;
    if (strcmp(name, "live-motor") == 0) {
        const bool armed = (v[0] & 1) != 0;
        const dk_live_motor_t lm = {
            0, (uint8_t)(armed ? DK_LIVE_ARMED | DK_LIVE_DRIVING : 0),
            DK_MV_RPM | DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER | DK_MV_ESC_T,
            (uint16_t)v[1], (uint16_t)v[2], (uint16_t)v[3], (int16_t)v[4],
            (uint16_t)v[5], (int8_t)v[6], 0, (uint16_t)v[7] };
        dk_scale_t sc = dm_scales();
        if (v[8] > 0) {
            sc.rpm = (uint16_t)v[8];
        }
        dm_screen(m, DK_FACE_MOTOR, lang, "MOTOR & ESC");
        dm_scale(m, sc);
        dm_motor(m, lm);
        dm_state(m, armed ? DK_STA_ARMED : 0);
        return true;
    }
    if (strcmp(name, "live-list") == 0) {
        dm_screen(m, DK_FACE_NAV, lang, part[0]);
        dm_nav(m, (uint8_t)v[0], (uint8_t)v[1], DK_SLOT_PREV, part[1]);
        dm_nav(m, (uint8_t)v[0], (uint8_t)v[1], DK_SLOT_FOCUS, part[2]);
        dm_nav(m, (uint8_t)v[0], (uint8_t)v[1], DK_SLOT_NEXT, part[3]);
        dm_state(m, 0);
        return true;
    }
    if (strcmp(name, "live-value") == 0) {
        dm_screen(m, DK_FACE_VALUE, lang, part[0]);
        dm_value(m, part[1], (uint8_t)v[0], (int32_t)v[1], (int32_t)v[2],
                 (int32_t)v[3], (int32_t)v[4], (int32_t)v[5], (uint8_t)v[6]);
        dm_state(m, 0);
        return true;
    }
    return false;
}

/**
 * @brief Render one face.
 * @param argc 4, or 5 or 6 for a live face
 * @param argv the program, the image, the face's name, the language, and
 *             for a live face its numbers and its texts
 * @return 0, or 1 on an unknown face or language or a write error
 */
int main(int argc, char **argv)
{
    dial_model_t m;
    gfx_canvas_t c;
    int id = -1;
    char none[1] = "";
    if (argc < 4 || argc > 6) {
        fprintf(stderr, "usage: render_dial <out.ppm> <name> <en|de> "
                        "[<numbers> [<texts>]]\n");
        return 1;
    }
    const bool live = strncmp(argv[2], "live-", 5) == 0 && argc > 4;
    for (int k = 0; k < DM_COUNT; ++k) {
        if (strcmp(argv[2], k_dial_model_name[k]) == 0) {
            id = k;
        }
    }
    const bool de = strcmp(argv[3], "de") == 0;
    if (live) {
        id = 0;
    }
    if (id < 0 || (!de && strcmp(argv[3], "en") != 0)) {
        fprintf(stderr, "render_dial: unknown face or language\n");
        return 1;
    }
    if (!dial_render_init(&s_r)) {
        fprintf(stderr, "render_dial: the renderer did not start\n");
        return 1;
    }
    s_r.lang = de ? DK_LANG_DE : DK_LANG_EN;
    if (!live) {
        dial_model(&m, id, s_r.lang);
    } else if (!live_model(&m, argv[2], s_r.lang, argv[4],
                           argc > 5 ? argv[5] : none)) {
        fprintf(stderr, "render_dial: unknown live face\n");
        return 1;
    }
    gfx_canvas_init(&c, s_fb, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    dial_render(&s_r, &c, &m.face, &m.view);
    return write_ppm(argv[1]);
}
