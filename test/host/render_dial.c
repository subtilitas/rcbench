/**
 * @file
 * @brief Renders a Dial face from its fixed model (dial_models.h) to a PPM
 *        (portable pixmap) image: the pixels the Dial's frame holds, drawn
 *        by the code the Dial runs.  Driven by tools/render_dial.py, whose
 *        committed images CI compares with the current render.
 *
 *     render_dial <out.ppm> <name> <en|de>
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
/** @brief The ring tables' sort scratch. */
static uint32_t s_keys[DIAL_RING_KEYS];

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
 * @brief Render one face.
 * @param argc 4
 * @param argv the program, the image, the face's name, the language
 * @return 0, or 1 on an unknown face or language or a write error
 */
int main(int argc, char **argv)
{
    dial_model_t m;
    gfx_canvas_t c;
    int id = -1;
    if (argc != 4) {
        fprintf(stderr, "usage: render_dial <out.ppm> <name> <en|de>\n");
        return 1;
    }
    for (int k = 0; k < DM_COUNT; ++k) {
        if (strcmp(argv[2], k_dial_model_name[k]) == 0) {
            id = k;
        }
    }
    const bool de = strcmp(argv[3], "de") == 0;
    if (id < 0 || (!de && strcmp(argv[3], "en") != 0)) {
        fprintf(stderr, "render_dial: unknown face or language\n");
        return 1;
    }
    if (!dial_render_init(&s_r, s_keys)) {
        fprintf(stderr, "render_dial: the ring tables did not build\n");
        return 1;
    }
    s_r.lang = de ? DK_LANG_DE : DK_LANG_EN;
    dial_model(&m, id, s_r.lang);
    gfx_canvas_init(&c, s_fb, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    dial_render(&s_r, &c, &m.face, &m.view);
    return write_ppm(argv[1]);
}
