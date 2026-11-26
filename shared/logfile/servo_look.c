/**
 * @file
 * @brief What the card knows of a servo.  See servo_look.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_look.h"

#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "log_info.h"
#include "servo_id.h"
#include "servo_name.h"

/** @brief The folder's walk: the newest run's CSV so far. */
typedef struct {
    char     name[SERVO_NAME_SIZE];  /**< its name; "" for none yet */
    uint32_t stamp;                  /**< its FAT stamp             */
    int      number;                 /**< its number                */
} look_walk_t;

/**
 * @brief servo_look_visit_fn: keep the entry when it is a newer run.
 * @param walk   The walk, a look_walk_t.
 * @param name   The entry's name.
 * @param is_dir Whether it is a folder.
 * @param stamp  Its FAT stamp.
 */
static void look_visit(void *walk, const char *name, bool is_dir,
                       uint32_t stamp)
{
    look_walk_t *w = (look_walk_t *)walk;
    bool report = true;
    const int number = (!is_dir && name != NULL)
                           ? servo_run_parse(name, NULL, &report) : -1;
    if (number < 0 || report || strlen(name) >= sizeof(w->name)) {
        return;
    }
    if (w->name[0] != '\0'
        && (stamp < w->stamp || (stamp == w->stamp && number <= w->number))) {
        return;
    }
    (void)snprintf(w->name, sizeof(w->name), "%s", name);
    w->stamp = stamp;
    w->number = number;
}

/**
 * @brief The end of the line that starts at @p at.
 * @param text The bytes.
 * @param at   Where the line starts.
 * @param len  The bytes' count.
 * @return The index of its '\n'; @p len when it does not end inside.
 */
static size_t line_end(const char *text, size_t at, size_t len)
{
    while (at < len && text[at] != '\n') {
        ++at;
    }
    return at;
}

/**
 * @brief Whether a character is padding around a cell.
 * @param c The character.
 * @return true for a space, a tab or a CR.
 */
static bool pad(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/**
 * @brief Cell @p col of the line [@p at, @p end), padding trimmed.
 * @param text The bytes.
 * @param at   The line's start; receives the cell's start, @p end when
 *             the line has no such cell.
 * @param end  The line's end.
 * @param col  The cell's index, 0 for the first.
 * @return The cell's length.
 */
static size_t cell_at(const char *text, size_t *at, size_t end, int col)
{
    size_t i = *at;
    for (int c = 0; c < col; ++c) {
        while (i < end && text[i] != ';') {
            ++i;
        }
        if (i == end) {
            *at = end;
            return 0u;
        }
        ++i;
    }
    size_t stop = i;
    while (stop < end && text[stop] != ';') {
        ++stop;
    }
    while (i < stop && pad(text[i])) {
        ++i;
    }
    while (stop > i && pad(text[stop - 1u])) {
        --stop;
    }
    *at = i;
    return stop - i;
}

/**
 * @brief Whether the line [@p at, @p end) holds nothing but separators.
 * @param text The bytes.
 * @param at   The line's start.
 * @param end  The line's end.
 * @return true for a blank line.
 */
static bool blank(const char *text, size_t at, size_t end)
{
    for (; at < end; ++at) {
        if (text[at] != ';' && !pad(text[at])) {
            return false;
        }
    }
    return true;
}

bool servo_look_info(const char *text, size_t len, char *name, size_t n)
{
    if (name != NULL && n > 0u) {
        name[0] = '\0';
    }
    if (text == NULL || name == NULL || n == 0u) {
        return false;
    }
    const size_t head_end = line_end(text, 0u, len);
    if (head_end == len) {
        return false;
    }
    int col = -1;
    for (int c = 0; c < 64; ++c) {
        size_t at = 0u;
        const size_t k = cell_at(text, &at, head_end, c);
        if (at == head_end && k == 0u) {
            break;
        }
        if (k == sizeof(LOG_INFO_HEAD) - 1u
            && memcmp(text + at, LOG_INFO_HEAD, k) == 0) {
            col = c;
        }
    }
    if (col < 0) {
        return false;
    }
    size_t row = head_end + 1u;
    size_t end = line_end(text, row, len);
    while (end < len && blank(text, row, end)) {
        row = end + 1u;
        end = line_end(text, row, len);
    }
    if (end == len) {
        return false;  /* the row runs past the read: no cut name */
    }
    const size_t k = cell_at(text, &row, end, col);
    char cell[LOG_INFO_MAX];
    if (k == 0u || k >= sizeof(cell)) {
        return false;
    }
    memcpy(cell, text + row, k);
    cell[k] = '\0';
    log_info_t info;
    if (!log_info_parse(cell, &info)) {
        return false;
    }
    (void)snprintf(name, n, "%s", info.dut);
    return true;
}

servo_look_t servo_look(const servo_look_io_t *io, const char *id,
                        char *name, size_t n, char *buf, size_t buf_n)
{
    if (name != NULL && n > 0u) {
        name[0] = '\0';
    }
    char dir[CARD_RUN_DIR_MAX];
    if (io == NULL || io->list == NULL || id == NULL
        || !servo_id_folder(dir, sizeof(dir), id)) {
        return SERVO_LOOK_NO_FOLDER;
    }
    look_walk_t w = { .name = "" };
    const int listed = io->list(io->ctx, dir, look_visit, &w);
    if (listed == SERVO_LOOK_LIST_NO_DIR) {
        return SERVO_LOOK_NO_FOLDER;
    }
    if (listed < 0) {
        return SERVO_LOOK_UNREADABLE;
    }
    if (w.name[0] == '\0') {
        return SERVO_LOOK_NO_RUNS;
    }
    char path[CARD_RUN_DIR_MAX + SERVO_NAME_SIZE];
    (void)snprintf(path, sizeof(path), "%s/%s", dir, w.name);
    const size_t got = (io->read != NULL && buf != NULL && buf_n > 0u)
                           ? io->read(io->ctx, path, buf, buf_n) : 0u;
    (void)servo_look_info(buf, (got <= buf_n) ? got : buf_n, name, n);
    return SERVO_LOOK_RUNS;
}
