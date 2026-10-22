/**
 * @file esc_stick_panel_rig.h
 * @brief ESC STICK's cases as test_esc_stick_panel.c and test_esc_stick_panel_*.c
 * share them: the places of the list, the timing page and the hold, a press
 * and a hold, the maker and model openers and the modelled bench's steps,
 * on top of stick_rig.h.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_esc_stick_panel.c
 * - test_esc_stick_panel_phases.c
 * - test_esc_stick_panel_list.c
 * - test_esc_stick_panel_steps.c
 * - test_esc_stick_panel_makers.c
 * - test_esc_stick_panel_hold.c
 * - test_esc_stick_panel_touch.c
 * - test_esc_stick_panel_home.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_sim.h"
#include "gfx.h"
#include "kst_panel.h"
#include "esc_stick_panel.h"
#include "motor_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "touch_feed.h"

#include "stick_rig.h"

/* ------------------------------------------------------- ESC STICK */

/* Mirrored from esc_stick_panel.c, as the rig's are from esc_stick_int.h:
 * the list's rows from 70, the crumb row's controls at 30 to 60, and the
 * panels' buttons UI_BTN_H_TALL px tall, 20 px over the bottom margin. */
#define SP_ROW_CY(n) (70 + (n) * 36 + 16)
#define LIST_UP_X    (SP_RIGHT - 76 + 17)
#define LIST_DN_X    (SP_RIGHT - 38 + 17)
#define LIST_CY      45
#define TIMING_X     (SP_RIGHT - 168 + 84)
#define TIMING_Y     45
#define HOLD_X       (UI_MARGIN + 20 + 130)
#define HOLD_Y       (UI_BODY_H - UI_MARGIN - 20 - UI_BTN_H_TALL / 2)
#define CANCEL_X     (UI_PANEL_W - UI_MARGIN - 20 - 90)
#define HOLD_ID      7

/** @brief Hands the screen one touch event of the hold's finger at (@p x, @p y). */
static inline void press(touch_event_type_t type, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = HOLD_ID, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}

/** @brief Holds the warning's button for @p seconds seconds in 0.25 s ticks. */
static inline void hold_for(float seconds)
{
    press(TOUCH_EVENT_DOWN, HOLD_X, HOLD_Y);
    for (float t = 0.0f; t < seconds; t += 0.25f) {
        scr->tick(0.25f);
    }
    press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
}

/** @brief The supply reading off for 300 ms: its own state off, the current at
 * 0, as the warning needs before it asks for a step at the ESC. */
static inline void supply_reads_off(void)
{
    for (uint32_t t = 0; t <= 300u; t += 100u) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = (uint16_t)(t / 100u + 1u);
        st.taken_ms = t;
        st.stamped = true;
        st.mode = SUPPLY_MODE_OFF;
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        esc_stick_panel_supply(&st);
    }
    esc_stick_panel_bench(300u, false, 0u, 0u, false);
    scr->tick(0.02f);
}

/** @brief @p a and @p b alike with case folded, as the list groups makers. */
static inline bool same_name(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        x -= (x >= 'a' && x <= 'z') ? 32 : 0;
        y -= (y >= 'a' && y <= 'z') ? 32 : 0;
        if (x != y) {
            return false;
        }
        if (x == 0) {
            return true;
        }
    }
}

/** @brief Tap row @p at of the level showing, paging to it first. */
static inline void tap_listed(int at)
{
    int top = 0;
    (void)esc_stick_panel_listed(&top);
    for (int g = 0; g < 64 && at < top; ++g) {
        tap(LIST_UP_X, LIST_CY);
        (void)esc_stick_panel_listed(&top);
    }
    for (int g = 0; g < 64 && at >= top + 9; ++g) {
        tap(LIST_DN_X, LIST_CY);
        (void)esc_stick_panel_listed(&top);
    }
    tap(ROW_CX, SP_ROW_CY(at - top));
}

/** @brief On the makers: open the one named @p name. */
static inline void open_maker(const char *name)
{
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        const char *m = esc_stick_panel_maker_at(i);
        if (m != NULL && same_name(m, name)) {
            tap_listed(i);
            return;
        }
    }
    T_FAIL("no maker %s listed", name);
}

/** @brief On a maker's models: open the first of profile @p id, or the one named
 * @p model. */
static inline void open_model(const char *id, const char *model)
{
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        int m = -1;
        const esc_profile_t *p = esc_stick_panel_row(i, &m);
        if (p != NULL && strcmp(p->id, id) == 0
            && (model == NULL || strcmp(p->models[m].name, model) == 0)) {
            tap_listed(i);
            supply_reads_off();          /* a panel's readings, off */
            return;
        }
    }
    T_FAIL("no model of %s listed", id);
}

/** @brief The stick class, the profile's maker, and the profile's first model. */
static inline void open_profile(const char *id)
{
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    const esc_profile_t *p = esc_profiles_find(id);
    if (p == NULL) {
        T_FAIL("no profile %s", id);
        return;
    }
    open_maker(p->brand);
    open_model(id, NULL);
}

