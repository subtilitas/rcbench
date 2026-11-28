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
 * Scenes: values, no-sensor, hold (node_view.c); splash, setup-list,
 * setup-name, setup-scan, setup-addr, setup-shunt (node_setup_view.c).
 * English is the node without a link; German is the node linked to a
 * panel set to German, or on the splash and SETUP the last panel's
 * language.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "node_setup.h"
#include "node_setup_view.h"
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
 * @brief A scan of the display board's bus: the codec at 0x18, the touch
 *        controller at 0x38, INA228s at 0x40 and 0x45, an ADS1115 at 0x48.
 * @param r Receives it.
 */
static void bus_scan(nscan_t *r)
{
    static const uint8_t k_addr[] = { 0x18u, 0x38u, 0x40u, 0x45u, 0x48u };
    memset(r, 0, sizeof *r);
    r->done = true;
    for (size_t i = 0; i < sizeof k_addr; ++i) {
        const uint8_t a = k_addr[i];
        r->answered[a >> 3] = (uint8_t)(r->answered[a >> 3] | (1u << (a & 7u)));
    }
    r->ina228[0x40 >> 3] = (uint8_t)((1u << (0x40 & 7u)) | (1u << (0x45 & 7u)));
    r->n = (uint8_t)sizeof k_addr;
    r->n_ina228 = 2u;
}

/**
 * @brief Tap a target's middle.
 * @param s The state.  @param t The target.  @param now The clock.
 */
static void tap(ns_t *s, int t, uint32_t now)
{
    gfx_rect_t r;
    if (ns_box(s, t, &r)) {
        (void)ns_tap(s, r.x + r.w / 2, r.y + r.h / 2, now, false);
    }
}

/**
 * @brief Render a splash or SETUP scene.
 * @param scene The scene's name.
 * @param lang  RCNODE_LANG_*.
 * @return 0, or 1 for a scene that is none of these.
 */
static int setup_scene(const char *scene, uint8_t lang)
{
    nst_vals_t v;
    memset(&v, 0, sizeof v);
    strcpy(v.name, "XT90 MAIN");
    v.i228 = 0x45u;
    v.shunt = 500u;
    const uint8_t fw[3] = { 0u, 23u, 17u };
    ns_t s;
    ns_init(&s, &v, lang, fw, 0u);
    uint32_t now = 3500u;
    if (strcmp(scene, "splash") != 0) {
        tap(&s, NS_T_SETUP, now);
        nscan_t r;
        bus_scan(&r);
        ns_scan_done(&s, &r);
    }
    if (strcmp(scene, "setup-name") == 0) {
        tap(&s, NS_T_ROW0, now);
        tap(&s, NS_T_KEY0 + NS_KB_SPACE, now);
        tap(&s, NS_T_KEY0 + 1, now);           /* "2" */
    } else if (strcmp(scene, "setup-scan") == 0) {
        tap(&s, NS_T_ROW0 + 1, now);
    } else if (strcmp(scene, "setup-addr") == 0) {
        tap(&s, NS_T_ROW0 + 2, now);
    } else if (strcmp(scene, "setup-shunt") == 0) {
        tap(&s, NS_T_ROW3, now);
        tap(&s, NS_T_KEY0 + NS_KP_CLR, now);
        tap(&s, NS_T_KEY0 + 2, now);
        tap(&s, NS_T_KEY0 + 0, now);
        tap(&s, NS_T_KEY0 + NS_KP_OK, now);    /* 20uΩ: refused */
    } else if (strcmp(scene, "setup-list") != 0
               && strcmp(scene, "splash") != 0) {
        return 1;
    }
    gfx_canvas_t c;
    nsv_t view;
    gfx_canvas_init(&c, s_fb, NV_W, NV_H, 0);
    nsv_init(&view);
    (void)nsv_render(&view, &c, &s, now);
    return 0;
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
    if (strncmp(argv[2], "setup", 5) == 0 || strcmp(argv[2], "splash") == 0) {
        if (setup_scene(argv[2], m.lang) != 0) {
            fprintf(stderr, "unknown scene %s\n", argv[2]);
            return 1;
        }
        return write_ppm(argv[1]);
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
