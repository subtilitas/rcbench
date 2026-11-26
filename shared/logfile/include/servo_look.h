/**
 * @file
 * @brief What the card knows of a servo: its folder, its runs and the DUT
 *        NAME of its newest run.
 *
 * servo_look() lists `Servos/<id>` once and reads the start of one file:
 * the newest run's CSV, at most the caller's buffer.  The newest run is
 * the one with the latest FAT stamp; equal stamps (a bench without a set
 * clock) go to the higher number.  The DUT NAME is the `dut` value of the
 * `info` cell on the file's first data row (log_info.h).  A first data row
 * that does not end inside the buffer gives no name, never a cut one.
 *
 * The card is reached through the caller's io, so the panel reads it on
 * its logger task and the host suite from memory.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes servo_look() reads of the newest run: the header and the
 *         first data row of every run the bench files fit in 2,048. */
#define SERVO_LOOK_READ 2048u

/** @brief What servo_look() found. */
typedef enum {
    SERVO_LOOK_NO_FOLDER = 0,  /**< no `Servos/<id>`, or no id            */
    SERVO_LOOK_NO_RUNS,        /**< the folder holds no run's CSV         */
    SERVO_LOOK_RUNS,           /**< runs; the name of the newest, or ""   */
    SERVO_LOOK_UNREADABLE,     /**< the folder does not read to its end   */
} servo_look_t;

/**
 * @brief One entry of the folder, handed to the walk.
 * @param walk   The walk's state, as given to the list call.
 * @param name   The entry's name.
 * @param is_dir Whether it is a folder.
 * @param stamp  FAT's date in bits 31..16 and time in bits 15..0.
 */
typedef void (*servo_look_visit_fn)(void *walk, const char *name,
                                    bool is_dir, uint32_t stamp);

/** @brief The list call's answer for a folder that is not there. */
#define SERVO_LOOK_LIST_NO_DIR (-2)

/** @brief The card, as servo_look() reaches it. */
typedef struct {
    /**
     * @brief List folder @p dir, each entry to @p visit.
     * @return The entries listed; SERVO_LOOK_LIST_NO_DIR for no folder;
     *         another negative value for a folder that does not read.
     */
    int (*list)(void *ctx, const char *dir, servo_look_visit_fn visit,
                void *walk);
    /**
     * @brief Read the start of file @p path into @p buf.
     * @return The bytes read, at most @p n; 0 for a file that does not read.
     */
    size_t (*read)(void *ctx, const char *path, char *buf, size_t n);
    void *ctx;  /**< handed to both calls */
} servo_look_io_t;

/**
 * @brief Look servo @p id up on the card.
 * @param io    The card.
 * @param id    The id.
 * @param name  Receives the newest run's DUT NAME; "" for none.
 * @param n     Its size, bytes; LOG_INFO_TEXT_MAX holds every name.
 * @param buf   Room for the read, SERVO_LOOK_READ bytes.
 * @param buf_n Its size, bytes.
 * @return What the card holds.
 */
servo_look_t servo_look(const servo_look_io_t *io, const char *id,
                        char *name, size_t n, char *buf, size_t buf_n);

/**
 * @brief The DUT NAME of a CSV's start: the `info` column of the header
 *        and its cell on the first data row that is not blank.
 * @param text The file's first bytes; not NUL ended.
 * @param len  Their count.
 * @param name Receives the name; "" for none.
 * @param n    Its size, bytes.
 * @return true when the row ends inside @p len and its cell holds a known
 *         key.
 */
bool servo_look_info(const char *text, size_t len, char *name, size_t n);

#ifdef __cplusplus
}
#endif
