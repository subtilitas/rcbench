/**
 * @file panel_servo.c
 * @brief The labelled servos on the panel; see panel_servo.h.
 *
 * Two hand-overs, both as main's other card jobs make them: NEW ID is a
 * request counted up by the frame task and an answer counted up by the
 * logger task, with the prefix and the answer under s_mux; the walk's view
 * is published under s_mux by the logger task and read by the frame task.
 * A console line is a slot under s_mux the frame task empties.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_servo.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "card_tree.h"
#include "log_take.h"
#include "panel.h"
#include "servo_active.h"
#include "servo_screen.h"
#include "servo_trend.h"
#include "storage.h"

static const char *TAG = "rcbench";  /**< ESP-IDF log tag. */

/** @brief Guards s_view, s_prefix, s_new_id, s_new_why and s_line. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/** @brief The last walk's view; the logger task writes it. */
static panel_servo_view_t s_view;
/** @brief The mount and log_series_edits() the view was read at; the
 *         logger task's own. */
static unsigned s_view_mount;
static unsigned s_view_edits;   /**< see s_view_mount */

/** @brief NEW ID's prefix, copied by the frame task. */
static char s_prefix[SERVO_ID_PREFIX_MAX + 1u];
/** @brief NEW ID's answer: the id made, "" for none. */
static char s_new_id[SERVO_ID_SIZE];
/** @brief NEW ID's answer: how it ended. */
static panel_servo_new_t s_new_why;
/** @brief NEW IDs asked, by the frame task. */
static atomic_uint s_new_sent;
/** @brief NEW IDs answered, by the logger task. */
static atomic_uint s_new_done;
/** @brief The frame task's: the answer count it took last. */
static unsigned s_new_taken;

/** @brief A console line waiting for the frame task; "" for none. */
static char s_line[48];

/** @brief The frame task's: the active servo's serial copied last. */
static uint32_t s_active_copied = UINT32_MAX;
/** @brief The frame task's: the filed count handed to the analyzer last. */
static unsigned s_filed_taken;

/* ------------------------------------------------------- the logger task */

/** @brief storage_walk()'s callback: one entry of `Servos`. */
static void walk_entry(const storage_entry_t *entry, void *ctx)
{
    servo_ids_visit((servo_ids_t *)ctx, entry->name, entry->is_dir);
}

/**
 * @brief Walk `Servos` on the card held now and publish what it holds.
 * @return false when it does not read to its end.
 */
static bool walk_ids(void)
{
    const int64_t t0 = esp_timer_get_time();
    servo_ids_t ids;
    servo_ids_begin(&ids);
    const int n = storage_walk(CARD_SERVOS, NULL, walk_entry, &ids, NULL);
    const bool ok = n >= 0 || n == STORAGE_WALK_NO_DIR;
    const unsigned ms = (unsigned)((esp_timer_get_time() - t0) / 1000);
    taskENTER_CRITICAL(&s_mux);
    s_view.known = ok;
    if (ok) {
        s_view.ids = ids;
    }
    s_view.walk_ms = ms;
    taskEXIT_CRITICAL(&s_mux);
    if (ok) {
        ESP_LOGI(TAG, "servo ids: %u folders, walk %ums", (unsigned)ids.count,
                 ms);
    } else {
        ESP_LOGW(TAG, "servo ids: %s does not read (%d)", CARD_SERVOS, n);
    }
    return ok;
}

/**
 * @brief NEW ID on the card held now: the walk, the next id, its folder.
 * @param prefix ID PREFIX.
 * @param id     Takes the id; "" when none was made.
 * @return how it ended.
 */
