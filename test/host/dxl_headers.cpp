/*
 * The public headers of protocols/dynamixel, compiled as C++17.  The build
 * fails when one of them is not valid C++, and the link fails when one
 * loses its extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxl_bus.h"
#include "dxl_packet.h"
#include "dxl_pio.h"
#include "dxl_table.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int dxl_headers_link(void)
{
    uint8_t buf[DXL_WIRE_MAX];
    dxl_packet_t pkt;
    dxl_bus_t bus;
    dxl_pio_div_t div;
    const dxl_driver_t drv = dxl_driver_t();

    const size_t n = dxl_ping(1, buf, sizeof(buf));
    (void)dxl_parse(buf, n, &pkt, nullptr);
    dxl_bus_init(&bus, &drv);
    (void)dxl_pio_divider(150000000u, 57600u, &div);
    const dxl_model_t *model = dxl_model_find(1060);
    return model != nullptr && pkt.id == 1u
               && dxl_write_class(116, 4) == DXL_WR_RAM
               ? static_cast<int>(n) : -1;
}

int main()
{
    return dxl_headers_link() == 10 ? 0 : 1;
}
