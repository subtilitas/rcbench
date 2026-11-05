/**
 * @file help_check.h
 * @brief The screen guide's page check the help suites share: every page
 *        of a screen, as the bench is now, fits its dock and marks controls
 *        the dock leaves in view (check_pages()), and the same over every
 *        language, theme, SAFE and ARMED, link up and down
 *        (check_every_state()).
 *
 * Only static helpers.  Include after greatest.h.
 *
 * Used by:
 * - test_help_pages.c
 * - test_help_setup.c
 * - test_help_outputs.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "ui_band.h"
#include "ui_help.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/**
 * @brief Whether @p a and @p b come closer than @p gap px.
 * @param a   A rect.
 * @param b   Another.
 * @param gap The distance, px; 0 for an overlap.
 * @return True when they do.
 */
static bool __attribute__((unused)) near(gfx_rect_t a, gfx_rect_t b, int gap)
{
    return a.x < b.x + b.w + gap && b.x < a.x + a.w + gap
           && a.y < b.y + b.h + gap && b.y < a.y + a.h + gap;
}

/**
 * @brief Whether @p in lies inside @p out.
 * @param in  The inner rect.
 * @param out The outer rect.
 * @return True when it does.
 */
static bool __attribute__((unused)) inside(gfx_rect_t in, gfx_rect_t out)
{
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w
           && in.y + in.h <= out.y + out.h;
}

/**
 * @brief Whether @p a and @p b are the same rect.
 * @param a A rect.
 * @param b Another.
 * @return True when x, y, w and h match.
 */
