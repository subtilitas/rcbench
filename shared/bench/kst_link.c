/*
 * The panel's half of the KST link page.  See kst_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_link.h"

#include <stddef.h>
#include <string.h>

/* The exchange in flight. */
enum {
    X_NONE = 0,
    X_STATUS,        /* read 0 to 3 */
    X_STAGE,         /* write a window of 4 to 24 */
    X_SUPPLY,        /* write 26 and 27 */
    X_LEAVE_MS,      /* write 28 */
    X_CMD,           /* write 0 to 3 */
    X_RESULTS,       /* read 4 to 15 */
    X_VIEW_BACKUP,   /* write VIEW 1 */
    X_BACKUP,        /* read 16 to 31 */
    X_VIEW_IMAGE,    /* write VIEW 0 */
    X_IMAGE,         /* read 16 to 31 */
};

/* The staged registers go out in windows of 4: 5 whole ones and
 * START_CRC. */
#define STAGE_WINDOWS 6u
#define RESULTS_N ((unsigned)LINK_KS_IMAGE - (unsigned)LINK_KS_FAIL)
#define RES(reg) ((unsigned)(reg) - (unsigned)LINK_KS_FAIL)

_Static_assert((STAGE_WINDOWS - 1u) * 4u + 1u == LINK_KS_W_STAGED,
               "stage windows");
_Static_assert(KST_REG_COUNT == 2u * LINK_KS_IMAGE_REGS, "image registers");

static void image_to_regs(const kst_image_t *img, uint16_t *regs)
{
    for (size_t i = 0; i < LINK_KS_IMAGE_REGS; ++i) {
        regs[i] = (uint16_t)(img->r[2u * i]
                             | ((uint16_t)img->r[2u * i + 1u] << 8));
    }
}

static void image_from_regs(const uint16_t *regs, kst_image_t *img)
{
    for (size_t i = 0; i < LINK_KS_IMAGE_REGS; ++i) {
        img->r[2u * i] = (uint8_t)(regs[i] & 0xFFu);
        img->r[2u * i + 1u] = (uint8_t)(regs[i] >> 8);
    }
}

static bool op_writes(link_kst_op_t op)
{
    return op == LINK_KST_OP_WRITE || op == LINK_KST_OP_RESTORE
           || op == LINK_KST_OP_RELEASE;
}

static bool op_staged(link_kst_op_t op)
{
    return op_writes(op) || op == LINK_KST_OP_VERIFY;
}

/* The operation on its way ends here, as @p kind. */
static void finish(kst_link_t *k, kst_link_outcome_kind_t kind, uint8_t nack)
{
    if (k->active) {
        kst_link_outcome_t *o = &k->outcome;

        memset(o, 0, sizeof(*o));
        o->kind = kind;
        o->op = (link_kst_op_t)k->op;
        o->id = k->id;
        o->nack = nack;
        o->taken = k->taken;
        o->fail_reg = 0xFFu;
        if (kind == KST_LINK_OUT_REFUSED) {
            o->refusal =
                (link_kst_refusal_t)(k->status[LINK_KS_RESULT] >> 8);
        } else if (kind == KST_LINK_OUT_DONE) {
            const uint16_t *r = k->results;

            o->result = (uint8_t)(k->end_status[LINK_KS_RESULT] & 0xFFu);
            o->frames = LINK_KS_FRAMES_OF(k->end_status[LINK_KS_OP]);
            o->fail_reg = (uint8_t)(r[RES(LINK_KS_FAIL)] & 0xFFu);
            o->steps_done = (uint8_t)(r[RES(LINK_KS_FAIL)] >> 8);
            o->diff_regs = r[RES(LINK_KS_DIFF_LO)]
                           | ((uint32_t)r[RES(LINK_KS_DIFF_HI)] << 16);
            o->bad_regs = r[RES(LINK_KS_BAD_LO)]
                          | ((uint32_t)r[RES(LINK_KS_BAD_HI)] << 16);
        }
        k->outcome_new = true;
    }
    k->active = false;
    k->verdict = false;
    k->cmd_doubt = false;
    if (k->phase != (uint8_t)KST_LINK_DOWN
        && k->phase != (uint8_t)KST_LINK_NO_PAGE) {
        k->phase = (uint8_t)(k->known ? KST_LINK_READY : KST_LINK_SYNC);
    }
}

