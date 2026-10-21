/*
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

#include "kst_card.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "link_pages.h"
#include "settings.h"
#include "ui_card_text.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

const row_t kpn_rows[] = {
    { PG_SERVO, 0, 0, 1, K_ACT, A_PORT, -1, "PORT" },
    { PG_SERVO, 1, 0, 0, K_ACT, A_READ, -1, "SERVO" },
    { PG_SERVO, 1, 1, 0, K_ACT, A_FILES, TX_KP_FILES, NULL },
    { PG_SERVO, 2, 0, 0, K_ACT, A_RESTORE, TX_KP_RESTORE, NULL },
    { PG_SERVO, 2, 1, 0, K_ACT, A_MORE, TX_KP_MORE, NULL },
    { PG_SERVO, 3, 0, 1, K_ACT, A_ENABLE, TX_KP_ENABLE, NULL },
    { PG_SERVO, 4, 0, 1, K_ACT, A_WRITE, TX_KP_CHANGES, NULL },

    { PG_TUNE, 0, 0, 1, K_STEP, KST_F_DEAD_BAND, -1, "DEAD BAND" },
    { PG_TUNE, 1, 0, 0, K_FIELD, KST_F_DUTY, -1, "MAX. DUTY" },
    { PG_TUNE, 1, 1, 0, K_FIELD, KST_F_BOOST, -1, "BOOST" },
    { PG_TUNE, 2, 0, 0, K_FIELD, KST_F_STRETCH, -1, "STRETCH" },
    { PG_TUNE, 2, 1, 0, K_FIELD, KST_F_SOFT_START, -1, "SOFT START" },
    { PG_TUNE, 3, 0, 0, K_FIELD, KST_F_SPD_SEL, TX_KP_SPEED_LIMIT, NULL },
    { PG_TUNE, 3, 1, 0, K_FIELD, KST_F_SPD, TX_KP_SPEED, NULL },

    { PG_PULSE, 0, 0, 1, K_STEP, KST_F_NEUTRAL, -1, "NEUTRAL" },
    { PG_PULSE, 1, 0, 0, K_FIELD, KST_F_PULSE_LOWER, -1, "PULSE LOWER" },
    { PG_PULSE, 1, 1, 0, K_FIELD, KST_F_PULSE_UPPER, -1, "PULSE UPPER" },
    { PG_PULSE, 2, 0, 0, K_FIELD, KST_F_LEFT_ANGLE, -1, "LEFT ANGLE" },
    { PG_PULSE, 2, 1, 0, K_FIELD, KST_F_RIGHT_ANGLE, -1, "RIGHT ANGLE" },
    { PG_PULSE, 3, 0, 0, K_FIELD, KST_F_REVERSION, -1, "REVERSION" },

    { PG_FAILSAFE, 0, 0, 1, K_FIELD, KST_F_ALLOW_UNCONT, TX_KP_FS_MOVE, NULL },
    { PG_FAILSAFE, 1, 0, 0, K_FIELD, KST_F_UNCONT_POS, -1, "POSITION" },
    { PG_FAILSAFE, 1, 1, 0, K_FIELD, KST_F_UNCONT_TIME, TX_KP_FS_AFTER, NULL },

    { PG_LOCKED, 0, 0, 0, K_LOCKED, KST_F_20K_SEL, TX_KP_TYPE, NULL },
    { PG_LOCKED, 0, 1, 0, K_LOCKED, KST_F_LOCK, -1, "LOCK" },
    { PG_LOCKED, 1, 0, 0, K_LOCKED, KST_F_GYRO, -1, "MODE" },
    { PG_LOCKED, 1, 1, 0, K_LOCKED, KST_F_PROT1_EN, -1, "PROT1 EN" },
    { PG_LOCKED, 2, 0, 0, K_LOCKED, KST_F_PROT1_CANCLE, -1, "PROT1 CANCLE" },
    { PG_LOCKED, 2, 1, 0, K_LOCKED, KST_F_PROT1_TIME, -1, "PROT1 TIME" },
    { PG_LOCKED, 3, 0, 0, K_LOCKED, KST_F_PROT1_PWM, -1, "PROT1 PWM" },
    { PG_LOCKED, 3, 1, 0, K_LOCKED, KST_F_PROT_POT_ERR, -1, "POT ERR" },
    { PG_LOCKED, 4, 0, 0, K_LOCKED, KST_F_POT_SAM_TIMES, -1, "POT SAMPLES" },
    { PG_LOCKED, 4, 1, 0, K_LOCKED, KST_F_PROT_PWM_ERR, -1, "PWM ERR" },
};
_Static_assert(sizeof(kpn_rows) / sizeof(kpn_rows[0]) == ROW_COUNT,
               "ROW_COUNT in kst_panel_int.h counts kpn_rows");

static const ui_text_id_t k_pages[PG_COUNT] = {
    TX_KP_TAB_SERVO, TX_KP_TAB_TUNE, TX_KP_TAB_PULSE, TX_KP_TAB_FAILSAFE,
    TX_KP_TAB_LOCKED,
};

/* The state, its type in kst_panel_int.h. */
kst_panel_state_t kst_panel_k;

