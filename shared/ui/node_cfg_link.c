/**
 * @file
 * @brief A node's own settings over the link, on the panel
 *        (node_cfg_link.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "node_cfg_link.h"

#include <string.h>

_Static_assert(RCNODE_CONFIG_LEN <= BLEU_EVT_DATA, "CONFIG is one event");
_Static_assert(RCNODE_CTL_MAX <= BLEU_WRITE_MAX, "NAME is one write");

/** @brief The write's kinds the state follows. */
enum { WR_NONE = 0, WR_ASK, WR_SET };

void ncl_link(ncl_t *n)
{
    if (n == NULL) {
        return;
    }
    const bool lost = n->name_owed || n->i228_owed || (n->wr && n->wr_set);
    const uint32_t refused = n->refused;
    const uint32_t req_no = n->req_no;
    const uint8_t req_why = n->req_why;
    const uint32_t dropped = n->dropped + (lost ? 1u : 0u);
    memset(n, 0, sizeof(*n));
    n->refused = refused;
    n->req_no = req_no;
    n->req_why = req_why;
    n->dropped = dropped;
}

bool ncl_packet(ncl_t *n, const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (n == NULL || data == NULL || len == 0u || data[0] != RCNODE_T_CONFIG) {
        return false;
    }
    rcnode_pkt_t p;
    if (rcnode_decode(data, len, &p) == RCNODE_OK) {
        n->cfg = p.u.config;
        n->cfg_ms = now_ms;
        n->st = NCL_HAVE;
        ++n->taken;
    }
    return true;
}

void ncl_follow(ncl_t *n, const bleu_t *b, uint32_t now_ms)
{
    if (n == NULL || b == NULL) {
        return;
    }
    if (n->wr && b->wr != BLEU_WR_BUSY) {
        n->wr = false;
        if (n->st == NCL_ASKING) {
            if (b->wr == BLEU_WR_OK) {
                n->st = NCL_WAIT;
                n->asked_ms = now_ms;
            } else if (b->wr_status != BLEU_STATUS_LOST) {
                n->st = NCL_NONE;   /* a node built before CONFIG */
            }
        } else if (b->wr != BLEU_WR_OK) {
            if (b->wr_status == BLEU_STATUS_LOST) {
                ++n->dropped;
            } else {
                ++n->refused;
            }
        }
        n->wr_set = false;
    }
    if (n->st == NCL_WAIT && now_ms - n->asked_ms >= NCL_WAIT_MS) {
        /* No CONFIG: ask once more, then count the node as one without. */
        n->st = n->asks < NCL_ASKS ? NCL_UNKNOWN : NCL_NONE;
    }
}

/**
 * @brief Hand a control write to the central.
 * @param b The central.  @param w The write.  @param now_ms The clock.
 * @return true when it took it.
 */
static bool write(bleu_t *b, const rcnode_ctl_t *w, uint32_t now_ms)
{
    uint8_t buf[RCNODE_CTL_MAX];
    const size_t len = rcnode_enc_ctl(w, buf, sizeof(buf));
    return len != 0u && bleu_send(b, buf, len, now_ms);
}

bool ncl_send(ncl_t *n, bleu_t *b, bool ready, bool armed, uint32_t now_ms)
{
    if (n == NULL || b == NULL || !ready || n->wr || !bleu_can_send(b)) {
        return false;
    }
    rcnode_ctl_t w;
    memset(&w, 0, sizeof(w));
    if (n->st == NCL_UNKNOWN) {
        w.op = RCNODE_OP_CONFIG;
        if (!write(b, &w, now_ms)) {
            return false;
        }
        n->st = NCL_ASKING;
        n->wr = true;
        ++n->asks;
        return true;
    }
    if (n->st != NCL_HAVE || armed) {
        return false;
    }
    if (n->name_owed) {
        w.op = RCNODE_OP_NAME;
        memcpy(w.name, n->name, sizeof(w.name));
        n->name_owed = false;   /* a refused encode is not sent again */
    } else if (n->i228_owed) {
        w.op = RCNODE_OP_I228;
        w.i228 = n->i228;
        n->i228_owed = false;
    } else {
        return false;
    }
    if (!write(b, &w, now_ms)) {
        return false;
    }
    n->wr = true;
    n->wr_set = true;
    return true;
}

/**
 * @brief Whether a request may queue now.
 * @param n The state.  @param armed The bench is ARMED or an arm waits.
 * @return NCL_REQ_OK, or the refusal.
 */
static ncl_req_t may(const ncl_t *n, bool armed)
{
    if (armed) {
        return NCL_REQ_ARMED;
    }
    return ncl_have(n) ? NCL_REQ_OK : NCL_REQ_UNKNOWN;
}

/**
 * @brief A request's answer, a refusal counted.
 * @param n The state.  @param r The answer.
 * @return @p r.
 */
static ncl_req_t answer(ncl_t *n, ncl_req_t r)
{
    if (r != NCL_REQ_OK) {
        ++n->req_no;
        n->req_why = (uint8_t)r;
    }
    return r;
}

ncl_req_t ncl_set_name(ncl_t *n, const char *name, bool armed)
{
    if (n == NULL) {
        return NCL_REQ_UNKNOWN;
    }
    const ncl_req_t r = may(n, armed);
    if (r != NCL_REQ_OK) {
        return answer(n, r);
    }
    if (name == NULL || !rcnode_uname_ok(name)) {  /* 16 characters at most */
        return answer(n, NCL_REQ_BAD);
    }
    memset(n->name, 0, sizeof(n->name));
    memcpy(n->name, name, strlen(name));
    n->name_owed = true;
    return NCL_REQ_OK;
}

ncl_req_t ncl_set_i228(ncl_t *n, uint8_t a, bool armed)
{
    if (n == NULL) {
        return NCL_REQ_UNKNOWN;
    }
    const ncl_req_t r = may(n, armed);
    if (r != NCL_REQ_OK) {
        return answer(n, r);
    }
    if (!rcnode_i228_ok(a) || (n->cfg.flags & RCNODE_CF_SHUNT) == 0u) {
        return answer(n, NCL_REQ_BAD);
    }
    n->i228 = a;
    n->i228_owed = true;
    return NCL_REQ_OK;
}

bool ncl_have(const ncl_t *n)
{
    return n != NULL && n->st == NCL_HAVE;
}

bool ncl_pending(const ncl_t *n)
{
    return n != NULL && (n->st == NCL_UNKNOWN || n->st == NCL_ASKING
                         || n->st == NCL_WAIT);
}

uint16_t ncl_node_age_s(const ncl_t *n, uint32_t now_ms)
{
    if (n == NULL || n->cfg.shunt_age_s == RCNODE_AGE_NONE) {
        return RCNODE_AGE_NONE;
    }
    const uint32_t age = (uint32_t)n->cfg.shunt_age_s
                         + (now_ms - n->cfg_ms) / 1000u;
    return (uint16_t)(age < RCNODE_AGE_NONE ? age : RCNODE_AGE_NONE - 1u);
}
