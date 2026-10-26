/**
 * @file hitec_panel.c
 * @brief The overlay of the programmer for servos by Hitec: its state, rows,
 *        geometry and the caller's side.  See hitec_panel.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.
 *
 * The texts are hitec_panel_text.c, the actions and editors
 * hitec_panel_edit.c, the presses hitec_panel_touch.c and the drawing
 * hitec_panel_draw.c, with hitec_panel_int.h between them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "hitec_reg.h"
#include "settings.h"
#include "ui_tabs.h"
#include "ui_text.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The classic model name row: no parameter of hitec_reg. */
#define W_NAME ((uint8_t)HITEC_P_COUNT)

/** @brief The rows of every tab and both series; hitec_panel_int.h. */
const hpn_row_t hpn_rows[] = {
    { HITEC_PANEL_SERVO, 0, 0, 1, K_ACT, A_PORT, S_ANY, -1, "PORT" },
    { HITEC_PANEL_SERVO, 1, 0, 0, K_ACT, A_SERIES, S_ANY, TX_HP_SERIES, NULL },
    { HITEC_PANEL_SERVO, 1, 1, 0, K_ACT, A_READ, S_ANY, -1, "SERVO" },
    { HITEC_PANEL_SERVO, 2, 0, 0, K_ACT, A_MORE, S_ANY, TX_KP_MORE, NULL },
    { HITEC_PANEL_SERVO, 2, 1, 0, K_ACT, A_CLEAR, S_ANY, TX_HP_STOP_ROW,
      NULL },

    { HITEC_PANEL_MOTION, 0, 0, 1, K_SHOW, HITEC_P_D_SPEED, S_D,
      TX_HP_SPEED, NULL },
    { HITEC_PANEL_MOTION, 1, 0, 1, K_SHOW, HITEC_P_D_DEAD_BAND, S_D,
      TX_HP_DEADBAND, NULL },
    { HITEC_PANEL_MOTION, 2, 0, 1, K_SHOW, HITEC_P_D_SOFT_START, S_D,
      TX_HP_SOFTSTART, NULL },
    { HITEC_PANEL_MOTION, 0, 0, 1, K_SHOW, HITEC_P_C_SPEED, S_C,
      TX_HP_SPEED, NULL },
    { HITEC_PANEL_MOTION, 1, 0, 1, K_SHOW, HITEC_P_C_DEAD_BAND, S_C,
      TX_HP_DEADBAND, NULL },
    { HITEC_PANEL_MOTION, 2, 0, 1, K_SHOW, HITEC_P_C_RESOLUTION, S_C,
      TX_HP_RESOLUTION, NULL },

    { HITEC_PANEL_TRAVEL, 0, 0, 1, K_SHOW, HITEC_P_D_NEUTRAL, S_D,
      TX_HP_NEUTRAL, NULL },
    { HITEC_PANEL_TRAVEL, 1, 0, 1, K_SHOW, HITEC_P_D_LEFT_END, S_D,
      TX_HP_END_L, NULL },
    { HITEC_PANEL_TRAVEL, 2, 0, 1, K_SHOW, HITEC_P_D_RIGHT_END, S_D,
      TX_HP_END_R, NULL },
    { HITEC_PANEL_TRAVEL, 3, 0, 1, K_SHOW, HITEC_P_D_DIRECTION, S_D,
      TX_HP_DIRECTION, NULL },
    { HITEC_PANEL_TRAVEL, 0, 0, 1, K_SHOW, HITEC_P_C_CENTRE, S_C,
      TX_HP_CENTRE, NULL },
    { HITEC_PANEL_TRAVEL, 1, 0, 1, K_SHOW, HITEC_P_C_LEFT_END, S_C,
      TX_HP_END_L, NULL },
    { HITEC_PANEL_TRAVEL, 2, 0, 1, K_SHOW, HITEC_P_C_RIGHT_END, S_C,
      TX_HP_END_R, NULL },
    { HITEC_PANEL_TRAVEL, 3, 0, 1, K_SHOW, HITEC_P_C_DIRECTION, S_C,
      TX_HP_DIRECTION, NULL },

    { HITEC_PANEL_PROTECT, 0, 0, 1, K_SHOW, HITEC_P_D_FAILSAFE, S_D,
      TX_HP_FAILSAFE, NULL },
    { HITEC_PANEL_PROTECT, 1, 0, 1, K_SHOW, HITEC_P_D_OVERLOAD, S_D,
      TX_HP_OVERLOAD, NULL },
    { HITEC_PANEL_PROTECT, 2, 0, 1, K_SHOW, HITEC_P_D_SMART_SENSOR, S_D,
      -1, "SMART SENSOR" },
    { HITEC_PANEL_PROTECT, 3, 0, 1, K_SHOW, HITEC_P_D_SENSITIVITY, S_D,
      TX_HP_SENS, NULL },
    { HITEC_PANEL_PROTECT, 0, 0, 1, K_SHOW, HITEC_P_C_FAILSAFE, S_C,
      TX_HP_FAILSAFE, NULL },
    { HITEC_PANEL_PROTECT, 1, 0, 1, K_SHOW, HITEC_P_C_FS_POSITION, S_C,
      TX_HP_FS_POS, NULL },
    { HITEC_PANEL_PROTECT, 2, 0, 1, K_SHOW, HITEC_P_C_OVERLOAD, S_C,
      TX_HP_OVERLOAD, NULL },

    { HITEC_PANEL_INFO, 0, 0, 0, K_SHOW, HITEC_P_D_MODEL, S_D, TX_HP_MODEL,
      NULL },
    { HITEC_PANEL_INFO, 0, 1, 0, K_SHOW, HITEC_P_D_ID, S_D, -1, "ID" },
    { HITEC_PANEL_INFO, 1, 0, 0, K_SHOW, HITEC_P_D_POSITION, S_D,
      TX_HP_POSITION, NULL },
    { HITEC_PANEL_INFO, 1, 1, 0, K_SHOW, HITEC_P_D_R04, S_D, -1, "REG 04" },
    { HITEC_PANEL_INFO, 2, 0, 0, K_SHOW, HITEC_P_D_R22, S_D, -1, "REG 22" },
    { HITEC_PANEL_INFO, 2, 1, 0, K_SHOW, HITEC_P_D_R8A, S_D, -1, "REG 8A" },
    { HITEC_PANEL_INFO, 3, 0, 0, K_SHOW, HITEC_P_D_R8C, S_D, -1, "REG 8C" },
    { HITEC_PANEL_INFO, 3, 1, 0, K_SHOW, HITEC_P_D_RD4, S_D, -1, "REG D4" },
    { HITEC_PANEL_INFO, 4, 0, 0, K_SHOW, HITEC_P_D_RD6, S_D, -1, "REG D6" },
    { HITEC_PANEL_INFO, 0, 0, 1, K_SHOW, W_NAME, S_C, TX_HP_MODEL, NULL },
    { HITEC_PANEL_INFO, 1, 0, 1, K_SHOW, HITEC_P_C_VERSION, S_C,
      TX_HP_VERSION, NULL },
    { HITEC_PANEL_INFO, 2, 0, 1, K_SHOW, HITEC_P_C_DATE, S_C, TX_HP_DATE,
      NULL },
    { HITEC_PANEL_INFO, 3, 0, 1, K_SHOW, HITEC_P_C_SERIAL, S_C,
      TX_HP_SERIAL, NULL },
    { HITEC_PANEL_INFO, 4, 0, 1, K_SHOW, HITEC_P_C_CHECKSUM, S_C,
      TX_HP_CHECKSUM, NULL },
};
_Static_assert(sizeof(hpn_rows) / sizeof(hpn_rows[0]) == HP_ROW_COUNT,
               "HP_ROW_COUNT in hitec_panel_int.h counts hpn_rows");

