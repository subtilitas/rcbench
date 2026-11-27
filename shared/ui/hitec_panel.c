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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hitec_move.h"
#include "hitec_reg.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_tabs.h"
#include "ui_text.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The classic model name row: no parameter of hitec_reg. */
#define W_NAME ((uint8_t)HITEC_P_COUNT)

/**
 * @brief The rows of every tab and both series, in the frame's flow order
 *        per tab: a wide row on a line of its own, two narrow rows to a
 *        line (sprog.h).  Rows of one place that never show together (the
 *        series, SERIES and AFTER STOP, DIRECTION and FIND STOP) follow
 *        each other.  X(tab, wide, kind, what, series, show bits, control
 *        width or 0, label, name).
 */
#define HP_ROWS(X)                                                           \
    /* The head: the port, the series, the supply; CONNECT, ABORT, LEAVE */ \
    X(HITEC_PANEL_SERVO, 1, K_ACT, A_PORT, S_ANY, 0, 0, -1, "PORT")          \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_READ, S_ANY, 0, 0, -1, "SERVO")         \
    /* The brand slot: SERIES before CONNECT, AFTER STOP while held. */      \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_SERIES, S_ANY, HT_SHOW_FREE, 0,         \
      TX_HP_SERIES, NULL)                                                    \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_CLEAR, S_ANY, HT_SHOW_HELD, 0,          \
      TX_HP_STOP_ROW, NULL)                                                  \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_FILES, S_ANY, 0, 0, TX_KP_FILES, NULL)  \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_MORE, S_ANY, 0, 0, TX_KP_MORE, NULL)    \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_RESTORE, S_ANY, 0, 0, TX_KP_RESTORE,    \
      NULL)                                                                  \
    /* WRITE n VALUES says what it writes: no label.  A 2s hold. */          \
    X(HITEC_PANEL_SERVO, 0, K_ACT, A_WRITE, S_ANY, 0, HP_WRITE_W, -1, "")    \
                                                                             \
    X(HITEC_PANEL_MOTION, 1, K_SHOW, HITEC_P_D_SPEED, S_D, HT_SHOW_D, 0,     \
      TX_HP_SPEED, NULL)                                                     \
    X(HITEC_PANEL_MOTION, 1, K_SHOW, HITEC_P_D_DEAD_BAND, S_D, HT_SHOW_D, 0, \
      TX_HP_DEADBAND, NULL)                                                  \
    X(HITEC_PANEL_MOTION, 1, K_SHOW, HITEC_P_D_SOFT_START, S_D, HT_SHOW_D,   \
      0, TX_HP_SOFTSTART, NULL)                                              \
    X(HITEC_PANEL_MOTION, 1, K_SHOW, HITEC_P_C_SPEED, S_C, HT_SHOW_C, 0,     \
      TX_HP_SPEED, NULL)                                                     \
    X(HITEC_PANEL_MOTION, 1, K_SHOW, HITEC_P_C_DEAD_BAND, S_C, HT_SHOW_C, 0, \
      TX_HP_DEADBAND, NULL)                                                  \
    X(HITEC_PANEL_MOTION, 1, K_SHOW, HITEC_P_C_RESOLUTION, S_C, HT_SHOW_C,   \
      0, TX_HP_RESOLUTION, NULL)                                             \
                                                                             \
    /* The positions' unit in the label: the value narrows to its keys      \
     * while MOVE SERVO runs. */                                             \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_D_NEUTRAL, S_D, HT_SHOW_D, 0,   \
      TX_HP_NEUTRAL_N, NULL)                                                 \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_D_LEFT_END, S_D, HT_SHOW_D, 0,  \
      TX_HP_END_L_N, NULL)                                                   \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_D_RIGHT_END, S_D, HT_SHOW_D, 0, \
      TX_HP_END_R_N, NULL)                                                   \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_D_DIRECTION, S_D,               \
      HT_SHOW_STILL, 0, TX_HP_DIRECTION, NULL)                               \
    /* FIND STOP: LEFT END and RIGHT END keys in DIRECTION's place while    \
     * MOVE SERVO's loop runs, which holds DIRECTION at CW. */               \
    X(HITEC_PANEL_TRAVEL, 1, K_ACT, A_FIND, S_D, HT_SHOW_MOVE, 0,            \
      TX_HP_FIND, NULL)                                                      \
    /* MOVE SERVO: the horn follows the position edited (hitec_move.h). */  \
    X(HITEC_PANEL_TRAVEL, 1, K_ACT, A_MOVE, S_D, HT_SHOW_D, HP_VAL_W,        \
      TX_HP_MOVE, NULL)                                                      \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_C_CENTER, S_C, HT_SHOW_C, 0,    \
      TX_HP_CENTER, NULL)                                                    \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_C_LEFT_END, S_C, HT_SHOW_C, 0,  \
      TX_HP_END_L, NULL)                                                     \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_C_RIGHT_END, S_C, HT_SHOW_C, 0, \
      TX_HP_END_R, NULL)                                                     \
    X(HITEC_PANEL_TRAVEL, 1, K_SHOW, HITEC_P_C_DIRECTION, S_C, HT_SHOW_C, 0, \
      TX_HP_DIRECTION, NULL)                                                 \
                                                                             \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_D_FAILSAFE, S_D, HT_SHOW_D, 0, \
      TX_HP_FAILSAFE, NULL)                                                  \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_D_OVERLOAD, S_D, HT_SHOW_D, 0, \
      TX_HP_OVERLOAD, NULL)                                                  \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_D_SMART_SENSOR, S_D,           \
      HT_SHOW_D, 0, -1, "SMART SENSOR")                                      \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_D_SENSITIVITY, S_D, HT_SHOW_D, \
      0, TX_HP_SENS, NULL)                                                   \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_C_FAILSAFE, S_C, HT_SHOW_C, 0, \
      TX_HP_FAILSAFE, NULL)                                                  \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_C_FS_POSITION, S_C, HT_SHOW_C, \
      0, TX_HP_FS_POS, NULL)                                                 \
    X(HITEC_PANEL_PROTECT, 1, K_SHOW, HITEC_P_C_OVERLOAD, S_C, HT_SHOW_C, 0, \
      TX_HP_OVERLOAD, NULL)                                                  \
                                                                             \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_MODEL, S_D, HT_SHOW_D, 0,       \
      TX_HP_MODEL, NULL)                                                     \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_ID, S_D, HT_SHOW_D, 0, -1, "ID") \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_POSITION, S_D, HT_SHOW_D, 0,    \
      TX_HP_POSITION, NULL)                                                  \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_R04, S_D, HT_SHOW_D, 0, -1,     \
      "REG 04")                                                              \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_R22, S_D, HT_SHOW_D, 0, -1,     \
      "REG 22")                                                              \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_R8A, S_D, HT_SHOW_D, 0, -1,     \
      "REG 8A")                                                              \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_R8C, S_D, HT_SHOW_D, 0, -1,     \
      "REG 8C")                                                              \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_RD4, S_D, HT_SHOW_D, 0, -1,     \
      "REG D4")                                                              \
    X(HITEC_PANEL_INFO, 0, K_SHOW, HITEC_P_D_RD6, S_D, HT_SHOW_D, 0, -1,     \
      "REG D6")                                                              \
    X(HITEC_PANEL_INFO, 1, K_SHOW, W_NAME, S_C, HT_SHOW_C, 0, TX_HP_MODEL,   \
      NULL)                                                                  \
    X(HITEC_PANEL_INFO, 1, K_SHOW, HITEC_P_C_VERSION, S_C, HT_SHOW_C, 0,     \
      TX_HP_VERSION, NULL)                                                   \
    X(HITEC_PANEL_INFO, 1, K_SHOW, HITEC_P_C_DATE, S_C, HT_SHOW_C, 0,        \
      TX_HP_DATE, NULL)                                                      \
    X(HITEC_PANEL_INFO, 1, K_SHOW, HITEC_P_C_SERIAL, S_C, HT_SHOW_C, 0,      \
      TX_HP_SERIAL, NULL)                                                    \
    X(HITEC_PANEL_INFO, 1, K_SHOW, HITEC_P_C_CHECKSUM, S_C, HT_SHOW_C, 0,    \
      TX_HP_CHECKSUM, NULL)

