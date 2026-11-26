/**
 * @file
 * @brief What the card knows of a servo: its folder, its runs and the DUT
 *        NAME its runs carry.
 *
 * servo_look() lists `Servos/<id>` once and reads the start of one CSV per
 * kind that has runs, its highest number (numbers count per kind; the FAT
 * stamp is no order, a file written before NTP carries 1980): at most 4
 * reads of the caller's buffer.  The name is the `dut` value of the `info`
 * cell on each file's first data row (log_info.h); a kind without one is
 * left out.  Where the kinds' names agree, that is the name.  Where they
 * differ, the name of the newest stamp, only when every stamp compared is
 * a real date (servo_look_stamp_valid()) and one is newest; otherwise no
 * name, never a wrong one.  A first data row that does not end inside the
 * buffer gives no name, never a cut one.
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

/** @brief Bytes servo_look() reads of a run: the header and the
 *         first data row of every run the bench files fit in 2,048. */
#define SERVO_LOOK_READ 2048u

/** @brief What servo_look() found. */
typedef enum {
    SERVO_LOOK_NO_FOLDER = 0,  /**< no `Servos/<id>`, or no id            */
    SERVO_LOOK_NO_RUNS,        /**< the folder holds no run's CSV         */
    SERVO_LOOK_RUNS,           /**< runs; their name, or ""               */
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
 * @param name  Receives the runs' DUT NAME; "" for none.
 * @param n     Its size, bytes; LOG_INFO_TEXT_MAX holds every name.
 * @param buf   Room for the read, SERVO_LOOK_READ bytes.
 * @param buf_n Its size, bytes.
 * @return What the card holds.
 */
servo_look_t servo_look(const servo_look_io_t *io, const char *id,
                        char *name, size_t n, char *buf, size_t buf_n);

/**
 * @brief Whether a FAT stamp is a real date: a year after 1980.  A file
 *        written before the clock is set carries 1980.
 * @param stamp FAT's date in bits 31..16 and time in bits 15..0.
 * @return true for a year from 1981.
 */
bool servo_look_stamp_valid(uint32_t stamp);

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