/** @brief The tabs' labels. */
static const ui_text_id_t k_tabs[HITEC_PANEL_PAGES] = {
    TX_KP_TAB_SERVO, TX_HP_TAB_MOTION, TX_HP_TAB_TRAVEL, TX_HP_TAB_PROTECT,
    TX_HP_TAB_INFO,
};

/* The state, its type in hitec_panel_int.h. */
hpn_state_t hitec_panel_h;

/** @brief Not part of hp: a start of the screens leaves it. */
static hitec_prog_gate_t *s_gate;

/* ------------------------------------------------------------------- rows */

bool hpn_row_shown(const hpn_row_t *r)
{
    const uint8_t series = hitec_prog_series(&hp.prog);
    const uint8_t mask = series == (uint8_t)HITEC_D ? (uint8_t)S_D
                                                    : (uint8_t)S_C;

    if ((r->series & mask) == 0u) {
        return false;
    }
    if (r->kind == K_SHOW && r->what < (uint8_t)HITEC_P_COUNT) {
        return hitec_param(hitec_prog_layout(&hp.prog), r->what) != NULL;
    }
    return true;
}

const hpn_row_t *hpn_row_at(int page, int row, int col)
{
    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        const hpn_row_t *r = &hpn_rows[i];

        if (r->page == page && r->row == row && r->col == col
            && hpn_row_shown(r)) {
            return r;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------- geometry */

gfx_rect_t hpn_tabs_rect(void)
{
    return (gfx_rect_t){ (int16_t)(hp.area.x + 8), (int16_t)(hp.area.y
                                                             + HP_TAB_Y),
                         (int16_t)(hp.area.w - 16 - HP_CLOSE_W - 8),
                         HP_TAB_H };
}

int hpn_foot(void)
{
    const int area = hp.area.y + hp.area.h - 8;
    const int band = UI_ALERT_Y - UI_BAND_H - HP_FOOT_GAP;

    return area < band ? area : band;
}

int hpn_notes_foot(void)
{
    const int area = hp.area.y + hp.area.h - 6;
    const int band = UI_ALERT_Y - UI_BAND_H - HP_FOOT_GAP;

    return area < band ? area : band;
}

gfx_rect_t hpn_row_rect(const hpn_row_t *r)
{
    const int col_w = (hp.area.w - 30) / 2;
    const int x = r->col == 0u ? hp.area.x + 10 : hp.area.x + 20 + col_w;

    return (gfx_rect_t){ (int16_t)x,
                         (int16_t)(hp.area.y + HP_ROW0 + r->row * HP_PITCH),
                         (int16_t)(r->wide != 0u ? hp.area.w - 20 : col_w),
                         HP_ROW_H };
}

gfx_rect_t hpn_value_rect(const hpn_row_t *r)
{
    const gfx_rect_t rr = hpn_row_rect(r);
    const int w = r->wide != 0u && r->kind == K_SHOW ? HP_WIDE_W : HP_VAL_W;

    return (gfx_rect_t){ (int16_t)(rr.x + rr.w - w), rr.y, (int16_t)w,
                         rr.h };
}

int hpn_cells(void)
{
    const int cells = (hp.area.w - 20) / HP_CELL;

    return cells > 0 ? cells : 1;
}

/**
 * @brief The bytes of @p s its first row takes: all of it where it fits,
 *        else up to the last space that leaves the row no wider than the
 *        line; a word longer than the line is cut.
 * @param s the text
 * @return the bytes
 */
static size_t row_len(const char *s)
{
    const size_t fit = gfx_text_prefix(s, hpn_cells());
    size_t cut = fit;

    if (s[fit] == '\0' || s[fit] == ' ') {
        return fit;
    }
    while (cut > 0u && s[cut] != ' ') {
        --cut;
    }
    return cut > 0u ? cut : fit;
}

/**
 * @brief The start of the row after one of @p len bytes at @p s.
 * @param s   the row
 * @param len its bytes
 * @return the next row
 */
static const char *next_row(const char *s, size_t len)
{
    s += len;
    while (*s == ' ') {
        ++s;
    }
    return s;
}

int hitec_panel_text_rows(const char *s)
{
    int n = 0;

    while (s != NULL && *s != '\0') {
        s = next_row(s, row_len(s));
        ++n;
    }
    return n;
}

int hpn_text_rows(gfx_canvas_t *c, int x, int y, const char *s,
                  gfx_color_t color)
{
    while (*s != '\0') {
        char row[HITEC_PANEL_LINE];
        const size_t len = row_len(s);

        snprintf(row, sizeof(row), "%.*s", (int)len, s);
        gfx_text(c, x, y, row, HP_FONT, color, 1);
        s = next_row(s, len);
        y += HP_LINE_H;
    }
    return y;
}

gfx_rect_t hitec_panel_tab_rect(int page)
{
    return hp.tabs.rect[page >= 0 && page < HITEC_PANEL_PAGES ? page : 0];
}

gfx_rect_t hitec_panel_close_rect(void)
{
    return (gfx_rect_t){ (int16_t)(hp.area.x + hp.area.w - 8 - HP_CLOSE_W),
                         (int16_t)(hp.area.y + HP_TAB_Y), HP_CLOSE_W,
                         HP_TAB_H };
}

gfx_rect_t hitec_panel_value_rect(int page, int row, int col)
{
    const hpn_row_t *r = hpn_row_at(page, row, col);

    return r != NULL ? hpn_value_rect(r) : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t hitec_panel_choice_rect(int i)
{
    return (gfx_rect_t){ (int16_t)(hp.area.x + 10),
                         (int16_t)(hp.area.y + 44 + i * HP_PITCH),
                         (int16_t)(hp.area.w - 20), HP_ROW_H };
}

gfx_rect_t hitec_panel_choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(hp.area.x + hp.area.w - 10 - 150),
                         (int16_t)(hpn_foot() - UI_BTN_H), 150, UI_BTN_H };
}

gfx_rect_t hitec_panel_apply_rect(void)
{
    return (gfx_rect_t){ (int16_t)(hp.area.x + 20),
                         (int16_t)(hpn_foot() - UI_BTN_H_TALL), 300,
                         UI_BTN_H_TALL };
}

gfx_rect_t hitec_panel_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(hp.area.x + hp.area.w - 20 - 180),
                         (int16_t)(hpn_foot() - UI_BTN_H_TALL), 180,
                         UI_BTN_H_TALL };
}

