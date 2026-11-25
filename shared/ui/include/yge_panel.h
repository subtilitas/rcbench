/**
 * @file
 * @brief Programming for YGE ESCs (electronic speed controllers), MOTOR & ESC
 *        > PROGRAM's row `YGE ESCs`: the render loop's one model (yge_prog.h)
 *        and the pages that drive it.
 *
 * CONNECT names the programming port and the power, and takes a red 2s hold.
 * The ESC is entered over its telemetry wire and its table read; its identity
 * and its fields show in tabs, 8 rows a tab.  A field docs/OpenYGE.md marks
 * CHECK draws in the warning colour, and while the draft changes one the line
 * over the footer names it with its new value.  An edit changes a draft here.
 * WRITE n VALUES in the footer is a red 2s hold: no ENABLE WRITE, no confirm.
 * The first write of a session saves the backup to BENCH/YGE/CONF first;
 * without a card the write goes and the old values are kept in RAM only.
 * LEAVE ends the session, a tap; with edits not written it is a page held
 * 2s.  FILES loads a settings file into the draft.
 *
 * Every page of the session draws under the output strip of MOTOR & ESC >
 * PROGRAM: `PORT 1 GPIO 13`, the supply or `BY HAND`, and SAFE
 * (yge_panel_target_text(), yge_panel_by_hand()).
 *
 * A held pin counts for ARM and the runs that arm the bench through
 * prog_reg_servo_holds().  The row stays dimmed until the IOmcu reports
 * LINK_CAP_YGE.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_PANEL_H
#define RCBENCH_YGE_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "yge_card.h"
#include "yge_prog.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Programming ports the CONNECT page offers at most. */
#define YGE_PANEL_PORTS 4u

/** @brief Where the session is. */
typedef enum {
    YGE_PANEL_CONNECT = 0, /**< nothing held: the values, CONNECT             */
    YGE_PANEL_ENTERING,    /**< OPEN to the first read                        */
    YGE_PANEL_HAND,        /**< BY HAND: connect the ESC's battery            */
    YGE_PANEL_SESSION,     /**< the ESC answered: tabs and rows               */
    YGE_PANEL_RUNNING,     /**< a read, a write or a leave runs               */
    YGE_PANEL_STOPPED,     /**< a stop or a fault stands                      */
    YGE_PANEL_OTHER,       /**< another writer holds the pin                  */
    YGE_PANEL_NOT_BACK,    /**< the port did not take the pin back            */
    YGE_PANEL_PIN_HELD,    /**< OPEN, no stop, no flow: ENTER again or PIN BACK */
} yge_panel_stage_t;

/** @brief The session's hooks, for the programmers' registry. */
extern const prog_page_set_t yge_pages;

/**
 * @brief Sets up the model with the gate its commands are claimed at.
 * @param g The gate.
 */
void yge_panel_gate(yge_prog_gate_t *g);

/**
 * @brief The frame's feed.
 * @param f What the control task published.
 */
void yge_panel_feed(const yge_prog_feed_t *f);

/**
 * @brief The command for the control task, once.
 * @param out Receives it.
 * @return true when there is one.
 */
bool yge_panel_next_cmd(yge_prog_cmd_t *out);

/**
 * @brief Whether the YGE_PROG page holds a pin, any writer's, or is asked to.
 * @return true then.
 */
bool yge_panel_holds_port(void);

/**
 * @brief Whether nothing of the YGE_PROG page is under way (yge_prog_idle()).
 * @return true then; false for a page not known.
 */
bool yge_panel_idle(void);

/**
 * @brief The pin this session drives or would drive, for the output strip.
 * @param out  Takes `PORT 1 GPIO 13`, or `PORT 1` while the port's pin is
 *             not read.
 * @param size Bytes at @p out.
 * @return true for a pin not known: in the warning colour.
 */
bool yge_panel_target_text(char *out, size_t size);

/**
 * @brief Whether the session's power is BY HAND: the strip says so in the
 *        supply's place.
 * @return true then.
 */
bool yge_panel_by_hand(void);

