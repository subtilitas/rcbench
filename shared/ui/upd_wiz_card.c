/**
 * @file upd_wiz_card.c
 * @brief UPDATES' card: the current step's heading, its versions, what
 *        its update does, why it waits and how its last attempt ended; or,
 *        with no step current, the last card: one of three headings
 *        (upd_wiz_end()), the running version, the home network, the last
 *        check's result and the steps not connected, not updatable here
 *        or skipped.  See upd_wiz_view.h.
 *
 * The card's text is 12 x 24, 41 cells to the line, 28px apart, from
 * body y 32.  It ends 8px over the card's control when the card has one,
 * else over SKIP THIS STEP, else 12px over the card's foot.  What does not
 * fit gives way, last first: the lines that say what an update does and
 * the notes on the last card go, the versions, the reasons and the
 * results stay.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen.h"
#include "upd_screen_int.h"
#include "upd_wiz_view.h"

/** @brief The paragraphs a card holds at most. */
#define PARA_MAX 12
/** @brief Bytes of one paragraph. */
#define PARA_LEN 192
/** @brief The first line's top. */
#define TEXT_Y (UPD_WIZ_CARD_Y + UPD_WIZ_PAD)
/** @brief The text's left edge. */
#define TEXT_X (UPD_WIZ_CARD_X + UPD_WIZ_PAD)
/** @brief The text's width: 41 cells of UPD_FONT_BODY. */
#define TEXT_W (UPD_WIZ_CARD_W - 2 * UPD_WIZ_PAD)

/** @brief One paragraph of the card. */
typedef struct {
    char        t[PARA_LEN]; /**< the text */
    gfx_color_t col;         /**< its colour */
    int         gap;         /**< px over its first line */
    bool        keep;        /**< it stays when the card is full */
} para_t;

/** @brief A card's paragraphs. */
typedef struct {
    para_t p[PARA_MAX]; /**< in their order */
    int    n;           /**< how many */
} card_t;

/** @brief The card's paragraphs, off the render task's stack. */
static card_t s_card;

/** @brief The builders' scratch line, off the render task's stack: each
 *         text is copied into a paragraph before the next is written. */
static char s_line[PARA_LEN];
/** @brief The names of the steps a line of the last card lists, or the
 *         first line of why CHECK is grey. */
static char s_names[96];
/** @brief The line under a result or a reason. */
static char s_second[96];

/**
 * @brief Adds a paragraph; an empty text adds none.
 * @param t    the text
 * @param col  its colour
 * @param gap  px over its first line
 * @param keep whether it stays when the card is full
 */
static void add(const char *t, gfx_color_t col, int gap, bool keep)
{
    if (t == NULL || t[0] == '\0' || s_card.n >= PARA_MAX) {
        return;
    }
    para_t *p = &s_card.p[s_card.n++];
    (void)snprintf(p->t, sizeof(p->t), "%s", t);
    p->col = col;
    p->gap = gap;
    p->keep = keep;
}

/**
 * @brief The lines a text takes in the card's column.
 * @param t the text
 * @return the lines
 */
static int lines_of(const char *t)
{
    return ui_text_wrap(NULL, gfx_rect_make(TEXT_X, 0, TEXT_W,
                                            gfx_text_height(UPD_FONT_BODY,
                                                            1)),
                        PITCH, t, UPD_FONT_BODY, UI_TEXT);
}

/**
 * @brief The foot of the paragraphs from TEXT_Y: their last line's bottom.
 * @return the y
 */
static int foot_of(void)
{
    int y = TEXT_Y;
    for (int i = 0; i < s_card.n; ++i) {
        y += s_card.p[i].gap + PITCH * lines_of(s_card.p[i].t);
    }
    return y - (PITCH - gfx_text_height(UPD_FONT_BODY, 1));
}

/**
 * @brief Drops paragraphs until the rest end at @p bottom: the last one
 *        that may go first, then, if still too long, the last one.
 * @param bottom the lowest y the text may reach
 */