/** @brief The stick class, then hobbywing-flyfun-8item. */
static inline void descend_to_hobbywing(void)
{
    open_profile("hobbywing-flyfun-8item");
}

/** @brief Cutoff mode (item 3) to hard cutoff, its second value. */
static inline void pick_cutoff(void)
{
    tap(STEP_UP_X, STEP_CY(2));
    tap(STEP_UP_X, STEP_CY(2));
}

/** @brief Takes every queued motor command and returns how many are of kind @p want. */
static inline int drain_cmds(motor_cmd_kind_t want)
{
    int n = 0;
    motor_cmd_t mc;
    while (esc_stick_panel_poll_cmd(&mc)) {
        n += (mc.kind == want) ? 1 : 0;
    }
    return n;
}

/* The modelled bench a run goes to, as the panel's without an IOmcu:
 * the arm and the supply follow what is asked, the supply is read every
 * 50 ms, and the simulated ESC draws the current. */
typedef struct {
    esc_sim_t sim;
    bool      armed, on;
    float     pct;
    uint32_t  now, next, seq, stops, pressed;
    int       arms, disarms;
    /* What can go wrong with the bench. */
    bool      no_arm, no_power, silent, offline, link_lost, report_off;
    bool      module_on;      /* what the supply itself reads         */
    uint32_t  off_at, off_lag; /* it goes off this much after the OFF */
    uint32_t  moved_while_on;
    float     last_pct;
    uint32_t  every;          /* ms between readings; 0 is 50 */
    int32_t   extra_ma;
    bool      no_watch;       /* nobody taps DONE once the ESC has
                                 confirmed (before_power_off) */
} rig_t;

/** @brief One step of the modelled bench: applies the motor and supply commands the panel posted and moves the supply's flags. */
static inline void rig_step(rig_t *r)
{
    motor_cmd_t mc;
    while (esc_stick_panel_poll_cmd(&mc)) {
        if (mc.kind == MOTOR_CMD_ARM) {
            r->armed = !r->no_arm;
            r->arms++;
        } else if (mc.kind == MOTOR_CMD_DISARM) {
            r->armed = false;
            r->pct = 0.0f;
            r->disarms++;
        } else if (mc.kind == MOTOR_CMD_THROTTLE) {
            r->pct = mc.value;
        }
    }
    supply_cmd_t sc;
    if (supply_screen_poll_cmd(&sc)) {
        if (sc.off && r->on) {
            r->off_at = r->now;
        }
        r->on = sc.off ? false : (sc.on ? !r->no_power : r->on);
        if (r->on) {
            r->module_on = true;
        }
        supply_screen_set_output(r->on);
        supply_screen_set_on_coming(false);
    }
    /* The panel's flag follows its request; the module follows it off_lag
     * later, as the PD mini does. */
    if (!r->on && r->module_on && r->now - r->off_at >= r->off_lag) {
        r->module_on = false;
    }
    const float eff = r->armed ? r->pct : -1.0f;
    if (eff != r->last_pct && r->module_on && !r->on) {
        r->moved_while_on++;
    }
    r->last_pct = eff;
    esc_stick_panel_bench(r->now, r->armed, r->stops, r->pressed,
                            !r->link_lost);
    const int32_t ma = esc_sim_step(&r->sim, r->now, r->module_on,
                                    r->armed ? r->pct : -1.0f);
    if (r->now >= r->next && !r->silent) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = (uint16_t)++r->seq;
        st.taken_ms = r->now;
        st.stamped = true;
        st.i = r->module_on ? (float)(ma + r->extra_ma) / 1000.0f : 0.0f;
        st.output = r->on;                              /* asked */
        st.mode = (r->module_on && !r->report_off) ? SUPPLY_MODE_CV
                                                   : SUPPLY_MODE_OFF;
        st.online = !r->offline;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        esc_stick_panel_supply(&st);
        r->next = r->now + ((r->every != 0u) ? r->every : 50u);
    }
    scr->tick(0.001f);
    /* The operator watching the ESC confirm taps DONE once it counts. */
    const esc_stick_t *run = esc_stick_panel_stick();
    if (!r->no_watch && run->phase == ESC_STICK_HAND_END
        && esc_stick_hand_ready(run)) {
        tap(HOLD_X, HOLD_Y);
    }
    r->now++;
}

/** @brief Resets the rig and starts a simulated ESC that waits for a hand step. */
static inline void rig_start(rig_t *r)
{
    memset(r, 0, sizeof(*r));
    r->last_pct = -1.0f;
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.wait_hand = true;     /* a before_menu menu starts at the action */
    esc_sim_init(&r->sim, esc_stick_panel_stick()->p, &c);
}

/** @brief Steps the rig for @p ms ms or until the run ends, then five steps more to take what the end asked for. */
static inline void rig_run(rig_t *r, uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(esc_stick_panel_stick());
         ++i) {
        rig_step(r);
    }
    for (int i = 0; i < 5; ++i) {
        rig_step(r);            /* what the end asked for, taken */
    }
}

/* ------------------------------------------------------------ the search */

/* Mirrored from esc_stick_panel.c: the field, its X, and the keyboard
 * docked right of SP_DOCK_X below the crumb row. */
