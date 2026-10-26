/**
 * @file hitec_panel_edit.c
 * @brief The actions and editors of the Hitec programmer's overlay: the
 *        SERVO tab's buttons, the SERIES and MORE lists, the keypad of the
 *        programming voltage and current, the hold pages of LEAVE and
 *        RELEASE PORT, and the options kept in the settings.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <math.h>
#include <stdio.h>

#include "pdmini.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/* ---------------------------------------------------------------- editors */

void hpn_close_editors(void)
{
    ui_keypad_close(&hp.kp);
    hp.kp_what = KP_NONE;
    hp.ch.open = false;
    hp.page.open = false;
    hp.page.down = false;
    ui_hold_reset(&hp.page.hold);
}

/**
 * @brief Opens the list of @p kind with its title; choice_add() fills it.
 * @param kind  C_*
 * @param title the title
 */
static void choice_open(int kind, const char *title)
{
    hp.ch.open = true;
    hp.ch.kind = kind;
    hp.ch.count = 0;
    snprintf(hp.ch.title, sizeof(hp.ch.title), "%s", title);
}

/**
 * @brief Adds an entry to the open list while it has room.
 * @param label the entry's label
 * @param value its value
 */
static void choice_add(const char *label, int value)
{
    if (hp.ch.count < HP_CHOICE_MAX) {
        snprintf(hp.ch.labels[hp.ch.count], sizeof(hp.ch.labels[0]), "%s",
                 label);
        hp.ch.values[hp.ch.count++] = value;
    }
}

/**
 * @brief Opens a hold page of @p kind.
 * @param kind  W_*
 * @param title its title
 * @param apply the hold button's label
 */
static void page_open(int kind, const char *title, const char *apply)
{
    hpn_close_editors();
    hp.page.open = true;
    hp.page.kind = kind;
    hp.page.count = 0;
    snprintf(hp.page.title, sizeof(hp.page.title), "%s", title);
    snprintf(hp.page.apply, sizeof(hp.page.apply), "%s", apply);
}

/**
 * @brief Adds a line to the open page.
 * @param s the line
 */
static void page_add(const char *s)
{
    if (hp.page.count < HP_PAGE_LINES) {
        snprintf(hp.page.lines[hp.page.count++], sizeof(hp.page.lines[0]), "%s",
                 s);
    }
}

void hpn_page_leave(void)
{
    page_open(W_LEAVE, TR(KP_LEAVE_TITLE), TR(KP_HOLD_LEAVE));
    if (hitec_prog_series(&hp.prog) == (uint8_t)HITEC_CLASSIC) {
        page_add(TR(HP_LEAVE_C));
    } else {
        page_add(TR(KP_LEAVE_1));
        page_add(TR(HP_LEAVE_D));
    }
    page_add(TR(KP_LEAVE_2));
}

void hpn_page_release(void)
{
    char line[HITEC_PANEL_LINE];

    page_open(W_RELEASE, TR(HP_RELEASE), TR(HP_HOLD_RELEASE));
    snprintf(line, sizeof(line), TR(HP_RELEASE_1),
             (unsigned)hitec_prog_port(&hp.prog) + 1u);
    page_add(line);
    page_add(TR(HP_RELEASE_2));
    page_add(TR(KP_LEAVE_2));
    page_add(TR(HP_RELEASE_3));
}

void hpn_page_apply(void)
{
    const int kind = hp.page.kind;

    hpn_close_editors();
    if (kind == W_LEAVE) {
        (void)hitec_prog_leave(&hp.prog);
    } else {
        (void)hitec_prog_release(&hp.prog);
    }
    hpn_moved();
}

/* ---------------------------------------------------------------- options */

/**
 * @brief The options as the settings keep them: the voltage and current,
 *        which the KST programmer shares, and the series.
 */
static void options_keep(void)
{
    settings_set(SET_SERVO_PROG_V,
                 (float)hp.prog.supply_mv / 1000.0f);
    settings_set(SET_SERVO_PROG_I, (float)hp.prog.supply_ma / 1000.0f);
    settings_set(SET_HITEC_SERIES, (float)hitec_prog_series(&hp.prog));
    settings_request_save();
}

void hpn_options_load(void)
{
    const long mv = lroundf(settings_get(SET_SERVO_PROG_V) * 10.0f) * 100L;
    const long ma = lroundf(settings_get(SET_SERVO_PROG_I) * 1000.0f);
    const int series = settings_get_int(SET_HITEC_SERIES);

    hitec_prog_supply_set(&hp.prog, (uint16_t)(mv > 0 ? mv : 0),
                          (uint16_t)(ma > 0 ? ma : 0));
    (void)hitec_prog_series_set(&hp.prog, series == (int)HITEC_CLASSIC
                                             ? (uint8_t)HITEC_CLASSIC
                                             : (uint8_t)HITEC_D);
}

