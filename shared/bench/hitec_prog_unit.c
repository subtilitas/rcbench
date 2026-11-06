/**
 * @file
 * @brief Which unit the session's backup is of: whether the programmer for
 *        servos by Hitec tells the servo read apart from every other unit
 *        of its model, which decides whether RESTORE of the session's
 *        backup asks the operator to confirm the unit.  See hitec_prog.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * A classic servo names its unit by the serial in cells 0x80 to 0x86, 7
 * ASCII digits.  A D series servo has no serial register the programmer
 * knows of: the ID 0x32 is a setting, and two units of one model can hold
 * the same ID and the same values.  The session's chain (hitec_prog_file.c)
 * ends the backup where the serial, the ID or another written value
 * differs; where nothing differs, only a serial says the unit is the same.
 * Without one the operator says it: the screen's 2s hold calls
 * hitec_prog_restore_backup_unit().
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog.h"

/**
 * @brief Whether a classic image holds a serial: cells 0x80 to 0x86 read,
 *        each an ASCII digit.
 *
 * @param img A classic image of a full read.
 * @return true when it does.
 */
static bool serial_read(const hitec_image_t *img)
{
    for (unsigned c = 0; c < HITEC_C_SERIAL_LEN; ++c) {
        const uint8_t at = (uint8_t)(HITEC_C_SERIAL + c);
        const uint8_t v = hitec_image_cell(img, at);

        if (!hitec_image_known(img, at) || v < (uint8_t)'0'
            || v > (uint8_t)'9') {
            return false;
        }
    }
    return true;
}

bool hitec_prog_unit_told_apart(const hitec_prog_t *p)
{
    if (!p->have_bk || !p->have_servo
        || p->servo.series != (uint8_t)HITEC_CLASSIC
        || p->bk.series != (uint8_t)HITEC_CLASSIC) {
        return false;
    }
    /* A backup of another serial is no backup of this servo read: the
     * chain takes a new one at such a read.  Both serials are checked so
     * that the answer does not lean on that. */
    if (!serial_read(&p->servo) || !serial_read(&p->bk)) {
        return false;
    }
    for (unsigned c = 0; c < HITEC_C_SERIAL_LEN; ++c) {
        const uint8_t at = (uint8_t)(HITEC_C_SERIAL + c);

        if (hitec_image_cell(&p->servo, at) != hitec_image_cell(&p->bk, at)) {
            return false;
        }
    }
    return true;
}

uint32_t hitec_prog_backup_age_ms(const hitec_prog_t *p)
{
    return p->have_bk ? p->f.now_ms - p->bk_ms : 0u;
}

hitec_prog_msg_id_t hitec_prog_restore_backup_unit(hitec_prog_t *p)
{
    hitec_prog_msg_id_t m;

    /* The confirmation holds for this call only: the write's gate reads it
     * once, at its start. */
    p->bk_unit_ok = true;
    m = hitec_prog_restore_backup(p);
    p->bk_unit_ok = false;
    return m;
}
