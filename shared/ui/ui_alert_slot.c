/*
 * The control task's alert slot (ui_alert_slot.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_alert_slot.h"

#include <stdio.h>

uint32_t ui_alert_slot_post(ui_alert_slot_t *s, const char *text)
{
    (void)snprintf(s->text, sizeof(s->text), "%s", text);
    s->pending = true;
    ++s->posted;
    if (s->posted == 0u) {
        s->posted = 1u;                 /* 0 is "none" */
    }
    return s->posted;
}

void ui_alert_slot_withdraw(ui_alert_slot_t *s, uint32_t number)
{
    if (number == 0u) {
        return;
    }
    if (s->pending && s->posted == number) {
        s->pending = false;             /* never shown */
    } else if (s->taken == number) {
        s->withdrawn = number;          /* in the band, or cleared there */
    }
}

bool ui_alert_slot_take(ui_alert_slot_t *s, char *out, size_t size)
{
    if (!s->pending) {
        return false;
    }
    if (size > 0u) {
        (void)snprintf(out, size, "%s", s->text);
    }
    s->pending = false;
    s->taken   = s->posted;
    return true;
}

bool ui_alert_band_update(ui_alert_band_t *b, const char *text,
                          uint32_t taken, uint32_t withdrawn)
{
    if (text != NULL) {
        ui_router_set_alert(text);
        b->slot = taken;
        b->band = ui_router_alert_number();
    }
    if (withdrawn == 0u || withdrawn != b->slot) {
        return false;
    }
    const bool cleared = ui_router_withdraw_alert(b->band);
    b->slot = 0u;
    b->band = 0u;
    return cleared;
}
