/*
 * The public headers of protocols/sbus, compiled as C++17.  The build fails
 * when one of them is not valid C++, and the link fails when one loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sbus.h"
#include "sbus2.h"
#include "sbus_out.h"
#include "sbus_pio.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
static int sbus_headers_link()
{
    sbus_frame_t frame = sbus_frame_t();
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_map_t map;
    sbus_pio_clock_t clock;
    sbus_out_t out;

    sbus_encode(&frame, SBUS_FOOTER, bytes);
    sbus2_map_default(&map);
    (void)sbus_pio_clock(150000000u, &clock);
    sbus_out_init(&out, nullptr);
    return static_cast<int>(bytes[0]) + static_cast<int>(map.slot[0].decode);
}

int main()
{
    return sbus_headers_link() == static_cast<int>(SBUS_HEADER)
                                      + static_cast<int>(SBUS2_DEC_VOLT)
               ? 0 : 1;
}
