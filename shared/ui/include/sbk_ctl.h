/**
 * @file
 * @brief The settings backup's driver: SAVE SETTINGS, LOAD SETTINGS, the
 *        automatic copy and the restore offer at start, between the screens
 *        and the task that has the card.
 *
 * The panel calls sbk_tick() once a frame on the task that draws.  It hands
 * out at most one request (settings_backup.h) at a time, its text in
 * sbk_buffer(), and waits for sbk_done() before the next.  The task that has
 * the card carries it out (sb_run()), so nothing here waits for the card:
 * the screens and the control task never do.
 *
 * - SAVE SETTINGS (sbk_ask_save()) writes the stored settings as
 *   SETTINGS.TXT.
 * - The automatic copy: a frame whose feed says the settings were written to
 *   the panel's store (sbk_feed_t's @c committed), with a card mounted, owes
 *   a copy, written as AUTO1.TXT with the older copies moved on.  A copy that
 *   does not reach the card is said once in the alert band; the next copy
 *   that does clears that, so the next failure is said again.  No card is
 *   no failure.  A failed copy is no fault of the bench: it does not switch
 *   an output off.  While BACKUP shows, a copy owed waits and is written
 *   once the screen is left: a copy renames the files the screen lists.
 *   Each copy written has the list read again, and a list that changes
 *   under a chosen file drops the choice (sbk_screen_relisted()).
 * - LOAD SETTINGS (sbk_ask_load()) reads a file whole and puts it in force
 *   (sf_apply()) on a SAFE bench, then asks for the save.
 * - The restore offer: a panel that started with the defaults only
 *   (settings_stored_defaults()) lists the card at its first mount, reads
 *   the newest file, and offers it (SCREEN_SET_LOAD over OVERVIEW, on a SAFE
 *   bench) when it holds other settings.  Until the offer is answered, or
 *   found not due, no automatic copy is written: a copy of the defaults would
 *   push a good copy one place on.  A list or read that finds no card puts
 *   the offer back to waiting for the next mount.
 *
 * The restore offer's states (sbk_offer_t) and what moves them.  PULL is
 * a card gone, or another card in its place (a new mount number), which
 * is PULL and then MOUNT in one frame.  An answer to a request handed out
 * before a PULL is not the offer's.  "-" leaves the state as it is.
 *
 * | event                        | WAIT     | CHECKING            | DUE               | SHOWN                    |
 * |------------------------------|----------|---------------------|-------------------|--------------------------|
 * | MOUNT                        | CHECKING | -                   | -                 | -                        |
 * | PULL                         | -        | WAIT                | WAIT              | WAIT; BACKUP: No card    |
 * | list OK                      | -        | read newest; none: DONE | newest picked again; none: DONE | the screen checks its choice |
 * | list NO_CARD                 | -        | WAIT (CHECKING with a card in) | -      | -                        |
 * | list, another error          | -        | DONE                | -                 | -                        |
 * | read: other settings         | -        | DUE                 | -                 | -                        |
 * | read: same, refused, error   | -        | DONE                | -                 | -                        |
 * | read NO_CARD                 | -        | WAIT (CHECKING with a card in) | -      | -                        |
 * | SAFE on OVERVIEW             | -        | -                   | SHOWN             | -                        |
 * | SAFE, BACKUP open as offer   | -        | -                   | SHOWN, newest chosen | -                     |
 * | ARMED                        | -        | -                   | - (not shown)     | - (no hold)              |
 * | BACKUP left: SKIP, BACK, home| DONE (if it was the offer's) | - | -              | DONE                     |
 * | a LOAD put in force          | DONE     | DONE                | DONE              | - (left: DONE)           |
 * | the store holds other settings | DONE   | DONE                | DONE              | -                        |
 *
 * DONE is left by nothing: the offer comes once a start.  No automatic
 * copy is written before DONE.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_backup.h"
#include "settings_file.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the panel tells the driver each frame. */
typedef struct {
    bool    mounted;    /**< a card is mounted                            */
    bool    armed;      /**< the bench is ARMED                           */
    bool    committed;  /**< the settings were written to the store this
                             frame (settings_save_tick())                 */
    int64_t wall_s;     /**< the clock, s since 1970; a time wall_valid()
                             refuses writes "not known"                   */
    unsigned mount;     /**< the mount's number (storage_mount_count()): a
                             new number while mounted is another card     */
} sbk_feed_t;

/** @brief SAVE SETTINGS' state, for its row. */
typedef enum {
    SBK_SAVE_IDLE = 0,  /**< nothing asked since the start                */
    SBK_SAVE_WRITING,   /**< asked, not answered                          */
    SBK_SAVE_WRITTEN,   /**< the last one reached the card                */
    SBK_SAVE_FAILED,    /**< the last one did not; sbk_save_code() says why */
} sbk_save_t;

/** @brief The list's state, for LOAD SETTINGS' screen. */
typedef enum {
    SBK_LIST_NONE = 0,  /**< none asked                                   */
    SBK_LIST_READING,   /**< asked, not answered                          */
    SBK_LIST_READY,     /**< answered; sbk_list_code() says how           */
} sbk_list_t;

/** @brief A load's state. */
typedef enum {
    SBK_LOAD_IDLE = 0,  /**< none asked since the screen opened           */
    SBK_LOAD_READING,   /**< the file is being read                       */
    SBK_LOAD_DONE,      /**< put in force; sbk_load_result() lists refusals */
    SBK_LOAD_FAILED,    /**< not put in force; sbk_load_why() says why    */
} sbk_load_t;

