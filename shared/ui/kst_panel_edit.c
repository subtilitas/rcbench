/**
 * @file kst_panel_edit.c
 * @brief The editors of the programmer's overlay.
 *
 * The editors of the programmer's overlay for KST and Chaservo servos: the
 * list of choices, the keypad, the keyboard for a file's name, the
 * steppers, the actions of the SERVO page's rows and the options kept;
 * and whether a row takes input now.  The state and the seam are
 * kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <math.h>
#include <stdio.h>

#include "kst_port.h"
#include "kst_reg.h"
#include "link_pages.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/* ------------------------------------------------------------------ editors */

void kpn_close_editors(void)
{
    if (k.warn.open && k.warn.kind == W_SOFT) {
        kst_prog_soft_cancel(&k.prog);
    }
    ui_keypad_close(&k.kp);
    ui_textkey_close(&k.tk);
    k.name_wait = false;
    k.files_wait = false;
    k.ch.open = false;
    k.warn.open = false;
    k.warn.down = false;
    ui_hold_reset(&k.warn.hold);
}

/**
 * @brief Opens the list of choices of kind @p kind with its title; the entries
 *        follow in choice_add().
 */
static void choice_open(int kind, const char *title)
{
    k.ch.open = true;
    k.ch.kind = kind;
    k.ch.count = 0;
    snprintf(k.ch.title, sizeof(k.ch.title), "%s", title);
}

/* The FILES list is whole, with the entries around its files.  STRETCH's
 * list of every factor the hard limit leaves is held to CHOICE_MAX by
 * test_kst_panel_layout. */
_Static_assert(KST_PROG_FILES_MAX + 2u <= CHOICE_MAX,
               "the FILES list fits CHOICE_MAX entries");

/**
 * @brief Adds an entry to the open list while it has room, CHOICE_MAX at most.
 */
static void choice_add(const char *label, int value)
{
    if (k.ch.count < CHOICE_MAX) {
        snprintf(k.ch.labels[k.ch.count], sizeof(k.ch.labels[0]), "%s",
                 label);
        k.ch.values[k.ch.count++] = value;
    }
}

/** @brief Opens the file-name keyboard for the reason @p why. */
static void name_open(unsigned why)
{
    ui_textkey_open(&k.tk, k.area,
                    why == KST_PROG_MSG_NAME_EXISTS ? TR(KP_NAME_EXISTS)
                    : why == KST_PROG_MSG_NAME_BAD  ? TR(KP_NAME_BAD)
                                                    : TR(KP_NAME_TITLE),
                    k.name, KST_PANEL_NAME_TYPED);
}

/* The name is typed: the save, and the keyboard again for a name the card's
 * side does not take. */
void kpn_name_done(const char *name)
{
    snprintf(k.name, sizeof(k.name), "%.*s", (int)sizeof(k.name) - 1, name);
    if (kst_prog_save(&k.prog, k.name) == KST_PROG_MSG_NONE) {
        k.name_wait = true;
    } else if (kst_prog_message(&k.prog)->id == KST_PROG_MSG_NAME_BAD) {
        name_open(KST_PROG_MSG_NAME_BAD);
    }
}

/**
 * @brief The list of files as the card gave it, under the entry that saves.
 */
static void files_open(void)
{
    const kst_prog_t *p = &k.prog;
    unsigned total = 0;
    const unsigned n = kst_prog_file_count(p, &total);
    char title[64];

    const unsigned first = kst_prog_files_first(p);

    if (total > n && n != 0u) {
        snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), first + 1u,
                 first + n, total);
    } else {
        snprintf(title, sizeof(title), "%s", TR(KP_FILES_TITLE));
    }
    choice_open(C_FILES, title);
    choice_add(TR(KP_FILE_SAVE), FILES_SAVE);
    for (unsigned i = 0; i < n; ++i) {
        char label[KST_PANEL_LINE];
        const unsigned type = kst_prog_file_type(p, i);

        if (type != 0u) {
            snprintf(label, sizeof(label), "%-16s V%u",
                     kst_prog_file_name(p, i), type);
        } else {
            snprintf(label, sizeof(label), "%s", kst_prog_file_name(p, i));
        }
        choice_add(label, (int)i);
    }
    if (total > n) {
        choice_add(TR(KP_FILES_NEXT), FILES_NEXT);
    }
}

