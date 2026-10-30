/**
 * @file
 * @brief The PROG 5V box: the programming ports' own 5V on a board that
 *        switches it (the rbmod carrier's GPIO 21), its state, its live
 *        volts and amps, and its switch.
 *
 * One box shape, drawn by the screens that show the ports' supply: SERVO's
 * output card (under the KST and Hitec programmers as well) and SUPPLY's
 * LIVE task.  It is made of the controls those screens have: two lines of
 * text and a button with OUTPUT ON's 2s hold.
 *
 *   PROG PORTS 5V, GPIO 21: OFF   [ PROG 5V ON  ]
 *   5.02V  0.120A
 *
 * - The first line names the pin it drives and the state the PROG_SUPPLY
 *   page reads: ON, OFF or NOT READ.
 * - The second line is the INA3221 channel that watches the ports' 5V
 *   (the carrier's 0x40 CH3), its last 50ms window; `--V  --A` without
 *   one.  While a programming port holds the supply it reads
 *   `HELD BY PROG PORT n` instead.
 * - The button: PROG 5V ON, a 2s hold (UI_HOLD_S), while the supply reads
 *   OFF and no port holds it; PROG 5V OFF, a tap, while it reads ON and no
 *   port holds it.  It takes no press otherwise and is drawn dimmed.
 *
 * What the box asks goes out as a command (prog5v_box_poll_cmd()); the
 * control task judges an ON as it judges the PD mini's OUTPUT ON.  The box
 * remembers which screen switched the supply on: leaving that screen
 * switches it off (prog5v_box_left()), as leaving SUPPLY does for the PD
 * mini's output, unless a port holds it.
 *
 * The state, the pin, the holding port and the reading are written by the
 * control task's publisher and read by app_main: atomic words.  The boxes
 * and the command are app_main's.
 *
 * Pure C, no ESP-IDF.  Host-tested in test_prog5v_box.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG5V_BOX_H
#define RCBENCH_PROG5V_BOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_text.h"     /* before ui_widgets.h: ui_theme.h names UI_TEXT */
#include "ui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The box's height: one button, UI_TAP_MIN (48px). */
#define PROG5V_BOX_H     48
/** @brief The button's width, px: PROG 5V AUS is 11 cells of 10px. */
#define PROG5V_BOX_BTN_W 136
/** @brief The box's least width, px: the 8 x 16 lines' 27 cells, a gap
 *         and the button. */
#define PROG5V_BOX_MIN_W (27 * 8 + 4 + PROG5V_BOX_BTN_W)

/** @brief No port holds the supply. */
#define PROG5V_HELD_NONE 0xFFu
/** @brief A port holds it, and the panel does not know which. */
#define PROG5V_HELD_SOME 0xFEu

/** @brief The screen a box is on, and the one that switched the supply on. */
typedef enum {
    PROG5V_BY_NONE = 0,
    PROG5V_BY_SERVO,   /**< SERVO's output card, the programmers' too */
    PROG5V_BY_SUPPLY,  /**< SUPPLY's LIVE task                        */
} prog5v_by_t;

/** @brief What a box asked for, once (prog5v_box_poll_cmd()). */
typedef struct {
    bool        on;    /**< the hold completed: OUTPUT 1              */
    bool        off;   /**< a tap, or the screen that switched it on
                            was left: OUTPUT 0                        */
    prog5v_by_t by;    /**< the screen that asked                     */
    uint32_t    cuts;  /**< for an ON: the PROG_SUPPLY cuts published
                            as its hold began
                            (prog5v_box_publish_cuts()); an error or an
                            OFF since refuses it                      */
} prog5v_cmd_t;

/** @brief One box on one screen. */
typedef struct {
    gfx_rect_t  r;         /**< the box                                 */
    prog5v_by_t by;        /**< the screen it is on                     */
    ui_hold_t   hold;      /**< PROG 5V ON's hold                       */
    bool        pressed;   /**< a press is on the button                */
    bool        press_on;  /**< the supply read ON when it began        */
    bool        was_on;    /**< ON as last seen, for the hold's reach   */
    uint32_t    rev;       /**< moves with anything the box shows       */
    uint32_t    seen;      /**< the published state word, last seen     */
    uint32_t    seen_read; /**< the published reading word, last seen   */
    uint32_t    cuts;      /**< the cuts published as the press began   */
} prog5v_box_t;

/* --------------------------------------------- from the control task */

/**
 * @brief Publish the supply as the PROG_SUPPLY page reads.
 * @param state     a prog_supply_state_t: 0 none (no box), 1 not read,
 *                  2 off, 3 on
 * @param pin       the switching GPIO
 * @param held_port the port a programmer session holds it with, 0 to 3;
 *                  PROG5V_HELD_SOME, PROG5V_HELD_NONE
 */
void prog5v_box_publish(uint8_t state, uint8_t pin, uint8_t held_port);

/**
 * @brief Publish the PROG_SUPPLY link's cuts (prog_supply_link_cuts()): an
 *        ON carries them from the moment its hold begins, so an error
 *        during the hold refuses it as well.
 * @param cuts the count
 */