static void fit(int bottom)
{
    while (s_card.n > 0 && foot_of() > bottom) {
        int drop = s_card.n - 1;
        for (int i = s_card.n - 1; i >= 0; --i) {
            if (!s_card.p[i].keep) {
                drop = i;
                break;
            }
        }
        for (int i = drop; i + 1 < s_card.n; ++i) {
            s_card.p[i] = s_card.p[i + 1];
        }
        --s_card.n;
    }
}

/**
 * @brief A version's text, or @p none's while it is not known.
 * @param ver  the step's version, "-" when not known
 * @param none the text for not known
 * @return the text
 */
static const char *ver_or(const char *ver, const char *none)
{
    return (ver[0] == '\0' || strcmp(ver, "-") == 0) ? none : ver;
}

/**
 * @brief The running and the included version, and the warning when the
 *        included one is older.
 * @param st       the step
 * @param none     the text for a version not known
 * @param included the image is in the panel's (the IOmcu's); else it is
 *                 the release's file, fetched when the hold asks for it
 */
static void versions(const upd_wiz_step_t *st, const char *none,
                     bool included)
{
    (void)snprintf(s_line, sizeof(s_line), TR(UPD_CU_RUNNING),
                   ver_or(st->ver, none));
    add(s_line, UI_TEXT, 8, true);
    if (included) {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_CU_CARRIED),
                       ver_or(st->next, TR(UPD_C6_NONE)));
    } else {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_WIZ_FOR_PANEL),
                       ver_or(st->next, TR(UPD_C6_NONE)));
    }
    add(s_line, UI_TEXT, 0, true);
    if (st->older) {
        add(TR(CU_Q_OLDER), UI_WARN, 0, true);
    }
}

/**
 * @brief The result lines of the last check or download, as the card had
 *        them before the steps: the line and the one under it.
 * @param v the panel's view
 */
static void panel_result(const upd_view_t *v)
{
    upd_result_text(v, s_line, sizeof(s_line), s_second, sizeof(s_second));
    if (s_line[0] != '\0' && !upd_result_said_above(v)) {
        add(s_line, upd_busy(v) ? UI_TEXT : upd_result_colour(v), 8, true);
        add(s_second, UI_TEXT_DIM, 0, true);
    }
}

/**
 * @brief The home network, or that none is kept, or that Wi-Fi is not
 *        available.
 * @param v the panel's view
 */
static void home_lines(const upd_view_t *v)
{
    if (v->unavailable) {
        add(TR(WF_UNAVAIL_1), UI_WARN, 0, true);
        add(TR(WF_UNAVAIL_2), UI_WARN, 0, true);
    } else if (v->have_net) {
        (void)snprintf(s_line, sizeof(s_line), TR(WF_HOME_IS), v->net);
        add(s_line, UI_TEXT_DIM, 0, false);
    } else {
        add(TR(WF_HOME_NONE), UI_WARN, 0, true);
    }
}

/**
 * @brief PANEL's card: the running version, the check's result, the home
 *        network, what the install does, why it waits.
 * @param w the steps
 */
static void panel_card(const upd_wiz_t *w)
{
    const upd_view_t *v = upd_screen_view();
    const upd_wiz_step_t *st = &w->step[UPD_WIZ_PANEL];
    (void)snprintf(s_line, sizeof(s_line), TR(WF_Q_RUN), v->running);
    add(s_line, UI_TEXT, 8, true);
    panel_result(v);
    home_lines(v);
    if (st->state == UPD_WIZ_OPEN || st->state == UPD_WIZ_HELD) {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_WIZ_DO_PANEL),
                       st->next);
        add(s_line, UI_TEXT_DIM, 12, false);
    }
}

/**
 * @brief The IOmcu's card: its versions, what the update does, the
 *        section's line while it waits, runs or is held.
 * @param w the steps
 */
