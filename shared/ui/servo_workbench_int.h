/**
 * @file
 * @brief The SERVO workbench's internals, which servo_screen_int.h takes in at its
 * end: which task and which panel hold the left column, servo_workbench.c's
 * side (the tasks, TEST's and LIMIT RUN's page), and the page area and the
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
 * The programmers' sessions, the registry's SERVO rows (prog_registry.h),
 * as the screen drives them: each distinct page set once.  KST's and
 * HITEC's screens show their session over the left column; while it holds
 * its port it stays shown there.
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
/** @brief A session holding its port and not shown shows again, on its
 *         own screen (KST's or HITEC's). */
void prog_follow_hold(void);

/* The page of rows shown, PG_*, or -1: on LIMIT RUN its run's settings,
 * on KST its EXPERT MODE row. */
static inline int rows_page(void)
{
    switch (wb_task()) {
    case UI_WB_TEST:
        return (s.run_list == RL_LIMIT) ? PG_LIMRUN : -1;
    case UI_WB_PROGRAM:
        return (s.run_list == RL_KST) ? PG_KST : -1;
    default:             return -1;
    }
}

/* Whether a page of rows or a panel holds the left column: what took the
 * settings' presses and owned the knob as the settings overlay did. */
static inline bool ov_shown(void)
{
    return modal_open() || rows_page() >= 0;
}

/* Whether LIVE's dial is in sight and takes presses: LIVE or SWEEP, no
 * panel, no session.  The hub shows no dial. */
static inline bool live_shown(void)
{
    return s.fn != SERVO_FN_HUB && wb_task() == UI_WB_LIVE && !modal_open()
           && !prog_shown();
}

/* --- servo_settings_panel.c's side, which the workbench calls ----------- */

/* Whether the TARGET row and PORT PWM are locked: ARMED, a sweep, a run,
 * the programmer holding the port, a finger on a start hold or the ARM +
 * START sequence under way (servo_settings_panel.c).  @p id is
 * SET_SERVO_TARGET or SET_SERVO_PORT_PWM. */
bool target_locked_for(setting_id_t id);
/**
 * @brief target_locked_for() less a start hold's finger and the ARM + START
 *        sequence: what the sequence itself may switch.
 * @param id SET_SERVO_TARGET or SET_SERVO_PORT_PWM; false for any other
 * @return true while the bench locks it
 */
bool target_locked_by_bench(setting_id_t id);
/* The pages of rows, their presses (ov_down()) included. */
void draw_rows_page(gfx_canvas_t *c);

/* --- servo_workbench.c's side, which the screen calls -------------------- */

/* A press on an opener's button, under the TARGET setting's lock; SUPPLY's
 * LIMITS and SENSORS' angle source entry opened, the home tag there coming
 * back to the screen they were opened from. */
bool opener_down(const touch_event_t *evt, gfx_rect_t r, int code);
void wb_open_caps(void);
void wb_open_encoder(void);
/** @brief SETUP > SENSORS on the INA3221's entry, for RUDDER AUTO's METER
 *         row. */
void wb_open_meter(void);

/* The task strip at its start (reset); a screen entered; leaving (a page
 * of rows closes); touch events lost. */
void wb_init(void);
void wb_enter(void);
void wb_leave(void);
void wb_cancel(void);
/* The home tag: a panel closes; a page of rows closes to its screen.
 * Whether it did one; false lets the screen go. */
bool wb_back(void);
/** @brief A panel or a programming page covers the screen.
 *  @return true while one does */
bool svs_covered(void);
/* The rest of a press on RUDDER's card, AUTO's box, the footer or the PORT
 * button: whether it was theirs. */
bool wb_rest(const touch_event_t *evt);
/* A DOWN on TEST's or LIMIT RUN's page: its run card. */
void test_page_down(const touch_event_t *evt);
/* TEST's or LIMIT RUN's page; SUPPLY's caps, read-only. */
void draw_test_page(gfx_canvas_t *c);
/** @brief The run card's SETTINGS, grey while svs_list_bench_why() refuses
 *         its page. @param c the canvas */
void draw_run_set(gfx_canvas_t *c);
void draw_caps_button(gfx_canvas_t *c);
/* START LIMIT RUN on its card. */
gfx_rect_t lim_start_rect(void);

/* --- servo_guide.c: the screen guide's pages (ui_help.h) ---------------- */

struct ui_help_page;

/**
 * @brief The guide's pages for what SERVO shows now: the task's, then the
 *        output card's two; 0 under a panel, a programmer or a run's
 *        settings.
 * @return the count
 */
int servo_help_count(void);

/**
 * @brief Page @p i of those servo_help_count() counts.
 * @param i the page, from 0
 * @param out the page
 * @return false past the last or for NULL
 */
bool servo_help_page(int i, struct ui_help_page *out);

/* --- the page area and TEST's run cards --------------------------------- */

/* The page area: the left card, down to the card's floor over the footer. */
static inline gfx_rect_t page_area(void)
{
    return (gfx_rect_t){ PAD, CARD_Y, LCARD_W, CARD_H };
}

/*
 * The run card, AUTOMATIC TEST on TEST and LIMIT RUN on LIMIT RUN, at the
 * top of the page area: its name and a summary line, on LIMIT RUN its
 * START hold with SETTINGS beside it, and the lines under them.  TEST's
 * card is shorter: its hold is the footer's and its settings the footer's
 * list, so its lines follow the summary (TEST_LINE_DY).  On TEST the caps
 * button follows the card, the run's box and the rail's plot under it.
 */
