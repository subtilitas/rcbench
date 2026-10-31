/**
 * @file
 * @brief The screenshot: one picture of the panel held in PSRAM
 *        (pseudo-static RAM), and the popup that names and saves it.
 *
 * A 1000ms hold on the empty part of the band takes the frame as it was
 * drawn, band included, at the end of that frame's render (ui_screen.h,
 * ui_router_shot_gap()).  The copy is 1,228,800B, one frame's worth, on the
 * render task; nothing else on the bench waits for it.  One picture is
 * held: another hold while it waits replaces it, in the same memory.  A
 * restart loses it.  With no memory for it the band says so and nothing
 * else changes.
 *
 * The popup covers the whole body and opens when the router's rule allows
 * (ui_router_shot_set_idle()): the bench idle, no other level up, and no
 * contact on the glass, so the press that disarmed or acknowledged cannot
 * land on it.  It shows the picture at a quarter size, 256 x 150px, and the
 * file-name keyboard, whose OK key reads SAVE.  When the rule stops holding
 * it closes and the picture waits again, the name as typed; only CANCEL or
 * a save that worked ends the picture.
 *
 * SAVE runs on a task of its own (ui_shot_io_t) and the popup follows it. A
 * failure stays on the popup with the picture kept; a taken name is refused,
 * nothing is overwritten.  A save stopped by the bench leaves no file
 * (shot_file.h).
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 * Render task only, as the router.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_io.h"
#include "gfx.h"
#include "touch_types.h"
#include "ui_layout.h"
#include "ui_textkey.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The picture's pixels: the panel's. */
#define UI_SHOT_PIXELS ((size_t)UI_PANEL_W * (size_t)UI_PANEL_H)
/** @brief The preview's width, a quarter of the panel's. */
#define UI_SHOT_PREVIEW_W (UI_PANEL_W / 4)
/** @brief The preview's height, a quarter of the panel's. */
#define UI_SHOT_PREVIEW_H (UI_PANEL_H / 4)
/** @brief What one picture takes in PSRAM: the picture and its preview,
 *         1,305,600B. */
#define UI_SHOT_BYTES \
    ((UI_SHOT_PIXELS + (size_t)UI_SHOT_PREVIEW_W * UI_SHOT_PREVIEW_H) \
     * sizeof(gfx_color_t))

/** @brief What the card's task is asked for. */
typedef enum {
    UI_SHOT_JOB_NONE = 0,   /**< nothing                                  */
    UI_SHOT_JOB_NEXT,       /**< the next default number (shot_file_next) */
    UI_SHOT_JOB_SAVE,       /**< the file (shot_file_save)                */
} ui_shot_job_t;

/** @brief What the application gives the screenshot. */
typedef struct {
    /**
     * @brief Memory for one picture, in PSRAM.
     * @param n    bytes, UI_SHOT_BYTES
     * @param ctx  this table's context
     * @return the memory, or NULL
     */
    void *(*alloc)(size_t n, void *ctx);
    /**
     * @brief Give alloc()'s memory back.
     * @param p    the memory
     * @param ctx  this table's context
     */
    void (*release)(void *p, void *ctx);
    /**
     * @brief Start a job on the card's task; one at a time.
     * @param job     what is asked
     * @param pixels  the picture, UI_PANEL_W wide, for UI_SHOT_JOB_SAVE
     * @param base    the name without its extension, for UI_SHOT_JOB_SAVE;
     *                held by the caller until answered
     * @param ctx     this table's context
     * @return false when it cannot start
     */
    bool (*start)(ui_shot_job_t job, const gfx_color_t *pixels,
                  const char *base, void *ctx);
    /**
     * @brief The job's answer, once.
     * @param err     takes what it came to
     * @param number  takes the number of a UI_SHOT_JOB_NEXT
     * @param ctx     this table's context
     * @return true when it has ended
     */
    bool (*poll)(fm_err_t *err, unsigned *number, void *ctx);
    /**
     * @brief How far a save has come.
     * @param ctx  this table's context
     * @return 0 to 100
     */
    int (*percent)(void *ctx);
    /**
     * @brief Ask a running save to stop at its next row.
     * @param ctx  this table's context
     */
    void (*cancel)(void *ctx);
    /**
     * @brief The microsecond clock, for the copy's time; may be NULL.
     * @param ctx  this table's context
     * @return the clock
     */
    uint32_t (*now_us)(void *ctx);
    /**
     * @brief A copy's time, for the console; may be NULL.
     * @param us   the copy's microseconds
     * @param ctx  this table's context
     */
    void (*copied)(uint32_t us, void *ctx);
    void *ctx;  /**< passed to each */
} ui_shot_io_t;

