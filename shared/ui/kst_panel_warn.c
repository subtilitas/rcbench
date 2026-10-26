/**
 * @file kst_panel_warn.c
 * @brief The warning pages of the programmer's overlay.
 *
 * The warning pages of the programmer's overlay for KST and Chaservo
 * servos: what a write, a restore, a soft limit, ENABLE WRITE TO SERVO, a
 * file, a release, a leave, a check and HV SERVO each say before they
 * act, and APPLY's hold that acts.  The state and the seam are
 * kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <stdio.h>
#include <string.h>

#include "kst_limits.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "ui_card_text.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/**
 * @brief Opens the warning page of kind @p kind with its title and the label
 *        of its hold button.
 */
static void warn_open(int kind, bool danger, const char *title,
                      const char *apply)
{
    memset(&k.warn, 0, sizeof(k.warn));
    k.warn.open = true;
    k.warn.kind = kind;
    k.warn.danger = danger;
    snprintf(k.warn.title, sizeof(k.warn.title), "%s", title);
    snprintf(k.warn.apply, sizeof(k.warn.apply), "%s", apply);
}

/**
 * @brief Takes the next line of the warning page at @p level and returns its
 *        buffer.
 */
static char *warn_line(kst_prog_level_t level)
{
    const int i = k.warn.count < WARN_LINES ? k.warn.count++ : WARN_LINES - 1;

    k.warn.level[i] = (uint8_t)level;
    return k.warn.lines[i];
}

/** @brief Adds the line @p s to the warning page at @p level. */
static void warn_text(kst_prog_level_t level, const char *s)
{
    snprintf(warn_line(level), KST_PANEL_LINE, "%s", s);
}

/**
 * @brief A write of @p to moves the horn or turns it round: it changes the
 *        direction, the centre, an end point or an angle.
 */