/** @brief A row of hpn_rows. */
#define HP_ROW(tab, wide, kind, what, series, show, w, label, name)          \
    { (tab), (kind), (what), (series), (label), (name) },
/** @brief A row of the frame's table: PORT the head, FIND STOP a pair of
 *         keys, every other action a button, every parameter a value. */
#define HP_FROW(tab, wide, kind, what, series, show, w, label, name)         \
    { (tab), (wide),                                                         \
      (kind) != K_ACT              ? SPROG_K_VALUE                           \
      : (int)(what) == (int)A_PORT ? SPROG_K_HEAD                            \
      : (int)(what) == (int)A_FIND ? SPROG_K_PAIR                            \
                                   : SPROG_K_ACT,                            \
      (what), (show), (w), (label), (name) },

/** @brief The rows of every tab and both series; hitec_panel_int.h. */
const hpn_row_t hpn_rows[] = { HP_ROWS(HP_ROW) };
_Static_assert(sizeof(hpn_rows) / sizeof(hpn_rows[0]) == HP_ROW_COUNT,
               "HP_ROW_COUNT in hitec_panel_int.h counts hpn_rows");

/** @brief The same rows as the frame lays them out, index for index. */
static const sprog_row_t k_frows[] = { HP_ROWS(HP_FROW) };

