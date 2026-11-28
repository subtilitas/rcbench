/**
 * @file kst_panel.c
 * @brief The overlay of the programmer for KST and Chaservo servos: its
 *        state, rows, shared helpers, geometry and the caller's side.
 *
 * The overlay of the programmer for KST and Chaservo servos.  See
 * kst_panel.h.
 *
 * This file is the state, the rows, the helpers the other files share,
 * the geometry, and the caller's side: the feed, the hooks and the page
 * set.  The texts are kst_panel_text.c, the warning pages
 * kst_panel_warn.c, the editors kst_panel_edit.c, the presses
 * kst_panel_touch.c, the limit run's sessions kst_panel_run.c and the
 * drawing kst_panel_draw.c, with kst_panel_int.h between them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <stdio.h>
#include <string.h>

#include "kst_port.h"
#include "kst_reg.h"
#include "link_pages.h"
#include "prog_choice.h"
#include "ui_board.h"
#include "settings.h"
#include "ui_card_text.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: k names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/**
 * @brief The rows, one table for both views of it: kpn_rows for the overlay's
 * files (page, row and column with every row shown, the overlay's kind),
 * k_srows for the frame (tab, the frame's kind, show bits, the control's
 * width).  An index is the same row in both.
 *
 * ROW(page, row, col, wide, kind, frame kind, what, show, w, label, name)
 *
 * The SERVO tab: the head (port and supply, CONNECT), READ ALL with the
 * operator's TYPE beside it (as the Hitec programmer's SERIES), FILES and
 * MORE, RESTORE and WRITE n VALUES, which says what it writes: no label
 * beside it, 220px, a 2s hold.  LOCKED's 20K SEL is a drive setting, not
 * the series: the type is SERVO's TYPE row.
 */