#define FIND_X     (SP_RIGHT - 182)
#define FIND_CLR_X (SP_RIGHT - 100)
#define FIND_Y     45
static const gfx_rect_t k_dock __attribute__((unused)) = {
    352, 68, UI_PANEL_W - UI_MARGIN - 352, UI_BODY_H - UI_MARGIN - 68 };

/** @brief One key of the docked search keyboard; "\n" is OK, "~" CANCEL, "<" DEL,
 * "#" CLR. */
static inline void find_key(char ch)
{
    static const char keys[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM*.";
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_search(&k, k_dock, "", "", 16);
    const char *at = strchr(keys, ch);
    const int key = (ch == '\n') ? UI_TK_OK
                  : (ch == '~')  ? UI_TK_CANCEL
                  : (ch == '<')  ? UI_TK_DEL
                  : (ch == '#')  ? UI_TK_CLR
                  : (ch == ' ')  ? UI_TK_SPACE
                                 : (int)(at - keys);
    const gfx_rect_t r = ui_textkey_key_rect(&k, key);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Types each character of @p s on the search keyboard. */
static inline void find_type(const char *s)
{
    for (; *s != '\0'; ++s) {
        find_key(*s);
    }
}

/** @brief Number of rows the list shows. */
static inline int listed(void)
{
    return esc_stick_panel_listed(NULL);
}

/* ------------------------------------------------------ manual steps */

/* MANUAL INTERVENTION REQUIRED, in the item list's header. */
#define HAND_X 264
#define HAND_Y 124

/** @brief The stick class, page two, row five: kontronik-jazz, the fifth of the
 * Kontronik profiles that run. */
static inline void descend_to_jazz(void)
{
    open_profile("kontronik-jazz");
}

/** @brief Renders the screen onto a cleared canvas and checks that it lit more than 20,000 pixels. */
static inline void draws(void)
{
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/** @brief Everything the screen shows and every setting, as one number: two looks
 * that agree say nothing happened in between. */
static inline uint32_t look(void)
{
    uint32_t h = 2166136261u;
    stick_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    for (int i = 0; i < W * H; ++i) {
        h = (h ^ (uint32_t)fb[i]) * 16777619u;
    }
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const float v = settings_get((setting_id_t)i);
        uint32_t bits;
        memcpy(&bits, &v, sizeof(bits));
        h = (h ^ bits) * 16777619u;
    }
    const uint32_t more[] = {
        (uint32_t)esc_stick_panel_stage(),
        (uint32_t)esc_stick_panel_level(),
        esc_stick_panel_runs(),
        esc_stick_panel_typing() ? 1u : 0u,
        esc_stick_panel_hand_shown() ? 1u : 0u,
        settings_save_asked() ? 1u : 0u,
    };
    for (size_t i = 0; i < sizeof(more) / sizeof(more[0]); ++i) {
        h = (h ^ more[i]) * 16777619u;
    }
    return h;
}
/** @brief Opens the session on the makers list with the bench reporting nothing. */
static inline void at_makers(void)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
}
/* Hobbywing's models; s_model_y is the row of the first that runs. */
static int s_model_y __attribute__((unused));
/** @brief Opens the Hobbywing maker's models list and notes the y of a model row in s_model_y. */
static inline void at_models(void)
{
    at_makers();
    open_maker("Hobbywing");
    s_model_y = SP_ROW_CY(0);
    for (int i = 0; i < 9; ++i) {
        int m = -1;
        const esc_profile_t *p = esc_stick_panel_row(i, &m);
        if (p != NULL && esc_stick_panel_row_why(i) == NULL) {
            s_model_y = SP_ROW_CY(i);
            return;
        }
    }
}
/** @brief Opens the makers list and enters the search HOB. */
static inline void at_searched(void)
{
    at_makers();
    tap(FIND_X, FIND_Y);
    find_type("HOB\n");
}
/** @brief Opens the session on Hobbywing's items page. */
static inline void at_items(void)     { fresh(); descend_to_hobbywing(); }
/** @brief As at_items(), with the cutoff item picked. */
static inline void at_picked(void)    { at_items(); pick_cutoff(); }
/** @brief As at_picked(), with WRITE tapped: the warning shows. */
static inline void at_warning(void)   { at_picked(); tap(WRITE_X, BTN_CY); }
/** @brief Opens the items page's timing page and steps BEEP MIN off its default. */
static inline void at_timing(void)
{
    at_items();
    tap(TIMING_X, TIMING_Y);
    tap(STEP_UP_X, STEP_CY(2));              /* BEEP MIN off its default */
}
/** @brief Opens the session on the Jazz steps page. */
static inline void at_steps(void)     { fresh(); descend_to_jazz(); }
/** @brief Holds the warning for 2.25 s, then taps ABORT: the result shows. */
static inline void at_result(void)
{
    at_warning();
    hold_for(2.25f);
    tap(WRITE_X, BTN_CY);                    /* ABORT: the result shows */
    (void)drain_cmds(MOTOR_CMD_ARM);
}

typedef struct {
    const char *name;
    void (*page)(void);
    int x, y;                /* y < 0: s_model_y, set by the page */
} control_t;
