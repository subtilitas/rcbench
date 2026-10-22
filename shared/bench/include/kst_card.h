/**
 * @file
 * @brief The card's side of the servo programmer's files: a save, a list and a
 *        load of kst_prog.h carried out on the card.
 *
 * kst_prog.h asks in images and base names.  This file answers with the
 * text of kst_file.h, the names and the directory of kst_store.h and the
 * servo type of kst_reg.h; it states none of the three itself.  The card is
 * reached through the 5 functions of kst_store_io_t, so the same code runs
 * on a host without a card.
 *
 * kst_card_run() blocks as long as the card does and works in static
 * buffers: one caller at a time, on a task that may wait for the card,
 * never inside a draw.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "kst_prog.h"
#include "kst_reg.h"
#include "kst_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File names a list holds at a time.
 *
 * A list reaches every file of the
 *  directory: for a page behind the first KST_CARD_LIST_MAX files the
 *  directory is walked once more per KST_CARD_LIST_MAX files, or fewer by
 *  up to KST_PROG_FILES_MAX, in front of the page.
 */
#define KST_CARD_LIST_MAX 64u

/**
 * @brief The servo type of an image as a number the screen prints behind a "V":
 *        6 or 8.
 *
 * 0 for NULL.
 *
 * @param img The image to read.
 * @return 6 or 8; 0 for NULL.
 */
unsigned kst_card_type(const kst_image_t *img);

/**
 * @brief Carry out @p req on the card behind @p io and write the answer to @p
 *        done.
 *
 * Every request gets an answer.
 *
 * KST_PROG_CARD_SAVE writes the text of req->image to a new file: named by
 * kst_store_preset() for a preset save, else req->name.  No file is
 * replaced.
 *
 * KST_PROG_CARD_LIST gives the base names of up to KST_PROG_FILES_MAX
 * files from req->first on in the order of kst_store_list(), the latest
 * proposed name first among the proposed ones, each with the servo type its text states, and the
 * next preset name.  done->total is the number of servo files in the
 * directory, 65535 for that many or more.
 *
 * KST_PROG_CARD_LOAD reads the file of req->name and holds it against
 * req->image with kst_file_load(): done->image is the servo's image with
 * the editable fields of the file.
 *
 * @param io   The store the request is carried out on.
 * @param req  The request.
 * @param done Receives the answer.
 */
void kst_card_run(const kst_store_io_t *io, const kst_prog_card_req_t *req,
                  kst_prog_card_done_t *done);

#ifdef __cplusplus
}
#endif
