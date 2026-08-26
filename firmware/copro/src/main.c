/*
 * The coprocessor application.
 *
 * At this commit it is the wiring proof: it shows that shared/link compiles
 * and links under the pico-sdk from the same directory the panel and the host
 * suite build it from, and that the PIO hardware layer is reachable.  The
 * poll loop, the register pages, the failsafe and the PIO programs replace it
 * as they are written.
 *
 * Note what is *not* here and will not be: this processor never speaks
 * unsolicited.  Everything it does is an answer.
 */
#include <stdio.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "link_crc.h"

int main(void)
{
    stdio_init_all();

    const bool crc_ok =
        link_crc(LINK_CRC_INIT, "123456789", 9) == LINK_CRC_CHECK;

    for (;;) {
        printf("rcbench-copro: link crc check %s, %u PIO blocks\n",
               crc_ok ? "passes" : "FAILS", (unsigned)NUM_PIOS);
        sleep_ms(1000);
    }
}
