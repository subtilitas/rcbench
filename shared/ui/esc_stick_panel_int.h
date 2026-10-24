/**
 * @file esc_stick_panel_int.h
 * @brief The seam between the files of ESC STICK's pages.
 *
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
#include "ui_keypad.h"
#include "ui_textkey.h"

#include "esc_stick_int.h"

/* ------------------------------------------------------- the stick class */

/**
 * @brief The profile list: SP_ROWS rows to a page, UI_TAP_MIN tall at an
 *        SP_ROW_P pitch from SP_ROW_Y0, as wide as the body less 20px a
 *        side; the footer's line under them ends 4px above the alert
 *        band.
 */
#define SP_MAX     (128)
#define SP_ROWS    7                          /**< rows to a page      */
#define SP_ROW_Y0  CARD_Y                     /**< the first row's top */
#define SP_ROW_H   UI_TAP_MIN                 /**< a row, 48px        */
#define SP_ROW_P   56                         /**< its pitch           */
#define SP_ROW_X   (PAD + 12)                 /**< a row's left        */
#define SP_ROW_TY  ((SP_ROW_H - 20) / 2)      /**< its text, from its top */
#define SP_QUEUE   8                          /**< commands queued     */
#define SP_NOTE    160                        /**< a note's bytes      */

/**
 * @brief A model's row, from its left edge: the name in SP_NAME_CELLS
 *        cells from 12px, the current and the voltage right-aligned in
 *        56 and 64px, and the family.  Right of the family, what the
 *        profile is or why it does not run: 260px, or up to SP_WHY_CELLS
 *        where a reason needs them, the family giving way.
 */
#define SP_NAME_CELLS 30    /**< a model's name, cells          */
#define SP_AMPS_X   320     /**< its current's column           */
#define SP_VOLTS_X  384     /**< its voltage's column           */
#define SP_FAMILY_X 460     /**< its family's column            */
/** @brief The cells right of the family's column. */
#define SP_WHY_CELLS ((W - 2 * PAD - 24 - 12 - SP_FAMILY_X) / SP_CELL)
/** @brief A maker's count of models, right-aligned at its row's end. */
#define SP_MAKER_RIGHT_W 370

/**
 * @brief The search field in the crumb row, X at its right end, and the
 *        page keys right of it; the keyboard docks right of SP_DOCK_X
 *        below them while the field is typed in.
 */
#define SP_FIND_MAX 16
#define SP_KEY_W    56                        /**< "^" and "v", px     */
#define SP_LIST_DN_X (W - PAD - 12 - SP_KEY_W)     /**< "v"               */
#define SP_LIST_UP_X (SP_LIST_DN_X - 8 - SP_KEY_W) /**< "^"               */
#define SP_FIND_W   260                           /**< the field's width */
#define SP_FIND_X   (SP_LIST_UP_X - 8 - SP_FIND_W) /**< its left          */
#define SP_CLR_W    UI_TAP_MIN                /**< X in the field      */
#define SP_DOCK_X   352                       /**< the keyboard's left */
/** @brief The trail left of the field, in SP_FONT_CRUMB's cells. */
#define SP_TRAIL_CELLS ((SP_FIND_X - 8 - (PAD + 12 + BACK_W + 16)) \
                        / SP_CRUMB_CELL)
/** @brief The footer under the rows: the count at its right, or at its
 *         left beside the docked keyboard. */
#define SP_FOOT_Y   (SP_ROW_Y0 + (SP_ROWS - 1) * SP_ROW_P + SP_ROW_H + 4)
#define SP_COUNT_W  360
_Static_assert(SP_FOOT_Y + 20 <= SP_FOOT, "the list's footer ends above "
               "the alert band");
/* The most models one maker's list holds: as many as the registry lets a
 * maker have, so the count and the rows always agree. */
#define SP_MODELS   ((int)ESC_MAKER_MODELS_MAX)

/* The stack light at the right of the run's card: a cap, the red and the
 * green lens, each with a collar under it, and the base, px. */
