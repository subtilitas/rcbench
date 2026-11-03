/**
 * @file supply_pdmini.c
 * @brief SUPPLY > SETTINGS' PD MINI tab: the PD mini's wiring, enable, TX, RX
 *        and baud (SET_CAT_PDMINI).
 *
 * A change applies at once, as the settings' observer publishes it, and is
 * saved when the bench is idle (settings_request_save()), as on the LIMITS
 * tab.  Any change of the wiring switches a live output off on the control
 * task (ALERT_PDMINI_WIRING or ALERT_SUPPLY_CHANGED), so all four rows take
 * no press while the output is on or an ON is on its way, whichever screen
 * asked for it; while a programming port holds the supply; and while the
 * bench is ARMED (settings_safe_only()).  A lock closes a keypad open on a
 * row, what it holds not taken.  On a board that solders the pins (the rbmod
 * carrier) TX and RX have no keys (settings_fixed()); enable and baud stay.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "settings.h"
#include "supply_screen.h"
#include "ui_setting_keypad.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "supply_screen_int.h"

/** @brief The tab's rows, top down. */
static const setting_id_t k_pd_rows[SUP_PD_ROWS] = {
    SET_PDMINI_EN, SET_PDMINI_TX, SET_PDMINI_RX, SET_PDMINI_BAUD,
};

sup_pd_lock_t sup_pd_lock(void)
{
    if (s.port_held) {
        return SUP_PD_LOCK_PORT;
    }
    if (s.on || s.on_asked || s.pending.on) {
        return SUP_PD_LOCK_LIVE;
    }
    return settings_held(SET_PDMINI_EN) ? SUP_PD_LOCK_ARMED : SUP_PD_FREE;
}

/**
 * @brief The words of a lock, for a row's help line and the alert band.
 * @param l The lock.
 * @return Its text; "" with none.
 */
static const char *lock_text(sup_pd_lock_t l)
{
    switch (l) {
    case SUP_PD_LOCK_PORT:  return TR(SUP_PD_LOCK_PORT);
    case SUP_PD_LOCK_LIVE:  return TR(SUP_PD_LOCK_ON);
    case SUP_PD_LOCK_ARMED: return TR(MO_SET_SAFE_ONLY);
    case SUP_PD_FREE:
    default:                return "";
    }
}

gfx_rect_t sup_pd_row_rect(int i)
{
    return (gfx_rect_t){ OV_COL0_X, (int16_t)(OV_Y + OV_PD_DY + i * OV_PITCH),
                         (int16_t)(OV_W - 2 * INNER), OV_ROW_H };
}

gfx_rect_t sup_pd_value_rect(int i)
{
    const gfx_rect_t r = sup_pd_row_rect(i);
    return (gfx_rect_t){ (int16_t)(r.x + r.w - OV_PD_KEY_W - OV_PD_GAP
                                   - OV_VAL_W),
                         r.y, OV_VAL_W, OV_ROW_H };
}

gfx_rect_t sup_pd_key_rect(int i, bool plus)
{
    const gfx_rect_t v = sup_pd_value_rect(i);
    return (gfx_rect_t){ (int16_t)(plus ? v.x + v.w + OV_PD_GAP
                                        : v.x - OV_PD_GAP - OV_PD_KEY_W),
                         v.y, OV_PD_KEY_W, OV_ROW_H };
}

/**
 * @brief Whether row @p i has - and + keys: a pin not soldered, and the baud.
 *        Enable flips on a tap of its value.
 * @param i The row.
 * @return true when it has.
 */
static bool has_keys(int i)
{
    return i != 0 && !settings_fixed(k_pd_rows[i]);
}

void sup_pd_down(const touch_event_t *evt, int x, int y)
{
    for (int i = 0; i < SUP_PD_ROWS; ++i) {
        const bool plus = has_keys(i)
                          && gfx_rect_contains(sup_pd_key_rect(i, true), x, y);
        const bool minus = has_keys(i)
                           && gfx_rect_contains(sup_pd_key_rect(i, false), x, y);
        const bool value = !settings_fixed(k_pd_rows[i])
                           && gfx_rect_contains(sup_pd_value_rect(i), x, y);
        if (!plus && !minus && !value) {
            continue;
        }
        const sup_pd_lock_t l = sup_pd_lock();
        if (l != SUP_PD_FREE) {
            /* Nothing is taken, and the band says why. */
            ui_router_set_alert(lock_text(l));
            return;
        }
        sup_take(evt, plus ? P_PD_PLUS : minus ? P_PD_MINUS : P_PD_VALUE);
        s.press_row = i;
        return;
    }
}

/**
 * @brief A change made: on the page now, saved when the bench is idle.
 */
static void changed(void)
{
    settings_request_save();
    ++s.ov_rev;
}

void sup_pd_released(int was, int row, int x, int y)
{
    /* Locked since the press: the release changes nothing. */
    if (row < 0 || row >= SUP_PD_ROWS || sup_pd_lock() != SUP_PD_FREE
        || settings_fixed(k_pd_rows[row])) {
        return;
    }
    const setting_id_t id = k_pd_rows[row];
    if (was == P_PD_VALUE) {
        if (!gfx_rect_contains(sup_pd_value_rect(row), x, y)) {
            return;
        }
        if (id == SET_PDMINI_EN) {
            settings_set(id, settings_get_bool(id) ? 0.0f : 1.0f);
            changed();
        } else if (id == SET_PDMINI_BAUD) {
            settings_adjust(id, 1);
            changed();
        } else {
            ui_setting_keypad_open(&s.kp, sup_overlay_area(), id);
            s.kp_target  = KP_PD;
            s.kp_setting = id;
            supply_invalidate();
        }
    } else if (gfx_rect_contains(sup_pd_key_rect(row, was == P_PD_PLUS), x, y)) {
        settings_adjust(id, (was == P_PD_PLUS) ? 1 : -1);
        changed();
    }
}

