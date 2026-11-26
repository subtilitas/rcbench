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

/** @brief One kind's highest run in the folder. */
typedef struct {
    char     name[SERVO_NAME_SIZE];  /**< its CSV's name; "" for none */
    uint32_t stamp;                  /**< its FAT stamp               */
    int      number;                 /**< its number                  */
} look_kind_t;

/** @brief The folder's walk: each kind's highest run. */
typedef struct {
    look_kind_t kind[SERVO_KIND_COUNT];  /**< by servo_kind_t */
} look_walk_t;

/**
 * @brief servo_look_visit_fn: keep the entry when it is its kind's highest
 *        run so far.
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
    servo_kind_t k = SERVO_KIND_COUNT;
    const int number = (!is_dir && name != NULL)
                           ? servo_run_parse(name, &k, &report) : -1;
    if (number < 0 || report || (unsigned)k >= SERVO_KIND_COUNT
        || strlen(name) >= SERVO_NAME_SIZE) {
        return;
    }
    look_kind_t *o = &w->kind[k];
    if (o->name[0] != '\0' && number <= o->number) {
        return;
    }
    (void)snprintf(o->name, sizeof(o->name), "%s", name);
    o->stamp = stamp;
    o->number = number;
}

bool servo_look_stamp_valid(uint32_t stamp)
{
    return (stamp >> 25) != 0u;  /* FAT's year field: years after 1980 */
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
    look_walk_t w;
    memset(&w, 0, sizeof(w));
    const int listed = io->list(io->ctx, dir, look_visit, &w);
    if (listed == SERVO_LOOK_LIST_NO_DIR) {
        return SERVO_LOOK_NO_FOLDER;
    }
    if (listed < 0) {
        return SERVO_LOOK_UNREADABLE;
    }
    /* Each kind's highest run's name; the kinds that agree name it. */
    char best[LOG_INFO_TEXT_MAX] = "";
    uint32_t best_stamp = 0u;
    bool runs = false;
    bool differ = false;
    bool all_valid = true;
    bool tie = false;
    for (unsigned k = 0u; k < (unsigned)SERVO_KIND_COUNT; ++k) {
        const look_kind_t *o = &w.kind[k];
        if (o->name[0] == '\0') {
            continue;
        }
        runs = true;
        char path[CARD_RUN_DIR_MAX + SERVO_NAME_SIZE];
        (void)snprintf(path, sizeof(path), "%s/%s", dir, o->name);
        const size_t got = (io->read != NULL && buf != NULL && buf_n > 0u)
                               ? io->read(io->ctx, path, buf, buf_n) : 0u;
        char dut[LOG_INFO_TEXT_MAX];
        if (!servo_look_info(buf, (got <= buf_n) ? got : buf_n, dut,
                             sizeof(dut))
            || dut[0] == '\0') {
            continue;
        }
        all_valid = all_valid && servo_look_stamp_valid(o->stamp);
        if (best[0] == '\0') {
            (void)snprintf(best, sizeof(best), "%s", dut);
            best_stamp = o->stamp;
            continue;
        }
        const bool same = strcmp(dut, best) == 0;
        differ = differ || !same;
        if (o->stamp > best_stamp) {
            (void)snprintf(best, sizeof(best), "%s", dut);
            best_stamp = o->stamp;
            tie = false;
        } else if (o->stamp == best_stamp && !same) {
            tie = true;
        }
    }
    if (!runs) {
        return SERVO_LOOK_NO_RUNS;
    }
    /* Names that differ: the newest stamp, only when every stamp is a real
     * date and one is newest; else no name, never a wrong one. */
    if (!differ || (all_valid && !tie)) {
        (void)snprintf(name, n, "%s", best);
    }
    return SERVO_LOOK_RUNS;
}