#define KPN_ROWS(ROW)                                                      \
    ROW(PG_SERVO, 0, 0, 1, K_ACT, SPROG_K_HEAD, A_PORT, 0, 0, -1, "PORT")  \
    ROW(PG_SERVO, 1, 0, 0, K_ACT, SPROG_K_ACT, A_READ, 0, 0, -1, "SERVO")  \
    ROW(PG_SERVO, 1, 1, 0, K_ACT, SPROG_K_ACT, A_TYPE, 0, 0, TX_KP_TYPE,   \
        NULL)                                                              \
    ROW(PG_SERVO, 2, 0, 0, K_ACT, SPROG_K_ACT, A_FILES, 0, 0, TX_KP_FILES, \
        NULL)                                                              \
    ROW(PG_SERVO, 2, 1, 0, K_ACT, SPROG_K_ACT, A_MORE, 0, 0, TX_KP_MORE,   \
        NULL)                                                              \
    ROW(PG_SERVO, 3, 0, 0, K_ACT, SPROG_K_ACT, A_RESTORE, 0, 0,            \
        TX_KP_RESTORE, NULL)                                               \
    ROW(PG_SERVO, 3, 1, 0, K_ACT, SPROG_K_ACT, A_WRITE, 0, WRITE_W, -1, "")\
                                                                           \
    ROW(PG_TUNE, 0, 0, 1, K_STEP, SPROG_K_STEP, KST_F_DEAD_BAND,           \
        KST_SHOW_EXPERT, 0, -1, "DEAD BAND")                               \
    ROW(PG_TUNE, 1, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_DUTY, 0, 0, -1,    \
        "MAX. DUTY")                                                       \
    ROW(PG_TUNE, 1, 1, 0, K_FIELD, SPROG_K_VALUE, KST_F_BOOST,             \
        KST_SHOW_EXPERT, 0, -1, "BOOST")                                   \
    ROW(PG_TUNE, 2, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_STRETCH,           \
        KST_SHOW_EXPERT, 0, -1, "STRETCH")                                 \
    ROW(PG_TUNE, 2, 1, 0, K_FIELD, SPROG_K_VALUE, KST_F_SOFT_START, 0, 0,  \
        -1, "SOFT START")                                                  \
    ROW(PG_TUNE, 3, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_SPD_SEL, 0, 0,     \
        TX_KP_SPEED_LIMIT, NULL)                                           \
    ROW(PG_TUNE, 3, 1, 0, K_FIELD, SPROG_K_VALUE, KST_F_SPD, 0, 0,         \
        TX_KP_SPEED, NULL)                                                 \
                                                                           \
    ROW(PG_PULSE, 0, 0, 1, K_STEP, SPROG_K_STEP, KST_F_NEUTRAL, 0, 0, -1,  \
        "NEUTRAL")                                                         \
    ROW(PG_PULSE, 1, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_PULSE_LOWER, 0,   \
        0, -1, "PULSE LOWER")                                              \
    ROW(PG_PULSE, 1, 1, 0, K_FIELD, SPROG_K_VALUE, KST_F_PULSE_UPPER, 0,   \
        0, -1, "PULSE UPPER")                                              \
    ROW(PG_PULSE, 2, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_LEFT_ANGLE, 0, 0, \
        -1, "LEFT ANGLE")                                                  \
    ROW(PG_PULSE, 2, 1, 0, K_FIELD, SPROG_K_VALUE, KST_F_RIGHT_ANGLE, 0,   \
        0, -1, "RIGHT ANGLE")                                              \
    ROW(PG_PULSE, 3, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_REVERSION, 0, 0,  \
        -1, "REVERSION")                                                   \
                                                                           \
    ROW(PG_FAILSAFE, 0, 0, 1, K_FIELD, SPROG_K_VALUE, KST_F_ALLOW_UNCONT,  \
        0, 0, TX_KP_FS_MOVE, NULL)                                         \
    ROW(PG_FAILSAFE, 1, 0, 0, K_FIELD, SPROG_K_VALUE, KST_F_UNCONT_POS, 0, \
        0, -1, "POSITION")                                                 \
    ROW(PG_FAILSAFE, 1, 1, 0, K_FIELD, SPROG_K_VALUE, KST_F_UNCONT_TIME,   \
        0, 0, TX_KP_FS_AFTER, NULL)                                        \
                                                                           \
    ROW(PG_LOCKED, 0, 0, 0, K_LOCKED, SPROG_K_SHOW, KST_F_20K_SEL,         \
        KST_SHOW_EXPERT, 0, -1, "20K SEL")                                 \
    ROW(PG_LOCKED, 0, 1, 0, K_LOCKED, SPROG_K_SHOW, KST_F_LOCK,            \
        KST_SHOW_EXPERT, 0, -1, "LOCK")                                    \
    ROW(PG_LOCKED, 1, 0, 0, K_LOCKED, SPROG_K_SHOW, KST_F_GYRO,            \
        KST_SHOW_EXPERT, 0, -1, "MODE")                                    \
    ROW(PG_LOCKED, 1, 1, 0, K_LOCKED, SPROG_K_SHOW, KST_F_PROT1_EN,        \
        KST_SHOW_EXPERT, 0, -1, "PROT1 EN")                                \
    ROW(PG_LOCKED, 2, 0, 0, K_LOCKED, SPROG_K_SHOW, KST_F_PROT1_CANCLE,    \
        KST_SHOW_EXPERT, 0, -1, "PROT1 CANCLE")                            \
    ROW(PG_LOCKED, 2, 1, 0, K_LOCKED, SPROG_K_SHOW, KST_F_PROT1_TIME,      \
        KST_SHOW_EXPERT, 0, -1, "PROT1 TIME")                              \
    ROW(PG_LOCKED, 3, 0, 0, K_LOCKED, SPROG_K_SHOW, KST_F_PROT1_PWM,       \
        KST_SHOW_EXPERT, 0, -1, "PROT1 PWM")                               \
    ROW(PG_LOCKED, 3, 1, 0, K_LOCKED, SPROG_K_SHOW, KST_F_PROT_POT_ERR,    \
        KST_SHOW_EXPERT, 0, -1, "POT ERR")                                 \
    ROW(PG_LOCKED, 4, 0, 0, K_LOCKED, SPROG_K_SHOW, KST_F_POT_SAM_TIMES,   \
        KST_SHOW_EXPERT, 0, -1, "POT SAMPLES")                             \
    ROW(PG_LOCKED, 4, 1, 0, K_LOCKED, SPROG_K_SHOW, KST_F_PROT_PWM_ERR,    \
        KST_SHOW_EXPERT, 0, -1, "PWM ERR")

