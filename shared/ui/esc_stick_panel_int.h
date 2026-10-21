/*
 * The private seam between the files of ESC STICK's pages:
 * esc_stick_panel.c, the state, a run and the application's side;
 * esc_stick_list.c, the profile list and its texts; esc_stick_touch.c,
 * the presses; esc_stick_draw.c, the list and a profile's pages drawn;
 * and esc_stick_draw_run.c, a run, the RUN warning and the manual steps
 * drawn.  The pages' own layout, the state they read and write, and the
 * functions one calls of another, all prefixed sp_.  The seam with the
 * page set, esc_stick_pages.c, is esc_stick_int.h, which this header
 * includes.  Nothing outside these five files includes this header; the
 * interface is esc_stick_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esc_profile.h"
#include "esc_stick.h"
#include "settings.h"
#include "ui_textkey.h"

#include "esc_stick_int.h"

/* ------------------------------------------------------- the stick class */

/* The profile list: rows of SP_ROW_H from SP_ROW_Y0, SP_ROWS to a page. */
#define SP_MAX     (128)
#define SP_ROWS    9
#define SP_ROW_Y0  (CRUMB_Y + 40)
#define SP_ROW_H   36
#define SP_QUEUE   8
#define SP_NOTE    160

/* A model's row, from its left edge: the name in 26 cells from 12 px, the
 * current and the voltage right-aligned in 48 and 56 px, and the family.
 * Right of the family, what the profile is or why it does not run: 220 px,
 * or up to SP_WHY_CELLS where a reason needs them, the family giving way. */
#define SP_AMPS_X   224
#define SP_VOLTS_X  276
#define SP_FAMILY_X 344
#define SP_WHY_CELLS ((W - 2 * PAD - 24 - 12 - SP_FAMILY_X) / 8)

/* The search field in the crumb row, and the keyboard docked to the right
 * of SP_DOCK_X below it while the field is typed in. */
#define SP_FIND_MAX 16
#define SP_FIND_X   (W - PAD - 12 - 76 - 8 - SP_FIND_W)
#define SP_FIND_W   172
#define SP_DOCK_X   352
/* The trail left of the field, from the crumb's text: 48 cells. */
#define SP_TRAIL_CELLS ((SP_FIND_X - 8 - (PAD + 12 + 96 + 16)) / 8)
/* The footer under the rows: the count at its right, 45 cells, or at its
 * left beside the docked keyboard. */
#define SP_FOOT_Y   (SP_ROW_Y0 + SP_ROWS * SP_ROW_H + 6)
#define SP_COUNT_W  360
/* The most models one maker's list holds: as many as the registry lets a
 * maker have, so the count and the rows always agree. */
#define SP_MODELS   ((int)ESC_MAKER_MODELS_MAX)

/* The stack light at the right of the run's card: a cap, the red and the
 * green lens, each with a collar under it, and the base, px. */
#define SP_TOWER_W     44
#define SP_TOWER_X     (W - PAD - 16 - SP_TOWER_W)
#define SP_TOWER_Y     (PARM_Y + 14)
#define SP_TOWER_CAP   8
#define SP_TOWER_LENS  58
#define SP_TOWER_RING  3
#define SP_TOWER_BASE  26
/* What the run's and the result's lines may take beside it, in cells. */
#define SP_LINE_CELLS  86

/* The settings the TIMING page shows, in esc_stick_panel.c, and their
 * count. */
#define SP_SETTINGS 14
extern const setting_id_t k_sp_settings[SP_SETTINGS];

/* A maker on the list's first level: how many models it has, how many the
 * search finds, how many of those run, and whether any has manual steps. */
typedef struct {
    const char *name;
    uint16_t    models, found, runs;
    bool        manual;
} sp_maker_t;

/* A model on the second level: its profile's index in the registry and its
 * own index in the profile. */
typedef struct {
    uint16_t prof, model;
} sp_row_t;

typedef struct {
    /* The list in two levels.  Level 0 the makers, alphabetical; level 1
     * the models of one maker, by current, voltage and name.  count,
     * runnable and scroll are the level's showing; bscroll is the makers'
     * kept while a maker is open. */
    int        level;
    sp_maker_t makers[SP_MAX];
    sp_row_t   rows_m[SP_MODELS];
    const char *maker;      /* the maker open on level 1 */
    int        count, runnable, scroll, bscroll;
    int        model;       /* the page's model, -1 for none */

    /* The profile picked, and what is to change: per item, -1 to keep it,
     * else the index of the value to store. */
    const esc_profile_t *p;
    int  pick[256];
    int  iscroll, picked;
    char note[SP_NOTE];     /* why RUN cannot start; "" when it can */
    bool could;             /* whether RUN could, as last drawn */

    /* The warning, and its hold. */
    bool      warn, warn_down;
    uint8_t   warn_id;
    ui_hold_t hold;

    bool timing;
    int  tscroll, tpicked;

    /* The run, and whether its result is still on screen. */
    esc_stick_t run;
    bool        shown;
    uint32_t    runs;
    uint32_t    sig;        /* what the progress showed last */
    uint32_t    built_key;  /* sp_list_key() when the list was built */
    uint32_t    builds;     /* how often the list has been built */

    /* The bench, as the application last said. */
    uint32_t now_ms, stops, pressed;
    bool     armed, link_up;
    /* The output binding, as the application last said: whether one is
     * read, and the channels it binds as a throttle (one bit each). */
    bool     bind_read;
    uint8_t  throttle_mask;

    /* The search: the pattern the list shows, what it was when the
     * keyboard opened (CANCEL goes back to it), and the keyboard. */
    char         find[SP_FIND_MAX + 1];
    char         find_was[SP_FIND_MAX + 1];
    ui_textkey_t tk;
    uint32_t     tk_rev;    /* the keyboard's revision last drawn */
    gfx_rect_t   find_box, find_clr;

    /* The stack light's green, as the last tick had it. */
    esc_stick_light_t light;
    bool              green;

    /* What the run has asked for, and what is still to be taken. */
    motor_cmd_t q[SP_QUEUE];
    int         qn;
    bool        sent_arm, sent_supply;
    float       sent_pct;

    /* The manual steps' pop-up and the profile it shows: the page's, or a
     * row's that does not run.  It opens by itself the first time a
     * profile with manual steps is opened, one bit a profile by its index
     * in the registry. */
    bool                 hand_open;
    const esc_profile_t *hand_p;
    int                  hand_model;    /* the model it was opened for, or
                                           -1: the family's lowest      */
    bool                 was_running;   /* the run, as the last tick saw */
    bool                 warn_read;     /* ALL STEPS read over the warning */

    /* The supply as its newest reading had it, run or no run: whether its
     * output is live, and since when it has read off. */
    bool                 sup_have, sup_live, sup_off_known, sup_gate;
    bool                 sup_off_drawn; /* what a result last showed     */
    uint32_t             sup_at, sup_off_since;
    uint8_t              hand_seen[SP_MAX / 8];

    gfx_rect_t rows[SP_ROWS], list_up, list_dn;
    gfx_rect_t hold_btn, cancel_btn, hand_btn, steps_btn, timing_btn;
} stick_t;

