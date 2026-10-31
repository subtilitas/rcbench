/**
 * @file
 * @brief The screenshot and its popup (ui_shot.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_shot.h"

#include <stdio.h>
#include <string.h>

#include "fm_name.h"
#include "shot_file.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The preview's left and top edge in the body: 12px. */
#define PREV_M 12
/** @brief The keyboard's left edge: right of the preview and its margins. */
#define KEYS_X (2 * PREV_M + UI_SHOT_PREVIEW_W)
/** @brief The folder's line below the preview. */
#define LINE_Y (PREV_M + UI_SHOT_PREVIEW_H + 12)
/** @brief A line of the left column to the next: 22px. */
#define LINE_PITCH 22
/** @brief The status text's top. */
#define STATUS_Y (LINE_Y + 3 * LINE_PITCH + 8)
/** @brief The progress bar's height. */
#define BAR_H 16
/** @brief The progress bar's top, below six rows of status. */
#define BAR_Y (STATUS_Y + 6 * LINE_PITCH + 8)
/** @brief The left column's face: 10 x 20px. */
#define COL_FONT UI_FONT_BAND

/** @brief Where a picture stands. */
typedef enum {
    ST_NONE = 0,    /**< none held                                       */
    ST_WAITING,     /**< held, the popup shut                            */
    ST_OPEN,        /**< held, on the popup                              */
    ST_DROPPING,    /**< CANCEL during a save: gone when the save stops  */
} state_t;

/** @brief The picture and the popup. */
typedef struct {
    const ui_shot_io_t *io;     /**< the application's side              */
    state_t       state;        /**< where the picture stands            */
    bool          want;         /**< the next frame's end takes it       */
    gfx_color_t  *image;        /**< UI_SHOT_PIXELS, then the preview    */
    bool          previewed;    /**< the preview shows this picture      */
    ui_shot_job_t job;          /**< the job under way                   */
    ui_textkey_t  tk;           /**< the name's keyboard                 */
    char          name[SHOT_BASE_MAX + 1u];  /**< as typed               */
    char          fallback[SHOT_BASE_MAX + 1u]; /**< the default offered */
    char          status[96];   /**< what the left column says           */
    int           percent;      /**< the save's progress as drawn        */
    uint32_t      key[2];       /**< what each buffer holds              */
    uint8_t       drawn;        /**< the buffers whose key holds         */
} shot_t;

/** @brief The one picture, render task only. */
static shot_t s;

void ui_shot_bind(const ui_shot_io_t *io) { s.io = io; }

/**
 * @brief Keep a name, cut to what a screenshot's name takes.
 * @param text  the name
 */
static void name_keep(const char *text)
{
    size_t n = strlen(text);
    if (n > SHOT_BASE_MAX) {
        n = SHOT_BASE_MAX;
    }
    memcpy(s.name, text, n);
    s.name[n] = '\0';
}

/** @brief Give the picture back. */
static void drop_image(void)
{
    if (s.image != NULL && s.io != NULL && s.io->release != NULL) {
        s.io->release(s.image, s.io->ctx);
    }
    s.image = NULL;
    s.previewed = false;
    s.state = ST_NONE;
}

void ui_shot_reset(void)
{
    ui_textkey_close(&s.tk);
    drop_image();
    s.want = false;
    s.job = UI_SHOT_JOB_NONE;
    s.status[0] = '\0';
    s.name[0] = '\0';
    s.drawn = 0u;
}

void ui_shot_want(void) { s.want = true; }
bool ui_shot_wanted(void) { return s.want; }
bool ui_shot_held(void) { return s.state == ST_WAITING || s.state == ST_OPEN; }
bool ui_shot_open(void) { return s.state == ST_OPEN; }
const gfx_color_t *ui_shot_image(void)
{
    return ui_shot_held() ? s.image : NULL;
}
const gfx_color_t *ui_shot_preview(void)
{
    return (ui_shot_held() && s.previewed) ? s.image + UI_SHOT_PIXELS : NULL;
}
const char *ui_shot_status(void) { return s.status; }
void ui_shot_invalidate(void) { s.drawn = 0u; }
const ui_textkey_t *ui_shot_keyboard(void) { return &s.tk; }
gfx_rect_t ui_shot_key_rect(int key)
{
    return ui_textkey_key_rect(&s.tk, key);
}
const char *ui_shot_name(void)
{
    return s.tk.open ? s.tk.text : s.name;
}

