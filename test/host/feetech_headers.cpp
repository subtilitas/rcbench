/*
 * The public headers of protocols/feetech, compiled as C++17.  The build
 * fails when one of them is not valid C++, and the link fails when one
 * loses its extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feetech_bus.h"
#include "feetech_pio.h"
#include "feetech_reg.h"
#include "feetech_wire.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int feetech_headers_link(void)
{
    feetech_frame_t frame;
    feetech_bus_t bus;
    feetech_pio_clock_t clock;

    (void)feetech_ping(&frame, 1);
    (void)feetech_bus_init(&bus, nullptr, 1000000u);
    (void)feetech_pio_clock(150000000u, 1000000u, &clock);
    return static_cast<int>(feetech_baud(FEETECH_BAUD_DEFAULT) / 1000000u)
           + static_cast<int>(frame.n);
}

int main()
{
    return feetech_headers_link() == 7 ? 0 : 1;
}
