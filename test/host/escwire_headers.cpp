/*
 * The public headers of protocols/escwire, compiled as C++.  The build fails
 * when one of them is not valid C++, and the link fails when one loses its
 * extern "C" guard.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_a.h"
#include "ew_guard.h"
#include "ew_id.h"
#include "ew_map.h"
#include "ew_pio.h"
#include "ew_session.h"
#include "ew_wire.h"

/* A reference to every module's entry point: with C++ linkage on a
 * declaration the link of this file against the C library fails. */
int main()
{
    ew_guard_t guard;
    size_t count = 0;

    ew_guard_init(&guard);
    (void)ew_crc16(nullptr, 0);
    (void)ew_map_table(EW_FW_S, &count);
    (void)ew_xfer_valid(nullptr);
    (void)ew_pio_clock(0u, 0u, nullptr);
    (void)ew_session_step(nullptr);
    uint8_t answer[EW_A_ANSWER_LEN] = {};
    ew_contact_t contact;
    ew_id_contact(answer, &contact);
    return count == 0;
}
