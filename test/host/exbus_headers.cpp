/*
 * The public headers of protocols/exbus, compiled as C++.  The build fails
 * when one of them is not valid C++, and the link fails when one loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "exbus_pio.h"
#include "exbus_sched.h"
#include "exbus_telem.h"
#include "exbus_wire.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int exbus_headers_link(void)
{
    uint8_t out[EXBUS_REQUEST_LEN];
    exbus_tm_packet_t packet;
    exbus_cfg_t cfg;
    exbus_pio_clock_t clock;

    const size_t n = exbus_build_request(0, out);
    (void)exbus_tm_parse(out, n, &packet);
    exbus_cfg_default(&cfg);
    (void)exbus_pio_clock(150000000u, cfg.baud, &clock);
    return static_cast<int>(n) + static_cast<int>(clock.div256 >> 8);
}

int main()
{
    /* 8 bytes and a divider of 150. */
    return exbus_headers_link() == 158 ? 0 : 1;
}
