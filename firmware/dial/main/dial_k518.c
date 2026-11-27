/**
 * @file
 * @brief The Guition JC3636K518C's power calls (dial.h), which do nothing
 *        yet.
 *
 * The battery reading, the LOW and EMPTY flags and the soft OFF are a
 * later slice; GPIO1 is not read.  Until then the Dial's STATUS names the
 * battery unknown.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "dial.h"
#include "driver/usb_serial_jtag.h"

/* --- power ----------------------------------------------------------- */

void dial_power_hold(void)
{
    /* No latch: the slide switch holds the power. */
}

void dial_power_off(void)
{
    /* No latch: the slide switch cuts the power. */
}

void dial_supply_read(dial_supply_t *out)
{
    memset(out, 0, sizeof *out);
    out->usb = usb_serial_jtag_is_connected();
}

void dial_supply_inject(uint16_t mv)
{
    (void)mv;
}
