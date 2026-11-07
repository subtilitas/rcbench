/**
 * @file
 * @brief The SERVO hub and its function screens as the router sees them: one
 * ui_screen_t each, over one state and one set of hooks (servo_screen.c).
 *
 * The hub is SCREEN_SERVO; each tile opens one function screen,
 * SCREEN_SERVO_LIVE to SCREEN_SERVO_HITEC.  A function screen's enter()
 * shows its function (servo_screen_show_fn()): the task its panels draw on
 * (LIVE for LIVE and SWEEP, TEST for TEST and LIMIT RUN, RUDDER for RUDDER
 * and RUDDER AUTO, PROGRAM for KST and HITEC), RUDDER AUTO's view, and on
 * KST and HITEC their session.  The home tag climbs one level: from a list
 * to its screen, from a function screen to the hub, from the hub to the
 * menu (ui_router_parent()).
 *
 * Every leave of a function screen is servo_screen.c's leave(): SAFE, a
 * sweep and a run ended.  The ports' 5V stays on between SERVO's screens
 * and goes off for any other.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "hitec_panel.h"
#include "kst_panel.h"
#include "ui_screen.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/* ------------------------------------------------------------ the map */

/** @brief The router screen of each function, SERVO_FN_HUB first. */
static const ui_screen_id_t k_ids[SERVO_FN_COUNT] = {
    SCREEN_SERVO,        SCREEN_SERVO_LIVE,   SCREEN_SERVO_SWEEP,
    SCREEN_SERVO_TEST,   SCREEN_SERVO_LIMRUN, SCREEN_SERVO_RUDDER,
    SCREEN_SERVO_RAUTO,  SCREEN_SERVO_KST,    SCREEN_SERVO_HITEC,
};

/** @brief The task each function's panels draw on. */
static const ui_wb_task_t k_task[SERVO_FN_COUNT] = {
    UI_WB_LIVE,   UI_WB_LIVE,   UI_WB_LIVE,   UI_WB_TEST,   UI_WB_TEST,
    UI_WB_RUDDER, UI_WB_RUDDER, UI_WB_PROGRAM, UI_WB_PROGRAM,
};

servo_fn_t servo_fn_of_screen(ui_screen_id_t id)
{
    for (int f = 1; f < SERVO_FN_COUNT; ++f) {
        if (k_ids[f] == id) {
            return (servo_fn_t)f;
        }
    }
    return SERVO_FN_HUB;
}

ui_screen_id_t servo_fn_screen_id(servo_fn_t fn)
{
    return (fn > SERVO_FN_HUB && fn < SERVO_FN_COUNT) ? k_ids[fn]
                                                      : SCREEN_SERVO;
}

bool servo_screen_owns(ui_screen_id_t id)
{
    return id == SCREEN_SERVO || servo_fn_of_screen(id) != SERVO_FN_HUB;
}

servo_fn_t servo_screen_fn(void) { return (servo_fn_t)s.fn; }

/**
 * @brief The programmer's session of function @p fn.
 * @param fn The function.
 * @return KST's or HITEC's page set; NULL for every other function.
 */
static const prog_page_set_t *fn_session(servo_fn_t fn)
{
    if (fn == SERVO_FN_KST) {
        return &kst_panel_pages;
    }
    return (fn == SERVO_FN_HITEC) ? &hitec_panel_pages : NULL;
}

void servo_screen_show_fn(servo_fn_t fn)
{
    if (fn >= SERVO_FN_COUNT) {
        fn = SERVO_FN_HUB;
    }
    /* The screen shows first: no list or panel of the one shown before. */
    svs_list_close();
    close_panels();
    s.fn = (uint8_t)fn;
    (void)ui_wb_strip_select(&s.wb, k_task[fn]);
    drive_layout();
    s.run_list = RL_NONE;
    /* RUDDER AUTO is RUDDER's view of AUTO's box, on a screen of its own;
     * RUDDER shows the drive buttons.  A run keeps its view: it runs only
     * on RUDDER AUTO, which a leave ends. */
    if (!servo_rauto_running(&s.ra.run)) {
        rauto_view(fn == SERVO_FN_RAUTO);
    }
    /* KST and HITEC open their session at once, as PROGRAM's row did; the
     * other session's pages go.  A run would go on behind the session,
     * stepping the rail and the position of the port CONNECT takes. */
    const prog_page_set_t *want = fn_session(fn);
    const prog_page_set_t *shown = prog_shown_set();
    if (shown != NULL && shown != want && !prog_ps_holds(shown)) {
        prog_ps_back(shown);
    }
    if (want != NULL && !prog_ps_is_open(want)) {
        test_end_now(SERVO_TEST_AB_OPERATOR);
        close_panels();
        prog_ps_open(want);
    }
    s.fp_pressed = FP_NONE;
    s.fp_tile = -1;
    ++s.fp_rev;
    ++s.ctrl_rev;
    servo_invalidate();
}

/**
 * @brief Whether the function shown is a programmer whose session has
 *        closed under it -- its own CLOSE -- so the screen goes back to
 *        the hub.
 * @return true when KST or HITEC shows no session, panel or page
 */
bool svs_session_gone(void)
{
    const prog_page_set_t *want = fn_session((servo_fn_t)s.fn);
    return want != NULL && !prog_ps_is_open(want) && !modal_open()
           && rows_page() < 0 && !svs_list_shown();
}

