/**
 * @file
 * @brief The QR scanner's serial stream split into code lines and command
 *        answer frames (scan_line.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "scan_line.h"

#include <string.h>

#include "gm861.h"

/** @brief Bytes in front of an answer frame's data: head, type, length. */
#define FRAME_HEAD 4u
/** @brief CRC bytes at the end of an answer frame. */
#define FRAME_CRC 2u

/** @brief Back to the start of a line, the open part forgotten. */
static void restart(scan_line_t *s)
{
    s->at = SCAN_AT_START;
    s->poisoned = false;
    s->n = 0u;
    s->want = 0u;
}

void scan_line_init(scan_line_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

scan_out_t scan_line_idle(scan_line_t *s, uint32_t now_ms)
{
    if (s == NULL || !s->seen) {
        return SCAN_OUT_NONE;
    }
    if ((uint32_t)(now_ms - s->last_ms) < SCAN_GAP_MS) {
        return SCAN_OUT_NONE;
    }
    if (s->at != SCAN_AT_START) {
        ++s->counts.partial;
    }
    restart(s);
    s->after_cr = false;
    s->seen = false;
    return SCAN_OUT_NONE;
}

/** @brief A line ended at CR or LF: hand it out, drop it, or count it. */
static scan_out_t line_end(scan_line_t *s)
{
    const size_t n = s->n;
    const bool poisoned = s->poisoned;
    restart(s);
    if (poisoned) {
        ++s->counts.noise;
        return SCAN_OUT_NONE;
    }
    if (n == 0u) {
        return SCAN_OUT_NONE;
    }
    const size_t keep = n > SCAN_LINE_MAX ? SCAN_LINE_MAX : n;
    memcpy(s->text, s->buf, keep);
    s->text[keep] = '\0';
    if (n > SCAN_LINE_MAX) {
        ++s->counts.overlong;
        return SCAN_OUT_LONG;
    }
    ++s->counts.lines;
    return SCAN_OUT_LINE;
}

/** @brief One byte inside an answer frame. */
static scan_out_t frame_byte(scan_line_t *s, uint8_t byte)
{
    s->buf[s->n++] = byte;
    if (s->n == 2u || s->n == 3u) {         /* 02 00 00: head and type */
        if (byte != 0x00u) {
            ++s->counts.bad_crc;
            restart(s);
            s->at = SCAN_AT_SKIP;
        }
        return SCAN_OUT_NONE;
    }
    if (s->n == FRAME_HEAD) {
        if (byte == 0u || byte > SCAN_DATA_MAX) {
            ++s->counts.bad_crc;
            restart(s);
            s->at = SCAN_AT_SKIP;
            return SCAN_OUT_NONE;
        }
        s->want = FRAME_HEAD + byte + FRAME_CRC;
        return SCAN_OUT_NONE;
    }
    if (s->want == 0u || s->n < s->want) {
        return SCAN_OUT_NONE;
    }
    const size_t n = s->n;
    const uint16_t crc = gm861_crc(&s->buf[2], n - 2u - FRAME_CRC);
    const bool good = s->buf[n - 2u] == (uint8_t)(crc >> 8)
                      && s->buf[n - 1u] == (uint8_t)(crc & 0xFFu);
    restart(s);
    if (!good) {
        ++s->counts.bad_crc;
        return SCAN_OUT_NONE;
    }
    memcpy(s->frame, s->buf, n);
    s->frame_len = n;
    ++s->counts.frames;
    return SCAN_OUT_FRAME;
}

scan_out_t scan_line_feed(scan_line_t *s, uint8_t byte, uint32_t now_ms)
{
    if (s == NULL) {
        return SCAN_OUT_NONE;
    }
    (void)scan_line_idle(s, now_ms);
    s->seen = true;
    s->last_ms = now_ms;

    const bool after_cr = s->after_cr;
    s->after_cr = false;
    if (s->at == SCAN_AT_FRAME) {
        return frame_byte(s, byte);
    }
    if (byte == '\r' || byte == '\n') {
        if (s->at == SCAN_AT_SKIP) {
            restart(s);
            return SCAN_OUT_NONE;
        }
        if (byte == '\n' && after_cr) {
            return SCAN_OUT_NONE;           /* the LF of CR LF */
        }
        s->after_cr = byte == '\r';
        return line_end(s);
    }
    if (s->at == SCAN_AT_SKIP) {
        return SCAN_OUT_NONE;
    }
    if (s->at == SCAN_AT_START && (byte == 0x02u || byte == 0x03u)) {
        s->at = SCAN_AT_FRAME;
        s->buf[0] = byte;
        s->n = 1u;
        return SCAN_OUT_NONE;
    }
    s->at = SCAN_AT_LINE;
    if (byte < 0x20u || byte > 0x7Eu) {
        s->poisoned = true;
    }
    if (s->n < SCAN_LINE_MAX) {
        s->buf[s->n] = byte;
    }
    if (s->n <= SCAN_LINE_MAX) {
        ++s->n;                             /* SCAN_LINE_MAX + 1: too long */
    }
    return SCAN_OUT_NONE;
}

const char *scan_line_text(const scan_line_t *s)
{
    return s == NULL ? "" : s->text;
}

const uint8_t *scan_line_frame(const scan_line_t *s, size_t *len)
{
    if (s == NULL) {
        if (len != NULL) {
            *len = 0u;
        }
        return NULL;
    }
    if (len != NULL) {
        *len = s->frame_len;
    }
    return s->frame;
}

scan_counts_t scan_line_counts(const scan_line_t *s)
{
    return s->counts;
}
