/**
 * @file hitec_panel_page.c
 * @brief The hold pages of the Hitec programmer's overlay: LEAVE, RELEASE
 *        PORT, CHECK THIS CHANGE, REPAIR CHECKSUM and RESTORE, what each
 *        says before it acts, and what its hold does, also for the pages
 *        of hitec_panel_file.c.  WRITE n VALUES has no page: its own 2s
 *        hold writes.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "hitec_move.h"
#include "hitec_reg.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

void hpn_page_open(int kind, bool danger, const char *title,
                   const char *apply, int hint)
{
    hpn_close_editors();
    hp.page.open = true;
    hp.page.kind = kind;
    hp.page.danger = danger;
    hp.page.count = 0;
    hp.page.vfirst = 0;
    hp.page.vcount = 0;
    hp.page.vtotal = 0;
    hp.page.drop = false;
    snprintf(hp.page.title, sizeof(hp.page.title), "%s", title);
    snprintf(hp.page.apply, sizeof(hp.page.apply), "%s", apply);
    hp.page.hint = hint;
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
    const hitec_prog_t *p = &hp.prog;
    const hitec_image_t *draft = hitec_prog_draft(p);
    const bool drop = draft != NULL && hitec_prog_changes(p) != 0u;

    if (drop) {
        /* The edits listed: the hold leaves without writing them, BACK
         * keeps them. */
        hpn_page_open(W_LEAVE, false, TR(KP_LEAVE_TITLE),
                      TR(KP_HOLD_LEAVE_DROP), TX_HOLD_HINT_LEAVE_DROP);
        hp.page.drop = true;
        hpn_page_add(HITEC_PROG_WARN, TR(KP_LEAVE_CHANGES));
        hpn_page_values(draft, HP_PAGE_VALUES);
    } else {
        hpn_page_open(W_LEAVE, false, TR(KP_LEAVE_TITLE), TR(KP_HOLD_LEAVE),
                      TX_HOLD_HINT_LEAVE);
    }
    if (hitec_prog_series(p) == (uint8_t)HITEC_CLASSIC) {
        hpn_page_add(HITEC_PROG_INFO, TR(HP_LEAVE_C));
    } else {
        hpn_page_add(HITEC_PROG_INFO, TR(KP_LEAVE_1));
        hpn_page_add(HITEC_PROG_INFO, TR(HP_LEAVE_D));
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_LEAVE_2));
    (void)hpn_page_fit();
}

void hpn_page_release(void)
{
    char line[HITEC_PANEL_LINE];

    hpn_page_open(W_RELEASE, false, TR(HP_RELEASE),  TR(HP_HOLD_RELEASE), TX_HOLD_HINT_RELEASE);
    snprintf(line, sizeof(line), TR(HP_RELEASE_1),
             (unsigned)hitec_prog_port(&hp.prog) + 1u);
    hpn_page_add(HITEC_PROG_INFO, line);
    hpn_page_add(HITEC_PROG_INFO, TR(HP_RELEASE_2));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_LEAVE_2));
    hpn_page_add(HITEC_PROG_INFO, TR(HP_RELEASE_3));
}

/* ------------------------------------------------------------- the pages */

void hpn_page_values(const hitec_image_t *to, int room)
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
    hp.page.vfirst = hp.page.count;
    hp.page.vtotal = n;
    for (int i = 0; i < n && room > 0; ++i) {
        hitec_value_t v;
        unsigned level = HITEC_PROG_INFO;

        if (i == room - 1 && n > room) {
            snprintf(line, sizeof(line), TR(KP_VALUES_MORE),
                     (unsigned)(n - i));
            hpn_page_add(HITEC_PROG_INFO, line);
            break;
        }
        if (hitec_param_get(to, hitec_prog_layout(&hp.prog), ids[i], &v)
                == HITEC_GET_OK
            && hitec_prog_rules(&hp.prog, ids[i], v.value) != 0u) {
            level = HITEC_PROG_WARN;
        }
        hpn_change_line(ids[i], to, line, sizeof(line));
        hpn_page_add(level, line);
    }
    hp.page.vcount = hp.page.count - hp.page.vfirst;
}