/* ------------------------------------------------------------ the hooks */

/**
 * @brief The home tag: over a list, a panel, a page or a session, one level
 *        up of the screen's own; otherwise the screen goes, a function
 *        screen to the hub, the hub to the menu.
 * @return true when the screen took the tag
 */
static bool fn_back(void)
{
    /* A panel closes first, as its CANCEL does; a run's page climbs to
     * its card. */
    if (wb_back()) {
        return true;
    }
    if (svs_list_back()) {
        ov_let_go();
        servo_invalidate();
        ++s.ctrl_rev;
        return true;
    }
    const prog_page_set_t *ps = prog_shown_set();
    if (s.fn != SERVO_FN_HUB && ps != NULL) {
        /* The session's own back: an editor closes, or a port in
         * programming asks for LEAVE and the session stays; a session
         * that closes takes the screen back to the hub. */
        prog_ps_back(ps);
        ov_let_go();
        servo_invalidate();
        ++s.ctrl_rev;
        return prog_shown_set() != NULL;
    }
    return false;
}

/**
 * @brief ui_screen_t's covered: a panel, a list's cover, a programming
 *        page.
 * @return true while one is up
 */
static bool fn_covered(void)
{
    return svs_covered() || ui_setlist_covered(&s.sl);
}

/** @brief Each screen's enter(): its function shown, then the screen's. */
#define FN_ENTER(name, fn)                                                   \
    static void name(void)                                                   \
    {                                                                        \
        servo_screen_show_fn(fn);                                            \
        svs_enter();                                                         \
    }

FN_ENTER(enter_hub, SERVO_FN_HUB)
FN_ENTER(enter_live, SERVO_FN_LIVE)
FN_ENTER(enter_sweep, SERVO_FN_SWEEP)
FN_ENTER(enter_test, SERVO_FN_TEST)
FN_ENTER(enter_limrun, SERVO_FN_LIMRUN)
FN_ENTER(enter_rudder, SERVO_FN_RUDDER)
FN_ENTER(enter_rauto, SERVO_FN_RAUTO)
FN_ENTER(enter_kst, SERVO_FN_KST)
FN_ENTER(enter_hitec, SERVO_FN_HITEC)

/*
 * The tables, one each and written out, so tools/stack_check.py finds
 * every callback by the table's name.  The titles are the band's home tag:
 * SERVO on the hub, "SERVO > name" on a function screen where the tag ends
 * left of the band's first chip in every state (KST alone), the
 * function's name alone where it would not (test_nav_bubble and
 * test_servo_hub measure them).
 */
/** @brief the hub's screen table. */
static const ui_screen_t k_hub = {
    .covered = fn_covered,
    .title  = "SERVO",
    .reset  = svs_reset,
    .enter  = enter_hub,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief LIVE's screen table. */
static const ui_screen_t k_live = {
    .covered = fn_covered,
    .title  = "LIVE",
    .reset  = svs_reset,
    .enter  = enter_live,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief SWEEP's screen table. */
static const ui_screen_t k_sweep = {
    .covered = fn_covered,
    .title  = "SWEEP",
    .reset  = svs_reset,
    .enter  = enter_sweep,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief TEST's screen table. */
static const ui_screen_t k_test = {
    .covered = fn_covered,
    .title  = "TEST",
    .reset  = svs_reset,
    .enter  = enter_test,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief LIMIT RUN's screen table. */
static const ui_screen_t k_limrun = {
    .covered = fn_covered,
    .title  = "LIMIT RUN",
    .reset  = svs_reset,
    .enter  = enter_limrun,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief RUDDER's screen table. */
static const ui_screen_t k_rudder = {
    .covered = fn_covered,
    .title  = "RUDDER",
    .reset  = svs_reset,
    .enter  = enter_rudder,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief RUDDER AUTO's screen table. */
static const ui_screen_t k_rauto = {
    .covered = fn_covered,
    .title  = "RUDDER AUTO",
    .reset  = svs_reset,
    .enter  = enter_rauto,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief KST's screen table. */
static const ui_screen_t k_kst = {
    .covered = fn_covered,
    .title  = "SERVO > KST",
    .reset  = svs_reset,
    .enter  = enter_kst,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

/** @brief HITEC's screen table. */
static const ui_screen_t k_hitec = {
    .covered = fn_covered,
    .title  = "HITEC",
    .reset  = svs_reset,
    .enter  = enter_hitec,
    .leave  = svs_leave,
    .tick   = svs_tick,
    .event  = svs_event,
    .cancel = svs_cancel,
    .render = svs_render,
    .back   = fn_back,
    .safe_at = svs_safe_at,
    .help_count = servo_help_count,
    .help_page  = servo_help_page,
};

const ui_screen_t *servo_screen(void) { return &k_hub; }

const ui_screen_t *servo_fn_screen(servo_fn_t fn)
{
    switch (fn) {
    case SERVO_FN_LIVE:   return &k_live;
    case SERVO_FN_SWEEP:  return &k_sweep;
    case SERVO_FN_TEST:   return &k_test;
    case SERVO_FN_LIMRUN: return &k_limrun;
    case SERVO_FN_RUDDER: return &k_rudder;
    case SERVO_FN_RAUTO:  return &k_rauto;
    case SERVO_FN_KST:    return &k_kst;
    case SERVO_FN_HITEC:  return &k_hitec;
    default:              return &k_hub;
    }
}
