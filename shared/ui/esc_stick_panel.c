/*
 * ESC STICK, the session MOTOR & ESC > PROGRAM opens: an ESC (electronic
 * speed controller) with no wire to talk on, programmed through its
 * throttle-stick menu.  Its list is the ESC profiles (esc_profile.h), its
 * parameters are the profile's menu items, and its write is a run of the
 * stick programmer (esc_stick.h): the bench arms, holds the throttle at the
 * entry position, switches the supply on and counts the beeps in the
 * supply current.  A run starts only from a red warning held for two
 * seconds, as ARM and the servo's HV warning are, and every move it makes
 * goes out as the MOTOR & ESC screen's commands do, through the arming
 * policy and the output bank.
 *
 * The profile list has two levels: the makers, alphabetical, and one
 * maker's models, by current, voltage and name, each opening its family's
 * profile.  A search field filters both.  Its keyboard docks on the right
 * while it is open and the rows narrow to its left, so the list filters
 * with every key and stays in view.  A run shows a stack light: green while
 * the detector holds a beep, red on a result that ended because something
 * was not as expected (esc_stick_reason_is_fault()).
 *
 * A profile whose ESC needs a person at it -- a jumper, a button -- lists
 * those steps (esc_profile_t's manual).  Its page carries MANUAL
 * INTERVENTION REQUIRED, which shows them over the screen, as does the
 * first opening; the warning lists those due before the power-up, and a
 * run that waits for one covers the page with DONE and ABORT.
 *
 * The pages draw on the session's layout, the body under MOTOR & ESC's
 * output strip, and take their controls through the session's tap walk
 * (esc_stick_int.h, esc_stick_pages.c).  The interface is
 * esc_stick_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esc_profile.h"
#include "kst_panel.h"
#include "esc_stick_int.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

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

/* The settings the TIMING page shows, in its order. */
static const setting_id_t k_sp_settings[] = {
    SET_STICK_V, SET_STICK_I, SET_STICK_BEEP_MIN, SET_STICK_GAP_MIN,
    SET_STICK_LONG, SET_STICK_LONG_MAX, SET_STICK_GROUP_GAP, SET_STICK_ENTRY,
    SET_STICK_STORE, SET_STICK_OFF, SET_STICK_SILENCE, SET_STICK_TIMEOUT,
    SET_STICK_THRESHOLD, SET_STICK_HYST,
};
#define SP_SETTINGS ((int)(sizeof(k_sp_settings) / sizeof(k_sp_settings[0])))

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

/* --------------------------------------------------------------- the state */

static struct {
    stick_t  st;

    /* The phase tap, as the control task last read it. */
    tone_readout_t tone;
} s;

static bool hit_down(gfx_rect_t r, int px, int py)
{
    return pg.pass == PASS_PRESS && !pg.found && gfx_rect_contains(r, px, py);
}

void esc_stick_panel_reset(void)
{
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < SP_ROWS; ++i) {
        s.st.rows[i] = (gfx_rect_t){ (int16_t)(PAD + 12),
                                     (int16_t)(SP_ROW_Y0 + i * SP_ROW_H),
                                     (int16_t)(W - 2 * PAD - 24),
                                     SP_ROW_H - 4 };
    }
    s.st.find_box = (gfx_rect_t){ SP_FIND_X, CRUMB_Y, SP_FIND_W, 30 };
    s.st.find_clr = (gfx_rect_t){ (int16_t)(SP_FIND_X + SP_FIND_W - 30),
                                  (int16_t)(CRUMB_Y + 2), 28, 26 };
    s.st.list_up = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76), CRUMB_Y,
                                 STEP_W, 30 };
    s.st.list_dn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38), CRUMB_Y,
                                 STEP_W, 30 };
    s.st.hold_btn   = (gfx_rect_t){ PAD + 20, SP_PBTN_Y, 260, SP_PBTN_H };
    s.st.cancel_btn = (gfx_rect_t){ (int16_t)(W - PAD - 20 - 180), SP_PBTN_Y,
                                    180, SP_PBTN_H };
    /* Between HOLD TO RUN and CANCEL, on the warning. */
    s.st.steps_btn  = (gfx_rect_t){ PAD + 20 + 260 + 24, SP_PBTN_Y, 266,
                                    SP_PBTN_H };
    /* In the item list's header, between CHANGE and the count. */
    s.st.hand_btn   = (gfx_rect_t){ PAD + 108, (int16_t)(PARM_Y + 4), 300,
                                    24 };
    /* In the profile page's crumb row, at its right, so the card's line
     * of how the menu works has the card's width: CONNECT's size, level
     * with BACK's middle. */
    s.st.timing_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                    (int16_t)(CRUMB_Y - 3), 168, 36 };
}

static void sp_find_close(void);

/* ============================================== the stick class ======== */

/*
 * A text cut to @p max_chars, with ".." in place of what did not fit: the
 * profiles' names are the manuals' and run long.
 */
static void sp_text(gfx_canvas_t *c, int x, int y, const char *text,
                    int max_chars, gfx_color_t ink)
{
    char buf[192];
    sp_copy(buf, sizeof(buf), text);
    sp_cut(buf, sizeof(buf), max_chars);
    gfx_text(c, x, y, buf, UI_FONT_LABEL, ink, 1);
}

int esc_stick_panel_wrap_lines(const char *text, int cells)
{
    return sp_wrap_lines(text, cells, INT_MAX);
}

/* A manual step in the language showing: the profile's German where it
 * gives one and German shows, else its English. */
const char *esc_stick_panel_step_text(const esc_manual_t *m)
{
    if (m == NULL) {
        return "";
    }
    if (ui_text_language() == UI_LANG_DE && m->action_de != NULL
        && m->action_de[0] != '\0') {
        return m->action_de;
    }
    return (m->action != NULL) ? m->action : "";
}

static const char *sp_action(const esc_manual_t *m)
{
    return esc_stick_panel_step_text(m);
}

/* When a manual step is due, in the language showing. */
static const char *sp_when_text(const esc_manual_t *m)
{
    static char buf[96];
    switch (m->when) {
    case ESC_MANUAL_BEFORE_POWER:
        return TR(SP_HAND_WHEN_BEFORE_POWER);
    case ESC_MANUAL_AT_POWER_UP:
        if (m->hold_ms == 0u) {
            return TR(SP_HAND_WHEN_AT_POWER);
        }
        snprintf(buf, sizeof(buf), TR(SP_HAND_WHEN_AT_POWER_MS),
                 (unsigned)(m->hold_ms / 1000u),
                 (unsigned)(m->hold_ms % 1000u / 100u));
        return buf;
    case ESC_MANUAL_BEFORE_MENU:
        return TR(SP_HAND_WHEN_BEFORE_MENU);
    case ESC_MANUAL_DURING_MENU:
        return TR(SP_HAND_WHEN_DURING_MENU);
    case ESC_MANUAL_BEFORE_POWER_OFF:
        return TR(SP_HAND_WHEN_BEFORE_OFF);
    case ESC_MANUAL_AFTER_PROGRAMMING:
        return TR(SP_HAND_WHEN_AFTER);
    }
    return "?";
}

/*
 * The stick engine's refusals are English, held where the engine writes
 * them (esc_stick.c); the screen shows each in the language showing by
 * matching the engine's words.  One the table does not know shows as the
 * engine wrote it.
 */
static const struct {
    const char *engine;
    ui_text_id_t text;
} k_why[] = {
    { "no profile", TX_ESC_WHY_NO_PROFILE },
    { "needs a person at the ESC", TX_ESC_WHY_PERSON },
    { "no usable procedure", TX_ESC_WHY_NO_PROCEDURE },
    { "entered after power-on", TX_ESC_WHY_AFTER_POWER },
    { "melody menu", TX_ESC_WHY_MELODY },
    { "yes/no menu", TX_ESC_WHY_YES_NO },
    { "stick-position menu", TX_ESC_WHY_POSITION },
    { "menu of its own kind", TX_ESC_WHY_OWN_KIND },
    { "tones told apart by pitch", TX_ESC_WHY_PITCH },
    { "no items", TX_ESC_WHY_NO_ITEMS },
    { "no select move", TX_ESC_WHY_NO_SELECT },
    { "rest move, no entry time", TX_ESC_WHY_REST_ENTRY },
    { "store move, two stages", TX_ESC_WHY_STORE_TWO },
    { "two moves, no value tones", TX_ESC_WHY_TWO_MOVES },
    { "select move is the rest", TX_ESC_WHY_SELECT_REST },
    { "value move = select move", TX_ESC_WHY_VALUE_SELECT },
    { "item and value, one move", TX_ESC_WHY_ONE_MOVE },
    { "many changes, one stage", TX_ESC_WHY_MANY_ONE },
    { "items counted, one stage", TX_ESC_WHY_COUNTED_ONE },
    { "values repeat across items", TX_ESC_WHY_REPEAT },
    { "store move = select move", TX_ESC_WHY_STORE_SELECT },
    { "no timing", TX_ESC_WHY_NO_TIMING },
    { "nothing to change", TX_ESC_WHY_NOTHING },
    { "too many changes", TX_ESC_WHY_TOO_MANY },
    { "no such item", TX_ESC_WHY_NO_ITEM },
    { "reset and exit are actions, not settings", TX_ESC_WHY_ACTIONS },
    { "no such value", TX_ESC_WHY_NO_VALUE },
    { "value 0 is not sounded", TX_ESC_WHY_ZERO },
    { "one change per item", TX_ESC_WHY_ONE_PER_ITEM },
    { "BEEP MIN and GAP MIN above 0", TX_ESC_WHY_BEEP_GAP },
    { "LONG above BEEP MIN", TX_ESC_WHY_LONG },
    { "LONG MAX above LONG", TX_ESC_WHY_LONG_MAX },
    { "GROUP GAP above GAP MIN", TX_ESC_WHY_GROUP_GAP },
    { "THRESHOLD above HYSTERESIS", TX_ESC_WHY_THRESHOLD },
    { "ENTRY above 500 ms", TX_ESC_WHY_ENTRY },
    { "GROUP GAP misses the select window", TX_ESC_WHY_SELECT_WINDOW },
    { "GROUP GAP misses the value window", TX_ESC_WHY_VALUE_WINDOW },
    { "no run", TX_ESC_WHY_NO_RUN },
    { "one value: nothing to choose", TX_ESC_WHY_ONE_VALUE },
    { "manual step", TX_ESC_WHY_HAND },
    { "changes need different power-up positions", TX_ESC_WHY_ENTRY_POS },
    { "changes need different entry times", TX_ESC_WHY_ENTRY_TIME },
    { "moves after the value, two stages", TX_ESC_WHY_AFTER_TWO },
    { "a move after the value makes no move", TX_ESC_WHY_AFTER_NONE },
    { "menu start, rest elsewhere", TX_ESC_WHY_MENU_REST },
};

const char *esc_stick_panel_why_text(const char *why)
{
    if (why == NULL) {
        return TR(SP_REFUSED);
    }
    for (size_t i = 0; i < sizeof(k_why) / sizeof(k_why[0]); ++i) {
        if (strcmp(why, k_why[i].engine) == 0) {
            return ui_tr(k_why[i].text);
        }
    }
    return why;
}

static const char *sp_why_text(const char *why)
{
    return esc_stick_panel_why_text(why);
}

/*
 * What the supply's set points break of model @p model's ratings
 * (esc_stick_rating()), into @p buf, or NULL: the short form for a row,
 * the long one for the page's note.
 */
static const char *sp_rating_why(const esc_profile_t *p, int model,
                                 uint32_t mv, uint32_t ma, bool by_hand,
                                 bool longer, char *buf, size_t n)
{
    const uint32_t v_max = esc_stick_model_v_max(p, model);
    const uint32_t ma_max = esc_stick_model_ma_max(p, model);
    switch (esc_stick_rating(p, model, mv, ma, by_hand)) {
    case ESC_STICK_RATING_V_OVER:
        snprintf(buf, n,
                 longer ? (by_hand ? TR(SP_V_OVER_ESC_NOTE)
                                   : TR(SP_V_CELLS_OVER_ESC_NOTE))
                        : TR(SP_V_OVER_ESC),
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u),
                 (unsigned)(v_max / 1000u),
                 (unsigned)(v_max % 1000u / 100u));
        return buf;
    case ESC_STICK_RATING_V_UNDER: {
        const uint32_t v_min = esc_stick_model_v_min(p, model);
        snprintf(buf, n, longer ? TR(SP_V_UNDER_ESC_NOTE) : TR(SP_V_UNDER_ESC),
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u),
                 (unsigned)(v_min / 1000u),
                 (unsigned)(v_min % 1000u / 100u));
        return buf;
    }
    case ESC_STICK_RATING_I_OVER:
        snprintf(buf, n, longer ? TR(SP_I_OVER_ESC_NOTE) : TR(SP_I_OVER_ESC),
                 (unsigned)(ma / 1000u), (unsigned)(ma % 1000u / 100u),
                 (unsigned)(ma_max / 1000u));
        return buf;
    case ESC_STICK_RATING_OK:
    default:
        return NULL;
    }
}

/*
 * The warning for a voltage the data cannot vouch for
 * (esc_stick_rating_unknown()), into @p buf, or NULL: the short form for a
 * row, the long one for the page's note.  The run goes ahead.
 */
static const char *sp_rating_warn(const esc_profile_t *p, int model,
                                  uint32_t mv, bool by_hand, bool longer,
                                  char *buf, size_t n)
{
    if (!esc_stick_rating_unknown(p, model, mv, by_hand)) {
        return NULL;
    }
    if (!longer) {
        snprintf(buf, n, "%s", TR(SP_V_UNRATED));
    } else if (by_hand) {
        snprintf(buf, n, TR(SP_V_UNRATED_NOTE),
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u));
    } else {
        snprintf(buf, n, TR(SP_V_UNRATED_CELLS_NOTE),
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u));
    }
    return buf;
}

/* The same for the model on a row, or the page's, with the settings now. */
static const char *sp_model_warn(const esc_profile_t *p, int model, char *buf,
                                 size_t n)
{
    const float v = settings_get(SET_STICK_V);
    const uint32_t mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                                   : esc_stick_model_mv(p, model);
    return sp_rating_warn(p, model, mv, v > 0.0f, false, buf, n);
}

/*
 * Why model @p model of a profile cannot run, into @p buf, or NULL when it
 * can: the engine's reason, a voltage or current over the ESC's rating,
 * or a voltage the supply cannot give -- VOLTAGE where it is set, else
 * the model's cell count, or with no model (-1) or none stated the
 * family's lowest.
 */
static const char *sp_model_why(const esc_profile_t *p, int model, char *buf,
                                size_t n)
{
    const char *why = NULL;
    if (esc_stick_kind(p, &why) == ESC_STICK_KIND_NONE) {
        return sp_why_text(why);
    }
    const float v = settings_get(SET_STICK_V);
    const uint32_t mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                                   : esc_stick_model_mv(p, model);
    const uint32_t ma = (uint32_t)lroundf(settings_get(SET_STICK_I)
                                          * 1000.0f);
    if (sp_rating_why(p, model, mv, ma, v > 0.0f, false, buf, n) != NULL) {
        return buf;
    }
    const unsigned cap = (unsigned)lroundf(supply_screen_caps().v_max
                                           * 1000.0f);
    if (mv > cap) {
        snprintf(buf, n, TR(SP_NEEDS_V),
                 (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u),
                 cap / 1000u, cap % 1000u / 100u);
        return buf;
    }
    return NULL;
}

/* The two things outside the profile that decide whether it runs: VOLTAGE
 * and the SUPPLY cap, mV. */
static uint32_t sp_list_key(void)
{
    const uint32_t v = (uint32_t)lroundf(settings_get(SET_STICK_V) * 1000.0f);
    const uint32_t cap = (uint32_t)lroundf(supply_screen_caps().v_max
                                           * 1000.0f);
    return (v << 16) ^ cap;
}

/* @p a against @p b with the letters A to Z folded, as the search folds
 * them: the makers' order and their grouping. */
