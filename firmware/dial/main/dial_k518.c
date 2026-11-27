/**
 * @file
 * @brief The Guition JC3636K518C's display, touch, input, click and power
 *        calls (dial.h), which do nothing yet.
 *
 * The K518 build runs the BLE peripheral, its bind, the console and the
 * face task; the face task draws its 240 x 240 frame and the flush sends
 * nothing.  No pin of dial_board_k518.h is driven: the display, the touch,
 * the encoder, the haptic driver and the battery reading are later slices.
 * Until then the Dial sends no EVENT, its STATUS names the battery unknown,
 * and every update is refused, since no key confirms one.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "dial.h"
#include "driver/usb_serial_jtag.h"

/** @brief The click per detent, as the panel's SCREEN flag sets it. */
static atomic_bool s_sound = true;

/* --- power ----------------------------------------------------------- */

void dial_power_hold(void)
{
    /* No latch: the slide switch holds the power. */
}

void dial_power_off(void)
{
    /* No latch: the slide switch cuts the power. */
}

void dial_supply_read(dial_supply_t *out)
{
    memset(out, 0, sizeof *out);
    out->usb = usb_serial_jtag_is_connected();
}

void dial_supply_inject(uint16_t mv)
{
    (void)mv;
}

/* --- click ----------------------------------------------------------- */

void dial_buzz_init(void)
{
}

void dial_buzz_click(void)
{
}

void dial_buzz_tone(uint32_t hz, uint32_t ms)
{
    (void)hz;
    (void)ms;
}

/* --- display --------------------------------------------------------- */

bool dial_lcd_init(void)
{
    return false;
}

void dial_lcd_flush(const uint16_t *fb, int y0, int y1)
{
    (void)fb;
    (void)y0;
    (void)y1;
}

void dial_lcd_flush_rect(const uint16_t *fb, int x, int y, int w, int h)
{
    (void)fb;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void dial_lcd_backlight(unsigned pct)
{
    (void)pct;
}

/* --- touch ----------------------------------------------------------- */

bool dial_touch_init(void)
{
    return false;
}

bool dial_touch_read(uint8_t *x, uint8_t *y)
{
    *x = 0u;
    *y = 0u;
    return false;
}

/* --- input ----------------------------------------------------------- */

void dial_input_start(void)
{
}

#if RCBENCH_DIAL_HOOKS
bool dial_input_inject(uint8_t kind, int32_t arg, uint8_t x, uint8_t y)
{
    (void)kind;
    (void)arg;
    (void)x;
    (void)y;
    return false;
}
#endif

void dial_input_sound(bool on)
{
    atomic_store(&s_sound, on);
}

bool dial_input_sound_on(void)
{
    return atomic_load(&s_sound);
}

void dial_input_link(bool any, uint32_t last_ms)
{
    (void)any;
    (void)last_ms;
}

void dial_input_off(dial_face_view_t *v)
{
    v->off = 0u;
    v->off_lit = false;
    v->off_focus = false;
    v->off_secs = 0u;
}

void dial_input_stats(dial_input_stats_t *out)
{
    memset(out, 0, sizeof *out);
}

bool dial_input_update_key(void)
{
    return false;
}
