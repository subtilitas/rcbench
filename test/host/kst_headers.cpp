/*
 * The public headers of protocols/kst, compiled as C++.  The build fails
 * when one of them is not valid C++, and the link fails when one loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_limits.h"
#include "kst_plan.h"
#include "kst_reg.h"
#include "kst_session.h"
#include "kst_wire.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int kst_headers_link(void)
{
    kst_frame_t frame;
    kst_image_t img = kst_image_t();
    kst_plan_t plan;
    kst_session_t ses;

    kst_frame_sync(&frame);
    (void)kst_fingerprint(&img, nullptr);
    (void)kst_limits_image(&img);
    (void)kst_plan_release_pairing(&img, &plan);
    (void)kst_session_init(&ses, nullptr);
    return static_cast<int>(frame.n_half);
}

int main()
{
    return kst_headers_link() == static_cast<int>(KST_FRAME_MAX_HALF_CELLS)
               ? 0 : 1;
}