static int sp_casecmp(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        int x = (unsigned char)*a;
        int y = (unsigned char)*b;
        x -= (x >= 'a' && x <= 'z') ? 32 : 0;
        y -= (y >= 'a' && y <= 'z') ? 32 : 0;
        if (x != y || x == 0) {
            return x - y;
        }
    }
}

static bool sp_model_runs(const esc_profile_t *p, int model)
{
    char buf[40];
    return sp_model_why(p, model, buf, sizeof(buf)) == NULL;
}

/*
 * The first level: every maker, alphabetical with case folded, that the
 * search finds a model of (esc_model_matches(); every one while it is
 * empty), with its models, those found and those of them that run.  A
 * card profile joins its maker as a built-in one does.
 */
static void sp_build_makers(stick_t *t)
{
    int n = 0;
    const size_t total = esc_profiles_count();
    for (size_t i = 0; i < total; ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        int at = 0;
        while (at < n && sp_casecmp(t->makers[at].name, p->brand) < 0) {
            ++at;
        }
        if (at == n || sp_casecmp(t->makers[at].name, p->brand) != 0) {
            if (n >= SP_MAX) {
                continue;
            }
            memmove(&t->makers[at + 1], &t->makers[at],
                    (size_t)(n - at) * sizeof(t->makers[0]));
            t->makers[at] = (sp_maker_t){ p->brand, 0u, 0u, 0u, false };
            ++n;
        }
        sp_maker_t *mk = &t->makers[at];
        for (unsigned m = 0; m < p->model_count; ++m) {
            mk->models++;
            if (!esc_model_matches(p, m, t->find)) {
                continue;
            }
            mk->found++;
            if (sp_model_runs(p, (int)m)) {
                mk->runs++;
            }
            mk->manual = mk->manual || p->manual_count > 0u;
        }
    }
    int k = 0;
    t->runnable = 0;
    for (int i = 0; i < n; ++i) {
        if (t->makers[i].found > 0u) {
            t->runnable += (t->makers[i].runs > 0u) ? 1 : 0;
            t->makers[k++] = t->makers[i];
        }
    }
    t->count = k;
}

/* One key of the models' order: smaller first, 0 -- not stated -- last. */
static int sp_key_cmp(uint32_t a, uint32_t b)
{
    if (a == b) {
        return 0;
    }
    if (a == 0u || b == 0u) {
        return (a == 0u) ? 1 : -1;
    }
    return (a < b) ? -1 : 1;
}

/* Current, then voltage, then name with case folded; a key a model does
 * not state sorts it last on that key.  Ties keep the registry's order. */
static int sp_by_model(const void *a, const void *b)
{
    const sp_row_t *x = a, *y = b;
    const esc_model_t *mx = &esc_profiles_at(x->prof)->models[x->model];
    const esc_model_t *my = &esc_profiles_at(y->prof)->models[y->model];
    int c = sp_key_cmp(mx->current_a, my->current_a);
    if (c == 0) {
        c = sp_key_cmp(mx->v_max_mv, my->v_max_mv);
    }
    if (c == 0) {
        c = sp_casecmp(mx->name, my->name);
    }
    if (c == 0) {
        c = (x->prof != y->prof) ? ((x->prof < y->prof) ? -1 : 1)
                                 : ((x->model < y->model) ? -1 : 1);
    }
    return c;
}

/*
 * The second level: the open maker's models the search finds, one row a
 * model, by current, voltage and name; each row is its family's profile.
 * The registry holds a maker to ESC_MAKER_MODELS_MAX, SP_MODELS, so every
 * one of them has a row.
 */
static void sp_build_models(stick_t *t)
{
    int n = 0;
    const size_t total = esc_profiles_count();
    for (size_t i = 0; i < total && t->maker != NULL; ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        if (sp_casecmp(p->brand, t->maker) != 0) {
            continue;
        }
        for (unsigned m = 0; m < p->model_count && n < SP_MODELS; ++m) {
            if (esc_model_matches(p, m, t->find)) {
                t->rows_m[n++] = (sp_row_t){ (uint16_t)i, (uint16_t)m };
            }
        }
    }
    qsort(t->rows_m, (size_t)n, sizeof(t->rows_m[0]), sp_by_model);
    t->runnable = 0;
    for (int i = 0; i < n; ++i) {
        t->runnable += sp_model_runs(esc_profiles_at(t->rows_m[i].prof),
                                     t->rows_m[i].model) ? 1 : 0;
    }
    t->count = n;
}

/*
 * The level showing, built again whenever the list comes back on screen,
 * the search changes, or VOLTAGE or the SUPPLY cap moves under it, so the
 * counts follow the reasons the rows draw.
 */
static void sp_build_list(bool keep_scroll)
{
    stick_t *t = &s.st;
    ++t->builds;            /* the rows under a finger may be others */
    const int scroll = keep_scroll ? t->scroll : 0;
    t->built_key = sp_list_key();
    if (t->level == 1) {
        sp_build_models(t);
    } else {
        sp_build_makers(t);
    }
    t->scroll = (scroll < t->count) ? scroll : 0;
}

void esc_stick_panel_enter(void)
{
    pg.tap.live = false;
    if (pg.stage != STAGE_CLOSED) {
        sp_build_list(true);
        ++pg.rev;
    }
}

void esc_stick_panel_open(void)
{
    stick_t *t = &s.st;
    pg.tap.live = false;
    pg.stage = STAGE_LIST;
    t->level = 0;
    t->maker = NULL;
    t->shown = false;
    t->timing = false;
    t->bscroll = 0;
    sp_build_list(false);
    ++pg.rev;
}

static void sp_pick_shown(void);

static void sp_pick_profile(const esc_profile_t *p)
{
    stick_t *t = &s.st;
    t->p = p;
    for (int i = 0; i < 256; ++i) {
        t->pick[i] = -1;
    }
    t->iscroll = 0;
    t->picked = 0;
    t->shown = false;
    t->timing = false;
    t->warn = false;
    t->warn_down = false;
    ui_hold_reset(&t->hold);
    t->hand_open = false;
    t->model = -1;
    sp_pick_shown();
    /* A profile with manual steps says so by itself the first time it is
     * opened; MANUAL INTERVENTION REQUIRED shows it again. */
    if (p != NULL && p->manual_count > 0u) {
        const size_t total = esc_profiles_count();
        for (size_t i = 0; i < total && i < SP_MAX; ++i) {
            if (esc_profiles_at(i) != p) {
                continue;
            }
            const uint8_t bit = (uint8_t)(1u << (i & 7u));
            if ((t->hand_seen[i >> 3] & bit) == 0u) {
                t->hand_seen[i >> 3] |= bit;
                t->hand_open = true;
                t->hand_p = p;
                t->hand_model = -1;     /* the caller names the model */
            }
            break;
        }
    }
}

/*
 * The items the page offers, as indices into the profile's, in its order:
 * those on the model picked (esc_item_applies()); with no model, those on
 * every model.  sp_items() counts them; sp_item_at() is the profile's index
 * of the @p at-th, or -1.  Walked each time rather than kept in a list,
 * which would put 256 bytes on the stack of every caller, the page's drawer
 * among them.
 */
static int sp_items(void)
{
    const stick_t *t = &s.st;
    int n = 0;
    for (unsigned i = 0; t->p != NULL && i < t->p->item_count; ++i) {
        if (esc_item_applies(t->p, i, t->model)) {
            ++n;
        }
    }
    return n;
}

static int sp_item_at(int at)
{
    const stick_t *t = &s.st;
    int n = 0;
    for (unsigned i = 0; t->p != NULL && i < t->p->item_count; ++i) {
        if (esc_item_applies(t->p, i, t->model) && n++ == at) {
            return (int)i;
        }
    }
    return -1;
}

/* The picked item kept among those shown: the first shown otherwise. */
static void sp_pick_shown(void)
{
    stick_t *t = &s.st;
    if (t->p == NULL
        || esc_item_applies(t->p, (unsigned)t->picked, t->model)) {
        return;
    }
    const int first = sp_item_at(0);
    t->picked = (first >= 0) ? first : 0;
}

/* The picks as changes, in item order; how many were picked in all. */
static size_t sp_changes(esc_stick_change_t *ch, size_t *picked)
{
    const stick_t *t = &s.st;
    size_t n = 0, all = 0;
    for (unsigned i = 0; t->p != NULL && i < t->p->item_count; ++i) {
        if (t->pick[i] < 0) {
            continue;
        }
        ++all;
        if (n < ESC_STICK_MAX_CHANGES) {
            ch[n].item = (uint8_t)i;
            ch[n].value = (uint8_t)t->pick[i];
            ++n;
        }
    }
    *picked = all;
    return n;
}

/* The timing as the settings hold it.  The entry is the profile's own where
 * it states one: the manual's number, not the bench's guess. */
static void sp_timing(esc_stick_timing_t *t)
{
    const esc_profile_t *p = s.st.p;
    t->beep_min_ms   = (uint32_t)settings_get_int(SET_STICK_BEEP_MIN);
    t->gap_min_ms    = (uint32_t)settings_get_int(SET_STICK_GAP_MIN);
    t->long_ms       = (uint32_t)settings_get_int(SET_STICK_LONG);
    t->long_max_ms   = (uint32_t)settings_get_int(SET_STICK_LONG_MAX);
    t->group_gap_ms  = (uint32_t)settings_get_int(SET_STICK_GROUP_GAP);
    t->entry_ms      = (p != NULL && p->entry_hold_ms != 0u)
                           ? p->entry_hold_ms
                           : (uint32_t)settings_get_int(SET_STICK_ENTRY);
    t->store_ms      = (uint32_t)settings_get_int(SET_STICK_STORE);
    t->off_ms        = (uint32_t)settings_get_int(SET_STICK_OFF);
    t->silence_ms    = (uint32_t)settings_get_int(SET_STICK_SILENCE);
    t->timeout_ms    = (uint32_t)settings_get_int(SET_STICK_TIMEOUT);
    t->threshold_ma  = (uint32_t)settings_get_int(SET_STICK_THRESHOLD);
    t->hysteresis_ma = (uint32_t)settings_get_int(SET_STICK_HYST);
}

/* The supply's set points for a run, mV and mA: VOLTAGE, or the profile's
 * cell count when it is 0. */
static void sp_supply(uint32_t *mv, uint32_t *ma)
{
    const float v = settings_get(SET_STICK_V);
    *mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                     : esc_stick_model_mv(s.st.p, s.st.model);
    *ma = (uint32_t)lroundf(settings_get(SET_STICK_I) * 1000.0f);
}

/*
 * Everything a run needs, and whether it can start.  When it cannot, the
 * note says why; with nothing picked the note stays empty and RUN is simply
 * not offered.  Refused, never adjusted: a voltage over the SUPPLY cap is
 * the operator's to change.
 */
static bool sp_supply_reads_off(void);
static bool sp_warn_steps_fit(const esc_profile_t *p);

/*
 * Why the bench cannot carry a run, or NULL: the supply is the panel's
 * model and switches nothing, the output binding is not read, or it binds
 * no pin as a throttle.  The stick has no pin to move then, or the ESC no
 * supply to be powered from.
 */
static const char *sp_bench_why(void)
{
    const stick_t *t = &s.st;
    if (supply_screen_model()) {
        return TR(SP_SUPPLY_MODEL);
    }
    if (!t->bind_read) {
        return TR(SP_BIND_NOT_READ);
    }
    if (t->throttle_mask == 0u) {
        return TR(SP_NO_THROTTLE);
    }
    return NULL;
}

static bool sp_plan(esc_stick_change_t *ch, size_t *n,
                    esc_stick_timing_t *t, uint32_t *mv, uint32_t *ma)
{
    stick_t *st = &s.st;
    st->note[0] = '\0';
    size_t picked = 0;
    *n = sp_changes(ch, &picked);
    sp_timing(t);
    sp_supply(mv, ma);
    if (st->p == NULL || picked == 0u) {
        return false;
    }
    if (picked > ESC_STICK_MAX_CHANGES) {
        snprintf(st->note, sizeof(st->note), TR(SP_AT_MOST),
                 (unsigned)ESC_STICK_MAX_CHANGES);
        return false;
    }
    /* The ESC's own ratings, for the model picked: a set point the data
     * cannot vouch for is refused, never sent. */
    if (sp_rating_why(st->p, st->model, *mv, *ma,
                      settings_get(SET_STICK_V) > 0.0f, true, st->note,
                      sizeof(st->note)) != NULL) {
        return false;
    }
    if (*mv == 0u) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_NO_CELLS));
        return false;
    }
    /* Every change on the model picked: an item another model has may
     * share its number and values with one this model has, and the run
     * would change that one instead. */
    for (size_t i = 0; i < *n; ++i) {
        if (!esc_item_applies(st->p, ch[i].item, st->model)) {
            const esc_item_t *it = &st->p->items[ch[i].item];
            snprintf(st->note, sizeof(st->note), TR(SP_ITEM_NOT_HERE),
                     (unsigned)it->number, it->name);
            return false;
        }
    }
    const supply_caps_t caps = supply_screen_caps();
    const unsigned vmax = (unsigned)lroundf(caps.v_max * 1000.0f);
    const unsigned vmin = (unsigned)lroundf(caps.v_min * 1000.0f);
    const unsigned imax = (unsigned)lroundf(caps.i_max * 1000.0f);
    if (*mv > vmax) {
        snprintf(st->note, sizeof(st->note), TR(SP_ABOVE_CAP),
                 (unsigned)(*mv / 1000u), (unsigned)(*mv % 1000u / 10u),
                 vmax / 1000u, vmax % 1000u / 10u);
        return false;
    }
    if (*mv < vmin) {
        snprintf(st->note, sizeof(st->note), TR(SP_BELOW_MIN),
                 vmin / 1000u, vmin % 1000u / 10u);
        return false;
    }
    if (*ma > imax) {
        snprintf(st->note, sizeof(st->note), TR(SP_I_ABOVE_CAP),
                 imax / 1000u, imax % 1000u / 10u);
        return false;
    }
    const char *bench = sp_bench_why();
    if (bench != NULL) {
        snprintf(st->note, sizeof(st->note), "%s", bench);
        return false;
    }
    /* Asked on, or read live: the output on, or current through it. */
    if (supply_screen_output_live() || st->sup_live) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_OUTPUT_LIVE));
        return false;
    }
    /* No reading, an old one, or a supply that does not answer is not a
     * supply known off: a run moves the stick and may ask for a hand at
     * the ESC on that word. */
    if (!sp_supply_reads_off()) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_SUPPLY_NOT_OFF));
        return false;
    }
    /* A run arms the bench, and programming a servo and an armed bench
     * are exclusive. */
    if (kst_panel_holds_port()) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_KST_HELD));
        return false;
    }
    const char *why = NULL;
    if (!esc_stick_check(st->p, ch, *n, t, &why)) {
        snprintf(st->note, sizeof(st->note), "%s", sp_why_text(why));
        return false;
    }
    /* It runs; a voltage the data cannot vouch for is said, not refused. */
    (void)sp_rating_warn(st->p, st->model, *mv,
                         settings_get(SET_STICK_V) > 0.0f, true, st->note,
                         sizeof(st->note));
    return true;
}

/*
 * Whether the supply reads off now: a reading no older than
 * ESC_STICK_STALE_MS in which the supply itself reports its output off
 * with the current at or under ESC_STICK_OFF_MA, and has for
 * ESC_STICK_OFF_SETTLE_MS -- the rule a run holds the supply to.  The
 * warning shows a step at an unpowered ESC only then.
 */
static bool sp_supply_reads_off(void)
{
    const stick_t *t = &s.st;
    if (!t->sup_have || !t->sup_off_known || supply_screen_output_live()) {
        return false;
    }
    const int32_t age = (int32_t)(t->now_ms - t->sup_at);
    const uint32_t held = t->sup_at - t->sup_off_since;
    return age <= (int32_t)ESC_STICK_STALE_MS
           && held >= ESC_STICK_OFF_SETTLE_MS;
}

/* Whether the warning asks a step at an unpowered ESC of the operator and
 * may not, as the supply does not read off. */
