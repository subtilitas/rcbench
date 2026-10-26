/**
 * @file hitec_panel_page.c
 * @brief The hold pages of the Hitec programmer's overlay: LEAVE, RELEASE
 *        PORT, ENABLE WRITE TO SERVO, WRITE n VALUES, CHECK THIS CHANGE,
 *        REPAIR CHECKSUM and RESTORE, what each says before it acts, and
 *        what its hold does.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>

#include "hitec_reg.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

void hpn_page_open(int kind, bool danger, const char *title,
                   const char *apply)
{
    hpn_close_editors();
    hp.page.open = true;
    hp.page.kind = kind;
    hp.page.danger = danger;
    hp.page.count = 0;
    snprintf(hp.page.title, sizeof(hp.page.title), "%s", title);
    snprintf(hp.page.apply, sizeof(hp.page.apply), "%s", apply);
}

void hpn_page_add(unsigned level, const char *s)
{
    if (hp.page.count < HP_PAGE_LINES) {
        hp.page.level[hp.page.count] = (uint8_t)level;
        snprintf(hp.page.lines[hp.page.count++], sizeof(hp.page.lines[0]),
                 "%s", s);
    }
}

void hpn_page_leave(void)
{
    hpn_page_open(W_LEAVE, false, TR(KP_LEAVE_TITLE), TR(KP_HOLD_LEAVE));
    if (hitec_prog_series(&hp.prog) == (uint8_t)HITEC_CLASSIC) {
        hpn_page_add(HITEC_PROG_INFO, TR(HP_LEAVE_C));
    } else {
        hpn_page_add(HITEC_PROG_INFO, TR(KP_LEAVE_1));
        hpn_page_add(HITEC_PROG_INFO, TR(HP_LEAVE_D));
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_LEAVE_2));
}

void hpn_page_release(void)
{
    char line[HITEC_PANEL_LINE];

    hpn_page_open(W_RELEASE, false, TR(HP_RELEASE), TR(HP_HOLD_RELEASE));
    snprintf(line, sizeof(line), TR(HP_RELEASE_1),
             (unsigned)hitec_prog_port(&hp.prog) + 1u);
    hpn_page_add(HITEC_PROG_INFO, line);
    hpn_page_add(HITEC_PROG_INFO, TR(HP_RELEASE_2));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_LEAVE_2));
    hpn_page_add(HITEC_PROG_INFO, TR(HP_RELEASE_3));
}

/* ------------------------------------------------------------- the write */

/**
 * @brief Whether the series in use is the classic one.
 * @return true for CLASSIC
 */
static bool classic(void)
{
    return hitec_prog_series(&hp.prog) == (uint8_t)HITEC_CLASSIC;
}

/**
 * @brief Whether @p to moves the horn or turns it round: it changes the
 *        direction, the centre or an end point.
 * @param to the image a write leaves
 * @return true when it does
 */
static bool moves_horn(const hitec_image_t *to)
{
    static const uint8_t k_moves[] = {
        HITEC_P_D_DIRECTION, HITEC_P_D_NEUTRAL, HITEC_P_D_LEFT_END,
        HITEC_P_D_RIGHT_END, HITEC_P_C_DIRECTION, HITEC_P_C_CENTRE,
        HITEC_P_C_LEFT_END, HITEC_P_C_RIGHT_END,
    };

    for (unsigned i = 0; i < sizeof(k_moves); ++i) {
        if (hitec_prog_differs(&hp.prog, to, k_moves[i])) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The values @p to changes, in the order of the tabs, on at most
 *        @p room lines: more than fit leave the last line to their count.
 *        A value that breaks a soft rule is in the warning colour.
 * @param to   the image the write leaves
 * @param room the lines
 */
static void page_values(const hitec_image_t *to, int room)
{
    uint8_t ids[HP_ROW_COUNT];
    int n = 0;
    char line[HITEC_PANEL_LINE];

    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        const hpn_row_t *r = &hpn_rows[i];

        if (r->kind == K_SHOW && r->what < (uint8_t)HITEC_P_COUNT
            && hpn_row_shown(r) && hitec_prog_differs(&hp.prog, to, r->what)) {
            ids[n++] = r->what;
        }
    }
    for (int i = 0; i < n; ++i) {
        hitec_value_t v;
        unsigned level = HITEC_PROG_INFO;

        if (i == room - 1 && n > room) {
            snprintf(line, sizeof(line), TR(KP_VALUES_MORE),
                     (unsigned)(n - i));
            hpn_page_add(HITEC_PROG_INFO, line);
            return;
        }
        if (hitec_param_get(to, hitec_prog_layout(&hp.prog), ids[i], &v)
                == HITEC_GET_OK
            && hitec_prog_rules(&hp.prog, ids[i], v.value) != 0u) {
            level = HITEC_PROG_WARN;
        }
        hpn_change_line(ids[i], to, line, sizeof(line));
        hpn_page_add(level, line);
    }
}

/** @brief Where the values before a write are recorded: the card or RAM. */
static void page_record(void)
{
    hpn_page_add(HITEC_PROG_INFO, hp.prog.f.card ? TR(HP_WRITE_RECORD)
                                                 : TR(KP_WRITE_NO_CARD));
}

void hpn_page_write(void)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_image_t *to = hitec_prog_draft(p);
    const unsigned c = hitec_prog_changes(p);
    const bool horn = to != NULL && (!hitec_prog_first_done(p)
                                     || moves_horn(to));
    const int fixed = (horn ? 2 : 0) + 3;
    char title[64];

    if (c == 1u) {
        snprintf(title, sizeof(title), "%s", TR(KP_WRITE_ONE));
    } else {
        snprintf(title, sizeof(title), TR(KP_WRITE_N), c);
    }
    hpn_page_open(W_WRITE, true, title, TR(KP_HOLD_WRITE));
    /* The write gate has passed: a servo is read, so a draft stands. */
    if (to != NULL) {
        page_values(to, HP_PAGE_LINES - fixed < HP_PAGE_VALUES
                            ? HP_PAGE_LINES - fixed
                            : HP_PAGE_VALUES);
    }
    if (horn) {
        hpn_page_add(HITEC_PROG_DANGER, TR(KP_HORN_FREE));
        hpn_page_add(HITEC_PROG_WARN, TR(KP_BENCH_FIRST));
    }
    hpn_page_add(HITEC_PROG_INFO, classic() ? TR(HP_WRITE_C)
                                            : TR(HP_WRITE_D));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
    page_record();
}

