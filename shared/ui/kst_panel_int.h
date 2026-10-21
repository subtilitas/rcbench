/*
 * The private seam between the files of the programmer's overlay for KST
 * and Chaservo servos: kst_panel.c, the state, the rows, the helpers and
 * the caller's side; kst_panel_text.c, the texts of the messages;
 * kst_panel_warn.c, the warning pages; kst_panel_edit.c, the editors and
 * the actions; kst_panel_touch.c, the presses; kst_panel_run.c, the limit
 * run's sessions; and kst_panel_draw.c, the notes and the pages drawn.
 * The layout they draw on, the rows, the state they read and write, and
 * the functions one calls of another, prefixed kpn_.  Nothing outside
 * these seven files includes this header; the interface is kst_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "kst_panel.h"
#include "kst_prog.h"
#include "touch_types.h"
#include "ui_card_text.h"
#include "ui_keypad.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* ------------------------------------------------------------------ geometry */

#define TAB_H    26
#define TAB_W    376          /* five tabs of 72 px, 9 cells each */
#define ROW0     48           /* below the area's top */
#define PITCH    42
#define ROW_H    34
#define VAL_W    116          /* 14 cells of a button */
#define WIDE_W   200          /* a wide row's value */
#define STEP_W   44           /* its minus and plus keys */
#define LINE_H   18

/* The plus and minus keys repeat as SETUP's do. */
#define REPEAT_DELAY_S 0.45f
#define REPEAT_SLOW_HZ 8.0f
#define REPEAT_FAST_S  1.8f
#define REPEAT_FAST_HZ 30.0f

enum { PG_SERVO = 0, PG_TUNE, PG_PULSE, PG_FAILSAFE, PG_LOCKED, PG_COUNT };

enum { K_FIELD = 0, K_STEP, K_LOCKED, K_ACT };

enum {
    A_PORT = 0, A_READ, A_FILES, A_RESTORE, A_MORE, A_ENABLE,
    A_WRITE
};

typedef struct {
    uint8_t page, row, col, wide, kind, what;
    int16_t label;          /* a ui_text_id_t, or -1 for name */
    const char *name;       /* the field's name, as the vendor's tool has it */
} row_t;

/* The rows of the five pages, in kst_panel.c, and their count. */
#define ROW_COUNT 33
extern const row_t k_rows[ROW_COUNT];

/* A soft limit's words, in kst_panel_text.c. */
extern const int16_t k_rule[KST_RULE_COUNT];

/* -------------------------------------------------------------------- state */

enum {
    P_NONE = 0, P_TAB, P_CLOSE, P_ROW, P_MINUS, P_PLUS, P_KEYPAD, P_CHOICE,
    P_CHOICE_CANCEL, P_APPLY, P_CANCEL, P_TEXTKEY, P_NOTES
};

enum { C_STRETCH = 0, C_RESTORE, C_MORE, C_FILES };
#define FILES_SAVE (-1)       /* the list's first entry: a new file */
#define FILES_NEXT (-2)       /* its last: the files behind these   */
enum { MORE_VERIFY = 0, MORE_DISCARD, MORE_RELEASE, MORE_VOLTAGE,
       MORE_CURRENT, MORE_WAIT };
#define RESTORE_FORGET ((int)KST_PROG_FROM_COUNT)

enum { W_SOFT = 0, W_ENABLE, W_WRITE, W_RESTORE, W_RELEASE, W_LEAVE,
       W_CHECK, W_HV };

/* The keypad edits what an ENTER asks of the supply, not a row. */
#define KP_VOLTAGE (-1)
#define KP_CURRENT (-2)
#define KP_WAIT    (-3)
#define CHOICE_MAX 8
#define WARN_LINES 12
/* The rows a page's lines take at most, wrapped to the page's width: 14
 * rows at LINE_H take 250 px of the 268 px from WARN_TOP to 8 px above
 * the buttons. */
#define WARN_ROWS 14
/* The lines a page gives to the warnings of soft limits. */
#define WARN_RULES 7
/* The lines the page of a file gives to its values. */
#define FILE_VALUES 4
/* The page's first line, under its title; the pitch of its lines, the
 * widest of these that fits, and the rows of a wrapped line at LINE_H. */
#define WARN_TOP 68

/* The overlay's state. */
typedef struct {
    kst_prog_t prog;
    gfx_rect_t area;
    bool       open;
    uint8_t    note_page;      /* the notes' page shown, when they take more */
    uint32_t   note_sig;       /* the notes' places and rows the page is of */
    ui_tabs_t  tabs;
    bool       hv;             /* HV SERVO: above KST_PANEL_STD_MV may be set */
    bool       loaded;         /* the options were read from the settings   */
    bool       card_look;      /* the card in the slot is asked for         */

    ui_keypad_t kp;
    int        kp_row;

    ui_textkey_t tk;               /* a new file's name */
    char       name[KST_PROG_NAME_MAX];
    bool       name_wait;          /* the save of that name is on its way */
    bool       files_wait;         /* the list was asked for */
    uint32_t   files_seq;

    struct {
        bool open;
        int  kind;
        int  count;
        char title[64];
        char labels[CHOICE_MAX][KST_PANEL_LINE];
        int  values[CHOICE_MAX];
    } ch;

    struct {
        bool      open;
        int       kind;
        bool      danger;
        bool      down;
        ui_hold_t hold;
        int       count;
        char      title[64];
        char      apply[64];
        char      lines[WARN_LINES][KST_PANEL_LINE];
        uint8_t   level[WARN_LINES];
        bool      no_file;      /* the page says: no card, no file */
        uint16_t  mv;           /* W_HV: the voltage the hold sets */
        kst_prog_restore_t plan;
    } warn;

    bool    have;              /* a press is on the glass */
    uint8_t id;
    int     pressed;
    int     row;
    float   held_s;            /* on a plus or minus key */

    kst_prog_state_t state_seen;
    uint32_t sig;
    uint32_t rev;
    uint32_t hold_rev;

    /* A limit run's step of the programmer (kst_panel_run_connect()). */
    struct {
        uint8_t  op;            /* RUN_OP_* */
        bool     kept;          /* the operator's supply values are kept */
        uint16_t keep_mv, keep_ma;
        kst_field_id_t want_id[2]; /* the write's two fields */
        uint16_t want[2];       /* and their raw values */
        unsigned writes;        /* writes of the run that ended well */
    } run;
} kst_panel_state_t;

