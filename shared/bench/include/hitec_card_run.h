/**
 * @file
 * @brief The card's side of the Hitec programmer's servo files: a save, a
 *        list and a load of hitec_card.h carried out on the card.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The model asks in images and base names (hitec_card_req_t).  This file
 * answers with the text of hitec_card.h and the names and the folder of
 * servo_store.h.  The card is reached through the 5 functions of
 * servo_store_io_t, so the same code runs on a host without a card.
 *
 * hitec_card_run() blocks as long as the card does and works in static
 * buffers: one caller at a time, on a task that may wait for the card,
 * never inside a draw.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_card.h"
#include "servo_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File names a list holds at a time: a page behind the first
 *        HITEC_CARD_LIST_MAX files walks the folder once more per
 *        HITEC_CARD_LIST_MAX files in front of it.
 */
#define HITEC_CARD_LIST_MAX 64u

/**
 * @brief Carry out @p req on the card behind @p io and write the answer to
 *        @p done.  Every request gets an answer.
 *
 * HITEC_CARD_SAVE writes the text of req->image to a new file: named by
 * servo_store_preset() for a preset save, else req->name.  No file is
 * replaced.
 *
 * HITEC_CARD_LIST gives the base names of up to HITEC_CARD_FILES files from
 * req->first on, in the order of servo_store_list_after(), each with the
 * model its text names, and the next proposed name.
 *
 * HITEC_CARD_LOAD reads and parses the file of req->name: done->image is
 * the file's image.
 *
 * @param io   The store, its stem HITEC_CARD_STEM.
 * @param req  The request.
 * @param done Receives the answer.
 */
void hitec_card_run(const servo_store_io_t *io, const hitec_card_req_t *req,
                    hitec_card_done_t *done);

#ifdef __cplusplus
}
#endif