#define RUN_CARD_X  (PAD + 10)     /**< The run cards' left edge, px. */
#define RUN_CARD_W  (LCARD_W - 20) /**< Their width: 600px. */
#define RUN_CARD_H  188            /**< A run card's height, px. */
#define RUN_CARD_GAP 4             /**< Above and between the cards, px. */
/** @brief A run card's top, AUTOMATIC TEST's and LIMIT RUN's alike: each
 *         on its own screen, at the page area's top. */
#define RUN_CARD_Y  (CARD_Y + RUN_CARD_GAP)
#define RUN_NAME_DY 8              /**< A card's name, from its top. */
#define RUN_SUM_DY  32             /**< A card's summary line, in it. */
#define RUN_BTN_DY  56             /**< Its START's top, in it. */
#define RUN_BTN_W   300            /**< START's width, px. */
#define RUN_BTN_H   UI_TAP_MIN             /**< START's height, px. */
#define RUN_LINE_DY 108            /**< The lines under START, in it. */
#define RUN_LINE_P  22             /**< Their pitch in SV_FONT_LABEL. */
#define RUN_SET_W   160            /**< A card's SETTINGS, px. */
#define TEST_CARD_H 136            /**< TEST's card's height, px. */
#define TEST_LINE_DY 56            /**< TEST's lines, in its card. */
/** @brief The caps button under TEST's card. */
#define RUN_CAPS_Y  (RUN_CARD_Y + TEST_CARD_H + RUN_CARD_GAP)
_Static_assert(RUN_CAPS_Y + UI_TAP_MIN <= CARD_FLOOR,
               "the caps button ends above the footer");
_Static_assert(RUN_LINE_DY + 2 * RUN_LINE_P + 20 <= RUN_CARD_H,
               "a card's three lines under START end inside it");
_Static_assert(TEST_LINE_DY + 2 * RUN_LINE_P + 20 <= TEST_CARD_H,
               "TEST's three lines end inside its card");

/** @brief The run card the function shows: 0 AUTOMATIC TEST on TEST, 1
 *         LIMIT RUN on LIMIT RUN. @return the card */
static inline int run_card_shown(void)
{
    return (s.fn == SERVO_FN_LIMRUN) ? 1 : 0;
}

/**
 * @brief Run card @p k.
 * @param k 0 AUTOMATIC TEST, 1 LIMIT RUN
 * @return the card
 */
static inline gfx_rect_t run_card_rect(int k)
{
    return (gfx_rect_t){ RUN_CARD_X, (int16_t)RUN_CARD_Y, RUN_CARD_W,
                         (int16_t)((k == 0) ? TEST_CARD_H : RUN_CARD_H) };
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
    (void)k;
    return (gfx_rect_t){ (int16_t)(RUN_CARD_X + RUN_CARD_W - 10 - RUN_SET_W),
                         (int16_t)(RUN_CARD_Y + RUN_BTN_DY
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

/** @brief A run's name on its settings, from the page's top. */
#define RUN_PAGE_NAME_DY 4
/** @brief The save line on a run's settings, under the name. */
#define RUN_PAGE_SAVE_DY 28

/**
 * @brief The save line of a page of rows, 20px tall, under the run's
 *        name, left of CLOSE, as the rows and the STALL AT note fill the
 *        page down to the card's floor.  The German "not saved" line is
 *        37 cells, 370px.
 * @return the line
 */
static inline gfx_rect_t save_line_rect(void)
{
    const int x = OV_X + 10;
    return (gfx_rect_t){ (int16_t)x, (int16_t)(CARD_Y + RUN_PAGE_SAVE_DY),
                         (int16_t)(run_close_rect().x - 10 - x), 20 };
}
_Static_assert(CARD_Y + RUN_PAGE_SAVE_DY + 20 <= RL_ROW0 - 4,
               "a run's save line ends over its first row");

/**
 * @brief The caps button under TEST's card, which opens SUPPLY's LIMITS.
 * @return the button, 600 x UI_TAP_MIN px
 */
static inline gfx_rect_t caps_open_rect(void)
{
    return (gfx_rect_t){ RUN_CARD_X, (int16_t)RUN_CAPS_Y, RUN_CARD_W,
                         UI_TAP_MIN };
}

_Static_assert(BOX_Y_TEST >= RUN_CAPS_Y + UI_TAP_MIN + 4,
               "TEST's run box starts under the caps button");
_Static_assert(BOX_Y_LIM >= RUN_CARD_Y + RUN_CARD_H + 4,
               "LIMIT RUN's run box starts under its card");

/** @brief Whether the run's box shows and takes presses: TEST's with the
 *         automatic test's, LIMIT RUN's with the limit run's, no page, panel
 *         or session over it.  @return true when it does */
static inline bool run_box_shown(void)
{
    return wb_task() == UI_WB_TEST && rows_page() < 0 && !modal_open()
           && !prog_shown()
           && ((s.fn == SERVO_FN_TEST) ? s.test_box : s.lim_box);
}

/** @brief TEST's HOLD 2s: ARM + START, the footer's middle slot.
 *  @return the button */
static inline gfx_rect_t test_start_rect(void)
{
    return ui_footer_rect(UI_FOOTER_HOLD);
}

/**
 * @brief The rail's plot on TEST, under the caps button: right of the
 *        run's box while it shows, else across the card.  Its meter's
 *        name on its first 24px.
 * @return the plot's place, its name included
 */
static inline gfx_rect_t test_plot_rect(void)
{
    const int x = s.test_box ? RUN_CARD_X + BOX_W + 8 : RUN_CARD_X;
    return (gfx_rect_t){ (int16_t)x, (int16_t)BOX_Y_TEST,
                         (int16_t)(RUN_CARD_X + RUN_CARD_W - x), BOX_H };
}