/* The state, kst_panel_k, in kst_panel.c, so its .bss stays where
 * psram_bss.lf puts kst_panel's.  The files read it as k, the name it had
 * as the one file's static: the object's own name keeps it apart from
 * every other screen's state. */
extern kst_panel_state_t kst_panel_k;
#define k kst_panel_k

/* --- the functions one file calls of another ---------------------------- */

/* kst_panel.c: the card's words for @p at; the bits set in @p v; the
 * programming port and whether the tester drives one; a flag of the
 * port's report; whether the port is in programming; the servo's type. */
ui_text_id_t kpn_card_text_id(ui_card_at_t at);
const char *kpn_card_tr(ui_card_at_t at);
unsigned kpn_ones(uint64_t v);
uint8_t kpn_port_no(void);
bool kpn_on_port(void);
bool kpn_flag(uint16_t f);
bool kpn_programming(void);
unsigned kpn_servo_type(void);
/* A row's label; a field's name, its membership of @p set, its unit, its
 * decimals and its value as a row shows it; whether the draft changed
 * it. */
const char *kpn_row_label(const row_t *r);
const char *kpn_field_name(unsigned id);
bool kpn_in_set(kst_field_set_t set, unsigned id);
const char *kpn_unit_of(kst_field_id_t id);
int kpn_decimals_of(kst_field_id_t id);
void kpn_fmt_value(kst_field_id_t id, uint16_t raw, char *out, size_t n,
                   bool unit);
void kpn_fmt_field(kst_field_id_t id, uint16_t raw, char *out, size_t n);
bool kpn_changed(kst_field_id_t id);
/* @p s in rows of the area's width from @p y down; the y under it. */
int kpn_text_rows(gfx_canvas_t *c, int x, int y, const char *s,
                  gfx_color_t color);
/* The tabs, a row, its value, its plus or minus key, and the row at a
 * place of a page. */
gfx_rect_t kpn_tabs_rect(void);
gfx_rect_t kpn_row_rect(const row_t *r);
gfx_rect_t kpn_value_rect(const row_t *r);
gfx_rect_t kpn_step_rect(const row_t *r, bool up);
const row_t *kpn_row_at(int page, int row, int col);
/* A file name as a line shows it. */
const char *kpn_shown(const char *name);

/* kst_panel_text.c: the lines of a message -- how many values stay, a line
 * of the interrupted write's record, what an edited file is, and how many
 * values a write sends. */
void kpn_stay_line(char *out, unsigned n);
void kpn_journal_line(char *out, int text, unsigned arg);
void kpn_kind_line(char *out, int text);
void kpn_write_n(char *out, size_t size, unsigned n);

/* kst_panel_warn.c: the rows a page's lines take; the warning pages, each
 * opened by what it says before it acts; the name of a restore's source;
 * APPLY's hold completed. */
int kpn_warn_rows(void);
void kpn_warn_soft(const row_t *r);
void kpn_warn_enable(void);
void kpn_warn_file(void);
void kpn_warn_write(void);
ui_text_id_t kpn_from_text(unsigned from);
void kpn_warn_restore(const kst_prog_restore_t *plan);
void kpn_warn_check(const kst_prog_restore_t *plan);
void kpn_warn_release(void);
void kpn_warn_leave(void);
void kpn_warn_hv(uint16_t mv);
void kpn_warn_apply(void);

/* kst_panel_edit.c: every editor shut; a file's name typed; what the
 * card's side brought; a row's editor opened; the options kept and
 * loaded; the keypad's value; a plus or minus key; whether a LEAVE's
 * wait runs; a row's action; the list's choice; whether a row's control
 * takes a tap now. */
void kpn_close_editors(void);
void kpn_name_done(const char *name);
void kpn_card_moved(void);
void kpn_edit_field(const row_t *r);
void kpn_options_keep(void);
void kpn_options_load(void);
void kpn_keypad_done(float v);
void kpn_step(const row_t *r, bool up);
bool kpn_leave_waits(const kst_prog_t *p);
void kpn_act(const row_t *r);
void kpn_choice_done(int value);
bool kpn_row_live(const row_t *r);

/* kst_panel_touch.c: the supply's own controls follow the port. */
void kpn_supply_sync(void);

/* kst_panel_draw.c: whether (x, y) is on notes that take more than one
 * page. */
bool kpn_notes_paged_at(int x, int y);
