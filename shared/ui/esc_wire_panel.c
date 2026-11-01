/**
 * @file
 * @brief The render loop's ESC programmer.  See esc_wire_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_panel.h"

/** @brief The model. */
static esc_prog_t s_prog;

void esc_wire_panel_gate(esc_prog_gate_t *g)
{
    esc_prog_init(&s_prog);
    esc_prog_gate_set(&s_prog, g);
}

void esc_wire_panel_feed(const esc_prog_feed_t *f)
{
    esc_prog_feed(&s_prog, f);
}

bool esc_wire_panel_next_cmd(esc_prog_cmd_t *out)
{
    return esc_prog_next_cmd(&s_prog, out);
}

bool esc_wire_panel_holds_port(void)
{
    return esc_prog_holds_port(&s_prog);
}

bool esc_wire_panel_idle(void)
{
    return esc_prog_idle(&s_prog);
}
