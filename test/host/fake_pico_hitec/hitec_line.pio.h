/*
 * What pioasm makes of protocols/hitec/rp2350/hitec_line.pio for
 * hitec_line.c, for test_hitec_line.c: the program and its default
 * configuration.  test_hitec_pio runs the program itself.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FAKE_HITEC_LINE_PIO_H
#define FAKE_HITEC_LINE_PIO_H

#include "fake_pico_hitec.h"

extern const pio_program_t hitec_line_program;
pio_sm_config hitec_line_program_get_default_config(uint offset);

#endif /* FAKE_HITEC_LINE_PIO_H */
