/**
 * @file hitec_panel_edit.c
 * @brief The actions and editors of the Hitec programmer's overlay: the
 *        SERVO tab's buttons, the lists (SERIES, MORE, RESTORE, a value's
 *        choices), the keypad of the supply and of a value, and the options
 *        kept in the settings.  The hold pages are hitec_panel_page.c's,
 *        FILES and its keyboard hitec_panel_file.c's.
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

#include "hitec_reg.h"
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
    if (hp.page.open && hp.page.kind == W_SOFT) {
        /* CHECK THIS CHANGE closed without its hold: the edit is dropped. */
        hitec_prog_cancel(&hp.prog);
    }
    if (hp.page.open
        && (hp.page.kind == W_LOAD || hp.page.kind == W_LOAD_MORE)) {
        /* CHECK THIS FILE closed without its hold: the file is dropped. */
        hitec_prog_load_cancel(&hp.prog);
    }
    ui_keypad_close(&hp.kp);
    ui_textkey_close(&hp.tk);
    hp.kp_what = KP_NONE;
    hp.ch.open = false;
    hp.page.open = false;
    hp.page.down = false;
    ui_hold_reset(&hp.page.hold);
}

/**
 * @brief Opens the list of @p kind with its title; hpn_choice_add() fills it.
 * @param kind  C_*
 * @param title the title
 */
void hpn_choice_open(int kind, const char *title)
{
    hp.ch.open = true;
    hp.ch.kind = kind;
    hp.ch.count = 0;
    hp.ch.current = INT_MIN;
    snprintf(hp.ch.title, sizeof(hp.ch.title), "%s", title);
}

/**
 * @brief Adds an entry to the open list while it has room.
 * @param label the entry's label
 * @param value its value
 */