static bool sp_warn_gated(void)
{
    const stick_t *t = &s.st;
    return t->p != NULL
           && esc_profile_manual_count(t->p, ESC_MANUAL_BEFORE_POWER) > 0u
           && !sp_supply_reads_off();
}

/*
 * Whether the warning shows every before-power step whole: under its
 * lines (sp_draw_warning(): five, the profile, the supply and the
 * unverified line from the panel's top + 68 at 22 px, then the steps'
 * label), each
 * step wrapped to two lines, and room kept for the line about later steps,
 * above HOLD TO RUN.
 */
static unsigned sp_later_steps(const esc_profile_t *p);

static bool sp_warn_steps_fit(const esc_profile_t *p)
{
    if (p == NULL || esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER)
                         == 0u) {
        return true;
    }
    const int last = s.st.hold_btn.y - 20;
    const unsigned later = sp_later_steps(p);
    const int keep = (later > 0u) ? 22 : 0;
    int y = SP_PANEL_Y + 68 + 5 * 22 + 66 + 22;
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_BEFORE_POWER) {
            const int need = sp_wrap_lines(sp_action(m), 90, 2);
            if (y + (need - 1) * 22 > last - keep) {
                return false;
            }
            y += 22 * need;
        }
    }
    return true;
}

/* The steps the run stops for on its way, after the warning's: at the
 * power-up, before the menu, and before the supply goes off. */
static unsigned sp_later_steps(const esc_profile_t *p)
{
    return esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP)
           + esc_profile_manual_count(p, ESC_MANUAL_BEFORE_MENU)
           + esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER_OFF);
}

/* Whether HOLD TO RUN may not count: the supply does not read off, or the
 * before-power steps do not fit and ALL STEPS has not been read. */
static bool sp_hold_blocked(void)
{
    const stick_t *t = &s.st;
    return sp_warn_gated() || (!sp_warn_steps_fit(t->p) && !t->warn_read);
}

static bool sp_can_run(void)
{
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n;
    esc_stick_timing_t t;
    uint32_t mv, ma;
    return sp_plan(ch, &n, &t, &mv, &ma);
}

/* A command for the application.  The queue keeps the newest: a DISARM is
 * never the one dropped, because nothing is queued behind one. */
static void sp_push(motor_cmd_kind_t kind, float value)
{
    stick_t *t = &s.st;
    if (t->qn == SP_QUEUE) {
        memmove(&t->q[0], &t->q[1], (SP_QUEUE - 1) * sizeof(t->q[0]));
        t->qn--;
    }
    t->q[t->qn].kind = kind;
    t->q[t->qn].value = value;
    t->qn++;
}

/* What the run asks for, onto the paths an operator's own presses take:
 * ARM, DISARM and THROTTLE as the MOTOR screen sends them, the supply as
 * SUPPLY's own switch. */
static void sp_follow(void)
{
    stick_t *t = &s.st;
    const esc_stick_out_t *o = esc_stick_out(&t->run);
    if (o->arm != t->sent_arm) {
        t->sent_arm = o->arm;
        sp_push(o->arm ? MOTOR_CMD_ARM : MOTOR_CMD_DISARM, 0.0f);
    }
    if (o->throttle_pct != t->sent_pct) {
        t->sent_pct = o->throttle_pct;
        sp_push(MOTOR_CMD_THROTTLE, o->throttle_pct);
    }
    if (o->supply_on != t->sent_supply) {
        t->sent_supply = o->supply_on;
        if (o->supply_on) {
            supply_screen_put((float)o->supply_mv / 1000.0f,
                              (float)o->supply_ma / 1000.0f);
            supply_screen_ask_on(SUPPLY_BY_STICK);
        } else {
            supply_screen_ask_off();
        }
    }
}

static void sp_start(void)
{
    stick_t *t = &s.st;
    t->warn = false;
    t->warn_down = false;
    ui_hold_reset(&t->hold);
    ++pg.rev;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n;
    esc_stick_timing_t tm;
    uint32_t mv, ma;
    if (!sp_plan(ch, &n, &tm, &mv, &ma) || sp_hold_blocked()) {
        return;     /* the note says why */
    }
    const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                  t->link_up, t->pressed };
    const char *why = NULL;
    if (!esc_stick_start(&t->run, t->p, ch, n, &tm, mv, ma, &b, &why)) {
        snprintf(t->note, sizeof(t->note), "%s", sp_why_text(why));
        return;
    }
    t->runs++;
    t->shown = true;
    esc_stick_light_reset(&t->light, &t->run);
    t->green = false;
    t->sent_arm = false;
    /* Sent as on, so the run's off goes out: the supply is asked off at
     * the start, whatever it was left as. */
    t->sent_supply = true;
    t->sent_pct = ESC_STICK_PCT_MIN;
    t->sig = 0u;
    /* The set points before anything else, so an ON never meets old ones. */
    supply_screen_put((float)mv / 1000.0f, (float)ma / 1000.0f);
    sp_follow();
}

static void sp_end_hold(void)
{
    if (s.st.warn_down || s.st.hold.held_s > 0.0f) {
        s.st.warn_down = false;
        ui_hold_reset(&s.st.hold);
        ++pg.rev;
    }
}

/* What the display shows of a run, to repaint only when it changes. */
static uint32_t sp_signature(void)
{
    const esc_stick_t *e = &s.st.run;
    const uint32_t tenths = (e->phase == ESC_STICK_ENTRY
                             || e->phase == ESC_STICK_SIGNAL)
                                ? e->now_ms / 100u : 0u;
    const uint32_t parts[] = {
        (uint32_t)e->phase, esc_stick_beeps(e), e->groups, e->last_count,
        e->last_valid ? 1u : 0u, e->last_trusted ? 1u : 0u,
        e->last_in_order ? 1u : 0u,
        (uint32_t)e->ma, (uint32_t)esc_det_floor_ma(&e->det), e->iv_ms,
        esc_stick_done_count(e), e->entries, e->active, tenths,
        s.st.green ? 1u : 0u,
        esc_stick_hand_ready(e) ? 1u : 0u,
        (esc_stick_hand_left_ms(e) + 999u) / 1000u, e->hand,
        e->hand_menu ? 1u : 0u,
        /* The phase tap's readout, which a run's page shows beside it. */
        (uint32_t)s.tone.state, s.tone.read ? 1u : 0u,
        s.tone.overrun ? 1u : 0u,
        s.tone.win_freq_dhz, s.tone.lost, s.tone.missed,
        s.tone.n, (s.tone.n > 0u) ? s.tone.beeps[0].seq : 0u,
    };
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        h = (h ^ parts[i]) * 16777619u;
    }
    return h;
}

void esc_stick_panel_tick(float dt_s)
{
    stick_t *t = &s.st;
    if (pg.stage == STAGE_LIST && sp_list_key() != t->built_key) {
        sp_build_list(true);
        ++pg.rev;
    }
    if (t->shown && t->p != NULL && t->p->manual_count > 0u) {
        const bool off = sp_supply_reads_off();
        if (off != t->sup_off_drawn) {
            t->sup_off_drawn = off;
            ++pg.rev;
        }
    }
    if (t->warn && sp_bench_why() != NULL) {
        /* A bench that can no longer carry a run closes the warning: its
         * HOLD TO RUN would end in a refusal, and the note says why. */
        t->warn = false;
        sp_end_hold();
        ++pg.rev;
    }
    if (t->warn) {
        /* The steps at an unpowered ESC show, and HOLD TO RUN counts, only
         * while the supply reads off; a hold under way ends when it stops
         * reading so. */
        const bool gated = sp_hold_blocked();
        if (gated != t->sup_gate) {
            t->sup_gate = gated;
            ++pg.rev;
        }
        if (gated) {
            sp_end_hold();
        }
    }
    if (t->warn && t->warn_down) {
        ++pg.rev;            /* the hold's fill */
        if (ui_hold_tick(&t->hold, dt_s)) {
            sp_start();
        }
    }
    const bool running = esc_stick_running(&t->run);
    /* Not gated on the result still showing: a tap queued in the frame
     * the run ended -- on ABORT, which the result's OK replaces -- closes
     * the result before this tick sees the end, and the steps are still
     * due. */
    if (t->was_running && !running && t->run.p != NULL
        && esc_profile_manual_count(t->run.p,
                                    ESC_MANUAL_AFTER_PROGRAMMING) > 0u) {
        /* The run is over: what is to be done now, every step of it. */
        t->hand_open = true;
        t->hand_p = t->run.p;
        t->hand_model = t->model;
        ++pg.rev;
    }
    t->was_running = running;
    if (running) {
        const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                      t->link_up, t->pressed };
        esc_stick_step(&t->run, &b);
        sp_follow();
        /* A light that changes repaints both buffers: the signature moves
         * the revision, and each buffer redraws on a revision it has not
         * drawn. */
        t->green = esc_stick_light_green(&t->light, &t->run, t->now_ms);
        const uint32_t sig = sp_signature();
        if (sig != t->sig) {
            t->sig = sig;
            ++pg.rev;
        }
        return;
    }
    if (pg.stage == STAGE_PAGE && t->p != NULL && !t->shown) {
        /* RUN follows the supply and the caps, which change elsewhere. */
        char was[SP_NOTE];
        memcpy(was, t->note, sizeof(was));
        const bool could = sp_can_run();
        if (strcmp(was, t->note) != 0 || could != t->could) {
            t->could = could;
            ++pg.rev;
        }
    }
}

void esc_stick_panel_leave(void)
{
    stick_t *t = &s.st;
    pg.tap.live = false;
    if (esc_stick_running(&t->run)) {
        esc_stick_abort(&t->run, ESC_STICK_R_LEFT);
        sp_follow();
    }
    /* The run's abort asks the supply off; and whatever a run switched on
     * goes off with the screen in any case. */
    supply_screen_left(SUPPLY_BY_STICK);
    /* A warning not held is a run not started.  The search keeps what was
     * typed. */
    t->warn = false;
    t->hand_open = false;
    sp_end_hold();
    sp_find_close();
    if (t->timing) {
        t->timing = false;
        if (settings_dirty()) {
            settings_request_save();
        }
    }
}

/*
 * Drop the ARM and the THROTTLE a run has queued and the application has
 * not taken: the hold completes in the frame's tick, after the frame has
 * handed its commands over, so they wait a frame here -- and the next frame
 * would stamp them with what it knows then, past a stop or a loss that
 * came in between.  Returns whether an ARM was among them.
 */
static bool sp_drop_drive(void)
{
    stick_t *t = &s.st;
    bool arm = false;
    int k = 0;
    for (int i = 0; i < t->qn; ++i) {
        const motor_cmd_kind_t kind = t->q[i].kind;
        if (kind == MOTOR_CMD_ARM || kind == MOTOR_CMD_THROTTLE) {
            arm = arm || kind == MOTOR_CMD_ARM;
            continue;
        }
        t->q[k++] = t->q[i];
    }
    t->qn = k;
    return arm;
}

/* The run ends now, and nothing it queued to drive survives the end. */
static void sp_end_run(esc_stick_reason_t why)
{
    stick_t *t = &s.st;
    if (!esc_stick_running(&t->run)) {
        return;
    }
    (void)sp_drop_drive();
    esc_stick_abort(&t->run, why);
    sp_follow();
    ++pg.rev;
}

/*
 * Touch went missing: a hold under way may have no finger on it, and an
 * arm the warning's hold asked for may have come from a contact the panel
 * lost.  Before the arm is in force the run ends; once the bench is armed
 * the control task's watch over the arm decides, as it does for ARM.
 */
void esc_stick_panel_cancel(void)
{
    /* A press that may become a tap has no release to wait for. */
    pg.tap.live = false;
    sp_end_hold();
    stick_t *t = &s.st;
    if (t->tk.open && t->tk.pressed >= 0) {
        ui_textkey_cancel_press(&t->tk);
        t->tk_rev = t->tk.revision;
        ++pg.rev;
    }
    bool queued = false;
    for (int i = 0; i < t->qn; ++i) {
        queued = queued || t->q[i].kind == MOTOR_CMD_ARM;
    }
    if (esc_stick_running(&t->run)
        && (queued || t->run.phase == ESC_STICK_ARMING)) {
        sp_end_run(ESC_STICK_R_TOUCH);
    }
}

static void sp_track(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    if (!t->warn_down || evt->point.id != t->warn_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* A finger that leaves the button abandons the hold, as on ARM. */
        if (!gfx_rect_contains(t->hold_btn, evt->point.x, evt->point.y)
            && ui_hold_leave(&t->hold)) {
            t->warn_down = false;
            ++pg.rev;
        }
        return;
    }
    (void)ui_hold_end(&t->hold);
    t->warn_down = false;
    ++pg.rev;
}

static int sp_rows_shown(int total, int scroll)
{
    const int n = total - scroll;
    return (n > ROWS_MAX) ? ROWS_MAX : (n < 0) ? 0 : n;
}

/* The search's keyboard, docked right of the narrowed rows. */
static gfx_rect_t sp_dock(void)
{
    return (gfx_rect_t){ SP_DOCK_X, (int16_t)(SP_ROW_Y0 - 2),
                         (int16_t)(W - PAD - SP_DOCK_X),
                         (int16_t)(H - PAD - SP_ROW_Y0 + 2) };
}

/* A profile row, narrowed to the left of the keyboard while it is open. */
static gfx_rect_t sp_row(int i)
{
    gfx_rect_t r = s.st.rows[i];
    if (s.st.tk.open) {
        r.w = (int16_t)(SP_DOCK_X - 8 - r.x);
    }
    return r;
}

/* The keyboard closes; the search stays as typed. */
static void sp_find_close(void)
{
    stick_t *t = &s.st;
    if (t->tk.open) {
        ui_textkey_close(&t->tk);
        t->tk_rev = t->tk.revision;
        ++pg.rev;
    }
}

/*
 * One event for the search's keyboard.  Every key that changes the text
 * filters the list at once; OK keeps the text, CANCEL goes back to the
 * search the keyboard opened on.
 */
static void sp_find_event(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    char out[SP_FIND_MAX + 1];
    const ui_textkey_result_t r = ui_textkey_event(&t->tk, evt, out,
                                                   sizeof(out));
    const char *typed = (r == UI_TEXTKEY_OK)        ? out
                      : (r == UI_TEXTKEY_CANCELLED) ? t->find_was
                                                    : t->tk.text;
    if (strcmp(typed, t->find) != 0) {
        snprintf(t->find, sizeof(t->find), "%.*s", SP_FIND_MAX, typed);
        sp_build_list(false);           /* a new search starts at the top */
        if (t->level == 0) {
            t->bscroll = 0;
        }
        ++pg.rev;
    }
    if (t->tk.revision != t->tk_rev) {
        t->tk_rev = t->tk.revision;
        ++pg.rev;
    }
}

bool esc_stick_panel_holds_press(uint8_t id)
{
    const stick_t *t = &s.st;
    return (t->warn_down && t->warn_id == id)
           || (t->tk.open && t->tk.pressed >= 0 && t->tk.press_id == id);
}

void esc_stick_panel_follow(const touch_event_t *evt)
{
    if (s.st.tk.open) {
        sp_find_event(evt);
    }
    sp_track(evt);
}

uint32_t esc_stick_panel_showing(uint32_t h)
{
    const stick_t *t = &s.st;
    const uint32_t parts[] = {
        (uint32_t)t->level, (uint32_t)t->scroll, (uint32_t)t->count,
        (uint32_t)t->iscroll, (uint32_t)t->tscroll,
        t->warn ? 1u : 0u, t->hand_open ? 1u : 0u, t->shown ? 1u : 0u,
        t->timing ? 1u : 0u, t->tk.open ? 1u : 0u, t->builds,
        esc_stick_running(&t->run) ? 1u : 0u,
        (esc_stick_hand(&t->run) != NULL) ? 1u : 0u,
    };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        h = (h ^ parts[i]) * 16777619u;
    }
    return h;
}

/* BACK on the list climbs a level: a maker's models to the makers, with
 * the makers' scroll and the search kept; the makers close the session,
 * back to PROGRAM's list.  The keyboard closes, the search as typed. */
