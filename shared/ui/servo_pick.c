/**
 * @file servo_pick.c
 * @brief The programmer a run writes with, chosen on the screen that runs
 *        it: LIMIT RUN's and RUDDER AUTO's SERVO, KST/CHASERVO, HITEC D
 *        SERIES or HITEC CLASSIC.
 *
 * Each screen keeps its own saved value: SET_LIMIT_FAMILY for LIMIT RUN,
 * SET_RAUTO_FAMILY for RUDDER AUTO, in servo_limrun_family_t's order.  A
 * tap on the READY card's SERVO row opens the choice list over the left
 * column (the PORT button's list, CH_FAMILY); the settings list's SERVO row
 * opens the same three entries as its own choice list.  A pick writes that
 * screen's setting and asks for a save.  Nothing else writes either value:
 * opening the KST or HITEC programmer, or a pick on the other screen,
 * leaves it as it is.  The Hitec series of a run is its own value's, not
 * the HITEC page's SET_HITEC_SERIES.
 *
 * A pick is refused off a programming port (SVL_PORT_ONLY) and wherever the
 * screen's SETTINGS list is refused (svs_list_refused(): ARMED, a run, a
 * sweep, a sequence, a port held by a programmer, a press held).  With HITEC
 * D SERIES the settings list shows D RECEIVE (SET_HITEC_RX) under SERVO,
 * the HITEC page's own setting.
 *
 * HITEC's READY card has a SERIES row of its own: a tap opens D SERIES and
 * CLASSIC (CH_SERIES), a pick writes SET_HITEC_SERIES, the series the
 * HITEC page's next CONNECT enters, and reloads the page's options.  It is
 * refused while the page holds the port or a step of its session runs.
 *
 * KST, Chaservo and Hitec are names or trademarks of their owners; they
 * are used here only to say which servos the runs work with.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "hitec_panel.h"
#include "hitec_wire.h"
#include "prog_registry.h"
#include "servo_limrun.h"
#include "settings.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

setting_id_t svs_family_id(servo_fn_t fn)
{
    return fn == SERVO_FN_RAUTO ? SET_RAUTO_FAMILY : SET_LIMIT_FAMILY;
}

uint8_t svs_family_of(servo_fn_t fn)
{
    const int v = settings_get_int(svs_family_id(fn));
    return (v > 0 && v < (int)SERVO_LIMRUN_FAM_COUNT) ? (uint8_t)v
                                                      : (uint8_t)SERVO_LIMRUN_FAM_KST;
}

uint8_t lim_family(void)
{
    return svs_family_of(s.fn == SERVO_FN_RAUTO ? SERVO_FN_RAUTO
                                                : SERVO_FN_LIMRUN);
}

const char *svs_family_name(uint8_t fam)
{
    return ui_setting_option(SET_LIMIT_FAMILY,
                             fam < (uint8_t)SERVO_LIMRUN_FAM_COUNT ? fam : 0);
}

const char *svs_family_refused(void)
{
    const char *why = svs_list_refused();
    if (why == NULL && !lim_reads()) {
        why = TR(SVL_PORT_ONLY);
    }
    return why;
}

bool svs_family_d_receive(void)
{
    return (s.fn == SERVO_FN_LIMRUN || s.fn == SERVO_FN_RAUTO)
           && svs_family_of((servo_fn_t)s.fn) == SERVO_LIMRUN_FAM_HITEC_D;
}

void svs_family_open(void)
{
    const char *why = svs_family_refused();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    close_panels();
    open_choice(CH_FAMILY, svs_family_id((servo_fn_t)s.fn), TR(SV_FAM_TITLE));
    for (uint8_t f = 0u; f < (uint8_t)SERVO_LIMRUN_FAM_COUNT; ++f) {
        choice_add(svs_family_name(f), (int)f);
    }
    servo_invalidate();
    ++s.ctrl_rev;
}

void svs_family_pick(setting_id_t id, int value)
{
    const char *why = svs_family_refused();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    if (value < 0 || value >= (int)SERVO_LIMRUN_FAM_COUNT
        || (id != SET_LIMIT_FAMILY && id != SET_RAUTO_FAMILY)) {
        return;
    }
    if (settings_get_int(id) != value) {
        settings_set(id, (float)value);
        settings_request_save();
    }
    ++s.ctrl_rev;
    ++s.sl_rev;
    servo_invalidate();
}

/* ------------------------------------------------------ HITEC's SERIES */