void kst_link_init(kst_link_t *k)
{
    if (k != NULL) {
        memset(k, 0, sizeof(*k));
    }
}

void kst_link_lost(kst_link_t *k)
{
    if (k == NULL) {
        return;
    }
    k->phase = (uint8_t)KST_LINK_DOWN;
    finish(k, KST_LINK_OUT_NO_ANSWER, 0u);
    k->up = false;
    k->page = false;
    k->minor = 0;
    k->known = false;
    k->pending = X_NONE;
    k->tries = 0;
    k->polled = false;
    k->poll_now = false;
    k->cmd_live = false;
    k->cmd_abort = false;
    k->abort_wanted = false;
    k->was_held = false;
}

void kst_link_came_up(kst_link_t *k, uint16_t minor)
{
    if (k == NULL) {
        return;
    }
    kst_link_lost(k);
    k->up = true;
    k->page = minor >= KST_LINK_MINOR;
    k->minor = minor;
    k->phase = (uint8_t)(k->page ? KST_LINK_SYNC : KST_LINK_NO_PAGE);
}

bool kst_link_has_page(const kst_link_t *k)
{
    return k != NULL && k->up && k->page;
}

static kst_link_start_t ready(const kst_link_t *k)
{
    if (!k->up) {
        return KST_LINK_START_DOWN;
    }
    if (!k->page) {
        return KST_LINK_START_NO_PAGE;
    }
    if (k->phase != (uint8_t)KST_LINK_READY) {
        return KST_LINK_START_BUSY;
    }
    return KST_LINK_START_OK;
}

kst_link_start_t kst_link_start(kst_link_t *k, const kst_link_request_t *r)
{
    kst_link_start_t s;
    uint16_t cmd;

    if (k == NULL || r == NULL) {
        return KST_LINK_START_ARG;
    }
    s = ready(k);
    if (s != KST_LINK_START_OK) {
        return s;
    }
    if (r->op == LINK_KST_OP_NONE || r->op >= LINK_KST_OP_COUNT
        || r->op == LINK_KST_OP_ABORT || r->port >= LINK_PG_PORTS_MAX
        || (r->confirm && r->op != LINK_KST_OP_RESTORE)
        || (r->op == LINK_KST_OP_LEAVE
            && r->leave_ms > LINK_KST_LEAVE_MS_MAX)) {
        return KST_LINK_START_ARG;
    }
    if (r->op == LINK_KST_OP_LEAVE && k->minor < LINK_MINOR_KST_LEAVE) {
        return KST_LINK_START_NO_OP;
    }
    if (op_writes(r->op) && !r->write_enabled) {
        return KST_LINK_START_NO_ENABLE;
    }
    if ((r->op == LINK_KST_OP_WRITE || r->op == LINK_KST_OP_RELEASE)
        && !k->have_image) {
        return KST_LINK_START_NO_IMAGE;
    }

    memset(k->staged, 0, sizeof(k->staged));
    if (r->op != LINK_KST_OP_RELEASE) {
        image_to_regs(&r->image, &k->staged[0]);
    }
    if (r->op == LINK_KST_OP_WRITE) {
        uint16_t *u = &k->staged[LINK_KS_W_UNLOCK - LINK_KS_W_IMAGE];

        for (unsigned i = 0; i < 4u; ++i) {
            u[i] = (uint16_t)(r->unlock >> (16u * i));
        }
    }
    if (r->op == LINK_KST_OP_WRITE || r->op == LINK_KST_OP_RELEASE) {
        k->staged[LINK_KS_W_START_CRC - LINK_KS_W_IMAGE] =
            link_kst_crc(k->image_regs, LINK_KS_IMAGE_REGS);
    }

    k->supply_due = r->op == LINK_KST_OP_ENTER;
    k->supply[0] = r->supply_mv;
    k->supply[1] = r->supply_ma;
    k->leave_due = r->op == LINK_KST_OP_LEAVE;
    k->leave_ms = r->leave_ms;

    cmd = LINK_KS_W_CMD_OF(r->op, r->port);
    if (r->confirm) {
        cmd |= LINK_KS_W_CMD_CONFIRM;
    }
    k->cmd_proto[LINK_KS_W_CMD] = cmd;
    k->cmd_proto[LINK_KS_W_SEQ] = 0u;
    k->cmd_proto[LINK_KS_W_KEY] = op_writes(r->op) ? LINK_KST_WRITE_KEY : 0u;
    k->cmd_proto[LINK_KS_W_CRC] =
        op_staged(r->op) ? link_kst_crc(k->staged, LINK_KS_W_STAGED) : 0u;

    k->op = (uint8_t)r->op;
    k->id = r->id;
    k->stage_at = op_staged(r->op) ? 0u : (uint8_t)STAGE_WINDOWS;
    k->active = true;
    k->verdict = false;
    k->taken = false;
    k->phase = (uint8_t)KST_LINK_SENDING;
    return KST_LINK_START_OK;
}

