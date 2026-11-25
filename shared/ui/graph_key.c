/**
 * @file
 * @brief GRAPH on the result cards (graph_key.h): each card's last run, the
 *        key's state and look, the tap that hands the file to LOGS, and the
 *        reason a tap opened nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#include "graph_key.h"

#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "log_name.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The note's room: MO_LOG_MISSING with a labelled name. */
#define GK_NOTE_MAX 96

/** @brief One card's key. */
typedef struct {
    bool     run;                       /**< a run whose CSV is or comes  */
    bool     open;                      /**< the run or its file is open  */
    char     name[LOG_LABELLED_SIZE];   /**< the CSV, "" while not known  */
    char     dir[CARD_DIR_MAX];         /**< its folder                   */
    unsigned mount;                     /**< the mount it was written on  */
    /** @brief The reason the last tap opened nothing, "" for none. */
    char     note[GK_NOTE_MAX];
    graph_key_state_t note_state;       /**< the state it was given in    */
    char     note_name[LOG_LABELLED_SIZE]; /**< the file it was given for */
} gk_t;

/** @brief Every card's key; zero is no run, as at power-on. */
static gk_t g[GRAPH_KEY_COUNT];

/**
 * @brief Card @p k's record, or NULL for a card outside the enum.
 * @param k the card
 * @return the record
 */
static gk_t *rec(graph_key_card_t k)
{
    return ((unsigned)k < (unsigned)GRAPH_KEY_COUNT) ? &g[k] : NULL;
}

void graph_key_reset(void)
{
    memset(g, 0, sizeof(g));
}

void graph_key_set(graph_key_card_t k, const graph_key_file_t *f)
{
    gk_t *r = rec(k);
    if (r == NULL) {
        return;
    }
    if (f == NULL) {
        r->run = false;
        r->open = false;
        r->name[0] = '\0';
        return;
    }
    r->run = f->run;
    r->open = f->open;
    snprintf(r->name, sizeof(r->name), "%s",
             (f->name != NULL) ? f->name : "");
    snprintf(r->dir, sizeof(r->dir), "%s", (f->dir != NULL) ? f->dir : "");
    r->mount = f->mount;
}

/**
 * @brief The state of record @p r.
 * @param r the record
 * @return its state
 */
static graph_key_state_t state_of(const gk_t *r)
{
    if (!r->run) {
        return GRAPH_KEY_HIDDEN;
    }
    if (r->open || r->name[0] == '\0') {
        return GRAPH_KEY_WRITING;
    }
    const ui_bench_status_t *st = ui_router_status();
    if (st != NULL && st->armed) {
        return GRAPH_KEY_ARMED;
    }
    return GRAPH_KEY_LIVE;
}

graph_key_state_t graph_key_state(graph_key_card_t k)
{
    const gk_t *r = rec(k);
    return (r != NULL) ? state_of(r) : GRAPH_KEY_HIDDEN;
}

/**
 * @brief @p src into @p dst, cut to @p n - 1 bytes and ended.
 * @param dst takes it
 * @param n   bytes at @p dst, at least 1
 * @param src the text
 */
static void copy(char *dst, size_t n, const char *src)
{
    size_t len = 0u;
    while (len + 1u < n && src[len] != '\0') {
        ++len;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 * @brief Keep @p text as record @p r's note, given in state @p st.
 * @param r    the record
 * @param st   the state
 * @param text the reason
 */
static void note(gk_t *r, graph_key_state_t st, const char *text)
{
    copy(r->note, sizeof(r->note), text);
    r->note_state = st;
    memcpy(r->note_name, r->name, sizeof(r->note_name));
}

/**
 * @brief Hand record @p r's file to LOGS and enter it.
 * @param r the record
 * @return true when LOGS opened the file
 */
static bool open_in_logs(gk_t *r)
{
    log_viewer_tab_t tab;
    if (!log_viewer_tab_of(r->dir, &tab)
        || log_viewer_show(tab, r->name, r->mount) != LOG_SHOW_OPEN) {
        /* The folder no tab lists now, the card swapped since the run, or
         * the file renamed or deleted: the card in the slot has no such
         * file. */
        char t[GK_NOTE_MAX];
        snprintf(t, sizeof(t), TR(MO_LOG_MISSING), r->name);
        note(r, GRAPH_KEY_LIVE, t);
        return false;
    }
    r->note[0] = '\0';
    ui_router_goto(SCREEN_LOGS);
    log_viewer_back_leaves();
    return true;
}

bool graph_key_tap(graph_key_card_t k)
{
    gk_t *r = rec(k);
    if (r == NULL) {
        return false;
    }
    const graph_key_state_t st = state_of(r);
    switch (st) {
    case GRAPH_KEY_LIVE:
        return open_in_logs(r);
    case GRAPH_KEY_ARMED:
        note(r, st, TR(GK_ARMED));
        return false;
    case GRAPH_KEY_WRITING:
        note(r, st, TR(GK_WRITING));
        return false;
    default:
        return false;
    }
}

const char *graph_key_note(graph_key_card_t k)
{
    gk_t *r = rec(k);
    if (r == NULL || r->note[0] == '\0') {
        return NULL;
    }
    /* A reason that no longer holds goes: the key's state or its file
     * changed since the tap. */
    if (state_of(r) != r->note_state || strcmp(r->name, r->note_name) != 0) {
        r->note[0] = '\0';
        return NULL;
    }
    return r->note;
}

void graph_key_draw(gfx_canvas_t *c, gfx_rect_t r, graph_key_card_t k,
                    bool pressed, const gfx_font_t *font)
{
    const graph_key_state_t st = graph_key_state(k);
    if (c == NULL || st == GRAPH_KEY_HIDDEN || r.w <= 0 || r.h <= 0) {
        return;
    }
    ui_button_font(c, r, TR(GK_KEY), ui_theme_color(UI_C_PANEL_HI), pressed,
                   st == GRAPH_KEY_LIVE, font);
}

uint32_t graph_key_sig(graph_key_card_t k)
{
    const char *n = graph_key_note(k);
    uint32_t h = 2166136261u;           /* FNV-1a over the state and note */
    h = (h ^ (uint32_t)graph_key_state(k)) * 16777619u;
    for (const char *p = (n != NULL) ? n : ""; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h;
}
