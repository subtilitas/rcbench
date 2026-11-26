/**
 * @file graph_key.h
 * @brief GRAPH on the result cards: the key that opens LOGS with the last
 *        run's CSV in the graph, shared by LIMIT RUN, RUDDER AUTO,
 *        AUTOMATIC TEST, MOTOR & ESC and SUPPLY.
 *
 * Each card hands over its last run since power-on (graph_key_set()): whether
 * there is one whose CSV is or will be on the card, whether the run goes on
 * or its file is still open, the file's name, folder and mount.  The key
 * then has one of four states (graph_key_state()):
 *
 *   - hidden: no such run since power-on, USER LOGS off at its start, a run
 *     that wrote no CSV, or a card that took no file;
 *   - grey, WRITING: the run goes on or its file is still open;
 *   - grey, ARMED: the bench is ARMED (ui_router_status());
 *   - live.
 *
 * A tap on the live key opens LOGS on the file's tab with the file open
 * (log_viewer_show()), in the graph for a file a preset matches, and the
 * home tag on that graph leaves LOGS for the card again
 * (log_viewer_back_leaves(); the card is LOGS' opener in the router's
 * k_openers).  Leaving the card switches the bench to SAFE, as every way out
 * of a bench screen does; the key is live only while it is SAFE anyway.  A
 * file the card in the slot does not hold now -- the card swapped since,
 * the file renamed or deleted -- is refused.  A tap on a grey key and a
 * refused tap leave the reason as the key's note (graph_key_note()), which
 * the card says in its own line; the note goes once the key's state or file
 * changes.
 *
 * The graph drives no output and shows no target.  The key holds no press:
 * each card takes the press with its own controls' rules and calls
 * graph_key_tap() on a tap's release.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "log_viewer_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The cards that carry the key. */
typedef enum {
    GRAPH_KEY_LIMIT = 0,  /**< LIMIT RUN: LIMITnnn.CSV            */
    GRAPH_KEY_RAUTO,      /**< RUDDER AUTO: RUDDRnnn.CSV          */
    GRAPH_KEY_ATEST,      /**< AUTOMATIC TEST: ATESTnnn.CSV       */
    GRAPH_KEY_MOTOR,      /**< MOTOR & ESC: the MOTOR log closed last */
    GRAPH_KEY_SUPPLY,     /**< SUPPLY: the SUPPL log closed last  */
    GRAPH_KEY_COUNT
} graph_key_card_t;

/** @brief What the key shows. */
typedef enum {
    GRAPH_KEY_HIDDEN = 0, /**< not drawn, takes no tap          */
    GRAPH_KEY_WRITING,    /**< grey: the run's file is open     */
    GRAPH_KEY_ARMED,      /**< grey: the bench is ARMED         */
    GRAPH_KEY_LIVE        /**< a tap opens the graph            */
} graph_key_state_t;

/** @brief The last run of a card as graph_key_set() takes it. */
typedef struct {
    /** @brief A run since power-on whose CSV is on the card or on its way. */
    bool        run;
    bool        open;   /**< the run goes on, or its file is still open */
    const char *name;   /**< the CSV's name; NULL or "" while not known  */
    const char *dir;    /**< its folder, a path from the card's root     */
    unsigned    mount;  /**< the mount it was written on (log_viewer_io_t
                             mounts())                                   */
} graph_key_file_t;

/** @brief The key's width on the SERVO cards, px. */
#define GRAPH_KEY_W  96
/** @brief Its height on every card, px. */
#define GRAPH_KEY_H  40

/** @brief Every card forgets its run, as at power-on. */
void graph_key_reset(void);

/**
 * @brief Card @p k's last run, as it stands now.  The key keeps a name of
 *        LOG_LABELLED_MAX bytes and a folder of CARD_RUN_DIR_MAX - 1, a
 *        servo's `Servos/<id>` with an id of 23 characters; a longer one is
 *        refused, as no run, rather than cut to another file's name.
 * @param k the card; one outside the enum is ignored
 * @param f the run; NULL forgets it
 */
void graph_key_set(graph_key_card_t k, const graph_key_file_t *f);

/**
 * @brief What card @p k's key shows now.
 * @param k the card
 * @return its state; GRAPH_KEY_HIDDEN for a card outside the enum
 */
graph_key_state_t graph_key_state(graph_key_card_t k);

/**
 * @brief A tap on card @p k's key.  Live: LOGS, on the file's tab with the
 *        file open, and the home tag back to the card.  Otherwise the
 *        reason becomes the key's note and nothing opens.
 * @param k the card
 * @return true when LOGS opened
 */
bool graph_key_tap(graph_key_card_t k);

/**
 * @brief Why the last tap on card @p k's key opened nothing, while that
 *        reason still holds.
 * @param k the card
 * @return the reason; NULL for none
 */
const char *graph_key_note(graph_key_card_t k);

/**
 * @brief The key, in @p r: GRAPH, grey unless live.  A hidden key draws
 *        nothing.
 * @param c       the canvas
 * @param r       the key
 * @param k       the card
 * @param pressed a finger is on it
 * @param font    the label's face
 */
void graph_key_draw(gfx_canvas_t *c, gfx_rect_t r, graph_key_card_t k,
                    bool pressed, const gfx_font_t *font);

/**
 * @brief What card @p k's key and its note show, as one number for the
 *        card's redraw.
 * @param k the card
 * @return the number
 */
uint32_t graph_key_sig(graph_key_card_t k);

#ifdef __cplusplus
}
#endif