void hpn_choice_add(const char *label, int value)
{
    if (hp.ch.count < HP_CHOICE_MAX) {
        snprintf(hp.ch.labels[hp.ch.count], sizeof(hp.ch.labels[0]), "%s",
                 label);
        hp.ch.values[hp.ch.count++] = value;
    }
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

/* ------------------------------------------------------------ value edits */

hitec_prog_edit_t hpn_edit_take(int32_t value)
{
    const hitec_prog_edit_t r = hitec_prog_edit(&hp.prog, hp.ed_id, value);

    if (r == HITEC_PROG_EDIT_CHECK) {
        hpn_page_soft();
    }
    return r;
}

/**
 * @brief Opens the keypad for the value being edited.
 * @param title    its title
 * @param unit     the unit
 * @param value    the value as it is
 * @param min      the range's lower end
 * @param max      its upper end
 * @param decimals digits after the point
 */
static void value_keypad(const char *title, const char *unit, float value,
                         float min, float max, int decimals)
{
    ui_keypad_open(&hp.kp, hp.area, title, unit, value, min, max, decimals);
    hp.kp_what = KP_VALUE;
}

/**
 * @brief The list of every value of a parameter with few of them.
 * @param title its title
 * @param info  the parameter's row
 * @param now   the draft's value; INT32_MIN not known
 */
static void value_list(const char *title, const hitec_param_info_t *info,
                       int32_t now)
{
    char label[HITEC_PANEL_LINE];

    hpn_choice_open(C_VALUE, title);
    hp.ch.current = now != INT32_MIN ? (int)now : INT_MIN;
    for (int32_t v = info->min; v <= info->max; v += info->step) {
        hpn_fmt_param(hp.ed_id, v, label, sizeof(label));
        hpn_choice_add(label, (int)v);
    }
}

/**
 * @brief The editor of row @p r: a list of a few values, the D series
 *        FAILSAFE's list, or the keypad.
 * @param r    the row
 * @param info its parameter
 * @param now  the draft's value; INT32_MIN where it fits no encoding
 */
static void editor_for(const hpn_row_t *r, const hitec_param_info_t *info,
                       int32_t now)
{
    const char *title = hpn_label(r);

    hp.ed_id = r->what;
    switch (info->unit) {
    case HITEC_UNIT_CHOICE:
    case HITEC_UNIT_STEP:
    case HITEC_UNIT_OVERLOAD:
    case HITEC_UNIT_PERCENT:
        if ((info->max - info->min) / info->step + 1 <= HP_CHOICE_MAX) {
            value_list(title, info, now);
        } else {
            value_keypad(title, "%",
                         now != INT32_MIN ? (float)now : (float)info->max,
                         (float)info->min, (float)info->max, 0);
        }
        break;
    case HITEC_UNIT_FS_US:
        hpn_choice_open(C_VALUE, title);
        hp.ch.current = now == HITEC_FS_OFF || now == HITEC_FS_LIMP
                            ? (int)now
                        : now != INT32_MIN ? HP_FS_POSITION
                                           : INT_MIN;
        hpn_choice_add(TR(OFF), HITEC_FS_OFF);
        hpn_choice_add(TR(HP_FS_LIMP), HITEC_FS_LIMP);
        hpn_choice_add(TR(HP_POSITION), HP_FS_POSITION);
        break;
    case HITEC_UNIT_QUARTER_US:
        value_keypad(title, "us",
                     now != INT32_MIN ? (float)now / 4.0f : 1500.0f,
                     (float)info->min / 4.0f, (float)info->max / 4.0f, 2);
        break;
    default:
        value_keypad(title, "",
                     now != INT32_MIN ? (float)now : (float)info->min,
                     (float)info->min, (float)info->max, 0);
        break;
    }
}

void hpn_edit_open(const hpn_row_t *r)
{
    const uint8_t layout = hitec_prog_layout(&hp.prog);
    const hitec_param_info_t *info = hitec_param(layout, r->what);
    const hitec_image_t *draft = hitec_prog_draft(&hp.prog);
    hitec_value_t cur;

    /* A row is live only for a writable value of a servo read. */
    if (info != NULL && draft != NULL && info->step != 0u) {
        editor_for(r, info,
                   hitec_param_get(draft, layout, r->what, &cur)
                           == HITEC_GET_OK
                       ? cur.value : INT32_MIN);
    }
}

/**
 * @brief The D series FAILSAFE's POSITION on the keypad.
 */
static void fs_position_keypad(void)
{
    const uint8_t layout = hitec_prog_layout(&hp.prog);
    const hitec_param_info_t *info = hitec_param(layout, hp.ed_id);
    const hitec_image_t *draft = hitec_prog_draft(&hp.prog);
    hitec_value_t cur;

    /* FAILSAFE's list opened on a servo read. */
    if (info != NULL && draft != NULL) {
        float now = 1500.0f;

        if (hitec_param_get(draft, layout, hp.ed_id, &cur) == HITEC_GET_OK
            && cur.value >= info->min) {
            now = (float)cur.value;
        }
        value_keypad(TR(HP_POSITION), "us", now, (float)info->min,
                     (float)info->max, 0);
    }
}

/* ---------------------------------------------------------------- actions */

/**
 * @brief MORE's entries while the port is held: DISCARD EDITS with an edit
 *        standing, REPAIR CHECKSUM with a classic sum that is wrong,
 *        FACTORY DEFAULT for a classic servo read.
 * @param add true to add them to the open list
 * @return the entries
 */
static int more_held(bool add)
{
    const hitec_prog_t *p = &hp.prog;
    hitec_image_t factory;
    int n = 0;

    if (hitec_prog_changes(p) != 0u) {
        if (add) {
            hpn_choice_add(TR(KP_DISCARD), MORE_DISCARD);
        }
        ++n;
    }
    if (hitec_prog_sum_bad(p)) {
        if (add) {
            hpn_choice_add(TR(HP_MORE_REPAIR), MORE_REPAIR);
        }
        ++n;
    }
    if (hitec_prog_factory_image(p, &factory)) {
        if (add) {
            hpn_choice_add(TR(HP_MORE_FACTORY), MORE_FACTORY);
        }
        ++n;
    }
    return n;
}

bool hpn_row_live(const hpn_row_t *r)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(p);

    if (r->kind != K_ACT) {
        return hitec_prog_can_edit(p) && hitec_prog_editable(p, r->what);
    }
    switch (r->what) {
    case A_PORT:
        return st == HITEC_PROG_PWM || st == HITEC_PROG_HELD
               || st == HITEC_PROG_OTHER || st == HITEC_PROG_OPENING
               || st == HITEC_PROG_ENTERING || st == HITEC_PROG_READING
               || st == HITEC_PROG_RECORDING || st == HITEC_PROG_WRITING
               || st == HITEC_PROG_BACKUP;
    case A_SERIES:
        return st == HITEC_PROG_PWM;
    case A_MORE:
        return st == HITEC_PROG_PWM
               || (st == HITEC_PROG_HELD && p->have_servo
                   && more_held(false) > 0);
    case A_READ:
        return st == HITEC_PROG_HELD && hitec_prog_in_mode(p)
               && !hitec_prog_stopped(p);
    case A_ENABLE:
        /* Off is always taken; on wants an editable, SAFE port. */
        return hitec_prog_write_on(p)
               || (hitec_prog_can_edit(p) && !p->f.stopped);
    case A_RESTORE:
        return st == HITEC_PROG_HELD
               && (hitec_prog_record(p) || hitec_prog_backup_differs(p));
    case A_FILES: {
        /* Without a card the tap says so. */
        const hitec_prog_msg_id_t m = hitec_prog_files_gate(p);

        return m == HITEC_PROG_MSG_NONE || m == HITEC_PROG_MSG_CARD_NONE;
    }
    case A_WRITE:
        return st == HITEC_PROG_HELD && hitec_prog_changes(p) != 0u;
    default:
        /* Not with a STOP latched here: the IOmcu would refuse it. */
        return st == HITEC_PROG_HELD && hitec_prog_stopped(p) && !p->f.stopped;
    }
}