static bool moves_horn(const kst_image_t *to)
{
    static const uint8_t k_moves[] = {
        KST_F_REVERSION, KST_F_NEUTRAL, KST_F_PULSE_LOWER, KST_F_PULSE_UPPER,
        KST_F_LEFT_ANGLE, KST_F_RIGHT_ANGLE,
    };

    for (unsigned i = 0; i < sizeof(k_moves); ++i) {
        const kst_field_id_t id = (kst_field_id_t)k_moves[i];

        if (kst_field_get(to, id)
            != kst_field_get(kst_prog_servo(&k.prog), id)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The two lines of the first write of a session, and of every write that
 *        @p moves the horn.
 */
static bool horn_due(bool moves)
{
    return moves || kst_prog_first_write(&k.prog);
}

/** @brief Adds the horn warning, when one is due. */
static void warn_horn(bool moves)
{
    if (horn_due(moves)) {
        warn_text(KST_PROG_DANGER, TR(KP_HORN_FREE));
        warn_text(KST_PROG_WARN, TR(KP_BENCH_FIRST));
    }
}

/**
 * @brief One value a write changes: the servo's, the new one, the unit once.  In
 *        the warning colour where the field is at fault for a soft limit.
 */
static void value_line(kst_field_id_t id, const kst_image_t *to,
                       kst_field_set_t soft)
{
    const char *name = kpn_field_name(id);
    char was[24], now[24];

    kpn_fmt_value(id, kst_field_get(kst_prog_servo(&k.prog), id), was,
              sizeof(was), false);
    kpn_fmt_field(id, kst_field_get(to, id), now, sizeof(now));
    snprintf(warn_line(kpn_in_set(soft, id) ? KST_PROG_WARN : KST_PROG_INFO),
             KST_PANEL_LINE, "%s: %s -> %s",
             name != NULL ? name : kst_field(id)->name, was, now);
}

int kpn_warn_room(void)
{
    const int fit = (kst_panel_apply_rect().y - 8 - (k.area.y + WARN_TOP))
                  / LINE_H;

    return fit < WARN_ROWS ? fit : WARN_ROWS;
}

/* The rows the page's lines take. */
int kpn_warn_rows(void)
{
    int n = 0;

    for (int i = 0; i < k.warn.count; ++i) {
        n += kst_panel_text_rows(k.warn.lines[i]);
    }
    return n;
}

/**
 * @brief Line @p i of @p n, of @p rows rows, is shown with @p used rows taken by
 *        the lines before it, on at most @p room lines and @p room_rows rows: a
 *        line that is not the last leaves a row for the count of the rest.
 */
static bool warn_room(int i, int n, int rows, int used, int room,
                      int room_rows)
{
    const int after = i == n - 1 ? 0 : 1;

    return i + 1 + after <= room && used + rows + after <= room_rows;
}

/**
 * @brief The values in which @p to differs from the servo, in the order of the
 *        tabs, on at most @p room lines and @p room_rows rows.  More than fit
 *        leave the last line to their count.
 */
static void warn_values(const kst_image_t *to, int room, int room_rows)
{
    const kst_field_set_t soft = kst_prog_soft_fields(&k.prog, to);
    uint8_t ids[ROW_COUNT];
    int n = 0;

    for (int i = 0; i < ROW_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)kpn_rows[i].what;

        if (kpn_rows[i].kind != K_ACT
            && kst_field_get(to, id)
                   != kst_field_get(kst_prog_servo(&k.prog), id)) {
            ids[n++] = (uint8_t)id;
        }
    }
    for (int i = 0, used = 0; i < n; ++i) {
        if (!warn_room(i, n, 1, used, room, room_rows)) {
            snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE,
                     TR(KP_VALUES_MORE), (unsigned)(n - i));
            break;
        }
        value_line((kst_field_id_t)ids[i], to, soft);
        used += kst_panel_text_rows(k.warn.lines[k.warn.count - 1]);
    }
}

/**
 * @brief The warnings of @p rules, one line each, on at most @p room lines and
 *        @p room_rows rows.  More than fit leave the last line to their count.
 */
static void warn_rules(kst_rules_t rules, int room, int room_rows)
{
    int16_t lines[KST_RULE_COUNT];
    int n = 0;

    for (unsigned rule = 0; rule < (unsigned)KST_RULE_COUNT; ++rule) {
        bool twice = false;

        if ((rules & KST_RULE_BIT(rule)) == 0u) {
            continue;
        }
        /* Two rules share a line where the line says the same of both. */
        for (int i = 0; i < n; ++i) {
            twice = twice || lines[i] == kpn_rule[rule];
        }
        if (!twice) {
            lines[n++] = kpn_rule[rule];
        }
    }
    for (int i = 0, used = 0; i < n; ++i) {
        const char *t = ui_tr((ui_text_id_t)lines[i]);
        const int rows = kst_panel_text_rows(t);

        if (!warn_room(i, n, rows, used, room, room_rows)) {
            if (n - i == 1) {
                warn_text(KST_PROG_WARN, TR(KP_CHECK_MORE_ONE));
            } else {
                snprintf(warn_line(KST_PROG_WARN), KST_PANEL_LINE,
                         TR(KP_CHECK_MORE), (unsigned)(n - i));
            }
            break;
        }
        warn_text(KST_PROG_WARN, t);
        used += rows;
    }
}

void kpn_warn_soft(const row_t *r)
{
    const kst_field_id_t id = (kst_field_id_t)r->what;

    warn_open(W_SOFT, false, TR(KP_SOFT_TITLE), TR(SV_HOLD_TO_APPLY));
    /* From the servo's value, not from an edit before this one. */
    value_line(id, kst_prog_soft_image(&k.prog), 0u);
    warn_rules(kst_prog_soft_rules(&k.prog), WARN_RULES - 1,
               kpn_warn_room() - kpn_warn_rows());
}

void kpn_warn_enable(void)
{
    warn_open(W_ENABLE, true, TR(KP_ENABLE_TITLE), TR(KP_HOLD_ENABLE));
    warn_text(KST_PROG_DANGER, TR(KP_ENABLE_1));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_2));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_3));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_4));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_5));
}

/**
 * @brief A write that saves the servo to a file first, and no card to take one:
 *        it goes without, and its page says so.
 */
static bool no_card(void)
{
    return kst_prog_save_none(&k.prog);
}

/**
 * @brief What a write with no card leaves out, on the page whose hold sends it.
 */
static bool warn_no_file(void)
{
    if (!no_card()) {
        return false;
    }
    k.warn.no_file = true;
    warn_text(KST_PROG_WARN, kpn_card_tr(UI_CARD_AT_KP_NO_FILE));
    warn_text(KST_PROG_WARN, TR(KP_NO_FILE_2));
    warn_text(KST_PROG_WARN, TR(KP_NO_FILE_3));
    return true;
}

/** @brief The rows of what warn_card() says. */
static int card_rows(void)
{
    if (no_card()) {
        return kst_panel_text_rows(kpn_card_tr(UI_CARD_AT_KP_NO_FILE))
             + kst_panel_text_rows(TR(KP_NO_FILE_2))
             + kst_panel_text_rows(TR(KP_NO_FILE_3));
    }
    if (kst_prog_save_first(&k.prog)) {
        return kst_panel_text_rows(TR(KP_WRITE_BACKUP));
    }
    return kst_panel_text_rows(k.prog.f.card
                                   ? TR(KP_WRITE_CARD)
                                   : kpn_card_tr(UI_CARD_AT_KP_WRITE));
}

/**
 * @brief Adds the line that says where the write's backup goes, or that the
 *        card is missing.
 */
