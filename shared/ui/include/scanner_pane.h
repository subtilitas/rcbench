/**
 * @file
 * @brief SETUP > SENSORS > SCANNER: the QR scanner's rows, the TEST window
 *        and the commands to the panel's scanner task.
 *
 * The entry lists, under the settings Scanner and READ MODE:
 *
 * - Without a scanner: every feature works by typing the servo id on the
 *   SERVO ID row; shown whether the scanner is ON or OFF;
 * - State: the sequence's state and the rate, or the zone the module
 *   refused; NOT CONNECTED is a state, not an error: no alert, no fault;
 * - Last scan: the last line, cut to SCANNER_LAST_CELLS, and its age in s;
 * - Taken as: what the line did (SERVO, NOT A SERVO ID, CODE TOO LONG,
 *   REFUSED: RUN), or the TEST verdict;
 * - Counts: lines, noise, partial, long, bad CRC;
 * - Active servo: the active id; its choice list offers NONE, which clears
 *   it outside a run and is refused during one;
 * - Test: SCANNER_TEST_MS (30s) in which a scanned line shows here and
 *   leaves the active servo as it is;
 * - Check again: gm861_recheck(), which also ends the hold on a refused
 *   zone;
 * - Show setup code and Factory reset: open SETUP CODE (scanner_code.h).
 *
 * The rows below the settings show while the Scanner row is ON; OFF shows
 * the Scanner row and the line on typing.
 *
 * The panel hands the scanner's view in each frame
 * (scanner_pane_set_view()) and takes the commands (scanner_pane_take()).
 * scan_flow.c hands every line it takes to scanner_pane_line(), which
 * keeps it for Last scan, and asks scanner_pane_testing() before it runs
 * a line: during TEST the line goes to the page alone.
 *
 * State of the frame task.  Pure C.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "servo_active.h"
#include "ui_setlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief TEST's window, ms. */
#define SCANNER_TEST_MS 30000u
/** @brief Cells of the last line Last scan shows. */
#define SCANNER_LAST_CELLS 13u
/** @brief Custom rows the entry lists at most. */
#define SCANNER_ROWS_MAX 10

/** @brief The scanner as the panel's task last published it. */
typedef struct {
    bool     on;        /**< SETUP's Scanner row is ON */
    uint8_t  state;     /**< gm861_state_t */
    uint32_t baud;      /**< the rate UART1 is open at, 0 closed */
    uint16_t bad_zone;  /**< the zone refused, or GM861_NO_ZONE */
    uint32_t lines;     /**< lines handed out */
    uint32_t noise;     /**< lines with a byte outside 0x20 to 0x7E */
    uint32_t partial;   /**< lines and frames cut by the gap */
    uint32_t longs;     /**< lines past 64 bytes */
    uint32_t bad_crc;   /**< answer frames refused */
} scanner_view_t;

/** @brief A command for the panel's scanner task. */
typedef enum {
    SCANNER_CMD_RECHECK = 1,  /**< gm861_recheck() */
    SCANNER_CMD_RESET,        /**< gm861_factory_reset() */
} scanner_cmd_t;

/** @brief How the last line was taken, for Taken as. */
typedef enum {
    SCANNER_TAKEN_NONE = 0,   /**< no line yet */
    SCANNER_TAKEN_SERVO,      /**< an id: set, again, or the run's own */
    SCANNER_TAKEN_NOT_ID,     /**< no servo id */
    SCANNER_TAKEN_LONG,       /**< past 64 bytes */
    SCANNER_TAKEN_RUN,        /**< another id during a run: refused */
    SCANNER_TAKEN_TEST_OK,    /**< TEST: an id the flow would take */
    SCANNER_TAKEN_TEST_NOT_ID,/**< TEST: no servo id */
    SCANNER_TAKEN_TEST_LONG,  /**< TEST: past 64 bytes */
    SCANNER_TAKEN_TEST_RUN,   /**< TEST: an id, refused as a run is active */
} scanner_taken_t;

/**
 * @brief The panel's view, once a frame, and the frame's clock.  A change
 *        the page shows redraws it.
 * @param v      The view; NULL reads as the scanner OFF.
 * @param now_ms The millisecond clock.
 */
void scanner_pane_set_view(const scanner_view_t *v, uint32_t now_ms);

/**
 * @brief The view last handed in.
 * @return It.
 */
const scanner_view_t *scanner_pane_view(void);

/**
 * @brief The oldest command for the panel.
 * @param out Receives it.
 * @return false when none waits.
 */
bool scanner_pane_take(scanner_cmd_t *out);

/**
 * @brief Ask for a command; the queue of 4 drops one past it.
 * @param cmd The command.
 */
void scanner_pane_ask(scanner_cmd_t cmd);

/**
 * @brief Whether TEST's window is open at @p now_ms.  Leaving the page
 *        ends TEST at once: another screen on top, another SENSORS entry,
 *        or the Scanner row OFF.  Coming back does not open it again.
 * @param now_ms The millisecond clock.
 * @return true from TEST's tap until SCANNER_TEST_MS after it, while the
 *         page shows.
 */
bool scanner_pane_testing(uint32_t now_ms);

/**
 * @brief A line taken during TEST: kept with its verdict; the active
 *        servo stays.
 * @param line       The line.
 * @param too_long   It was cut.
 * @param run_active A run is active.
 * @param now_ms     The millisecond clock.
 */
void scanner_pane_test_line(const char *line, bool too_long, bool run_active,
                            uint32_t now_ms);

/**
 * @brief A line the scan flow ran, and what it did.
 * @param line   The line.
 * @param how    servo_scan_decide()'s result.
 * @param now_ms The millisecond clock.
 */
void scanner_pane_line(const char *line, servo_scan_t how, uint32_t now_ms);

/**
 * @brief The last line kept and how it was taken.
 * @param at_ms Receives when it came; may be NULL.
 * @return The line, "" for none; scanner_pane_taken() says how.
 */
const char *scanner_pane_last(uint32_t *at_ms);

/**
 * @brief How the last line was taken.
 * @return SCANNER_TAKEN_NONE before the first.
 */
scanner_taken_t scanner_pane_taken(void);

/**
 * @brief The entry's rows after its settings.
 * @param out Receives the rows; NULL counts them.
 * @param max Room in @p out.
 * @return The rows: SCANNER_ROWS_MAX while ON, 1 while OFF.
 */
int scanner_pane_rows(const ui_setlist_custom_t **out, int max);

/**
 * @brief The state word of the entry in the left list, and its colour:
 *        OFF dim, READY green, every other state dim; never amber.
 * @param buf Receives the words.
 * @param n   Its size.
 * @param ink Receives the colour.
 * @return @p buf.
 */
const char *scanner_pane_state(char *buf, size_t n, gfx_color_t *ink);

/**
 * @brief A state's name.
 * @param state gm861_state_t.
 * @return The name in the language showing.
 */
const char *scanner_pane_state_name(uint8_t state);

/**
 * @brief Forget the view, the last line, TEST and the commands: SETUP's
 *        start and the host suite's.
 */
void scanner_pane_reset(void);

#ifdef __cplusplus
}
#endif
