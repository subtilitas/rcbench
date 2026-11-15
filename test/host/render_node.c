/**
 * @file
 * @brief Renders the display node's screen from a fixed model to a PPM
 *        (portable pixmap) image: the pixels the node's frame holds, drawn
 *        by the code the node runs (firmware/node/main/node_view.c).
 *        Driven by tools/render_node.py, whose committed images CI
 *        compares with the current render.
 *
 *     render_node <out.ppm> <scene> <en|de>
 *
 * Scenes: values, no-sensor, hold.  English is the node without a link;
 * German is the node linked to a panel set to German.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "node_view.h"

/** @brief The frame. */
static gfx_color_t s_fb[NV_W * NV_H];

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
    fprintf(f, "P6\n%d %d\n255\n", NV_W, NV_H);
    for (int i = 0; i < NV_W * NV_H; ++i) {
        uint8_t rgb[3];
        gfx_unpack(s_fb[i], &rgb[0], &rgb[1], &rgb[2]);
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0 ? 0 : 1;
}

/**
 * @brief A run on a 6S pack: windows from 0.2A at rest to 68.4A.
 * @param m Receives the values.
 */
static void run_values(nv_model_t *m)
{
    nv_stats_t s;
    nv_stats_init(&s);
    nv_stats_add(&s, 25150, 200000);
    nv_stats_add(&s, 21480, 68400000);
    nv_stats_frame(&s, &m->f);
    nv_stats_add(&s, 22180, 35240000);
    nv_stats_add(&s, 22200, 35160000);
    nv_stats_frame(&s, &m->f);
}

/**
 * @brief Render one scene.
 * @param argc 4
 * @param argv the program, the image, the scene, the language
 * @return 0, or 1 on an unknown scene or language or a write error
 */
int main(int argc, char **argv)
{
    nv_model_t m;
    nv_view_t v;
    gfx_canvas_t c;
    if (argc != 4) {
        fprintf(stderr, "usage: render_node <out.ppm> <scene> <en|de>\n");
        return 1;
    }
    memset(&m, 0, sizeof m);
    if (strcmp(argv[3], "de") == 0) {
        m.linked = true;
        m.lang = RCNODE_LANG_DE;
    } else if (strcmp(argv[3], "en") != 0) {
        fprintf(stderr, "unknown language %s\n", argv[3]);
        return 1;
    }
    if (strcmp(argv[2], "values") == 0) {
        run_values(&m);
    } else if (strcmp(argv[2], "hold") == 0) {
        run_values(&m);
        m.hold_pm = 600u;
    } else if (strcmp(argv[2], "no-sensor") != 0) {
        fprintf(stderr, "unknown scene %s\n", argv[2]);
        return 1;
    }
    gfx_canvas_init(&c, s_fb, NV_W, NV_H, 0);
    nv_view_init(&v);
    (void)nv_render(&v, &c, &m);
    return write_ppm(argv[1]);
}