static panel_servo_new_t new_id(const char *prefix, char *id)
{
    id[0] = '\0';
    if (servo_id_prefix_check(prefix, NULL, 0u) != SERVO_ID_OK) {
        return PANEL_SERVO_NEW_PREFIX;
    }
    if (!walk_ids()) {
        return PANEL_SERVO_NEW_UNREADABLE;
    }
    servo_ids_t ids;
    taskENTER_CRITICAL(&s_mux);
    ids = s_view.ids;
    taskEXIT_CRITICAL(&s_mux);
    uint32_t next = 0u;
    if (!servo_id_next(ids.highest, &next)) {
        return PANEL_SERVO_NEW_NO_FREE;
    }
    char made[SERVO_ID_SIZE];
    char dir[CARD_RUN_DIR_MAX];
    if (!servo_id_build(made, sizeof(made), prefix, next)
        || !servo_id_folder(dir, sizeof(dir), made)) {
        return PANEL_SERVO_NEW_PREFIX;
    }
    /* The folder reserves the id: two NEW IDs in a row give two. */
    if (panel_card_make_dir(NULL, CARD_SERVOS) != CARD_DIR_OK
        || panel_card_make_dir(NULL, dir) != CARD_DIR_OK) {
        return PANEL_SERVO_NEW_FOLDER;
    }
    memcpy(id, made, sizeof(made));
    (void)walk_ids();
    return PANEL_SERVO_NEW_OK;
}

void panel_servo_card_serve(void)
{
    const unsigned sent = atomic_load(&s_new_sent);
    const bool asked = sent != atomic_load(&s_new_done);
    const unsigned mount = storage_mount_count();
    const unsigned edits = log_series_edits();
    const bool stale = mount != s_view_mount || edits != s_view_edits;
    if (!asked && !stale) {
        return;
    }
    (void)storage_check();
    const bool held = storage_hold();
    if (stale) {
        s_view_mount = mount;
        s_view_edits = edits;
        if (held) {
            (void)walk_ids();
        } else {
            taskENTER_CRITICAL(&s_mux);
            s_view.known = false;
            taskEXIT_CRITICAL(&s_mux);
        }
    }
    if (asked) {
        char prefix[sizeof(s_prefix)];
        taskENTER_CRITICAL(&s_mux);
        memcpy(prefix, s_prefix, sizeof(prefix));
        taskEXIT_CRITICAL(&s_mux);
        char id[SERVO_ID_SIZE];
        const panel_servo_new_t why =
            held ? new_id(prefix, id) : PANEL_SERVO_NEW_NO_CARD;
        if (!held) {
            id[0] = '\0';
        }
        taskENTER_CRITICAL(&s_mux);
        memcpy(s_new_id, id, sizeof(id));
        s_new_why = why;
        taskEXIT_CRITICAL(&s_mux);
        atomic_store(&s_new_done, sent);
    }
    if (held) {
        storage_release();
    }
}

void panel_servo_view(panel_servo_view_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    *out = s_view;
    taskEXIT_CRITICAL(&s_mux);
}

/* -------------------------------------------------------- the frame task */

bool panel_servo_new(const char *prefix)
{
    if (atomic_load(&s_new_sent) != atomic_load(&s_new_done)) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    (void)snprintf(s_prefix, sizeof(s_prefix), "%s",
                   (prefix != NULL) ? prefix : "");
    taskEXIT_CRITICAL(&s_mux);
    atomic_fetch_add(&s_new_sent, 1u);
    return true;
}

bool panel_servo_new_busy(void)
{
    return atomic_load(&s_new_sent) != atomic_load(&s_new_done);
}

/** @brief The console's word for how NEW ID ended. */
static const char *new_word(panel_servo_new_t why)
{
    switch (why) {
    case PANEL_SERVO_NEW_OK:         return "made";
    case PANEL_SERVO_NEW_NO_CARD:    return "no card";
    case PANEL_SERVO_NEW_UNREADABLE: return "Servos does not read";
    case PANEL_SERVO_NEW_NO_FREE:    return "no free id: the card holds ZZZZZ";
    case PANEL_SERVO_NEW_PREFIX:     return "ID PREFIX is no prefix";
    case PANEL_SERVO_NEW_FOLDER:     return "the folder cannot be made";
    case PANEL_SERVO_NEW_RUN:
    default:                         return "a run holds the servo";
    }
}

/** @brief The console's `servo` line: the active servo, the card's ids,
 *         the last run filed. */
