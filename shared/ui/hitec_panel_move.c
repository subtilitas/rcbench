/**
 * @file hitec_panel_move.c
 * @brief MOVE SERVO on the Hitec programmer's TRAVEL tab: its row and hold
 *        page, the -/+ keys of NEUTRAL, LEFT END and RIGHT END while it is
 *        on, their repeat, the live lines and its messages.  The loop is
 *        hitec_move.h's.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * FIND STOP is the row under MOVE SERVO: two keys, LEFT END and RIGHT END,
 * that take a tap while the loop's passes run.  A tap starts the search on
 * that side; a tap on the side that searches ends it.
 *
 * The keys are KST's: 56px, a step on the press, the repeat after 0.45s at
 * 8Hz and at 30Hz from 1.8s.  A step the draft refuses ends the repeat.
 * The value between the keys opens the keypad as on every value row.  The
 * knob moves nothing here: the knob rule of a SAFE bench stays.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "hitec_live.h"
#include "hitec_move.h"
#include "hitec_reg.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The degree sign, UTF-8. */
#define DEG "\xc2\xb0"

/* ------------------------------------------------------------------ keys */

bool hpn_stepped(const hpn_row_t *r)
{
    return r->kind == K_SHOW && r->page == HITEC_PANEL_TRAVEL
           && hitec_prog_move_live(&hp.prog, r->what);
}

bool hpn_step(const hpn_row_t *r, bool plus)
{
    hitec_value_t v;
    int32_t to;

    if (!hpn_stepped(r)
        || hitec_param_get(hitec_prog_draft(&hp.prog),
                           hitec_prog_layout(&hp.prog), r->what, &v)
               != HITEC_GET_OK) {
        return false;
    }
    to = plus ? v.value + (int32_t)HITEC_MOVE_STEP
              : v.value - (int32_t)HITEC_MOVE_STEP;
    ++hp.rev;
    return hitec_prog_move_edit(&hp.prog, r->what, to) == HITEC_PROG_EDIT_OK;
}

/* ------------------------------------------------------------- the switch */

void hpn_page_move(void)
{
    char line[HITEC_PANEL_LINE];

    hpn_page_open(W_MOVE, true, TR(HP_MOVE), TR(HP_MOVE_HOLD),
                  TX_HOLD_HINT_MOVE);
    hpn_page_add(HITEC_PROG_INFO, TR(HP_MOVE_1));
    hpn_page_add(HITEC_PROG_DANGER, TR(HP_MOVE_2));
    hpn_page_add(HITEC_PROG_WARN, TR(HP_MOVE_3));
    hpn_page_add(HITEC_PROG_INFO, TR(HP_MOVE_4));
    snprintf(line, sizeof(line), TR(HP_MOVE_5),
             (unsigned)hitec_prog_move_stall_ma(&hp.prog));
    hpn_page_add(HITEC_PROG_INFO, line);
    hpn_page_add(HITEC_PROG_INFO, TR(HP_MOVE_6));
    hpn_page_rules((uint16_t)(HITEC_RULE_BIT(HITEC_RULE_END)
                              | HITEC_RULE_BIT(HITEC_RULE_CENTER)));
}

void hpn_move_act(void)
{
    hitec_prog_t *p = &hp.prog;

    if (hitec_prog_move_on(p)) {
        hitec_prog_move_stop(p);
    } else if (hitec_prog_move_gate(p) != HITEC_PROG_MSG_NONE) {
        /* The refusal, said; nothing starts. */
        (void)hitec_prog_move_start(p);
    } else {
        hpn_page_move();
    }
}

void hpn_move_follow(void)
{
    if (hitec_prog_move_on(&hp.prog)
        && (!hp.open || hp.f.tabs.selected != HITEC_PANEL_TRAVEL)) {
        hitec_prog_move_stop(&hp.prog);
    }
}

/* ------------------------------------------------------------- FIND STOP */

bool hpn_find_live(bool right)
{
    const hitec_prog_t *p = &hp.prog;
    const int side = hitec_prog_find_side(p);
    const int mine = right ? (int)HITEC_FIND_RIGHT : (int)HITEC_FIND_LEFT;

    return hitec_prog_move_on(p)
           && hitec_prog_move_phase(p) != HITEC_MOVE_START
           && (side < 0 || side == mine);
}