/* The state, esc_stick_s, in esc_stick_panel.c, so its .bss stays where
 * psram_bss.lf puts esc_stick_panel's.  The files read it as s, the name
 * it had as the one file's static: the object's own name keeps it apart
 * from the SERVO screen's s. */
typedef struct {
    stick_t  st;

    /* The phase tap, as the control task last read it. */
    tone_readout_t tone;
} sp_state_t;
extern sp_state_t esc_stick_s;
#define s esc_stick_s

/* --- the functions one file calls of another ---------------------------- */

/* esc_stick_panel.c: the picks as changes; the timing and the supply's set
 * points for a run; whether the supply reads off; whether the warning
 * asks a step of the operator it may not; whether the warning shows every
 * before-power step whole; the steps after the warning's; whether HOLD TO
 * RUN may not count; whether a run can start; what the run asks for, onto
 * the operator's paths; the warning's hold ended; the run ended now. */
size_t sp_changes(esc_stick_change_t *ch, size_t *picked);
void sp_timing(esc_stick_timing_t *t);
void sp_supply(uint32_t *mv, uint32_t *ma);
bool sp_supply_reads_off(void);
bool sp_warn_gated(void);
bool sp_warn_steps_fit(const esc_profile_t *p);
unsigned sp_later_steps(const esc_profile_t *p);
bool sp_hold_blocked(void);
bool sp_can_run(void);
void sp_follow(void);
void sp_end_hold(void);
void sp_end_run(esc_stick_reason_t why);

/* esc_stick_list.c: a text cut to @p max_chars; a manual step's action and
 * when it is due; a reason in the language showing; what the supply's set
 * points break of a model's ratings, the warning for a voltage the data
 * cannot vouch for, the same for a model with the settings now, and why a
 * model cannot run; what decides whether a profile runs; whether a model
 * runs; the level showing built again; a profile picked; the items the
 * page offers and the profile's index of one; the picked item kept among
 * those shown. */
void sp_text(gfx_canvas_t *c, int x, int y, const char *text, int max_chars,
             gfx_color_t ink);
const char *sp_action(const esc_manual_t *m);
const char *sp_when_text(const esc_manual_t *m);
const char *sp_why_text(const char *why);
const char *sp_rating_why(const esc_profile_t *p, int model, uint32_t mv,
                          uint32_t ma, bool by_hand, bool longer, char *buf,
                          size_t n);
const char *sp_rating_warn(const esc_profile_t *p, int model, uint32_t mv,
                           bool by_hand, bool longer, char *buf, size_t n);
const char *sp_model_warn(const esc_profile_t *p, int model, char *buf,
                          size_t n);
const char *sp_model_why(const esc_profile_t *p, int model, char *buf,
                         size_t n);
uint32_t sp_list_key(void);
bool sp_model_runs(const esc_profile_t *p, int model);
void sp_build_list(bool keep_scroll);
void sp_pick_profile(const esc_profile_t *p);
int sp_items(void);
int sp_item_at(int at);
void sp_pick_shown(void);

/* esc_stick_touch.c: the rows a page shows from @p scroll; a profile row;
 * the search's keyboard closed. */
int sp_rows_shown(int total, int scroll);
gfx_rect_t sp_row(int i);
void sp_find_close(void);

/* esc_stick_draw.c: the entry time the page shows. */
uint32_t sp_entry_shown(void);

/* esc_stick_draw_run.c: the red light; the result's count and its last
 * line; a manual step's text while a run waits for it; the drawers of a
 * run, its result, the RUN warning, the manual steps and a run's prompt. */
bool sp_red(void);
void sp_result_head(const esc_stick_t *e, char *buf, size_t n);
void sp_result_more(const esc_stick_t *e, unsigned shown, char *buf,
                    size_t n);
const char *sp_left_text(const esc_stick_t *e);
void sp_draw_progress(gfx_canvas_t *c);
void sp_draw_result(gfx_canvas_t *c);
void sp_draw_warning(gfx_canvas_t *c);
void sp_draw_hand(gfx_canvas_t *c);
void sp_draw_prompt(gfx_canvas_t *c);