kst_link_start_t kst_link_fetch(kst_link_t *k)
{
    kst_link_start_t s;

    if (k == NULL) {
        return KST_LINK_START_ARG;
    }
    s = ready(k);
    if (s == KST_LINK_START_OK) {
        k->phase = (uint8_t)KST_LINK_FETCHING;
        k->fetch = X_RESULTS;
    }
    return s;
}

/* The request's command went and no answer came: it is not sent again.
 * Whether the page took it, the next status read says (on_status()).  That
 * read has KST_LINK_RETRIES tries of its own. */
static void cmd_in_doubt(kst_link_t *k)
{
    k->cmd_live = false;
    k->cmd_doubt = true;
    k->tries = 0;
}

void kst_link_abort(kst_link_t *k)
{
    if (k == NULL || !k->up || !k->page) {
        return;
    }
    if (k->active && k->phase == (uint8_t)KST_LINK_SENDING && !k->verdict
        && !k->cmd_doubt) {
        if (!k->cmd_live || k->cmd_abort) {
            /* The command has not gone: nothing of the request reaches
             * the servo.  A live command here is an ABORT before it. */
            finish(k, KST_LINK_OUT_DROPPED, 0u);
        } else if (k->pending == X_NONE) {
            cmd_in_doubt(k);
        }
        /* On its way: kst_link_done() decides with its answer. */
    }
    k->abort_wanted = true;
}

/* --- the exchanges ------------------------------------------------------------ */

static bool ask(kst_link_t *k, kst_link_xfer_t *x, uint8_t kind, bool write,
                uint8_t off, uint8_t n, const uint16_t *regs)
{
    memset(x, 0, sizeof(*x));
    x->write = write;
    x->page = (uint8_t)LINK_PAGE_KST;
    x->off = off;
    x->n = n;
    if (regs != NULL) {
        memcpy(x->regs, regs, n * sizeof(regs[0]));
    }
    k->pending = kind;
    return true;
}

static bool ask_status(kst_link_t *k, uint32_t now_ms, kst_link_xfer_t *x)
{
    k->polled = true;
    k->poll_now = false;
    k->status_ms = now_ms;
    return ask(k, x, X_STATUS, false, (uint8_t)LINK_KS_STATE,
               (uint8_t)LINK_KS_W_CMD_FRAME, NULL);
}

