/**
 * @file
 * @brief The PROG 5V box.  See prog5v_box.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog5v_box.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"

/** @brief The published state's values (prog_supply_state_t). */
enum { ST_NONE = 0, ST_UNKNOWN = 1, ST_OFF = 2, ST_ON = 3 };

/** @brief state << 16 | pin << 8 | held port, as published. */
static atomic_uint s_state = ATOMIC_VAR_INIT((unsigned)PROG5V_HELD_NONE);
/** @brief bit 31 a reading, mV in bits 16 to 30, mA as int16 in 0 to 15. */
static atomic_uint s_read;

/** @brief What the boxes asked and not yet polled.  app_main only. */
static prog5v_cmd_t s_cmd;
/** @brief The screen whose box switched the supply on.  app_main only. */
static prog5v_by_t  s_owner;

void prog5v_box_publish(uint8_t state, uint8_t pin, uint8_t held_port)
{
    atomic_store(&s_state, ((unsigned)state << 16) | ((unsigned)pin << 8)
                               | held_port);
}

void prog5v_box_reading(bool ok, uint16_t mv, int16_t ma)
{
    atomic_store(&s_read, ok ? (0x80000000u | ((unsigned)(mv & 0x7FFFu) << 16)
                                | (uint16_t)ma)
                             : 0u);
}

/** @brief The published state, ST_*. */
static unsigned st(void) { return (atomic_load(&s_state) >> 16) & 0xFFu; }
/** @brief The published pin. */
static unsigned pin(void) { return (atomic_load(&s_state) >> 8) & 0xFFu; }
/** @brief The published holding port, or PROG5V_HELD_NONE. */
static unsigned held(void) { return atomic_load(&s_state) & 0xFFu; }

bool prog5v_box_shown(void) { return st() != ST_NONE; }

bool prog5v_box_on(void) { return st() == ST_ON; }

bool prog5v_box_live(void)
{
    return (st() == ST_ON || st() == ST_OFF) && held() == PROG5V_HELD_NONE;
}

void prog5v_box_init(prog5v_box_t *b, gfx_rect_t r, prog5v_by_t by)
{
    if (b == NULL) {
        return;
    }
    memset(b, 0, sizeof(*b));
    b->r      = r;
    b->by     = by;
    b->was_on = prog5v_box_on();
    b->seen   = atomic_load(&s_state);
    b->seen_read = atomic_load(&s_read);
    ui_hold_reset(&b->hold);
}

gfx_rect_t prog5v_box_button(const prog5v_box_t *b)
{
    if (b == NULL) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(b->r.x + b->r.w - PROG5V_BOX_BTN_W), b->r.y,
                         PROG5V_BOX_BTN_W, PROG5V_BOX_H };
}

/**
 * @brief Ask for the supply @p on, from screen @p by: an OFF already asked
 *        stands over an ON in the same poll.
 * @param on whether ON
 * @param by the screen
 */
static void post(bool on, prog5v_by_t by)
{
    if (on) {
        if (!s_cmd.off) {
            s_cmd.on = true;
            s_cmd.by = by;
            s_owner  = by;
        }
        return;
    }
    s_cmd.off = true;
    s_cmd.on  = false;
    s_cmd.by  = by;
    s_owner   = PROG5V_BY_NONE;
}

bool prog5v_box_press(prog5v_box_t *b)
{
    if (b == NULL || !prog5v_box_live()) {
        return false;
    }
    b->pressed  = true;
    b->press_on = prog5v_box_on();
    if (!b->press_on) {
        ui_hold_begin(&b->hold);
    }
    ++b->rev;
    return true;
}

bool prog5v_box_drag(prog5v_box_t *b, int x, int y)
{
    if (b == NULL || !b->pressed) {
        return false;
    }
    if (!gfx_rect_contains(prog5v_box_button(b), x, y)
        && ui_hold_leave(&b->hold)) {
        b->pressed = false;
        ++b->rev;
        return true;
    }
    return false;
}

void prog5v_box_release(prog5v_box_t *b, int x, int y, bool tap)
{
    if (b == NULL || !b->pressed) {
        return;
    }
    (void)ui_hold_end(&b->hold);
    /* Off is a tap; on is a hold that has sent its command by the time the
     * finger lifts.  Which one is the press's (press_on): an ON reported
     * during a hold does not make its release an OFF. */
    if (tap && b->press_on && prog5v_box_on()
        && held() == PROG5V_HELD_NONE
        && gfx_rect_contains(prog5v_box_button(b), x, y)) {
        post(false, b->by);
    }
    b->pressed = false;
    ++b->rev;
}

void prog5v_box_cancel(prog5v_box_t *b)
{
    if (b == NULL) {
        return;
    }
    if (b->pressed || b->hold.down) {
        ++b->rev;
    }
    b->pressed = false;
    ui_hold_reset(&b->hold);
}

