/**
 * @file sensors_rows.h
 * @brief What SETUP > SENSORS says of its INA228 and INA3221 entries
 *        (sensors_pane.h): each part's state, the marks on its rows, and the
 *        words other screens use for its channels.
 *
 * A row of a part the IOmcu does not read is marked as SETUP marks a value
 * the IOmcu does not hold: its edge and value in the warning colour and a
 * line in place of its help (sensors_rows_why()).  That is a part the
 * SENSE_MAP page refused, one with every channel off, one on the INA228's
 * address, and on an IOmcu older than 4.25 every fitted part but the one
 * SENSE reads.  A LIMIT row is marked as well while the IOmcu does not hold
 * its limit: owed, refused, or any limit on an IOmcu older than 4.27.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "sense_limit_link.h"
#include "sense_map_link.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The parts' states, sense_map_link_status()'s word: 4 bits a part, part 0
 * lowest.  A change repaints SETUP.  0 at start: every part
 * SENS_PART_UNKNOWN.
 */
void sensors_rows_set_status(uint16_t word);

/** Part @p a's state as last set. */
sens_part_t sensors_rows_status(int a);

/**
 * @brief Sets the LIMIT rows' marks.  A change repaints SETUP.
 * @param word sense_limit_link_unheld()'s 12 bits in bits 0 to 11, and
 *             sense_limit_link_status() in bits 12 to 15.
 */
void sensors_rows_set_limits(uint16_t word);

/**
 * @brief Sets the INA228's state.  A change repaints SETUP.
 * @param st sense_map_link_i228()'s word; SENS_PART_UNKNOWN at start.
 */
void sensors_rows_set_i228(sens_part_t st);

/**
 * @brief The INA228's state as last set.
 * @return The state.
 */
sens_part_t sensors_rows_i228(void);

/**
 * @brief Part @p a's name, "INA3221 0x4n".
 * @param a   The part, 0 to 3.
 * @param buf Takes the name.
 * @param n   The size of @p buf, bytes.
 * @return @p buf; "" with no buffer.
 */
const char *sensors_rows_title(int a, char *buf, size_t n);

/** Part @p a's state in words, and the colour it is drawn in. */
const char *sensors_rows_state(int a);
gfx_color_t sensors_rows_state_color(int a);

/**
 * @brief A state in words, in the language showing.
 * @param st The state; one out of range reads as SENS_PART_UNKNOWN.
 * @return The words, at most 25 cells.
 */
const char *sensors_rows_word(sens_part_t st);

/**
 * @brief The colour a state is drawn in.
 * @param st The state.
 * @return UI_OK online, dim while it is not known yet, else UI_WARN.
 */
gfx_color_t sensors_rows_word_color(sens_part_t st);

/**
 * @brief Whether the row of @p id is marked.
 * @param id a setting id.
 * @return true for a SENSORS row of a part the IOmcu does not read, and
 *         for a LIMIT row the IOmcu does not hold
 *         (sensors_rows_set_limits()).
 */
bool sensors_rows_unheld(setting_id_t id);

/** The line a marked row shows in place of its help. */
const char *sensors_rows_why(setting_id_t id);

/**
 * @brief The band's line for an INA228 frame the IOmcu refused: the shunt and
 *        maximum past 163.84mV, with the highest maximum for that shunt; any
 *        other refusal in general words.
 * @param buf  where the line goes.
 * @param n    the size of @p buf, bytes.
 * @param uohm the shunt asked, uOhm.
 * @param da   the maximum asked, 0.1A.
 * @return @p buf; "" for no buffer.
 */
const char *sensors_rows_i228_alert(char *buf, size_t n, unsigned uohm,
                                    unsigned da);

/**
 * @brief The help line of INA228 shunt and INA228 max current: the highest
 *        value that works with the other row's value as it stands, so the row
 *        says what the IOmcu takes before anything is written.
 * @param id the row's setting.
 * @return the line, valid until the next call; "" for any other row.
 */
const char *sensors_rows_i228_line(setting_id_t id);