static void iomcu_card(const upd_wiz_t *w)
{
    const upd_wiz_step_t *st = &w->step[UPD_WIZ_IOMCU];
    versions(st, TR(UPD_CU_NOT_KNOWN), true);
    if (st->state == UPD_WIZ_WAITING) {
        add(TR(UPD_WIZ_WAIT_IOMCU), UI_WARN, 12, true);
    } else if (st->state != UPD_WIZ_OPEN && st->state != UPD_WIZ_FAILED) {
        const char *t = "";
        gfx_color_t col = UI_TEXT;
        upd_cu_text(upd_screen_coproc(), &t, &col);
        add(t, col, 12, true);
    }
    if (st->state != UPD_WIZ_RUNNING && st->state != UPD_WIZ_PENDING) {
        add(TR(CU_Q_WHAT), UI_TEXT_DIM, 12, false);
    }
}

/**
 * @brief The C6's card: its versions, that the panel restarts after it,
 *        why it waits.
 * @param w the steps
 */
static void c6_card(const upd_wiz_t *w)
{
    const upd_wiz_step_t *st = &w->step[UPD_WIZ_C6];
    versions(st, TR(UPD_C6_NOT_REPORTED), false);
    const upd_c6_view_t *cv = upd_c6_view();
    upd_fetch_line(&cv->fetch, s_line, sizeof(s_line));
    add(s_line, upd_fetch_warn(&cv->fetch) ? UI_WARN : UI_TEXT_DIM, 12,
        true);
    add(TR(UPD_WIZ_DO_C6), UI_TEXT, 12, false);
    const char *why = upd_c6_reason(upd_c6_view());
    if (why != NULL) {
        add(why, st->state == UPD_WIZ_HELD ? UI_WARN : UI_TEXT, 12, true);
    }
}

/**
 * @brief A device's card: its versions, how it accepts, its ask while it
 *        waits for it, why it waits.
 * @param w the steps
 */
static void dev_card(const upd_wiz_t *w)
{
    const unsigned i = w->current;
    const upd_wiz_step_t *st = &w->step[i];
    const upd_dev_id_t id = (upd_dev_id_t)(i - (unsigned)UPD_WIZ_DIAL);
    const upd_dev_view_t *v = upd_dev_view(id);
    const bool dial = id == UPD_DEV_DIAL;
    versions(st, TR(UPD_DEV_NOT_LINKED), false);
    upd_fetch_line(&v->fetch, s_line, sizeof(s_line));
    add(s_line, upd_fetch_warn(&v->fetch) ? UI_WARN : UI_TEXT_DIM, 12,
        true);
    if (st->state == UPD_WIZ_RUNNING) {
        /* While the image is on its way, the control says what runs. */
        if (v->fetch.run == UPD_FETCH_IDLE) {
            add(v->phase == DEVUPD_CONFIRM
                    ? (dial ? TR(UPD_WIZ_ASK_DIAL) : TR(UPD_WIZ_ASK_NODE))
                    : TR(DEV_WHY_UPDATING),
                UI_TEXT, 12, true);
        }
    } else if (st->state == UPD_WIZ_WAITING) {
        add(dial ? TR(UPD_WIZ_WAIT_DIAL) : TR(UPD_WIZ_WAIT_NODE), UI_WARN,
            12, true);
    } else if (st->state == UPD_WIZ_HELD || st->state == UPD_WIZ_PENDING) {
        const char *why = upd_dev_reason(v);
        add(why != NULL ? why : "",
            st->state == UPD_WIZ_HELD ? UI_WARN : UI_TEXT, 12, true);
    }
    if (st->state != UPD_WIZ_RUNNING && st->state != UPD_WIZ_PENDING) {
        add(dial ? TR(UPD_WIZ_DO_DIAL) : TR(UPD_WIZ_DO_NODE), UI_TEXT_DIM,
            12, false);
    }
}