/* ---------------------------------------------------------- for the tests */

hitec_prog_t *hitec_panel_model(void)
{
    return &hp.prog;
}

ui_keypad_t *hitec_panel_keypad(void)
{
    return &hp.kp;
}

const char *hitec_panel_label(int page, int row, int col)
{
    const hpn_row_t *r = hpn_row_at(page, row, col);

    return r != NULL ? hpn_label(r) : NULL;
}

const char *hitec_panel_value(int page, int row, int col)
{
    static char buf[HITEC_PANEL_LINE];
    const hpn_row_t *r = hpn_row_at(page, row, col);
    bool warn = false;

    if (r == NULL) {
        return NULL;
    }
    hpn_value_text(r, buf, sizeof(buf), &warn);
    return buf;
}

bool hitec_panel_row_live(int page, int row, int col)
{
    const hpn_row_t *r = hpn_row_at(page, row, col);

    return r != NULL && hpn_row_live(r);
}

const char *hitec_panel_port_line(int i)
{
    static char buf[HITEC_PANEL_LINE];

    return hpn_port_line(i, buf, sizeof(buf)) ? buf : NULL;
}

int hitec_panel_editor(void)
{
    return hp.kp.open ? 1 : hp.ch.open ? 2 : hp.page.open ? 3 : 0;
}

