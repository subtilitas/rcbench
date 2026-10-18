/*
 * SPDX-License-Identifier: MIT
 */

#include "prog_registry.h"

#include <stdio.h>
#include <string.h>

#include "esc_profile.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * The rows.  One live row today: the programmer for KST and Chaservo
 * servos on the IOmcu's programming port 0 (GP13 on a board that has
 * stored no other pin).  Its port is a channel of the servo PWM bank
 * (prog_port.h), so it needs LINK_CAP_SERVO_PWM.
 *
 * The next maker is one more row here and its page_set.
 */
static const prog_reg_row_t k_rows[] = {
    { PROG_DEV_SERVO, UI_WB_PROGRAM, TX_PL_KST, TX_PL_GRP_PROG_PORT,
      TX_SV_TARGET_PORT, 0u, LINK_CAP_SERVO_PWM, &kst_panel_pages },
};

/* The table the lists are built from: k_rows, or one a test hands in. */
static const prog_reg_row_t *s_rows = k_rows;
static size_t s_count = sizeof(k_rows) / sizeof(k_rows[0]);

const prog_reg_row_t *prog_registry(size_t *count)
{
    if (count != NULL) {
        *count = s_count;
    }
    return s_rows;
}

void prog_registry_use(const prog_reg_row_t *rows, size_t count)
{
    if (rows == NULL) {
        s_rows  = k_rows;
        s_count = sizeof(k_rows) / sizeof(k_rows[0]);
    } else {
        s_rows  = rows;
        s_count = count;
    }
}

/* ----------------------------------------------------------- the checks */

static bool hooks_filled(const prog_page_set_t *p)
{
    return p != NULL && p->init != NULL && p->entry != NULL
           && p->open != NULL && p->is_open != NULL && p->back != NULL
           && p->holds_port != NULL && p->armed != NULL && p->tick != NULL
           && p->has_press != NULL && p->event != NULL && p->render != NULL
           && p->revision != NULL && p->hold_revision != NULL
           && p->hold_rect != NULL && p->leave != NULL && p->cancel != NULL
           && p->pin != NULL;
}

static bool filled(ui_text_id_t id)
{
    return id < TX_COUNT && ui_tr(id)[0] != '\0';
}

static bool same_list(const prog_reg_row_t *a, const prog_reg_row_t *b)
{
    return a->device == b->device && a->task == b->task;
}

prog_reg_fault_t prog_reg_check(const prog_reg_row_t *rows, size_t n,
                                size_t *at)
{
    for (size_t i = 0; rows != NULL && i < n; ++i) {
        const prog_reg_row_t *r = &rows[i];
        prog_reg_fault_t f = PROG_REG_OK;
        if (r->device >= PROG_DEVICES) {
            f = PROG_REG_BAD_DEVICE;
        } else if (r->task != UI_WB_PROGRAM && r->task != UI_WB_TEST) {
            f = PROG_REG_BAD_TASK;
        } else if (!filled(r->name)) {
            f = PROG_REG_NO_NAME;
        } else if (!filled(r->group)) {
            f = PROG_REG_NO_GROUP;
        } else if (!filled(r->transport)) {
            f = PROG_REG_NO_TRANSPORT;
        } else if (r->port != PROG_REG_NO_PORT && r->port >= PROG_REG_PORTS) {
            f = PROG_REG_BAD_PORT;
        } else if (!hooks_filled(r->page_set)) {
            f = PROG_REG_NO_PAGE_SET;
        } else {
            size_t list = 0;
            for (size_t j = 0; j < n; ++j) {
                if (!same_list(r, &rows[j])) {
                    continue;
                }
                ++list;
                if (j < i && strcmp(ui_tr(rows[j].name), ui_tr(r->name)) == 0) {
                    f = PROG_REG_SAME_NAME;
                }
            }
            if (f == PROG_REG_OK && list > PROG_LIST_MAX) {
                f = PROG_REG_TOO_MANY;
            }
        }
        if (f != PROG_REG_OK) {
            if (at != NULL) {
                *at = i;
            }
            return f;
        }
    }
    return PROG_REG_OK;
}

/* ------------------------------------------------------- a row's state */

