/**
 * @file
 * @brief The card's side of the programmer for an ESC (electronic speed
 *        controller) over its signal wire: the session's backup, a list and a
 *        load of the files in CARD_ESCW_CONF, and the requests that carry them.
 *
 * A file holds the text of ew_file.h under ESCW001 to ESCW999 (servo_store.h,
 * stem EW_FILE_STEM).  The record of a write is ESCW.TXT in the same folder, and
 * the task that has the card replaces it whole (panel.h): esc_card_run() does not
 * touch it.
 *
 * esc_card_run() blocks as long as the card does and works in static buffers:
 * one caller at a time, on a task that may wait for the card, never in a draw.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_CARD_H
#define RCBENCH_ESC_CARD_H

#include <stddef.h>
#include <stdint.h>

#include "esc_prog.h"
#include "ew_file.h"
#include "servo_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Files a list answer holds. */
#define ESC_CARD_FILES 12u
/** @brief Characters of a base name, with the terminator. */
#define ESC_CARD_NAME_MAX (SERVO_STORE_BASE_MAX + 1)
/** @brief Bytes a request or an answer holds: the largest is the record of a write. */
#define ESC_CARD_TEXT_MAX ESC_PROG_RECORD_MAX

/** @brief What a request asks. */
typedef enum {
    ESC_CARD_NONE = 0,
    ESC_CARD_SAVE,   /**< @c text to a new file; the store proposes the name  */
    ESC_CARD_LIST,   /**< the names from @c first on                          */
    ESC_CARD_LOAD,   /**< the text of the file @c name                        */
    ESC_CARD_RECORD, /**< @c text replaces ESCW.TXT                           */
} esc_card_op_t;

/** @brief How a request ended. */
typedef enum {
    ESC_CARD_DONE_OK = 0,
    ESC_CARD_DONE_NO_CARD,
    ESC_CARD_DONE_FULL,    /**< no space, or 001 to 999 are taken             */
    ESC_CARD_DONE_DENIED,  /**< the card takes no file                        */
    ESC_CARD_DONE_WRITE,   /**< a write failed                                */
    ESC_CARD_DONE_READ,    /**< a read failed                                 */
    ESC_CARD_DONE_MISSING, /**< no file has that name                         */
    ESC_CARD_DONE_LARGE,   /**< the file is over EW_FILE_MAX bytes            */
} esc_card_result_t;

/** @brief A request. */
typedef struct {
    uint8_t  op;     /**< an esc_card_op_t                                    */
    uint16_t first;  /**< LIST: the first file wanted, from 0                 */
    uint32_t id;     /**< comes back in the answer                            */
    size_t   len;    /**< SAVE, RECORD: bytes of @c text                      */
    char     name[ESC_CARD_NAME_MAX]; /**< LOAD: the base name                */
    char     text[ESC_CARD_TEXT_MAX]; /**< SAVE, RECORD                       */
} esc_card_req_t;

/** @brief An answer. */
typedef struct {
    uint8_t  op;     /**< the request's                                       */
    uint8_t  result; /**< an esc_card_result_t                                */
    uint8_t  count;  /**< LIST: the names                                     */
    uint16_t total;  /**< LIST: the files on the card, 65535 for more         */
    uint32_t id;     /**< the request's                                       */
    size_t   len;    /**< LOAD: bytes of @c text                              */
    char     name[ESC_CARD_NAME_MAX]; /**< SAVE: the name it took             */
    char     names[ESC_CARD_FILES][ESC_CARD_NAME_MAX]; /**< LIST              */
    char     text[EW_FILE_MAX + 1u]; /**< LOAD                                */
} esc_card_done_t;

/**
 * @brief Carry out a SAVE, a LIST or a LOAD on the card behind @p io.
 * @param io   The store.
 * @param req  The request.
 * @param done Receives the answer; every request gets one.
 */
void esc_card_run(const servo_store_io_t *io, const esc_card_req_t *req,
                  esc_card_done_t *done);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_CARD_H */
