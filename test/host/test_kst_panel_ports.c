/*
 * The KST and Chaservo programmer on a programming port other than the
 * first (protocol 4.26): the session runs on the port SERVO TESTER names,
 * the IOmcu hands it that port's pin, and SERVO > PROGRAM's row names the
 * port and its pin and goes PORT IN USE while the HITEC page holds it.
 *
 * Under test: PORT 3 of a 4-port build entered, programmed on port 2 with
 * its GP14 and left; the same port without a pin refused; the row's
 * detail and its PORT IN USE, and its own session holding the port
 * keeping it live.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"

#include "prog_registry.h"

static const char *status(int i)
{
    const char *t = kst_panel_status_line(i);

    return t != NULL ? t : "";
}

/* A 4-port build, port 2 (PORT 3) on @p pin, 0 for none; the tester on
 * PORT 3; the link up. */
static void on_port_3(uint8_t pin)
{
    fresh();
    g_rig.ports = 4u;
    g_rig.pin_of[2] = pin;
    rig_port_fresh();
    g_rig.target = 3u;
    link_up();
    settle();
}

/* The shipped KST row's index; @p n rows. */
static size_t kst_row(const prog_reg_row_t *rows, size_t n)
{
    size_t i = 0;
    while (i < n && rows[i].page_set != &kst_panel_pages) {
        ++i;
    }
    return i;
}

TEST_CASE(a_session_runs_on_the_port_the_tester_drives)
{
    char want[KST_PANEL_LINE];

    on_port_3(14u);
    snprintf(want, sizeof(want), TR(KP_STATE_PWM_AS_IS), 3u);
    CHECK_STR_EQ(status(0), want);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK_EQ(kst_prog_port(M), 2u);
    CHECK_EQ(g_rig.taken_port, 2u);
    CHECK_EQ(g_rig.taken_pin, 14u);
    kst_link_readout_t rd;
    kst_link_readout(&g_rig.link, &rd);
    CHECK_EQ(rd.port_no, 2u);
    snprintf(want, sizeof(want), TR(KP_STATE_PROGRAMMING), 3u);
    CHECK_STR_EQ(status(0), want);
    CHECK_EQ(prog_ps_pin(&kst_panel_pages), 14u);
}

TEST_CASE(a_port_without_a_pin_is_refused)
{
    on_port_3(0u);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_PIN);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
}

TEST_CASE(the_program_row_names_the_port_and_its_use)
{
    size_t n = 0;
    char out[48];
    const uint16_t caps = LINK_CAP_SERVO_PWM;

    on_port_3(14u);
    const prog_reg_row_t *rows = prog_registry(&n);
    const size_t at = kst_row(rows, n);
    CHECK(at < n);
    ui_text_set_language(UI_LANG_EN);
    prog_registry_set_ports(2u, 0u);
    CHECK(!prog_reg_detail(rows, n, at, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT 3 GP14");
    /* A console Hitec session on port 2. */
    prog_registry_set_ports(2u, 0x4u);
    CHECK_EQ(prog_reg_state(rows, n, at, caps), PROG_ROW_PORT_BUSY);
    /* On another port it does not matter. */
    prog_registry_set_ports(2u, 0x2u);
    CHECK_EQ(prog_reg_state(rows, n, at, caps), PROG_ROW_LIVE);
    /* The session's own hold keeps the row live. */
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK(kst_panel_holds_port());
    prog_registry_set_ports(2u, 0x4u);
    CHECK_EQ(prog_reg_state(rows, n, at, caps), PROG_ROW_LIVE);
    prog_registry_set_ports(PROG_REG_NO_PORT, 0u);
}

int main(void)
{
    RUN(a_session_runs_on_the_port_the_tester_drives);
    RUN(a_port_without_a_pin_is_refused);
    RUN(the_program_row_names_the_port_and_its_use);
    return test_summary("kst_panel_ports");
}
