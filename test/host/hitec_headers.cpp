/*
 * The public headers of protocols/hitec, compiled as C++.  The build fails
 * when one of them is not valid C++, and the link fails when one loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_live.h"
#include "hitec_pio.h"
#include "hitec_pre.h"
#include "hitec_reg.h"
#include "hitec_session.h"
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
    hitec_image_t img;
    hitec_value_t v;
    hitec_plan_t plan;
    static hitec_session_t ses;
    hitec_ses_step_t st;
    hitec_live_ends_t ends = { 3351u, 8192u, 13003u };
    hitec_live_cmd_t cmd;
    hitec_live_find_t find;

    hitec_c_frame(HITEC_C_FIRST, 0x00, 0x00, frame);
    hitec_d_read_xfer(0x00, 0x0C, &x);
    (void)hitec_pio_clock(150000000u, HITEC_D, &clock);
    (void)hitec_d_decode(&cap, 0x0C, &r);
    if (hitec_pre_timing(150000000u, &t)) {
        (void)hitec_pre_begin(&run, &t, &x);
        hitec_pre_capture(&run, &t, 0, &cap);
    }
    hitec_image_init(&img, HITEC_D);
    (void)hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34485u);
    (void)hitec_image_forget(&img, 0x54);
    (void)hitec_param_get(&img, HITEC_LAYOUT_D, HITEC_P_D_MODEL, &v);
    (void)hitec_plan(&img, &img, &plan);
    if (hitec_session_init(&ses, HITEC_D, HITEC_SES_CLOCK_MS)
        && hitec_session_enter(&ses, false, 0) == HITEC_SES_BUSY) {
        (void)hitec_session_next(&ses, 0, &st);
        (void)hitec_session_abort(&ses);
        (void)hitec_session_move(&ses, 3000u, true);
    }
    (void)hitec_live_d_command(&ends, 8192u, 3351u, &cmd);
    if (hitec_live_find_start(&find, &ends, HITEC_FIND_LEFT, 1000u, 0u)) {
        hitec_live_find_abort(&find);
    }
    return frame[3];
}

int main()
{
    return hitec_headers_link() == 0x99 ? 0 : 1;
}