/* ---------------------------------------------------------------- actions */

bool hpn_row_live(const hpn_row_t *r)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(p);

    if (r->kind != K_ACT) {
        return false;
    }
    switch (r->what) {
    case A_PORT:
        return st == HITEC_PROG_PWM || st == HITEC_PROG_HELD
               || st == HITEC_PROG_OTHER || st == HITEC_PROG_OPENING
               || st == HITEC_PROG_ENTERING || st == HITEC_PROG_READING;
    case A_SERIES:
    case A_MORE:
        return st == HITEC_PROG_PWM;
    case A_READ:
        return st == HITEC_PROG_HELD && hitec_prog_in_mode(p)
               && !hitec_prog_stopped(p);
    default:
        /* Not with a STOP latched here: the IOmcu would refuse it. */
        return st == HITEC_PROG_HELD && hitec_prog_stopped(p) && !p->f.stopped;
    }
}

void hpn_act(const hpn_row_t *r)
{
    hitec_prog_t *p = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(p);
    char label[HITEC_PANEL_LINE];

    switch (r->what) {
    case A_PORT:
        if (st == HITEC_PROG_PWM) {
            /* Against the SUPPLY screen's limits as they are now. */
            const supply_caps_t caps = supply_screen_caps();

            hitec_prog_supply_caps(p,
                                   (uint16_t)lroundf(caps.v_max * 1000.0f),
                                   (uint16_t)lroundf(caps.i_max * 1000.0f));
            (void)hitec_prog_connect(p);
        } else if (st == HITEC_PROG_HELD) {
            hpn_page_leave();
        } else if (st == HITEC_PROG_OTHER) {
            hpn_page_release();
        } else {
            hitec_prog_abort(p);
        }
        break;
    case A_SERIES:
        choice_open(C_SERIES, TR(HP_SERIES));
        choice_add(TR(HP_SERIES_D), (int)HITEC_D);
        snprintf(label, sizeof(label), "%s  %s", TR(HP_SERIES_C),
                 TR(HP_SERIES_C_HELP));
        choice_add(label, (int)HITEC_CLASSIC);
        break;
    case A_READ:
        (void)hitec_prog_read(p);
        break;
    case A_MORE: {
        const unsigned mv = hitec_prog_mv(p);

        choice_open(C_MORE, TR(KP_MORE));
        snprintf(label, sizeof(label), TR(KP_SUPPLY_V), mv / 1000u,
                 mv % 1000u / 100u);
        choice_add(label, MORE_VOLTAGE);
        snprintf(label, sizeof(label), TR(KP_SUPPLY_A),
                 (unsigned)hitec_prog_ma(p));
        choice_add(label, MORE_CURRENT);
        break;
    }
    default:
        (void)hitec_prog_clear(p);
        break;
    }
    hpn_moved();
}

void hpn_choice_done(int value)
{
    hitec_prog_t *p = &hp.prog;
    const int kind = hp.ch.kind;

    hp.ch.open = false;
    if (kind == C_SERIES) {
        if (hitec_prog_series_set(p, (uint8_t)value)) {
            options_keep();
        }
    } else if (value == MORE_VOLTAGE) {
        ui_keypad_open(&hp.kp, hp.area, TR(KP_PROG_VOLTAGE), "V",
                       (float)hitec_prog_mv(p) / 1000.0f,
                       (float)HITEC_PROG_MV_MIN / 1000.0f,
                       (float)HITEC_PROG_MV_MAX / 1000.0f, 1);
        hp.kp_what = KP_VOLTAGE;
    } else {
        ui_keypad_open(&hp.kp, hp.area, TR(KP_PROG_CURRENT), "mA",
                       (float)hitec_prog_ma(p), (float)PDMINI_I_MIN_MA,
                       (float)PDMINI_I_MAX_MA, 0);
        hp.kp_what = KP_CURRENT;
    }
    ++hp.rev;
}

void hpn_keypad_done(float v)
{
    hitec_prog_t *p = &hp.prog;

    if (hp.kp_what == KP_VOLTAGE) {
        const long mv = lroundf(v * 10.0f) * 100L;

        if (mv >= (long)HITEC_PROG_MV_MIN && mv <= (long)HITEC_PROG_MV_MAX) {
            hitec_prog_supply_set(p, (uint16_t)mv, p->supply_ma);
            options_keep();
        }
    } else if (hp.kp_what == KP_CURRENT) {
        const long ma = lroundf(v);

        if (ma >= (long)PDMINI_I_MIN_MA && ma <= (long)PDMINI_I_MAX_MA) {
            hitec_prog_supply_set(p, p->supply_mv, (uint16_t)ma);
            options_keep();
        }
    }
    hp.kp_what = KP_NONE;
    hpn_moved();
}