void hpn_find_act(bool right)
{
    hitec_prog_t *p = &hp.prog;
    const int mine = right ? (int)HITEC_FIND_RIGHT : (int)HITEC_FIND_LEFT;

    ++hp.rev;
    if (hitec_prog_find_side(p) == mine) {
        hitec_prog_find_stop(p);
    } else {
        (void)hitec_prog_find_start(p, (uint8_t)mine);
    }
}

/* ------------------------------------------------------------------ notes */

/**
 * @brief "+64.6°": a D position in degrees, 220deg over 16383 counts,
 *        8192 the center, to a tenth.
 * @param pos the position
 * @param out the text
 * @param n   bytes at @p out
 */
static void pos_deg(uint16_t pos, char *out, size_t n)
{
    const long d = ((long)pos - 8192L) * 2200L;
    const long t = (d >= 0 ? d + 8191L : d - 8191L) / 16383L;
    const long a = t < 0 ? -t : t;

    snprintf(out, n, "%c%ld.%ld" DEG, t < 0 ? '-' : '+', a / 10, a % 10);
}

/**
 * @brief The live line: the position read with its degrees, the AS5600
 *        angle where the SERVO screen has one, the supply's current.
 * @param v   the loop
 * @param out the line
 * @param n   bytes at @p out
 */
static void live_line(const hitec_move_view_t *v, char *out, size_t n)
{
    char deg[16];
    size_t len;

    pos_deg(v->pos, deg, sizeof(deg));
    snprintf(out, n, TR(HP_LIVE_POS), (unsigned)v->pos, deg);
    len = strlen(out);
    if (v->has_angle && len < n) {
        /* -180.0 to +179.9: the angle of a turn, 4096 counts. */
        long cdeg = (long)v->angle * 3600L / 4096L;

        if (cdeg >= 1800L) {
            cdeg -= 3600L;
        }
        snprintf(out + len, n - len, "  AS5600 %c%ld.%ld" DEG,
                 cdeg < 0 ? '-' : '+', (cdeg < 0 ? -cdeg : cdeg) / 10,
                 (cdeg < 0 ? -cdeg : cdeg) % 10);
        len = strlen(out);
    }
    if (v->has_ma && len < n) {
        /* Rounded once to 10mA, so that 1,996mA reads 2.00A. */
        const unsigned ca = ((unsigned)v->ma + 5u) / 10u;

        snprintf(out + len, n - len, "  %u.%02uA", ca / 100u, ca % 100u);
    }
}

/**
 * @brief The end point's note: where the horn of the value stands, and how
 *        far 1000us or 2000us take it toward that end.
 * @param left true for LEFT END
 * @param out  the note
 * @param n    bytes at @p out
 */
static void end_note(bool left, char *out, size_t n)
{
    const unsigned t_end = left ? HITEC_LIVE_D_T_MID - HITEC_LIVE_D_T_HALF
                                : HITEC_LIVE_D_T_MID + HITEC_LIVE_D_T_HALF;
    const unsigned us_end = HITEC_LIVE_D_US_ZERO + t_end / HITEC_LIVE_D_T_PER_US;
    const unsigned us = left ? 1000u : 2000u;

    snprintf(out, n, TR(HP_LIVE_END), left ? TR(HP_END_L) : TR(HP_END_R),
             us_end, us, (hitec_live_d_share_pm((uint16_t)us) + 5u) / 10u);
}

