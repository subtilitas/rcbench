/**
 * @file
 * @brief The SERVO workbench's internals, which servo_screen_int.h takes in at its
 * end: which task and which panel hold the left column, servo_workbench.c's
 * side (the strip, the TARGET row, TEST's page), and the page area and the
 * run cards' geometry.  It reads the screen's one state `s` and the layout
 * of servo_screen_int.h and is not to be included alone.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/* --- which task and which panel hold the left column --------------------- */

/* Whether a panel covers the left column: the keypad, the keyboard, the
 * list, a warning or the question. */
static inline bool modal_open(void)
{
    return s.kp.open || s.tk.open || s.ch.open || s.warn.open || s.ask.open;
}

static inline ui_wb_task_t wb_task(void) { return ui_wb_strip_task(&s.wb); }

/*
 * PROGRAM's sessions, the registry's SERVO rows (prog_registry.h), as the
 * screen drives them: each distinct page set once.  A session is shown
 * over the left column, the strip with it, one level down from PROGRAM's
 * list; while it holds its port it stays shown.
 */
/* A page set's hooks are called through prog_ps_*() (prog_registry.h). */
/* The session shown, or NULL. */
const prog_page_set_t *prog_shown_set(void);
static inline bool prog_shown(void) { return prog_shown_set() != NULL; }
/* A session holds its port: ARM, the TARGET row and PORT PWM lock. */
bool prog_holds(void);
/* Every session laid out over the left column; the screen entered; left;
 * touch events lost; the bench armed or asked to arm; a frame. */
void prog_init(void);
void prog_entry(void);
void prog_leave(void);
void prog_cancel(void);
void prog_armed(bool armed);
void prog_tick(float dt_s);
/* A session holding its port and not shown shows again, on PROGRAM. */
void prog_follow_hold(void);
/* PROGRAM's rows changed what they show (a capability, a pin, a port
 * taken): the page repaints. */
void prog_page_follow(void);

/* The page of rows shown, PG_*, or -1: SETTINGS' page, or on TEST a run's
 * settings. */
static inline int rows_page(void)
{
    switch (wb_task()) {
    case UI_WB_SETTINGS: return s.tabs.selected;
    case UI_WB_TEST:
        return (s.run_list == RL_AUTO)    ? PG_AUTO
               : (s.run_list == RL_LIMIT) ? PG_LIMRUN : -1;
    default:             return -1;
    }
}

/* Whether a page of rows or a panel holds the left column: what took the
 * settings' presses and owned the knob as the settings overlay did. */
static inline bool ov_shown(void)
{
    return modal_open() || rows_page() >= 0;
}

/* Whether LIVE's dial is in sight and takes presses: LIVE, no panel, no
 * session. */
static inline bool live_shown(void)
{
    return wb_task() == UI_WB_LIVE && !modal_open() && !prog_shown();
}

/* --- servo_settings_panel.c's side, which the workbench calls ----------- */

/* Whether the TARGET row and PORT PWM are locked: ARMED, a sweep, a run,
 * the programmer holding the port (servo_settings_panel.c).  @p id is
 * SET_SERVO_TARGET or SET_SERVO_PORT_PWM. */
bool target_locked_for(setting_id_t id);
/* The pages of rows, their presses (ov_down()) included. */
void draw_rows_page(gfx_canvas_t *c);

/* --- servo_workbench.c's side, which the screen calls -------------------- */

/* The strip, the TARGET row and PROGRAM's list laid out (reset); a tile's
 * entry (LIVE); leaving (the run's settings close); touch events lost. */
void wb_init(void);
void wb_enter(void);
void wb_leave(void);
void wb_cancel(void);
/* The home tag: a panel closes; in a session, the session's back()
 * (an editor closes, the session closes to PROGRAM's list, or a port in
 * programming asks for LEAVE); a run's settings close to TEST.  Whether
 * it did one; false lets the screen go. */
bool wb_back(void);
/* A DOWN on the strip or the TARGET row, and the rest of a press on
 * either: whether it was theirs. */
bool wb_down(const touch_event_t *evt);
bool wb_rest(const touch_event_t *evt);
/* A DOWN on TEST's page, its run cards; on PROGRAM's list. */
void test_page_down(const touch_event_t *evt);
void prog_page_down(const touch_event_t *evt);
/* The search typed on the keyboard opened from PROGRAM's list. */
void prog_search_done(const char *text);
/* The strip; TEST's page; the TARGET row on the right card; SUPPLY's caps,
 * read-only, at (@p x, @p y). */
