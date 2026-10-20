/*
 * The public headers of protocols/hitec, compiled as C++.  The build fails
 * when one of them is not valid C++, and the link fails when one loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_pio.h"
#include "hitec_pre.h"
#include "hitec_wire.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int hitec_headers_link(void)
{
    uint8_t frame[HITEC_C_FRAME_LEN];
    hitec_xfer_t x;
    hitec_pio_clock_t clock;
    hitec_reply_t r;
    hitec_capture_t cap = hitec_capture_t();
    hitec_pre_timing_t t;
    hitec_pre_run_t run;

    hitec_c_frame(HITEC_C_FIRST, 0x00, 0x00, frame);
    hitec_d_read_xfer(0x00, 0x0C, &x);
    (void)hitec_pio_clock(150000000u, HITEC_D, &clock);
    (void)hitec_d_decode(&cap, 0x0C, &r);
    if (hitec_pre_timing(150000000u, &t)) {
        (void)hitec_pre_begin(&run, &t, &x);
        hitec_pre_capture(&run, &t, 0, &cap);
    }
    return frame[3];
}

int main()
{
    return hitec_headers_link() == 0x99 ? 0 : 1;
}