/**
 * @brief A write of @p kind asked for: the switch's page while it is off,
 *        the write's own page where the write is taken, else the write's
 *        refusal, said.
 * @param kind hitec_prog_wr_t
 */
static void ask_write(uint8_t kind)
{
    hitec_prog_t *p = &hp.prog;
    const hitec_prog_msg_id_t m = hitec_prog_write_gate(p, kind);
    const bool go = m == HITEC_PROG_MSG_NONE;

    if (m == HITEC_PROG_MSG_WRITE_OFF) {
        hpn_page_enable();
    } else if (kind == (uint8_t)HITEC_PROG_WR_EDITS) {
        if (go) {
            hpn_page_write();
        } else {
            (void)hitec_prog_write(p);
        }
    } else if (kind == (uint8_t)HITEC_PROG_WR_REPAIR) {
        if (go) {
            hpn_page_repair();
        } else {
            (void)hitec_prog_repair(p);
        }
    } else if (kind == (uint8_t)HITEC_PROG_WR_FACTORY) {
        if (go) {
            hpn_page_factory();
        } else {
            (void)hitec_prog_factory(p);
        }
    } else if (kind == (uint8_t)HITEC_PROG_WR_BACKUP) {
        if (go) {
            hpn_page_backup();
        } else {
            (void)hitec_prog_restore_backup(p);
        }
    } else if (go) {
        hpn_page_restore();
    } else {
        (void)hitec_prog_restore(p);
    }
}

/**
 * @brief The action of the PORT row.
 * @param p the programmer
 */
static void port_act(hitec_prog_t *p)
{
    const hitec_prog_state_t st = hitec_prog_state(p);

    if (st == HITEC_PROG_PWM) {
        /* Against the SUPPLY screen's limits as they are now. */
        const supply_caps_t caps = supply_screen_caps();

        hitec_prog_supply_caps(p, (uint16_t)lroundf(caps.v_max * 1000.0f),
                               (uint16_t)lroundf(caps.i_max * 1000.0f));
        (void)hitec_prog_connect(p);
    } else if (st == HITEC_PROG_HELD) {
        hpn_page_leave();
    } else if (st == HITEC_PROG_OTHER) {
        hpn_page_release();
    } else {
        hitec_prog_abort(p);
    }
}

/**
 * @brief MORE opened: the supply's entries in PWM, the session's while
 *        held.
 * @param p the programmer
 */
static void more_open(const hitec_prog_t *p)
{
    char label[HITEC_PANEL_LINE];

    hpn_choice_open(C_MORE, TR(KP_MORE));
    if (hitec_prog_state(p) == HITEC_PROG_HELD) {
        (void)more_held(true);
        return;
    }
    snprintf(label, sizeof(label), TR(KP_SUPPLY_V), hitec_prog_mv(p) / 1000u,
             hitec_prog_mv(p) % 1000u / 100u);
    hpn_choice_add(label, MORE_VOLTAGE);
    snprintf(label, sizeof(label), TR(KP_SUPPLY_A),
             (unsigned)hitec_prog_ma(p));
    hpn_choice_add(label, MORE_CURRENT);
}