/** @brief A row of KPN_ROWS as the overlay's files see it. */
#define ROW_OF_PANEL(pg, rw, cl, wd, kk, sk, wh, sh, w, lb, nm) \
    { pg, rw, cl, wd, kk, wh, lb, nm },
/** @brief A row of KPN_ROWS as the frame sees it. */
#define ROW_OF_FRAME(pg, rw, cl, wd, kk, sk, wh, sh, w, lb, nm) \
    { pg, wd, sk, wh, sh, w, lb, nm },

const row_t kpn_rows[] = { KPN_ROWS(ROW_OF_PANEL) };
_Static_assert(sizeof(kpn_rows) / sizeof(kpn_rows[0]) == ROW_COUNT,
               "ROW_COUNT in kst_panel_int.h counts kpn_rows");

/** @brief The rows as the frame lays them out, by kpn_rows' index. */
static const sprog_row_t k_srows[] = { KPN_ROWS(ROW_OF_FRAME) };

/** @brief The tab titles, in tab order. */
static const ui_text_id_t k_pages[PG_COUNT] = {
    TX_KP_TAB_SERVO, TX_KP_TAB_TUNE, TX_KP_TAB_PULSE, TX_KP_TAB_FAILSAFE,
    TX_KP_TAB_LOCKED,
};

/** @brief Each tab's show bits: LOCKED with KST EXPERT MODE ON. */
static const uint8_t k_tab_show[PG_COUNT] = { 0, 0, 0, 0, KST_SHOW_EXPERT };

/** @brief The overlay's table for the frame. */
static const sprog_def_t k_def = {
    k_pages, k_tab_show, PG_COUNT, k_srows, ROW_COUNT, TX_SPROG_EDIT_NOTE,
};

/* The state, its type in kst_panel_int.h. */
kst_panel_state_t kst_panel_k;

/**
 * @brief What the panel's slot holds, as the feed last said: no card, a card not
 * formatted as FAT32 or exFAT, or one that cannot be read.
 */
static card_state_t card_state(void)
{
    return k.prog.fed ? (card_state_t)k.prog.f.card_state : CARD_STATE_NONE;
}

/* The words of @p at for the slot as it is. */
ui_text_id_t kpn_card_text_id(ui_card_at_t at)
{
    return ui_card_text_id(card_state(), at);
}

const char *kpn_card_tr(ui_card_at_t at)
{
    return ui_tr(kpn_card_text_id(at));
}

/* ------------------------------------------------------------------ helpers */

unsigned kpn_ones(uint64_t v)
{
    unsigned n = 0;

    for (; v != 0u; v &= v - 1u) {
        ++n;
    }
    return n;
}

/* The programming port the overlay works on: the SERVO screen's target,
 * and the first port while that is the servo outputs. */
uint8_t kpn_port_no(void)
{
    const uint8_t t = k.prog.f.target;

    return t != 0u ? (uint8_t)(t - 1u) : 0u;
}

/* The SERVO screen's tester drives a programming port. */
bool kpn_on_port(void)
{
    return k.prog.f.target != 0u;
}

bool kpn_flag(uint16_t f)
{
    return (k.prog.f.rd.flags & f) != 0u;
}

bool kpn_programming(void)
{
    return kst_prog_state(&k.prog) == KST_PROG_PROGRAMMING;
}

