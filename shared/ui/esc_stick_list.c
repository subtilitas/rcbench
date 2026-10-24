/**
 * @file esc_stick_list.c
 * @brief ESC STICK's profile list.
 *
 * ESC STICK's profile list: the texts a row and a page are drawn from,
 * why a profile cannot run on this bench, the makers and the models built
 * from the profiles and the search, and the profile picked.  The state and
 * the seam are esc_stick_panel_int.h.
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
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD) and s names the state, which no header included after it
 * may meet. */
#include "esc_stick_panel_int.h"

/* ============================================== the stick class ======== */

/*
 * A text cut to @p max_chars, with ".." in place of what did not fit: the
 * profiles' names are the manuals' and run long.
 */
void sp_text(gfx_canvas_t *c, int x, int y, const char *text,
                    int max_chars, gfx_color_t ink)
{
    char buf[192];
    sp_copy(buf, sizeof(buf), text);
    sp_cut(buf, sizeof(buf), max_chars);
    gfx_text(c, x, y, buf, SP_FONT, ink, 1);
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

const char *sp_action(const esc_manual_t *m)
{
    return esc_stick_panel_step_text(m);
}

/* When a manual step is due, in the language showing. */
const char *sp_when_text(const esc_manual_t *m)
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

const char *sp_why_text(const char *why)
{
    return esc_stick_panel_why_text(why);
}

/*
 * What the supply's set points break of model @p model's ratings
 * (esc_stick_rating()), into @p buf, or NULL: the short form for a row,
 * the long one for the page's note.
 */
const char *sp_rating_why(const esc_profile_t *p, int model,
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
const char *sp_rating_warn(const esc_profile_t *p, int model,
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
const char *sp_model_warn(const esc_profile_t *p, int model, char *buf,
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
const char *sp_model_why(const esc_profile_t *p, int model, char *buf,
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
uint32_t sp_list_key(void)
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

bool sp_model_runs(const esc_profile_t *p, int model)
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
void sp_build_list(bool keep_scroll)
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

void sp_pick_profile(const esc_profile_t *p)
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
int sp_items(void)
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

int sp_item_at(int at)
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
void sp_pick_shown(void)
{
    stick_t *t = &s.st;
    if (t->p == NULL
        || esc_item_applies(t->p, (unsigned)t->picked, t->model)) {
        return;
    }
    const int first = sp_item_at(0);
    t->picked = (first >= 0) ? first : 0;
}