static void warn_card(void)
{
    if (warn_no_file()) {
        return;
    }
    if (kst_prog_save_first(&k.prog)) {
        warn_text(KST_PROG_INFO, TR(KP_WRITE_BACKUP));
    } else {
        warn_text(KST_PROG_INFO, k.prog.f.card
                                     ? TR(KP_WRITE_CARD)
                                     : kpn_card_tr(UI_CARD_AT_KP_WRITE));
    }
}

/**
 * @brief The fields in which @p a differs from the servo, register 01's copy of
 *        Max. Duty left out.
 */
static unsigned differing(const kst_image_t *a)
{
    unsigned n = 0;

    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        if (id != KST_F_DUTY_COPY
            && kst_field_get(a, (kst_field_id_t)id)
                   != kst_field_get(kst_prog_servo(&k.prog),
                                    (kst_field_id_t)id)) {
            ++n;
        }
    }
    return n;
}

/** @brief Returns the text id of a file's kind, -1 for any other. */
static int kind_text(kst_prog_file_kind_t kind)
{
    return kind == KST_PROG_FILE_EDITED    ? TX_KP_FILE_EDITED
         : kind == KST_PROG_FILE_UNTYPED   ? TX_KP_FILE_UNTYPED
                                           : -1;
}

/* A file whose values pass soft limits: its warnings, and the draft takes
 * it after the hold. */
void kpn_warn_file(void)
{
    const kst_prog_t *p = &k.prog;
    const int kind = kind_text(kst_prog_load_kind(p));

    warn_open(W_SOFT, false, TR(KP_FILE_TITLE), TR(KP_HOLD_LOAD));
    if (differing(kst_prog_soft_image(p)) == 1u) {
        snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE,
                 TR(KP_FILE_DIFF_ONE), kpn_shown(kst_prog_card_name(p)));
    } else {
        snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, TR(KP_FILE_DIFF),
                 kpn_shown(kst_prog_card_name(p)),
                 differing(kst_prog_soft_image(p)));
    }
    if (kind >= 0) {
        kpn_kind_line(warn_line(KST_PROG_INFO), kind);
    }
    warn_values(kst_prog_soft_image(p), FILE_VALUES, FILE_VALUES);
    warn_rules(kst_prog_soft_rules(p), WARN_LINES - k.warn.count,
               kpn_warn_room() - kpn_warn_rows());
}

void kpn_warn_write(void)
{
    char title[64];

    const kst_image_t *to = kst_prog_draft(&k.prog);
    const bool moves = moves_horn(to);
    /* The lines under the values: the horn's two, the two of every write
     * and the card's one; a write with no card has three for the card and
     * leaves the first of the two out. */
    const int fixed = (horn_due(moves) ? 2 : 0) + (no_card() ? 4 : 3);
    const int fixed_rows =
        (horn_due(moves) ? kst_panel_text_rows(TR(KP_HORN_FREE))
                               + kst_panel_text_rows(TR(KP_BENCH_FIRST))
                         : 0)
        + (no_card() ? 0 : kst_panel_text_rows(TR(KP_WRITE_1)))
        + kst_panel_text_rows(TR(KP_WRITE_2)) + card_rows();

    kpn_write_n(title, sizeof(title), kst_prog_changes(&k.prog));
    warn_open(W_WRITE, true, title, TR(KP_HOLD_WRITE));
    warn_values(to, WARN_LINES - fixed, kpn_warn_room() - fixed_rows);
    warn_horn(moves);
    if (!no_card()) {
        warn_text(KST_PROG_INFO, TR(KP_WRITE_1));
    }
    warn_text(KST_PROG_INFO, TR(KP_WRITE_2));
    warn_card();
}

ui_text_id_t kpn_from_text(unsigned from)
{
    return from == KST_PROG_FROM_SESSION ? TX_KP_FROM_SESSION
         : from == KST_PROG_FROM_RUN     ? TX_KP_FROM_RUN
                                         : TX_KP_FROM_JOURNAL;
}

void kpn_warn_restore(const kst_prog_restore_t *plan)
{
    warn_open(W_RESTORE, true, TR(KP_RESTORE_TITLE), TR(KP_HOLD_RESTORE));
    k.warn.plan = *plan;
    warn_text(KST_PROG_INFO, ui_tr(kpn_from_text(plan->from)));
    if (plan->writes == 0xFFu) {
        warn_text(KST_PROG_INFO, TR(KP_RESTORE_ALL));
    } else if (plan->writes == 1u) {
        warn_text(KST_PROG_INFO, TR(KP_RESTORE_ONE));
    } else {
        snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, TR(KP_RESTORE_N),
                 (unsigned)plan->writes);
    }
    if (plan->stay != 0u) {
        kpn_stay_line(warn_line(KST_PROG_WARN), (unsigned)plan->stay);
    }
    if (plan->unchecked) {
        warn_text(KST_PROG_WARN, TR(KP_RESTORE_UNCHECKED));
    }
    warn_horn(true);
    if (plan->from != KST_PROG_FROM_SESSION) {
        warn_card();
    } else {
        (void)warn_no_file();
    }
}