/** @brief Ask for the list from file @p first on. */
static void files_ask(unsigned first)
{
    k.files_seq = kst_prog_files_seq(&k.prog);
    k.files_wait = kst_prog_files(&k.prog, first) == KST_PROG_MSG_NONE;
}

/* What the card's side brought: the list, the name again, a file's
 * warnings. */
void kpn_card_moved(void)
{
    const kst_prog_t *p = &k.prog;
    const unsigned id = kst_prog_message(p)->id;

    if (kst_prog_card_busy(p)) {
        return;
    }
    if (k.files_wait) {
        k.files_wait = false;
        if (kst_prog_files_seq(p) != k.files_seq && kpn_programming()
            && kst_panel_editor() == 0) {
            files_open();
        }
    }
    if (k.name_wait) {
        k.name_wait = false;
        if ((id == KST_PROG_MSG_NAME_BAD || id == KST_PROG_MSG_NAME_EXISTS)
            && kpn_programming() && kst_panel_editor() == 0) {
            name_open(id);
        }
    }
    if (kst_prog_soft_is_load(p) && kst_prog_soft_rules(p) != 0u
        && !k.warn.open) {
        ui_keypad_close(&k.kp);
        ui_textkey_close(&k.tk);
        k.ch.open = false;
        kpn_warn_file();
    }
}

/** @brief What an edit came to: a warning page for a soft limit. */
static void edited(const row_t *r, kst_prog_edit_t e)
{
    if (e == KST_PROG_EDIT_SOFT) {
        kpn_warn_soft(r);
    }
}

void kpn_edit_field(const row_t *r)
{
    const kst_field_id_t id = (kst_field_id_t)r->what;
    const kst_unit_t unit = (kst_unit_t)kst_field(id)->unit;
    const uint16_t raw = kst_field_get(kst_prog_draft(&k.prog), id);
    uint16_t lo = 0, hi = 0;
    int32_t dlo = 0, dhi = 0, dv = 0;
    float scale;

    if (unit == KST_UNIT_BOOL) {
        edited(r, kst_prog_edit(&k.prog, id,
                                (uint16_t)(raw != 0u ? 0u : 1u)));
        return;
    }
    if (!kst_prog_editable(&k.prog)
        || !kst_prog_range(&k.prog, id, &lo, &hi)
        || !kst_field_display(id, lo, &dlo)
        || !kst_field_display(id, hi, &dhi)
        || !kst_field_display(id, raw, &dv)) {
        /* The model says why not. */
        (void)kst_prog_edit(&k.prog, id, raw);
        return;
    }
    if (unit == KST_UNIT_FACTOR) {
        choice_open(C_STRETCH, kpn_row_label(r));
        for (unsigned v = lo; v <= hi; ++v) {
            char label[16];

            kpn_fmt_field(id, (uint16_t)v, label, sizeof(label));
            choice_add(label, (int)v);
        }
        k.kp_row = (int)(r - kpn_rows);
        return;
    }
    scale = kpn_decimals_of(id) != 0 ? 10.0f : 1.0f;
    ui_keypad_open(&k.kp, k.area, kpn_row_label(r), kpn_unit_of(id),
                   (float)dv / scale,
                   (float)(dlo < dhi ? dlo : dhi) / scale,
                   (float)(dlo < dhi ? dhi : dlo) / scale, kpn_decimals_of(id));
    k.kp_row = (int)(r - kpn_rows);
}

/**
 * @brief The highest programming voltage the keypad takes: a standard servo's
 *        until HV SERVO is on, then the port's.
 */
static uint16_t supply_max_mv(void)
{
    return k.hv ? (uint16_t)KST_PORT_SUPPLY_MAX_MV
                : (uint16_t)KST_PANEL_STD_MV;
}

/**
 * @brief A voltage as the settings hold it: a standard servo's at most, and the
 *        port's lowest at least.
 */
static uint16_t std_mv(long mv)
{
    if (mv < (long)KST_PORT_SUPPLY_MIN_MV) {
        return (uint16_t)KST_PORT_SUPPLY_MIN_MV;
    }
    return mv > (long)KST_PANEL_STD_MV ? (uint16_t)KST_PANEL_STD_MV
                                       : (uint16_t)mv;
}

