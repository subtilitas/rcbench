/*
 * The card's side of the KST servo programmer's files, and the one place
 * that knows the servo file module (protocols/kst and the store layer).
 *
 * kst_prog.h asks for a save, a list and a load in images and names.  This
 * file answers: it has the text of a file, the rules of its name and the
 * servo type, none of which it states itself.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "kst_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The servo type of an image as a number the screen prints behind a "V":
 * 6 or 8.  0 for NULL.
 */
unsigned kst_card_type(const kst_image_t *img);

/** The characters a file's name may have at most. */
unsigned kst_card_name_max(void);

/**
 * What a file's image comes to on a servo: @p out is the servo's image
 * with the editable fields of the file.  Returns the fields in which the
 * file differs from the servo and that are never written: the locked ones,
 * the servo type among them.
 */
kst_field_set_t kst_card_merge(const kst_image_t *servo,
                               const kst_image_t *file, kst_image_t *out);

#ifdef __cplusplus
}
#endif
