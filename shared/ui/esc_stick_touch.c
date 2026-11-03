/**
 * @file esc_stick_touch.c
 * @brief The presses on ESC STICK's pages.
 *
 * The presses on ESC STICK's pages: the search field and its keyboard,
 * the list's rows and pages, a profile's rows, TIMING and its keypad, the
 * warning's hold,
 * a run's ABORT and the manual steps, BACK and the home tag.  The state
 * and the seam are esc_stick_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_stick_panel.h"

#include <stdio.h>
#include <string.h>

#include "esc_profile.h"
#include "settings.h"
#include "ui_keypad.h"
#include "ui_setting_keypad.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD) and s names the state, which no header included after it
 * may meet. */
#include "esc_stick_panel_int.h"

/**
 * @brief Returns whether a press at (@p px, @p py) is on rectangle @p r and
 *        nothing has claimed it yet.
 */
static bool hit_down(gfx_rect_t r, int px, int py)
{
    return pg.pass == PASS_PRESS && !pg.found && gfx_rect_contains(r, px, py);
}

/**
 * @brief Follows the warning's hold: the contact leaving the button ends it; a
 *        release ends it.
 */
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
    (void)ui_hold_end(&t->hold, evt, t->hold_btn);
    t->warn_down = false;
    ++pg.rev;
}

int sp_rows_shown(int total, int scroll)
{
    const int n = total - scroll;
    return (n > ROWS_MAX) ? ROWS_MAX : (n < 0) ? 0 : n;
}

/** @brief The search's keyboard, docked right of the narrowed rows. */
static gfx_rect_t sp_dock(void)
{
    return (gfx_rect_t){ SP_DOCK_X, SP_ROW_Y0, (int16_t)(W - PAD - SP_DOCK_X),
                         (int16_t)(H - PAD - SP_ROW_Y0) };
}

/* A profile row, narrowed to the left of the keyboard while it is open. */
gfx_rect_t sp_row(int i)
{
    gfx_rect_t r = s.st.rows[i];
    if (s.st.tk.open) {
        r.w = (int16_t)(SP_DOCK_X - 8 - r.x);
    }
    return r;
}

/* The keyboard closes; the search stays as typed. */
void sp_find_close(void)
{
    stick_t *t = &s.st;
    if (t->tk.open) {
        ui_textkey_close(&t->tk);
        t->tk_rev = t->tk.revision;
        ++pg.rev;
    }
}

/**
 * @brief One event for the search's keyboard.  Every key that changes the text
 *        filters the list at once; OK keeps the text, CANCEL goes back to the
 *        search the keyboard opened on.
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

/* ---------------------------------------------------- TIMING's keypad */

/**
 * @brief Where TIMING's row @p i shows its value: the control column, over
 *        the row's height.  A tap there opens the row's keypad.
 * @param i The row on the page, 0 to ROWS_MAX - 1.
 * @return The value's rectangle.
 */
static gfx_rect_t sp_value_rect(int i)
{
    return (gfx_rect_t){ CTRL_X, (int16_t)(ROW_Y0 + i * ROW_P), CTRL_W,
                         ROW_H };
}

/** @brief TIMING's keypad: from under the output strip to 4px above the
 *         alert band, 1008 x 464px. */
#define SP_KP_H (H - UI_ALERT_H - 4 - SP_PANEL_Y)

/**
 * @brief The keypad of TIMING's setting @p idx, over the session's pages.
 * @param idx The setting's place in sp_settings_list.
 */
static void sp_kp_open(int idx)
{
    stick_t *t = &s.st;
    ui_setting_keypad_open(&t->kp,
                           (gfx_rect_t){ PAD, SP_PANEL_Y,
                                         (int16_t)(W - 2 * PAD), SP_KP_H },
                           sp_settings_list[idx]);
    t->kp_idx = idx;
    t->kp_rev = t->kp.revision;
    ++pg.rev;
}

void sp_kp_close(void)
{
    stick_t *t = &s.st;
    if (t->kp.open) {
        ui_keypad_close(&t->kp);
        t->kp_rev = t->kp.revision;
        ++pg.rev;
    }
}

/**
 * @brief One event for TIMING's keypad.  OK sets the value through
 *        settings_set(), the call a step ends in, and CLOSE asks for the
 *        save as it does after a step; a value the steps never reach keeps
 *        the keypad open, refused.
 * @param evt The event.
 */
static void sp_kp_event(const touch_event_t *evt)
{
    stick_t *t = &s.st;
    float v = 0.0f;
    if (ui_keypad_event(&t->kp, evt, &v) == UI_KEYPAD_OK) {
        (void)ui_setting_keypad_ok(&t->kp, sp_settings_list[t->kp_idx], v);
    }
    if (t->kp.revision != t->kp_rev) {
        t->kp_rev = t->kp.revision;
        ++pg.rev;
    }
}

const ui_keypad_t *esc_stick_panel_keypad(void)
{
    return &s.st.kp;
}

int esc_stick_panel_keypad_setting(void)
{
    const stick_t *t = &s.st;
    return t->kp.open ? (int)sp_settings_list[t->kp_idx] : -1;
}

bool esc_stick_panel_holds_press(uint8_t id)
{
    const stick_t *t = &s.st;
    return (t->warn_down && t->warn_id == id)
           || (t->tk.open && t->tk.pressed >= 0 && t->tk.press_id == id)
           || (t->kp.open && t->kp.pressed >= 0 && t->kp.press_id == id);
}

void esc_stick_panel_follow(const touch_event_t *evt)
{
    if (s.st.tk.open) {
        sp_find_event(evt);
    }
    if (s.st.kp.open) {
        sp_kp_event(evt);
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
        t->kp.open ? 1u : 0u,
        esc_stick_running(&t->run) ? 1u : 0u,
        (esc_stick_hand(&t->run) != NULL) ? 1u : 0u,
    };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        h = (h ^ parts[i]) * 16777619u;
    }
    return h;
}

/**
 * @brief BACK on the list climbs a level: a maker's models to the makers, with
 *        the makers' scroll and the search kept; the makers close the session,
 *        back to PROGRAM's list.  The keyboard closes, the search as typed.
 */
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

/**
 * @brief BACK on a profile's page: the list it was picked from, its scroll kept.
 */
static void sp_to_list(void)
{
    s.st.shown = false;
    pg.stage = STAGE_LIST;
    sp_build_list(true);
    ++pg.rev;
}

/** @brief TIMING closes; what it changed is asked to be saved. */
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
    if (t->kp.open) {
        sp_kp_close();                  /* as CANCEL: TIMING stays */
        return true;
    }
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

    /* TIMING's keypad covers the pages; its keys follow their own press
     * and release. */
    if (t->kp.open) {
        if (hit_down(t->kp.area, px, py)) {
            sp_kp_event(evt);
        }
        return true;
    }

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
            ui_hold_begin(&t->hold, TX_HOLD_HINT_RUN);
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
        if (t->p->manual_count > 0u && hit(t->hand_res, px, py)) {
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
                settings_adjust(sp_settings_list[idx], by);
            }
            if (hit(sp_value_rect(i), px, py)) {
                t->tpicked = idx;       /* the value: its keypad */
                sp_kp_open(idx);
                return true;
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