static bool __attribute__((unused)) same(gfx_rect_t a, gfx_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/**
 * @brief Every page of @p scr, on top, as the bench is now: PREV and NEXT
 *        in one place over no chip, "?" or STOP; 1 to 5 items, each with a
 *        label and a text of 3 rows at most, in the live half or the band
 *        and under no dock or button; badges in the live half, 2px apart;
 *        on a page for ARMED no safe control and no must_show under the
 *        dock.  A finding fails the case with @p what.
 * @param scr  The screen.
 * @param what The state, for a failure.
 */
static void __attribute__((unused))
check_pages(const ui_screen_t *scr, const char *what)
{
    /* PREV and NEXT where page 1 has them, on every page. */
    const bool armed = ui_router_status()->armed;
    ui_help_page_t p0;
    if (!ui_help_page(scr, armed, 0, &p0)) {
        T_FAIL("%s: no page 1", what);
        return;
    }
    const gfx_rect_t prev0 = ui_help_button_rect(p0.side, UI_HELP_HIT_PREV);
    const gfx_rect_t next0 = ui_help_button_rect(p0.side, UI_HELP_HIT_NEXT);
    /* And clear of every chip the band shows now, "?" and STOP. */
    for (int chip = UI_BAND_CHIP_NONE + 1; chip < UI_BAND_CHIP_COUNT;
         ++chip) {
        gfx_rect_t r;
        if (ui_band_chip_rect(ui_router_status(), (ui_band_chip_t)chip, &r)
            && (near(r, prev0, 2) || near(r, next0, 2))) {
            T_FAIL("%s: chip %d at x %d is under PREV or NEXT", what, chip,
                   r.x);
        }
    }
    if (near(ui_band_guide_rect(), next0, 2)
        || near(ui_band_stop_rect(), next0, 2)) {
        T_FAIL("%s: NEXT covers \"?\" or STOP", what);
    }
    const int count = ui_help_count(scr, armed);
    if (count != scr->help_count() && !armed) {
        T_FAIL("%s: %d pages offered, %d given", what, count,
               scr->help_count());
    }
    const bool note = ui_help_armed_note(scr, armed);
    const gfx_rect_t band = gfx_rect_make(0, 0, UI_PANEL_W, UI_BAND_H);
    for (int n = 0; n < count; ++n) {
        ui_help_page_t p;
        if (!ui_help_page(scr, armed, n, &p)) {
            T_FAIL("%s: page %d missing", what, n + 1);
            continue;
        }
        const gfx_rect_t dock = ui_help_dock_rect(p.side);
        const gfx_rect_t live = ui_help_live_rect(p.side);
        const gfx_rect_t prev = ui_help_button_rect(p.side,
                                                    UI_HELP_HIT_PREV);
        const gfx_rect_t next = ui_help_button_rect(p.side,
                                                    UI_HELP_HIT_NEXT);
        if (!same(next, next0) || !same(prev, prev0)) {
            T_FAIL("%s: NEXT at x %d y %d on page %d, x %d y %d on page 1",
                   what, next.x, next.y, n + 1, next0.x, next0.y);
        }
        if (p.count < 1 || p.count > UI_HELP_ITEMS) {
            T_FAIL("%s: page %d has %d items", what, n + 1, p.count);
        }
        if (ui_tr(p.title)[0] == '\0') {
            T_FAIL("%s: page %d has no title", what, n + 1);
        }
        const int bottom = ui_help_text_bottom(&p, note && n == 0);
        if (bottom > ui_help_text_limit()) {
            T_FAIL("%s: page %d's text ends at y %d, past %d", what, n + 1,
                   bottom, ui_help_text_limit());
        }
        for (int i = 0; i < p.count; ++i) {
            const ui_help_item_t *it = &p.item[i];
            gfx_rect_t r = it->rect;
            r.y = (int16_t)(r.y + UI_BAND_H);
            if (it->label == NULL || it->label[0] == '\0'
                || ui_tr(it->text)[0] == '\0') {
                T_FAIL("%s: page %d item %d has no label or text", what,
                       n + 1, i + 1);
            }
            const int rows = ui_help_item_rows(&p, i);
            if (rows < 1 || rows > UI_HELP_ROWS) {
                T_FAIL("%s: page %d item %d (%s) takes %d rows", what, n + 1,
                       i + 1, it->label, rows);
            }
            if (!inside(r, live) && !inside(r, band)) {
                T_FAIL("%s: page %d item %d (%s) at x %d y %d w %d h %d is "
                       "not in the live half or the band", what, n + 1,
                       i + 1, it->label, r.x, r.y, r.w, r.h);
            }
            if (near(r, dock, 0)) {
                T_FAIL("%s: page %d item %d under the dock", what, n + 1,
                       i + 1);
            }
            if (near(r, next, 0) || near(r, prev, 0)) {
                T_FAIL("%s: page %d item %d (%s) under PREV or NEXT", what,
                       n + 1, i + 1, it->label);
            }
            const gfx_rect_t b = ui_help_badge_rect(&p, i);
            if (!inside(b, live)) {
                T_FAIL("%s: page %d badge %d not in the live half", what,
                       n + 1, i + 1);
            }
            for (int k = 0; k < i; ++k) {
                if (near(b, ui_help_badge_rect(&p, k), 2)) {
                    T_FAIL("%s: page %d badges %d and %d closer than 2px",
                           what, n + 1, k + 1, i + 1);
                }
            }
        }
        if (!p.armed_ok) {
            continue;
        }
        /* On a page for ARMED, the dock covers no control that stops an
         * output and no rect the screen must keep in view. */
        if (p.must_show.w > 0) {
            gfx_rect_t m = p.must_show;
            m.y = (int16_t)(m.y + UI_BAND_H);
            if (near(m, dock, 0)) {
                T_FAIL("%s: page %d's dock covers its must_show", what,
                       n + 1);
            }
        }
        for (int y = dock.y; y < dock.y + dock.h && scr->safe_at; y += 8) {
            for (int x = dock.x; x < dock.x + dock.w; x += 8) {
                if (scr->safe_at(x, y - UI_BAND_H)) {
                    T_FAIL("%s: page %d's dock covers a safe control at "
                           "x %d y %d", what, n + 1, x, y);
                }
            }
        }
    }
}


/** @brief A pose: the screen in the state its pages are checked in. */
typedef void (*help_pose_fn)(void);

/**
 * @brief check_pages() over each language and theme, SAFE and ARMED, with
 *        the link up and every capability and with it down; @p pose puts
 *        the screen in its state first, each time.
 * @param scr  The screen.
 * @param pose The pose.
 * @param name The state's name for a failure.
 * @return The pages the state offers while SAFE, in the last language.
 */
static int __attribute__((unused))
check_every_state(const ui_screen_t *scr, help_pose_fn pose,
                  const char *name)
{
    int pages = 0;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (int th = 0; th < (int)UI_THEME_COUNT; ++th) {
            for (int armed = 0; armed < 2; ++armed) {
                for (int up = 0; up < 2; ++up) {
                    pose();
                    ui_theme_set((ui_theme_id_t)th);
                    ui_text_set_language((ui_lang_t)lang);
                    ui_bench_status_t st;
                    memset(&st, 0, sizeof(st));
                    st.link_up = up != 0;
                    st.mode = ui_band_mode(up != 0);
                    st.armed = armed != 0;
                    st.armed_roles = armed ? UI_ARMED_MOTOR : 0u;
                    st.faults = up ? 0x7Fu : 0u;
                    st.run_seconds = 10u * 3600u;
                    st.capabilities = up ? 0xFFFFu : 0u;
                    ui_router_set_status(&st);
                    char what[96];
                    snprintf(what, sizeof(what),
                             "%s lang %d theme %d armed %d up %d", name,
                             lang, th, armed, up);
                    check_pages(scr, what);
                    if (!armed) {
                        pages = ui_help_count(scr, false);
                    }
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
    return pages;
}
