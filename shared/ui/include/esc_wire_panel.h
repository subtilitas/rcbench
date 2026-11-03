/**
 * @file
 * @brief The ESC signal-wire session, MOTOR & ESC > PROGRAM's row for AM32,
 *        Bluejay, BLHeli_S and ESCape32: the render loop's one model (esc_prog.h),
 *        and the pages that drive it.
 *
 * CONNECT names the output and the power, and takes a 2s hold.  The ESC is entered
 * and read; its identity and its settings show in tabs.  An edit changes a draft
 * here.  WRITE n VALUES takes a 2s hold on a page that names each second-look
 * field with its new value; the first write of a session saves the backup first,
 * and without a card asks.  LEAVE takes a 2s hold, and on ESCape32 says that a
 * power cycle ends the session.  An AM32 whose settings read erased offers only
 * RESTORE FROM BACKUP.
 *
 * A held pin counts for ARM and the runs that arm the bench through
 * prog_reg_servo_holds().  The row stays dimmed until the IOmcu reports
 * LINK_CAP_PROGRAM.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_WIRE_PANEL_H
#define RCBENCH_ESC_WIRE_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esc_card.h"
#include "esc_prog.h"
#include "gfx.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_keypad.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What can be programmed at most: the throttle slots and the ports. */
#define ESC_WIRE_TARGETS 8u
/** @brief ENTRY HOLD's default, ms: covers a melody of about 1.1s. */
#define ESC_WIRE_HOLD_DEFAULT_MS 3000u
/** @brief ENTRY HOLD's lowest, ms. */
#define ESC_WIRE_HOLD_MIN_MS 500u
/** @brief ENTRY HOLD's highest, ms. */
#define ESC_WIRE_HOLD_MAX_MS 120000u
/** @brief OFF TIME's default, ms. */
#define ESC_WIRE_OFF_DEFAULT_MS 1000u

/** @brief An output this session can take. */
typedef struct {
    uint16_t target; /**< LINK_EW_W_TARGET's value                            */
    uint8_t  gpio;   /**< its GPIO                                            */
} esc_wire_target_t;

/** @brief Where the session is. */
typedef enum {
    ESC_WIRE_CONNECT = 0, /**< nothing held: the values, CONNECT              */
    ESC_WIRE_ENTERING,    /**< OPEN to the first read                         */
    ESC_WIRE_HAND,        /**< BY HAND: connect the power                     */
    ESC_WIRE_SESSION,     /**< the ESC answered: tabs and rows                */
    ESC_WIRE_RUNNING,     /**< a read, a write or a leave runs                */
    ESC_WIRE_STOPPED,     /**< a stop or a fault stands                       */
    ESC_WIRE_OTHER,       /**< another writer holds a pin                     */
    ESC_WIRE_NOT_BACK,    /**< the output did not take the pin back           */
    ESC_WIRE_PIN_HELD,    /**< OPEN, no stop, no flow: ENTER again or PIN BACK */
} esc_wire_stage_t;

/** @brief The session's hooks, for the programmers' registry. */
extern const prog_page_set_t esc_wire_pages;

/**
 * @brief Sets up the model with the gate its commands are claimed at.
 * @param g The gate.
 */
void esc_wire_panel_gate(esc_prog_gate_t *g);

/**
 * @brief The frame's feed.
 * @param f What the control task published.
 */
void esc_wire_panel_feed(const esc_prog_feed_t *f);

/**
 * @brief The command for the control task, once.
 * @param out Receives it.
 * @return true when there is one.
 */
bool esc_wire_panel_next_cmd(esc_prog_cmd_t *out);

/**
 * @brief Whether the ESC_WIRE page holds a pin, any writer's, or is asked to.
 * @return true then.
 */
bool esc_wire_panel_holds_port(void);

/**
 * @brief Whether nothing of the ESC_WIRE page is under way (esc_prog_idle()).
 * @return true then; false for a page not known.
 */
bool esc_wire_panel_idle(void);

/**
 * @brief What can be programmed, from the binding as read.
 * @param t     The outputs.
 * @param n     Their count, at most ESC_WIRE_TARGETS.
 * @param known Whether the binding was read.
 */
void esc_wire_panel_set_targets(const esc_wire_target_t *t, unsigned n, bool known);

/**
 * @brief What can be programmed, from the two output pages as read and the ports
 *        as last read (port_view.h): every slot with a throttle channel and every
 *        DShot port the IOmcu holds a pin for.  A slot's number is its place on the
 *        page, the IOmcu's LINK_EW_W_TARGET.
 * @param slots The slot page, NULL with @p known false.
 * @param cfg   The channel page.
 * @param known Whether the pages were read.
 */