/* The programmer's options as the settings keep them.  A voltage above a
 * standard servo's is the session's alone: the settings keep
 * KST_PANEL_STD_MV for it. */
void kpn_options_keep(void)
{
    settings_set(SET_SERVO_PROG_V, (float)std_mv(kst_prog_supply_mv(&k.prog))
                                       / 1000.0f);
    settings_set(SET_SERVO_PROG_I,
                 (float)kst_prog_supply_ma(&k.prog) / 1000.0f);
    settings_set(SET_SERVO_PROG_WAIT, (float)kst_prog_wait_ms(&k.prog));
    settings_request_save();
}

/* The options from the settings.  A stored voltage above a standard
 * servo's is never used, with HV SERVO on or off: it is loaded as
 * KST_PANEL_STD_MV and stored as that.  A voltage above it comes from the
 * warning page and its hold alone (W_HV). */
void kpn_options_load(void)
{
    const long stored = lroundf(settings_get(SET_SERVO_PROG_V) * 10.0f) * 100L;
    const uint16_t mv = std_mv(stored);
    const long ma = lroundf(settings_get(SET_SERVO_PROG_I) * 1000.0f);
    const int wait = settings_get_int(SET_SERVO_PROG_WAIT);

    if (stored != (long)mv) {
        settings_set(SET_SERVO_PROG_V, (float)mv / 1000.0f);
        settings_request_save();
    }
    kst_prog_supply_set(&k.prog, mv, (uint16_t)ma);
    (void)kst_prog_wait_set(&k.prog, (uint32_t)wait);
}

void kpn_keypad_done(float v)
{
    if (k.kp_row == KP_VOLTAGE) {
        const long mv = lroundf(v * 10.0f) * 100L;

        if (mv < (long)KST_PORT_SUPPLY_MIN_MV || mv > (long)supply_max_mv()) {
            return;
        }
        if (mv > (long)KST_PANEL_STD_MV) {
            kpn_warn_hv((uint16_t)mv);
        } else {
            kst_prog_supply_set(&k.prog, (uint16_t)mv,
                                kst_prog_supply_ma(&k.prog));
            kpn_options_keep();
        }
        return;
    }
    if (k.kp_row == KP_CURRENT) {
        const long ma = lroundf(v);

        if (ma >= (long)PDMINI_I_MIN_MA && ma <= (long)PDMINI_I_MAX_MA) {
            kst_prog_supply_set(&k.prog, kst_prog_supply_mv(&k.prog),
                                (uint16_t)ma);
            kpn_options_keep();
        }
        return;
    }
    if (k.kp_row == KP_WAIT) {
        const long ms = lroundf(v);

        if (ms >= 0L && kst_prog_wait_set(&k.prog, (uint32_t)ms)) {
            kpn_options_keep();
        }
        return;
    }
    {
        const row_t *r = &kpn_rows[k.kp_row];
        const kst_field_id_t id = (kst_field_id_t)r->what;
        const float scale = kpn_decimals_of(id) != 0 ? 10.0f : 1.0f;
        uint16_t raw = 0;

        if (!kst_field_raw_from_display(id, (int32_t)lroundf(v * scale),
                                        &raw)) {
            raw = 0xFFFFu;      /* outside the field: the model says so */
        }
        edited(r, kst_prog_edit(&k.prog, id, raw));
    }
}

/* One step of a plus or minus key, in the direction the shown value moves. */
void kpn_step(const row_t *r, bool up)
{
    const kst_field_id_t id = (kst_field_id_t)r->what;
    int32_t a = 0, b = 1;
    bool rising;

    (void)kst_field_display(id, 0, &a);
    (void)kst_field_display(id, 1, &b);
    rising = b >= a;
    edited(r, kst_prog_step(&k.prog, id, up == rising ? 1 : -1));
}

/* A LEAVE whose wait runs: an abort keeps the session. */
bool kpn_leave_waits(const kst_prog_t *p)
{
    return kst_prog_state(p) == KST_PROG_LEAVING
        && kst_prog_op(p) == LINK_KST_OP_LEAVE;
}