const char *kpn_head_line(int i)
{
    static char line[2][KST_PANEL_LINE];

    if (!kpn_on_port()) {
        return i == 0 ? TR(HP_PORT_NONE) : NULL;
    }
    if (i == 0) {
        /* The GPIO the port's feed names, else the carrier's. */
        const outbind_roles_t *roles = ui_board_roles();
        unsigned pin = kst_panel_pin();

        if (pin == PROG_REG_NO_PIN && prog_choice_offered() && roles != NULL) {
            pin = roles->prog[kpn_port_no()];
        }
        if (pin == PROG_REG_NO_PIN) {
            snprintf(line[0], sizeof(line[0]), TR(SV_TARGET_PN),
                     (unsigned)kpn_port_no() + 1u);
        } else {
            snprintf(line[0], sizeof(line[0]), TR(SV_TARGET_PN_PIN),
                     (unsigned)kpn_port_no() + 1u, pin);
        }
        return line[0];
    }
    if (ui_board_prog_5v_pin() >= 0) {
        return TR(HP_SUPPLY_5V_FIXED);
    }
    if (kpn_flag(LINK_KS_F_RAIL)) {
        /* What CONNECT asks of the PD mini, and what it holds after. */
        const unsigned mv = kst_prog_supply_mv(&k.prog);

        snprintf(line[1], sizeof(line[1]), "PD MINI  %u.%uV %umA", mv / 1000u,
                 mv % 1000u / 100u, (unsigned)kst_prog_supply_ma(&k.prog));
        return line[1];
    }
    return TR(HP_SUPPLY_AS_IS);
}

const char *kst_panel_head_line(int i)
{
    return i == 0 || i == 1 ? kpn_head_line(i) : NULL;
}

/* A row's label: its text, or for PORT the head's first line. */
const char *kpn_row_label(const row_t *r)
{
    if (r->kind == K_ACT && r->what == A_PORT) {
        return kpn_head_line(0);
    }
    return r->label >= 0 ? ui_tr((ui_text_id_t)r->label) : r->name;
}

/* The name a field has on its row; NULL for one without a row. */
const char *kpn_field_name(unsigned id)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (kpn_rows[i].kind != K_ACT && kpn_rows[i].what == id) {
            return kpn_row_label(&kpn_rows[i]);
        }
    }
    return NULL;
}

bool kpn_in_set(kst_field_set_t set, unsigned id)
{
    return (set & KST_FIELD_BIT(id)) != 0u;
}

const char *kpn_unit_of(kst_field_id_t id)
{
    switch ((kst_unit_t)kst_field(id)->unit) {
    case KST_UNIT_TENTH_US:
        return "us";
    case KST_UNIT_PERCENT:
    case KST_UNIT_TENTH_PERCENT:
        return "%";
    case KST_UNIT_TENTH_S:
        return "s";
    default:
        return "";
    }
}

/* Digits after the point a field's displayed value has. */
int kpn_decimals_of(kst_field_id_t id)
{
    switch ((kst_unit_t)kst_field(id)->unit) {
    case KST_UNIT_TENTH_US:
    case KST_UNIT_TENTH_PERCENT:
    case KST_UNIT_TENTH_S:
        return 1;
    default:
        return 0;
    }
}

/* A field's value as its row shows it; without @p unit where a line gives
 * the unit once behind two values. */
void kpn_fmt_value(kst_field_id_t id, uint16_t raw, char *out, size_t n,
                      bool unit)
{
    int32_t v = 0;

    if (id == KST_F_GYRO) {
        snprintf(out, n, "%s", raw != 0u ? "GYRO" : "SERVO");
        return;
    }
    if (id == KST_F_NODE_ADDR) {
        /* The pairing value as the LOCKED tab gives it: two hex digits. */
        snprintf(out, n, "%02X", (unsigned)raw);
        return;
    }
    if (!kst_field_display(id, raw, &v)) {
        snprintf(out, n, "%u", (unsigned)raw);
        return;
    }
    switch ((kst_unit_t)kst_field(id)->unit) {
    case KST_UNIT_BOOL:
        snprintf(out, n, "%s", v != 0 ? TR(ON) : TR(OFF));
        break;
    case KST_UNIT_FACTOR:
        snprintf(out, n, "x%d", (int)v);
        break;
    case KST_UNIT_TENTH_US:
    case KST_UNIT_TENTH_PERCENT:
    case KST_UNIT_TENTH_S:
        snprintf(out, n, "%d.%d%s", (int)(v / 10), (int)(v % 10),
                 unit ? kpn_unit_of(id) : "");
        break;
    case KST_UNIT_PERCENT:
        snprintf(out, n, "%d%s", (int)v, unit ? "%" : "");
        break;
    default:
        snprintf(out, n, "%d", (int)v);
        break;
    }
}

