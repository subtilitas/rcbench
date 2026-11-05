/**
 * @file hc05emu_int.h
 * @brief What the source files of the HC-05 emulator share: the action
 *        queue, the answer lines, the error codes and the commands' entry.
 *        Not for callers of the module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05EMU_INT_H
#define HC05EMU_INT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hc05emu.h"

/** @brief The HC-05 error codes of ERROR:(hh) this module answers, from
 *         the HC-05 datasheet's table. @{ */
#define EMU_E_CMD        0x00u /**< a command it does not take; 0, so an
                                     *   address code of 0 is this one */
#define EMU_E_NAP        0x05u /**< the address's NAP is too long */
#define EMU_E_UAP        0x06u /**< the address's UAP is too long */
#define EMU_E_LAP        0x07u /**< the address's LAP is too long */
#define EMU_E_PIN_EMPTY  0x0Fu /**< the PIN is empty */
#define EMU_E_PIN_LONG   0x10u /**< the PIN is longer than 16 bytes */
#define EMU_E_ROLE       0x11u /**< a role out of range, or AT+INQ as a slave */
#define EMU_E_BAUD       0x12u /**< a baud it does not take */
#define EMU_E_STOP       0x13u /**< a stop bit setting out of range */
#define EMU_E_PARITY     0x14u /**< a parity setting out of range */
#define EMU_E_NOT_INIT   0x16u /**< AT+INQ before AT+INIT */
#define EMU_E_INIT_AGAIN 0x17u /**< AT+INIT once more */
#define EMU_E_INQ_MODE   0x18u /**< an inquiry mode out of range */
#define EMU_E_INQ_TIME   0x19u /**< an inquiry time out of range */
#define EMU_E_NO_ADDR    0x1Au /**< no address given */
/** @} */

/**
 * @brief Queue an action of @p kind, its other fields zero.
 * @param e    The machine.
 * @param kind The kind.
 * @return The action to fill in; NULL when the queue is full, which
 *         counts in hc05emu_t.q_lost.
 */
hc05emu_act_t *hc05emu_q(hc05emu_t *e, hc05emu_act_kind_t kind);

/** @brief Queue the answer line @p text with CR LF.  @param e The machine.
 *         @param text The line, cut to HC05EMU_OUT_MAX - 2 bytes. */
void hc05emu_say(hc05emu_t *e, const char *text);

/** @brief Queue `ERROR:(hh)`, @p code in hex without leading zeros.
 *         @param e The machine. @param code The code. */
void hc05emu_error(hc05emu_t *e, uint8_t code);

/** @brief Queue SAVE and then OK: a setting changed.
 *         @param e The machine. */
void hc05emu_saved(hc05emu_t *e);

/**
 * @brief Write @p addr as NAP:UAP:LAP in upper-case hex without leading
 *        zeros, the form +INQ: prints.
 * @param addr The 6 bytes.
 * @param out  Receives the text, at least 15 bytes.
 */
void hc05emu_addr_text(const uint8_t *addr, char *out);

/**
 * @brief Read an address of 1 to 4, 1 to 2 and 1 to 6 hex digits joined by
 *        commas or by colons, one kind in one address, from *@p p, which is
 *        moved past it.
 * @param p    The text.
 * @param addr Receives the 6 bytes.
 * @return 0 for an address, else the error code: EMU_E_NO_ADDR for none,
 *         EMU_E_NAP, EMU_E_UAP or EMU_E_LAP for a part too long, EMU_E_CMD
 *         for another form.
 */
uint8_t hc05emu_addr_read(const char **p, uint8_t *addr);

/** @brief The index of @p addr in hc05emu_t.found, or -1.
 *         @param e The machine. @param addr The address. @return The index. */
int hc05emu_find(const hc05emu_t *e, const uint8_t *addr);

/** @brief Copy a name the radio gave, cut to HC05EMU_NAME_MAX - 1 bytes,
 *         each byte outside printable ASCII as `?`.  @param dst The buffer,
 *         HC05EMU_NAME_MAX bytes. @param src The name. */
void hc05emu_copy_name(char *dst, const char *src);

/** @brief Whether AT+UART takes @p baud.  @param baud The baud.
 *         @return true for one of the datasheet's 10 bauds. */
bool hc05emu_baud_ok(uint32_t baud);

/**
 * @brief Take one command line, AT mode, no command waiting for the radio.
 * @param e      The machine.
 * @param line   The line without CR LF.
 * @param over   The line was longer than HC05EMU_LINE_MAX - 1 bytes.
 * @param now_ms The clock.
 */
void hc05emu_command(hc05emu_t *e, const char *line, bool over,
                     uint32_t now_ms);

/** @brief Close the data-mode link and end its procedures: STOP when one
 *         runs or the link is up; the link IDLE.  @param e The machine. */
void hc05emu_link_stop(hc05emu_t *e);

/** @brief Ask for the data-mode link as the settings say: CMODE 0 the
 *         bound address, CMODE 1 a search for any slave; nothing as a
 *         slave or outside data mode.  @param e The machine.
 *         @param now_ms The clock. */
void hc05emu_link_start(hc05emu_t *e, uint32_t now_ms);

/** @brief A connect failed or the link was lost: CMODE 1's next device,
 *         else the retry in HC05EMU_RETRY_MS; IDLE outside data mode.
 *         @param e The machine. @param now_ms The clock. */
void hc05emu_link_failed(hc05emu_t *e, uint32_t now_ms);

/** @brief CMODE 1's search ended: its devices by RSSI, strongest first,
 *         and the first of them tried.  @param e The machine.
 *         @param now_ms The clock. */
void hc05emu_search_end(hc05emu_t *e, uint32_t now_ms);

/** @brief Start an inquiry for AT+INQ: OP_INQ, the found list emptied, the
 *         link closed first.  @param e The machine. @param now_ms The clock. */
void hc05emu_inq_start(hc05emu_t *e, uint32_t now_ms);

/** @brief The state word AT+STATE? answers.  @param e The machine.
 *         @return The word. */
const char *hc05emu_state_word(const hc05emu_t *e);

#endif