int hitec_panel_page(void)
{
    return hp.tabs.selected;
}

const char *hitec_panel_choice_label(int i)
{
    return hp.ch.open && i >= 0 && i < hp.ch.count ? hp.ch.labels[i] : NULL;
}

const char *hitec_panel_page_line(int i)
{
    if (!hp.page.open || i < 0 || i > hp.page.count) {
        return NULL;
    }
    return i == 0 ? hp.page.title : hp.page.lines[i - 1];
}

/* --------------------------------------------------------------- the caller */

void hitec_panel_gate(hitec_prog_gate_t *g)
{
    s_gate = g;
    hitec_prog_gate_set(&hp.prog, g);
}

void hitec_panel_init(gfx_rect_t area)
{
    memset(&hp, 0, sizeof(hp));
    hitec_prog_init(&hp.prog);
    hitec_prog_gate_set(&hp.prog, s_gate);
    hp.area = area;
    ui_tabs_init_text(&hp.tabs, k_tabs, HITEC_PANEL_PAGES, hpn_tabs_rect());
    hp.kp.pressed = -1;
}

/**
 * @brief The options from the settings where they are loaded: at a screen
 *        entry and at an opening, not at hitec_panel_init(), which runs
 *        before the settings are loaded.  A session keeps what it began
 *        with.
 */
static void options_fresh(void)
{
    if (!hp.loaded || !hitec_panel_holds_port()) {
        hpn_options_load();
        hp.loaded = true;
    }
}