/** @brief The tabs' labels. */
static const ui_text_id_t k_tabs[HITEC_PANEL_PAGES] = {
    TX_KP_TAB_SERVO, TX_HP_TAB_MOTION, TX_HP_TAB_TRAVEL, TX_HP_TAB_PROTECT,
    TX_HP_TAB_INFO,
};

/** @brief The frame's data: five tabs, always shown; the rows. */
static const sprog_def_t k_def = {
    .tabs = k_tabs,
    .tab_show = NULL,
    .tab_count = HITEC_PANEL_PAGES,
    .rows = k_frows,
    .row_count = HP_ROW_COUNT,
    .hint = TX_SPROG_EDIT_NOTE,
};

/* The state, its type in hitec_panel_int.h. */
hpn_state_t hitec_panel_h;

/* ------------------------------------------------------------------- rows */

uint8_t hpn_mask(void)
{
    const bool d = hitec_prog_series(&hp.prog) == (uint8_t)HITEC_D;
    unsigned bits = d ? HT_SHOW_D : HT_SHOW_C;

    bits |= hp.prog.owned ? HT_SHOW_HELD : HT_SHOW_FREE;
    if (d) {
        bits |= hitec_prog_move_phase(&hp.prog) != HITEC_MOVE_OFF
                    ? HT_SHOW_MOVE : HT_SHOW_STILL;
    }
    return (uint8_t)bits;
}

bool hpn_row_shown(const hpn_row_t *r)
{
    const sprog_row_t *fr = &k_frows[r - hpn_rows];

    if (fr->show != 0u && (fr->show & hpn_mask()) == 0u) {
        return false;
    }
    if (r->kind == K_SHOW && r->what < (uint8_t)HITEC_P_COUNT) {
        /* A parameter the layout lacks keeps its place, empty. */
        return hitec_param(hitec_prog_layout(&hp.prog), r->what) != NULL;
    }
    return true;
}

void hpn_place(void)
{
    sprog_view_begin(&hp.f, hpn_mask());
    for (int i = 0; i < hp.f.cells; ++i) {
        const hpn_row_t *r = &hpn_rows[hp.f.cell_row[i]];

        if (!hpn_row_shown(r)) {
            hp.f.view.cell[i].kind = SPROG_K_GAP;
        } else if (hpn_stepped(r)) {
            hp.f.view.cell[i].kind = SPROG_K_STEP;
        }
    }
}

void hpn_mask_set(void)
{
    hp.f.view.mask = hpn_mask();
}

