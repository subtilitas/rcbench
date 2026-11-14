/**
 * @file node_console.c
 * @brief The console on USB Serial/JTAG: the `node` task's line every
 *        second, and the commands `status`, `stay`, `sleep` and `off`.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

#include "rcbench_version.h"

/**
 * @brief `status`: the last status in full.
 *
 * @param argc Unused.
 * @param argv Unused.
 * @return 0.
 */
static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rcnode_status_t s;
    node_last_status(&s);
    printf("%s, firmware %s, role %s, protocol %u\n", node_ble_name(),
           RCBENCH_VERSION_STRING, node_role.word, (unsigned)RCNODE_PROTO);
    printf("flags 0x%04X, bat %umV, sensor %u, errors %u, resets %u, "
           "dropped %u, link %s, stay %s\n", (unsigned)s.flags,
           (unsigned)s.vbat, (unsigned)s.sensor, (unsigned)s.errors,
           (unsigned)s.resets, (unsigned)s.dropped,
           node_ble_linked() ? "up" : "none", node_staying() ? "on" : "off");
    return 0;
}

/**
 * @brief `stay on` holds the node awake until `stay off` or a restart.
 *
 * @param argc The argument count.
 * @param argv `stay`, then `on` or `off`.
 * @return 0, 1 for another argument.
 */
static int cmd_stay(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "on") == 0) {
        node_stay(true);
    } else if (argc == 2 && strcmp(argv[1], "off") == 0) {
        node_stay(false);
    } else {
        printf("stay on | stay off\n");
        return 1;
    }
    printf("stay %s\n", node_staying() ? "on" : "off");
    return 0;
}

/**
 * @brief `sleep`: deep sleep with the 10s check, now.
 *
 * @param argc Unused.
 * @param argv Unused.
 * @return 0.
 */
static int cmd_sleep(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    node_sleep_now();
    return 0;
}

/**
 * @brief `off`: deep sleep without a wake source, now.
 *
 * @param argc Unused.
 * @param argv Unused.
 * @return 0.
 */
static int cmd_off(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    node_off_now();
    return 0;
}

void node_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "node> ";
    esp_console_dev_usb_serial_jtag_config_t hw =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if (esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl) != ESP_OK) {
        ESP_LOGE(NODE_TAG, "console: no REPL on USB Serial/JTAG");
        return;
    }
    static const esp_console_cmd_t cmds[] = {
        { .command = "status", .help = "Print the node's status",
          .func = cmd_status },
        { .command = "stay", .help = "Hold the node awake: stay on | stay off",
          .func = cmd_stay },
        { .command = "sleep", .help = "Sleep now; the power node checks every 10s",
          .func = cmd_sleep },
        { .command = "off", .help = "Off now; the RESET key starts the node",
          .func = cmd_off },
    };
    for (size_t k = 0; k < sizeof cmds / sizeof cmds[0]; ++k) {
        (void)esp_console_cmd_register(&cmds[k]);
    }
    (void)esp_console_register_help_command();
    (void)esp_console_start_repl(repl);
}