/* The two hooks the registry calls, each from a function of its own, so
 * tools/stack_check.py follows them by name (PROG_CALLS). */
static PROG_ONE_CALL bool prog_reg_call_holds(
    const prog_page_set_t *ps)
{
    return ps->holds_port();
}

static PROG_ONE_CALL uint8_t prog_reg_call_pin(
    const prog_page_set_t *ps)
{
    return ps->pin();
}

uint16_t prog_reg_missing(const prog_reg_row_t *row, uint16_t caps)
{
    if (row == NULL) {
        return 0u;
    }
    const uint16_t lack = (uint16_t)(row->needs_capability & ~caps);
    return (uint16_t)(lack & (uint16_t)(~lack + 1u));
}

prog_row_state_t prog_reg_state(const prog_reg_row_t *rows, size_t n,
                                size_t i, uint16_t caps)
{
    if (rows == NULL || i >= n) {
        return PROG_ROW_NO_CAP;
    }
    const prog_reg_row_t *r = &rows[i];
    if (prog_reg_missing(r, caps) != 0u) {
        return PROG_ROW_NO_CAP;
    }
    if (r->port == PROG_REG_NO_PORT) {
        return PROG_ROW_LIVE;
    }
    /* Exclusive: another session on the port holds it.  A row sharing the
     * page set is the same session, not another one. */
    for (size_t j = 0; j < n; ++j) {
        const prog_reg_row_t *o = &rows[j];
        if (j != i && o->port == r->port && o->page_set != r->page_set
            && o->page_set != NULL && o->page_set->holds_port != NULL
            && prog_reg_call_holds(o->page_set)) {
            return PROG_ROW_PORT_BUSY;
        }
    }
    return PROG_ROW_LIVE;
}

/* The link's names for link_cap_t's bits, from bit 0: field terms, the
 * same in every language. */
static const char *const k_cap_name[] = {
    "ESC DRIVE", "ESC TELEMETRY", "SERVO PWM", "SERVO SENSE", "PACK SENSE",
    "RECEIVER", "VIBRATION", "CELLS", "PROGRAM",
};

const char *prog_reg_cap_name(uint16_t bit)
{
    for (unsigned b = 0; b < sizeof(k_cap_name) / sizeof(k_cap_name[0]);
         ++b) {
        if (bit == (uint16_t)(1u << b)) {
            return k_cap_name[b];
        }
    }
    return "";
}

bool prog_reg_detail(const prog_reg_row_t *rows, size_t n, size_t i,
                     uint16_t caps, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return true;
    }
    out[0] = '\0';
    if (rows == NULL || i >= n) {
        return true;
    }
    const prog_reg_row_t *r = &rows[i];
    switch (prog_reg_state(rows, n, i, caps)) {
    case PROG_ROW_NO_CAP:
        snprintf(out, size, TR(PL_NO_CAP),
                 prog_reg_cap_name(prog_reg_missing(r, caps)));
        return true;
    case PROG_ROW_PORT_BUSY:
        snprintf(out, size, "%s", TR(PL_PORT_BUSY));
        return true;
    default:
        break;
    }
    const uint8_t pin = (r->page_set != NULL && r->page_set->pin != NULL)
                            ? prog_reg_call_pin(r->page_set)
                            : PROG_REG_NO_PIN;
    if (pin == PROG_REG_NO_PIN) {
        snprintf(out, size, "%s", ui_tr(r->transport));
    } else {
        snprintf(out, size, "%s GP%u", ui_tr(r->transport), (unsigned)pin);
    }
    return false;
}

/* -------------------------------------------------------- the list model */