void prog5v_box_tick(prog5v_box_t *b, float dt_s)
{
    if (b == NULL) {
        return;
    }
    const uint32_t now = atomic_load(&s_state);
    const uint32_t rd  = atomic_load(&s_read);
    if (now != b->seen || rd != b->seen_read) {
        b->seen      = now;
        b->seen_read = rd;
        ++b->rev;
    }
    const bool on = prog5v_box_on();
    if (on != b->was_on) {
        b->was_on = on;
        if (on) {
            ui_hold_reached(&b->hold);
        } else {
            if (ui_hold_left(&b->hold)) {
                b->pressed = false;
            }
            if (st() == ST_OFF) {
                s_owner = PROG5V_BY_NONE;    /* off, whoever switched it */
            }
        }
        ++b->rev;
    }
    /* A port taking the supply ends a press the box has. */
    if (b->pressed && !prog5v_box_live()) {
        prog5v_box_cancel(b);
    }
    if (b->pressed && !b->press_on && !on) {
        ++b->rev;
        if (ui_hold_tick(&b->hold, dt_s)) {
            post(true, b->by);
        }
    }
    if (b->hold.flash_ms > 0) {
        ui_hold_flash_step(&b->hold, dt_s);
        ++b->rev;
    }
}

/**
 * @brief The button's fill: OUTPUT ON's, the OK green when off fading to
 *        the danger red under the hold, red when on.
 * @param b the box
 * @return the colour
 */
static gfx_color_t fill(const prog5v_box_t *b)
{
    const bool on = prog5v_box_on();
    gfx_color_t f = on ? ui_theme_color(UI_C_DANGER) : ui_theme_color(UI_C_OK);
    if (b->hold.flash_ms > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), b->hold.flash_ms);
    }
    if (!on && b->hold.held_s > 0.0f) {
        f = ui_hold_fill(f, ui_theme_color(UI_C_DANGER), b->hold.held_s);
    }
    return f;
}

const char *prog5v_box_text(int line, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    const unsigned s = st();
    if (s == ST_NONE) {
        return buf;
    }
    if (line == 0) {
        const char *what = (s == ST_ON)  ? TR(SUP_PROG5V_ON)
                         : (s == ST_OFF) ? TR(SUP_PROG5V_OFF)
                                         : TR(SUP_PROG5V_UNKNOWN);
        snprintf(buf, n, TR(SUP_PROG5V_LINE), pin(), what);
        return buf;
    }
    const unsigned h = held();
    if (h != PROG5V_HELD_NONE) {
        if (h < 4u) {
            snprintf(buf, n, TR(P5_HELD), h + 1u);
        } else {
            snprintf(buf, n, "%s", TR(P5_HELD_SOME));
        }
        return buf;
    }
    const uint32_t rd = atomic_load(&s_read);
    if ((rd & 0x80000000u) == 0u) {
        snprintf(buf, n, "--V  --A");
        return buf;
    }
    /* Hundredths of a volt, rounded; mA as thousandths of an amp. */
    const unsigned cv = (((rd >> 16) & 0x7FFFu) + 5u) / 10u;
    const int ma = (int16_t)(uint16_t)(rd & 0xFFFFu);
    const unsigned a = (unsigned)abs(ma);
    snprintf(buf, n, "%u.%02uV  %s%u.%03uA", cv / 100u, cv % 100u,
             (ma < 0) ? "-" : "", a / 1000u, a % 1000u);
    return buf;
}

void prog5v_box_render(const prog5v_box_t *b, gfx_canvas_t *c)
{
    if (b == NULL || c == NULL) {
        return;
    }
    gfx_fill_rect(c, b->r.x, b->r.y, b->r.w, b->r.h,
                  ui_theme_color(UI_C_PANEL));
    const unsigned s = st();
    if (s == ST_NONE) {
        return;
    }
    char line[48];
    const int y1 = b->r.y + 6;
    gfx_text(c, b->r.x, y1, prog5v_box_text(0, line, sizeof(line)),
             UI_FONT_FINE, (s == ST_ON) ? ui_theme_color(UI_C_ACCENT)
                                        : ui_theme_color(UI_C_TEXT), 1);
    gfx_text(c, b->r.x, y1 + 20, prog5v_box_text(1, line, sizeof(line)),
             UI_FONT_FINE, (held() != PROG5V_HELD_NONE)
                               ? ui_theme_color(UI_C_WARN)
                               : ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_button_font(c, prog5v_box_button(b),
                   (s == ST_ON) ? TR(P5_OFF) : TR(P5_ON), fill(b), b->pressed,
                   prog5v_box_live(), &gfx_font_10x20);
}

void prog5v_box_left(prog5v_by_t by)
{
    if (by == PROG5V_BY_NONE || s_owner != by || held() != PROG5V_HELD_NONE) {
        return;
    }
    if (prog5v_box_on() || s_cmd.on) {
        post(false, by);
    }
    s_owner = PROG5V_BY_NONE;
}

bool prog5v_box_poll_cmd(prog5v_cmd_t *out)
{
    if (!s_cmd.on && !s_cmd.off) {
        return false;
    }
    if (out != NULL) {
        *out = s_cmd;
    }
    memset(&s_cmd, 0, sizeof(s_cmd));
    return true;
}

void prog5v_box_reset(void)
{
    memset(&s_cmd, 0, sizeof(s_cmd));
    s_owner = PROG5V_BY_NONE;
    prog5v_box_publish(0u, 0u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_reading(false, 0u, 0);
}