static bool due(const kst_link_t *k, uint32_t now_ms, uint32_t period)
{
    return !k->polled || (uint32_t)(now_ms - k->status_ms) >= period;
}

static bool ask_cmd(kst_link_t *k, kst_link_xfer_t *x)
{
    return ask(k, x, X_CMD, true, (uint8_t)LINK_KS_W_CMD,
               (uint8_t)LINK_KS_W_CMD_FRAME, k->cmd);
}

static bool ask_fetch(kst_link_t *k, kst_link_xfer_t *x)
{
    uint16_t view;

    switch (k->fetch) {
    case X_RESULTS:
        return ask(k, x, X_RESULTS, false, (uint8_t)LINK_KS_FAIL,
                   (uint8_t)RESULTS_N, NULL);
    case X_VIEW_IMAGE:
    case X_VIEW_BACKUP:
        view = k->fetch == X_VIEW_BACKUP ? 1u : 0u;
        return ask(k, x, k->fetch, true, (uint8_t)LINK_KS_W_VIEW, 1u, &view);
    case X_IMAGE:
    case X_BACKUP:
    default:
        return ask(k, x, k->fetch, false, (uint8_t)LINK_KS_IMAGE,
                   (uint8_t)LINK_KS_IMAGE_REGS, NULL);
    }
}

bool kst_link_next(kst_link_t *k, uint32_t now_ms, kst_link_xfer_t *x)
{
    bool held;

    if (k == NULL || x == NULL || !k->up || !k->page
        || k->pending != X_NONE) {
        return false;
    }
    if (!k->known) {
        return due(k, now_ms, KST_LINK_POLL_MS) && ask_status(k, now_ms, x);
    }
    /* A command without an answer goes again as it was, unless an abort
     * came for it (cmd_doubt). */
    if (k->cmd_live) {
        return ask_cmd(k, x);
    }
    if (k->verdict || k->poll_now || k->cmd_doubt) {
        return ask_status(k, now_ms, x);
    }
    if (k->abort_wanted) {
        k->cmd[LINK_KS_W_CMD] = LINK_KS_W_CMD_OF(
            LINK_KST_OP_ABORT, LINK_KS_PORT_OF(k->status[LINK_KS_STATE]));
        k->cmd[LINK_KS_W_SEQ] = (uint16_t)(k->seq + 1u);
        k->cmd[LINK_KS_W_KEY] = 0u;
        k->cmd[LINK_KS_W_CRC] = 0u;
        k->cmd_live = true;
        k->cmd_abort = true;
        return ask_cmd(k, x);
    }
    if (k->phase == (uint8_t)KST_LINK_SENDING) {
        if (k->stage_at < STAGE_WINDOWS) {
            const unsigned at = 4u * k->stage_at;
            const unsigned n = at + 4u <= LINK_KS_W_STAGED
                                   ? 4u : LINK_KS_W_STAGED - at;

            return ask(k, x, X_STAGE, true,
                       (uint8_t)(LINK_KS_W_IMAGE + at), (uint8_t)n,
                       &k->staged[at]);
        }
        if (k->supply_due) {
            return ask(k, x, X_SUPPLY, true, (uint8_t)LINK_KS_W_SUPPLY_MV,
                       2u, k->supply);
        }
        if (k->leave_due) {
            return ask(k, x, X_LEAVE_MS, true, (uint8_t)LINK_KS_W_LEAVE_MS,
                       1u, &k->leave_ms);
        }
        memcpy(k->cmd, k->cmd_proto, sizeof(k->cmd));
        k->cmd[LINK_KS_W_SEQ] = (uint16_t)(k->seq + 1u);
        k->cmd_live = true;
        k->cmd_abort = false;
        return ask_cmd(k, x);
    }
    if (k->phase == (uint8_t)KST_LINK_FETCHING) {
        return ask_fetch(k, x);
    }
    held = k->phase == (uint8_t)KST_LINK_RUNNING
           || LINK_KS_STATE_OF(k->status[LINK_KS_STATE])
                  != (uint8_t)LINK_KST_PWM
           || (k->status[LINK_KS_STATE] & LINK_KS_F_BUSY) != 0u;
    return due(k, now_ms, held ? KST_LINK_POLL_MS : KST_LINK_IDLE_MS)
           && ask_status(k, now_ms, x);
}