/**
 * @brief The names of the steps whose tag is one of @p tags, joined by
 *        ", ".
 * @param w     the steps
 * @param tags  the tags
 * @param ntags how many
 * @param state the state they must be in
 * @param out   the names, empty for none
 * @param n     bytes of @p out
 */
static void names_of(const upd_wiz_t *w, const upd_wiz_tag_t *tags,
                     size_t ntags, upd_wiz_state_t state, char *out, size_t n)
{
    out[0] = '\0';
    size_t len = 0u;
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        bool hit = false;
        for (size_t k = 0u; k < ntags; ++k) {
            hit = hit || w->step[i].tag == tags[k];
        }
        if (!hit || w->step[i].state != state || len >= n) {
            continue;
        }
        const int got = snprintf(out + len, n - len, "%s%s",
                                 len > 0u ? ", " : "", upd_wiz_name(i));
        len = got < 0 ? n : len + (size_t)got;
    }
}

/**
 * @brief The last card under its heading: the running version, the home
 *        network, the last job's result, the steps not connected, not
 *        updatable here (a device OFF on WIRELESS or without Bluetooth LE
 *        among them) and skipped,
 *        and the notes on the reminder and RELEASES.
 * @param w the steps
 */
static void end_card(const upd_wiz_t *w)
{
    static const upd_wiz_tag_t k_gone[] = {
        UPD_WIZ_TAG_NO_LINK, UPD_WIZ_TAG_NOT_CONNECTED,
    };
    static const upd_wiz_tag_t k_other[] = {
        UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_TAG_BAD_IMAGE,
        UPD_WIZ_TAG_CHECK_FAILED, UPD_WIZ_TAG_USB_ONLY,
        UPD_WIZ_TAG_LINK_FAILED, UPD_WIZ_TAG_NOT_REPORTED,
        UPD_WIZ_TAG_OFF, UPD_WIZ_TAG_NO_BLE,
    };
    static const upd_wiz_tag_t k_skipped[] = { UPD_WIZ_TAG_SKIPPED };
    const upd_view_t *v = upd_screen_view();
    (void)snprintf(s_line, sizeof(s_line), TR(WF_Q_RUN), v->running);
    add(s_line, UI_TEXT, 8, true);
    home_lines(v);
    panel_result(v);
    /* Why CHECK is grey, when the result did not already say so. */
    if (!v->unavailable && v->have_net && !upd_busy(v)
        && v->block != UPD_WHY_NONE
        && !(v->got == UPD_GOT_BLOCKED && v->why == v->block)) {
        upd_blocked_text(v->block, s_names, sizeof(s_names), s_second,
                         sizeof(s_second));
        add(s_names, UI_WARN, 8, true);
        add(s_second, UI_WARN, 0, true);
    }
    names_of(w, k_gone, sizeof(k_gone) / sizeof(k_gone[0]), UPD_WIZ_ABSENT,
             s_names, sizeof(s_names));
    if (s_names[0] != '\0') {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_WIZ_END_NOT_CONN),
                       s_names);
        add(s_line, UI_TEXT, 12, true);
        add(TR(UPD_WIZ_END_CONNECT), UI_TEXT_DIM, 0, false);
    }
    names_of(w, k_other, sizeof(k_other) / sizeof(k_other[0]),
             UPD_WIZ_ABSENT, s_names, sizeof(s_names));
    if (s_names[0] != '\0') {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_WIZ_END_OTHER),
                       s_names);
        add(s_line, UI_TEXT, 12, true);
    }
    names_of(w, k_skipped, 1u, UPD_WIZ_SKIPPED, s_names, sizeof(s_names));
    if (s_names[0] != '\0') {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_WIZ_END_SKIPPED),
                       s_names);
        add(s_line, UI_TEXT, 12, true);
    }
    /* The release line's end when it is capped, the reminder and
     * RELEASES: they give way first.  The line's end in UI_TEXT. */
    if (v->line_capped) {
        add(TR(UPD_LAST_LINE), UI_TEXT, 12, false);
    }
    add(TR(UPD_ABOUT_1), UI_TEXT_DIM, v->line_capped ? 0 : 12, false);
    add(TR(UPD_ABOUT_2), UI_TEXT_DIM, 0, false);
    add(v->line_capped ? TR(UPD_ABOUT_3_LINE) : TR(UPD_ABOUT_3),
        UI_TEXT_DIM, 0, false);
}

