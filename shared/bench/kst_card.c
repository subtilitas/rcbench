/*
 * The card's side of the KST servo programmer's files: see kst_card.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_card.h"

#include <stddef.h>

unsigned kst_card_name_max(void)
{
    return 8u;
}

kst_field_set_t kst_card_merge(const kst_image_t *servo,
                               const kst_image_t *file, kst_image_t *out)
{
    kst_field_set_t skipped = 0;

    *out = *servo;
    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);
        const uint16_t raw = kst_field_get(file, id);

        if (i == KST_F_DUTY_COPY || raw == kst_field_get(servo, id)) {
            continue;          /* the copy follows Max. Duty */
        }
        if (f != NULL && f->access == KST_ACCESS_EDITABLE) {
            (void)kst_field_edit(out, id, raw);
        } else {
            skipped |= KST_FIELD_BIT(i);
        }
    }
    return skipped;
}

unsigned kst_card_type(const kst_image_t *img)
{
    if (img == NULL) {
        return 0;
    }
    return kst_field_get(img, KST_F_20K_SEL) != 0u ? 6u : 8u;
}
