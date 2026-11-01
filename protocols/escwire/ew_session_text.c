/**
 * @file ew_session_text.c
 * @brief The session's branch for the text interface; see ew_session.h.
 *
 * `set` changes the ESC's RAM and only `save` its flash, so a failure before a
 * `save` leaves the flash as read.  `show` reads RAM: only a `save` answered
 * with `OK` alone, with a `show` after it equal to the target, closes a write.
 * Until then @c save_open stands, and RAM equal to either page proves nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "ew_session_int.h"
#include "ew_text.h"

/**
 * @brief Send what stands in @c tx at the text rate.
 * @param s       the session
 * @param n       its bytes
 * @param first   the wait for the reply's first byte, us
 */
static void send_line(ew_session_t *s, size_t n, uint32_t first)
{
    const ew_refuse_t r = ew_guard_text(&s->guard, s->tx, n);

    if (r != EW_REF_NONE) {
        ew_ses_refused(s, r);
        return;
    }
    const bool is_save = memcmp(s->tx, "save", 4u) == 0;

    if (is_save || memcmp(s->tx, "set ", 4u) == 0) {
        /* From here on the ESC's RAM may not be the page read. */
        s->dirty = true;
        s->reread = false;
        s->save_ok = false;
    }
    if (is_save) {
        s->writing = true;
        s->save_prior = s->save_open;
        s->save_open = true;
    }
    s->x = (ew_xfer_t){
        .rate = EW_RATE_TEXT,
        .n_tx = (uint16_t)n,
        .max_rx = EW_RX_TEXT_MAX,
        .first_us = first,
        .gap_us = EW_TEXT_GAP_US,
        .tx = s->tx,
        .rx = s->rx_text,
    };
    ew_ses_launch(s);
}

void ew_ses_text_act(ew_session_t *s, uint8_t a)
{
    const char *word = a == A_TEXT_INFO ? "info"
                       : a == A_TEXT_SHOW ? "show" : "save";

    send_line(s, ew_text_word(s->tx, sizeof(s->tx), word),
              a == A_TEXT_SAVE ? EW_TEXT_SAVE_US : EW_TEXT_FIRST_US);
}

/**
 * @brief The first key whose RAM value is not the target's.
 * @param s the session
 * @return its place, EW_E32_KEYS for none
 */
static size_t next_diff(const ew_session_t *s)
{
    size_t i = 0u;

    while (i < EW_E32_KEYS
           && ew_e32_get(s->page, i) == ew_e32_get(s->target, i)) {
        i++;
    }
    return i;
}

bool ew_ses_text_set(ew_session_t *s)
{
    const size_t i = next_diff(s);

    if (i >= EW_E32_KEYS) {
        return false;
    }
    send_line(s, ew_text_set(s->tx, sizeof(s->tx), ew_map_e32[i].key,
                             ew_e32_get(s->target, i)),
              EW_TEXT_FIRST_US);
    return true;
}

/**
 * @brief The ESC answered `info` on an entry: it is identified.
 * @param s the session
 */
static void identified(ew_session_t *s)
{
    memset(&s->contact, 0, sizeof(s->contact));
    s->contact.cpu = EW_CPU_TEXT;
    ew_guard_text_target(&s->guard);
    memset(&s->id, 0, sizeof(s->id));
    s->id.fw = EW_FW_E32;
    s->id.layout = s->info.rev;
    s->id.len = EW_MAP_LEN_E32;
    s->id.writable = ew_text_rev_known(&s->info);
    s->id.why = (uint8_t)(s->id.writable ? EW_WHY_NONE : EW_WHY_TEXT_REVISION);
    memcpy(s->id.name, s->info.target, sizeof(s->id.name));
    s->in_mode = true;
}

/**
 * @brief The reply of `info`.
 * @param s    the session
 * @param body bytes before its last line
 */
static void info_done(ew_session_t *s, size_t body)
{
    if (!ew_text_info(s->rx_text, body, &s->info)) {
        s->last_ans = (uint8_t)EW_ANS_OTHER;
        ew_ses_try_failed(s, A_TEXT_INFO);
        return;
    }
    if (s->op == OP_ENTER) {
        identified(s);
    } else if (!ew_text_rev_known(&s->info) || s->info.rev != s->id.layout) {
        ew_guard_done(&s->guard);
        ew_ses_finish(s, EW_SES_ERR_CHANGED);
        return;
    }
    s->tries = 0u;
    s->pc++;
}

