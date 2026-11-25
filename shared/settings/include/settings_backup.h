/**
 * @file
 * @brief The settings files on the card: SAVE SETTINGS' file, the automatic
 *        copies and their rotation, the list LOAD SETTINGS shows, and a
 *        file read whole.  The requests that carry them between the task
 *        that draws and the task that has the card.
 *
 * Every file is in one folder of the card, CARD_SETUP (card_tree.h), which
 * the caller's functions (sb_io_t) reach:
 *
 *   SETTINGS.TXT   SAVE SETTINGS on SETUP > SD CARD; each save replaces it
 *   AUTO1.TXT      the newest automatic copy, written at each save of the
 *                  settings to the panel's store while a card is in
 *   AUTO2.TXT      the copy before it
 *   AUTO3.TXT      the oldest; the next copy removes it
 *   AUTO.TMP       an automatic copy being written
 *   SETTINGS.TMP   SAVE SETTINGS' file being written
 *
 * A file is written whole under its .TMP name first.  Only a complete
 * write moves the copies on (AUTO3 removed, AUTO2 to AUTO3, AUTO1 to AUTO2,
 * AUTO.TMP to AUTO1), so a full card or a failed write leaves the copies
 * there as they were.  The text is settings_file.h's.
 *
 * sb_run() blocks as long as the card does: one caller at a time, on the
 * task that has the card.  sb_serve() runs it under the card's job lock,
 * shared with the other writers of the folder, and never waits for it.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_file.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SAVE SETTINGS' file. */
#define SB_SAVED      "SETTINGS.TXT"
/** @brief SAVE SETTINGS' file while it is written. */
#define SB_SAVED_TMP  "SETTINGS.TMP"
/** @brief An automatic copy while it is written. */
#define SB_AUTO_TMP   "AUTO.TMP"
/** @brief The automatic copies kept: AUTO1.TXT to AUTO3.TXT. */
#define SB_AUTO_COPIES 3
/** @brief The files a list holds at most: the copies and SETTINGS.TXT. */
#define SB_FILES      (SB_AUTO_COPIES + 1)
/** @brief Room for a file name, with the terminator. */
#define SB_NAME_MAX   16u
/** @brief Bytes from the start of a file a list takes its first lines from. */
#define SB_HEAD_BYTES 256u

/** @brief What a function of the card reports. */
typedef enum {
    SB_IO_OK = 0,
    SB_IO_MISSING,  /**< no file has the name                               */
    SB_IO_FULL,     /**< no space; what was created is removed              */
    SB_IO_DENIED,   /**< the card takes no file: write protected, or a file
                         holds a folder's name                              */
    SB_IO_ERROR,    /**< any other failure; what was created is removed     */
} sb_io_result_t;

/** @brief The card's folder CARD_SETUP; every name is a name in it. */
typedef struct {
    void *ctx;                          /**< passed to every function       */
    /** @brief Whether the caller holds a mounted volume. */
    bool (*mounted)(void *ctx);
    /** @brief The size of a file in bytes; -1 when no file has the name. */
    int32_t (*size)(void *ctx, const char *name);
    /** @brief Up to @p cap bytes from the start of a file: the count; -1 on
     *         a failure. */
    int32_t (*read)(void *ctx, const char *name, char *buf, size_t cap);
    /** @brief Make the folder where it is missing, create or empty the file,
     *         write @p len bytes, commit them to the card and close. */
    sb_io_result_t (*write)(void *ctx, const char *name, const char *data,
                            size_t len);
    /** @brief Give file @p from the name @p to, which no file has. */
    sb_io_result_t (*rename)(void *ctx, const char *from, const char *to);
    /** @brief Remove a file; SB_IO_MISSING when there is none. */
    sb_io_result_t (*remove)(void *ctx, const char *name);
    /** @brief sb_serve(): take the card's job lock, which other writers of
     *         the folder share, without waiting; false while another holds
     *         it.  NULL with @c unlock: no lock. */
    bool (*lock)(void *ctx);
    /** @brief sb_serve(): give the lock @c lock took back. */
    void (*unlock)(void *ctx);
} sb_io_t;

/** @brief How a request ended.  The numbers are stable: the screen shows
 *         them as codes. */
typedef enum {
    SB_OK = 0,
    SB_NO_CARD = 1,  /**< no card is mounted                               */
    SB_FULL = 2,     /**< the card has no space for the file               */
    SB_DENIED = 3,   /**< the card takes no file                           */
    SB_WRITE = 4,    /**< a write, a rename or a remove failed             */
    SB_READ = 5,     /**< a read failed                                    */
    SB_MISSING = 6,  /**< no file has that name                            */
    SB_LARGE = 7,    /**< the file is over SF_FILE_MAX bytes               */
    SB_ARG = 8,      /**< a NULL, an unknown request or a name not of this
                          folder                                           */
} sb_result_t;

/** @brief One file of a list. */
typedef struct {
    char      name[SB_NAME_MAX]; /**< AUTO1.TXT, ..., SETTINGS.TXT          */
    uint8_t   err;               /**< sf_head()'s sf_err_t; SF_OK readable  */
    int32_t   size;              /**< its bytes as the list found them      */
    sf_head_t head;              /**< its first lines                       */
    bool      summed;            /**< read whole: up to SF_FILE_MAX bytes   */
    uint32_t  crc;               /**< summed: the CRC-32 of its bytes
                                      (fm_crc32()); 0 otherwise             */
} sb_entry_t;