void ui_shot_capture(const gfx_canvas_t *c, bool opens_now)
{
    if (!s.want) {
        return;
    }
    s.want = false;
    if (s.io == NULL || c == NULL || c->pixels == NULL
        || c->width != UI_PANEL_W || c->height != UI_PANEL_H) {
        return;
    }
    /* A save that a bench change or CANCEL stopped reads the picture until
     * its answer comes: at most one row later. */
    if (s.job == UI_SHOT_JOB_SAVE || s.state == ST_OPEN
        || s.state == ST_DROPPING) {
        ui_router_set_notice(TR(SHOT_BUSY));
        return;
    }
    const bool replaced = s.state == ST_WAITING;
    if (s.image == NULL) {
        s.image = (gfx_color_t *)s.io->alloc(UI_SHOT_BYTES, s.io->ctx);
        if (s.image == NULL) {
            ui_router_set_alert(TR(SHOT_NO_MEMORY));
            return;
        }
    }
    const uint32_t t0 = (s.io->now_us != NULL) ? s.io->now_us(s.io->ctx) : 0u;
    for (int y = 0; y < UI_PANEL_H; ++y) {
        memcpy(s.image + (size_t)y * UI_PANEL_W,
               c->pixels + (size_t)y * (size_t)c->stride,
               (size_t)UI_PANEL_W * sizeof(gfx_color_t));
    }
    if (s.io->now_us != NULL && s.io->copied != NULL) {
        s.io->copied(s.io->now_us(s.io->ctx) - t0, s.io->ctx);
    }
    s.previewed = false;
    s.state = ST_WAITING;
    ui_router_set_notice(replaced    ? TR(SHOT_REPLACED)
                         : opens_now ? TR(SHOT_TAKEN)
                                     : TR(SHOT_WAITS));
}

/**
 * @brief The preview from the picture: each pixel the mean of 4 x 4.
 */
static void make_preview(void)
{
    gfx_color_t *out = s.image + UI_SHOT_PIXELS;
    for (int y = 0; y < UI_SHOT_PREVIEW_H; ++y) {
        const gfx_color_t *r0 = s.image + (size_t)y * 4u * UI_PANEL_W;
        for (int x = 0; x < UI_SHOT_PREVIEW_W; ++x) {
            unsigned r = 0u;
            unsigned g = 0u;
            unsigned b = 0u;
            for (int j = 0; j < 4; ++j) {
                const gfx_color_t *row = r0 + (size_t)j * UI_PANEL_W
                                         + (size_t)x * 4u;
                for (int i = 0; i < 4; ++i) {
                    const unsigned v = row[i];
                    r += (v >> 11) & 0x1Fu;
                    g += (v >> 5) & 0x3Fu;
                    b += v & 0x1Fu;
                }
            }
            *out++ = (gfx_color_t)((((r + 8u) / 16u) << 11)
                                   | (((g + 8u) / 16u) << 5)
                                   | ((b + 8u) / 16u));
        }
    }
    s.previewed = true;
}

/**
 * @brief The keyboard open over the popup on @p text, its layer kept.
 * @param text   the name
 * @param layer  the layer to show
 */
static void kb_open(const char *text, ui_textkey_layer_t layer)
{
    const gfx_rect_t area = { KEYS_X, 0, (int16_t)(UI_PANEL_W - KEYS_X),
                              UI_BODY_H };
    ui_textkey_open_file(&s.tk, area, TR(SHOT_TITLE), text);
    s.tk.max_len = (int)SHOT_BASE_MAX;
    s.tk.ok_label = TR(SHOT_SAVE);
    s.tk.layer = layer;
}

/**
 * @brief The popup, opened on the picture that waits.
 */
static void popup_open(void)
{
    if (!s.previewed) {
        make_preview();
    }
    if (s.name[0] == '\0') {
        (void)snprintf(s.fallback, sizeof(s.fallback), "%s0001", SHOT_PREFIX);
        name_keep(s.fallback);
    }
    kb_open(s.name, UI_TK_UPPER);
    s.status[0] = '\0';
    s.percent = 0;
    s.drawn = 0u;
    s.state = ST_OPEN;
    /* The number the card's folder gives, unless a name is typed. */
    if (s.job == UI_SHOT_JOB_NONE && strcmp(s.name, s.fallback) == 0
        && s.io->start(UI_SHOT_JOB_NEXT, NULL, NULL, s.io->ctx)) {
        s.job = UI_SHOT_JOB_NEXT;
    }
}

