/**
 * @file
 * @brief GRAPH on SERVO's result cards (graph_key.h): LIMIT RUN beside its
 *        report's name, TEST beside the file name in the run's box, and
 *        RUDDER AUTO right of MARGIN and TOLERANCE.
 *
 * The three runs write into USER LOG FOLDER: LIMITnnn.CSV, ATESTnnn.CSV
 * and RUDDRnnn.CSV under the number the logger answers
 * (servo_screen_test_files()), on the mount it took then.  A run started
 * with USER LOGS off, a run the card took no file for, and a LIMIT RUN that
 * judged no position (its report alone) have no key.  The key is grey while
 * the run goes on, while its outbox still holds lines or its END, and while
 * the logger holds its OPEN or END.
 *
 * LIMIT RUN's and TEST's key take a press as the page's other buttons do
 * (OP_GRAPH, one press at a time); RUDDER AUTO's as the box's controls do
 * (s.ra.pressed).  Each acts on its own contact's tap release only; a
 * release the router flags TOUCH_FLAG_NO_TAP activates nothing.  The
 * reason a tap opened nothing stands in the card's first line
 * (svs_gk_note()).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "log_name.h"
#include "log_viewer_screen.h"
#include "servo_limrun.h"
#include "servo_rudder_auto.h"
#include "servo_test.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/** @brief The empty rectangle: no key. */
static const gfx_rect_t k_none = { 0, 0, 0, 0 };

/**
 * @brief One run's key from its state: its file under @p prefix and
 *        @p file in USER LOG FOLDER.
 * @param k       the card
 * @param prefix  the run's writer
 * @param ran     a run since power-on
 * @param running it goes on
 * @param drained its outbox has handed every line and its END over
 * @param off     USER LOGS was off at its start
 * @param file    its number; 0 not yet, -1 none
 * @param csv     it writes, or wrote, a CSV
 * @param mount   the mount its number came on
 */
static void put(graph_key_card_t k, log_prefix_t prefix, bool ran,
                bool running, bool drained, bool off, int file, bool csv,
                unsigned mount)
{
    char name[LOG_RUN_NAME_MAX];
    name[0] = '\0';
    if (file > 0) {
        log_name_build(name, sizeof(name), prefix, file, false);
    }
    const graph_key_file_t f = {
        .run   = ran && !off && file >= 0 && (running || csv),
        .open  = running || !drained || file == 0 || s.logger_busy,
        .name  = name,
        .dir   = log_viewer_tab_dir(LOG_TAB_USER),
        .mount = mount,
    };
    graph_key_set(k, &f);
}

void svs_gk_sync(void)
{
    put(GRAPH_KEY_LIMIT, LOG_PREFIX_LIMIT, s.lim.state != SERVO_LIMRUN_IDLE,
        servo_limrun_running(&s.lim), servo_limrun_drained(&s.lim),
        s.lim_off, s.lim_file, s.lim.rows_n > 0u, s.gk_mount[PEEK_LIM]);
    put(GRAPH_KEY_RAUTO, LOG_PREFIX_RUDDR,
        s.ra.run.state != SERVO_RAUTO_IDLE, servo_rauto_running(&s.ra.run),
        servo_rauto_drained(&s.ra.run), s.ra.off, s.ra.file, true,
        s.gk_mount[PEEK_RAUTO]);
    put(GRAPH_KEY_ATEST, LOG_PREFIX_ATEST, s.test.state != SERVO_TEST_IDLE,
        servo_test_running(&s.test), servo_test_drained(&s.test),
        s.test_off, s.test_file, true, s.gk_mount[PEEK_TEST]);
}

bool svs_gk_shown(graph_key_card_t k)
{
    svs_gk_sync();
    return graph_key_state(k) != GRAPH_KEY_HIDDEN;
}