void sup_pd_keypad_ok(float v)
{
    /* A lock closes the keypad at the next tick; an OK in the frame between
     * takes nothing. */
    if (sup_pd_lock() == SUP_PD_FREE
        && ui_setting_keypad_ok(&s.kp, s.kp_setting, v) == UI_SETKEY_SET) {
        changed();
    }
}

/**
 * @brief The rows' values and solder as one word: a change made outside the
 *        tab (a board found, a discard) repaints it.
 * @return the word
 */
static uint32_t rows_signature(void)
{
    uint32_t sig = 0u;
    for (int i = 0; i < SUP_PD_ROWS; ++i) {
        const uint32_t v = (uint32_t)(settings_get_int(k_pd_rows[i]) + 1)
                           | (settings_fixed(k_pd_rows[i]) ? 0x40u : 0u);
        sig = (sig << 8) | (v & 0xFFu);
    }
    return sig;
}

void sup_pd_tick(void)
{
    const sup_pd_lock_t l = sup_pd_lock();
    const uint32_t sig = rows_signature();
    if ((uint8_t)l != s.pd_lock || sig != s.pd_sig) {
        s.pd_lock = (uint8_t)l;
        s.pd_sig  = sig;
        ++s.ov_rev;
    }
    if (l == SUP_PD_FREE) {
        return;
    }
    if (s.pressed == P_PD_VALUE || s.pressed == P_PD_MINUS
        || s.pressed == P_PD_PLUS) {
        sup_let_go();
    }
    if (s.kp.open && s.kp_target == KP_PD) {
        if (s.pressed == P_KEYPAD) {
            sup_let_go();
        }
        ui_keypad_close(&s.kp);
        s.kp_target = KP_NONE;
        supply_invalidate();
    }
}

/**
 * @brief Row @p i's help line: a lock's reason, a pin soldered, one pin for
 *        both, or the setting's own.
 * @param i The row.
 * @param warn Takes whether the line is a warning.
 * @return The line.
 */
static const char *row_help(int i, bool *warn)
{
    const setting_id_t id = k_pd_rows[i];
    const sup_pd_lock_t l = sup_pd_lock();
    *warn = true;
    if ((id == SET_PDMINI_TX || id == SET_PDMINI_RX)
        && settings_pdmini_same_pin()) {
        return TR(SET_PD_SAME_PIN);
    }
    if (settings_fixed(id)) {
        *warn = false;
        return TR(SET_PIN_SOLDERED);
    }
    if (l != SUP_PD_FREE) {
        return lock_text(l);
    }
    *warn = false;
    return ui_setting_help(id);
}

void sup_pd_draw(gfx_canvas_t *c)
{
    const bool free = sup_pd_lock() == SUP_PD_FREE;
    for (int i = 0; i < SUP_PD_ROWS; ++i) {
        const setting_id_t id = k_pd_rows[i];
        const gfx_rect_t r = sup_pd_row_rect(i);
        const bool fixed = settings_fixed(id);
        bool warn = false;
        const char *help = row_help(i, &warn);
        gfx_text(c, r.x, r.y + 2, ui_setting_label(id), SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, r.x, r.y + 28, help, SU_FONT_FINE,
                 warn ? ui_theme_color(UI_C_WARN)
                      : ui_theme_color(UI_C_TEXT_FAINT), 1);
        char v[24], line[40];
        (void)ui_setting_value(id, v, sizeof(v));
        const setting_def_t *d = settings_def(id);
        if (d->unit[0] != '\0') {
            snprintf(line, sizeof(line), "%s %s", d->unit, v);
        } else {
            snprintf(line, sizeof(line), "%s", v);
        }
        ui_button_font(c, sup_pd_value_rect(i), line,
                       ui_theme_color(UI_C_PANEL_SUNK),
                       s.pressed == P_PD_VALUE && s.press_row == i,
                       free && !fixed, SU_FONT_LABEL);
        if (has_keys(i)) {
            const bool cycles = settings_cycles(id);
            ui_button_font(c, sup_pd_key_rect(i, false), cycles ? "<" : "-",
                           ui_theme_color(UI_C_EDGE_HI),
                           s.pressed == P_PD_MINUS && s.press_row == i, free,
                           SU_FONT_LABEL);
            ui_button_font(c, sup_pd_key_rect(i, true), cycles ? ">" : "+",
                           ui_theme_color(UI_C_EDGE_HI),
                           s.pressed == P_PD_PLUS && s.press_row == i, free,
                           SU_FONT_LABEL);
        }
    }
    const int ny = OV_Y + OV_PD_DY + SUP_PD_ROWS * OV_PITCH + 4;
    gfx_text(c, OV_COL0_X, ny, TR(SUP_PD_NOTE_1), SU_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, OV_COL0_X, ny + OV_HELP_PITCH, TR(SUP_PD_NOTE_2), SU_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

void supply_screen_open_pdmini(void)
{
    (void)ui_wb_strip_select(&s.wb, UI_WB_SETTINGS);
    s.ov_tabs.selected = SUP_OV_PDMINI;
    ++s.strip_rev;
    ++s.ov_rev;
    supply_invalidate();
}

void supply_screen_open_limits(void)
{
    (void)ui_wb_strip_select(&s.wb, UI_WB_SETTINGS);
    s.ov_tabs.selected = SUP_OV_LIMITS;
    ++s.strip_rev;
    ++s.ov_rev;
    supply_invalidate();
}

int supply_screen_settings_tab(void)
{
    return s.ov_tabs.selected;
}