void kpn_act(const row_t *r)
{
    kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    /* A card put in, taken out or swapped since the last look is found
     * before the next tap. */
    k.card_look = k.card_look || p->fed;
    switch (r->what) {
    case A_PORT:
        if (st == KST_PROG_PWM) {
            /* Against the SUPPLY screen's limits as they are now. */
            const supply_caps_t caps = supply_screen_caps();

            kst_prog_supply_caps(p, (uint16_t)lroundf(caps.v_max * 1000.0f),
                                 (uint16_t)lroundf(caps.i_max * 1000.0f));
            kst_prog_supply_floor(p, (uint16_t)KST_PORT_SUPPLY_MIN_MV);
            (void)kst_prog_connect(p, kpn_port_no());
        } else if (st == KST_PROG_PROGRAMMING) {
            kpn_warn_leave();
        } else if (st == KST_PROG_ENTERING || st == KST_PROG_WORKING
                   || kpn_leave_waits(p)) {
            kst_prog_abort(p);
        }
        break;
    case A_READ:
        (void)kst_prog_read(p);
        break;
    case A_FILES:
        files_ask(0);
        break;
    case A_RESTORE:
        choice_open(C_RESTORE, TR(KP_RESTORE_TITLE));
        for (unsigned from = KST_PROG_FROM_SESSION;
             from < (unsigned)KST_PROG_FROM_COUNT; ++from) {
            if (kst_prog_restore_offered(p, (kst_prog_from_t)from)) {
                choice_add(ui_tr(kpn_from_text(from)), (int)from);
            }
        }
        if (p->journal) {
            choice_add(TR(KP_FORGET), RESTORE_FORGET);
        }
        break;
    case A_MORE:
        choice_open(C_MORE, TR(KP_MORE));
        if (st == KST_PROG_PWM) {
            char label[KST_PANEL_LINE];
            const unsigned mv = kst_prog_supply_mv(p);

            snprintf(label, sizeof(label), TR(KP_SUPPLY_V), mv / 1000u,
                     mv % 1000u / 100u);
            choice_add(label, MORE_VOLTAGE);
            snprintf(label, sizeof(label), TR(KP_SUPPLY_A),
                     (unsigned)kst_prog_supply_ma(p));
            choice_add(label, MORE_CURRENT);
            snprintf(label, sizeof(label), TR(KP_PWM_WAIT),
                     (unsigned)kst_prog_wait_ms(p));
            choice_add(label, MORE_WAIT);
        } else {
            choice_add(TR(KP_VERIFY), MORE_VERIFY);
            choice_add(TR(KP_DISCARD), MORE_DISCARD);
            choice_add(TR(KP_RELEASE_TITLE), MORE_RELEASE);
        }
        break;
    case A_ENABLE:
        if (kst_prog_write_enabled(p)) {
            (void)kst_prog_enable(p, false);
        } else if (kst_prog_enable_check(p) == KST_PROG_MSG_NONE) {
            /* It can be: off until the hold says so. */
            kpn_warn_enable();
        } else {
            (void)kst_prog_enable(p, true);     /* says why not */
        }
        break;
    default:
        if (kst_prog_write_check(p) == KST_PROG_MSG_NONE) {
            kpn_warn_write();
        } else if (kst_prog_write_check(p) == KST_PROG_MSG_NEED_ENABLE
                   && kst_prog_enable_check(p) == KST_PROG_MSG_NONE) {
            /* The switch is all the write waits for: its page, and off
             * until the hold. */
            kpn_warn_enable();
        } else {
            (void)kst_prog_write(p);    /* says why not */
        }
        break;
    }
}