static void print_state(void)
{
    const char *id = servo_active_id();
    panel_servo_view_t v;
    panel_servo_view(&v);
    printf("servo: active %s, run %s\n", (id != NULL) ? id : "none",
           servo_screen_run_active() ? "holds it" : "none");
    if (v.known) {
        char next[6] = "";
        uint32_t n = 0u;
        if (servo_id_next(v.ids.highest, &n)) {
            (void)servo_id_serial_text(n, next);
        }
        printf("servo: card %u id folders, next serial %s, walk %ums\n",
               (unsigned)v.ids.count, (next[0] != '\0') ? next : "none",
               v.walk_ms);
        for (unsigned i = 0u; i < v.ids.recent_n; ++i) {
            printf("servo:   %s\n", v.ids.recent[i]);
        }
    } else {
        printf("servo: card ids not read\n");
    }
    const servo_trend_last_t *f = servo_trend_last();
    if (f->count != 0u) {
        printf("servo: last filed %s kind %d run %03d\n", f->id, (int)f->kind,
               f->number);
    }
}

/** @brief Run console line @p line on the frame task. */
static void console_run(const char *line)
{
    if (strcmp(line, "servo") == 0) {
        print_state();
    } else if (strcmp(line, "servo new") == 0) {
        if (servo_screen_run_active()) {
            printf("servo: new: %s\n", new_word(PANEL_SERVO_NEW_RUN));
        } else if (!panel_servo_new(settings_text(SET_TEXT_SERVO_PREFIX))) {
            printf("servo: new: one is under way\n");
        }
    } else if (strcmp(line, "servo none") == 0) {
        printf("servo: none: %s\n",
               servo_active_clear(servo_screen_run_active())
                   ? "no servo active" : "refused, a run holds the servo");
    } else if (strncmp(line, "servo id ", 9u) == 0) {
        const char *id = line + 9;
        const servo_id_err_t e = servo_id_why(id);
        if (e != SERVO_ID_OK) {
            printf("servo: id: not an id (rule %d)\n", (int)e);
        } else if (!servo_active_set(id, SERVO_ACTIVE_PICK,
                                     servo_screen_run_active())) {
            printf("servo: id: refused, a run holds the servo\n");
        } else {
            printf("servo: id: %s active\n", servo_active_id());
        }
    } else {
        printf("servo: commands: servo, servo new, servo id <id>, "
               "servo none\n");
    }
}

void panel_servo_console(const char *line)
{
    if (line == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    (void)snprintf(s_line, sizeof(s_line), "%s", line);
    taskEXIT_CRITICAL(&s_mux);
}

void panel_servo_frame(void)
{
    char line[sizeof(s_line)];
    taskENTER_CRITICAL(&s_mux);
    memcpy(line, s_line, sizeof(line));
    s_line[0] = '\0';
    taskEXIT_CRITICAL(&s_mux);
    if (line[0] != '\0') {
        console_run(line);
    }
    const unsigned done = atomic_load(&s_new_done);
    if (done != s_new_taken && done == atomic_load(&s_new_sent)) {
        s_new_taken = done;
        char id[SERVO_ID_SIZE];
        panel_servo_new_t why;
        taskENTER_CRITICAL(&s_mux);
        memcpy(id, s_new_id, sizeof(id));
        why = s_new_why;
        taskEXIT_CRITICAL(&s_mux);
        if (why == PANEL_SERVO_NEW_OK
            && !servo_active_set(id, SERVO_ACTIVE_NEW,
                                 servo_screen_run_active())) {
            why = PANEL_SERVO_NEW_RUN;  /* its folder stays: the id is used */
        }
        ESP_LOGI(TAG, "servo new: %s%s%s", new_word(why),
                 (id[0] != '\0') ? " " : "", id);
    }
    /* The active servo to the logger: a SERVO run files by it from its
     * first row; a test's OPEN carries it (test_lines_service()). */
    if (servo_active_serial() != s_active_copied) {
        s_active_copied = servo_active_serial();
        log_set_run_servo(servo_active_id());
    }
    /* A run whose files in a servo's folder are closed. */
    log_filed_t f;
    log_filed(&f);
    if (f.count != s_filed_taken) {
        s_filed_taken = f.count;
        if (f.id[0] != '\0') {
            servo_trend_filed(f.id, f.kind, f.number);
        }
    }
}