void prog_list_build(prog_list_t *l, const prog_reg_row_t *rows, size_t n,
                     uint8_t device, uint8_t task, const char *query)
{
    if (l == NULL) {
        return;
    }
    memset(l, 0, sizeof(*l));
    if (rows == NULL) {
        return;
    }
    /* The list's rows, then those the search finds, in the table's order. */
    uint8_t found[PROG_LIST_MAX] = { 0 };
    for (size_t i = 0; i < n && i <= 0xFFu; ++i) {
        if (rows[i].device != device || rows[i].task != task
            || l->total >= PROG_LIST_MAX) {
            continue;
        }
        ++l->total;
        if (esc_text_matches(ui_tr(rows[i].name), query)) {
            found[l->rows++] = (uint8_t)i;
        }
    }
    l->search   = l->total > PROG_LIST_SEARCH_OVER;
    l->headings = l->rows > PROG_LIST_HEADINGS_OVER;
    if (!l->headings) {
        for (uint8_t k = 0; k < l->rows; ++k) {
            l->e[l->count++] = (prog_list_entry_t){ found[k], false };
        }
        return;
    }
    /* Under headings: each group where the table first names it, its rows
     * in the table's order under it. */
    bool placed[PROG_LIST_MAX] = { false };
    for (uint8_t k = 0; k < l->rows; ++k) {
        if (placed[k]) {
            continue;
        }
        const ui_text_id_t g = rows[found[k]].group;
        l->e[l->count++] = (prog_list_entry_t){ found[k], true };
        for (uint8_t m = k; m < l->rows; ++m) {
            if (!placed[m] && rows[found[m]].group == g) {
                placed[m] = true;
                l->e[l->count++] = (prog_list_entry_t){ found[m], false };
            }
        }
    }
}

/* --------------------------------------------------------------- the view */

void prog_view_init(prog_view_t *v, gfx_rect_t area)
{
    if (v == NULL) {
        return;
    }
    memset(v, 0, sizeof(*v));
    v->area    = area;
    v->pressed = PROG_VIEW_P_NONE;
}

void prog_view_top(prog_view_t *v)
{
    if (v != NULL) {
        v->top = 0;
        v->pressed = PROG_VIEW_P_NONE;
    }
}

static int entry_h(const prog_list_t *l, int k)
{
    return (k >= 0 && k < l->count && l->e[k].heading) ? PROG_VIEW_HEAD_P
                                                       : PROG_VIEW_ROW_P;
}

/* The height the entries take from the top of the area, bar aside. */
static int room(const prog_view_t *v, bool bar)
{
    return v->area.h - 2 * PROG_VIEW_PAD + 6
           - (bar ? PROG_VIEW_BAR_H + 8 : 0);
}

bool prog_view_has_bar(const prog_view_t *v, const prog_list_t *l)
{
    if (v == NULL || l == NULL) {
        return false;
    }
    if (l->search) {
        return true;
    }
    int h = 0;
    for (int k = 0; k < l->count; ++k) {
        h += entry_h(l, k);
    }
    return h > room(v, false);
}

/* The entries shown from @p top: as many as fit, one at least. */
static int shown_from(const prog_view_t *v, const prog_list_t *l, int top)
{
    const int space = room(v, prog_view_has_bar(v, l));
    int h = 0, k = top;
    while (k < l->count && h + entry_h(l, k) <= space) {
        h += entry_h(l, k);
        ++k;
    }
    if (k == top && top < l->count) {
        ++k;
    }
    return k - top;
}

int prog_view_shown(const prog_view_t *v, const prog_list_t *l)
{
    if (v == NULL || l == NULL || v->top < 0 || v->top >= l->count) {
        return 0;
    }
    return shown_from(v, l, v->top);
}

static int list_y(const prog_view_t *v, const prog_list_t *l)
{
    return v->area.y + PROG_VIEW_PAD
           + (prog_view_has_bar(v, l) ? PROG_VIEW_BAR_H + 8 : 0);
}

gfx_rect_t prog_view_entry_rect(const prog_view_t *v, const prog_list_t *l,
                                int k)
{
    if (v == NULL || l == NULL || k < 0 || k < v->top
        || k >= v->top + prog_view_shown(v, l)) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    int y = list_y(v, l);
    for (int j = v->top; j < k; ++j) {
        y += entry_h(l, j);
    }
    const int16_t x = (int16_t)(v->area.x + PROG_VIEW_PAD);
    const int16_t w = (int16_t)(v->area.w - 2 * PROG_VIEW_PAD);
    return (gfx_rect_t){ x, (int16_t)y, w,
                         l->e[k].heading ? PROG_VIEW_HEAD_H
                                         : PROG_VIEW_ROW_H };
}