/* A backup whose values pass soft limits the servo's own keep: its
 * warnings first, and the restore's page after the hold. */
void kpn_warn_check(const kst_prog_restore_t *plan)
{
    warn_open(W_CHECK, false, TR(KP_CHECK_TITLE), TR(KP_HOLD_NEXT));
    k.warn.plan = *plan;
    warn_text(KST_PROG_INFO, ui_tr(kpn_from_text(plan->from)));
    warn_text(KST_PROG_INFO, TR(KP_CHECK_1));
    warn_rules(plan->soft, WARN_RULES - 2, kpn_warn_room() - kpn_warn_rows());
}

void kpn_warn_release(void)
{
    warn_open(W_RELEASE, true, TR(KP_RELEASE_TITLE), TR(KP_HOLD_WRITE));
    warn_text(KST_PROG_WARN, TR(KP_RELEASE_1));
    warn_text(KST_PROG_INFO, TR(KP_RELEASE_2));
    warn_text(KST_PROG_INFO, TR(KP_RELEASE_3));
    warn_horn(false);
    (void)warn_no_file();
}

/* The way back to PWM: through a supply cycle after a write with a PD mini,
 * with a note of the cycle the values wait for without one. */
void kpn_warn_leave(void)
{
    warn_open(W_LEAVE, false, TR(KP_LEAVE_TITLE), TR(KP_HOLD_LEAVE));
    if (kst_prog_in_doubt(&k.prog)) {
        warn_text(KST_PROG_DANGER, TR(KP_LEAVE_DOUBT_1));
        warn_text(KST_PROG_DANGER, TR(KP_LEAVE_DOUBT_2));
    }
    if (kst_prog_changes(&k.prog) != 0u) {
        warn_text(KST_PROG_WARN, TR(KP_LEAVE_CHANGES));
    }
    if (kst_prog_leave_cycles(&k.prog)) {
        /* The written values act after this power-up. */
        warn_text(KST_PROG_INFO, TR(KP_LEAVE_CYCLE));
    } else {
        warn_text(KST_PROG_INFO, TR(KP_LEAVE_1));
        if (kst_prog_cycle_owed(&k.prog)) {
            warn_text(KST_PROG_WARN, TR(KP_M_LEFT_NOTE));
        }
    }
    warn_text(KST_PROG_WARN, TR(KP_LEAVE_2));
}

/* A programming voltage above what a standard servo is rated for. */
void kpn_warn_hv(uint16_t mv)
{
    warn_open(W_HV, true, TR(KP_PROG_VOLTAGE), TR(SV_HOLD_TO_APPLY));
    k.warn.mv = mv;
    snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, TR(KP_SUPPLY_V),
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u));
    warn_text(KST_PROG_WARN, TR(SV_HV_1));
    warn_text(KST_PROG_WARN, TR(SV_HV_2));
    warn_text(KST_PROG_WARN, TR(SV_HV_3));
    warn_text(KST_PROG_DANGER, TR(SV_HV_4));
    warn_text(KST_PROG_INFO, TR(SV_HV_5));
}

/* The hold completed: what the page asked for. */
void kpn_warn_apply(void)
{
    const int kind = k.warn.kind;
    const kst_prog_restore_t plan = k.warn.plan;
    const uint16_t mv = k.warn.mv;
    /* The card left the slot under a page that promised a file: the page
     * comes back with what a write without one leaves out, and nothing is
     * sent before its own hold. */
    const bool unsaid = no_card() && !k.warn.no_file;

    k.warn.open = false;
    k.warn.down = false;
    ui_hold_reset(&k.warn.hold);
    k.pressed = P_NONE;
    switch (kind) {
    case W_SOFT:
        kst_prog_soft_confirm(&k.prog);
        break;
    case W_ENABLE:
        (void)kst_prog_enable(&k.prog, true);
        break;
    case W_WRITE:
        if (unsaid) {
            kpn_warn_write();
        } else {
            (void)kst_prog_write(&k.prog);
        }
        break;
    case W_RESTORE:
        if (unsaid) {
            kpn_warn_restore(&plan);
        } else {
            (void)kst_prog_restore(&k.prog, &plan);
        }
        break;
    case W_CHECK:
        kpn_warn_restore(&plan);
        break;
    case W_RELEASE:
        if (unsaid) {
            kpn_warn_release();
        } else {
            (void)kst_prog_release(&k.prog);
        }
        break;
    case W_HV:
        if (k.hv) {
            kst_prog_supply_set(&k.prog, mv, kst_prog_supply_ma(&k.prog));
            kpn_options_keep();
        }
        break;
    default:
        (void)kst_prog_leave(&k.prog);
        break;
    }
}
