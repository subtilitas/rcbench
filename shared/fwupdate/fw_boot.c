/*
 * The first start after an update: see fw_boot.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_boot.h"

#include <stddef.h>

fw_boot_t fw_boot_decide(const fw_boot_ops_t *ops, bool ui_up, bool noted)
{
    if (ops->pending(ops->ctx)) {
        if (!ui_up) {
            /*
             * Back to the build before.  mark_invalid restarts when it
             * works; when it returns, a plain restart leaves the trial
             * unconfirmed, which the bootloader also takes back.  Either
             * way this build is never marked valid.
             */
            ops->mark_invalid(ops->ctx);
            ops->restart(ops->ctx);
            return FW_BOOT_UNCONFIRMED;
        }
        return ops->mark_valid(ops->ctx) ? FW_BOOT_UPDATED
                                         : FW_BOOT_UNCONFIRMED;
    }
    /*
     * A start after a written build that did not stay: the bootloader has
     * marked that slot invalid and started this one.  The slot stays marked
     * until the next update, so the note from before the restart is what
     * makes this report once.
     */
    if (noted && ops->last_invalid(ops->ctx)) {
        return FW_BOOT_ROLLED_BACK;
    }
    return FW_BOOT_NORMAL;
}
