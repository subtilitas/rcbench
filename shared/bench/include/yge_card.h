/**
 * @file
 * @brief The card's side of programming for YGE ESCs (electronic speed
 *        controllers): the session's backup saved, the settings files of
 *        CARD_YGE_CONF listed and one loaded, and the requests that carry
 *        them.
 *
 * A file holds the text of openyge_file.h under YGE001 to YGE999
 * (servo_store.h, stem OY_FILE_STEM), with the ESC type code as a tag:
 * YGE001-5712 for type code 5712.  One number runs through the folder, so a
 * number is never taken twice and the newest file lists first.
 *
 * yge_card_run() blocks as long as the card does and works in static
 * buffers: one caller at a time, on a task that may wait for the card, never
 * in a draw.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_CARD_H
#define RCBENCH_YGE_CARD_H

#include <stddef.h>
#include <stdint.h>

#include "openyge_file.h"
#include "servo_store.h"
#include "yge_prog.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Files a list answer holds. */
#define YGE_CARD_FILES 12u
/** @brief Characters of a base name, with the terminator. */
#define YGE_CARD_NAME_MAX (SERVO_STORE_BASE_MAX + 1)
/** @brief Characters of a tag, with the terminator: a type code of up to 5
 *         digits. */
#define YGE_CARD_TAG_MAX 6u

/** @brief What a request asks. */
typedef enum {
    YGE_CARD_NONE = 0,
    YGE_CARD_SAVE, /**< @c text to a new file; the store proposes the name    */
    YGE_CARD_LIST, /**< the names from @c first on                            */
    YGE_CARD_LOAD, /**< the text of the file @c name                          */
} yge_card_op_t;

/** @brief How a request ended. */
typedef enum {
    YGE_CARD_DONE_OK = 0,
    YGE_CARD_DONE_NO_CARD,
    YGE_CARD_DONE_FULL,    /**< no space, or 001 to 999 are taken             */
    YGE_CARD_DONE_DENIED,  /**< the card takes no file                        */
    YGE_CARD_DONE_WRITE,   /**< a write failed                                */
    YGE_CARD_DONE_READ,    /**< a read failed                                 */
    YGE_CARD_DONE_MISSING, /**< no file has that name                         */
    YGE_CARD_DONE_LARGE,   /**< the file is over OY_FILE_MAX bytes            */
} yge_card_result_t;

/** @brief A request. */
typedef struct {
    uint8_t  op;     /**< a yge_card_op_t                                     */
    uint16_t first;  /**< LIST: the first file wanted, from 0                 */
    uint32_t id;     /**< comes back in the answer                            */
    size_t   len;    /**< SAVE: bytes of @c text                              */
    char     name[YGE_CARD_NAME_MAX]; /**< LOAD: the base name                */
    char     tag[YGE_CARD_TAG_MAX];   /**< SAVE: the type code, "" none       */
    char     text[OY_FILE_MAX];       /**< SAVE                               */
} yge_card_req_t;

/** @brief An answer. */
typedef struct {
    uint8_t  op;     /**< the request's                                       */
    uint8_t  result; /**< a yge_card_result_t                                 */
    uint8_t  count;  /**< LIST: the names                                     */
    uint16_t total;  /**< LIST: the files on the card, 65535 for more         */
    uint32_t id;     /**< the request's                                       */
    size_t   len;    /**< LOAD: bytes of @c text                              */
    char     name[YGE_CARD_NAME_MAX]; /**< SAVE: the name it took             */
    char     names[YGE_CARD_FILES][YGE_CARD_NAME_MAX]; /**< LIST              */
    char     text[OY_FILE_MAX + 1u]; /**< LOAD, terminated                    */
} yge_card_done_t;

/**
 * @brief Carry out a SAVE, a LIST or a LOAD on the card behind @p io.
 * @param io   The store, its stem OY_FILE_STEM.
 * @param req  The request.
 * @param done Receives the answer; every request gets one.
 */
void yge_card_run(const servo_store_io_t *io, const yge_card_req_t *req,
                  yge_card_done_t *done);

/**
 * @brief A SAVE's result as the model takes it (yge_prog_card_done()).
 * @param result A yge_card_result_t.
 * @return A yge_prog_card_t.
 */
uint8_t yge_card_prog_result(uint8_t result);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_YGE_CARD_H */
