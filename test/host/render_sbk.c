/**
 * @file render_sbk.c
 * @brief The settings backup's views for tools/render_ui.py (render_sbk.h).
 *
 * The card is a folder in memory (sbk_card_rig.h) holding four files a
 * panel wrote: AUTO1.TXT to AUTO3.TXT on 21, 20 and 18 November 2026 by
 * 0.23.15, 0.23.15 and 0.23.12, and SETTINGS.TXT of 2026-11-17 by 0.23.12,
 * edited by hand: three values a load refuses.  The driver's
 * requests run on it between frames, as the logger task runs them.
 *
 * setup-card-backup: SD CARD scrolled to its end, SAVE SETTINGS just
 * written.  setup-backup: BACKUP from LOAD SETTINGS, AUTO1.TXT chosen.
 * setup-backup-offer: the restore offer at start, on a panel whose store
 * held the defaults only.  setup-backup-loaded: SETTINGS.TXT loaded, its
 * three refused lines listed.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_sbk.h"

#include <stdio.h>
#include <string.h>

#include "sbk_card_rig.h"
#include "sbk_ctl.h"
#include "sbk_screen.h"
#include "settings.h"
#include "settings_file.h"
#include "settings_screen.h"
#include "touch_types.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"

bool render_sbk_view(const char *view)
{
    return strcmp(view, "setup-card-backup") == 0
           || strncmp(view, "setup-backup", 12) == 0;
}

/** @brief The values a memory store holds. */
static float s_vals[SETTING_COUNT];
/** @brief The strings it holds. */
static char s_texts[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];
/** @brief Whether it holds any. */
static bool s_have;

/** @brief settings_store_t.load from memory. */
static bool m_load(float *values, int count)
{
    if (s_have) {
        memcpy(values, s_vals, (size_t)count * sizeof(float));
    }
    return s_have;
}

/** @brief settings_store_t.save to memory. */
static bool m_save(const float *values, int count)
{
    memcpy(s_vals, values, (size_t)count * sizeof(float));
    s_have = true;
    return true;
}

/** @brief settings_store_t.load_text from memory. */
static bool m_load_text(char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    if (s_have) {
        memcpy(texts, s_texts, (size_t)count * SETTINGS_TEXT_MAX);
    }
    return s_have;
}

/** @brief settings_store_t.save_text to memory. */
static bool m_save_text(const char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    memcpy(s_texts, texts, (size_t)count * SETTINGS_TEXT_MAX);
    return true;
}

/** @brief The memory store. */
static const settings_store_t k_mem = { m_load, m_save, m_load_text,
                                        m_save_text };

/** @brief What the panel feeds the driver: a card in, SAFE. */
static sbk_feed_t s_feed = { true, false, false, 1795269933, 0u };

/** @brief Frames: the driver's requests run on the folder, the router
 *         ticked. */
static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        sb_req_t q;
        if (sbk_tick(&s_feed, &q)) {
            sb_done_t d;
            sb_run(&k_io, &q, sbk_buffer(), sbk_buffer_size(), &d);
            sbk_done(&d, s_feed.armed);
        }
        ui_router_tick(0.016f);
    }
}

/** @brief A tap on the middle of body rectangle @p r. */
static void tap_body(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(UI_BAND_H + r.y
                                                  + r.h / 2) } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief The value of @p key's line in @p text replaced by @p value, as
 *         a person edits it; the line's comment stays. */
static void edit(char *text, const char *key, const char *value)
{
    char at[24];
    (void)snprintf(at, sizeof(at), "\n%s = ", key);
    char *p = strstr(text, at);
    if (p == NULL) {
        return;
    }
    p += strlen(at);
    const char *end = strchr(p, ' ');
    char rest[SF_FILE_MAX + 1u];
    (void)snprintf(rest, sizeof(rest), "%s", (end != NULL) ? end : "");
    (void)snprintf(p, SF_FILE_MAX - (size_t)(p - text), "%s%s", value,
                   rest);
}

/** @brief The stored settings as @p name, written by @p version on
 *         @p written; edited by hand when @p by_hand. */
static void put(const char *name, const char *version, const char *written,
                bool by_hand)
{
    static char text[SF_FILE_MAX + 1u];
    (void)sf_format(text, sizeof(text), version, written);
    if (by_hand) {
        edit(text, "theme", "PURPLE");
        edit(text, "pack_mah", "99999");
        edit(text, "motor_kv", "two thousand");
    }
    (void)f_write(NULL, name, text, strlen(text));
}

/** @brief The four files, from settings saved to the memory store in the
 *         language @p lang. */
static void fill_card(int lang)
{
    fake_fresh();
    s_have = false;
    settings_set_store(&k_mem);
    settings_init();
    settings_set(SET_LANGUAGE, (float)lang);
    settings_set(SET_OUT_RAMP, 60.0f);
    settings_set_text(SET_TEXT_DUT_NAME, "WING L");
    (void)settings_save();
    put("SETTINGS.TXT", "0.23.12", "2026-11-17 19:42:10 UTC", true);
    settings_set_text(SET_TEXT_DIAL_NAME, "DIAL-3A10");
    (void)settings_save();
    put("AUTO3.TXT", "0.23.12", "2026-11-18 08:15:02 UTC", false);
    settings_set(SET_DIM_AFTER, 5.0f);
    (void)settings_save();
    put("AUTO2.TXT", "0.23.15", "2026-11-20 21:03:47 UTC", false);
    settings_set(SET_PACK_CELLS, 4.0f);
    (void)settings_save();
    put("AUTO1.TXT", "0.23.15", "2026-11-21 14:05:33 UTC", false);
}

/** @brief SD CARD's list dragged to its end. */
static void card_list_end(void)
{
    settings_screen_open_on(SET_CAT_CARD, 0);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 300, .y = 480 } };
    ui_router_event(&e);
    for (int y = 470; y >= 300; y -= 10) {
        e.type = TOUCH_EVENT_MOVE;
        e.point.y = (int16_t)y;
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

void render_sbk_pose(const char *view)
{
    if (!render_sbk_view(view)) {
        return;
    }
    const int lang = settings_get_int(SET_LANGUAGE);
    fill_card(lang);
    if (strcmp(view, "setup-backup-offer") == 0) {
        /* A store that held nothing: the defaults, the language on top
         * as an edit that is not stored. */
        s_have = false;
        settings_init();
        sbk_init();
        ui_router_goto(SCREEN_OVERVIEW);
        frames(10);
        settings_set(SET_LANGUAGE, (float)lang);
        ui_text_set_language((ui_lang_t)lang);
        sbk_screen_invalidate();
        return;
    }
    sbk_init();
    frames(2);
    if (strcmp(view, "setup-card-backup") == 0) {
        card_list_end();
        sbk_setup_row(0)->tap();
        frames(4);
        settings_screen_invalidate();
        return;
    }
    sbk_screen_open(false);
    frames(4);
    if (strcmp(view, "setup-backup-loaded") == 0) {
        sbk_ask_load(3);
        frames(4);
    } else {
        tap_body(sbk_screen_row(0));
    }
    sbk_screen_invalidate();
}