void kpn_fmt_field(kst_field_id_t id, uint16_t raw, char *out, size_t n)
{
    kpn_fmt_value(id, raw, out, n, true);
}

bool kpn_changed(kst_field_id_t id)
{
    return kst_prog_have_servo(&k.prog)
        && kst_field_get(kst_prog_draft(&k.prog), id)
           != kst_field_get(kst_prog_servo(&k.prog), id);
}

/* --------------------------------------------------------------- wrapping */

int kst_panel_text_rows(const char *s)
{
    return sprog_text_rows(&k.f, s);
}

/* ----------------------------------------------------------------- geometry */

/** @brief Row @p r's index, the frame's row. */
static int row_no(const row_t *r)
{
    return (int)(r - kpn_rows);
}

/** @brief Row @p r's control, between a step's keys.
 * @param r the row
 * @return the rectangle */
static gfx_rect_t kpn_value_rect(const row_t *r)
{
    return sprog_value_rect(&k.f, row_no(r));
}

/** @brief Row @p r's + key (@p up) or - key.
 * @param r the row
 * @param up true for +
 * @return the rectangle */
static gfx_rect_t kpn_step_rect(const row_t *r, bool up)
{
    return sprog_step_rect(&k.f, row_no(r), up);
}

const row_t *kpn_row_at(int page, int row, int col)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (kpn_rows[i].page == page && kpn_rows[i].row == row
            && kpn_rows[i].col == col) {
            return &kpn_rows[i];
        }
    }
    return NULL;
}

/**
 * @brief The row at (@p row, @p col) of page @p page while it shows.
 * @param page the page
 * @param row the row on the page, as kpn_rows has it
 * @param col the column in the row
 * @return the row, or NULL for none and for one hidden
 */
static const row_t *shown_at(int page, int row, int col)
{
    const row_t *r = kpn_row_at(page, row, col);

    return r != NULL && kpn_row_shown(r) ? r : NULL;
}

gfx_rect_t kst_panel_tab_rect(int page)
{
    return sprog_tab_rect(&k.f, (unsigned)page < PG_COUNT ? page : PG_SERVO);
}