/** @brief What a request asks. */
typedef enum {
    SB_REQ_NONE = 0,
    SB_REQ_SAVE,   /**< the buffer's @c len bytes as SETTINGS.TXT          */
    SB_REQ_AUTO,   /**< the buffer's @c len bytes as the newest copy       */
    SB_REQ_LIST,   /**< the files there are                                */
    SB_REQ_READ,   /**< the file @c name into the buffer                   */
} sb_req_op_t;

/** @brief A request; its text is in a buffer the caller holds. */
typedef struct {
    uint8_t op;                /**< an sb_req_op_t                         */
    size_t  len;               /**< SAVE, AUTO: bytes of the text          */
    char    name[SB_NAME_MAX]; /**< READ: the file                         */
} sb_req_t;

/** @brief An answer. */
typedef struct {
    uint8_t    op;              /**< the request's                         */
    uint8_t    result;          /**< an sb_result_t                        */
    uint8_t    count;           /**< LIST: files in @c files               */
    size_t     len;             /**< READ: bytes in the buffer             */
    char       name[SB_NAME_MAX]; /**< READ: the file                      */
    sb_entry_t files[SB_FILES]; /**< LIST: AUTO1 to AUTO3, then SETTINGS   */
} sb_done_t;

/**
 * @brief The name of automatic copy @p i.
 * @param i 1 for the newest to SB_AUTO_COPIES for the oldest.
 * @return "AUTO1.TXT" to "AUTO3.TXT"; NULL for another @p i.
 */
const char *sb_auto_name(int i);

/**
 * @brief Write @p text as SETTINGS.TXT: SETTINGS.TMP whole, then in the
 *        place of the file there was.
 * @param io   The card.
 * @param text The text.
 * @param len  Its bytes.
 * @return SB_OK, or why the file was not written.  On a failure before the
 *         rename the old file is as it was.
 */
sb_result_t sb_write_saved(const sb_io_t *io, const char *text, size_t len);

/**
 * @brief Write @p text as the newest automatic copy: AUTO.TMP whole, then
 *        the copies moved on and AUTO.TMP to AUTO1.TXT.
 * @param io   The card.
 * @param text The text.
 * @param len  Its bytes.
 * @return SB_OK, or why the copy was not written.  A failure writing
 *         AUTO.TMP leaves the copies as they were.
 */
sb_result_t sb_write_auto(const sb_io_t *io, const char *text, size_t len);

/**
 * @brief The files there are, with their first lines and the CRC-32 of
 *        their bytes: AUTO1 to AUTO3, then SETTINGS.TXT.
 *
 * Each file is read whole into @p buf, at most SF_FILE_MAX + 1 bytes
 * (32,769B), so a full list reads at most 4 x 32,769 = 131,076B.  The first
 * lines come from the first SB_HEAD_BYTES (256B).  A load compares the
 * file it reads with the sum, so an edit that keeps the size and the first
 * lines is found.
 *
 * @param io    The card.
 * @param buf   Takes each file in turn.
 * @param cap   Bytes at @p buf, SF_FILE_MAX + 1 or more.
 * @param out   Takes the files there are, in that order.
 * @param count Takes their number.
 * @return SB_OK, SB_NO_CARD or SB_ARG.  A file whose bytes do not read is
 *         listed with err SF_ERR_ARG; a file over SF_FILE_MAX bytes is
 *         listed with its first lines and not summed.
 */
sb_result_t sb_list(const sb_io_t *io, char *buf, size_t cap,
                    sb_entry_t out[SB_FILES], uint8_t *count);

/**
 * @brief The newest file of a list that reads: the latest `Written:` date
 *        where two files say theirs, else the first in list order.
 * @param files The list.
 * @param count Its files.
 * @return Its index; -1 when no file reads.
 */
int sb_newest(const sb_entry_t *files, uint8_t count);

/**
 * @brief Read a file of the folder whole.
 * @param io   The card.
 * @param name One of SB_SAVED and the copies' names.
 * @param buf  Takes the text, terminated.
 * @param cap  Bytes at @p buf, SF_FILE_MAX + 1 or more.
 * @param len  Takes its bytes.
 * @return SB_OK, SB_NO_CARD, SB_MISSING, SB_LARGE, SB_READ or SB_ARG.
 */
sb_result_t sb_read(const sb_io_t *io, const char *name, char *buf,
                    size_t cap, size_t *len);

/**
 * @brief Carry out a request on the card behind @p io.
 * @param io   The card.
 * @param req  The request.
 * @param buf  SAVE and AUTO: the text; READ: takes the file.
 * @param cap  Bytes at @p buf.
 * @param done Takes the answer; every request gets one.
 */
void sb_run(const sb_io_t *io, const sb_req_t *req, char *buf, size_t cap,
            sb_done_t *done);

/**
 * @brief sb_run() under the card's job lock (sb_io_t.lock), or nothing while
 *        another writer holds it.
 *
 * A FILES job or a write of the Wi-Fi page checks a file and replaces it
 * under that lock: a rotation of the copies between the two would make the
 * replace remove the newest copy.  The request waits instead, and the
 * caller asks again at its next pass.
 *
 * @param io   The card.
 * @param req  The request.
 * @param buf  SAVE and AUTO: the text; READ: takes the file.
 * @param cap  Bytes at @p buf.
 * @param done Takes the answer when the request ran; untouched otherwise.
 * @return true when the request ran and @p done holds its answer; false
 *         while the lock is held elsewhere.
 */
bool sb_serve(const sb_io_t *io, const sb_req_t *req, char *buf, size_t cap,
              sb_done_t *done);

#ifdef __cplusplus
}
#endif