void hpn_act(const hpn_row_t *r)
{
    hitec_prog_t *p = &hp.prog;
    char label[HITEC_PANEL_LINE];

    if (r->kind != K_ACT) {
        hpn_edit_open(r);
        hpn_moved();
        return;
    }
    /* The taps that look at the card slot, as KST's do. */
    hp.card_look = hp.card_look || r->what == A_PORT || r->what == A_FILES
                   || r->what == A_RESTORE || r->what == A_MORE
                   || r->what == A_ENABLE || r->what == A_WRITE;
    switch (r->what) {
    case A_PORT:
        port_act(p);
        break;
    case A_SERIES:
        hpn_choice_open(C_SERIES, TR(HP_SERIES));
        hp.ch.current = (int)hitec_prog_series(p);
        hpn_choice_add(TR(HP_SERIES_D), (int)HITEC_D);
        snprintf(label, sizeof(label), "%s  %s", TR(HP_SERIES_C),
                 TR(HP_SERIES_C_HELP));
        hpn_choice_add(label, (int)HITEC_CLASSIC);
        break;
    case A_READ:
        (void)hitec_prog_read(p);
        break;
    case A_MORE:
        more_open(p);
        break;
    case A_ENABLE:
        if (hitec_prog_write_on(p)) {
            (void)hitec_prog_write_enable(p, false);
        } else {
            hpn_page_enable();
        }
        break;
    case A_RESTORE: {
        const hitec_prog_msg_id_t m =
            hitec_prog_write_gate(p, (uint8_t)HITEC_PROG_WR_RESTORE);

        hpn_choice_open(C_RESTORE, TR(KP_RESTORE));
        if (hitec_prog_backup_differs(p)) {
            hpn_choice_add(TR(KP_FROM_SESSION), RS_BACKUP);
        }
        if (hitec_prog_record(p)) {
            if (m != HITEC_PROG_MSG_REC_OTHER
                && m != HITEC_PROG_MSG_MUST_READ) {
                hpn_choice_add(TR(KP_FROM_JOURNAL), RS_RESTORE);
            }
            hpn_choice_add(TR(KP_FORGET), RS_FORGET);
        }
        break;
    }
    case A_FILES:
        hpn_files_ask();
        break;
    case A_WRITE:
        ask_write((uint8_t)HITEC_PROG_WR_EDITS);
        break;
    default:
        (void)hitec_prog_clear(p);
        break;
    }
    hpn_moved();
}

/**
 * @brief An entry of MORE or RESTORE.
 * @param p     the programmer
 * @param kind  C_MORE or C_RESTORE
 * @param value the entry's value
 */
static void list_done(hitec_prog_t *p, int kind, int value)
{
    if (kind == C_RESTORE) {
        if (value == RS_RESTORE) {
            ask_write((uint8_t)HITEC_PROG_WR_RESTORE);
        } else if (value == RS_BACKUP) {
            ask_write((uint8_t)HITEC_PROG_WR_BACKUP);
        } else {
            (void)hitec_prog_forget(p);
        }
    } else if (value == MORE_DISCARD) {
        hitec_prog_discard(p);
    } else if (value == MORE_REPAIR) {
        ask_write((uint8_t)HITEC_PROG_WR_REPAIR);
    } else if (value == MORE_FACTORY) {
        ask_write((uint8_t)HITEC_PROG_WR_FACTORY);
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
    } else if (kind == C_FILES) {
        hpn_files_choice(value);
    } else if (kind == C_VALUE) {
        if (value == HP_FS_POSITION) {
            fs_position_keypad();
        } else {
            (void)hpn_edit_take((int32_t)value);
        }
    } else {
        list_done(p, kind, value);
    }
    hpn_moved();
    ++hp.rev;
}

/**
 * @brief The keypad's value of a parameter: the edit, or the reason it is
 *        refused with the keypad open again.
 * @param v the value typed
 */
static void value_done(float v)
{
    const hitec_param_info_t *info =
        hitec_param(hitec_prog_layout(&hp.prog), hp.ed_id);
    const bool quarter =
        info != NULL && info->unit == (uint8_t)HITEC_UNIT_QUARTER_US;
    const long value = quarter ? lroundf(v * 4.0f) : lroundf(v);

    if (hpn_edit_take((int32_t)value) == HITEC_PROG_EDIT_REFUSED) {
        char lines[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

        const int n =
            hitec_panel_message_lines(hitec_prog_message(&hp.prog), lines);

        /* A refusal always says why; NULL keeps the keypad's own line. */
        ui_keypad_refuse_why(&hp.kp, n > 0 ? lines[0] : NULL);
        return;
    }
    hp.kp_what = KP_NONE;
}

void hpn_keypad_done(float v)
{
    hitec_prog_t *p = &hp.prog;

    if (hp.kp_what == KP_VALUE) {
        value_done(v);
        hpn_moved();
        return;
    }
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