static void sp_list_up(void)
{
    stick_t *t = &s.st;
    sp_find_close();
    if (t->level == 1) {
        t->level = 0;
        t->scroll = t->bscroll;
        sp_build_list(true);
    } else {
        pg.stage = STAGE_CLOSED;
    }
    ++pg.rev;
}

/* BACK on a profile's page: the list it was picked from, its scroll kept. */
static void sp_to_list(void)
{
    s.st.shown = false;
    pg.stage = STAGE_LIST;
    sp_build_list(true);
    ++pg.rev;
}

/* TIMING closes; what it changed is asked to be saved. */
static void sp_timing_close(void)
{
    s.st.timing = false;
    if (settings_dirty()) {
        settings_request_save();
    }
    ++pg.rev;
}

/*
 * The home tag, one step at a time.  What covers the page closes first:
 * the manual steps' pop-up as OK closes it, the RUN warning as CANCEL.  A
 * run under way ends as ABORT ends it, and the page stays.  Otherwise the
 * session climbs a level as BACK does: TIMING to the profile's page, the
 * page to the list, a maker's models to the makers, the makers out of the
 * session to PROGRAM's list.
 */
bool esc_stick_panel_back(void)
{
    stick_t *t = &s.st;
    if (t->hand_open) {
        t->hand_open = false;
        t->warn_read = t->warn_read || t->warn;
        ++pg.rev;
        return true;
    }
    if (t->warn) {
        t->warn = false;
        sp_end_hold();
        ++pg.rev;
        return true;
    }
    if (pg.stage == STAGE_LIST) {
        sp_list_up();
        return true;
    }
    if (esc_stick_running(&t->run)) {
        sp_end_run(ESC_STICK_R_USER);
        return true;
    }
    if (t->timing) {
        sp_timing_close();
        return true;
    }
    sp_to_list();
    return true;
}

bool esc_stick_panel_safe_at(int px, int py)
{
    const stick_t *t = &s.st;
    /* esc_stick_panel_down()'s order: the steps' pop-up and the warning
     * cover the page, the list has no run, and a run takes ABORT. */
    if (t->hand_open || t->warn || pg.stage == STAGE_LIST
        || !esc_stick_running(&t->run)) {
        return false;
    }
    if (esc_stick_hand(&t->run) != NULL) {
        return gfx_rect_contains(t->cancel_btn, px, py);
    }
    return gfx_rect_contains(pg.write_btn, px, py);
}

bool esc_stick_panel_down(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    const int px = evt->point.x, py = evt->point.y;

    /* The manual steps' pop-up covers the screen; OK closes it. */
    if (t->hand_open) {
        if (hit(t->cancel_btn, px, py)) {
            t->hand_open = false;
            /* Read over the warning: the hold may count. */
            t->warn_read = t->warn_read || t->warn;
            ++pg.rev;
        }
        return true;
    }

    /* The warning covers the screen, BACK included. */
    if (t->warn) {
        if (!sp_warn_steps_fit(t->p) && hit(t->steps_btn, px, py)) {
            t->hand_open = true;            /* ALL STEPS, over the warning */
            t->hand_p = t->p;
            t->hand_model = t->model;
            sp_end_hold();
            ++pg.rev;
            return true;
        }
        if (!sp_hold_blocked() && hit_down(t->hold_btn, px, py)) {
            t->warn_down = true;
            t->warn_id = evt->point.id;
            ui_hold_begin(&t->hold);
            ++pg.rev;
        } else if (hit(t->cancel_btn, px, py)) {
            t->warn = false;
            sp_end_hold();
            ++pg.rev;
        }
        return true;
    }

    if (pg.stage == STAGE_LIST) {
        if (t->tk.open && hit_down(t->tk.area, px, py)) {
            sp_find_event(evt);
            return true;
        }
        if (hit(pg.back, px, py)) {
            sp_list_up();
            return true;
        }
        if (!t->tk.open && t->find[0] != '\0'
            && hit(t->find_clr, px, py)) {
            t->find[0] = '\0';                  /* X: the whole list */
            sp_build_list(false);
            ++pg.rev;
            return true;
        }
        if (!t->tk.open && hit(t->find_box, px, py)) {
            memcpy(t->find_was, t->find, sizeof(t->find_was));
            ui_textkey_open_search(&t->tk, sp_dock(), TR(SP_FIND_TITLE),
                                   t->find, SP_FIND_MAX);
            t->tk_rev = t->tk.revision;
            ++pg.rev;
            return true;
        }
        if (hit(t->list_up, px, py) && t->scroll > 0) {
            t->scroll = (t->scroll > SP_ROWS) ? t->scroll - SP_ROWS : 0;
            ++pg.rev;
            return true;
        }
        if (hit(t->list_dn, px, py)
            && t->scroll + SP_ROWS < t->count) {
            t->scroll += SP_ROWS;
            ++pg.rev;
            return true;
        }
        for (int i = 0; i < SP_ROWS && t->scroll + i < t->count; ++i) {
            if (!hit(sp_row(i), px, py)) {
                continue;
            }
            const int at = t->scroll + i;
            /* A maker opens its models, the search kept; the makers'
             * scroll is kept for BACK.  No maker opens by itself: one the
             * search narrows to is shown, and a tap opens it. */
            if (t->level == 0) {
                sp_find_close();
                t->maker = t->makers[at].name;
                t->bscroll = t->scroll;
                t->level = 1;
                sp_build_list(false);
                ++pg.rev;
                return true;
            }
            /* A model opens its family's profile.  One whose profile the
             * engine cannot run says why on its row and goes no further,
             * unless the profile has manual steps: those it shows.  One
             * picked while the keyboard is open closes it, the search
             * kept. */
            const esc_profile_t *p = esc_profiles_at(t->rows_m[at].prof);
            const int model = t->rows_m[at].model;
            if (sp_model_runs(p, model)) {
                sp_find_close();
                sp_pick_profile(p);
                t->model = model;
                t->hand_model = model;
                sp_pick_shown();
                pg.stage = STAGE_PAGE;
                ++pg.rev;
            } else if (p->manual_count > 0u) {
                /* The steps, and why this model -- not the family -- does
                 * not run: the row's own reason. */
                sp_find_close();
                t->hand_open = true;
                t->hand_p = p;
                t->hand_model = model;
                ++pg.rev;
            }
            return true;
        }
        return true;
    }

    /* The device page.  A run under way takes ABORT and nothing else; STOP
     * is in the band, and leaving the screen aborts.  ABORT acts on the
     * press: it stops a motor. */
    if (esc_stick_running(&t->run)) {
        /* A manual step asked covers the page: DONE or ABORT.  DONE is
         * acted on in the next tick, after the bench has been judged. */
        if (esc_stick_hand(&t->run) != NULL) {
            if (hit(t->hold_btn, px, py)) {
                if (esc_stick_confirm(&t->run)) {
                    ++pg.rev;
                }
            } else if (hit_down(t->cancel_btn, px, py)) {
                sp_end_run(ESC_STICK_R_USER);
            }
            return true;
        }
        if (hit_down(pg.write_btn, px, py)) {
            esc_stick_abort(&t->run, ESC_STICK_R_USER);
            sp_follow();
            ++pg.rev;
        }
        return true;
    }
    if (t->shown) {
        if (t->p->manual_count > 0u && hit(t->hand_btn, px, py)) {
            t->hand_open = true;            /* every step, after included */
            t->hand_p = t->p;
            t->hand_model = t->model;
            ++pg.rev;
            return true;
        }
        if (hit(pg.back, px, py)) {
            sp_to_list();
        } else if (hit(pg.write_btn, px, py)) {
            t->shown = false;               /* OK: back to the menu */
            ++pg.rev;
        }
        return true;
    }
    if (t->timing) {
        if (hit(pg.back, px, py)
            || hit(pg.write_btn, px, py)) {
            sp_timing_close();              /* CLOSE */
            return true;
        }
        if (hit(pg.read_btn, px, py)) {
            settings_reset(SET_CAT_STICK);  /* DEFAULTS */
            ++pg.rev;
            return true;
        }
        const int max_scroll = SP_SETTINGS - ROWS_MAX;
        if (hit(pg.page_up, px, py) && t->tscroll > 0) {
            --t->tscroll;
            ++pg.rev;
            return true;
        }
        if (hit(pg.page_dn, px, py) && t->tscroll < max_scroll) {
            ++t->tscroll;
            ++pg.rev;
            return true;
        }
        for (int i = 0; i < sp_rows_shown(SP_SETTINGS, t->tscroll); ++i) {
            const int idx = t->tscroll + i;
            const int by = hit(pg.down[i], px, py)  ? -1
                         : hit(pg.up[i], px, py)    ?  1 : 0;
            if (by != 0) {
                settings_adjust(k_sp_settings[idx], by);
            }
            if (by != 0 || hit(pg.row[i], px, py)) {
                t->tpicked = idx;
                ++pg.rev;
                return true;
            }
        }
        return true;
    }

    if (hit(pg.back, px, py)) {
        sp_to_list();
        return true;
    }
    if (t->p->manual_count > 0u && hit(t->hand_btn, px, py)) {
        t->hand_open = true;                /* MANUAL INTERVENTION REQUIRED */
        t->hand_p = t->p;
        t->hand_model = t->model;
        ++pg.rev;
        return true;
    }
    if (hit(t->timing_btn, px, py)) {
        t->timing = true;                   /* TIMING */
        t->tscroll = 0;
        t->tpicked = 0;
        ++pg.rev;
        return true;
    }
    if (hit(pg.write_btn, px, py)) {
        if (sp_can_run()) {
            t->warn = true;                 /* RUN asks first */
            t->warn_read = false;
            t->warn_down = false;
            ui_hold_reset(&t->hold);
        }
        ++pg.rev;
        return true;
    }
    const int items = sp_items();
    const int max_scroll = (items > ROWS_MAX) ? items - ROWS_MAX : 0;
    if (hit(pg.page_up, px, py) && t->iscroll > 0) {
        --t->iscroll;
        ++pg.rev;
        return true;
    }
    if (hit(pg.page_dn, px, py) && t->iscroll < max_scroll) {
        ++t->iscroll;
        ++pg.rev;
        return true;
    }
    for (int i = 0; i < sp_rows_shown(items, t->iscroll); ++i) {
        const int at = t->iscroll + i;
        const int idx = sp_item_at(at);
        if (idx < 0) {
            break;
        }
        const int by = hit(pg.down[i], px, py)  ? -1
                     : hit(pg.up[i], px, py)    ?  1 : 0;
        if (by != 0 && esc_stick_not_offered(&t->p->items[idx]) == NULL) {
            /* KEEP, then each value in the profile's order; clamped, not
             * wrapped, as every stepper here is.  Reset and exit are
             * actions, not settings, and are not offered. */
            int v = t->pick[idx] + by;
            const int hi = (int)t->p->items[idx].value_count - 1;
            v = (v < -1) ? -1 : (v > hi) ? hi : v;
            t->pick[idx] = v;
        }
        if (by != 0 || hit(pg.row[i], px, py)) {
            t->picked = idx;
            ++pg.rev;
            return true;
        }
    }
    return true;
}

/* ------------------------------------------- the stick class, drawn ----- */

/* The search field: what is searched for, or SEARCH; X clears it while
 * the keyboard is closed. */
static DRAWER void sp_draw_find(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_rect_t b = t->find_box;
    const bool any = t->find[0] != '\0';
    const bool clr = any && !t->tk.open;
    gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                        t->tk.open ? ui_theme_color(UI_C_ACCENT)
                                   : ui_theme_color(UI_C_EDGE));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(b.x + 8), b.y,
                                 (int16_t)(b.w - 16 - (clr ? 28 : 0)),
                                 b.h },
                any ? t->find : TR(SP_FIND), UI_FONT_LABEL,
                any ? ui_theme_color(UI_C_TEXT)
                    : ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_LEFT);
    if (clr) {
        ui_button(c, t->find_clr, "X", ui_theme_color(UI_C_PANEL_HI), false,
                  true);
    }
}

/* The red MANUAL tag's width, px. */
static int sp_tag_w(void)
{
    return gfx_text_cells(TR(SP_HAND_TAG)) * 8 + 12;
}

/* The red MANUAL tag, its right edge at @p right; its width, px. */
static int sp_draw_tag(gfx_canvas_t *c, int right, int y)
{
    const char *tag = TR(SP_HAND_TAG);
    const int w = sp_tag_w();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_round_rect(c, right - w, y, w, 20, 4, red);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(right - w), (int16_t)y,
                                 (int16_t)w, 20 },
                tag, UI_FONT_LABEL,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    return w;
}

/* "55 A" and "25 V" or "25.2 V" for a model's current and voltage; "-"
 * where it states none. */
static void sp_amps(char *out, size_t n, const esc_model_t *m)
{
    if (m->current_a == 0u) {
        snprintf(out, n, "-");
    } else {
        snprintf(out, n, "%u A", (unsigned)m->current_a);
    }
}

static void sp_volts(char *out, size_t n, const esc_model_t *m)
{
    if (m->v_max_mv == 0u) {
        snprintf(out, n, "-");
    } else if (m->v_max_mv % 1000u == 0u) {
        snprintf(out, n, "%u V", (unsigned)(m->v_max_mv / 1000u));
    } else {
        snprintf(out, n, "%u.%u V", (unsigned)(m->v_max_mv / 1000u),
                 (unsigned)(m->v_max_mv % 1000u / 100u));
    }
}

/* The run mark of a narrowed row: filled in the accent, or a ring. */
static void sp_mark_runs(gfx_canvas_t *c, gfx_rect_t r, bool runs)
{
    const int mx = r.x + r.w - 14, my = r.y + r.h / 2;
    if (runs) {
        gfx_fill_circle(c, mx, my, 5, ui_theme_color(UI_C_ACCENT));
    } else {
        gfx_draw_circle(c, mx, my, 5, ui_theme_color(UI_C_TEXT_FAINT));
    }
}

/* A maker's row: its name, the MANUAL tag where a model it lists has
 * manual steps, and its models: those that run of all, or while a search
 * is typed those found of all and those of them that run. */
static DRAWER void sp_draw_maker(gfx_canvas_t *c, gfx_rect_t r,
                                 const sp_maker_t *m, bool dock,
                                 bool searching)
{
    const bool runs = m->runs > 0u;
    ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                       : ui_theme_color(UI_C_PANEL_SUNK));
    const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    char right[64];
    if (dock) {
        snprintf(right, sizeof(right), "%u", (unsigned)m->found);
        const int tag = m->manual ? sp_draw_tag(c, r.x + r.w - 28, r.y + 6)
                                        + 8 : 0;
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 28 - tag - 48),
                                     (int16_t)(r.y + 8), 40, 16 },
                    right, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                    GFX_ALIGN_RIGHT);
        sp_text(c, r.x + 8, r.y + 8, m->name,
                (r.w - 8 - 28 - tag - 56) / 8, ink);
        sp_mark_runs(c, r, runs);
        return;
    }
    sp_text(c, r.x + 12, r.y + 8, m->name, 30, ink);
    if (m->manual) {
        (void)sp_draw_tag(c, r.x + r.w - 312 - 8, r.y + 6);
    }
    if (searching) {
        snprintf(right, sizeof(right), TR(SP_MAKER_FOUND),
                 (unsigned)m->found, (unsigned)m->models, (unsigned)m->runs);
    } else {
        snprintf(right, sizeof(right), TR(SP_MAKER_MODELS),
                 (unsigned)m->runs, (unsigned)m->models);
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 312),
                                 (int16_t)(r.y + 8), 300, 16 },
                right, UI_FONT_LABEL,
                runs ? ui_theme_color(UI_C_ACCENT)
                     : ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);
}

/* A model's row: its name, current and voltage, its family, the MANUAL
 * tag, and what its family's profile is or why it does not run. */