/**
 * @brief The reply of `show`.  One that lacks a key or has others is a failed
 *        try, until a read has no try left or the revision is not known: then
 *        it is what the ESC has, shown and not written.
 * @param s    the session
 * @param body bytes before its last line
 */
static void show_done(ew_session_t *s, size_t body)
{
    uint8_t page[EW_MAP_LEN_E32];
    const bool reading = s->op == OP_READ;
    const bool known = ew_map_writable(EW_FW_E32, s->id.layout);
    const bool whole = ew_text_show(s->rx_text, body, page, NULL);

    if (!whole && known && (!reading || s->tries + 1u < EW_SES_TRIES)) {
        s->last_ans = (uint8_t)EW_ANS_LENGTH;
        ew_ses_try_failed(s, A_TEXT_SHOW);
        return;
    }
    memcpy(s->page, page, sizeof(page));
    if (reading) {
        (void)ew_text_show(s->rx_text, body, page, s->music);
        if (!s->dirty) {
            memcpy(s->orig, page, sizeof(page));
        }
        s->id.writable = known && whole;
        s->id.why = (uint8_t)(!known ? EW_WHY_TEXT_REVISION
                              : !whole ? EW_WHY_SHOW_KEYS : EW_WHY_NONE);
    }
    s->tries = 0u;
    s->pc++;
}

/**
 * @brief The write ends on what the ESC said to a `set` or a `save`.
 * @param s    the session
 * @param slot the key, EW_E32_KEYS for a `save`
 * @param r    the result
 */
static void ended_by_esc(ew_session_t *s, size_t slot, ew_ses_t r)
{
    if (slot < EW_E32_KEYS) {
        s->bad_off = (uint16_t)(slot * 2u);
    }
    ew_guard_done(&s->guard);
    ew_ses_finish(s, r);
}

/**
 * @brief The reply of `set`: the value stored stands in RAM.  One other than
 *        the value sent is a clip, and no `save` follows.
 * @param s    the session
 * @param ans  the reply's judgement
 * @param body bytes before its last line
 */
static void set_done(ew_session_t *s, ew_ans_t ans, size_t body)
{
    const size_t i = next_diff(s);
    int16_t v = 0;

    if (ans == EW_ANS_TEXT_ERROR) {
        ended_by_esc(s, i, EW_SES_ERR_ESC_ERROR);
        return;
    }
    if (ans != EW_ANS_OK
        || !ew_text_set_reply(s->rx_text, body, ew_map_e32[i].key, &v)) {
        s->last_ans = (uint8_t)(ans != EW_ANS_OK ? ans : EW_ANS_OTHER);
        ew_ses_try_failed(s, A_TEXT_SET);
        return;
    }
    s->tries = 0u;
    ew_e32_put(s->page, i, v);
    if (v != ew_e32_get(s->target, i)) {
        s->clipped = v;
        ended_by_esc(s, i, EW_SES_ERR_CLIPPED);
    }
}

void ew_ses_text_done(ew_session_t *s, uint8_t a, const ew_capture_t *c)
{
    size_t body = 0u;
    const ew_ans_t ans = ew_text_reply(c, s->rx_text, &body);

    s->last_ans = (uint8_t)ans;
    if (a == A_TEXT_SET) {
        set_done(s, ans, body);
    } else if (a == A_TEXT_SAVE) {
        if (ans == EW_ANS_OK && body == 0u) {
            s->save_ok = true;
            s->tries = 0u;
            s->pc++;
        } else if (ans == EW_ANS_TEXT_ERROR && body == 0u) {
            /* This one wrote nothing; an earlier one may have. */
            s->save_open = s->save_prior;
            ended_by_esc(s, EW_E32_KEYS, EW_SES_ERR_ESC_ERROR);
        } else {
            ew_ses_try_failed(s, a);
        }
    } else if (ans != EW_ANS_OK) {
        ew_ses_try_failed(s, a);
    } else if (a == A_TEXT_INFO) {
        info_done(s, body);
    } else {
        show_done(s, body);
    }
}