/**
 * @brief The popup shut, the name kept and a save under way told to stop.
 * @param to  what the picture does now
 */
static void popup_close(state_t to)
{
    if (s.tk.open) {
        name_keep(s.tk.text);
    }
    ui_textkey_close(&s.tk);
    if (s.job == UI_SHOT_JOB_SAVE) {
        s.io->cancel(s.io->ctx);
    }
    s.state = to;
    s.drawn = 0u;
}

ui_shot_change_t ui_shot_service(bool may_stay, bool may_open)
{
    if (s.state == ST_OPEN && !may_stay) {
        popup_close(ST_WAITING);
        return UI_SHOT_CLOSED;
    }
    if (s.state == ST_WAITING && may_open && s.job != UI_SHOT_JOB_SAVE
        && s.io != NULL) {
        popup_open();
        return UI_SHOT_OPENED;
    }
    return UI_SHOT_SAME;
}

/**
 * @brief What the popup says about a failed save.
 * @param e  the answer
 * @return the text
 */
static const char *err_text(fm_err_t e)
{
    switch (e) {
    case FM_ERR_NO_CARD:
    case FM_ERR_CARD_GONE: return TR(SHOT_E_NOCARD);
    case FM_ERR_FULL:      return TR(SHOT_E_FULL);
    case FM_ERR_EXISTS:    return TR(SHOT_E_EXISTS);
    case FM_ERR_DENIED:    return TR(SHOT_E_DENIED);
    case FM_ERR_NAME:      return TR(SHOT_E_NAME);
    case FM_ERR_BUSY:      return TR(SHOT_E_BUSY);
    case SHOT_ERR_MEMORY:  return TR(SHOT_E_MEMORY);
    default:               return TR(SHOT_E_WRITE);
    }
}

void ui_shot_poll(void)
{
    if (s.job == UI_SHOT_JOB_NONE || s.io == NULL) {
        return;
    }
    if (s.job == UI_SHOT_JOB_SAVE && s.state == ST_OPEN) {
        s.percent = s.io->percent(s.io->ctx);
    }
    fm_err_t err = FM_OK;
    unsigned number = 1u;
    if (!s.io->poll(&err, &number, s.io->ctx)) {
        return;
    }
    const ui_shot_job_t job = s.job;
    s.job = UI_SHOT_JOB_NONE;
    if (job == UI_SHOT_JOB_NEXT) {
        /* A name typed meanwhile stays; so does one a failure left. */
        if (err == FM_OK && s.state == ST_OPEN
            && strcmp(s.tk.text, s.fallback) == 0) {
            (void)snprintf(s.fallback, sizeof(s.fallback), "%s%04u",
                           SHOT_PREFIX, number);
            (void)snprintf(s.tk.text, sizeof(s.tk.text), "%s", s.fallback);
            s.tk.len = (int)strlen(s.tk.text);
            ++s.tk.revision;
        }
        return;
    }
    if (s.state == ST_DROPPING) {
        drop_image();
        return;
    }
    if (err == FM_OK) {
        /* Also a save that ended whole as the bench stopped being idle: the
         * file is on the card, and that ends the picture. */
        char line[UI_ALERT_MAX];
        (void)snprintf(line, sizeof(line), TR(SHOT_SAVED), SHOT_DIR, s.name,
                       SHOT_EXT);
        ui_textkey_close(&s.tk);
        s.name[0] = '\0';
        drop_image();
        ui_router_invalidate();
        ui_router_set_notice(line);
        return;
    }
    if (s.state != ST_OPEN) {
        return;     /* the bench stopped it: the picture waits */
    }
    (void)snprintf(s.status, sizeof(s.status), "%s", err_text(err));
}

/**
 * @brief SAVE: the name as the keyboard took it, on its way to the card.
 * @param typed  the name; the keyboard has closed on it
 * @param layer  the layer the keyboard showed
 */
static void save_start(const char *typed, ui_textkey_layer_t layer)
{
    kb_open(typed, layer);
    s.percent = 0;
    if (shot_name_check(typed) != (int)FM_NAME_OK) {
        (void)snprintf(s.status, sizeof(s.status), "%s", TR(SHOT_E_NAME));
        return;
    }
    name_keep(typed);
    if (s.job != UI_SHOT_JOB_NONE
        || !s.io->start(UI_SHOT_JOB_SAVE, s.image, s.name, s.io->ctx)) {
        (void)snprintf(s.status, sizeof(s.status), "%s", TR(SHOT_E_BUSY));
        return;
    }
    s.job = UI_SHOT_JOB_SAVE;
    s.status[0] = '\0';
}