#define SP_TOWER_W     44
#define SP_TOWER_X     (W - PAD - 16 - SP_TOWER_W)
#define SP_TOWER_Y     (RUN_Y + 14)   /**< the stack light's top */
#define SP_TOWER_CAP   8
#define SP_TOWER_LENS  58
#define SP_TOWER_RING  3
#define SP_TOWER_BASE  26
/** @brief What the run's and the result's lines may take left of the
 *         stack light, in SP_CELL cells: 92. */
#define SP_LINE_CELLS  ((SP_TOWER_X - 12 - (PAD + 12)) / SP_CELL)
/** @brief What a result's lines under the stack light may take, from its
 *         seventh line on, in SP_CELL cells: 98. */
#define SP_WIDE_CELLS  ((W - 2 * PAD - 24) / SP_CELL)
_Static_assert(SP_TOWER_Y + SP_TOWER_CAP + 2 * (SP_TOWER_LENS + SP_TOWER_RING)
                   + SP_TOWER_BASE + 4 <= RUN_L0 + 6 * LINE_P,
               "a result's lines from its seventh pass under the stack "
               "light");

/** @brief The settings the TIMING page shows, in esc_stick_panel.c, and
 *         their count. */
#define SP_SETTINGS 14
extern const setting_id_t sp_settings_list[SP_SETTINGS];

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
    /* TIMING's keypad, the row of sp_settings_list it types, and its
     * revision as the page last followed it. */
    ui_keypad_t kp;             /**< TIMING's keypad                    */
    int         kp_idx;         /**< the row of sp_settings_list it types */
    uint32_t    kp_rev;         /**< its revision as the page last
                                     followed it                        */

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
    /** @brief MANUAL INTERVENTION REQUIRED on a run's result: in the
     *         foot row, left of OK; hand_btn is its place on the
     *         profile's page. */
    gfx_rect_t hand_res;
} stick_t;

/** @brief The panel's state: the stick class's, and the phase tap's. */
typedef struct {
    stick_t  st;

    /* The phase tap, as the control task last read it. */
    tone_readout_t tone;
} sp_state_t;
/** @brief The state, esc_stick_s, in esc_stick_panel.c, so its .bss stays
 *         where psram_bss.lf puts esc_stick_panel's.  The files read it
 *         as s, the name it had as the one file's static: the object's
 *         own name keeps it apart from the SERVO screen's s. */
extern sp_state_t esc_stick_s;
#define s esc_stick_s

/* --- the functions one file calls of another ---------------------------- */

/* esc_stick_panel.c */

/**
 * @brief The picks as changes, in item order.
 * @param ch the changes, ESC_STICK_MAX_CHANGES of them at most
 * @param picked how many were picked in all
 * @return the changes written to @p ch
 */
size_t sp_changes(esc_stick_change_t *ch, size_t *picked);
/** @brief The timing as the settings hold it, the profile's own entry
 *         where it states one. */
void sp_timing(esc_stick_timing_t *t);
/** @brief The supply's set points for a run, mV and mA: VOLTAGE, or the
 *         profile's cell count when it is 0. */
void sp_supply(uint32_t *mv, uint32_t *ma);
/** @brief Whether the supply reads off now, by the rule a run holds the
 *         supply to. */
bool sp_supply_reads_off(void);
/** @brief Whether the warning asks a step at an unpowered ESC of the
 *         operator and may not, as the supply does not read off. */
bool sp_warn_gated(void);
/** @brief Whether the warning shows every before-power step of @p p
 *         whole. */
bool sp_warn_steps_fit(const esc_profile_t *p);
/** @brief The steps of @p p the run stops for after the warning's. */
unsigned sp_later_steps(const esc_profile_t *p);
/** @brief Whether HOLD TO RUN may not count. */
bool sp_hold_blocked(void);
/** @brief Whether a run can start now. */
bool sp_can_run(void);
/** @brief What the run asks for, onto the paths an operator's own presses
 *         take. */
void sp_follow(void);
/** @brief The warning's hold dropped, with nothing started. */
void sp_end_hold(void);
/** @brief The run ends now as @p why; nothing it queued survives. */
void sp_end_run(esc_stick_reason_t why);

/* esc_stick_list.c */