/* What the panel's slot holds, as the feed last said: no card, a card not
 * formatted as FAT32 or exFAT, or one that cannot be read. */
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

unsigned kpn_servo_type(void)
{
    return kst_prog_have_servo(&k.prog)
               ? kst_card_type(kst_prog_servo(&k.prog)) : 0u;
}

const char *kpn_row_label(const row_t *r)
{
    if (r->kind == K_ACT && r->what == A_READ && kpn_servo_type() != 0u) {
        /* Static: the caller draws the label after this returns. */
        static char typed[24];

        snprintf(typed, sizeof(typed), TR(KP_SERVO_V), kpn_servo_type());
        return typed;
    }
    if (r->kind == K_ACT && r->what == A_PORT && kpn_on_port()
        && kpn_flag(LINK_KS_F_RAIL)) {
        /* What CONNECT asks of the supply, and what it holds after. */
        static char port[40];
        const unsigned mv = kst_prog_supply_mv(&k.prog);

        snprintf(port, sizeof(port), TR(KP_PORT_SUPPLY), mv / 1000u,
                 mv % 1000u / 100u, (unsigned)kst_prog_supply_ma(&k.prog));
        return port;
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
        snprintf(out, n, "%d.%d%s%s", (int)(v / 10), (int)(v % 10),
                 unit ? " " : "", unit ? kpn_unit_of(id) : "");
        break;
    case KST_UNIT_PERCENT:
        snprintf(out, n, "%d%s", (int)v, unit ? " %" : "");
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

/* The cells of the 8 x 16 face a line of text has: the area less 10 px on
 * each side, 58 on the 488 px card. */
static int text_cells(void)
{
    return (k.area.w - 20) / 8;
}

/* The bytes of @p s its first row takes: all of it where it fits, else up
 * to the last space that leaves the row no wider than the line, and a word
 * longer than the line is cut. */
static size_t row_len(const char *s)
{
    /* At least one cell: an area narrower than one, as before
     * kst_panel_init(), still ends every row. */
    const int cells = text_cells();
    const size_t fit = gfx_text_prefix(s, cells > 0 ? cells : 1);
    size_t cut = fit;

    if (s[fit] == '\0' || s[fit] == ' ') {
        return fit;
    }
    while (cut > 0u && s[cut] != ' ') {
        --cut;
    }
    return cut > 0u ? cut : fit;
}

/* The start of the row after the one of @p len bytes at @p s. */
static const char *next_row(const char *s, size_t len)
{
    s += len;
    while (*s == ' ') {
        ++s;
    }
    return s;
}

int kst_panel_text_rows(const char *s)
{
    int n = 0;

    while (s != NULL && *s != '\0') {
        s = next_row(s, row_len(s));
        ++n;
    }
    return n;
}

/* @p s from @p y down, a row each LINE_H.  Returns the y under it. */
int kpn_text_rows(gfx_canvas_t *c, int x, int y, const char *s,
                     gfx_color_t color)
{
    while (*s != '\0') {
        char row[KST_PANEL_LINE];
        const size_t len = row_len(s);

        snprintf(row, sizeof(row), "%.*s", (int)len, s);
        gfx_text(c, x, y, row, &gfx_font_8x16, color, 1);
        s = next_row(s, len);
        y += LINE_H;
    }
    return y;
}

/* ----------------------------------------------------------------- geometry */

gfx_rect_t kpn_tabs_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + 8), (int16_t)(k.area.y + 8),
                         TAB_W, TAB_H };
}