static DRAWER void sp_draw_model(gfx_canvas_t *c, gfx_rect_t r,
                                 const sp_row_t *row, bool dock)
{
    const esc_profile_t *p = esc_profiles_at(row->prof);
    const esc_model_t *m = &p->models[row->model];
    char cap[64];
    const char *why = sp_model_why(p, row->model, cap, sizeof(cap));
    const bool runs = why == NULL;
    ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                       : ui_theme_color(UI_C_PANEL_SUNK));
    const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    const gfx_color_t dim = runs ? ui_theme_color(UI_C_TEXT_DIM)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    char amps[16], volts[16];
    sp_amps(amps, sizeof(amps), m);
    sp_volts(volts, sizeof(volts), m);
    if (dock) {
        const int tag = (p->manual_count > 0u)
                            ? sp_draw_tag(c, r.x + r.w - 28, r.y + 6) + 8
                            : 0;
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 28 - tag - 56),
                                     (int16_t)(r.y + 8), 48, 16 },
                    amps, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
        sp_text(c, r.x + 8, r.y + 8, m->name,
                (r.w - 8 - 28 - tag - 64) / 8, ink);
        sp_mark_runs(c, r, runs);
        return;
    }
    sp_text(c, r.x + 12, r.y + 8, m->name, 26, ink);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + SP_AMPS_X),
                                 (int16_t)(r.y + 8), 48, 16 },
                amps, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + SP_VOLTS_X),
                                 (int16_t)(r.y + 8), 56, 16 },
                volts, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
    char right[80];
    char warn[64];
    const bool unrated = runs
                         && sp_model_warn(p, row->model, warn, sizeof(warn))
                                != NULL;
    if (unrated) {
        snprintf(right, sizeof(right), "%s", warn);
    } else if (runs) {
        char items[24];
        const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
        snprintf(items, sizeof(items),
                 (p->item_count == 1u) ? TR(SP_ITEM) : TR(SP_ITEMS),
                 (unsigned)p->item_count);
        snprintf(right, sizeof(right), "%s%s%s  %s",
                 esc_profiles_is_override(p) ? TR(SP_CARD) : "",
                 esc_profiles_is_override(p) ? "  " : "", items,
                 (kind == ESC_STICK_KIND_TWO_STAGE) ? TR(SP_TWO_STAGE)
                                                    : TR(SP_ONE_STAGE));
    } else {
        snprintf(right, sizeof(right), "%s", why);
    }
    /* The right text in 220 px, or as wide as it is up to the family's
     * column (SP_WHY_CELLS); the MANUAL tag left of it where both fit, and
     * the family in what is left, from 3 cells. */
    const int room = SP_WHY_CELLS * 8;
    int rw = gfx_text_cells(right) * 8;
    rw = (rw < 220) ? 220 : rw;
    rw = (rw > room) ? room : rw;
    sp_cut(right, sizeof(right), rw / 8);
    const int rx = r.w - 12 - rw;
    const int tag = (p->manual_count > 0u && rw + 4 + sp_tag_w() <= room)
                        ? sp_draw_tag(c, r.x + rx - 4, r.y + 6) + 8
                        : 0;
    const int fam = (rx - 4 - tag - SP_FAMILY_X) / 8;
    if (fam >= 3) {
        sp_text(c, r.x + SP_FAMILY_X, r.y + 8, p->family, fam,
                ui_theme_color(UI_C_TEXT_FAINT));
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + rx), (int16_t)(r.y + 8),
                                 (int16_t)rw, 16 },
                right, UI_FONT_LABEL,
                unrated ? ui_theme_color(UI_C_WARN)
                : runs  ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * The list: the crumb and the search field above, a level's rows, and the
 * footer: the count at the right under the rows, or beside the docked
 * keyboard at the left; the line that no profile is verified while the
 * keyboard is closed.
 */
static DRAWER void sp_draw_list(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const bool dock = t->tk.open;
    const bool searching = t->find[0] != '\0';
    char trail[96];
    if (t->level == 1 && t->maker != NULL) {
        snprintf(trail, sizeof(trail), "ESC STICK  >  %s", t->maker);
    } else {
        snprintf(trail, sizeof(trail), "ESC STICK");
    }
    sp_cut(trail, sizeof(trail), SP_TRAIL_CELLS);
    draw_crumb(c, trail);
    sp_draw_find(c);
    ui_button(c, t->list_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll > 0);
    ui_button(c, t->list_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll + SP_ROWS < t->count);

    if (t->count == 0) {
        gfx_text(c, PAD + 12, SP_ROW_Y0 + 8, TR(SP_FIND_NONE), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    for (int i = 0; i < SP_ROWS && t->scroll + i < t->count; ++i) {
        if (t->level == 1) {
            sp_draw_model(c, sp_row(i), &t->rows_m[t->scroll + i], dock);
        } else {
            sp_draw_maker(c, sp_row(i), &t->makers[t->scroll + i], dock,
                          searching);
        }
    }

    char count[64];
    const int last = (t->scroll + SP_ROWS < t->count) ? t->scroll + SP_ROWS
                                                      : t->count;
    const int first = (t->count > 0) ? t->scroll + 1 : 0;
    if (t->level == 1 && searching) {
        snprintf(count, sizeof(count), TR(SP_LIST_FOUND), first, last,
                 t->count, t->runnable);
    } else if (t->level == 1) {
        snprintf(count, sizeof(count), TR(SP_LIST_COUNT), first, last,
                 t->count, t->runnable);
    } else if (searching) {
        snprintf(count, sizeof(count), TR(SP_MAKERS_FOUND), first, last,
                 t->count, t->runnable);
    } else {
        snprintf(count, sizeof(count), TR(SP_MAKERS_COUNT), first, last,
                 t->count, t->runnable);
    }
    if (dock) {
        gfx_text_in(c, (gfx_rect_t){ PAD + 12, SP_FOOT_Y,
                                     (int16_t)(SP_DOCK_X - 8 - PAD - 12),
                                     16 },
                    count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_LEFT);
        ui_textkey_render(&t->tk, c);
        return;
    }
    gfx_text(c, PAD + 12, SP_FOOT_Y, TR(SP_UNVERIFIED_ALL), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - PAD - 12 - SP_COUNT_W),
                                 SP_FOOT_Y, SP_COUNT_W, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * One lens or the base of the stack light: a cylinder seen from the side.
 * Lit, the colour runs to a near-white band down the middle; unlit, the
 * same colour greyed and dark.  Both darken toward the edges, and a darker
 * line every 6 px is a rib of the lens.  Drawn row by row in runs of one
 * colour, since the panel's frame buffer is cached by rows.
 */
static void sp_cylinder(gfx_canvas_t *c, int x, int y, int w, int h,
                        gfx_color_t base, bool lit, bool ribs)
{
    gfx_color_t col[64];
    gfx_color_t rib[64];
    if (w > 64) {
        w = 64;
    }
    for (int i = 0; i < w; ++i) {
        /* 0 in the middle, 255 at either edge, in eight steps. */
        const int off = (2 * i + 1 - w < 0) ? w - 2 * i - 1 : 2 * i + 1 - w;
        const int d = ((off * 8 / (w + 1)) * 255) / 7;
        gfx_color_t k;
        if (d < 64) {
            k = gfx_lerp(base, GFX_WHITE, lit ? 120 : 24);
        } else {
            k = gfx_lerp(base, GFX_BLACK, (uint8_t)((d - 64) * 150 / 191));
        }
        col[i] = k;
        rib[i] = gfx_lerp(k, GFX_BLACK, 80);
    }
    for (int row = 0; row < h; ++row) {
        const bool dark = row == 0 || row == h - 1
                          || (ribs && row % 6 == 5);
        const gfx_color_t *line = dark ? rib : col;
        int run = 0;
        for (int i = 1; i <= w; ++i) {
            if (i == w || line[i] != line[run]) {
                gfx_hline(c, x + run, y + row, i - run, line[run]);
                run = i;
            }
        }
    }
}

/* A lens, lit or dark, with a glow around it when lit. */
static void sp_lens(gfx_canvas_t *c, int x, int y, gfx_color_t colour,
                    bool lit)
{
    const gfx_color_t panel = ui_theme_color(UI_C_PANEL);
    if (lit) {
        gfx_fill_rect(c, x - 4, y - 3, SP_TOWER_W + 8, SP_TOWER_LENS + 6,
                      gfx_lerp(panel, colour, 60));
        gfx_fill_rect(c, x - 2, y - 1, SP_TOWER_W + 4, SP_TOWER_LENS + 2,
                      gfx_lerp(panel, colour, 130));
        sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_LENS, colour, true, true);
        return;
    }
    /* Off: the colour greyed, then darkened. */
    const gfx_color_t off = gfx_lerp(gfx_lerp(colour, GFX_GREY(110), 150),
                                     GFX_BLACK, 150);
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_LENS, off, false, true);
}

/*
 * The stack light: red over green on a light grey base, as a signal tower
 * on a machine.  Green: the detector holds a beep.  Red: the run ended for
 * a reason esc_stick_reason_is_fault() names.
 */
static void sp_draw_tower(gfx_canvas_t *c, bool red, bool green)
{
    const int x = SP_TOWER_X;
    int y = SP_TOWER_Y;
    const gfx_color_t grey = GFX_GREY(196);
    sp_cylinder(c, x + 6, y, SP_TOWER_W - 12, SP_TOWER_CAP, GFX_GREY(90),
                false, false);
    y += SP_TOWER_CAP;
    sp_lens(c, x, y, ui_theme_color(UI_C_DANGER), red);
    y += SP_TOWER_LENS;
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_RING, grey, false, false);
    y += SP_TOWER_RING;
    sp_lens(c, x, y, ui_theme_color(UI_C_OK), green);
    y += SP_TOWER_LENS;
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_RING, grey, false, false);
    y += SP_TOWER_RING;
    sp_cylinder(c, x - 2, y, SP_TOWER_W + 4, SP_TOWER_BASE, grey, false,
                false);
    y += SP_TOWER_BASE;
    sp_cylinder(c, x - 8, y, SP_TOWER_W + 16, 4, GFX_GREY(160), false,
                false);
}

/* "MIN", "MID" or "MAX" for a percentage the run commands. */
static const char *sp_pos(float pct)
{
    return (pct >= ESC_STICK_PCT_MAX) ? "MAX"
         : (pct >= ESC_STICK_PCT_MID) ? "MID" : "MIN";
}

static const char *sp_reason_help(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_STOP:        return TR(SP_WHY_STOP);
    case ESC_STICK_R_BENCH_STOP:  return TR(SP_WHY_BENCH_STOP);
    case ESC_STICK_R_DISARMED:    return TR(SP_WHY_DISARMED);
    case ESC_STICK_R_LINK:        return TR(SP_WHY_LINK);
    case ESC_STICK_R_SUPPLY_OFF:  return TR(SP_WHY_SUPPLY_OFF);
    case ESC_STICK_R_SUPPLY_LOST: return TR(SP_WHY_SUPPLY_LOST);
    case ESC_STICK_R_STALE:       return TR(SP_WHY_STALE);
    case ESC_STICK_R_RATE:        return TR(SP_WHY_RATE);
    case ESC_STICK_R_NOT_ARMED:   return TR(SP_WHY_NOT_ARMED);
    case ESC_STICK_R_NO_POWER:    return TR(SP_WHY_NO_POWER);
    case ESC_STICK_R_NO_BEEPS:    return TR(SP_WHY_NO_BEEPS);
    case ESC_STICK_R_HIGH:        return TR(SP_WHY_HIGH);
    case ESC_STICK_R_TIMEOUT:     return TR(SP_WHY_TIMEOUT);
    case ESC_STICK_R_HAND: {
        static char buf[128];
        snprintf(buf, sizeof(buf), TR(SP_WHY_HAND),
                 (unsigned)(ESC_STICK_HAND_WAIT_MS / 1000u));
        return buf;
    }
    case ESC_STICK_R_SUPPLY_ON:   return TR(SP_WHY_SUPPLY_ON);
    case ESC_STICK_R_TOUCH:       return TR(SP_WHY_TOUCH);
    case ESC_STICK_R_USER:        return TR(SP_WHY_USER);
    case ESC_STICK_R_LEFT:        return TR(SP_WHY_LEFT);
    default:                      return "";
    }
}

/* The run's phase and its end in the language showing; the engine's
 * esc_stick_phase_text() and esc_stick_reason_text() are the English. */
static const char *sp_phase_text(esc_stick_phase_t ph)
{
    switch (ph) {
    case ESC_STICK_IDLE:    return TR(SP_PH_READY);
    case ESC_STICK_ARMING:  return TR(SP_PH_ARMING);
    case ESC_STICK_SIGNAL:  return TR(SP_PH_SIGNAL);
    case ESC_STICK_POWER:   return TR(SP_PH_POWER);
    case ESC_STICK_ENTRY:   return TR(SP_PH_ENTRY);
    case ESC_STICK_ITEMS:   return TR(SP_PH_ITEMS);
    case ESC_STICK_VALUES:  return TR(SP_PH_VALUES);
    case ESC_STICK_STORE:   return TR(SP_PH_STORE);
    case ESC_STICK_CYCLE:   return TR(SP_PH_CYCLE);
    case ESC_STICK_HAND_OFF: return TR(SP_PH_HAND_OFF);
    case ESC_STICK_HAND_ON: return TR(SP_PH_HAND_ON);
    case ESC_STICK_HAND_END: return TR(SP_PH_HAND_END);
    case ESC_STICK_OFF:     return TR(SP_PH_OFF);
    case ESC_STICK_DONE:    return TR(SP_PH_DONE);
    case ESC_STICK_ABORTED: return TR(SP_PH_ABORTED);
    }
    return "?";
}

static const char *sp_reason_text(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:        return "";
    case ESC_STICK_R_STOP:        return TR(SP_R_STOP);
    case ESC_STICK_R_BENCH_STOP:  return TR(SP_R_BENCH_STOP);
    case ESC_STICK_R_DISARMED:    return TR(SP_R_DISARMED);
    case ESC_STICK_R_LINK:        return TR(SP_R_LINK);
    case ESC_STICK_R_SUPPLY_OFF:  return TR(SP_R_SUPPLY_OFF);
    case ESC_STICK_R_SUPPLY_LOST: return TR(SP_R_SUPPLY_LOST);
    case ESC_STICK_R_STALE:       return TR(SP_R_STALE);
    case ESC_STICK_R_RATE:        return TR(SP_R_RATE);
    case ESC_STICK_R_NOT_ARMED:   return TR(SP_R_NOT_ARMED);
    case ESC_STICK_R_NO_POWER:    return TR(SP_R_NO_POWER);
    case ESC_STICK_R_SUPPLY_ON:   return TR(SP_R_SUPPLY_ON);
    case ESC_STICK_R_TOUCH:       return TR(SP_R_TOUCH);
    case ESC_STICK_R_NO_BEEPS:    return TR(SP_R_NO_BEEPS);
    case ESC_STICK_R_HIGH:        return TR(SP_R_HIGH);
    case ESC_STICK_R_TIMEOUT:     return TR(SP_R_TIMEOUT);
    case ESC_STICK_R_HAND:        return TR(SP_R_HAND);
    case ESC_STICK_R_USER:        return TR(SP_R_USER);
    case ESC_STICK_R_LEFT:        return TR(SP_R_LEFT);
    }
    return "?";
}

/* "3 Cutoff mode -> 2 hard cutoff" for a change. */
static void sp_change_text(const esc_stick_change_t *ch, char *out, size_t n)
{
    const esc_item_t *it = &s.st.p->items[ch->item];
    const esc_value_t *v = &it->values[ch->value];
    snprintf(out, n, "%u %s -> %u %s", (unsigned)it->number, it->name,
             (unsigned)v->number, v->name);
}

/*
 * "POWER-UP AT MID" for the positions the power-ups of @p n changes take,
 * in the order first met: "MAX, MID" where a one-stage run takes both.
 * False, and nothing written, when every one is MIN.
 */