void ui_shot_event(const touch_event_t *evt)
{
    if (s.state != ST_OPEN || evt == NULL) {
        return;
    }
    /* While it writes the keyboard takes only its CANCEL. */
    if (s.job == UI_SHOT_JOB_SAVE && evt->type == TOUCH_EVENT_DOWN
        && !gfx_rect_contains(ui_textkey_key_rect(&s.tk, UI_TK_CANCEL),
                              evt->point.x, evt->point.y)) {
        return;
    }
    const ui_textkey_layer_t layer = s.tk.layer;
    char typed[UI_TEXTKEY_PASS_MAX + 1];
    const ui_textkey_result_t r = ui_textkey_event(&s.tk, evt, typed,
                                                   sizeof(typed));
    if (r == UI_TEXTKEY_OK) {
        save_start(typed, layer);
    } else if (r == UI_TEXTKEY_CANCELLED) {
        const bool writing = s.job == UI_SHOT_JOB_SAVE;
        popup_close(ST_DROPPING);
        if (!writing) {
            s.name[0] = '\0';
            drop_image();
        } else {
            s.name[0] = '\0';
        }
        ui_router_invalidate();
    }
}

void ui_shot_cancel_press(void)
{
    if (s.state == ST_OPEN) {
        ui_textkey_cancel_press(&s.tk);
    }
}

/**
 * @brief What the popup's picture depends on.
 * @return the key
 */
static uint32_t popup_key(void)
{
    uint32_t h = s.tk.revision;
    h = h * 31u + (uint32_t)s.job;
    h = h * 31u + (uint32_t)s.percent;
    for (const char *p = s.status; *p != '\0'; ++p) {
        h = h * 31u + (uint8_t)*p;
    }
    return h;
}

void ui_shot_render(gfx_canvas_t *c, int buf)
{
    if (s.state != ST_OPEN || c == NULL) {
        return;
    }
    const unsigned b = (unsigned)buf & 1u;
    const uint32_t key = popup_key();
    if ((s.drawn & (1u << b)) != 0u && s.key[b] == key) {
        return;
    }
    s.key[b] = key;
    s.drawn |= (uint8_t)(1u << b);

    gfx_fill_rect(c, 0, 0, UI_PANEL_W, UI_BODY_H, UI_BG);
    gfx_draw_rect(c, PREV_M - 1, PREV_M - 1, UI_SHOT_PREVIEW_W + 2,
                  UI_SHOT_PREVIEW_H + 2, UI_EDGE);
    gfx_blit(c, PREV_M, PREV_M, s.image + UI_SHOT_PIXELS, UI_SHOT_PREVIEW_W,
             UI_SHOT_PREVIEW_H, UI_SHOT_PREVIEW_W);

    char line[FM_NAME_BUF + 16];
    (void)snprintf(line, sizeof(line), "%s/", SHOT_DIR);
    gfx_text(c, PREV_M, LINE_Y, line, COL_FONT, UI_TEXT_DIM, 1);
    (void)snprintf(line, sizeof(line), "%s%s", ui_shot_name(), SHOT_EXT);
    (void)ui_text_wrap(c, gfx_rect_make(PREV_M, LINE_Y + LINE_PITCH,
                                        UI_SHOT_PREVIEW_W, LINE_PITCH),
                       LINE_PITCH, line, COL_FONT, UI_TEXT);
    if (s.status[0] != '\0') {
        (void)ui_text_wrap(c, gfx_rect_make(PREV_M, STATUS_Y,
                                            UI_SHOT_PREVIEW_W, LINE_PITCH),
                           LINE_PITCH, s.status, COL_FONT, UI_WARN);
    }
    if (s.job == UI_SHOT_JOB_SAVE) {
        (void)snprintf(line, sizeof(line), TR(SHOT_SAVING), s.percent);
        gfx_text(c, PREV_M, STATUS_Y, line, COL_FONT, UI_TEXT, 1);
        gfx_draw_rect(c, PREV_M, BAR_Y, UI_SHOT_PREVIEW_W, BAR_H, UI_EDGE);
        gfx_fill_rect(c, PREV_M + 1, BAR_Y + 1,
                      (UI_SHOT_PREVIEW_W - 2) * s.percent / 100, BAR_H - 2,
                      UI_ACCENT);
    }
    ui_textkey_render(&s.tk, c);
}
