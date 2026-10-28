/*
 * The public header of protocols/i2c_txq, compiled as C++.  The build
 * fails when it is not valid C++, and the link fails when it loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "i2c_txq.h"

/* A reference to the module's entry points: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int i2c_txq_headers_link(void)
{
    i2c_txq_cfg_t cfg;
    i2c_txq_xfer_t x = i2c_txq_xfer_t();

    i2c_txq_cfg_default(&cfg);
    x.wr_len = 1;
    x.rd_len = 2;
    if (i2c_txq_bus_us(&x, cfg.hz) != 120u
        || i2c_txq_abrt_result(I2C_TXQ_ABRT_ADDR_NOACK) != I2C_TXQ_NACK_ADDR
        || i2c_txq_init(nullptr, nullptr, nullptr, nullptr)) {
        return 1;
    }
    return i2c_txq_res_name(I2C_TXQ_OK)[0] == 'o' ? 0 : 1;
}

int main()
{
    return i2c_txq_headers_link();
}