static bool sp_power_up_text(const esc_profile_t *p,
                             const esc_stick_change_t *ch, size_t n,
                             char *out, size_t size)
{
    bool seen[ESC_THR_NONE + 1] = { false };
    char list[24] = "";
    bool other = false;
    for (size_t i = 0; i < n; ++i) {
        const esc_throttle_t at = esc_stick_change_entry(p, &ch[i]);
        if (seen[at]) {
            continue;
        }
        seen[at] = true;
        other = other || at != ESC_THR_MIN;
        const size_t len = strlen(list);
        snprintf(list + len, sizeof(list) - len, "%s%s",
                 (len > 0u) ? ", " : "", sp_pos(esc_stick_pct(at)));
    }
    if (!other) {
        return false;
    }
    snprintf(out, size, TR(SP_POWER_UP_AT), list);
    return true;
}

/* The longest hold of the profile's at_power_up steps, ms; 0 for none. */
static uint32_t sp_hold_ms(const esc_profile_t *p)
{
    uint32_t hold = 0u;
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        if (p->manual[i].when == ESC_MANUAL_AT_POWER_UP
            && p->manual[i].hold_ms > hold) {
            hold = p->manual[i].hold_ms;
        }
    }
    return hold;
}

/* The phase tap's state as its readout is worded. */
static const char *sp_tone_state(const tone_readout_t *r)
{
    switch (r->state) {
    case TONE_STATE_NO_PAGE:
        return TR(SP_TONE_NO_PAGE);
    case TONE_STATE_WAITING:
        return TR(SP_TONE_WAITING);
    case TONE_STATE_RUNNING:
        return (r->read && r->overrun) ? TR(SP_TONE_OVERRUN)
                                       : TR(SP_TONE_RUNNING);
    case TONE_STATE_REFUSED:
        return TR(SP_TONE_REFUSED);
    case TONE_STATE_STOPPED:
    default:
        return TR(SP_TONE_STOPPED);
    }
}

/* One beep of the readout: its number, length in ms and pitch in Hz, to
 * the tenth, as the page reports them. */
static void sp_tone_beep(const tone_beep_t *b, char *buf, size_t n)
{
    snprintf(buf, n, TR(SP_TONE_BEEP), (unsigned)b->seq,
             (unsigned)(b->len_dms / 10u), (unsigned)(b->len_dms % 10u),
             (unsigned)(b->freq_dhz / 10u), (unsigned)(b->freq_dhz % 10u));
}

/* The beeps' two columns and the cells each takes. */
#define SP_TONE_COL_CELLS 30

/*
 * The phase tap, read only, on the run's page while the tap is enabled
 * on SETUP: a head line, then the last four beeps two to a line.
 */
static DRAWER void sp_draw_tone(gfx_canvas_t *c, int y, int pitch)
{
    const tone_readout_t *r = &s.tone;
    if (r->state == TONE_STATE_OFF) {
        return;
    }
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    const bool fault = r->state == TONE_STATE_NO_PAGE
                       || r->state == TONE_STATE_REFUSED
                       || r->state == TONE_STATE_STOPPED
                       || (r->read && r->overrun);
    /* The pitch and the lost count are the page's, and shown only while a
     * status read younger than TONE_LINK_STALE_MS speaks for it. */
    char pitch_txt[16];
    char lost_txt[8];
    if (!r->read || r->win_freq_dhz == 0u) {
        snprintf(pitch_txt, sizeof(pitch_txt), "--");
    } else {
        snprintf(pitch_txt, sizeof(pitch_txt), "%u.%u Hz",
                 (unsigned)(r->win_freq_dhz / 10u),
                 (unsigned)(r->win_freq_dhz % 10u));
    }
    if (r->read) {
        snprintf(lost_txt, sizeof(lost_txt), "%u", (unsigned)r->lost);
    } else {
        snprintf(lost_txt, sizeof(lost_txt), "--");
    }
    char line[128];
    snprintf(line, sizeof(line), TR(SP_TONE_HEAD), sp_tone_state(r),
             pitch_txt, lost_txt, (unsigned)r->missed);
    gfx_text(c, PAD + 12, y, line, UI_FONT_LABEL,
             fault ? ui_theme_color(UI_C_WARN) : dim, 1);
    const int x0 = PAD + 12 + (gfx_text_cells(TR(SP_TONE_BEEPS)) + 2) * 8;
    gfx_text(c, PAD + 12, y + pitch, TR(SP_TONE_BEEPS), UI_FONT_LABEL, dim,
             1);
    if (r->n == 0u) {
        gfx_text(c, x0, y + pitch, TR(SP_TONE_NONE), UI_FONT_LABEL, dim, 1);
        return;
    }
    for (unsigned i = 0u; i < r->n && i < TONE_LINK_SHOWN; ++i) {
        sp_tone_beep(&r->beeps[i], line, sizeof(line));
        gfx_text(c, x0 + (int)(i % 2u) * SP_TONE_COL_CELLS * 8,
                 y + pitch + (int)(i / 2u) * pitch, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
    }
}

static DRAWER void sp_draw_progress(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    const gfx_color_t txt = ui_theme_color(UI_C_TEXT);
    char line[128];

    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_RUN), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    if (e->entry != ESC_THR_MIN) {
        snprintf(line, sizeof(line), TR(SP_POWER_UP_AT),
                 sp_pos(esc_stick_pct(e->entry)));
        gfx_text(c, PAD + 12 + gfx_text_cells(TR(SP_RUN)) * 8 + 24,
                 PARM_Y + 12, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
    gfx_text(c, PAD + 12, PARM_Y + 34, sp_phase_text(e->phase),
             UI_FONT_HEAD, txt, 1);
    /* The count beside the stack light. */
    snprintf(line, sizeof(line), "%u", esc_stick_beeps(e));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(SP_TOWER_X - 24 - 210),
                                 (int16_t)(PARM_Y + 14), 210, 34 },
                line, UI_FONT_NUM, ui_theme_color(UI_C_CURR), 1,
                GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(SP_TOWER_X - 24 - 210),
                                 (int16_t)(PARM_Y + 50), 210, 16 },
                TR(SP_BEEPS_GROUP), UI_FONT_LABEL,
                ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);

    const int y0 = PARM_Y + 80;
    const int pitch = 22;
    /* The change being made; while items are counted, the first one still
     * to make, since the menu decides which comes up first. */
    uint8_t shown = e->active;
    if (e->phase != ESC_STICK_VALUES && e->phase != ESC_STICK_STORE) {
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                shown = i;
                break;
            }
        }
    }
    char what[96];
    sp_change_text(&e->ch[shown], what, sizeof(what));
    const unsigned done = esc_stick_done_count(e);
    snprintf(line, sizeof(line), TR(SP_SELECTION),
             (done < e->n) ? done + 1u : (unsigned)e->n, (unsigned)e->n,
             what);
    sp_text(c, PAD + 12, y0, line, SP_LINE_CELLS, txt);

    switch (e->phase) {
    case ESC_STICK_ARMING:
        snprintf(line, sizeof(line), "%s", TR(SP_DO_ARMING));
        break;
    case ESC_STICK_SIGNAL:
        snprintf(line, sizeof(line), TR(SP_DO_SIGNAL),
                 sp_pos(e->out.throttle_pct),
                 (unsigned)((e->now_ms - e->phase_ms < ESC_STICK_SIGNAL_MS)
                                ? ESC_STICK_SIGNAL_MS
                                      - (e->now_ms - e->phase_ms)
                                : 0u));
        break;
    case ESC_STICK_POWER:
        snprintf(line, sizeof(line), TR(SP_DO_POWER),
                 (unsigned)(e->out.supply_mv / 1000u),
                 (unsigned)(e->out.supply_mv % 1000u / 10u));
        break;
    case ESC_STICK_ENTRY: {
        const uint32_t in = e->now_ms - e->on_ms;
        const uint32_t wait = e->entry_wait;
        const uint32_t left = (in < wait) ? wait - in : 0u;
        /* A button held while the supply came on: how long still, then
         * that it can go. */
        const uint32_t hold = sp_hold_ms(e->p);
        if (hold > in) {
            snprintf(line, sizeof(line), TR(SP_DO_HOLD),
                     (unsigned)((hold - in) / 1000u),
                     (unsigned)((hold - in) % 1000u / 100u));
        } else if (hold > 0u) {
            snprintf(line, sizeof(line), TR(SP_DO_LET_GO),
                     (unsigned)(left / 1000u),
                     (unsigned)(left % 1000u / 100u));
        } else {
            snprintf(line, sizeof(line), TR(SP_DO_ENTRY),
                     sp_pos(e->out.throttle_pct),
                     (unsigned)(left / 1000u),
                     (unsigned)(left % 1000u / 100u));
        }
        break;
    }
    case ESC_STICK_ITEMS:
        line[0] = '\0';
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                snprintf(line, sizeof(line), TR(SP_DO_ITEMS),
                         (unsigned)e->p->items[e->ch[i].item].number,
                         sp_pos(esc_stick_pct(e->p->select_throttle)));
                break;
            }
        }
        break;
    case ESC_STICK_VALUES: {
        const esc_stick_change_t *ch = &e->ch[e->active];
        snprintf(line, sizeof(line), TR(SP_DO_VALUES),
                 (unsigned)e->p->items[ch->item].values[ch->value].number,
                 sp_pos(esc_stick_pct(
                     (e->kind == ESC_STICK_KIND_TWO_STAGE)
                         ? e->p->value_select_throttle
                         : e->p->select_throttle)));
        break;
    }
    case ESC_STICK_STORE:
        if (e->store_step > 0u) {
            snprintf(line, sizeof(line), TR(SP_DO_STORE_MOVE),
                     sp_pos(e->out.throttle_pct),
                     (unsigned)e->t.store_ms);
        } else {
            snprintf(line, sizeof(line), TR(SP_DO_STORE_HOLD),
                     (unsigned)e->t.store_ms);
        }
        break;
    case ESC_STICK_CYCLE:
        snprintf(line, sizeof(line), TR(SP_DO_CYCLE), (unsigned)e->t.off_ms);
        break;
    case ESC_STICK_OFF:
        snprintf(line, sizeof(line), TR(SP_DO_OFF),
                 sp_pos(e->out.throttle_pct));
        break;
    default:
        line[0] = '\0';
        break;
    }
    sp_text(c, PAD + 12, y0 + pitch, line, SP_LINE_CELLS, dim);

    if (e->groups == 0u) {
        snprintf(line, sizeof(line), "%s", TR(SP_LAST_NONE));
    } else {
        snprintf(line, sizeof(line), TR(SP_LAST),
                 (unsigned)e->last_count,
                 !e->last_valid ? TR(SP_NOT_CLEAN)
                 : e->last_trusted ? TR(SP_TWO_IN_ROW)
                 : e->last_in_order ? TR(SP_IN_ORDER) : TR(SP_NOT_IN_ORDER));
    }
    gfx_text(c, PAD + 12, y0 + 2 * pitch, line, UI_FONT_LABEL, txt, 1);
    snprintf(line, sizeof(line), TR(SP_STATS), (unsigned)e->groups,
             (unsigned)e->entries, sp_pos(e->out.throttle_pct),
             ui_on_off(e->out.supply_on));
    gfx_text(c, PAD + 12, y0 + 3 * pitch, line, UI_FONT_LABEL, dim, 1);
    snprintf(line, sizeof(line), TR(SP_CURRENT), (int)e->ma,
             (int)esc_det_floor_ma(&e->det), (unsigned)e->iv_ms);
    gfx_text(c, PAD + 12, y0 + 4 * pitch, line, UI_FONT_LABEL, dim, 1);
    sp_draw_tone(c, y0 + 5 * pitch, pitch);
    /* After the lines, so a line that reached under it would show as
     * painted over in the fit check. */
    sp_draw_tower(c, false, t->green);

    gfx_text(c, PAD + 12, HELP_Y + 9,
             TR(SP_ENDS_RUN), UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT),
             1);
    ui_button(c, pg.write_btn, TR(SP_ABORT), ui_theme_color(UI_C_DANGER),
              false, true);
}

/* The red light: a result on screen, until OK, of a run that ended on
 * something not as expected. */
static bool sp_red(void)
{
    const esc_stick_t *e = &s.st.run;
    return s.st.shown && e->phase == ESC_STICK_ABORTED
           && esc_stick_reason_is_fault(e->reason);
}

/* The result's count of the selections made, for certain: one cut short
 * while the ESC stored or confirmed it is not counted. */
static void sp_result_head(const esc_stick_t *e, char *buf, size_t n)
{
    snprintf(buf, n, TR(SP_MADE_N), esc_stick_made_count(e),
             (unsigned)e->n);
}

/* The changes past the first @p shown, as the result's last line says
 * them: how many, how many made, and the one unsure among them. */
static void sp_result_more(const esc_stick_t *e, unsigned shown, char *buf,
                           size_t n)
{
    unsigned made = 0u, unsure = 0u;
    for (unsigned i = shown; i < e->n; ++i) {
        unsure += esc_stick_unsure(e, i) ? 1u : 0u;
        made += (e->done[i] && !esc_stick_unsure(e, i)) ? 1u : 0u;
    }
    if (unsure > 0u) {
        snprintf(buf, n, TR(SP_MORE_UNSURE), (unsigned)(e->n - shown), made,
                 unsure);
    } else {
        snprintf(buf, n, TR(SP_MORE), (unsigned)(e->n - shown), made);
    }
}

static DRAWER void sp_draw_result(gfx_canvas_t *c)
{
    const esc_stick_t *e = &s.st.run;
    const bool done = e->phase == ESC_STICK_DONE;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    char line[128];
    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_RESULT), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    snprintf(line, sizeof(line), "%s", TR(SP_PH_DONE));
    if (!done) {
        snprintf(line, sizeof(line), TR(SP_ABORTED_WHY),
                 sp_reason_text(e->reason));
    }
    gfx_text(c, PAD + 12, PARM_Y + 34, line, UI_FONT_HEAD,
             done ? ui_theme_color(UI_C_OK) : ui_theme_color(UI_C_DANGER),
             1);
    const int y0 = PARM_Y + 80;
    const int pitch = 22;
    sp_result_head(e, line, sizeof(line));
    gfx_text(c, PAD + 12, y0, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    /* Five lines fit; past five, the fifth says how many more. */
    const unsigned shown = (e->n > 5u) ? 4u : e->n;
    for (unsigned i = 0; i < shown; ++i) {
        char what[96];
        sp_change_text(&e->ch[i], what, sizeof(what));
        snprintf(line, sizeof(line), "%s  %s",
                 esc_stick_unsure(e, i) ? TR(SP_MADE_CUT)
                 : e->done[i] ? TR(SP_MADE) : TR(SP_NOT_MADE), what);
        sp_text(c, PAD + 12, y0 + (int)(i + 1u) * pitch, line,
                SP_LINE_CELLS,
                e->done[i] ? ui_theme_color(UI_C_TEXT) : dim);
    }
    if (shown < e->n) {
        sp_result_more(e, shown, line, sizeof(line));
        gfx_text(c, PAD + 12, y0 + 5 * pitch, line, UI_FONT_LABEL, dim, 1);
    }
    /* Below the changes, two lines: what the profile has a person do once
     * the run is over, and after an abort that what was fitted before
     * the power-up may still be there.  Steps that need more than the two
     * lines are counted there instead, and MANUAL INTERVENTION REQUIRED
     * above -- open by itself when the run ended -- lists every one. */
    const esc_profile_t *p = e->p;
    if (p->manual_count > 0u) {
        ui_button(c, s.st.hand_btn, TR(SP_HAND_BTN),
                  ui_theme_color(UI_C_DANGER), false, true);
    }
    int y = y0 + 6 * pitch;
    int room = 2;
    int need = 0;
    if (p->manual_count > 0u && !sp_supply_reads_off()) {
        /* An abort asks the supply off; until it reads off, no hand at the
         * ESC for anything that follows. */
        sp_text(c, PAD + 12, y, TR(SP_HAND_OVER_LIVE), 93,
                ui_theme_color(UI_C_DANGER));
        y += pitch;
        room--;
    }
    /* The steps after programming: counted first, then drawn, each put
     * into the one buffer as it is needed. */
    char what[256];
    unsigned after = 0u;
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_AFTER_PROGRAMMING && after < ESC_MANUAL_MAX) {
            snprintf(what, sizeof(what), "%s: %s", sp_when_text(m),
                     sp_action(m));
            need += sp_wrap_lines(what, 93, 2);
            ++after;
        }
    }
    if (need > room && room > 0) {
        snprintf(line, sizeof(line), TR(SP_HAND_AFTER_N), after);
        sp_text(c, PAD + 12, y, line, 93, ui_theme_color(UI_C_WARN));
        y += pitch;
        room--;
    } else {
        unsigned drawn = 0u;
        for (unsigned i = 0; p->manual != NULL && i < p->manual_count
                             && drawn < after; ++i) {
            const esc_manual_t *m = &p->manual[i];
            if (m->when != ESC_MANUAL_AFTER_PROGRAMMING) {
                continue;
            }
            snprintf(what, sizeof(what), "%s: %s", sp_when_text(m),
                     sp_action(m));
            const int n = sp_wrap(c, PAD + 12, y, pitch, what, 93, 2,
                                  ui_theme_color(UI_C_WARN));
            y += n * pitch;
            room -= n;
            ++drawn;
        }
    }
    if (!done && room > 0
        && (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u
            || esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP) > 0u)) {
        sp_text(c, PAD + 12, y, TR(SP_HAND_UNDO), 93,
                ui_theme_color(UI_C_WARN));
    }
    sp_draw_tower(c, sp_red(), false);
    if (done) {
        gfx_text(c, PAD + 12, HELP_Y,
                 TR(SP_TONES_1),
                 UI_FONT_LABEL, dim, 1);
        gfx_text(c, PAD + 12, HELP_Y + 18,
                 TR(SP_TONES_2), UI_FONT_LABEL, dim, 1);
    } else {
        gfx_text(c, PAD + 12, HELP_Y, sp_reason_help(e->reason),
                 UI_FONT_LABEL, dim, 1);
        /* The supply went off while the ESC was to confirm: the value may
         * not be stored, and a Kontronik ESC whose step marks it takes
         * that for programming broken off and locks itself. */
        if (esc_stick_cut_short(e)) {
            gfx_text(c, PAD + 12, HELP_Y + 18,
                     esc_stick_lock_risk(e) ? TR(SP_HAND_LOCK)
                                            : TR(SP_HAND_CUT),
                     UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
        } else {
            gfx_text(c, PAD + 12, HELP_Y + 18, TR(SP_SAFE_NOW),
                     UI_FONT_LABEL, dim, 1);
        }
    }
    ui_button(c, pg.write_btn, "OK", ui_theme_color(UI_C_PANEL_HI), false,
              true);
}

