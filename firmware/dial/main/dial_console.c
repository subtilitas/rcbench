/**
 * @file
 * @brief The USB console, `dial>`: the Dial's state, its settings and, in a
 *        test build, the hooks that inject input as if the hardware did.
 *
 * Every build: `status`, `sound on|off`, `forget`, `help`.  A test build
 * (RCBENCH_DIAL_HOOKS=ON) adds `turn <n>`, `press`, `hold`, `tap <x> <y>`,
 * `batt <mV>`, `drop` and `ping` (PLAN-DIAL section 6); the released image
 * holds none of their code.  The console runs on the USB Serial/JTAG port
 * of the USB-C socket, 115,200 baud on a terminal (the port ignores the
 * rate), with the log on the same port.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dial.h"

#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/** @brief The log's tag. */
static const char *TAG = "console";

/** @brief `status`: the name, the addresses, the link, the counts and
 *         the internal heap.
 *  @param argc unused @param argv unused @return 0 */
static int cmd_status(int argc, char **argv)
{
    char own[20];
    char panel[20];
    dial_input_stats_t st;
    dial_supply_t sup;
    (void)argc;
    (void)argv;
    dial_ble_addresses(own, panel);
    dial_input_stats(&st);
    dial_supply_read(&sup);
    printf("%s %s, firmware %s, protocol %u%s\n", dial_ble_name(), own,
           esp_app_get_description()->version, (unsigned)DK_PROTOCOL,
           RCBENCH_DIAL_HOOKS ? ", test build" : "");
    printf("panel %s, link %s, rssi %d\n", panel[0] ? panel : "not bound",
           dial_ble_ready() ? "up" : "down", (int)dial_ble_rssi());
    printf("event seq %u counter %d sent %lu unsent %lu, sound %s\n",
           (unsigned)st.seq, (int)st.counter, (unsigned long)st.sent,
           (unsigned long)st.unsent, dial_input_sound_on() ? "on" : "off");
    if (sup.known) {
        printf("battery %umV\n", (unsigned)sup.mv);
    } else {
        printf("battery unknown\n");
    }
    printf("internal heap %u bytes free, %u at the lowest\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return 0;
}

/** @brief `sound on|off`: the click per detent.
 *  @param argc the word count @param argv the words @return 0, or 1 on a
 *  wrong argument */
static int cmd_sound(int argc, char **argv)
{
    if (argc != 2
        || (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0)) {
        printf("sound on|off\n");
        return 1;
    }
    dial_input_sound(strcmp(argv[1], "on") == 0);
    return 0;
}

/** @brief `forget`: as the 10s hold, the bound panel is forgotten.
 *  @param argc unused @param argv unused @return 0 */
static int cmd_forget(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    dial_ble_forget();
    printf("not bound\n");
    return 0;
}

#if RCBENCH_DIAL_HOOKS
/**
 * @brief Read a whole decimal number within a range.
 * @param s   the text
 * @param lo  the lowest value taken
 * @param hi  the highest value taken
 * @param out the number
 * @return false when the text is no number in the range
 */
static bool number(const char *s, long lo, long hi, long *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

/**
 * @brief Queue an injection and say when the queue is full.
 * @param kind dial_inj_kind_t
 * @param arg  TURN: detents
 * @param x    TAP: column
 * @param y    TAP: row
 * @return 0, or 1 when it was refused
 */
static int inject(uint8_t kind, int32_t arg, uint8_t x, uint8_t y)
{
    if (!dial_input_inject(kind, arg, x, y)) {
        printf("busy\n");
        return 1;
    }
    return 0;
}

/** @brief `turn <n>`: n detents, -1,000 to 1,000, one every 20ms.
 *  @param argc the word count @param argv the words @return 0 or 1 */
static int cmd_turn(int argc, char **argv)
{
    long n;
    if (argc != 2 || !number(argv[1], -1000, 1000, &n) || n == 0) {
        printf("turn <n>, n -1000 to 1000 and not 0\n");
        return 1;
    }
    return inject(DIAL_INJ_TURN, (int32_t)n, 0, 0);
}

/** @brief `press`: a PRESS gesture. @param argc unused @param argv unused
 *  @return 0 or 1 */
static int cmd_press(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return inject(DIAL_INJ_PRESS, 0, 0, 0);
}

/** @brief `hold`: a HOLD gesture. @param argc unused @param argv unused
 *  @return 0 or 1 */
static int cmd_hold(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return inject(DIAL_INJ_HOLD, 0, 0, 0);
}

/** @brief `tap <x> <y>`: a TAP gesture at x, y, 0 to 239.
 *  @param argc the word count @param argv the words @return 0 or 1 */
static int cmd_tap(int argc, char **argv)
{
    long x;
    long y;
    if (argc != 3 || !number(argv[1], 0, DK_TOUCH_MAX, &x)
        || !number(argv[2], 0, DK_TOUCH_MAX, &y)) {
        printf("tap <x> <y>, 0 to 239\n");
        return 1;
    }
    return inject(DIAL_INJ_TAP, 0, (uint8_t)x, (uint8_t)y);
}

/** @brief `batt <mV>`: the battery reading STATUS carries, 2,000 to
 *  5,000mV; 0 takes it back to unknown.
 *  @param argc the word count @param argv the words @return 0 or 1 */
static int cmd_batt(int argc, char **argv)
{
    long mv;
    if (argc != 2 || !number(argv[1], 0, DK_BATT_MAX_MV, &mv)
        || (mv != 0 && mv < (long)DK_BATT_MIN_MV)) {
        printf("batt <mV>, 0 or 2000 to 5000\n");
        return 1;
    }
    dial_supply_inject((uint16_t)mv);
    return 0;
}

/** @brief `drop`: end the link; the Dial advertises again.
 *  @param argc unused @param argv unused @return 0 */
static int cmd_drop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    dial_ble_drop();
    return 0;
}

/** @brief `ping`: an EVENT at once, with the echo of the last STATE.
 *  @param argc unused @param argv unused @return 0 or 1 */
static int cmd_ping(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("echo %u\n", (unsigned)dial_ble_echo());
    return inject(DIAL_INJ_PING, 0, 0, 0);
}
#endif

/** @brief The commands. */
static const esp_console_cmd_t k_cmds[] = {
    { .command = "status", .help = "name, addresses, link, counts, heap",
      .func = cmd_status },
    { .command = "sound", .help = "the click per detent", .hint = "on|off",
      .func = cmd_sound },
    { .command = "forget", .help = "forget the bound panel",
      .func = cmd_forget },
#if RCBENCH_DIAL_HOOKS
    { .command = "turn", .help = "inject detents, one every 20ms",
      .hint = "<n>", .func = cmd_turn },
    { .command = "press", .help = "inject a press", .func = cmd_press },
    { .command = "hold", .help = "inject a 1s hold", .func = cmd_hold },
    { .command = "tap", .help = "inject a tap on the glass",
      .hint = "<x> <y>", .func = cmd_tap },
    { .command = "batt", .help = "inject the battery reading",
      .hint = "<mV>", .func = cmd_batt },
    { .command = "drop", .help = "end the link", .func = cmd_drop },
    { .command = "ping", .help = "send an EVENT now", .func = cmd_ping },
#endif
};

void dial_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = "dial>";
    if (esp_console_new_repl_stdio(&cfg, &repl) != ESP_OK) {
        ESP_LOGE(TAG, "the console did not start");
        return;
    }
    esp_console_register_help_command();
    for (size_t i = 0; i < sizeof k_cmds / sizeof k_cmds[0]; ++i) {
        esp_console_cmd_register(&k_cmds[i]);
    }
    esp_console_start_repl(repl);
}
