/*
 * The card's side of the servo programmer's files: a save, a list and a
 * load of kst_prog.h carried out on the card.
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

/** Files of the directory a list reaches, sorted by name: the first
 *  KST_CARD_LIST_MAX.  A card with more shows these and counts no others. */
#define KST_CARD_LIST_MAX 64u

/**
 * The servo type of an image as a number the screen prints behind a "V":
 * 6 or 8.  0 for NULL.
 */
unsigned kst_card_type(const kst_image_t *img);

/**
 * Carry out @p req on the card behind @p io and write the answer to
 * @p done.  Every request gets an answer.
 *
 * KST_PROG_CARD_SAVE writes the text of req->image to a new file: named by
 * kst_store_preset() for a preset save, else req->name.  No file is
 * replaced.
 *
 * KST_PROG_CARD_LIST gives the base names of up to KST_PROG_FILES_MAX
 * files from req->first on, each with the servo type its text states, and
 * the next preset name.
 *
 * KST_PROG_CARD_LOAD reads the file of req->name and holds it against
 * req->image with kst_file_load(): done->image is the servo's image with
 * the editable fields of the file.
 */
void kst_card_run(const kst_store_io_t *io, const kst_prog_card_req_t *req,
                  kst_prog_card_done_t *done);

#ifdef __cplusplus
}
#endif