gfx_rect_t kpn_row_rect(const row_t *r)
{
    const int col_w = (k.area.w - 30) / 2;
    const int x = r->col == 0 ? k.area.x + 10 : k.area.x + 20 + col_w;

    return (gfx_rect_t){ (int16_t)x,
                         (int16_t)(k.area.y + ROW0 + r->row * PITCH),
                         (int16_t)(r->wide != 0u ? k.area.w - 20 : col_w),
                         ROW_H };
}

/* A row's controls: the value, and for a stepped row its keys beside it. */
static gfx_rect_t control_rect(const row_t *r)
{
    const gfx_rect_t rr = kpn_row_rect(r);
    const int w = r->wide != 0u ? WIDE_W : VAL_W;

    return (gfx_rect_t){ (int16_t)(rr.x + rr.w - w), rr.y, (int16_t)w, rr.h };
}

gfx_rect_t kpn_value_rect(const row_t *r)
{
    const gfx_rect_t v = control_rect(r);

    if (r->kind != K_STEP) {
        return v;
    }
    return (gfx_rect_t){ (int16_t)(v.x + STEP_W + 4), v.y,
                         (int16_t)(v.w - 2 * (STEP_W + 4)), v.h };
}

gfx_rect_t kpn_step_rect(const row_t *r, bool up)
{
    const gfx_rect_t v = control_rect(r);

    return (gfx_rect_t){ (int16_t)(up ? v.x + v.w - STEP_W : v.x), v.y,
                         STEP_W, v.h };
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

gfx_rect_t kst_panel_tab_rect(int page)
{
    return k.tabs.rect[page >= 0 && page < PG_COUNT ? page : 0];
}

gfx_rect_t kst_panel_close_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + k.area.w - 8 - 90),
                         (int16_t)(k.area.y + 6), 90, 30 };
}

gfx_rect_t kst_panel_value_rect(int page, int row, int col)
{
    const row_t *r = kpn_row_at(page, row, col);

    return r != NULL ? kpn_value_rect(r) : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t kst_panel_step_rect(int page, int row, bool up)
{
    const row_t *r = kpn_row_at(page, row, 0);

    return r != NULL ? kpn_step_rect(r, up) : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t kst_panel_choice_rect(int i)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + 10),
                         (int16_t)(k.area.y + 48 + i * 46),
                         (int16_t)(k.area.w - 20), 40 };
}

gfx_rect_t kst_panel_choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + k.area.w - 10 - 110),
                         (int16_t)(k.area.y + k.area.h - 46), 110, 36 };
}

gfx_rect_t kst_panel_apply_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + 20),
                         (int16_t)(k.area.y + k.area.h - 76), 260, 56 };
}

gfx_rect_t kst_panel_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + k.area.w - 20 - 160),
                         (int16_t)(k.area.y + k.area.h - 76), 160, 56 };
}

ui_keypad_t *kst_panel_keypad(void)
{
    return &k.kp;
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
    return &k.tk;
}

const char *kst_panel_warning_line(int i)
{
    return k.warn.open && i >= 0 && i < k.warn.count ? k.warn.lines[i] : NULL;
}

int kst_panel_warning_level(int i)
{
    return k.warn.open && i >= 0 && i < k.warn.count ? (int)k.warn.level[i]
                                                     : (int)KST_PROG_INFO;
}