/** @brief Why a load put nothing in force. */
typedef enum {
    SBK_WHY_NONE = 0,
    SBK_WHY_CARD,       /**< the card's answer, an sb_result_t           */
    SBK_WHY_FILE,       /**< the file is refused whole, an sf_err_t       */
    SBK_WHY_ARMED,      /**< the bench was ARMED when the file arrived    */
    SBK_WHY_CHANGED,    /**< the file read is not the one listed: another
                             size, date, version or format; the list is
                             read again and nothing is chosen             */
} sbk_why_t;

/** @brief The restore offer's state. */
typedef enum {
    SBK_OFFER_WAIT = 0, /**< the panel started on defaults, no card yet   */
    SBK_OFFER_CHECKING, /**< the card is listed and its newest file read  */
    SBK_OFFER_DUE,      /**< a file holds other settings: to be offered   */
    SBK_OFFER_SHOWN,    /**< on the screen                                */
    SBK_OFFER_DONE,     /**< answered, or not due                         */
} sbk_offer_t;

/** @brief Start: whether the panel started on defaults, nothing asked. Call
 *         after settings_init(). */
void sbk_init(void);

/**
 * @brief A frame: the feed taken in, the offer brought on, and the next
 *        request handed out when none is under way.
 * @param f   The feed.
 * @param req Takes the request; its text, if any, is in sbk_buffer().
 * @return true when @p req holds a request to carry out.
 */
bool sbk_tick(const sbk_feed_t *f, sb_req_t *req);

/**
 * @brief The text of a request and of a read's answer: SF_FILE_MAX + 1
 *        bytes, the card task's only while a request is under way.
 * @return The buffer.
 */
char *sbk_buffer(void);

/** @brief The bytes of sbk_buffer(). @return SF_FILE_MAX + 1. */
size_t sbk_buffer_size(void);

/**
 * @brief The answer to the request sbk_tick() handed out.
 *
 * @p armed is the bench's state in the frame that hands the answer in,
 * which may be the first to see ARMED: a read for LOAD SETTINGS is put in
 * force only when it is false, whatever the bench was when the read was
 * asked for or at the last sbk_tick().  The other answers change no setting
 * and drive nothing: a written file, a copy and a list are taken whatever
 * @p armed says, and the restore offer opens only on a SAFE bench.
 *
 * @param d     The answer.
 * @param armed Whether the bench is ARMED now.
 */
void sbk_done(const sb_done_t *d, bool armed);

/** @brief Whether a request is under way. @return true while one is. */
bool sbk_busy(void);

/** @brief Whether the last feed said ARMED. @return true when it did. */
bool sbk_armed(void);

/** @brief Whether the last feed said a card is mounted. @return true when
 *         it did. */
bool sbk_mounted(void);

/* ------------------------------------------------------------ SAVE SETTINGS */

/** @brief SAVE SETTINGS: write the stored settings as SETTINGS.TXT. */
void sbk_ask_save(void);

/** @brief SAVE SETTINGS' state. @return The state. */
sbk_save_t sbk_save_state(void);

/** @brief Why the last SAVE SETTINGS failed. @return An sb_result_t. */
uint8_t sbk_save_code(void);

/* ----------------------------------------------------------- LOAD SETTINGS */

/** @brief List the card's settings files anew. */
void sbk_ask_list(void);

/** @brief The list's state. @return The state. */
sbk_list_t sbk_list_state(void);

/** @brief How the last list ended. @return An sb_result_t. */
uint8_t sbk_list_code(void);

/**
 * @brief The files of the last list.
 * @param count Takes their number.
 * @return The files, AUTO1 to AUTO3 and SETTINGS.TXT as there are.
 */
const sb_entry_t *sbk_files(uint8_t *count);

/**
 * @brief Read file @p index of the list and put it in force once it is read.
 * @param index The file, from 0.
 */
void sbk_ask_load(int index);

/** @brief The load's state. @return The state. */
sbk_load_t sbk_load_state(void);

/**
 * @brief Why the last load failed.
 * @param code Takes the card's or the file's code; may be NULL.
 * @param line Takes the line of a file refused whole; may be NULL.
 * @return The kind.
 */
sbk_why_t sbk_load_why(uint8_t *code, unsigned *line);

/**
 * @brief The last load put in force.
 * @param changed Takes the values and strings it changed; may be NULL.
 * @return The file read, with the lines refused and the held settings.
 */
const sf_load_t *sbk_load_result(int *changed);

/** @brief The name of the file the last load read. @return The name. */
const char *sbk_load_name(void);

/** @brief Forget the last load's answer: the screen's list shows again. */
void sbk_load_clear(void);

/* --------------------------------------------------------- the restore offer */

/** @brief The restore offer's state. @return The state. */
sbk_offer_t sbk_offer_state(void);

/** @brief The file the offer names, an index of sbk_files(). @return The
 *         index; -1 for none. */
int sbk_offer_index(void);

/** @brief The offer is answered: loaded, skipped, or left. */
void sbk_offer_answered(void);

/** @brief Whether an automatic copy may be written now: the offer is
 *         answered or not due. @return true when it may. */
bool sbk_auto_allowed(void);

#ifdef __cplusplus
}
#endif
