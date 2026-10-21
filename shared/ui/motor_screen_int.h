/*
 * The MOTOR & ESC screen's private seam between motor_screen.c, the screen
 * and LIVE's page (with motor_events.c and motor_draw.c, which share its
 * state through motor_live_int.h), motor_workbench.c, its task strip and
 * the CONTROL card's TARGET row, motor_program_page.c, PROGRAM's list and
 * its sessions, and motor_settings_page.c, SETTINGS' page: the layout they
 * draw on and the functions each calls of the others.  Nothing outside
 * shared/ui includes this header; the screen's interface is
 * motor_screen.h.
 *
 * Each part keeps its own state; psram_bss.lf places each one's .bss.
 * Included last, or last but one before motor_live_int.h: its layout
 * macros have short names (W, H, PAD), which no other header included
 * after it may meet.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_screen.h"
#include "ui_workbench.h"

#define W UI_PANEL_W
#define H UI_BODY_H   /* the router owns the band */

/* Horizontal bands, not columns: seventeen full-height vertical lines cost
 * about 8,160 cache-line fills on this panel, and the same pixels drawn as
 * horizontal lines cost none. */
#define PAD       UI_MARGIN                    /* the screen's outer margin */
#define INNER     6                    /* a panel's own padding */

/*
 * Two columns.  The page on the left, the readouts and the CONTROL card on
 * a rail on the right, so a glance at the numbers and a hand on the
 * throttle do not compete for the same part of the screen.  The rail is
 * the same on every task.
 */
#define COL_GAP   6
#define LEFT_X    PAD
#define LEFT_W    546
#define RIGHT_X   (LEFT_X + LEFT_W + COL_GAP)
#define RIGHT_W   (W - PAD - RIGHT_X)

/*
 * The task strip at the top (ui_workbench.h), LIVE | PROGRAM | SETTINGS.  Under it,
 * on LIVE, the line with PLOT | TABLE, the poll rate, the link's errors and
 * the temperatures; on SETTINGS the temperatures alone, over the rail.
 */
#define HDR_Y     36
#define HDR_H     18

/* Upper row: LIVE's plot on the left, the four readouts on the right. */
#define UP_Y      58
#define LO_Y      272
#define UP_H      (LO_Y - 6 - UP_Y)

/* Lower row: the throttle on the left, the CONTROL card on the right. */
#define LO_H      (H - LO_Y - PAD)

/* A panel's title strip. */
#define TITLE_H   20

/*
 * The CONTROL card, top down: the TARGET row, ARM, the run's totals and
 * RESET PEAKS, 4 px apart.  The TARGET row is the card's width less its
 * padding: TARGET's 64 px, and a value box of 18 cells.
 */
#define BTN_H     UI_BTN_H
#define TARGET_Y  (LO_Y + TITLE_H + 6)
#define TARGET_H  22
#define ARM_Y     (TARGET_Y + TARGET_H + 4)
#define TOTALS_Y  (ARM_Y + BTN_H + 4)
#define RESET_Y   (LO_H + LO_Y - PAD - BTN_H)

/*
 * SETTINGS' and PROGRAM's page: the left column under the strip, down to
 * the screen's margin.  SETTINGS' keypad covers the strip as well, so no
 * tab switches the task under an open keypad.
 */
#define PAGE_X    LEFT_X
#define PAGE_Y    UI_WB_PAGE_Y
#define PAGE_W    LEFT_W
#define PAGE_H    (H - PAGE_Y - PAD)

/* --- motor_workbench.c: the strip and the TARGET row ---------------------- */

/* What a touch event was to the strip or the TARGET row. */
typedef enum {
    MWB_NONE = 0,     /* neither's                                   */
    MWB_TAKEN,        /* theirs; nothing to do                       */
    MWB_TASK,         /* the strip switched the task                 */
    MWB_TARGET,       /* a tap on the TARGET row: open the choice    */
} mwb_ev_t;

/* Laid out, LIVE shown (reset); a tile's entry: LIVE, unless the screen
 * comes back from the OUTPUTS screen its TARGET row opened; touch events
 * lost. */
void mwb_reset(void);
void mwb_enter(void);
void mwb_cancel(void);
/* The TARGET row opened the OUTPUTS screen: the navigation count to come
 * back on. */
void mwb_opened_outputs(void);
ui_wb_task_t mwb_task(void);
/* A touch event.  @p strip: whether the strip takes presses (no keypad
 * over it); @p locked: whether the TARGET row does. */
mwb_ev_t mwb_event(const touch_event_t *evt, bool strip, bool locked);
/* Whether the strip or the TARGET row owns a press under way. */
bool mwb_pressed(void);
/* Bumped when the strip's look changes. */
uint32_t mwb_revision(void);
void mwb_render_strip(gfx_canvas_t *c);
void mwb_render_target(gfx_canvas_t *c, const char *value, bool warn,
                       bool locked);
/* The strip's row with its trough, and the TARGET row. */
gfx_rect_t mwb_strip_area(void);
gfx_rect_t mwb_target_rect(void);

/* --- motor_program_page.c: PROGRAM's list and its sessions --------------- */

/* Laid out, each MOTOR session in the body under the output strip (reset);
 * the screen entered; left (each session closes, a run ends); touch events
 * lost; the bench armed or asked to arm; a frame. */
void mpp_reset(void);
void mpp_enter(void);
void mpp_leave(void);
void mpp_cancel(void);
void mpp_armed(bool armed);
void mpp_tick(float dt_s);
/* The session shown on PROGRAM, or NULL: the list shows. */
const prog_page_set_t *mpp_shown(void);
/* The home tag in a session: its back() (one level up; from its top the
 * session closes to the list).  Whether there was a session. */
bool mpp_back(void);
/* A touch event on the list; whether it was the list's.  A tap on a live
 * row opens its session. */
bool mpp_event(const touch_event_t *evt);
/* Whether a press on the list is under way; drop it. */
bool mpp_pressed(void);
void mpp_cancel_press(void);
/* Bumped when the list's look changes. */
uint32_t mpp_revision(void);
void mpp_render_page(gfx_canvas_t *c);
/* The output strip above a full-width session (motor_output_strip()), and
 * a number that changes whenever its text does. */
void mpp_render_strip(gfx_canvas_t *c, uint8_t proto, uint64_t pins,
                      bool known, bool armed);
uint32_t mpp_strip_signature(uint8_t proto, uint64_t pins, bool known,
                             bool armed);

/* --- motor_settings_page.c: SETTINGS' page -------------------------------- */

/* Laid out (reset); the screen left; touch events lost. */
void msp_reset(void);
void msp_leave(void);
void msp_cancel(void);
/* Whether the keypad is open over the page and the strip. */
bool msp_modal(void);
/* The home tag: the keypad closes, as its CANCEL does.  Whether it did. */
bool msp_back(void);
/* A touch event on the page.  @p safe: the bench is SAFE, nothing asked of
 * it (ui_value_gate.h's OFF), and ARM is not held; the pulse rows take a
 * press, and their keypad's OK, only then. */
void msp_event(const touch_event_t *evt, bool safe);
/* Whether a press of the page's is under way. */
bool msp_pressed(void);
/* A frame: a keypad for a pulse row closes once @p safe is false, and the
 * save line follows the store. */
void msp_tick(bool safe);
/* Bumped when the page's look changes. */
uint32_t msp_revision(void);
void msp_render(gfx_canvas_t *c, bool safe);
