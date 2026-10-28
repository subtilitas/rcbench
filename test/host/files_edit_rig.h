/**
 * @file files_edit_rig.h
 * @brief What the editor's cases share over files_rig.h: a file put on
 *        the card and opened in the viewer, the editor's keyboard typed
 *        on, its buttons tapped, and the card's bytes read back.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FILES_EDIT_RIG_H
#define FILES_EDIT_RIG_H

#include "files_rig.h"

#include "text_view.h"
#include "ui_textkey.h"

/** @brief Check @p c, and leave the case when it fails. */
#define NEED(c)                                                               \
    do {                                                                      \
        if (!(c)) {                                                           \
            CHECK(c);                                                         \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief Put @p len bytes of @p text at @p path on the card. */
static inline void put_n(const char *path, const char *text, size_t len)
{
    fk_file(&g_h.card, path, (const uint8_t *)text, len);
}

/**
 * @brief A fresh FILES over the default card with @p len bytes of @p text
 *        as @p name in @p dir, opened in the viewer as OPEN opens it.
 * @param dir   the folder, "" for the root
 * @param name  the file's name
 * @param text  its bytes
 * @param len   their count
 */
static inline void open_file(const char *dir, const char *name,
                             const char *text, size_t len)
{
    fresh_bare();
    card_default();
    char path[FM_PATH_SIZE + FM_NAME_BUF];
    snprintf(path, sizeof(path), "%s%s%s", dir, dir[0] != '\0' ? "/" : "",
             name);
    put_n(path, text, len);
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK_EQ(files_screen_open_text(dir, name, g_h.card.mount, len, false),
             FM_OK);
    settle();
    draw();
}

/** @brief open_file() of the NUL-terminated @p text in USER/LOGS. */
static inline void open_text(const char *name, const char *text)
{
    open_file(CARD_USER_LOGS, name, text, strlen(text));
}

/** @brief A tap on a key of the editor's keyboard. */
static inline void ed_key(int key)
{
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_editor(&k, gfx_rect_make(FS_SPOT_TV_KB_X, FS_SPOT_TV_KB_Y,
                                             FS_SPOT_TV_KB_W,
                                             FS_SPOT_TV_KB_H));
    const gfx_rect_t r = ui_textkey_key_rect(&k, key);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief Type @p text on the editor's keyboard: a space on SPACE, a line
 *        feed on ENTER, the layers switched as needed and left on the
 *        small letters.
 * @param text  the characters
 */
static inline void ed_type(const char *text)
{
    ui_textkey_layer_t layer = UI_TK_LOWER;
    ui_textkey_layer_t want = UI_TK_LOWER;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\n') {
            ed_key(*p == ' ' ? UI_TK_SPACE : UI_TK_ENTER);
            continue;
        }
        const int key = ui_textkey_editor_key(*p, &want);
        NEED(key >= 0);
        for (int k = ui_textkey_editor_step(layer, want, &layer); k >= 0;
             k = ui_textkey_editor_step(layer, want, &layer)) {
            ed_key(k);
        }
        ed_key(key);
    }
    for (int k = ui_textkey_editor_step(layer, UI_TK_LOWER, &layer); k >= 0;
         k = ui_textkey_editor_step(layer, UI_TK_LOWER, &layer)) {
        ed_key(k);
    }
}

/** @brief A tap on the footer button whose middle is at @p cx. */
static inline void foot(int cx)
{
    tap(cx, FS_SPOT_TV_FOOT_CY);
}

/** @brief EDIT in the viewer. */
static inline void edit(void)
{
    foot(FS_SPOT_TV_EDIT_CX);
}

/** @brief SAVE in the editor, the keyboard closed. */
static inline void save(void)
{
    foot(FS_SPOT_TV_SAVE_CX);
}

/** @brief The note in the footer, or in the header over the keyboard. */
static inline const char *note(void)
{
    static char out[256];
    return text_view_note(out, sizeof(out));
}

/** @brief Row @p k of the text on screen, "" for none. */
static inline const char *row_at(int k)
{
    static char out[512];
    if (!text_view_row_text(k, out, sizeof(out))) {
        out[0] = '\0';
    }
    return out;
}

/**
 * @brief Whether the card holds @p len bytes of @p want at @p path.
 * @param path  the file
 * @param want  the bytes
 * @param len   their count
 * @return true when it does, byte for byte
 */
static inline bool card_holds(const char *path, const char *want, size_t len)
{
    const fk_node_t *n = fk_get(&g_h.card, path);
    return n != NULL && !n->dir && n->size == len
           && (len == 0u || memcmp(n->data, want, len) == 0);
}

/** @brief The message of @p id with the file's name for its %s. */
static inline const char *said(ui_text_id_t id, const char *name)
{
    static char out[256];
    snprintf(out, sizeof(out), ui_tr(id), name);
    return out;
}

#endif