/* The bar: SEARCH from the left, "^" and "v" 38 px each at the right. */
gfx_rect_t prog_view_search_rect(const prog_view_t *v, const prog_list_t *l)
{
    if (v == NULL || l == NULL || !l->search) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(v->area.x + PROG_VIEW_PAD),
                         (int16_t)(v->area.y + PROG_VIEW_PAD), 220,
                         PROG_VIEW_BAR_H };
}

static bool pages(const prog_view_t *v, const prog_list_t *l)
{
    return prog_view_has_bar(v, l) && shown_from(v, l, 0) < l->count;
}

gfx_rect_t prog_view_up_rect(const prog_view_t *v, const prog_list_t *l)
{
    if (v == NULL || l == NULL || !pages(v, l)) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(v->area.x + v->area.w - PROG_VIEW_PAD
                                   - 76 - 4),
                         (int16_t)(v->area.y + PROG_VIEW_PAD), 38,
                         PROG_VIEW_BAR_H };
}

gfx_rect_t prog_view_down_rect(const prog_view_t *v, const prog_list_t *l)
{
    if (v == NULL || l == NULL || !pages(v, l)) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(v->area.x + v->area.w - PROG_VIEW_PAD
                                   - 38),
                         (int16_t)(v->area.y + PROG_VIEW_PAD), 38,
                         PROG_VIEW_BAR_H };
}

/* The top a page up from @p v's lands on: the first entry of the page
 * that ends just before it. */
static int page_up_top(const prog_view_t *v, const prog_list_t *l)
{
    const int space = room(v, true);
    int h = 0, k = v->top;
    while (k > 0 && h + entry_h(l, k - 1) <= space) {
        h += entry_h(l, k - 1);
        --k;
    }
    return (k == v->top && k > 0) ? k - 1 : k;
}

/* What is under (@p x, @p y): an entry, a bar key, or none. */
static int hit(const prog_view_t *v, const prog_list_t *l, int x, int y)
{
    if (gfx_rect_contains(prog_view_search_rect(v, l), x, y)) {
        return PROG_VIEW_P_SEARCH;
    }
    if (gfx_rect_contains(prog_view_up_rect(v, l), x, y)) {
        return PROG_VIEW_P_UP;
    }
    if (gfx_rect_contains(prog_view_down_rect(v, l), x, y)) {
        return PROG_VIEW_P_DOWN;
    }
    const int end = v->top + prog_view_shown(v, l);
    for (int k = v->top; k < end; ++k) {
        if (gfx_rect_contains(prog_view_entry_rect(v, l, k), x, y)) {
            return k;
        }
    }
    return PROG_VIEW_P_NONE;
}

prog_view_ev_t prog_view_event(prog_view_t *v, const prog_list_t *l,
                               const touch_event_t *evt, uint8_t *row)
{
    if (v == NULL || l == NULL || evt == NULL) {
        return PROG_VIEW_NONE;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!gfx_rect_contains(v->area, x, y)) {
            return PROG_VIEW_NONE;
        }
        if (v->pressed == PROG_VIEW_P_NONE) {
            const int h = hit(v, l, x, y);
            if (h != PROG_VIEW_P_NONE) {
                v->pressed = h;
                v->id      = evt->point.id;
            }
        }
        return PROG_VIEW_TAKEN;
    }
    if (v->pressed == PROG_VIEW_P_NONE || evt->point.id != v->id) {
        return PROG_VIEW_NONE;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return PROG_VIEW_TAKEN;
    }
    const int was = v->pressed;
    v->pressed = PROG_VIEW_P_NONE;
    if (!touch_event_is_tap_up(evt) || hit(v, l, x, y) != was) {
        return PROG_VIEW_TAKEN;
    }
    switch (was) {
    case PROG_VIEW_P_SEARCH:
        return PROG_VIEW_SEARCH;
    case PROG_VIEW_P_UP:
        v->top = page_up_top(v, l);
        return PROG_VIEW_TAKEN;
    case PROG_VIEW_P_DOWN: {
        const int next = v->top + prog_view_shown(v, l);
        if (next < l->count) {
            v->top = next;
        }
        return PROG_VIEW_TAKEN;
    }
    default:
        if (was >= 0 && was < l->count && !l->e[was].heading) {
            if (row != NULL) {
                *row = l->e[was].row;
            }
            return PROG_VIEW_ROW;
        }
        return PROG_VIEW_TAKEN;
    }
}