gfx_rect_t kst_panel_value_rect(int page, int row, int col)
{
    const row_t *r = shown_at(page, row, col);

    return r != NULL ? kpn_value_rect(r) : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t kst_panel_step_rect(int page, int row, bool up)
{
    const row_t *r = shown_at(page, row, 0);

    return r != NULL ? kpn_step_rect(r, up) : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t kst_panel_choice_rect(int i)
{
    return sprog_list_rect(&k.f, i);
}

gfx_rect_t kst_panel_choice_cancel_rect(void)
{
    return sprog_list_cancel_rect(&k.f);
}

gfx_rect_t kst_panel_apply_rect(void)
{
    return sprog_apply_rect(&k.f);
}

gfx_rect_t kst_panel_cancel_rect(void)
{
    return sprog_page_cancel_rect(&k.f);
}

ui_keypad_t *kst_panel_keypad(void)
{
    return &k.f.kp;
}

/* A file name as a line shows it: whole up to KST_PANEL_NAME_TYPED
 * characters, else its start and "..". */
const char *kpn_shown(const char *name)
{
    static char s[KST_PANEL_NAME_TYPED + 1];

    if (strlen(name) <= (size_t)KST_PANEL_NAME_TYPED) {
        return name;
    }
    snprintf(s, sizeof(s), "%.*s..", KST_PANEL_NAME_TYPED - 2, name);
    return s;
}

ui_textkey_t *kst_panel_textkey(void)
{
    return &k.f.tk;
}

const char *kst_panel_warning_line(int i)
{
    return k.f.page.open && i >= 0 && i < k.f.page.count ? k.f.page.line[i]
                                                         : NULL;
}

const char *kst_panel_apply_label(void)
{
    return k.f.page.open ? k.f.page.apply : NULL;
}

const char *kst_panel_cancel_label(void)
{
    return k.f.page.open ? k.f.page.cancel : NULL;
}

int kst_panel_warning_level(int i)
{
    if (!k.f.page.open || i < 0 || i >= k.f.page.count) {
        return (int)KST_PROG_INFO;
    }
    return k.f.page.level[i] == SPROG_LV_DANGER ? (int)KST_PROG_DANGER
         : k.f.page.level[i] == SPROG_LV_WARN   ? (int)KST_PROG_WARN
                                                : (int)KST_PROG_INFO;
}

const char *kst_panel_label(int page, int row, int col)
{
    const row_t *r = shown_at(page, row, col);

    return r != NULL ? kpn_row_label(r) : NULL;
}

const char *kst_panel_choice_title(void)
{
    return k.f.list.open ? k.f.list.title : NULL;
}

const char *kst_panel_choice_label(int i)
{
    return k.f.list.open && i >= 0 && i < k.f.list.count ? k.f.list.label[i]
                                                         : NULL;
}

int kst_panel_page(void)
{
    return kpn_page();
}

int kpn_page(void)
{
    return sprog_tab(&k.f);
}

/* ------------------------------------------------------------ expert mode */

bool kpn_expert(void)
{
    return k.expert;
}

uint8_t kpn_mask(void)
{
    return k.expert ? KST_SHOW_EXPERT : 0u;
}

bool kpn_row_shown(const row_t *r)
{
    const uint8_t show = k_srows[row_no(r)].show;

    return show == 0u || (show & kpn_mask()) != 0u;
}

bool kpn_field_hidden(unsigned id)
{
    if (k.expert) {
        return false;
    }
    if (id == KST_F_NODE_ADDR) {
        return true;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (kpn_rows[i].kind != K_ACT && kpn_rows[i].what == id) {
            return !kpn_row_shown(&kpn_rows[i]);
        }
    }
    return false;
}

void kpn_mode_sync(void)
{
    const bool on = settings_get_bool(SET_KST_EXPERT);

    if (on == k.expert) {
        return;
    }
    /* An editor, a page or a press -- a held step key repeats -- may be
     * about a row that goes. */
    kpn_close_editors();
    k.expert = on;
    /* The frame builds its strip again, the tab shown kept where it is
     * still there and SERVO otherwise; its press ends and its holds go. */
    sprog_view_begin(&k.f, kpn_mask());
    ++k.rev;
}

int kst_panel_editor(void)
{
    return k.f.kp.open ? 1 : k.f.tk.open ? 4 : k.f.list.open ? 2
         : k.f.page.open ? 3 : 0;
}

/* --------------------------------------------------------------- the caller */

void kst_panel_gate(kst_prog_gate_t *g)
{
    k.gate = g;
    kst_prog_gate_set(&k.prog, g);
}

/**
 * @brief The settings that reach the port: what the tester drives and whether the
 *        port renders its pulse.  Held while nothing but the programmer reaches
 *        the port.
 */
static bool holds_setting(setting_id_t id)
{
    return (id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM)
        && kst_prog_holds_port(&k.prog);
}

void kst_panel_init(gfx_rect_t area)
{
    kst_prog_gate_t *const gate = k.gate;

    memset(&k, 0, sizeof(k));
    k.gate = gate;
    kst_prog_init(&k.prog);
    settings_set_hold(holds_setting);
    kst_prog_gate_set(&k.prog, k.gate);
    k.area = area;
    k.expert = settings_get_bool(SET_KST_EXPERT);
    sprog_init(&k.f, &k_def, area);
    sprog_view_begin(&k.f, kpn_mask());
}

/**
 * @brief The options are read where the settings are there: at a screen entry and
 *        at an opening, never at kst_panel_init(), which runs before the settings
 *        are loaded.  A session keeps the values it began with, except one found
 *        running by a panel that has read none yet.
 */
static void options_fresh(void)
{
    if (!k.loaded || !kst_panel_holds_port()) {
        kpn_options_load();
        k.loaded = true;
    }
}

void kst_panel_screen_entry(void)
{
    kst_prog_screen_entry(&k.prog);
    options_fresh();
    kpn_mode_sync();
    ++k.rev;
}

void kst_panel_open(void)
{
    kpn_mode_sync();
    if (!k.f.open) {
        k.f.open = true;
        k.f.note_page = 0;
        /* The settings may have gone back to their defaults. */
        options_fresh();
        k.card_look = k.card_look || k.prog.fed;
        ++k.rev;
    }
}

bool kst_panel_is_open(void)
{
    return k.f.open;
}

bool kst_panel_holds_port(void)
{
    const kst_prog_state_t st = kst_prog_state(&k.prog);

    return st == KST_PROG_ENTERING || st == KST_PROG_PROGRAMMING
        || st == KST_PROG_WORKING || st == KST_PROG_LEAVING;
}

uint32_t kst_panel_revision(void)
{
    return k.rev + sprog_revision(&k.f);
}

uint32_t kst_panel_hold_revision(void)
{
    return sprog_hold_revision(&k.f);
}

/**
 * @brief Everything of the model a render shows, as one number.  The PD
 *        mini's reading counts as the state line writes it: 10mV and 1mA
 *        while the port is in programming, nothing in any other state, so
 *        a reading the page does not show repaints nothing.
 * @return the number
 */
static uint32_t signature(void)
{
    const kst_prog_t *p = &k.prog;
    uint32_t line = 2166136261u;

    for (const char *t = kst_panel_status_line(0); *t != '\0'; ++t) {
        line = (line ^ (uint8_t)*t) * 16777619u;
    }
    const uint32_t part[] = {
        (uint32_t)kst_prog_state(p), (uint32_t)kst_prog_op(p),
        kst_prog_planning(p),
        kst_prog_message(p)->seq, p->f.rd.flags, p->f.rd.frames,
        p->f.rd.port_no, kst_prog_have_servo(p), kst_prog_editable(p),
        (uint32_t)kst_prog_changed(p),
        (uint32_t)(kst_prog_changed(p) >> 32),
        kst_prog_backed_up(p), p->journal, kst_prog_in_doubt(p),
        kst_prog_write_enabled(p), p->f.stopped, p->f.card,
        kst_prog_card_busy(p), kst_prog_files_seq(p),
        (uint32_t)kst_prog_load_skipped(p),
        (uint32_t)(kst_prog_load_skipped(p) >> 32),
        p->f.target, p->f.supply_read, line,
        kst_prog_supply_mv(p), kst_prog_supply_ma(p), k.hv,
        kst_prog_wait_ms(p),
        /* RESTORE is offered from the backup once it is fetched. */
        p->f.have_backup,
        kst_prog_restore_offered(p, KST_PROG_FROM_SESSION),
        kst_prog_restore_offered(p, KST_PROG_FROM_JOURNAL),
        kst_prog_restore_offered(p, KST_PROG_FROM_RUN),
    };
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); ++i) {
        h = (h ^ part[i]) * 16777619u;
    }
    return h;
}