static void sp_setting_text(setting_id_t id, char *out, size_t n)
{
    if (id == SET_STICK_V && !(settings_get(id) > 0.0f)) {
        snprintf(out, n, "%s", TR(SP_FROM_PROFILE));
        return;
    }
    char v[24];
    ui_setting_value(id, v, sizeof(v));
    snprintf(out, n, "%s %s", v, settings_def(id)->unit);
}

static DRAWER void sp_draw_timing(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD),
                             (int16_t)(PARM_Y + PARM_H - DEV_Y) },
            ui_theme_color(UI_C_PANEL));
    gfx_text(c, PAD + 12, DEV_Y + 12, TR(SP_TIMING_TITLE), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, PAD + 12, DEV_Y + 34,
             TR(SP_TIMING_NOTE), UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    const int max_scroll = SP_SETTINGS - ROWS_MAX;
    ui_button(c, pg.page_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->tscroll > 0);
    ui_button(c, pg.page_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->tscroll < max_scroll);
    for (int i = 0; i < sp_rows_shown(SP_SETTINGS, t->tscroll); ++i) {
        const int idx = t->tscroll + i;
        const setting_id_t id = k_sp_settings[idx];
        const setting_def_t *d = settings_def(id);
        const int y = ROW_Y0 + i * ROW_H;
        if (idx == t->tpicked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        gfx_text(c, PAD + 12, y, ui_setting_label(id), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        char v[40];
        sp_setting_text(id, v, sizeof(v));
        const bool moved = settings_get(id) != d->def;
        widget_enum(c, y, v, moved ? ui_theme_color(UI_C_WARN)
                                   : ui_theme_color(UI_C_ACCENT));
        const float now = settings_get(id);
        ui_button(c, pg.down[i], "-", ui_theme_color(UI_C_PANEL_HI), false,
                  now > d->min);
        ui_button(c, pg.up[i], "+", ui_theme_color(UI_C_PANEL_HI), false,
                  now < d->max);
    }
    const setting_id_t picked = k_sp_settings[t->tpicked];
    gfx_text(c, PAD + 12, HELP_Y, ui_setting_label(picked), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, PAD + 12, HELP_Y + 18, ui_setting_help(picked),
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, pg.read_btn, TR(SP_DEFAULTS), ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, pg.write_btn, TR(SUP_CLOSE), ui_theme_color(UI_C_PANEL_HI),
              false, true);
}

static DRAWER void sp_draw_items(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    const int items = sp_items();
    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_CHANGE), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    if (p->manual_count > 0u) {
        ui_button(c, t->hand_btn, TR(SP_HAND_BTN),
                  ui_theme_color(UI_C_DANGER), false, true);
    }
    const int max_scroll = (items > ROWS_MAX) ? items - ROWS_MAX : 0;
    char count[40];
    snprintf(count, sizeof(count), TR(PG_RANGE_OF), t->iscroll + 1,
             t->iscroll + sp_rows_shown(items, t->iscroll), items);
    gfx_text_in(c, (gfx_rect_t){ 420, (int16_t)(PARM_Y + 12), 260, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, pg.page_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->iscroll > 0);
    ui_button(c, pg.page_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->iscroll < max_scroll);

    for (int i = 0; i < sp_rows_shown(items, t->iscroll); ++i) {
        const int at = t->iscroll + i;
        const int idx = sp_item_at(at);
        if (idx < 0) {
            break;
        }
        const esc_item_t *it = &p->items[idx];
        const int pick = t->pick[idx];
        const int y = ROW_Y0 + i * ROW_H;
        if (idx == t->picked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)it->number);
        gfx_text(c, PAD + 12, y, num, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
        if (pick >= 0) {
            gfx_fill_round_rect(c, PAD + 38, y + 2, 3, 12, 1,
                                ui_theme_color(UI_C_WARN));
        }
        sp_text(c, PAD + 48, y, it->name, (CTRL_X - PAD - 56) / 8,
                ui_theme_color(UI_C_TEXT));
        const bool action = esc_stick_not_offered(it) != NULL;
        char v[64];
        if (action) {
            snprintf(v, sizeof(v), "%s", esc_stick_is_action(it)
                                             ? TR(SP_ACTION)
                                             : TR(SP_NOTHING_TO_CHOOSE));
        } else if (pick < 0) {
            snprintf(v, sizeof(v), "%s", TR(SP_KEEP));
        } else {
            snprintf(v, sizeof(v), "%u %s",
                     (unsigned)it->values[pick].number,
                     it->values[pick].name);
        }
        sp_cut(v, sizeof(v), CTRL_W / 8);
        widget_enum(c, y, v, (pick >= 0) ? ui_theme_color(UI_C_WARN)
                                         : ui_theme_color(UI_C_TEXT_FAINT));
        ui_button(c, pg.down[i], "-", ui_theme_color(UI_C_PANEL_HI), false,
                  !action && pick > -1);
        ui_button(c, pg.up[i], "+", ui_theme_color(UI_C_PANEL_HI), false,
                  !action && pick < (int)it->value_count - 1);
    }

    /* The picked item: its values, and where it applies. */
    if (items == 0) {
        return;
    }
    const esc_item_t *it = &p->items[t->picked];
    char line[160];
    if (it->applies_when[0] != '\0') {
        snprintf(line, sizeof(line), TR(SP_ONLY_WHEN),
                 (unsigned)it->number, it->name, it->applies_when);
    } else if (it->applies_count > 0u) {
        snprintf(line, sizeof(line), TR(SP_ONLY_ON),
                 (unsigned)it->number, it->name,
                 (unsigned)it->applies_count);
    } else {
        snprintf(line, sizeof(line), "%u %s", (unsigned)it->number,
                 it->name);
    }
    sp_text(c, PAD + 12, HELP_Y, line, 94, ui_theme_color(UI_C_TEXT_DIM));
    size_t at = 0;
    line[0] = '\0';
    for (unsigned k = 0; k < it->value_count && at + 1u < sizeof(line); ++k) {
        const int w = snprintf(line + at, sizeof(line) - at, "%s%u %s%s%s",
                               (k > 0u) ? "   " : "",
                               (unsigned)it->values[k].number,
                               it->values[k].name,
                               it->values[k].is_default ? " " : "",
                               it->values[k].is_default
                                   ? TR(SP_DEFAULT_MARK) : "");
        if (w < 0) {
            break;
        }
        at += (size_t)w;
    }
    sp_text(c, PAD + 12, HELP_Y + 18, line, 94,
            ui_theme_color(UI_C_TEXT_FAINT));

    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    const bool can = sp_can_run();
    if (t->note[0] != '\0') {
        sp_note(c, PAD + 12, pg.write_btn.x - 8, t->note,
                ui_theme_color(UI_C_WARN));
    } else {
        sp_note(c, PAD + 12, pg.write_btn.x - 8,
                (picked == 0u) ? TR(SP_PICK_VALUE) : TR(SP_UNVERIFIED),
                ui_theme_color(UI_C_TEXT_FAINT));
    }
    char run[32];
    if (picked > 0u) {
        snprintf(run, sizeof(run), TR(SP_RUN_N), (unsigned)picked);
    } else {
        snprintf(run, sizeof(run), "%s", TR(SP_RUN_BTN));
    }
    ui_button(c, pg.write_btn, run,
              can ? ui_theme_color(UI_C_DANGER)
                  : ui_theme_color(UI_C_PANEL_HI), false, can);
}

/* " (" ... ")" around a word, as the line after a number carries it. */
static const char *sp_mark(ui_text_id_t id)
{
    static char buf[40];
    snprintf(buf, sizeof(buf), " %s", ui_tr(id));
    return buf;
}

/*
 * The entry time the page shows: during a run and on its result, the wait
 * of the run's power-up, kept after its change is made; else what the
 * first change picked would wait, or with none picked the entry's.
 */
static uint32_t sp_entry_shown(void)
{
    const stick_t *t = &s.st;
    if (esc_stick_running(&t->run) || t->shown) {
        return t->run.entry_wait;
    }
    esc_stick_timing_t tm;
    sp_timing(&tm);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    const size_t n = sp_changes(ch, &picked);
    return esc_stick_change_entry_ms(t->p, (n > 0u) ? &ch[0] : NULL, &tm);
}

/* The profile page's crumb: BACK and the profile's name. */
static DRAWER void sp_draw_crumb(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    char line[128];
    if (t->model >= 0 && (unsigned)t->model < p->model_count) {
        snprintf(line, sizeof(line), "ESC STICK  >  %s  >  %s  (%s)",
                 p->brand, p->models[t->model].name, p->family);
    } else {
        snprintf(line, sizeof(line), "ESC STICK  >  %s %s", p->brand,
                 p->family);
    }
    ui_button(c, pg.back, TR(LOG_BACK), ui_theme_color(UI_C_PANEL_HI), false,
              !esc_stick_running(&t->run));
    /* Up to TIMING where the profile page shows it. */
    const int x = pg.back.x + pg.back.w + 16;
    sp_text(c, x, CRUMB_Y + 8, line,
            t->timing ? 72 : (t->timing_btn.x - 8 - x) / 8,
            ui_theme_color(UI_C_TEXT_DIM));
}

/* The profile page's two cards, and in the upper one how the profile's
 * menu works and the supply and entry time a run uses. */
static DRAWER void sp_draw_card(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    char line[128];
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD), DEV_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, PARM_Y, (int16_t)(W - 2 * PAD), PARM_H },
            ui_theme_color(UI_C_PANEL));

    const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
    snprintf(line, sizeof(line), "%s   %s   %s",
             (kind == ESC_STICK_KIND_TWO_STAGE) ? TR(SP_TWO_STAGE_MENU)
                                                : TR(SP_ONE_STAGE_MENU),
             (p->encoding == ESC_ENC_SHORT_LONG) ? TR(SP_SHORT_LONG)
                                                 : TR(SP_COUNTED),
             p->one_change_per_entry ? TR(SP_ONE_CHANGE)
                                     : TR(SP_MANY_CHANGES));
    gfx_text(c, PAD + 12, DEV_Y + 4, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    esc_stick_timing_t tm;
    sp_timing(&tm);
    if (esc_stick_running(&t->run) || t->shown) {
        mv = t->run.out.supply_mv;
        ma = t->run.out.supply_ma;
    }
    tm.entry_ms = sp_entry_shown();
    snprintf(line, sizeof(line), TR(SP_SUPPLY_ENTRY), (unsigned)(mv / 1000u),
             (unsigned)(mv % 1000u / 10u), (unsigned)(ma / 1000u),
             (unsigned)(ma % 1000u / 10u), (unsigned)(tm.entry_ms / 1000u),
             (unsigned)(tm.entry_ms % 1000u / 100u),
             (p->entry_hold_ms != 0u) ? sp_mark(TX_SP_FROM_PROFILE_MARK)
                                      : sp_mark(TX_SP_FROM_SETTING_MARK));
    gfx_text(c, PAD + 12, DEV_Y + 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, t->timing_btn, "TIMING", ui_theme_color(UI_C_PANEL_HI),
              false, !esc_stick_running(&t->run) && !t->shown);
}

/* The profile page.  Its parts are drawn one after another, so none of
 * their buffers is on the stack below the stage drawn last. */
static void sp_draw_device(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    sp_draw_crumb(c);
    if (t->timing) {
        sp_draw_timing(c);
        return;
    }
    sp_draw_card(c);
    if (esc_stick_running(&t->run)) {
        sp_draw_progress(c);
    } else if (t->shown) {
        sp_draw_result(c);
    } else {
        sp_draw_items(c);
    }
}

/*
 * The warning a run needs before it starts: in the danger colour, over the
 * session's pages, saying what the run does to the ESC and what may happen
 * to a motor on it.
 */
/* A panel over the session's pages, edged and titled in the danger colour:
 * the warning's, the manual steps' and the prompt's.  The output strip
 * above it stays in sight.  Its area. */
