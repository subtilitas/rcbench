/**
 * @file upd_settings_int.h
 * @brief UPDATES' SETTINGS list (upd_settings.c), private to upd_screen.c
 *        and upd_settings.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"

/**
 * @brief Whether the list may open and its rows take a press: no check or
 *        download under way and no finger on the download's hold.
 * @return True when it may.
 */
bool upd_settings_free(void);

/** @brief The list closed, at its top, no press. */
void upd_list_reset(void);
/** @brief The list shown at its top. */
void upd_list_open(void);
/** @brief The list closed, what is unsaved left unsaved. */
void upd_list_close(void);
/** @brief Whether the list shows. @return True while it does. */
bool upd_list_shown(void);
/**
 * @brief The home tag over the list: the keypad, a choice list or the
 *        leave warning first, else the list closes.
 * @return True when the list was shown: the screen went up a level.
 */
bool upd_list_back(void);
/** @brief A touch event to the list. @param evt The event. */
void upd_list_event(const touch_event_t *evt);
/** @brief Touch events were lost: the list's press drops. */
void upd_list_cancel(void);
/** @brief A frame of the list. @param dt_s The frame's time, s. */
void upd_list_tick(float dt_s);
/** @brief The list over the body. @param c The canvas. */
void upd_list_render(gfx_canvas_t *c);
/** @brief Bumped whenever the list's look changes. @return The count. */
uint32_t upd_list_revision(void);