/**
 * @brief Whether a tap asked for a look at the card, once.
 * @return true then.
 */
bool yge_panel_card_look(void);

/**
 * @brief The card request for the log task, once: the session's backup, a
 *        list of the files or a load.
 * @param out Receives it.
 * @return true when there is one.
 */
bool yge_panel_card_take(yge_card_req_t *out);

/**
 * @brief The answer to the request yge_panel_card_take() gave.
 * @param d The answer.
 */
void yge_panel_card_done(const yge_card_done_t *d);

/**
 * @brief Where the session is.
 * @return The stage.
 */
yge_panel_stage_t yge_panel_stage(void);

/* --- for the tests and the renderer -------------------------------------- */

/**
 * @brief The model.
 * @return It.
 */
yge_prog_t *yge_panel_model(void);

/**
 * @brief A row's value, on the tab shown, or of the CONNECT page.
 * @param i The row, from 0.
 * @return The rectangle a tap takes; all 0 for no such row.
 */
gfx_rect_t yge_panel_value_rect(int i);

/**
 * @brief A row's name, on the tab shown.
 * @param i The row, from 0.
 * @return The name; NULL for no such row.
 */
const char *yge_panel_row_name(int i);

/**
 * @brief A row's value, on the tab shown.
 * @param i The row, from 0.
 * @return The text, in a static buffer; NULL for no such row.
 */
const char *yge_panel_row_value(int i);

/**
 * @brief Whether a row on the tab shown is a CHECK field.
 * @param i The row, from 0.
 * @return true then.
 */
bool yge_panel_row_check(int i);

/**
 * @brief Whether a row on the tab shown takes an edit.
 * @param i The row, from 0.
 * @return true then.
 */
bool yge_panel_row_live(int i);

/**
 * @brief The footer's buttons.
 * @param i From the left, 0 to 3.
 * @return The rectangle.
 */
gfx_rect_t yge_panel_action_rect(int i);

/**
 * @brief A tab.
 * @param tab The tab, from 0.
 * @return The rectangle.
 */
gfx_rect_t yge_panel_tab_rect(int tab);

/**
 * @brief BACK.
 * @return The rectangle.
 */
gfx_rect_t yge_panel_back_rect(void);

/**
 * @brief The 2s hold's control shown now: CONNECT, ENTER AGAIN, a page's,
 *        or WRITE n VALUES in the footer.
 * @return The rectangle; all 0 for none.
 */
gfx_rect_t yge_panel_hold_rect(void);

/**
 * @brief Whether a 2s hold on yge_panel_hold_rect() does anything: WRITE n
 *        VALUES is greyed with no write to make.
 * @return true then.
 */
bool yge_panel_hold_live(void);

/**
 * @brief A hold page's BACK, or a list's CANCEL.
 * @return The rectangle.
 */
gfx_rect_t yge_panel_cancel_rect(void);

/**
 * @brief A choice in the list.
 * @param i The entry.
 * @return The rectangle.
 */
gfx_rect_t yge_panel_choice_rect(int i);

/**
 * @brief A choice's label.
 * @param i The entry.
 * @return The label; NULL past the last.
 */
const char *yge_panel_choice_label(int i);

/**
 * @brief The hold page's kind: 0 none.
 * @return An index from 1 while a page shows.
 */
int yge_panel_page(void);

/**
 * @brief A hold page's line.
 * @param i The line.
 * @return The text; NULL past the last.
 */
const char *yge_panel_page_line(int i);

/**
 * @brief The keypad.
 * @return It.
 */
ui_keypad_t *yge_panel_keypad(void);

/**
 * @brief The note over the footer, or the model's message.
 * @return The text; "" for none.
 */
const char *yge_panel_message(void);

/**
 * @brief The line that names the draft's CHECK fields with their new values.
 * @return The text; "" while the draft changes none.
 */
const char *yge_panel_check_line(void);

/**
 * @brief The indices the draft changes.
 * @return Their count.
 */
int yge_panel_changed(void);

/**
 * @brief Changes when a render would show something else.
 * @return The count.
 */
uint32_t yge_panel_revision(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_YGE_PANEL_H */
