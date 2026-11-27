/**
 * @file
 * @brief Renders a Dial face from its fixed model (dial_models.h) to a PPM
 *        (portable pixmap) image: the pixels the Dial's frame holds, drawn
 *        by the code the Dial runs.  Driven by tools/render_dial.py, whose
 *        committed images CI compares with the current render.
 *
 *     render_dial <out.ppm> <name> <en|de>
 *     render_dial <out.ppm> packets <en|de> <file>
 *     render_dial <out.ppm> k518-boot <en|de>
 *
 * packets is for tools/video, which shows the Dial beside the panel: the
 * file holds the panel's packets, one a line in hex, as the panel's
 * renderer writes them (render_screen.c, RCBENCH_VIDEO_DIAL); the face is
 * what a linked Dial shows after them.  k518-boot is the Guition
 * JC3636K518C's boot face at its 360 x 360 (dk518_face.h); it has no words
 * to translate, so both languages draw the same image.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dial_face.h"
#include "dial_models.h"
#include "dk518_face.h"

/** @brief The frame, the K518's size: the M5Stack Dial's faces take its
 *         first 240 x 240 pixels. */
static gfx_color_t s_fb[DK518_SIZE * DK518_SIZE];
/** @brief The renderer. */
static dial_render_t s_r;

/**
 * @brief Write the frame as a binary PPM.
 * @param path the file
 * @param size the frame's width and height, pixels
 * @return 0, or 1 when it cannot be written
 */
static int write_ppm(const char *path, int size)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", size, size);
    for (int i = 0; i < size * size; ++i) {
        uint8_t rgb[3];
        gfx_unpack(s_fb[i], &rgb[0], &rgb[1], &rgb[2]);
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0 ? 0 : 1;
}

/**
 * @brief Build the model of a linked Dial from a file of panel packets.
 * @param m    takes the model
 * @param lang DK_LANG_EN or DK_LANG_DE
 * @param path the file, one packet a line in hex
 * @return false when the file cannot be read
 */
static bool packets_model(dial_model_t *m, uint8_t lang, const char *path)
{
    char line[2 * DK_PKT_MAX + 8];
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        perror(path);
        return false;
    }
    dial_model(m, DM_MOTOR_SAFE, lang);
    dk_face_init(&m->face);
    while (fgets(line, sizeof line, f) != NULL) {
        uint8_t b[DK_PKT_MAX];
        size_t n = 0;
        unsigned byte = 0;
        for (const char *p = line; n < sizeof b
                                   && sscanf(p, "%2x", &byte) == 1; p += 2) {
            b[n++] = (uint8_t)byte;
        }
        (void)dm_feed(m, b, n);
    }
    fclose(f);
    return true;
}

/**
 * @brief Render one face.
 * @param argc 4, or 5 for packets
 * @param argv the program, the image, the face's name, the language, and
 *             for packets their file
 * @return 0, or 1 on an unknown face or language or a write error
 */
int main(int argc, char **argv)
{
    dial_model_t m;
    gfx_canvas_t c;
    int id = -1;
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "usage: render_dial <out.ppm> <name> <en|de> "
                        "[<file>]\n");
        return 1;
    }
    if (strcmp(argv[2], "k518-boot") == 0 && argc == 4) {
        const dk518_boot_t b = { "DIAL-3A10", "0.23.18" };
        gfx_canvas_init(&c, s_fb, DK518_SIZE, DK518_SIZE, 0);
        return dk518_boot_draw(&c, &b) ? write_ppm(argv[1], DK518_SIZE) : 1;
    }
    const bool live = strcmp(argv[2], "packets") == 0 && argc == 5;
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
    } else if (!packets_model(&m, s_r.lang, argv[4])) {
        return 1;
    }
    gfx_canvas_init(&c, s_fb, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    dial_render(&s_r, &c, &m.face, &m.view);
    return write_ppm(argv[1], DIAL_FACE_SIZE);
}