/** @brief What ui_shot_service() did. */
typedef enum {
    UI_SHOT_SAME = 0,   /**< nothing                                      */
    UI_SHOT_OPENED,     /**< the popup opened                             */
    UI_SHOT_CLOSED,     /**< the popup closed and the picture waits       */
} ui_shot_change_t;

/**
 * @brief Give the screenshot its memory and its card; NULL takes them away.
 * @param io  the table, kept
 */
void ui_shot_bind(const ui_shot_io_t *io);

/** @brief Drop the picture and close the popup; a table bound stays. */
void ui_shot_reset(void);

/** @brief The next frame's end takes the picture. */
void ui_shot_want(void);

/**
 * @brief Whether a picture is asked for and not taken yet.
 * @return true until the next ui_shot_capture()
 */
bool ui_shot_wanted(void);

/**
 * @brief The frame drawn, into the picture, when one is asked for; the
 *        band's notice says what became of it.
 * @param c          the panel's canvas, UI_PANEL_W x UI_PANEL_H
 * @param opens_now  the popup can open on the next frame
 */
void ui_shot_capture(const gfx_canvas_t *c, bool opens_now);

/**
 * @brief The card's answers, each frame.
 */
void ui_shot_poll(void);

/**
 * @brief The popup against the router's rule, each frame and when the rule
 *        changes.
 * @param may_stay  an open popup stays
 * @param may_open  a waiting picture's popup opens
 * @return what changed
 */
ui_shot_change_t ui_shot_service(bool may_stay, bool may_open);

/**
 * @brief Whether a picture is held, waiting or on the popup.
 * @return true from the capture to CANCEL or a save that worked
 */
bool ui_shot_held(void);

/**
 * @brief Whether the popup is open.
 * @return true while it covers the body
 */
bool ui_shot_open(void);

/**
 * @brief One touch event on the popup, in body coordinates.
 * @param evt  the event; a contact that came down before the popup opened
 *             presses nothing
 */
void ui_shot_event(const touch_event_t *evt);

/** @brief A press on the popup is dropped: events were lost. */
void ui_shot_cancel_press(void);

/**
 * @brief Draw the popup where it changed in this buffer.
 * @param c    the body's canvas
 * @param buf  the framebuffer
 */
void ui_shot_render(gfx_canvas_t *c, int buf);

/** @brief Repaint the popup into both framebuffers. */
void ui_shot_invalidate(void);

/**
 * @brief The picture, for tests.
 * @return its pixels, or NULL with none held
 */
const gfx_color_t *ui_shot_image(void);

/**
 * @brief The preview, for tests.
 * @return its pixels, or NULL with none held
 */
const gfx_color_t *ui_shot_preview(void);

/**
 * @brief The name as typed, for tests and the popup.
 * @return the name without its extension
 */
const char *ui_shot_name(void);

/**
 * @brief The text the popup's status line shows, for tests.
 * @return the text, "" for none
 */
const char *ui_shot_status(void);

/**
 * @brief The popup's keyboard, for tests.
 * @return the keyboard
 */
const ui_textkey_t *ui_shot_keyboard(void);

/**
 * @brief Where a key of the popup's keyboard is, for tests.
 * @param key  the key (ui_textkey.h)
 * @return its rectangle in body coordinates
 */
gfx_rect_t ui_shot_key_rect(int key);

#ifdef __cplusplus
}
#endif