/**
 * @brief Why HITEC's SERIES takes no pick now: the page holds the port or
 *        a step of its session runs, or a press is held.
 * @return the reason, NULL while it takes one
 */
static const char *series_refused(void)
{
    prog_session_t ps;
    hitec_panel_session(&ps);
    if (ps.held || ps.busy) {
        return TR(PRR_SERIES_HELD);
    }
    return svs_press_held() ? TR(FOOT_PRESS_HELD) : NULL;
}

void svs_series_open(void)
{
    const char *why = series_refused();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    char label[64];
    close_panels();
    open_choice(CH_SERIES, SET_HITEC_SERIES, TR(PRR_SERIES_TITLE));
    choice_add(TR(HP_SERIES_D), (int)HITEC_D);
    snprintf(label, sizeof(label), "%s  %s", TR(HP_SERIES_C),
             TR(HP_SERIES_C_HELP));
    choice_add(label, (int)HITEC_CLASSIC);
    servo_invalidate();
    ++s.ctrl_rev;
}

void svs_series_pick(int value)
{
    const char *why = series_refused();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    if (value != (int)HITEC_D && value != (int)HITEC_CLASSIC) {
        return;
    }
    if (settings_get_int(SET_HITEC_SERIES) != value) {
        settings_set(SET_HITEC_SERIES, (float)value);
        settings_request_save();
        hitec_panel_settings_changed();
    }
    ++s.ctrl_rev;
    servo_invalidate();
}

/* ------------------------------------------- the settings list's SERVO row */

/** @brief SERVO's value: the screen's family. @param out takes it
 *  @param n its size */
static void row_value(char *out, size_t n)
{
    snprintf(out, n, "%s", svs_family_name(svs_family_of((servo_fn_t)s.fn)));
}

/** @brief SERVO's choice list: the three families. @return the count */
static int row_choices(void)
{
    return (int)SERVO_LIMRUN_FAM_COUNT;
}

/** @brief Choice @p i's text. @param i the choice @return its text */
static const char *row_choice(int i)
{
    return svs_family_name((uint8_t)i);
}

/** @brief The family in force. @return its index */
static int row_chosen(void)
{
    return (int)svs_family_of((servo_fn_t)s.fn);
}

/** @brief Choice @p i tapped: the screen's family. @param i the choice */
static void row_pick(int i)
{
    svs_family_pick(svs_family_id((servo_fn_t)s.fn), i);
}

/** @brief Why SERVO takes no press: the target is no programming port.
 *  @return the reason, NULL while it takes one */
static const char *row_locked(void)
{
    return lim_reads() ? NULL : TR(SVL_PORT_ONLY);
}

const ui_setlist_custom_t svs_family_row = {
    .label = TX_SV_ROW_LIM_SERVO, .help = TX_SVL_FAMILY_HELP,
    .value_text = row_value, .choices = row_choices, .choice = row_choice,
    .chosen = row_chosen, .pick = row_pick, .locked = row_locked,
};

/* ---------------------------------------------------------- for the tests */

int servo_screen_limit_family(void)
{
    return (int)svs_family_of(SERVO_FN_LIMRUN);
}

int servo_screen_rauto_family(void)
{
    return (int)svs_family_of(SERVO_FN_RAUTO);
}

int servo_screen_family_choices(void)
{
    return (s.ch.open && (s.ch.target == CH_FAMILY || s.ch.target == CH_SERIES))
               ? s.ch.count : -1;
}

const char *servo_screen_family_choice(int i)
{
    return (i >= 0 && i < servo_screen_family_choices()) ? s.ch.labels[i]
                                                          : "";
}

const char *servo_screen_family_refused(void)
{
    return svs_family_refused();
}