void prog5v_box_publish_cuts(uint32_t cuts);

/**
 * @brief The holding port to publish: the KST session's port, else the
 *        Hitec session's, else some port while either the panel or the
 *        page says one holds the supply.
 * @param kst_port   the KST session's port, 0 to 3; -1 without one
 * @param hitec_port the Hitec session's port, 0 to 3; -1 without one
 * @param some       a port holds it as the panel or the page has it
 * @return 0 to 3, PROG5V_HELD_SOME or PROG5V_HELD_NONE; a port past 3
 *         counts as none named
 */
uint8_t prog5v_box_held_port(int kst_port, int hitec_port, bool some);

/**
 * @brief Publish the last reading of the channel that watches the ports'
 *        5V.
 * @param ok whether there is one
 * @param mv its mean bus voltage, mV
 * @param ma its mean current, mA, signed
 */
void prog5v_box_reading(bool ok, uint16_t mv, int16_t ma);

/* ---------------------------------------------------- for the screens */

/**
 * @brief Whether a box is shown at all: the board switches the ports' own
 *        supply and the page serves it.
 * @return true while the published state is not none
 */
bool prog5v_box_shown(void);

/**
 * @brief Whether the supply reads ON as published.
 * @return true for ON
 */
bool prog5v_box_on(void);

/**
 * @brief Set a box up on a screen, nothing pressed.
 * @param b  the box; nothing for NULL
 * @param r  where it is drawn, PROG5V_BOX_H tall and PROG5V_BOX_MIN_W wide
 *           at least
 * @param by the screen
 */
void prog5v_box_init(prog5v_box_t *b, gfx_rect_t r, prog5v_by_t by);

/**
 * @brief The button's rect: the box's right end.
 * @param b the box
 * @return the rect; empty for NULL
 */
gfx_rect_t prog5v_box_button(const prog5v_box_t *b);

/**
 * @brief Whether the button takes a press now: shown, read ON or OFF, and
 *        no port holding the supply.
 * @return true when it does
 */
bool prog5v_box_live(void);

/**
 * @brief A contact came down on the button (the screen routed it here):
 *        the hold begins while the supply reads OFF.
 * @param b the box
 * @return true when the box took the press; false while the button takes
 *         none, and the press is the screen's to drop
 */
bool prog5v_box_press(prog5v_box_t *b);

/**
 * @brief The press moved: off the button, a hold not yet complete is
 *        abandoned.
 * @param b the box
 * @param x where the contact is, px
 * @param y where the contact is, px
 * @return true when the press ended here
 */
bool prog5v_box_drag(prog5v_box_t *b, int x, int y);

/**
 * @brief The press lifted: a tap on PROG 5V OFF, still on the button, asks
 *        OFF; a hold that completed asks nothing more; a hold let go
 *        before UI_HOLD_S shows HOLD_HINT_PROG5V on the band
 *        (ui_hold_end()).
 * @param b  the box
 * @param up the UP that ended the press, in screen coordinates; NULL for a
 *           press dropped without one, which asks nothing
 */
void prog5v_box_release(prog5v_box_t *b, const touch_event_t *up);

/**
 * @brief Every gesture dropped (a touch loss): a hold not complete asks
 *        nothing, and an ON this box's hold asked that no poll has
 *        collected yet is withdrawn; an OFF asked stays.
 * @param b the box
 */
void prog5v_box_cancel(prog5v_box_t *b);

/**
 * @brief Time passed: a hold of UI_HOLD_S on PROG 5V ON asks ON once; the
 *        published state followed.
 * @param b    the box
 * @param dt_s the time, s
 */
void prog5v_box_tick(prog5v_box_t *b, float dt_s);

/**
 * @brief A line of the box, in the language showing.
 * @param line 0: `PROG PORTS 5V, GPIO 21: OFF`; 1: `5.02V  0.120A`,
 *             `--V  --A` without a reading, `HELD BY PROG PORT 1` while a
 *             port holds the supply
 * @param buf  where it goes
 * @param n    the size of @p buf, bytes
 * @return @p buf; "" while no box is shown, for no buffer
 */
const char *prog5v_box_text(int line, char *buf, size_t n);

/**
 * @brief Draw the box: its ground, its two lines and its button.
 * @param b the box
 * @param c the canvas
 */
void prog5v_box_render(const prog5v_box_t *b, gfx_canvas_t *c);

/**
 * @brief The screen @p by was left: an OFF when its box switched the
 *        supply on, read ON yet or still on its way, and no port holds it.
 * @param by the screen
 */
void prog5v_box_left(prog5v_by_t by);

/**
 * @brief What the boxes asked, once.
 * @param out receives it; NULL drops it
 * @return true when there was something
 */
bool prog5v_box_poll_cmd(prog5v_cmd_t *out);

/**
 * @brief Forget the command and the owner: a fresh start (tests, a
 *        restart of the screens).
 */
void prog5v_box_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG5V_BOX_H */