void hpn_page_enable(void)
{
    hpn_page_open(W_ENABLE, true, TR(KP_ENABLE_TITLE), TR(KP_HOLD_ENABLE));
    hpn_page_add(HITEC_PROG_DANGER, classic() ? TR(HP_ENABLE_C)
                                              : TR(HP_ENABLE_D));
    hpn_page_add(HITEC_PROG_INFO, TR(HP_ENABLE_2));
    hpn_page_add(HITEC_PROG_INFO, classic() ? TR(HP_ENABLE_3C)
                                            : TR(KP_ENABLE_3));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_ENABLE_5));
}

/** @brief The warning of each soft rule, in hitec_prog_rule_t's order. */
static const ui_text_id_t k_rule[HITEC_RULE_COUNT] = {
    TX_KP_W_SPD_SLOW, TX_KP_W_DEAD_BAND_NARROWED, TX_HP_SOFT_OVERLOAD,
    TX_KP_W_END_MOVED, TX_KP_W_NEUTRAL_MOVED, TX_KP_W_REVERSION_CHANGED,
    TX_KP_W_UNCONT_POS_FAR,
};

void hpn_page_soft(void)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_image_t *draft = hitec_prog_draft(p);
    uint8_t id = 0;
    int32_t v = 0;
    const uint16_t rules = hitec_prog_pending(p, &id, &v);

    hpn_page_open(W_SOFT, false, TR(KP_SOFT_TITLE), TR(SV_HOLD_TO_APPLY));
    /* Opened for an edit that waits: a servo is read, so a draft stands. */
    if (draft != NULL) {
        hitec_image_t trial;
        char line[HITEC_PANEL_LINE];

        /* The edit as the draft would take it, from the servo's value:
         * not from an edit before this one. */
        trial = *draft;
        (void)hitec_param_set(&trial, hitec_prog_layout(p), id, v);
        hpn_change_line(id, &trial, line, sizeof(line));
        hpn_page_add(HITEC_PROG_INFO, line);
    }
    for (unsigned r = 0; r < (unsigned)HITEC_RULE_COUNT; ++r) {
        if ((rules & HITEC_RULE_BIT(r)) != 0u) {
            hpn_page_add(HITEC_PROG_WARN, ui_tr(k_rule[r]));
        }
    }
}

void hpn_page_repair(void)
{
    const hitec_image_t *s = hitec_prog_servo(&hp.prog);

    hpn_page_open(W_REPAIR, true, TR(HP_REPAIR), TR(HP_HOLD_REPAIR));
    /* The write gate has passed: a servo is read. */
    if (s != NULL) {
        char line[HITEC_PANEL_LINE];

        snprintf(line, sizeof(line), TR(HP_REPAIR_1),
                 (unsigned)hitec_image_cell(s, HITEC_C_SUM_LAST),
                 (unsigned)hitec_c_balance(s));
        hpn_page_add(HITEC_PROG_INFO, line);
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
    page_record();
}

void hpn_page_restore(void)
{
    hitec_image_t to;

    hpn_page_open(W_RESTORE, true, TR(KP_RESTORE_TITLE), TR(KP_HOLD_RESTORE));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_FROM_JOURNAL));
    /* The write gate has passed: a record of this servo stands. */
    if (hitec_prog_restore_image(&hp.prog, &to)) {
        page_values(&to, HP_PAGE_VALUES);
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
}

void hpn_page_apply(void)
{
    hitec_prog_t *p = &hp.prog;
    const int kind = hp.page.kind;

    /* Shut first, and not as a CANCEL: the edit CHECK THIS CHANGE asked
     * for is taken below, not dropped. */
    hp.page.open = false;
    hpn_close_editors();
    switch (kind) {
    case W_LEAVE:
        (void)hitec_prog_leave(p);
        break;
    case W_RELEASE:
        (void)hitec_prog_release(p);
        break;
    case W_ENABLE:
        (void)hitec_prog_write_enable(p, true);
        break;
    case W_WRITE:
        (void)hitec_prog_write(p);
        break;
    case W_SOFT:
        (void)hitec_prog_confirm(p);
        break;
    case W_REPAIR:
        (void)hitec_prog_repair(p);
        break;
    default:
        (void)hitec_prog_restore(p);
        break;
    }
    hpn_moved();
}