/**
 * @brief Whether the INA228's shunt and maximum as set are a pair the IOmcu
 *        refuses: more than 163.84mV across the shunt.
 * @return true when the IOmcu refuses them.
 */
bool sensors_rows_i228_over(void);

/**
 * @brief Builds the band's line for a SENSE_LIMIT alert, in the panel's
 *        language.
 * @param buf   where the line goes.
 * @param n     the size of @p buf, bytes; UI_ALERT_MAX holds every line.
 * @param ev    the alert, from sense_limit_link_event().
 * @param ch    for EV_OVER the channel, 0 to 11.
 * @param ma    for EV_OVER the limit it passed, mA, shown in A to 0.01A.
 * @param watch for EV_OVER the channel's WATCHES option, said in brackets;
 *              none for SENS_W_NONE.
 * @return @p buf; "" for EV_NONE, a channel past 11, or no buffer.
 */
const char *sensors_rows_alert(char *buf, size_t n, sense_limit_link_ev_t ev,
                               unsigned ch, uint16_t ma, int watch);

/**
 * @brief Builds the console line of a channel over its limit, in English:
 *        the part and channel, what it watches, the highest current of its
 *        windows the panel read since the last SENSE_LIMIT read, and the
 *        limit.  For example "INA3221 0x40 CH1 (PORT 1) over its limit of
 *        0.010A: highest window read 0.153A".
 * @param buf     where the line goes.
 * @param n       the size of @p buf, bytes; 96 holds every line.
 * @param ch      the channel, 0 to 11.
 * @param watch   the channel's WATCHES option, said in brackets; none for
 *                SENS_W_NONE.
 * @param have    a window of @p ch was read: @p peak_ma holds it; else the
 *                line says "no window read".
 * @param peak_ma the highest current, mA, signed, shown in A to 0.001A.
 * @param limit_ma the limit the page held, mA, shown in A to 0.001A.
 * @return @p buf; "" for a channel past 11 or no buffer.
 */
const char *sensors_rows_trip_line(char *buf, size_t n, unsigned ch,
                                   int watch, bool have, int16_t peak_ma,
                                   uint16_t limit_ma);

/**
 * @brief Channel @p c as the screens name it.
 *
 * "0x41 CH2"; a channel that watches a board's rail by the rail's name,
 * "RAIL B" (the rbmod carrier's INA3221), in the language showing.
 *
 * @param c   the channel, 0 to 11.
 * @param buf where the name goes.
 * @param n   the size of @p buf, bytes.
 * @return @p buf; "" for no channel or no buffer.
 */
const char *sensors_rows_channel(int c, char *buf, size_t n);

/** The same over SERVO's plot and on the TEST card: "INA 0x41 CH2", or
 *  "INA3221" with none; into @p buf. */
const char *sensors_rows_short(char *buf, size_t n);

/** The servo rail's INA3221 meter as the reports and the LIMIT RUN line
 *  name it: the channel watching the tester's target, "INA3221 0x41 CH2",
 *  or "INA3221" with none; into @p buf. */
const char *sensors_rows_meter(char *buf, size_t n);

/** The lowest channel with a shunt on a fitted part whose WATCH is the
 *  page code @p code (LINK_SM_WATCH_*), 0 to 11; -1 for none. */
int sensors_rows_watcher(uint16_t code);

/**
 * @brief Programming port @p port's WATCHED BY label: "INA3221 0x41 CH2"
 *        for the channel watching PORT n, else "INA3221 PROG 5V" for the
 *        one watching the ports' 5V (the rbmod carrier's CH3).
 *
 * @param port the port, 0 to 3.
 * @param buf  where the label goes.
 * @param n    the size of @p buf, bytes.
 * @return @p buf; "" while no channel watches the port, for a port past 3
 *         and for no buffer.
 */
const char *sensors_rows_port_watch(unsigned port, char *buf, size_t n);

/** @brief Each programming port's WATCHED BY line on OUTPUTS, from the
 *         settings as they stand (outputs_screen_set_port_watch(),
 *         sensors_rows_port_watch()). */
void sensors_rows_publish_watch(void);

#ifdef __cplusplus
}
#endif