gfx_rect_t svs_gk_rect(graph_key_card_t k)
{
    if ((unsigned)k > (unsigned)GRAPH_KEY_ATEST || !svs_gk_shown(k)) {
        return k_none;
    }
    if (k == GRAPH_KEY_RAUTO) {
        /* Right of TOLERANCE, to 10px inside the card's right edge, on its
         * row: 90 x 48px. */
        if (!rauto_shown()) {
            return k_none;
        }
        const gfx_rect_t t = rauto_rect(RA_C_TOL);
        const int x = t.x + t.w + 10;
        return (gfx_rect_t){ (int16_t)x, t.y,
                             (int16_t)(PAD + LCARD_W - 10 - x), t.h };
    }
    /* LIMIT RUN and TEST: where the card drew it last, on its own page. */
    if (k == GRAPH_KEY_LIMIT ? s.fn != SERVO_FN_LIMRUN : !run_box_shown()) {
        return k_none;
    }
    return s.gk_rect[k];
}

gfx_rect_t servo_screen_graph_rect(void)
{
    const graph_key_card_t k = (s.fn == SERVO_FN_LIMRUN) ? GRAPH_KEY_LIMIT
                               : (s.fn == SERVO_FN_RAUTO) ? GRAPH_KEY_RAUTO
                               : (s.fn == SERVO_FN_TEST)  ? GRAPH_KEY_ATEST
                                                          : GRAPH_KEY_COUNT;
    return svs_gk_rect(k);
}

void svs_gk_draw(gfx_canvas_t *c, graph_key_card_t k, gfx_rect_t r,
                 bool pressed)
{
    if ((unsigned)k > (unsigned)GRAPH_KEY_ATEST) {
        return;
    }
    if (!svs_gk_shown(k)) {
        r = k_none;
    }
    s.gk_rect[k] = r;
    graph_key_draw(c, r, k, pressed, SV_FONT_LABEL);
}

bool svs_gk_tap(graph_key_card_t k)
{
    svs_gk_sync();
    if (graph_key_tap(k)) {
        return true;
    }
    /* The card's line says why. */
    ++s.ctrl_rev;
    ++s.test_rev;
    return false;
}

const char *svs_gk_note(graph_key_card_t k)
{
    svs_gk_sync();
    return graph_key_note(k);
}

uint32_t svs_gk_sig(graph_key_card_t k)
{
    svs_gk_sync();
    return graph_key_sig(k);
}

/**
 * @brief The card whose key a page press can be on: LIMIT RUN's left card,
 *        or TEST's run box.
 * @return the card; GRAPH_KEY_COUNT on any other page
 */
static graph_key_card_t page_card(void)
{
    return (s.fn == SERVO_FN_LIMRUN) ? GRAPH_KEY_LIMIT
           : run_box_shown()         ? GRAPH_KEY_ATEST
                                     : GRAPH_KEY_COUNT;
}

bool svs_gk_down(const touch_event_t *evt)
{
    const graph_key_card_t k = page_card();
    if (evt == NULL || k == GRAPH_KEY_COUNT
        || !gfx_rect_contains(svs_gk_rect(k), evt->point.x, evt->point.y)) {
        return false;
    }
    if (!s.ov_have) {
        ov_take(evt, OP_GRAPH, -1);
    }
    return true;
}

void svs_gk_up(int x, int y)
{
    const graph_key_card_t k = page_card();
    if (k != GRAPH_KEY_COUNT && gfx_rect_contains(svs_gk_rect(k), x, y)) {
        (void)svs_gk_tap(k);
    }
}

bool svs_gk_rauto_down(const touch_event_t *evt)
{
    if (evt == NULL
        || !gfx_rect_contains(svs_gk_rect(GRAPH_KEY_RAUTO), evt->point.x,
                              evt->point.y)) {
        return false;
    }
    /* One press at a time among the box's controls. */
    if (s.ra.pressed == RA_C_NONE) {
        s.ra.pressed  = RA_C_GRAPH;
        s.ra.press_id = evt->point.id;
        ++s.ctrl_rev;
    }
    return true;
}

bool svs_gk_rauto_rest(const touch_event_t *evt)
{
    if (evt == NULL || s.ra.pressed != RA_C_GRAPH
        || evt->point.id != s.ra.press_id) {
        return false;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    s.ra.pressed = RA_C_NONE;
    ++s.ctrl_rev;
    if (touch_event_is_tap_up(evt)
        && gfx_rect_contains(svs_gk_rect(GRAPH_KEY_RAUTO), evt->point.x,
                             evt->point.y)) {
        (void)svs_gk_tap(GRAPH_KEY_RAUTO);
    }
    return true;
}