void esc_wire_panel_read_binding(const uint16_t *slots, const uint16_t *cfg,
                                 bool known);

/**
 * @brief The output this session drives or would drive, for the output strip.
 * @param out  Takes `SLOT 1 GPIO 0`, `PORT 1 GPIO 13`, or the word for none.
 * @param size Bytes at @p out.
 * @return true for no output: in the warning colour.
 */
bool esc_wire_panel_target_text(char *out, size_t size);

/**
 * @brief Whether a tap asked for a look at the card, once.
 * @return true then.
 */
bool esc_wire_panel_card_look(void);

/**
 * @brief The card request for the log task, once.
 * @param out Receives it.
 * @return true when there is one.
 */
bool esc_wire_panel_card_take(esc_card_req_t *out);

/**
 * @brief The answer to the request esc_wire_panel_card_take() gave.
 * @param d The answer.
 */
void esc_wire_panel_card_done(const esc_card_done_t *d);

/**
 * @brief Where the session is.
 * @return The stage.
 */
esc_wire_stage_t esc_wire_panel_stage(void);

/* --- for the tests and the renderer -------------------------------------- */

/**
 * @brief The model.
 * @return It.
 */
esc_prog_t *esc_wire_panel_model(void);

/**
 * @brief A row's value, on the tab shown.
 * @param i The row, from 0.
 * @return The rectangle a tap takes; all 0 for no such row.
 */
gfx_rect_t esc_wire_panel_value_rect(int i);

/**
 * @brief A row's name, on the tab shown.
 * @param i The row, from 0.
 * @return The name; NULL for no such row.
 */
const char *esc_wire_panel_row_name(int i);

/**
 * @brief A row's value, on the tab shown.
 * @param i The row, from 0.
 * @return The text, in a static buffer; NULL for no such row.
 */
const char *esc_wire_panel_row_value(int i);

/**
 * @brief The action row's buttons.
 * @param i From the left, 0 to 3.
 * @return The rectangle.
 */
gfx_rect_t esc_wire_panel_action_rect(int i);

/**
 * @brief A tab.
 * @param tab The tab, from 0.
 * @return The rectangle.
 */
gfx_rect_t esc_wire_panel_tab_rect(int tab);

/**
 * @brief BACK.
 * @return The rectangle.
 */
gfx_rect_t esc_wire_panel_back_rect(void);

/**
 * @brief The 2s hold's control.
 * @return The rectangle; all 0 for none.
 */
gfx_rect_t esc_wire_panel_hold_rect(void);

/**
 * @brief The hold page's CANCEL.
 * @return The rectangle.
 */
gfx_rect_t esc_wire_panel_cancel_rect(void);

/**
 * @brief The WRITE page's NEXT.
 * @return The rectangle; empty with one page.
 */
gfx_rect_t esc_wire_panel_next_rect(void);

/**
 * @brief The WRITE page's PREV.
 * @return The rectangle; empty with one page.
 */
gfx_rect_t esc_wire_panel_prev_rect(void);

/**
 * @brief A choice in the list.
 * @param i The entry.
 * @return The rectangle.
 */
gfx_rect_t esc_wire_panel_choice_rect(int i);

/**
 * @brief A choice's label.
 * @param i The entry.
 * @return The label; NULL past the last.
 */
const char *esc_wire_panel_choice_label(int i);

/**
 * @brief The hold page's kind: 0 none.
 * @return An index from 1 while a page shows.
 */
int esc_wire_panel_page(void);

/**
 * @brief A hold page's line.
 * @param i The line.
 * @return The text; NULL past the last.
 */
const char *esc_wire_panel_page_line(int i);

/**
 * @brief A value of the connect and pin-held pages as they show it, from the values
 *        CONNECT and ENTER AGAIN send.
 * @param row 1 POWER, 2 voltage, 3 limit, 4 ENTRY HOLD, 5 OFF TIME.
 * @return The text.
 */
const char *esc_wire_panel_value_text(int row);

/**
 * @brief Whether a hold page's line draws in the warning colour.
 * @param i The line.
 * @return true for a second-look field.
 */
bool esc_wire_panel_page_line_warns(int i);

/**
 * @brief The identity tab's name line, every character of the name.
 * @return The text.
 */
const char *esc_wire_panel_name_line(void);

/**
 * @brief The identity tab's `music` line on ESCape32, display only.  A byte the
 *        font has not shows as `?`.
 * @return The text.
 */
const char *esc_wire_panel_music_line(void);

/**
 * @brief The keypad.
 * @return It.
 */
ui_keypad_t *esc_wire_panel_keypad(void);

/**
 * @brief The note under the rows, or the model's message.
 * @return The text.
 */
const char *esc_wire_panel_message(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_WIRE_PANEL_H */