/* Whether the page @p regs took the command that was not sent again.  A
 * command taken leaves its SEQ, and either its operation in LINK_KS_OP or a
 * refusal in LINK_KS_RESULT's high byte.  A page that started again has SEQ
 * 0, operation NONE and no refusal: at SEQ 0, the command after 0xFFFF, the
 * SEQ alone does not tell the two apart.  The command's operation is never
 * NONE. */
static bool page_took_cmd(const kst_link_t *k, const uint16_t *regs)
{
    return regs[LINK_KS_SEQ] == k->cmd[LINK_KS_W_SEQ]
           && ((regs[LINK_KS_RESULT] >> 8) != 0u
               || LINK_KS_OP_OF(regs[LINK_KS_OP]) == k->op);
}

/* Whether the page @p regs is one that started again since the status last
 * read, whatever its SEQ.  The coprocessor names the operation it last
 * started in LINK_KS_OP and keeps it until the next one; a refused command
 * leaves it.  Only a start of the coprocessor sets it back to NONE.  A
 * page that has started no operation yet reads NONE before and after a
 * start, and only its SEQ tells. */
static bool op_went_back(const kst_link_t *k, const uint16_t *regs)
{
    return k->known
           && LINK_KS_OP_OF(k->status[LINK_KS_OP])
                  != (uint8_t)LINK_KST_OP_NONE
           && LINK_KS_OP_OF(regs[LINK_KS_OP]) == (uint8_t)LINK_KST_OP_NONE;
}

static void on_status(kst_link_t *k, const uint16_t *regs)
{
    const bool went_back = op_went_back(k, regs);
    bool restarted = k->known
                     && (regs[LINK_KS_SEQ] != k->seq || went_back);
    bool dropped = false;

    if (k->cmd_doubt) {
        /* The page says whether it took the command that was not sent
         * again: its SEQ and its trace, taken; the SEQ before it, not.  Any
         * other page started again: another SEQ, the command's SEQ without
         * its trace, or an operation back at NONE. */
        k->cmd_doubt = false;
        if (!went_back && page_took_cmd(k, regs)) {
            restarted = false;
            k->verdict = true;
            k->taken = true;
        } else {
            dropped = !restarted;
        }
    }
    memcpy(k->status, regs, sizeof(k->status));
    k->seq = regs[LINK_KS_SEQ];
    k->known = true;
    k->was_held = LINK_KS_STATE_OF(regs[LINK_KS_STATE])
                  != (uint8_t)LINK_KST_PWM;
    if (k->phase == (uint8_t)KST_LINK_SYNC) {
        k->phase = (uint8_t)KST_LINK_READY;
    }
    if (restarted) {
        /* Not the SEQ this module left there: nothing of what it knows
         * about the page holds. */
        finish(k, KST_LINK_OUT_RESTARTED, 0u);
        k->phase = (uint8_t)KST_LINK_READY;
        return;
    }
    if (dropped) {
        finish(k, KST_LINK_OUT_DROPPED, 0u);
        return;
    }
    if (k->active && k->verdict) {
        k->verdict = false;
        if ((regs[LINK_KS_RESULT] >> 8) != 0u) {
            finish(k, KST_LINK_OUT_REFUSED, 0u);
            return;
        }
        k->phase = (uint8_t)KST_LINK_RUNNING;
    }
    if (k->phase == (uint8_t)KST_LINK_RUNNING
        && (regs[LINK_KS_STATE] & LINK_KS_F_BUSY) == 0u) {
        memcpy(k->end_status, regs, sizeof(k->end_status));
        k->phase = (uint8_t)KST_LINK_FETCHING;
        k->fetch = X_RESULTS;
    }
}