static gfx_rect_t sp_panel(gfx_canvas_t *c, const char *title)
{
    const gfx_rect_t a = { PAD, SP_PANEL_Y, (int16_t)(W - 2 * PAD),
                           SP_PANEL_H };
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_fill_rect(c, a.x, a.y, a.w, 52, red);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text_in(c, (gfx_rect_t){ a.x, (int16_t)(a.y + 12), a.w, 28 },
                title, UI_FONT_HEAD,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    return a;
}

/*
 * The before-power-up steps of the profile, under the warning's lines: the
 * hold that starts the run is the operator's word that they are done.
 * Then, where the run asks for more on its way, a line that it will.
 */
static DRAWER void sp_draw_warning_hand(gfx_canvas_t *c,
                                        const esc_profile_t *p,
                                        gfx_rect_t a, int y)
{
    const int last = s.st.hold_btn.y - 20;     /* the last line's top */
    const unsigned later = sp_later_steps(p);
    if (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u
        && sp_warn_gated()) {
        /* No hand at the ESC until the supply reads off. */
        (void)sp_wrap(c, a.x + 20, y, 22, TR(SP_WARN_HAND_WAIT), 92, 2,
                      ui_theme_color(UI_C_DANGER));
        return;
    }
    if (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u) {
        gfx_text(c, a.x + 20, y, TR(SP_WARN_HAND), UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
        y += 22;
        if (!sp_warn_steps_fit(p)) {
            /* Not every step fits: none is shown cut, and ALL STEPS shows
             * them, as HOLD TO RUN needs. */
            char line[128];
            snprintf(line, sizeof(line), TR(SP_WARN_HAND_READ),
                     esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER));
            sp_text(c, a.x + 44, y, line, 90, ui_theme_color(UI_C_TEXT));
            y += 22;
        } else {
            for (unsigned i = 0; i < p->manual_count; ++i) {
                const esc_manual_t *m = &p->manual[i];
                if (m->when == ESC_MANUAL_BEFORE_POWER) {
                    y += 22 * sp_wrap(c, a.x + 44, y, 22, sp_action(m), 90,
                                      2, ui_theme_color(UI_C_TEXT));
                }
            }
        }
    }
    if (later > 0u && y <= last) {
        gfx_text(c, a.x + 20, y, TR(SP_WARN_HAND_LATER), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

static DRAWER void sp_draw_warning(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    const gfx_rect_t a = sp_panel(c, TR(SP_WARN_TITLE));
    static const ui_text_id_t k_lines[] = {
        TX_SP_WARN_1, TX_SP_WARN_2, TX_SP_WARN_3, TX_SP_WARN_4,
        TX_SP_WARN_5,
    };
    const int n = (int)(sizeof(k_lines) / sizeof(k_lines[0]));
    for (int i = 0; i < n; ++i) {
        gfx_text(c, a.x + 20, a.y + 68 + i * 22, ui_tr(k_lines[i]),
                 UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
    }
    char line[128];
    /* A profile with manual steps gives up the blank line above its name
     * to the steps below. */
    const int top = a.y + 68 + ((t->p->manual_count > 0u) ? n : n + 1) * 22;
    snprintf(line, sizeof(line), "%s %s", t->p->brand, t->p->family);
    sp_text(c, a.x + 20, top, line, 90, ui_theme_color(UI_C_ACCENT));
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    snprintf(line, sizeof(line),
             (picked == 1u) ? TR(SP_WARN_SUPPLY) : TR(SP_WARN_SUPPLY_N),
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 10u),
             (unsigned)(ma / 1000u), (unsigned)(ma % 1000u / 10u),
             (unsigned)picked);
    gfx_text(c, a.x + 20, top + 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_VOLT), 1);
    /* Where the stick is when the supply comes on, beside the supply,
     * when it is not MIN: every position the run's power-ups take. */
    char at[40];
    if (sp_power_up_text(t->p, ch, picked < ESC_STICK_MAX_CHANGES
                                       ? picked : ESC_STICK_MAX_CHANGES,
                         at, sizeof(at))) {
        gfx_text(c, a.x + 20 + gfx_text_cells(line) * 8 + 24, top + 22, at,
                 UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    }
    gfx_text(c, a.x + 20, top + 44, TR(SP_WARN_UNVERIFIED),
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    sp_draw_warning_hand(c, t->p, a, top + 66);
    ui_button(c, t->hold_btn, TR(SP_HOLD_TO_RUN),
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), red,
                           t->hold.held_s),
              t->warn_down, !sp_hold_blocked());
    if (!sp_warn_steps_fit(t->p)) {
        ui_button(c, t->steps_btn, TR(SP_STEPS_BTN),
                  t->warn_read ? ui_theme_color(UI_C_PANEL_HI) : red, false,
                  true);
    }
    ui_button(c, t->cancel_btn, TR(CANCEL), ui_theme_color(UI_C_PANEL_SUNK),
              false, true);
}

/*
 * The manual steps of one profile: when each is due and what it is, and
 * whether a run asks for them.  Opened by MANUAL INTERVENTION REQUIRED,
 * by the first opening of the profile, or by a tap on a row the bench does
 * not run.
 */
static DRAWER void sp_draw_hand(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->hand_p;
    const gfx_rect_t a = sp_panel(c, TR(SP_HAND_BTN));
    char line[256];
    snprintf(line, sizeof(line), "%s %s", p->brand, p->family);
    sp_text(c, a.x + 20, a.y + 64, line, 92, ui_theme_color(UI_C_ACCENT));
    /* ESC_MANUAL_MAX steps of a label and two lines each fit above the
     * buttons at a pitch of 19. */
    int y = a.y + 90;
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        snprintf(line, sizeof(line), "%u  %s", i + 1u, sp_when_text(m));
        gfx_text(c, a.x + 20, y, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
        y += 19;
        y += 19 * sp_wrap(c, a.x + 44, y, 19, sp_action(m), 90, 2,
                          ui_theme_color(UI_C_TEXT));
        y += 4;
    }
    char why[64];
    const char *no = sp_model_why(p, t->hand_model, why, sizeof(why));
    if (t->shown && t->p == p && !sp_supply_reads_off()) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_OVER_LIVE));
    } else if (t->shown && t->p == p) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_OVER));
    } else if (no == NULL) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_ASKS));
    } else {
        snprintf(line, sizeof(line), TR(SP_HAND_NOT_RUN), no);
    }
    sp_wrap(c, a.x + 20, t->cancel_btn.y + 8, 20, line,
            (t->cancel_btn.x - a.x - 36) / 8, 2,
            (t->shown && t->p == p && !sp_supply_reads_off())
                ? ui_theme_color(UI_C_DANGER)
                : ui_theme_color(UI_C_TEXT_DIM));
    ui_button(c, t->cancel_btn, "OK", ui_theme_color(UI_C_PANEL_HI), false,
              true);
}

/*
 * A run waiting for a manual step: what to do, where the supply and the
 * stick are, and how long it waits.  DONE goes on, ABORT ends the run;
 * STOP in the band ends it too.  DONE is dark for ESC_STICK_HAND_MIN_MS
 * after the step is asked.
 */
/* What no DONE does, as the prompt says it (sp_draw_prompt(), which
 * spells the choice out for the format check): before the supply goes
 * off, that the mode may not be stored, or that the ESC may lock while any
 * of the profile's steps before the power-off is still to confirm. */
static const char *sp_left_text(const esc_stick_t *e)
{
    if (e->phase != ESC_STICK_HAND_END) {
        return TR(SP_PROMPT_LEFT);
    }
    return esc_stick_end_locks(e) ? TR(SP_PROMPT_LEFT_END)
                                  : TR(SP_PROMPT_LEFT_CUT);
}

static DRAWER void sp_draw_prompt(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const esc_manual_t *m = esc_stick_hand(e);
    const gfx_rect_t a = sp_panel(c, TR(SP_PROMPT_TITLE));
    char line[256];
    snprintf(line, sizeof(line), "%s %s", e->p->brand, e->p->family);
    sp_text(c, a.x + 20, a.y + 64, line, 92, ui_theme_color(UI_C_ACCENT));
    gfx_text(c, a.x + 20, a.y + 92, sp_when_text(m), UI_FONT_LABEL,
             ui_theme_color(UI_C_WARN), 1);
    int y = a.y + 118;
    y += 22 * sp_wrap(c, a.x + 20, y, 22, sp_action(m), 92, 3,
                      ui_theme_color(UI_C_TEXT));
    y += 12;
    const char *pos = sp_pos(e->out.throttle_pct);
    if (e->hand_menu) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_LISTEN), pos);
    } else if (e->phase == ESC_STICK_HAND_ON) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_ON), pos);
    } else if (e->phase == ESC_STICK_HAND_END) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_END), pos);
    } else if (m->when == ESC_MANUAL_AT_POWER_UP) {
        snprintf(line, sizeof(line), "%s", TR(SP_PROMPT_AT_POWER));
    } else {
        snprintf(line, sizeof(line), TR(SP_PROMPT_OFF), pos);
    }
    sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_DIM));
    y += 22;
    if (m->when == ESC_MANUAL_AT_POWER_UP && m->hold_ms != 0u) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_HOLD),
                 (unsigned)(m->hold_ms / 1000u),
                 (unsigned)(m->hold_ms % 1000u / 100u));
        sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_DIM));
        y += 22;
    }
    if (e->hand_menu) {
        sp_text(c, a.x + 20, y, TR(SP_PROMPT_HEARD), 92,
                ui_theme_color(UI_C_TEXT_DIM));
        y += 22;
    }
    /* Before the supply goes off, an end switches it off under the ESC's
     * confirmation: said here, as the result says it after. */
    snprintf(line, sizeof(line),
             (e->phase != ESC_STICK_HAND_END) ? TR(SP_PROMPT_LEFT)
             : esc_stick_end_locks(e)         ? TR(SP_PROMPT_LEFT_END)
                                              : TR(SP_PROMPT_LEFT_CUT),
             (unsigned)((esc_stick_hand_left_ms(e) + 999u) / 1000u));
    (void)sp_wrap(c, a.x + 20, y, 22, line, 92, 2,
                  ui_theme_color(UI_C_TEXT_FAINT));
    const bool ready = esc_stick_hand_ready(e);
    ui_button(c, t->hold_btn, TR(SP_HAND_DONE),
              ready ? ui_theme_color(UI_C_OK)
                    : ui_theme_color(UI_C_PANEL_SUNK), false, ready);
    ui_button(c, t->cancel_btn, TR(SP_ABORT), ui_theme_color(UI_C_DANGER),
              false, true);
}

void esc_stick_panel_render(gfx_canvas_t *c)
{
    if (s.st.hand_open && s.st.hand_p != NULL) {
        sp_draw_hand(c);
        return;
    }
    if (pg.stage == STAGE_PAGE && s.st.p != NULL
        && esc_stick_hand(&s.st.run) != NULL) {
        sp_draw_prompt(c);
        return;
    }
    if (s.st.warn && s.st.p != NULL) {
        sp_draw_warning(c);
        return;
    }
    if (pg.stage == STAGE_LIST || s.st.p == NULL) {
        sp_draw_list(c);
        return;
    }
    sp_draw_device(c);
}

/* ------------------------------------------- the application's side ----- */

void esc_stick_panel_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             uint32_t pressed, bool link_up)
{
    stick_t *t = &s.st;
    /* A stop ends a hold under way; the warning stays, and a new hold is
     * a new decision.  It ends a run too, with any ARM it queued and the
     * application has not taken: that one would be stamped with this
     * frame's count and clear the stop. */
    if (stops != t->stops) {
        sp_end_hold();
        sp_end_run(esc_stick_stop_reason(stops - t->stops,
                                         pressed - t->pressed));
    }
    t->now_ms = now_ms;
    t->armed = armed;
    t->stops = stops;
    t->pressed = pressed;
    t->link_up = link_up;
}

void esc_stick_panel_set_throttle(bool read, uint8_t channels)
{
    s.st.bind_read = read;
    s.st.throttle_mask = read ? channels : 0u;
}

void esc_stick_panel_tone(const tone_readout_t *r)
{
    if (r != NULL) {
        s.tone = *r;
    }
}

void esc_stick_panel_supply(const supply_state_t *st)
{
    stick_t *t = &s.st;
    if (st == NULL) {
        return;
    }
    /* Every reading, run or no run: whether the supply is live, and since
     * when it has read off -- its own state, the current at or under
     * ESC_STICK_OFF_MA. */
    const bool reads_off = st->online && !st->output
                           && st->mode == SUPPLY_MODE_OFF
                           && (st->ok & SUPPLY_OK_CURRENT) != 0u
                           && lroundf(st->i * 1000.0f) <= ESC_STICK_OFF_MA;
    t->sup_have = true;
    t->sup_at = st->taken_ms;
    t->sup_live = !reads_off && st->online;
    if (!reads_off) {
        t->sup_off_known = false;
    } else if (!t->sup_off_known) {
        t->sup_off_known = true;
        t->sup_off_since = st->taken_ms;
    }
    if (!esc_stick_running(&t->run)) {
        return;
    }
    const esc_stick_sample_t x = {
        .seq = st->samples,
        .at_ms = st->taken_ms,
        .ma = (int32_t)lroundf(st->i * 1000.0f),
        .current_ok = (st->ok & SUPPLY_OK_CURRENT) != 0u,
        /* What the panel asked, and apart from it what the supply reports:
         * the PD mini's output follows a link exchange and a module
         * transaction behind the request, on and off alike.  On counts
         * only when both say so, off only when the supply says so. */
        .output = st->output,
        .reported_on = st->online && st->mode != SUPPLY_MODE_OFF,
        .online = st->online,
    };
    esc_stick_sample(&t->run, &x);
    sp_follow();        /* a reading can end the run */
}

bool esc_stick_panel_poll_cmd(motor_cmd_t *out)
{
    stick_t *t = &s.st;
    if (t->qn == 0 || out == NULL) {
        return false;
    }
    *out = t->q[0];
    memmove(&t->q[0], &t->q[1], (size_t)(t->qn - 1) * sizeof(t->q[0]));
    t->qn--;
    return true;
}

const esc_profile_t *esc_stick_panel_profile(void)
{
    return esc_stick_running(&s.st.run) ? s.st.run.p : NULL;
}

uint32_t esc_stick_panel_runs(void) { return s.st.runs; }

const esc_stick_t *esc_stick_panel_stick(void) { return &s.st.run; }

const char *esc_stick_panel_search(void) { return s.st.find; }

bool esc_stick_panel_typing(void) { return s.st.tk.open; }

int esc_stick_panel_listed(int *top)
{
    if (top != NULL) {
        *top = s.st.scroll;
    }
    return s.st.count;
}

int esc_stick_panel_level(void) { return s.st.level; }

const char *esc_stick_panel_maker(void)
{
    return (s.st.level == 1 && s.st.maker != NULL) ? s.st.maker : "";
}

int esc_stick_panel_model(void)
{
    return (pg.stage == STAGE_PAGE) ? s.st.model
                                                               : -1;
}

const esc_profile_t *esc_stick_panel_row(int i, int *model)
{
    const stick_t *t = &s.st;
    if (t->level != 1 || i < 0 || i >= t->count) {
        return NULL;
    }
    if (model != NULL) {
        *model = t->rows_m[i].model;
    }
    return esc_profiles_at(t->rows_m[i].prof);
}

const char *esc_stick_panel_row_why(int i)
{
    static char why[64];
    int model = -1;
    const esc_profile_t *p = esc_stick_panel_row(i, &model);
    return (p != NULL) ? sp_model_why(p, model, why, sizeof(why)) : NULL;
}

int esc_stick_panel_item_at(int i)
{
    return sp_item_at(i);
}

const char *esc_stick_panel_row_warn(int i)
{
    static char warn[64];
    int model = -1;
    const esc_profile_t *p = esc_stick_panel_row(i, &model);
    return (p != NULL && esc_stick_panel_row_why(i) == NULL)
               ? sp_model_warn(p, model, warn, sizeof(warn)) : NULL;
}

const char *esc_stick_panel_result_head(void)
{
    static char line[128];
    sp_result_head(&s.st.run, line, sizeof(line));
    return line;
}

const char *esc_stick_panel_result_more(void)
{
    static char line[128];
    const unsigned shown = (s.st.run.n > 5u) ? 4u : s.st.run.n;
    line[0] = '\0';
    if (shown < s.st.run.n) {
        sp_result_more(&s.st.run, shown, line, sizeof(line));
    }
    return line;
}

const char *esc_stick_panel_left_text(void)
{
    return sp_left_text(&s.st.run);
}

const char *esc_stick_panel_note(void)
{
    return s.st.note;
}

const char *esc_stick_panel_maker_at(int i)
{
    const stick_t *t = &s.st;
    return (t->level == 0 && i >= 0 && i < t->count) ? t->makers[i].name
                                                     : NULL;
}

uint32_t esc_stick_panel_entry_shown(void)
{
    return (s.st.p != NULL) ? sp_entry_shown() : 0u;
}

bool esc_stick_panel_supply_reads_off(void)
{
    return sp_supply_reads_off();
}

const char *esc_stick_panel_hand_why(void)
{
    const stick_t *t = &s.st;
    static char why[64];
    if (!t->hand_open || t->hand_p == NULL) {
        return NULL;
    }
    return sp_model_why(t->hand_p, t->hand_model, why, sizeof(why));
}

const esc_profile_t *esc_stick_panel_page(void)
{
    return (pg.stage == STAGE_PAGE) ? s.st.p
                                                               : NULL;
}

bool esc_stick_panel_hand_shown(void)
{
    return s.st.hand_open && s.st.hand_p != NULL;
}

void esc_stick_panel_lights(bool *red, bool *green)
{
    if (red != NULL) {
        *red = sp_red();
    }
    if (green != NULL) {
        *green = esc_stick_running(&s.st.run) && s.st.green;
    }
}