/**
 * @brief Follows the model's state after a feed or an operation: closes
 *        editors that asked for the state they were opened in and counts the
 *        revision when the picture changes.
 */
static void model_moved(void)
{
    const kst_prog_state_t st = kst_prog_state(&k.prog);
    uint32_t sig;

    if (st != k.state_seen) {
        /* An editor asks for something of the state it was opened in. */
        if (st != KST_PROG_PROGRAMMING) {
            kpn_close_editors();
            /* CONNECT's and WRITE's presses keep their holds, for the
             * release to end, and a tab its press; any other press taps
             * nothing on its release. */
            if (k.f.have && k.f.on != SPROG_ON_TAB
                && k.f.on != SPROG_ON_HOLD) {
                k.f.on = SPROG_ON_ABSORB;
            }
        }
        k.state_seen = st;
    }
    kpn_card_moved();
    kpn_supply_sync();
    kpn_gate_follow();
    sig = signature();
    if (sig != k.sig) {
        k.sig = sig;
        ++k.rev;
    }
}

void kst_panel_feed(const kst_prog_feed_t *f)
{
    kst_prog_feed(&k.prog, f);
    model_moved();
}

void kst_panel_set_hv(bool on)
{
    if (on == k.hv) {
        return;
    }
    k.hv = on;
    if (!on) {
        if (kst_prog_supply_mv(&k.prog) > KST_PANEL_STD_MV) {
            kst_prog_supply_set(&k.prog, KST_PROG_SUPPLY_MV,
                                kst_prog_supply_ma(&k.prog));
        }
        /* An editor that offers what HV SERVO allowed. */
        if ((k.f.kp.open && k.f.kp_what == KP_VOLTAGE)
            || (k.f.page.open && k.f.page.what == W_HV)) {
            sprog_close_editors(&k.f);
        }
    }
    model_moved();
}

