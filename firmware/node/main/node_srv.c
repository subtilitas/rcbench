/**
 * @file node_srv.c
 * @brief The servo role on the ES3C28P (CONFIG_RCNODE_ROLE_SERVO): its
 *        node_role_t, its part of the console line and the console's `srv`
 *        command.
 *
 * The role starts the safety pins (node_srv_hb.c) and the link to the
 * Core2350B (node_srv_link.c).  The board is always on: no sleep, no timer
 * wake.  Its STATUS sets RCNODE_ST_SENSOR while the IOmcu of board 3
 * answers.
 *
 * Console:
 *
 *     srv              the link, the panel, the switch to ARMED, the counts
 *     srv ka on|off    a KEEPALIVE every 50ms and a link from the console,
 *                      as from a connected panel; off is a disconnect.  It
 *                      arms nothing and switches nothing on
 *     srv stop         STOP, as the panel's
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"
#include "node_srv.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

/** @brief The band's word for the panel.  @param p The panel state.
 *  @return The word. */
static const char *panel_word(node_srv_panel_t p)
{
    switch (p) {
    case NODE_SRV_LINKED:     return "LINKED";
    case NODE_SRV_PANEL_LOST: return "PANEL LOST";
    case NODE_SRV_STOPPED:    return "STOP";
    default:                  return "STANDALONE";
    }
}

/** @brief The word for the switch to ARMED.  @param a The state.
 *  @return The word. */
static const char *arm_word(node_srv_arm_t a)
{
    switch (a) {
    case NODE_SRV_ARM_WAIT:  return "ARM WAIT";
    case NODE_SRV_ARM_WRITE: return "ARM WRITE";
    case NODE_SRV_ARMED:     return "ARMED";
    default:                 return "SAFE";
    }
}

/** @brief Start the pins and the link. */
static void srv_start(void)
{
    node_srv_hb_init();
    node_srv_link_start();
}

/** @brief No control op of the power or balance roles is the servo's.
 *  @param c The write. */
static void srv_control(const rcnode_ctl_t *c)
{
    ESP_LOGW(NODE_TAG, "srv: control op 0x%02X is not the servo role's",
             (unsigned)c->op);
}

/** @brief The role's status fields.  @param s The status. */
static void srv_status(rcnode_status_t *s)
{
    node_srv_view_t v;
    node_srv_link_view(&v);
    if (v.linked) {
        s->flags |= RCNODE_ST_SENSOR;
    }
    s->errors = (uint16_t)v.unanswered;
    s->resets = (uint16_t)v.run_pulses;
}

/** @brief No fields of the servo role in INFO.  @param i The info. */
static void srv_info(rcnode_info_t *i)
{
    (void)i;
}

/**
 * @brief The role's part of the console line.
 * @param buf The buffer.
 * @param n   Its size.
 */
static void srv_line(char *buf, size_t n)
{
    node_srv_view_t v;
    node_srv_link_view(&v);
    snprintf(buf, n, "iomcu %s, panel %s, %s, beat %s, faults 0x%04X, "
             "answered %lu, unanswered %lu, refused %lu",
             v.pol.iomcu_lost ? "LOST" : v.linked ? "up" : "none",
             panel_word(v.pol.panel), arm_word(v.pol.arm),
             v.beat ? "on" : "off",
             (unsigned)v.status[LINK_ST_FAULTS], (unsigned long)v.exchanges,
             (unsigned long)v.unanswered, (unsigned long)v.refused);
}

/** @brief Never busy: the board is always on.  @return false. */
static bool srv_busy(void)
{
    return false;
}

/** @brief No sleep on this board; the heartbeat low all the same. */
static void srv_sleep(void)
{
    node_srv_hb_set(false);
}

/**
 * @brief No timer wake on this board.
 * @param vbus_uv Receives 0.
 * @return false.
 */
static bool srv_wake(int32_t *vbus_uv)
{
    *vbus_uv = 0;
    return false;
}

const node_role_t node_role = {
    .role    = RCNODE_ROLE_SERVO,
    .word    = "srv",
    .timer_wake = false,
    .start   = srv_start,
    .control = srv_control,
    .status  = srv_status,
    .info    = srv_info,
    .line    = srv_line,
    .busy    = srv_busy,
    .sleep   = srv_sleep,
    .wake    = srv_wake,
};

/**
 * @brief `srv`, `srv ka on|off`, `srv stop`.
 * @param argc The word count.
 * @param argv The words.
 * @return 0, or 1 for a wrong command.
 */
static int cmd_srv(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "ka") == 0
        && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        node_srv_link_console_ka(strcmp(argv[2], "on") == 0);
    } else if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        node_srv_post_stop();
    } else if (argc != 1) {
        printf("srv | srv ka on | srv ka off | srv stop\n");
        return 1;
    }
    node_srv_view_t v;
    node_srv_link_view(&v);
    char line[200];
    srv_line(line, sizeof line);
    printf("%s\n", line);
    printf("identity %u.%u board %u, console ka %s, nacks %lu, preempted %lu, "
           "run pulses %lu, edges %lu\n",
           (unsigned)v.identity[LINK_ID_PROTOCOL_MAJOR],
           (unsigned)v.identity[LINK_ID_PROTOCOL_MINOR],
           (unsigned)v.identity[LINK_ID_HARDWARE],
           node_srv_link_console_ka_on() ? "on" : "off", (unsigned long)v.nacks,
           (unsigned long)v.preempted, (unsigned long)v.run_pulses,
           (unsigned long)v.beats);
    printf("power rail %u fitted 0x%03X on 0x%03X want 0x%03X tripped 0x%04X; "
           "supply flags 0x%04X %umV %umA\n",
           (unsigned)v.power[LINK_PW_RAIL], (unsigned)v.power[LINK_PW_FITTED],
           (unsigned)v.power[LINK_PW_PORT_ON], (unsigned)v.power[LINK_PW_PORT_WANT],
           (unsigned)v.power[LINK_PW_TRIPPED], (unsigned)v.supply[LINK_SP_FLAGS],
           (unsigned)v.supply[LINK_SP_V_MV], (unsigned)v.supply[LINK_SP_I_MA]);
    return 0;
}

void node_srv_console_register(void)
{
    static const esp_console_cmd_t cmd = {
        .command = "srv",
        .help = "The servo node's link: srv | srv ka on | srv ka off | srv stop",
        .func = cmd_srv,
    };
    (void)esp_console_cmd_register(&cmd);
}