void kpn_choice_done(int value)
{
    kst_prog_t *p = &k.prog;
    const int kind = k.ch.kind;

    k.ch.open = false;
    if (kind == C_STRETCH) {
        const row_t *r = &kpn_rows[k.kp_row];

        edited(r, kst_prog_edit(p, (kst_field_id_t)r->what, (uint16_t)value));
    } else if (kind == C_RESTORE) {
        kst_prog_restore_t plan;

        if (value == RESTORE_FORGET) {
            kst_prog_forget(p);
        } else if (kst_prog_restore_plan(p, (kst_prog_from_t)value, &plan)
                   == KST_PROG_MSG_NONE) {
            if (kst_prog_write_enabled(p)) {
                if (plan.soft != 0u) {
                    kpn_warn_check(&plan);
                } else {
                    kpn_warn_restore(&plan);
                }
            } else {
                (void)kst_prog_restore(p, &plan);   /* says why not */
            }
        }
    } else if (kind == C_FILES) {
        if (value == FILES_SAVE) {
            snprintf(k.name, sizeof(k.name), "%s", kst_prog_preset_name(p));
            name_open(KST_PROG_MSG_NONE);
        } else if (value == FILES_NEXT) {
            unsigned total = 0;
            const unsigned next = kst_prog_files_first(p)
                                + kst_prog_file_count(p, &total);

            /* Behind the last file the list starts again. */
            files_ask(next < total ? next : 0u);
        } else {
            (void)kst_prog_load(p, kst_prog_file_name(p, (unsigned)value));
        }
    } else if (value == MORE_VOLTAGE) {
        ui_keypad_open(&k.kp, k.area, TR(KP_PROG_VOLTAGE), "V",
                       (float)kst_prog_supply_mv(p) / 1000.0f,
                       (float)KST_PORT_SUPPLY_MIN_MV / 1000.0f,
                       (float)supply_max_mv() / 1000.0f, 1);
        k.kp_row = KP_VOLTAGE;
    } else if (value == MORE_CURRENT) {
        ui_keypad_open(&k.kp, k.area, TR(KP_PROG_CURRENT), "mA",
                       (float)kst_prog_supply_ma(p), (float)PDMINI_I_MIN_MA,
                       (float)PDMINI_I_MAX_MA, 0);
        k.kp_row = KP_CURRENT;
    } else if (value == MORE_WAIT) {
        ui_keypad_open(&k.kp, k.area, TR(KP_PWM_WAIT_TITLE), "ms",
                       (float)kst_prog_wait_ms(p), 0.0f,
                       (float)KST_PROG_WAIT_MAX_MS, 0);
        k.kp_row = KP_WAIT;
    } else if (value == MORE_VERIFY) {
        (void)kst_prog_verify(p);
    } else if (value == MORE_DISCARD) {
        kst_prog_discard(p);
    } else if (kst_prog_release_check(p) == KST_PROG_MSG_NONE) {
        kpn_warn_release();
    } else {
        (void)kst_prog_release(p);      /* says why not */
    }
}

/* Whether a row's control takes a tap now. */
bool kpn_row_live(const row_t *r)
{
    const kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    if (r->kind == K_LOCKED) {
        return false;
    }
    if (kst_prog_card_busy(p) && st == KST_PROG_PROGRAMMING) {
        /* A list or a load is on its way: it finds the servo as it is. */
        return false;
    }
    if (r->kind != K_ACT) {
        return kst_prog_editable(p) && st == KST_PROG_PROGRAMMING;
    }
    switch (r->what) {
    case A_PORT:
        return (st == KST_PROG_PWM && kpn_on_port())
            || st == KST_PROG_PROGRAMMING
            || st == KST_PROG_ENTERING || st == KST_PROG_WORKING
            || kpn_leave_waits(p);
    case A_READ:
        return st == KST_PROG_PROGRAMMING;
    case A_FILES:
        /* A file is saved from and loaded over a servo that is read. */
        return st == KST_PROG_PROGRAMMING && !kst_prog_in_doubt(p);
    case A_RESTORE:
        /* Not under an operation: its record is no interrupted write. */
        return (st == KST_PROG_PWM || st == KST_PROG_PROGRAMMING)
            && (p->journal
                || kst_prog_restore_offered(p, KST_PROG_FROM_SESSION)
                || kst_prog_restore_offered(p, KST_PROG_FROM_JOURNAL)
                || kst_prog_restore_offered(p, KST_PROG_FROM_RUN));
    case A_MORE:
        return (st == KST_PROG_PROGRAMMING && kst_prog_have_servo(p))
            || (st == KST_PROG_PWM && kpn_on_port());
    case A_ENABLE:
        /* Off at any time; on where the model takes it. */
        return kst_prog_write_enabled(p)
            || kst_prog_enable_check(p) == KST_PROG_MSG_NONE;
    default:
        return st == KST_PROG_PROGRAMMING && kst_prog_changes(p) != 0u;
    }
}

bool kst_panel_row_live(int page, int row, int col)
{
    const row_t *r = kpn_row_at(page, row, col);

    return r != NULL && kpn_row_live(r);
}