bool kst_panel_take(kst_prog_cmd_t *out)
{
    return kst_prog_take(&k.prog, out);
}

void kst_panel_bench_armed(bool armed)
{
    kst_prog_bench_armed(&k.prog, armed);
}

bool kst_panel_card_look(void)
{
    const bool look = k.card_look;

    k.card_look = false;
    return look;
}

bool kst_panel_card_take(kst_prog_card_req_t *out)
{
    return kst_prog_card_take(&k.prog, out);
}

void kst_panel_card_done(const kst_prog_card_done_t *done)
{
    kst_prog_card_done(&k.prog, done);
    model_moved();
    ++k.rev;
}

size_t kst_panel_file_take(char *out, size_t n)
{
    return kst_prog_file_take(&k.prog, out, n);
}

void kst_panel_file_done(bool ok)
{
    kst_prog_file_done(&k.prog, ok);
    model_moved();
}

bool kst_panel_file_load(const char *text, size_t len)
{
    const bool ok = kst_prog_file_load(&k.prog, text, len);

    model_moved();
    return ok;
}

bool kst_panel_file_late(const char *text, size_t len)
{
    /* A record the model holds is the one that stands, also after the
     * read that follows the interrupted write: RESTORE offers its values. */
    return !kst_prog_journal_held(&k.prog)
        && kst_panel_file_load(text, len);
}

/**
 * @brief Where hold_revision shows: CONNECT or WRITE n VALUES while its
 *        hold runs or flashes, else the hold page's button.
 * @return the rectangle, body pixels
 */
static gfx_rect_t kst_panel_hold_rect(void)
{
    return sprog_hold_rect(&k.f);
}

/* The session SERVO > PROGRAM opens from its row (prog_registry.h). */
const prog_page_set_t kst_panel_pages = {
    .init          = kst_panel_init,
    .entry         = kst_panel_screen_entry,
    .open          = kst_panel_open,
    .is_open       = kst_panel_is_open,
    .back          = kst_panel_back,
    .holds_port    = kst_panel_holds_port,
    .armed         = kst_panel_bench_armed,
    .tick          = kst_panel_tick,
    .has_press     = kst_panel_has_press,
    .event         = kst_panel_event,
    .render        = kst_panel_render,
    .revision      = kst_panel_revision,
    .hold_revision = kst_panel_hold_revision,
    .hold_rect     = kst_panel_hold_rect,
    .leave         = kst_panel_leave,
    .cancel        = kst_panel_cancel,
    .pin           = kst_panel_pin,
    .safe_at       = kst_panel_safe_at,
};