const char *kst_panel_label(int page, int row, int col)
{
    const row_t *r = kpn_row_at(page, row, col);

    return r != NULL ? kpn_row_label(r) : NULL;
}

const char *kst_panel_choice_title(void)
{
    return k.ch.open ? k.ch.title : NULL;
}

const char *kst_panel_choice_label(int i)
{
    return k.ch.open && i >= 0 && i < k.ch.count ? k.ch.labels[i] : NULL;
}

int kst_panel_page(void)
{
    return k.tabs.selected;
}

int kst_panel_editor(void)
{
    return k.kp.open ? 1 : k.tk.open ? 4 : k.ch.open ? 2
         : k.warn.open ? 3 : 0;
}

/* --------------------------------------------------------------- the caller */

/* Not part of k: a start of the screens leaves it. */
static kst_prog_gate_t *s_gate;

void kst_panel_gate(kst_prog_gate_t *g)
{
    s_gate = g;
    kst_prog_gate_set(&k.prog, g);
}

/* The settings that reach the port: what the tester drives and whether the
 * port renders its pulse.  Held while nothing but the programmer reaches
 * the port. */
static bool holds_setting(setting_id_t id)
{
    return (id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM)
        && kst_prog_holds_port(&k.prog);
}

void kst_panel_init(gfx_rect_t area)
{
    memset(&k, 0, sizeof(k));
    kst_prog_init(&k.prog);
    settings_set_hold(holds_setting);
    kst_prog_gate_set(&k.prog, s_gate);
    k.area = area;
    ui_tabs_init_text(&k.tabs, k_pages, PG_COUNT, kpn_tabs_rect());
    k.kp.pressed = -1;
    k.tk.pressed = -1;
}

/* The options are read where the settings are there: at a screen entry and
 * at an opening, never at kst_panel_init(), which runs before the settings
 * are loaded.  A session keeps the values it began with, except one found
 * running by a panel that has read none yet. */
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
    ++k.rev;
}

void kst_panel_open(void)
{
    if (!k.open) {
        k.open = true;
        k.note_page = 0;
        /* The settings may have gone back to their defaults. */
        options_fresh();
        k.card_look = k.card_look || k.prog.fed;
        ++k.rev;
    }
}

bool kst_panel_is_open(void)
{
    return k.open;
}

bool kst_panel_holds_port(void)
{
    const kst_prog_state_t st = kst_prog_state(&k.prog);

    return st == KST_PROG_ENTERING || st == KST_PROG_PROGRAMMING
        || st == KST_PROG_WORKING || st == KST_PROG_LEAVING;
}

uint32_t kst_panel_revision(void)
{
    return k.rev + k.kp.revision + k.tk.revision;
}

uint32_t kst_panel_hold_revision(void)
{
    return k.hold_rev;
}

/* Everything of the model a render shows, as one number. */
static uint32_t signature(void)
{
    const kst_prog_t *p = &k.prog;
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
        p->f.target, p->f.supply_read, p->f.out_mv, p->f.out_ma,
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

static void model_moved(void)
{
    const kst_prog_state_t st = kst_prog_state(&k.prog);
    uint32_t sig;

    if (st != k.state_seen) {
        /* An editor asks for something of the state it was opened in. */
        if (st != KST_PROG_PROGRAMMING) {
            kpn_close_editors();
            if (k.pressed != P_TAB && k.pressed != P_CLOSE) {
                k.pressed = P_NONE;
            }
        }
        k.state_seen = st;
    }
    kpn_card_moved();
    kpn_supply_sync();
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
        if (k.kp.open && k.kp_row == KP_VOLTAGE) {
            k.kp.open = false;
        }
        if (k.warn.open && k.warn.kind == W_HV) {
            k.warn.open = false;
            k.warn.down = false;
            ui_hold_reset(&k.warn.hold);
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
    .hold_rect     = kst_panel_apply_rect,
    .leave         = kst_panel_leave,
    .cancel        = kst_panel_cancel,
    .pin           = kst_panel_pin,
    .safe_at       = kst_panel_safe_at,
};
