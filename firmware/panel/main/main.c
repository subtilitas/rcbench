/*
 * The panel application.
 *
 * At this commit it is the wiring proof and nothing else: it exists to show
 * that shared/gfx, shared/touch and shared/link compile and link under
 * ESP-IDF, out of the same directories the host suite and the coprocessor
 * build from.  The boot sequence, the splash and the router replace it in the
 * slice that ports them.
 */
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/uart.h"

#include "gfx.h"
#include "link_crc.h"
#include "link_wire.h"
#include "panel_pins.h"
#include "touch_map.h"

void app_main(void)
{
    /* Not a framebuffer -- the panel is not up yet.  Enough canvas to prove
     * the rasteriser links and runs on the target's toolchain. */
    static gfx_color_t pixels[64 * 8];
    gfx_canvas_t canvas;
    gfx_canvas_init(&canvas, pixels, 64, 8, 0);
    gfx_clear(&canvas, GFX_BLACK);
    gfx_fill_rect(&canvas, 0, 0, 8, 8, GFX_WHITE);

    const touch_map_t map = {
        .src_w    = 800,
        .src_h    = 480,
        .mirror_x = false,
        .mirror_y = false,
        .rotation = TOUCH_ROTATION_0,
    };
    touch_point_t p = { .id = 0, .x = 400, .y = 240, .strength = 0 };
    touch_map_point(&map, &p);

    printf("rcbench-panel: gfx ok (%d,%d), touch maps (400,240) -> (%d,%d), "
           "link crc check %s\n",
           canvas.width, canvas.height, p.x, p.y,
           link_crc(LINK_CRC_INIT, "123456789", 9) == LINK_CRC_CHECK
               ? "passes" : "FAILS");

    /* Reading the pins back is not decoration: panel_pins.h is otherwise a
     * header nothing includes, so nothing would compile it and a wrong
     * constant would wait until the transport is written to be discovered. */
    printf("rcbench-panel: link TX=GPIO%d RX=GPIO%d @ %u baud, "
           "heartbeat GPIO%d (J8), turnaround %u us\n",
           (int)PANEL_LINK_PIN_TX, (int)PANEL_LINK_PIN_RX,
           (unsigned)LINK_BAUD_BRINGUP, (int)PANEL_HEARTBEAT_PIN,
           (unsigned)LINK_TURNAROUND_US);
}