static void fetch_done(kst_link_t *k)
{
    if (k->active) {
        finish(k, KST_LINK_OUT_DONE, 0u);
    }
    k->phase = (uint8_t)KST_LINK_READY;
    k->poll_now = true;
}

static void on_refused(kst_link_t *k, uint8_t kind, uint8_t nack)
{
    switch (kind) {
    case X_STATUS:
        /* BAD_PAGE: a coprocessor that reports the minor and has no page.
         * kst_link_done() takes every other refusal of a status read as
         * an exchange without an answer. */
        k->page = false;
        k->known = false;
        finish(k, KST_LINK_OUT_NACK, nack);
        k->phase = (uint8_t)KST_LINK_NO_PAGE;
        break;
    case X_CMD:
        /* The SEQ is not the page's: read it again. */
        k->cmd_live = false;
        k->known = false;
        k->polled = false;
        if (k->cmd_abort) {
            k->cmd_abort = false;
            if (nack != (uint8_t)LINK_NACK_BAD_VALUE) {
                k->abort_wanted = false;
            }
            if (!k->active) {
                k->phase = (uint8_t)KST_LINK_SYNC;
            }
        } else {
            finish(k, KST_LINK_OUT_NACK, nack);
        }
        break;
    default:
        finish(k, KST_LINK_OUT_NACK, nack);
        k->phase = (uint8_t)KST_LINK_READY;
        break;
    }
}

void kst_link_done(kst_link_t *k, int result, const uint16_t *regs)
{
    uint8_t kind;
    bool reads;

    if (k == NULL || k->pending == X_NONE) {
        return;
    }
    kind = k->pending;
    k->pending = X_NONE;
    reads = kind == X_STATUS || kind == X_RESULTS || kind == X_IMAGE
            || kind == X_BACKUP;
    if (result == KST_LINK_NO_ANSWER
        || (result == KST_LINK_ACK && reads && regs == NULL)
        || (kind == X_STATUS && result != KST_LINK_ACK
            && result != (int)LINK_NACK_BAD_PAGE)) {
        /* A status read refused for another reason than "no such page"
         * says nothing about the page: it is asked again. */
        if (kind == X_CMD && !k->cmd_abort && k->abort_wanted
            && k->active) {
            /* An abort came while the command was on its way, on any of
             * its tries. */
            cmd_in_doubt(k);
        } else if (++k->tries >= KST_LINK_RETRIES) {
            /* Given up: what the page holds is read again before
             * anything else is sent. */
            k->tries = 0;
            k->known = false;
            k->cmd_live = false;
            k->cmd_abort = false;
            finish(k, KST_LINK_OUT_NO_ANSWER, 0u);
            k->phase = (uint8_t)KST_LINK_SYNC;
        }
        return;
    }
    k->tries = 0;
    if (result != KST_LINK_ACK) {
        on_refused(k, kind, (uint8_t)result);
        return;
    }
    switch (kind) {
    case X_STATUS:
        on_status(k, regs);
        break;
    case X_STAGE:
        k->stage_at++;
        break;
    case X_SUPPLY:
        k->supply_due = false;
        break;
    case X_LEAVE_MS:
        k->leave_due = false;
        break;
    case X_CMD:
        k->seq = k->cmd[LINK_KS_W_SEQ];
        k->cmd_live = false;
        if (k->cmd_abort) {
            k->cmd_abort = false;
            k->abort_wanted = false;
            k->poll_now = true;
        } else {
            k->verdict = true;
            k->taken = true;
        }
        break;
    case X_RESULTS:
        memcpy(k->results, regs, sizeof(k->results));
        k->have_results = true;
        /* The backup first: the page is left showing the image. */
        if ((k->status[LINK_KS_STATE] & LINK_KS_F_BACKUP) != 0u) {
            k->fetch = X_VIEW_BACKUP;
        } else {
            k->have_backup = false;
            k->fetch = X_VIEW_IMAGE;
        }
        break;
    case X_VIEW_BACKUP:
        k->fetch = X_BACKUP;
        break;
    case X_BACKUP:
        k->have_backup = true;
        memcpy(k->backup_regs, regs, sizeof(k->backup_regs));
        k->fetch = X_VIEW_IMAGE;
        break;
    case X_VIEW_IMAGE:
        k->fetch = X_IMAGE;
        break;
    case X_IMAGE:
    default:
        k->have_image = (k->status[LINK_KS_STATE] & LINK_KS_F_IMAGE) != 0u;
        memcpy(k->image_regs, regs, sizeof(k->image_regs));
        fetch_done(k);
        break;
    }
}