const hpn_row_t *hpn_row_at(int page, int row, int col)
{
    hpn_mask_set();
    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        const hpn_row_t *r = &hpn_rows[i];
        gfx_rect_t rr;

        if (r->page != page || !hpn_row_shown(r)) {
            continue;
        }
        rr = sprog_row_rect(&hp.f, i);
        if (rr.w > 0 && (rr.y - hp.area.y - HP_ROW0) / HP_PITCH == row
            && (rr.x > hp.area.x + 10) == (col != 0)) {
            return r;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------- geometry */

int hitec_panel_text_rows(const char *s)
{
    return sprog_text_rows(&hp.f, s);
}

gfx_rect_t hitec_panel_tab_rect(int page)
{
    return hp.f.tabs.rect[page >= 0 && page < HITEC_PANEL_PAGES ? page : 0];
}

gfx_rect_t hitec_panel_value_rect(int page, int row, int col)
{
    const hpn_row_t *r = hpn_row_at(page, row, col);

    if (r == NULL) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    /* Between its -/+ keys while stepped: the view's kind. */
    hpn_view();
    return sprog_value_rect(&hp.f, (int)(r - hpn_rows));
}

gfx_rect_t hitec_panel_choice_rect(int i)
{
    return sprog_list_rect(&hp.f, i);
}

gfx_rect_t hitec_panel_choice_cancel_rect(void)
{
    return sprog_list_cancel_rect(&hp.f);
}

gfx_rect_t hitec_panel_apply_rect(void)
{
    return sprog_apply_rect(&hp.f);
}

gfx_rect_t hitec_panel_cancel_rect(void)
{
    return sprog_page_cancel_rect(&hp.f);
}

/* ---------------------------------------------------------- for the tests */

hitec_prog_t *hitec_panel_model(void)
{
    return &hp.prog;
}

void hitec_panel_settings_changed(void)
{
    hpn_options_load();
}

ui_keypad_t *hitec_panel_keypad(void)
{
    return &hp.f.kp;
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
    return hp.f.kp.open ? 1 : hp.f.tk.open ? 4 : hp.f.list.open ? 2
         : hp.f.page.open ? 3 : 0;
}

ui_textkey_t *hitec_panel_textkey(void)
{
    return &hp.f.tk;
}

int hitec_panel_page(void)
{
    return hp.f.tabs.selected;
}

const char *hitec_panel_choice_label(int i)
{
    return hp.f.list.open && i >= 0 && i < hp.f.list.count ? hp.f.list.label[i] : NULL;
}

const char *hitec_panel_apply_label(void)
{
    return hp.f.page.open ? hp.f.page.apply : NULL;
}

const char *hitec_panel_cancel_label(void)
{
    return hp.f.page.open ? hp.f.page.cancel : NULL;
}

const char *hitec_panel_choice_title(void)
{
    return hp.f.list.open ? hp.f.list.title : NULL;
}

const char *hitec_panel_page_line(int i)
{
    if (!hp.f.page.open || i < 0 || i > hp.f.page.count) {
        return NULL;
    }
    return i == 0 ? hp.f.page.title : hp.f.page.line[i - 1];
}

/* --------------------------------------------------------------- the caller */

void hitec_panel_gate(hitec_prog_gate_t *g)
{
    hp.gate = g;
    hitec_prog_gate_set(&hp.prog, g);
}

void hitec_panel_init(gfx_rect_t area)
{
    hitec_prog_gate_t *const gate = hp.gate;

    memset(&hp, 0, sizeof(hp));
    hp.gate = gate;
    hitec_prog_init(&hp.prog);
    hitec_prog_gate_set(&hp.prog, hp.gate);
    hp.area = area;
    sprog_init(&hp.f, &k_def, area);
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
    hitec_prog_move_stop(&hp.prog);
    options_fresh();
    /* Writing is switched off at every entry of the screen. */
    (void)hitec_prog_write_enable(&hp.prog, false);
    ++hp.rev;
}

void hitec_panel_open(void)
{
    if (!hp.open) {
        hp.open = true;
        hp.f.open = true;
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

bool hitec_panel_files_shown(void)
{
    /* FILES stands on the SERVO tab always; it is offered while this
     * panel holds the port, where SERIES gives way to AFTER STOP. */
    return hp.prog.owned;
}

uint8_t hitec_panel_pin(void)
{
    const uint8_t pin = hitec_prog_pin(&hp.prog);

    return pin == HITEC_PROG_NO_PIN ? (uint8_t)PROG_REG_NO_PIN : pin;
}

uint32_t hitec_panel_revision(void)
{
    return hp.rev + sprog_revision(&hp.f);
}

uint32_t hitec_panel_hold_revision(void)
{
    return sprog_hold_revision(&hp.f);
}

/**
 * @brief Everything of the model a render shows, as one number.  The PD
 *        mini's reading counts as the state line writes it: 10mV and 1mA
 *        while the port is held in mode, nothing in any other state, and
 *        as MOVE SERVO's live line writes the current: 10mA while the loop
 *        runs.  A reading the page does not show repaints nothing.
 * @return the number
 */
static uint32_t signature(void)
{
    const hitec_prog_t *p = &hp.prog;
    unsigned done = 0;
    unsigned total = 0;
    char text[HITEC_PANEL_LINE];
    uint32_t line = 2166136261u;

    hpn_state_line(text, sizeof(text));
    for (const char *t = text; *t != '\0'; ++t) {
        line = (line ^ (uint8_t)*t) * 16777619u;
    }
    const uint32_t part[] = {
        (uint32_t)hitec_prog_state(p), p->msg.seq, p->f.rd.flags,
        p->f.rd.port_no, p->f.rd.held, p->f.rd.ours, p->f.rd.have_line,
        p->f.rd.pin, p->f.rd.line, p->f.target, p->f.port_pin_known,
        p->f.port_pin[0], p->f.port_pin[1], p->f.port_pin[2],
        p->f.port_pin[3], p->f.stopped, p->f.supply_read,
        line, p->f.rx_mark, p->have_servo,
        p->series, hitec_prog_mv(p), hitec_prog_ma(p),
        hitec_prog_mv_capped(p), hitec_prog_in_mode(p),
        hitec_prog_progress(p, &done, &total) ? done : 0u,
        hitec_prog_retry_code(p), hitec_prog_write_on(p),
        hitec_prog_changes(p), hitec_prog_record(p),
        (uint32_t)hitec_prog_record_verdict(p), p->ses.step_index,
        p->must_read, p->f.card, p->ses.done, p->f.card_state, p->bk_saved,
        p->bk_no_file, p->have_bk, p->files_seq, hitec_prog_card_busy(p),
        p->load_pend, p->mv.phase, p->mv.off, p->mv.follow, p->mv.pos,
        p->mv.has_pos, p->f.has_angle ? p->f.angle : 0xFFFFu,
        (uint32_t)hitec_prog_find_side(p), p->mv.find_go, p->mv.fs.p,
        /* MOVE SERVO's live line writes the current in 10mA. */
        hitec_prog_state(p) == HITEC_PROG_MOVING && p->f.supply_read
            ? (p->f.out_ma + 5u) / 10u : 0u,
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
        /* A press on a row or a -/+ key taps nothing on its release;
         * CONNECT's and WRITE's presses keep their holds, for the release
         * to end, and a tab's press its tab. */
        if (hp.f.have && hp.f.on != SPROG_ON_HOLD
            && hp.f.on != SPROG_ON_TAB) {
            hp.f.on = SPROG_ON_ABSORB;
        }
        hp.state_seen = st;
    }
    hpn_move_follow();
    hpn_supply_sync();
    hpn_gate_follow();
    sig = signature();
    if (sig != hp.sig) {
        hp.sig = sig;
        ++hp.rev;
    }
}

void hitec_panel_feed(const hitec_prog_feed_t *f)
{
    hitec_prog_feed_t g = *f;
    float deg = 0.0f;

    /* The AS5600 on the horn as the SERVO screen shows it, for MOVE
     * SERVO's judge, where the feed carries none. */
    if (!g.has_angle && servo_screen_log_angle(&deg)) {
        const long counts = lroundf(deg * 4096.0f / 360.0f) % 4096L;

        g.has_angle = true;
        g.angle = (uint16_t)(counts < 0 ? counts + 4096L : counts);
    }
    hitec_prog_feed(&hp.prog, &g);
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

size_t hitec_panel_file_take(char *out, size_t n)
{
    return hitec_prog_file_take(&hp.prog, out, n);
}

void hitec_panel_file_done(bool ok)
{
    hitec_prog_file_done(&hp.prog, ok);
    hpn_moved();
}

bool hitec_panel_card_take(hitec_card_req_t *out)
{
    return hitec_prog_card_take(&hp.prog, out);
}

void hitec_panel_card_done(const hitec_card_done_t *d)
{
    hitec_prog_card_done(&hp.prog, d);
    hpn_file_answer();
    hpn_moved();
}

bool hitec_panel_card_look(void)
{
    const bool look = hp.card_look;

    hp.card_look = false;
    return look;
}

bool hitec_panel_file_load(const char *text, size_t len)
{
    const bool ok = hitec_prog_file_load(&hp.prog, text, len);

    hpn_moved();
    return ok;
}

/**
 * @brief Where hold_revision shows: CONNECT or WRITE n VALUES while its
 *        hold runs or flashes, else the page's hold button.
 * @return the rectangle, body pixels
 */
static gfx_rect_t hitec_panel_hold_rect(void)
{
    hpn_mask_set();
    return sprog_hold_rect(&hp.f);
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
    .hold_rect     = hitec_panel_hold_rect,
    .leave         = hitec_panel_leave,
    .cancel        = hitec_panel_cancel,
    .pin           = hitec_panel_pin,
    .safe_at       = hitec_panel_safe_at,
};