/**
 * @brief The bottom of the open page's last line as draw_hold() places it,
 *        px.
 * @return the bottom
 */
static int page_bottom(void)
{
    int y = hp.area.y + HP_PAGE_TOP;

    for (int i = 0; i < hp.page.count; ++i) {
        y += hitec_panel_text_rows(hp.page.lines[i]) * HP_LINE_H + 8;
    }
    return y - 8;
}

/**
 * @brief Drop line @p at of the open page, the lines below it moving up.
 * @param at the line
 */
static void page_drop(int at)
{
    for (int i = at; i + 1 < hp.page.count; ++i) {
        memcpy(hp.page.lines[i], hp.page.lines[i + 1],
               sizeof(hp.page.lines[0]));
        hp.page.level[i] = hp.page.level[i + 1];
    }
    hp.page.count--;
}

bool hpn_page_fit(void)
{
    const int limit = hitec_panel_apply_rect().y - HP_FOOT_GAP;

    while (page_bottom() > limit && hp.page.vcount > 0) {
        /* One line fewer for the values: the last real one gives way to
         * the count of the rest, or the count goes where none is left. */
        const int keep = hp.page.vcount - 1;
        const int last = hp.page.vfirst + hp.page.vcount - 1;

        page_drop(last);
        hp.page.vcount--;
        if (keep > 0) {
            snprintf(hp.page.lines[last - 1], sizeof(hp.page.lines[0]),
                     TR(KP_VALUES_MORE),
                     (unsigned)(hp.page.vtotal - (keep - 1)));
            hp.page.level[last - 1] = HITEC_PROG_INFO;
        }
    }
    return page_bottom() <= limit;
}

int hpn_page_part(int from)
{
    const int limit = hitec_panel_apply_rect().y - HP_FOOT_GAP;
    const int all = hp.page.count;
    /* A later part keeps the first line, which names what it is about. */
    const int head = from > 0 ? 1 : 0;
    int end;

    for (int i = head; i < from && hp.page.count > head; ++i) {
        page_drop(head);
    }
    while (hp.page.count > head + 1 && page_bottom() > limit) {
        page_drop(hp.page.count - 1);
    }
    end = from + hp.page.count - head;
    return end < all ? end : all;
}

/**
 * @brief The lines hpn_page_saves() adds.
 * @return 1 for the backup file to come, 3 for none without a card, 0
 *         where the backup is saved already
 */
static int saves_lines(void)
{
    const hitec_prog_t *p = &hp.prog;

    return hitec_prog_backup_due(p)                       ? 1
         : p->have_bk && !p->bk_saved && !p->f.card       ? 3
                                                          : 0;
}

/** @brief Where the values before a write are recorded: the card or RAM.
 *         The lines of no backup file without a card say RAM already. */
static void page_record(void)
{
    if (saves_lines() != 3) {
        hpn_page_add(HITEC_PROG_INFO, hp.prog.f.card ? TR(HP_WRITE_RECORD)
                                                     : TR(KP_WRITE_NO_CARD));
    }
}

/** @brief The warning of each soft rule, in hitec_prog_rule_t's order. */
static const ui_text_id_t k_rule[HITEC_RULE_COUNT] = {
    TX_KP_W_SPD_SLOW, TX_KP_W_DEAD_BAND_NARROWED, TX_HP_SOFT_OVERLOAD,
    TX_KP_W_END_MOVED, TX_KP_W_NEUTRAL_MOVED, TX_KP_W_REVERSION_CHANGED,
    TX_KP_W_UNCONT_POS_FAR,
};