int hpn_move_notes(const char **s, gfx_color_t *color, int max)
{
    static char port[HITEC_PANEL_LINE];
    static char live[HITEC_PANEL_LINE];
    static char reach[HITEC_PANEL_LINE];
    static char end[HITEC_PANEL_LINE];
    hitec_move_view_t v;
    int n = 0;

    if (!hitec_prog_move_view(&hp.prog, &v)
        || hp.f.tabs.selected != HITEC_PANEL_TRAVEL || max < 5) {
        return -1;
    }
    /* The output the loop drives, as the PORT row names it. */
    if (hpn_port_line(0, port, sizeof(port))) {
        s[n] = port;
        color[n++] = ui_theme_color(hpn_port_unbound() ? UI_C_WARN
                                                       : UI_C_TEXT);
    }
    if (v.has_pos) {
        live_line(&v, live, sizeof(live));
        s[n] = live;
        color[n++] = ui_theme_color(UI_C_TEXT);
    }
    if (!hitec_prog_move_on(&hp.prog)) {
        s[n] = TR(HP_LIVE_ENDING);
        color[n++] = ui_theme_color(UI_C_WARN);
        return n;
    }
    if (v.clamped) {
        snprintf(reach, sizeof(reach), TR(HP_LIVE_REACH), (unsigned)v.lo,
                 (unsigned)v.hi);
        s[n] = reach;
        color[n++] = ui_theme_color(UI_C_WARN);
    }
    if (v.find >= 0) {
        snprintf(end, sizeof(end), TR(HP_FIND_RUN),
                 v.find == (int8_t)HITEC_FIND_RIGHT ? TR(HP_END_R)
                                                    : TR(HP_END_L),
                 (unsigned)HITEC_LIVE_FIND_STEP,
                 (unsigned)HITEC_LIVE_FIND_PASSES,
                 (unsigned)(HITEC_LIVE_FIND_MS / 1000u));
        s[n] = end;
        color[n++] = ui_theme_color(UI_C_WARN);
        return n;
    }
    if (v.follow == (uint8_t)HITEC_P_D_LEFT_END
        || v.follow == (uint8_t)HITEC_P_D_RIGHT_END) {
        end_note(v.follow == (uint8_t)HITEC_P_D_LEFT_END, end, sizeof(end));
        s[n] = end;
        color[n++] = ui_theme_color(UI_C_TEXT_FAINT);
    }
    s[n] = TR(HP_LIVE_NOTE);
    color[n++] = ui_theme_color(UI_C_TEXT_FAINT);
    return n;
}

int hpn_move_lines(const hitec_prog_msg_t *m,
                   char out[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE])
{
    switch (m->id) {
    case HITEC_PROG_MSG_MOVE_OUTSIDE:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_MOVE_OUTSIDE),
                 (unsigned)m->arg);
        return 1;
    case HITEC_PROG_MSG_MOVE_BLOCKED:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_LIVE_BLOCKED),
                 (unsigned)m->arg, (unsigned)m->reg);
        return 1;
    case HITEC_PROG_MSG_MOVE_CURRENT:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_LIVE_CURRENT),
                 (unsigned)m->arg, (unsigned)m->reg);
        return 1;
    case HITEC_PROG_MSG_MOVE_ENC:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_LIVE_ENC),
                 (unsigned)m->reg);
        return 1;
    case HITEC_PROG_MSG_FIND_FOUND_L:
    case HITEC_PROG_MSG_FIND_FOUND_R:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_FIND_FOUND),
                 (unsigned)m->arg,
                 m->id == HITEC_PROG_MSG_FIND_FOUND_L ? TR(HP_END_L)
                                                      : TR(HP_END_R),
                 (unsigned)m->reg);
        return 1;
    case HITEC_PROG_MSG_FIND_FREE:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_FIND_FREE),
                 (unsigned)m->arg);
        return 1;
    case HITEC_PROG_MSG_FIND_TIME:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_FIND_TIME),
                 (unsigned)(HITEC_LIVE_FIND_MS / 1000u), (unsigned)m->arg);
        return 1;
    case HITEC_PROG_MSG_FIND_NEAR:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_FIND_NEAR),
                 (unsigned)m->arg);
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------- for the tests */

gfx_rect_t hitec_panel_step_rect(int page, int row, bool plus)
{
    const hpn_row_t *r = hpn_row_at(page, row, 0);

    hpn_mask_set();
    return r != NULL && hpn_stepped(r)
               ? sprog_step_rect(&hp.f, (int)(r - hpn_rows), plus)
               : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t hitec_panel_find_rect(bool right)
{
    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        const hpn_row_t *r = &hpn_rows[i];

        if (r->kind == K_ACT && r->what == A_FIND && hpn_row_shown(r)) {
            hpn_mask_set();
            return sprog_pair_rect(&hp.f, i, right ? 1 : 0);
        }
    }
    return (gfx_rect_t){ 0, 0, 0, 0 };
}

int hitec_panel_page_hint(void)
{
    return hp.f.page.open ? hp.f.page.hint : -1;
}