/* --- what the screen reads ---------------------------------------------------- */

/* The phase as the screen sees it: an operation that runs and has sent no
 * frame yet plans. */
static kst_link_phase_t phase_shown(const kst_link_t *k)
{
    if (k->phase == (uint8_t)KST_LINK_RUNNING
        && (k->status[LINK_KS_STATE] & LINK_KS_F_BUSY) != 0u
        && (k->status[LINK_KS_RESULT] & 0xFFu) == LINK_KST_RESULT_PLANNING) {
        return KST_LINK_PLANNING;
    }
    return (kst_link_phase_t)k->phase;
}

kst_link_phase_t kst_link_phase(const kst_link_t *k)
{
    return k != NULL ? phase_shown(k) : KST_LINK_DOWN;
}

bool kst_link_outcome(kst_link_t *k, kst_link_outcome_t *out)
{
    if (k == NULL || out == NULL || !k->outcome_new) {
        return false;
    }
    *out = k->outcome;
    k->outcome_new = false;
    return true;
}

void kst_link_readout(const kst_link_t *k, kst_link_readout_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    if (k == NULL) {
        return;
    }
    r->phase = phase_shown(k);
    r->was_held = k->was_held;
    r->have_image = k->have_image;
    r->have_backup = k->have_backup;
    if (k->known) {
        const uint16_t st = k->status[LINK_KS_STATE];

        r->known = true;
        r->port = (link_kst_state_t)LINK_KS_STATE_OF(st);
        r->port_no = LINK_KS_PORT_OF(st);
        r->flags = (uint16_t)(st & 0x0FF8u);
        r->op = (link_kst_op_t)LINK_KS_OP_OF(k->status[LINK_KS_OP]);
        r->frames = LINK_KS_FRAMES_OF(k->status[LINK_KS_OP]);
    }
    if (k->have_results) {
        const uint16_t *res = k->results;

        r->have_results = true;
        r->fp_rules = res[RES(LINK_KS_FP_RULES)];
        r->fp_regs = res[RES(LINK_KS_FP_REGS_LO)]
                     | ((uint32_t)res[RES(LINK_KS_FP_REGS_HI)] << 16);
        r->half_min_ns = res[RES(LINK_KS_HALF_MIN_NS)];
        r->half_max_ns = res[RES(LINK_KS_HALF_MAX_NS)];
        r->delay_min_us = res[RES(LINK_KS_DELAY_MIN_US)];
        r->delay_max_us = res[RES(LINK_KS_DELAY_MAX_US)];
    }
}

bool kst_link_image(const kst_link_t *k, kst_image_t *out)
{
    if (k == NULL || out == NULL || !k->have_image) {
        return false;
    }
    image_from_regs(k->image_regs, out);
    return true;
}

bool kst_link_backup(const kst_link_t *k, kst_image_t *out)
{
    if (k == NULL || out == NULL || !k->have_backup) {
        return false;
    }
    image_from_regs(k->backup_regs, out);
    return true;
}