void draw_strip(gfx_canvas_t *c);
void draw_test_page(gfx_canvas_t *c);
void draw_prog_page(gfx_canvas_t *c);
void draw_target(gfx_canvas_t *c);
void draw_caps_line(gfx_canvas_t *c, int x, int y);
/* The strip's row, and START LIMIT RUN on its card. */
gfx_rect_t strip_area(void);
gfx_rect_t lim_start_rect(void);

/* --- the page area and TEST's run cards --------------------------------- */

/* The page area: the left card under the strip. */
static inline gfx_rect_t page_area(void)
{
    return (gfx_rect_t){ PAD, CARD_Y, LCARD_W, CARD_H };
}

/*
 * TEST's two run cards, AUTOMATIC TEST over LIMIT RUN, across the page
 * area: each its name and a summary line, its START hold with SETTINGS
 * beside it, and the lines under them.  The caps line follows the second
 * card.
 */
#define RUN_CARD_X  (PAD + 10)     /**< The run cards' left edge, px. */
#define RUN_CARD_W  (LCARD_W - 20) /**< Their width: 600 px. */
#define RUN_CARD_H  220            /**< A run card's height, px. */
#define RUN_CARD_GAP 4             /**< Above and between the cards, px. */
/** @brief Run card @p k's top (0 AUTOMATIC TEST, 1 LIMIT RUN). */
#define RUN_CARD_Y(k) (CARD_Y + RUN_CARD_GAP + (k) * (RUN_CARD_H + RUN_CARD_GAP))
#define RUN_SUM_DY  42             /**< A card's summary line, in it. */
#define RUN_BTN_DY  74             /**< Its START's top, in it. */
#define RUN_BTN_W   300            /**< START's width, px. */
#define RUN_BTN_H   56             /**< START's height, px. */
#define RUN_LINE_DY 142            /**< The lines under START, in it. */
#define RUN_LINE_P  22             /**< Their pitch in SV_FONT_LABEL. */
#define RUN_SET_W   160            /**< A card's SETTINGS, px. */
/** @brief The caps line under the two cards. */
#define RUN_CAPS_Y  (RUN_CARD_Y(1) + RUN_CARD_H + RUN_CARD_GAP)
_Static_assert(RUN_CAPS_Y + 20 <= CARD_Y + CARD_H - 1,
               "the caps line ends inside the page area");

/**
 * @brief Run card @p k.
 * @param k 0 AUTOMATIC TEST, 1 LIMIT RUN
 * @return the card
 */
static inline gfx_rect_t run_card_rect(int k)
{
    return (gfx_rect_t){ RUN_CARD_X, (int16_t)RUN_CARD_Y(k), RUN_CARD_W,
                         RUN_CARD_H };
}

/**
 * @brief A card's SETTINGS, right of its START: its run's settings, one
 *        level down.  Clear of CLOSE on the run's settings, which takes
 *        the top right of the page: a second tap there closes nothing.
 * @param k 0 AUTOMATIC TEST, 1 LIMIT RUN
 * @return the button, RUN_SET_W x UI_TAP_MIN px
 */
static inline gfx_rect_t run_set_rect(int k)
{
    return (gfx_rect_t){ (int16_t)(RUN_CARD_X + RUN_CARD_W - 10 - RUN_SET_W),
                         (int16_t)(RUN_CARD_Y(k) + RUN_BTN_DY
                                   + (RUN_BTN_H - UI_TAP_MIN) / 2),
                         RUN_SET_W, UI_TAP_MIN };
}

/**
 * @brief CLOSE on a run's settings, top right in the line of the run's
 *        name: back to TEST, as the home tag goes.
 * @return the button, 140 x UI_TAP_MIN px
 */
static inline gfx_rect_t run_close_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RUN_CARD_X + RUN_CARD_W - 140),
                         (int16_t)(CARD_Y + 4), 140, UI_TAP_MIN };
}

/** @brief START TEST, on the AUTOMATIC TEST card. @return the button */
static inline gfx_rect_t test_start_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RUN_CARD_X + 10),
                         (int16_t)(RUN_CARD_Y(0) + RUN_BTN_DY), RUN_BTN_W,
                         RUN_BTN_H };
}