void hitec_panel_screen_entry(void)
{
    options_fresh();
    ++hp.rev;
}

void hitec_panel_open(void)
{
    if (!hp.open) {
        hp.open = true;
        options_fresh();
        ++hp.rev;
    }
}

bool hitec_panel_is_open(void)
{
    return hp.open;
}

bool hitec_panel_holds_port(void)
{
    return hitec_prog_holds_port(&hp.prog);
}

uint8_t hitec_panel_pin(void)
{
    const uint8_t pin = hitec_prog_pin(&hp.prog);

    return pin == HITEC_PROG_NO_PIN ? (uint8_t)PROG_REG_NO_PIN : pin;
}

bool hitec_panel_safe_at(int x, int y)
{
    /* Programming and an armed bench are exclusive, and ABORT here ends
     * the wire's transaction, not a drive. */
    (void)x;
    (void)y;
    return false;
}

uint32_t hitec_panel_revision(void)
{
    return hp.rev + hp.kp.revision;
}

uint32_t hitec_panel_hold_revision(void)
{
    return hp.hold_rev;
}

/** @brief Everything of the model a render shows, as one number. */
static uint32_t signature(void)
{
    const hitec_prog_t *p = &hp.prog;
    unsigned done = 0;
    unsigned total = 0;
    const uint32_t part[] = {
        (uint32_t)hitec_prog_state(p), p->msg.seq, p->f.rd.flags,
        p->f.rd.port_no, p->f.rd.held, p->f.rd.ours, p->f.rd.have_line,
        p->f.rd.pin, p->f.rd.line, p->f.target, p->f.port_pin_known,
        p->f.port_pin[0], p->f.port_pin[1], p->f.port_pin[2],
        p->f.port_pin[3], p->f.stopped, p->f.supply_read,
        p->f.out_mv / 10u, p->f.out_ma, p->f.rx_mark, p->have_servo,
        p->series, hitec_prog_mv(p), hitec_prog_ma(p),
        hitec_prog_mv_capped(p), hitec_prog_in_mode(p),
        hitec_prog_progress(p, &done, &total) ? done : 0u,
        hitec_prog_retry_code(p),
    };
    uint32_t sig = 2166136261u;

    for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); ++i) {
        sig = (sig ^ part[i]) * 16777619u;
    }
    return sig;
}

void hpn_moved(void)
{
    const hitec_prog_state_t st = hitec_prog_state(&hp.prog);
    uint32_t sig;

    if (st != hp.state_seen) {
        /* An editor asks for something of the state it was opened in. */
        hpn_close_editors();
        if (hp.pressed != P_TAB && hp.pressed != P_CLOSE) {
            hp.pressed = P_NONE;
        }
        hp.state_seen = st;
    }
    hpn_supply_sync();
    sig = signature();
    if (sig != hp.sig) {
        hp.sig = sig;
        ++hp.rev;
    }
}

void hitec_panel_feed(const hitec_prog_feed_t *f)
{
    hitec_prog_feed(&hp.prog, f);
    hpn_moved();
}

bool hitec_panel_take(hitec_prog_cmd_t *out)
{
    return hitec_prog_take(&hp.prog, out);
}

void hitec_panel_bench_armed(bool armed)
{
    hitec_prog_bench_armed(&hp.prog, armed);
}

/* The session SERVO > PROGRAM opens from its row (prog_registry.h). */
const prog_page_set_t hitec_panel_pages = {
    .init          = hitec_panel_init,
    .entry         = hitec_panel_screen_entry,
    .open          = hitec_panel_open,
    .is_open       = hitec_panel_is_open,
    .back          = hitec_panel_back,
    .holds_port    = hitec_panel_holds_port,
    .armed         = hitec_panel_bench_armed,
    .tick          = hitec_panel_tick,
    .has_press     = hitec_panel_has_press,
    .event         = hitec_panel_event,
    .render        = hitec_panel_render,
    .revision      = hitec_panel_revision,
    .hold_revision = hitec_panel_hold_revision,
    .hold_rect     = hitec_panel_apply_rect,
    .leave         = hitec_panel_leave,
    .cancel        = hitec_panel_cancel,
    .pin           = hitec_panel_pin,
    .safe_at       = hitec_panel_safe_at,
};
