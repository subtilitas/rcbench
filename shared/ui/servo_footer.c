/**
 * @file
 * @brief The SERVO screens' footer and the PORT button.
 *
 * The footer (ui_footer.h) of every function screen: left the function's
 * name with the port line under it -- what the tester drives now, in the
 * warning colour with no target -- middle ARM / DISARM, right SETTINGS,
 * which opens the function's list (servo_lists.c) and is grey with the
 * reason in the alert band while the bench is not SAFE.
 *
 * The PORT button over the output card, (636, 68, 380, 48) on the panel:
 * PORT and the same line.  A tap opens the choice of OUTPUTS and PROG PORT
 * 1 to 4 over the left column; a port the carrier refuses shows its
 * refusal line and takes no pick.  The choice is SET_SERVO_TARGET, as
 * OUTPUTS' SERVO TESTER rows set it: on the carrier through
 * prog_choice_pick().  Under the TARGET lock -- ARMED or asked to arm, a
 * sweep, a run, a programmer holding a port -- the button is grey and a
 * tap names the lock in the alert band.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "link_pages.h"
#include "prog_choice.h"
#include "settings.h"
#include "ui_footer.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief Each function's name in the footer and on its tile. */
static const ui_text_id_t k_name[SERVO_FN_COUNT] = {
    TX_SVH_SERVO, TX_SVH_LIVE,  TX_SVH_SWEEP, TX_SVH_TEST,  TX_SVH_LIMRUN,
    TX_SVH_RUDDER, TX_SVH_RAUTO, TX_SVH_KST,  TX_SVH_HITEC,
};

const char *svs_fn_name(servo_fn_t fn)
{
    return ui_tr(k_name[(fn < SERVO_FN_COUNT) ? fn : SERVO_FN_HUB]);
}

bool svs_port_line(char *out, size_t n)
{
    return servo_target_line(out, n, s.drive_target, s.drive_pin, s.drive_pwm,
                             s.pins, s.pins_known);
}

/** @brief The PORT button's label, left in it. */
#define PORT_LABEL_DX 16
/** @brief Its value's right edge, from the button's. */
#define PORT_VALUE_DX 16

void draw_port(gfx_canvas_t *c)
{
    const gfx_rect_t r = port_rect();
    const bool locked = prog_choice_locked();
    const bool down = s.fp_pressed == FP_PORT;
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_button_font(c, r, NULL, ui_theme_color(UI_C_PANEL_HI), down,
                   !locked, SV_FONT_LABEL);
    char line[48];
    const bool warn = svs_port_line(line, sizeof(line));
    const int ty = r.y + (r.h - 20) / 2;
    gfx_text(c, r.x + PORT_LABEL_DX, ty, TR(SVH_PORT), SV_FONT_LABEL,
             locked ? ui_theme_color(UI_C_TEXT_FAINT)
                    : ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 80), r.y,
                                 (int16_t)(r.w - 80 - PORT_VALUE_DX), r.h },
                line, SV_FONT_LABEL,
                warn ? ui_theme_color(UI_C_WARN)
                     : locked ? ui_theme_color(UI_C_TEXT_DIM)
                              : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

uint32_t foot_sig(void)
{
    char line[48];
    uint32_t h = 2166136261u;
    const bool warn = svs_port_line(line, sizeof(line));
    for (const char *p = line; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    h = (h ^ s.fp_rev) * 16777619u;
    h = (h ^ (uint32_t)s.fn) * 16777619u;
    h = (h ^ prog_write_sig()) * 16777619u;
    h = (h ^ ((warn ? 1u : 0u) | (prog_choice_locked() ? 2u : 0u)
              | (svs_list_refused() != NULL ? 4u : 0u)
              | (svs_list_offered() ? 8u : 0u)
              | (svs_hub_p5_on() ? 16u : 0u)))
        * 16777619u;
    return h;
}

bool svs_hub_p5_on(void)
{
    return s.fn == SERVO_FN_HUB && prog5v_box_shown() && prog5v_box_on();
}

void draw_footer(gfx_canvas_t *c)
{
    ui_footer_clear(c);
    char line[48];
    const bool warn = svs_port_line(line, sizeof(line));
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME),
                   svs_fn_name((servo_fn_t)s.fn), line, warn);
    if (s.fn != SERVO_FN_HUB) {
        draw_arm(c);
        draw_port(c);
    } else if (svs_hub_p5_on()) {
        /* The ports' 5V stays on across SERVO's screens: the hub, which
         * shows no PROG 5V box, names it in the middle slot. */
        gfx_text_in(c, ui_footer_rect(UI_FOOTER_HOLD), TR(P5_ON),
                    &gfx_font_12x24, ui_theme_color(UI_C_WARN), 1,
                    GFX_ALIGN_CENTER);
    }
    if (svs_list_offered()) {
        ui_footer_settings(c, s.fp_pressed == FP_SETTINGS,
                           svs_list_refused() == NULL);
    }
}