void prog_view_cancel(prog_view_t *v)
{
    if (v != NULL) {
        v->pressed = PROG_VIEW_P_NONE;
    }
}

void prog_view_render(const prog_view_t *v, const prog_list_t *l,
                      const prog_reg_row_t *rows, size_t n, uint16_t caps,
                      const char *query, gfx_canvas_t *c)
{
    if (v == NULL || l == NULL || c == NULL) {
        return;
    }
    const gfx_rect_t sr = prog_view_search_rect(v, l);
    if (sr.w > 0) {
        const bool typed = query != NULL && query[0] != '\0';
        gfx_fill_round_rect(c, sr.x, sr.y, sr.w, sr.h, UI_R_CTL,
                            v->pressed == PROG_VIEW_P_SEARCH
                                ? ui_theme_color(UI_C_PANEL_HI)
                                : ui_theme_color(UI_C_PANEL_SUNK));
        gfx_draw_round_rect(c, sr.x, sr.y, sr.w, sr.h, UI_R_CTL,
                            ui_theme_color(UI_C_EDGE));
        const gfx_rect_t in = { (int16_t)(sr.x + 10), sr.y,
                                (int16_t)(sr.w - 20), sr.h };
        gfx_text_in(c, in, typed ? query : TR(SP_FIND), UI_FONT_LABEL,
                    typed ? ui_theme_color(UI_C_TEXT)
                          : ui_theme_color(UI_C_TEXT_DIM),
                    1, GFX_ALIGN_LEFT);
    }
    const gfx_rect_t up = prog_view_up_rect(v, l);
    if (up.w > 0) {
        ui_button(c, up, "^", ui_theme_color(UI_C_PANEL_HI),
                  v->pressed == PROG_VIEW_P_UP, v->top > 0);
        ui_button(c, prog_view_down_rect(v, l), "v",
                  ui_theme_color(UI_C_PANEL_HI),
                  v->pressed == PROG_VIEW_P_DOWN,
                  v->top + prog_view_shown(v, l) < l->count);
    }
    const int end = v->top + prog_view_shown(v, l);
    for (int k = (v->top > 0) ? v->top : 0; k < end && k < l->count; ++k) {
        const gfx_rect_t r = prog_view_entry_rect(v, l, k);
        const uint8_t i = l->e[k].row;
        if (rows == NULL || i >= n) {
            continue;
        }
        if (l->e[k].heading) {
            gfx_text(c, r.x, r.y + (r.h - 16) / 2, ui_tr(rows[i].group),
                     UI_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1);
            continue;
        }
        char detail[48];
        const bool dim = prog_reg_detail(rows, n, i, caps, detail,
                                         sizeof(detail));
        gfx_color_t body = ui_theme_color(UI_C_PANEL_SUNK);
        if (v->pressed == k && !dim) {
            body = gfx_lerp(body, GFX_WHITE, 110);
        }
        gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL, body);
        if (dim) {
            gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL,
                                ui_theme_color(UI_C_EDGE));
        }
        const int ty = r.y + (r.h - 16) / 2;
        gfx_text(c, r.x + 12, ty, ui_tr(rows[i].name), UI_FONT_LABEL,
                 dim ? ui_theme_color(UI_C_TEXT_DIM)
                     : ui_theme_color(UI_C_TEXT), 1);
        const gfx_rect_t d = { (int16_t)(r.x + r.w / 2 - 20), r.y,
                               (int16_t)(r.w / 2 + 20 - 30), r.h };
        gfx_text_in(c, d, detail, UI_FONT_LABEL,
                    dim ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM),
                    1, GFX_ALIGN_RIGHT);
        if (!dim) {
            gfx_text(c, r.x + r.w - 20, ty, ">", UI_FONT_LABEL,
                     ui_theme_color(UI_C_ACCENT), 1);
        }
    }
}