void hpn_page_rules(uint16_t rules)
{
    for (unsigned r = 0; r < (unsigned)HITEC_RULE_COUNT; ++r) {
        if ((rules & HITEC_RULE_BIT(r)) != 0u) {
            hpn_page_add(HITEC_PROG_WARN, ui_tr(k_rule[r]));
        }
    }
}

void hpn_page_soft(void)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_image_t *draft = hitec_prog_draft(p);
    uint8_t id = 0;
    int32_t v = 0;
    const uint16_t rules = hitec_prog_pending(p, &id, &v);

    hpn_page_open(W_SOFT, false, TR(KP_SOFT_TITLE),  TR(SV_HOLD_TO_APPLY), TX_HOLD_HINT_APPLY);
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
    hpn_page_rules(rules);
}

void hpn_page_repair(void)
{
    const hitec_image_t *s = hitec_prog_servo(&hp.prog);

    hpn_page_open(W_REPAIR, true, TR(HP_REPAIR),  TR(HP_HOLD_REPAIR), TX_HOLD_HINT_REPAIR);
    /* The write gate has passed: a servo is read. */
    if (s != NULL) {
        char line[HITEC_PANEL_LINE];

        snprintf(line, sizeof(line), TR(HP_REPAIR_1),
                 (unsigned)hitec_image_cell(s, HITEC_C_SUM_LAST),
                 (unsigned)hitec_c_balance(s));
        hpn_page_add(HITEC_PROG_INFO, line);
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
    hpn_page_saves();
    page_record();
    (void)hpn_page_fit();
}

void hpn_page_restore(void)
{
    hitec_image_t to;

    hpn_page_open(W_RESTORE, true, TR(KP_RESTORE_TITLE),  TR(KP_HOLD_RESTORE), TX_HOLD_HINT_RESTORE);
    hpn_page_add(HITEC_PROG_INFO, TR(KP_FROM_JOURNAL));
    /* The write gate has passed: a record of this servo stands. */
    if (hitec_prog_restore_image(&hp.prog, &to)) {
        hpn_page_values(&to, HP_PAGE_VALUES);
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
    hpn_page_saves();
    (void)hpn_page_fit();
}

void hpn_page_apply(void)
{
    hitec_prog_t *p = &hp.prog;
    const int kind = hp.page.kind;
    const bool drop = hp.page.drop;

    /* Shut first, and not as a CANCEL: the edit CHECK THIS CHANGE asked
     * for is taken below, not dropped. */
    hp.page.open = false;
    hpn_close_editors();
    switch (kind) {
    case W_LEAVE:
        /* The edits the page listed go unwritten: dropped while the
         * session takes the LEAVE, kept while it is busy and the LEAVE
         * says why it waits. */
        if (drop && hitec_prog_state(p) == HITEC_PROG_HELD) {
            hitec_prog_discard(p);
        }
        (void)hitec_prog_leave(p);
        break;
    case W_RELEASE:
        (void)hitec_prog_release(p);
        break;
    case W_SOFT:
        (void)hitec_prog_confirm(p);
        break;
    case W_REPAIR:
        (void)hpn_write_go((uint8_t)HITEC_PROG_WR_REPAIR);
        break;
    case W_LOAD:
        (void)hitec_prog_load_confirm(p);
        break;
    case W_LOAD_MORE:
        /* The next part of CHECK THIS FILE; the load waits for its hold. */
        hpn_page_load_at(hp.page.next);
        break;
    case W_FACTORY:
        (void)hpn_write_go((uint8_t)HITEC_PROG_WR_FACTORY);
        break;
    case W_BACKUP:
        (void)hpn_write_go((uint8_t)HITEC_PROG_WR_BACKUP);
        break;
    case W_MOVE:
        (void)hitec_prog_move_start(p);
        break;
    default:
        (void)hpn_write_go((uint8_t)HITEC_PROG_WR_RESTORE);
        break;
    }
    hpn_moved();
}