/**
 * @brief The last card's heading: all firmware is current only when every
 *        step is; no panel release was looked for when only PANEL's check
 *        is missing or failed; else no step is open.
 * @param w the steps
 */
static void end_head(const upd_wiz_t *w)
{
    switch (upd_wiz_end(w)) {
    case UPD_WIZ_END_CURRENT:
        add(TR(UPD_WIZ_DONE_HEAD), UI_OK, 0, true);
        break;
    case UPD_WIZ_END_NO_CHECK:
        add(TR(UPD_WIZ_END_NO_CHECK), UI_TEXT, 0, true);
        break;
    default:
        add(TR(UPD_WIZ_END_NONE_OPEN), UI_TEXT, 0, true);
        break;
    }
}

/**
 * @brief The lowest y the card's text reaches: 8px over its control, else
 *        over SKIP, else 12px over its foot.
 * @param w the steps
 * @return the y
 */
static int text_bottom(const upd_wiz_t *w)
{
    if (w->act != UPD_WIZ_ACT_NONE || upd_wiz_net_shown(w)) {
        return UPD_WIZ_ACT_Y - 8;
    }
    if (w->can_skip || w->show_skipped) {
        return UPD_WIZ_SKIP_Y - 8;
    }
    return UPD_WIZ_CARD_Y + UPD_WIZ_CARD_H - 12;
}

int upd_wiz_card_build(const upd_wiz_t *w)
{
    s_card.n = 0;
    if (w->current >= UPD_WIZ_STEPS) {
        end_head(w);
        end_card(w);
    } else {
        (void)snprintf(s_line, sizeof(s_line), TR(UPD_WIZ_HEAD),
                       w->current + 1u, (unsigned)UPD_WIZ_STEPS,
                       upd_wiz_name(w->current));
        add(s_line, UI_ACCENT, 0, true);
        switch (w->current) {
        case UPD_WIZ_PANEL: panel_card(w); break;
        case UPD_WIZ_IOMCU: iomcu_card(w); break;
        case UPD_WIZ_C6:    c6_card(w);    break;
        default:            dev_card(w);   break;
        }
        const upd_wiz_step_t *st = &w->step[w->current];
        if (st->result != NULL) {
            add(st->result, UI_WARN, 12, true);
        }
        if (st->state == UPD_WIZ_HELD && w->current == UPD_WIZ_PANEL) {
            upd_wiz_why_text(w, s_line, sizeof(s_line));
            add(s_line, UI_WARN, 12, true);
        }
    }
    fit(text_bottom(w));
    return s_card.n;
}

const char *upd_wiz_card_para(int i)
{
    return (i >= 0 && i < s_card.n) ? s_card.p[i].t : NULL;
}

void upd_wiz_card_render(gfx_canvas_t *c, const upd_wiz_t *w)
{
    (void)upd_wiz_card_build(w);
    ui_card(c, upd_wiz_card_rect(), UI_PANEL);
    const int h = gfx_text_height(UPD_FONT_BODY, 1);
    int y = TEXT_Y;
    for (int i = 0; i < s_card.n; ++i) {
        const para_t *p = &s_card.p[i];
        y += p->gap;
        y += PITCH * ui_text_wrap(c, gfx_rect_make(TEXT_X, y, TEXT_W, h),
                                  PITCH, p->t, UPD_FONT_BODY, p->col);
    }
}