/** @brief @p text at (@p x, @p y) cut to @p max_chars, with ".." in place
 *         of what did not fit. */
void sp_text(gfx_canvas_t *c, int x, int y, const char *text, int max_chars,
             gfx_color_t ink);
/** @brief What a manual step asks, in the language showing. */
const char *sp_action(const esc_manual_t *m);
/** @brief When a manual step is due, in the language showing. */
const char *sp_when_text(const esc_manual_t *m);
/** @brief The engine's reason @p why in the language showing. */
const char *sp_why_text(const char *why);
/** @brief What the supply's set points break of a model's ratings, into
 *         @p buf, or NULL: the short form for a row, @p longer for the
 *         page's note. */
const char *sp_rating_why(const esc_profile_t *p, int model, uint32_t mv,
                          uint32_t ma, bool by_hand, bool longer, char *buf,
                          size_t n);
/** @brief The warning for a voltage the data cannot vouch for, into
 *         @p buf, or NULL; the run goes ahead. */
const char *sp_rating_warn(const esc_profile_t *p, int model, uint32_t mv,
                           bool by_hand, bool longer, char *buf, size_t n);
/** @brief sp_rating_warn() for model @p model with the settings now. */
const char *sp_model_warn(const esc_profile_t *p, int model, char *buf,
                          size_t n);
/** @brief Why model @p model cannot run, into @p buf, or NULL when it
 *         can. */
const char *sp_model_why(const esc_profile_t *p, int model, char *buf,
                         size_t n);
/** @brief VOLTAGE and the SUPPLY cap, mV: what decides from outside a
 *         profile whether it runs. */
uint32_t sp_list_key(void);
/** @brief Whether model @p model of @p p runs on this bench. */
bool sp_model_runs(const esc_profile_t *p, int model);
/** @brief The level showing built again, from the top or, with
 *         @p keep_scroll, where it was. */
void sp_build_list(bool keep_scroll);
/** @brief Profile @p p picked: its picks cleared, and its manual steps
 *         shown the first time it is opened. */
void sp_pick_profile(const esc_profile_t *p);
/** @brief The items the page offers. */
int sp_items(void);
/** @brief The profile's index of the @p at-th item offered, or -1. */
int sp_item_at(int at);
/** @brief The picked item kept among those shown; the first otherwise. */
void sp_pick_shown(void);

/* esc_stick_touch.c */

/** @brief The rows a page of ROWS_MAX shows of @p total from @p scroll. */
int sp_rows_shown(int total, int scroll);
/** @brief Profile row @p i, narrowed to the left of the keyboard while it
 *         is open. */
gfx_rect_t sp_row(int i);
/** @brief The keyboard closes; the search stays as typed. */
void sp_find_close(void);
/** @brief TIMING's keypad closes, with nothing typed taken. */
void sp_kp_close(void);

/* esc_stick_draw.c */

/** @brief The entry time the page shows, ms. */
uint32_t sp_entry_shown(void);

/* esc_stick_draw_run.c */

/** @brief The red light: a result on screen, until OK, of a run that ended
 *         on something not as expected. */
bool sp_red(void);
/** @brief Into @p buf: the result's count of the selections made for
 *         certain. */
void sp_result_head(const esc_stick_t *e, char *buf, size_t n);
/** @brief Into @p buf: the changes past the first @p shown, as the
 *         result's last line says them. */
void sp_result_more(const esc_stick_t *e, unsigned shown, char *buf,
                    size_t n);
/** @brief What a run waiting for a manual step asks of the operator. */
const char *sp_left_text(const esc_stick_t *e);
/** @brief A run under way drawn. */
void sp_draw_progress(gfx_canvas_t *c);
/** @brief A run's result drawn. */
void sp_draw_result(gfx_canvas_t *c);
/** @brief The RUN warning drawn. */
void sp_draw_warning(gfx_canvas_t *c);
/** @brief A profile's manual steps drawn. */
void sp_draw_hand(gfx_canvas_t *c);
/** @brief A run's prompt for a manual step drawn. */
void sp_draw_prompt(gfx_canvas_t *c);