void port_list_open(void)
{
    if (prog_choice_locked()) {
        char why[64];
        (void)prog_choice_lock_line(why, sizeof(why));
        ui_router_set_alert(why);
        return;
    }
    if (svs_press_held()) {
        ui_action_refused(TR(FOOT_PRESS_HELD));
        return;
    }
    close_panels();
    open_choice(CH_PORT, SETTING_COUNT, TR(SVH_PORT_TITLE));
    choice_add(TR(OUT_TESTER_OUTPUTS), 0);
    for (uint8_t port = 1u; port <= LINK_PG_PORTS_MAX; ++port) {
        char label[64];
        const bool refused = prog_choice_refusal(port, label, sizeof(label));
        if (!refused) {
            if (prog_choice_carrier()) {
                prog_choice_entry(port, label, sizeof(label));
            } else {
                snprintf(label, sizeof(label), TR(SV_TARGET_PN),
                         (unsigned)port);
            }
        }
        choice_add(label, (int)port);
        if (refused) {
            s.ch.refused = (uint16_t)(s.ch.refused | (1u << (s.ch.count - 1)));
        }
    }
    servo_invalidate();
    ++s.ctrl_rev;
}

void port_list_pick(int value)
{
    if (value < 0 || value > (int)LINK_PG_PORTS_MAX || prog_choice_locked()
        || svs_press_held()) {
        return;
    }
    if (prog_choice_carrier()) {
        /* The carrier's own rules: offered, not refused. */
        if (!prog_choice_pick((uint8_t)value)) {
            ui_router_set_alert(prog_choice_offered() ? TR(OUT_PORT_LOCKED)
                                                      : TR(PC_OLD_IOMCU));
        }
    } else if (settings_get_int(SET_SERVO_TARGET) != value) {
        settings_set(SET_SERVO_TARGET, (float)value);
        settings_request_save();
    }
    ++s.fp_rev;
    servo_invalidate();
}

bool servo_screen_rate_note(char *l1, char *l2, size_t n)
{
    return svs_rate_note(l1, l2, n);
}

bool servo_screen_port_line(char *out, size_t n)
{
    return svs_port_line(out, n);
}

int servo_screen_port_choices(void)
{
    return (s.ch.open && s.ch.target == CH_PORT) ? s.ch.count : -1;
}

const char *servo_screen_port_choice(int i, bool *refused)
{
    const bool in = servo_screen_port_choices() > i && i >= 0;
    if (refused != NULL) {
        *refused = in && (s.ch.refused & (1u << i)) != 0u;
    }
    return in ? s.ch.labels[i] : "";
}

gfx_rect_t servo_screen_port_choice_rect(int i) { return choice_rect(i); }

/* ------------------------------------------------------------- presses */

/**
 * @brief The slot a DOWN at (@p x, @p y) lands on: PORT, SETTINGS, or none.
 * @param x the point's x @param y its y
 * @return FP_*
 */
static uint8_t foot_at(int x, int y)
{
    if (s.fn != SERVO_FN_HUB && gfx_rect_contains(port_rect(), x, y)) {
        return FP_PORT;
    }
    if (svs_list_offered()
        && gfx_rect_contains(ui_footer_rect(UI_FOOTER_SETTINGS), x, y)) {
        return FP_SETTINGS;
    }
    return FP_NONE;
}

bool foot_down(const touch_event_t *evt)
{
    const uint8_t at = foot_at(evt->point.x, evt->point.y);
    if (at == FP_NONE || modal_open()) {
        return false;
    }
    if (s.fp_pressed == FP_NONE) {
        s.fp_pressed = at;
        s.fp_id = evt->point.id;
        ++s.fp_rev;
    }
    return true;
}

bool foot_rest(const touch_event_t *evt)
{
    if (s.fp_pressed == FP_NONE || s.fp_pressed == FP_TILE
        || evt->point.id != s.fp_id) {
        return false;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    const uint8_t was = s.fp_pressed;
    s.fp_pressed = FP_NONE;
    ++s.fp_rev;
    if (!touch_event_is_tap_up(evt)
        || foot_at(evt->point.x, evt->point.y) != was) {
        return true;
    }
    if (was == FP_PORT) {
        port_list_open();
    } else if (svs_list_refused() != NULL) {
        ui_action_refused(svs_list_refused());
    } else {
        svs_list_open();
    }
    return true;
}
