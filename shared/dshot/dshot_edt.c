/*
 * When to ask an ESC (electronic speed controller) for extended telemetry.
 * See dshot.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "dshot.h"

void dshot_edt_bind(dshot_edt_t *e, uint32_t now_ms)
{
    if (e == NULL) {
        return;
    }
    e->asked = false;
    dshot_edt_idle(e, now_ms);
}

void dshot_edt_idle(dshot_edt_t *e, uint32_t now_ms)
{
    if (e == NULL) {
        return;
    }
    e->left     = 0u;
    e->asks     = 0u;
    e->answered = false;
    /* Kept current while nothing is sent, so the comparison below never sees
     * a due time from before a long disarm: it is wrap-safe over half of the
     * millisecond clock and no further. */
    e->due_ms   = now_ms;
}

bool dshot_edt_frame(dshot_edt_t *e, bool stopped, uint32_t now_ms)
{
    if (e == NULL || e->answered) {
        return false;
    }
    if (!stopped) {
        /* The throttle goes out on this frame.  An ESC counts repeats in a
         * row, so the part of an ask sent before it counts for nothing. */
        e->left = 0u;
        return false;
    }
    if (e->left == 0u) {
        if (e->asks >= DSHOT_EDT_ASKS) {
            return false;
        }
        if ((uint32_t)(now_ms - e->due_ms) >= 0x80000000u) {
            return false;                     /* not due yet, wrap-safe */
        }
        e->left = (uint8_t)DSHOT_CMD_REPEATS;
        /*
         * Replies are read as extended telemetry from the first repeat on.
         * AM32 takes the command on the sixth and answers it once, with a
         * status frame on one of the replies that follow; read as a speed,
         * that frame is a period of zero and the answer is lost.  Reading early
         * costs nothing from an ESC that ignores the command: it normalises
         * its exponent, so none of its frames reads as an extended one.
         */
        e->asked = true;
    }
    if (--e->left == 0u) {
        ++e->asks;
        e->due_ms = now_ms + DSHOT_EDT_RETRY_MS;
    }
    return true;
}

void dshot_edt_heard(dshot_edt_t *e, dshot_telem_kind_t kind)
{
    /*
     * Only after an ask: before one the decoder reads every frame as a speed,
     * so an extended kind cannot arrive -- and if one did, it would not be an
     * answer to anything this end sent.
     */
    if (e != NULL && e->asked && kind != DSHOT_TELEM_ERPM) {
        e->answered = true;
    }
}
