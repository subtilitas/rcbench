/**
 * @file
 * @brief The Hitec programmer's record on the card: HITEC.TXT in the folder
 *        BENCH/HITEC/CONF, the values before a write (hitec_card.h).
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The render loop fills s_hitec_file and counts s_hitec_file_sent up; the
 * logger task writes it (hitec_record_write()), says in s_hitec_file_ok
 * whether the card took it whole, and counts s_hitec_file_done up.  One
 * text at a time: the render loop fills the buffer only while the two
 * counts are equal.
 *
 * The text goes to HITEC.NEW and takes the name HITEC.TXT once the card
 * holds it whole, so that a write the card or the supply ends leaves one of
 * the two whole: HITEC.TXT as it was, or HITEC.NEW complete.  FAT has no
 * rename over a file, so HITEC.TXT is removed between the two;
 * hitec_record_load() takes a whole HITEC.NEW before HITEC.TXT.
 *
 * The record is read once, at the start, before the tasks run.  A card put
 * in later is not read for a record.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "esp_log.h"

#include "card_tree.h"
#include "hitec_card.h"
#include "hitec_panel.h"
#include "panel.h"
#include "storage.h"

/** @brief The log tag. */
static const char *TAG = "hitec";

/** @brief The record's name, HITEC.TXT. */
#define HITEC_FILE_NAME HITEC_CARD_RECORD ".TXT"
/** @brief The record's name while it is written, HITEC.NEW. */
#define HITEC_FILE_NEW  HITEC_CARD_RECORD ".NEW"

/** @brief The record's text (panel.h). */
char        s_hitec_file[HITEC_CARD_TEXT_MAX];
/** @brief The bytes of s_hitec_file to write (panel.h). */
size_t      s_hitec_file_len;
/** @brief The mount the text is written to (panel.h). */
unsigned    s_hitec_file_mount;
/** @brief Texts handed over (panel.h). */
atomic_uint s_hitec_file_sent;
/** @brief Texts written or given up on (panel.h). */
atomic_uint s_hitec_file_done;
/** @brief Whether the card took the last text whole (panel.h). */
atomic_bool s_hitec_file_ok;

/** @brief The record's path, the logger's and the start's. */
static char s_path[96];
/** @brief The path of its new text. */
static char s_path_new[96];

/**
 * @brief The paths of the record and of its new text.
 */
static void paths(void)
{
    snprintf(s_path, sizeof(s_path), "%s/%s/%s", STORAGE_MOUNT_POINT,
             CARD_HITEC_CONF, HITEC_FILE_NAME);
    snprintf(s_path_new, sizeof(s_path_new), "%s/%s/%s", STORAGE_MOUNT_POINT,
             CARD_HITEC_CONF, HITEC_FILE_NEW);
}

/**
 * @brief Write the text the render loop handed over (panel.h).
 */
void hitec_record_write(void)
{
    const unsigned sent = atomic_load(&s_hitec_file_sent);

    if (sent == atomic_load(&s_hitec_file_done)) {
        return;
    }
    bool ok = false;
    const bool held = storage_hold_mount(s_hitec_file_mount);

    if (held
        && card_make_dirs(CARD_HITEC_CONF, panel_card_make_dir, NULL)
               == CARD_DIR_OK) {
        paths();
        FILE *f = fopen(s_path_new, "w");

        if (f != NULL) {
            ok = fwrite(s_hitec_file, 1, s_hitec_file_len, f)
                 == s_hitec_file_len;
            ok = (fflush(f) == 0) && ok;
            ok = (fsync(fileno(f)) == 0) && ok;
            ok = (fclose(f) == 0) && ok;
        }
        if (ok) {
            (void)unlink(s_path);   /* fails where there is none */
            ok = rename(s_path_new, s_path) == 0;
        }
    }
    if (held) {
        storage_release();
    }
    atomic_store(&s_hitec_file_ok, ok);
    atomic_store(&s_hitec_file_done, sent);
}

/**
 * @brief @p path into s_hitec_file.
 *
 * @param path The file.
 * @param len  Receives its length; 0 for a file that does not read.
 * @return false for no file.
 */
static bool record_read(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return false;
    }
    const size_t n = fread(s_hitec_file, 1, sizeof(s_hitec_file) - 1u, f);
    const bool failed = ferror(f) != 0;

    fclose(f);
    s_hitec_file[n] = '\0';
    *len = failed ? 0u : n;
    return true;
}

/**
 * @brief Read the record from the card at the start (panel.h).
 */
void hitec_record_load(void)
{
    size_t len = 0;

    if (!storage_hold()) {
        return;
    }
    paths();
    if (record_read(s_path_new, &len)) {
        hitec_card_record_t r;

        if (hitec_card_parse(s_hitec_file, len, &r) <= HITEC_CARD_NONE) {
            /* A whole text that did not reach its name: used, and named. */
            (void)hitec_panel_file_load(s_hitec_file, len);
            (void)unlink(s_path);
            if (rename(s_path_new, s_path) != 0) {
                ESP_LOGW(TAG, "%s keeps its name", HITEC_FILE_NEW);
            }
            storage_release();
            return;
        }
        (void)unlink(s_path_new);
    }
    if (record_read(s_path, &len) && !hitec_panel_file_load(s_hitec_file,
                                                             len)) {
        ESP_LOGW(TAG, "%s is not used", HITEC_FILE_NAME);
    }
    storage_release();
}
