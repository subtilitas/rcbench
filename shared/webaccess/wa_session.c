/*
 * Wi-Fi access. See wa_session.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_session.h"

#include <string.h>

/* ------------------------------------------------------------- names */

void wa_secret_make(char out[WA_SECRET_LEN + 1u], wa_rand_fn rand, void *ctx)
{
    static const char k_chars[] = WA_SECRET_CHARS;
    const uint32_t n = (uint32_t)(sizeof(k_chars) - 1u);
    /* The largest multiple of n that fits a byte: a byte at or above it
     * would favour the first characters, so it is drawn again. */
    const uint32_t fair = 256u - (256u % n);
    unsigned have = 0u;
    while (have < WA_SECRET_LEN) {
        uint32_t word = (rand != NULL) ? rand(ctx) : 0u;
        for (int b = 0; b < 4 && have < WA_SECRET_LEN; ++b) {
            const uint32_t byte = word & 0xFFu;
            word >>= 8;
            if (byte < fair) {
                out[have++] = k_chars[byte % n];
            }
        }
    }
    out[WA_SECRET_LEN] = '\0';
}

void wa_name(const uint8_t mac[6], char out[WA_NAME_MAX])
{
    static const char k_hex[] = "0123456789ABCDEF";
    memcpy(out, "rcbench-", 8u);
    out[8]  = k_hex[mac[4] >> 4];
    out[9]  = k_hex[mac[4] & 0x0Fu];
    out[10] = k_hex[mac[5] >> 4];
    out[11] = k_hex[mac[5] & 0x0Fu];
    out[12] = '\0';
}

/* ------------------------------------------------------- what is kept */

void wa_conf_defaults(wa_conf_t *c)
{
    memset(c, 0, sizeof(*c));
    c->code_on = true;
}

static wa_conf_err_t check_net(const char *ssid, size_t ssid_len,
                               const char *pass, size_t pass_len)
{
    if (ssid == NULL || ssid_len == 0u) {
        return WA_CONF_SSID_EMPTY;
    }
    if (ssid_len > WA_SSID_MAX) {
        return WA_CONF_SSID_LONG;
    }
    if (memchr(ssid, '\0', ssid_len) != NULL) {
        return WA_CONF_SSID_CHAR;
    }
    if (pass == NULL || pass_len < WA_PASS_MIN) {
        return WA_CONF_PASS_SHORT;
    }
    if (pass_len > WA_PASS_MAX) {
        return WA_CONF_PASS_LONG;
    }
    for (size_t i = 0; i < pass_len; ++i) {
        if (pass[i] < 0x20 || pass[i] > 0x7E) {
            return WA_CONF_PASS_CHAR;
        }
    }
    return WA_CONF_OK;
}

wa_conf_err_t wa_conf_set_net(wa_conf_t *c, const char *ssid, size_t ssid_len,
                              const char *pass, size_t pass_len)
{
    const wa_conf_err_t e = check_net(ssid, ssid_len, pass, pass_len);
    if (e != WA_CONF_OK) {
        return e;
    }
    memset(c->ssid, 0, sizeof(c->ssid));
    memset(c->pass, 0, sizeof(c->pass));
    memcpy(c->ssid, ssid, ssid_len);
    memcpy(c->pass, pass, pass_len);
    c->have_net = true;
    return WA_CONF_OK;
}

void wa_conf_load(const wa_store_t *s, wa_conf_t *out)
{
    wa_conf_t got;
    wa_conf_defaults(out);
    wa_conf_defaults(&got);
    if (s == NULL || s->load == NULL || !s->load(s->ctx, &got)) {
        return;
    }
    out->code_on = got.code_on;
    if (got.have_net) {
        /* Terminated within its field, then held to the same rule as a
         * network typed in. */
        got.ssid[WA_SSID_MAX] = '\0';
        got.pass[WA_PASS_MAX] = '\0';
        (void)wa_conf_set_net(out, got.ssid, strlen(got.ssid), got.pass,
                              strlen(got.pass));
    }
}

bool wa_conf_save(const wa_store_t *s, const wa_conf_t *c)
{
    return s != NULL && s->save != NULL && s->save(s->ctx, c);
}

bool wa_conf_forget(const wa_store_t *s, wa_conf_t *c)
{
    wa_conf_defaults(c);
    return s != NULL && s->erase != NULL && s->erase(s->ctx);
}

/* ------------------------------------------------- the writes owed */

void wa_pending_init(wa_pending_t *p)
{
    memset(p, 0, sizeof(*p));
}

static void pending_touched(wa_pending_t *p)
{
    ++p->gen;
    p->failed = false;
    p->left = WA_PENDING_RETRIES;
    p->next_ms = 0u;
}

void wa_pending_change(wa_pending_t *p)
{
    p->save = true;
    pending_touched(p);
}

void wa_pending_forget(wa_pending_t *p)
{
    p->erase = true;
    p->save = false;
    pending_touched(p);
}

bool wa_pending_due(const wa_pending_t *p, bool may_write, uint32_t now_ms)
{
    if (!may_write || !(p->erase || p->save)) {
        return false;
    }
    if (!p->failed) {
        return true;
    }
    /* After a failure: only while automatic tries are left, and not
     * before the next one is due. */
    return p->left > 0u && (int32_t)(now_ms - p->next_ms) >= 0;
}

void wa_pending_write(const wa_store_t *st, const wa_pending_t *snap,
                      const wa_conf_t *c, bool *erase_ok, bool *save_ok)
{
    *erase_ok = true;
    *save_ok = true;
    if (snap->erase) {
        wa_conf_t gone;
        *erase_ok = wa_conf_forget(st, &gone);
    }
    /* After a failed erase the save is not tried: the store's state is
     * unknown, and the erase is owed first. */
    if (snap->save && *erase_ok) {
        *save_ok = wa_conf_save(st, c);
    } else if (snap->save) {
        *save_ok = false;
    }
}

void wa_pending_done(wa_pending_t *p, const wa_pending_t *snap,
                     bool erase_ok, bool save_ok, uint32_t now_ms)
{
    if (p->gen != snap->gen) {
        /* A change came during the write: what it owes stands, and it
         * is tried on the next frame that may write. */
        return;
    }
    if (snap->erase && erase_ok) {
        p->erase = false;
    }
    if (snap->save && save_ok) {
        p->save = false;
    }
    if (erase_ok && save_ok) {
        p->failed = false;
        return;
    }
    if (p->failed && p->left > 0u) {
        --p->left;               /* an automatic try that failed      */
    }
    p->failed = true;
    p->next_ms = now_ms + WA_PENDING_RETRY_MS;
}

unsigned wa_modes(const wa_conf_t *c)
{
    return WA_CAN_AP | ((c != NULL && c->have_net) ? WA_CAN_JOIN : 0u);
}

/* ------------------------------------------------------- the scan list */

void wa_scan_clear(wa_scan_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void scan_sort(wa_scan_t *s)
{
    /* Insertion sort, strongest first; stable, so equal signals keep the
     * order the scan reported them in. */
    for (unsigned i = 1; i < s->n; ++i) {
        const wa_net_t v = s->net[i];
        unsigned j = i;
        while (j > 0u && s->net[j - 1u].rssi < v.rssi) {
            s->net[j] = s->net[j - 1u];
            --j;
        }
        s->net[j] = v;
    }
}

void wa_scan_add(wa_scan_t *s, const uint8_t *ssid, size_t len, int8_t rssi,
                 bool usable)
{
    if (!usable || ssid == NULL || len == 0u || len > WA_SSID_MAX
        || memchr(ssid, '\0', len) != NULL) {
        ++s->dropped;   /* open, enterprise, hidden or unnamed */
        return;
    }
    for (unsigned i = 0; i < s->n; ++i) {
        if (strlen(s->net[i].ssid) == len
            && memcmp(s->net[i].ssid, ssid, len) == 0) {
            if (rssi > s->net[i].rssi) {
                s->net[i].rssi = rssi;
                scan_sort(s);
            }
            return;   /* one row a name: a second access point of it */
        }
    }
    if (s->n == WA_SCAN_MAX) {
        if (rssi <= s->net[WA_SCAN_MAX - 1u].rssi) {
            ++s->dropped;
            return;
        }
        --s->n;
        ++s->dropped;
    }
    wa_net_t *n = &s->net[s->n++];
    memset(n, 0, sizeof(*n));
    memcpy(n->ssid, ssid, len);
    n->rssi = rssi;
    scan_sort(s);
}

const char *wa_scan_pick(const wa_scan_t *s, unsigned i)
{
    return (s != NULL && i < s->n) ? s->net[i].ssid : NULL;
}

/* ------------------------------------------------------- the session */

void wa_init(wa_t *w)
{
    memset(w, 0, sizeof(*w));
}

wa_off_t wa_gate(bool armed, bool supply, bool run)
{
    if (armed) {
        return WA_OFF_ARMED;
    }
    if (supply) {
        return WA_OFF_SUPPLY;
    }
    return run ? WA_OFF_RUN : WA_OFF_NONE;
}

static void enter(wa_t *w, wa_state_t st, uint32_t now_ms)
{
    w->state    = st;
    w->since_ms = now_ms;
}

wa_off_t wa_start(wa_t *w, wa_mode_t mode, wa_off_t gate, uint32_t now_ms,
                  wa_rand_fn rand, void *ctx)
{
    if (wa_radio(w)) {
        wa_close(w, WA_OFF_ASKED);
    }
    if (gate != WA_OFF_NONE) {
        w->off = gate;
        return gate;
    }
    /* Everything of the last session goes: a code shown then opens
     * nothing now. */
    wa_init(w);
    w->mode     = mode;
    w->last_ms  = now_ms;
    wa_secret_make(w->pass, rand, ctx);
    wa_secret_make(w->code, rand, ctx);
    switch (mode) {
    case WA_MODE_JOIN:
    case WA_MODE_FETCH: enter(w, WA_ST_JOINING, now_ms);  break;
    case WA_MODE_SCAN: enter(w, WA_ST_SCANNING, now_ms);  break;
    case WA_MODE_AP:
    default:           enter(w, WA_ST_STARTING, now_ms);  break;
    }
    return WA_OFF_NONE;
}

void wa_close(wa_t *w, wa_off_t why)
{
    if (w->state == WA_ST_OFF) {
        return;
    }
    w->state = WA_ST_OFF;
    w->off   = why;
    w->busy  = 0u;
    w->ip    = 0u;
    /* The secrets go with the session. */
    memset(w->code, 0, sizeof(w->code));
    memset(w->pass, 0, sizeof(w->pass));
}

bool wa_radio(const wa_t *w)
{
    return w->state != WA_ST_OFF;
}

bool wa_serving(const wa_t *w)
{
    return w->state == WA_ST_AP
           || (w->state == WA_ST_JOINED && w->mode != WA_MODE_FETCH);
}

void wa_on_ap_up(wa_t *w, uint32_t now_ms)
{
    if (w->state == WA_ST_STARTING) {
        enter(w, WA_ST_AP, now_ms);
        w->last_ms = now_ms;
    }
}

void wa_on_scan_done(wa_t *w)
{
    if (w->state == WA_ST_SCANNING) {
        wa_close(w, WA_OFF_NONE);
    }
}

void wa_on_associated(wa_t *w, uint32_t now_ms)
{
    if (w->state == WA_ST_JOINING) {
        enter(w, WA_ST_ADDRESS, now_ms);
    }
}

void wa_on_address(wa_t *w, uint32_t ip, uint32_t now_ms)
{
    if (w->state == WA_ST_ADDRESS || w->state == WA_ST_JOINING) {
        w->ip = ip;
        enter(w, WA_ST_JOINED, now_ms);
        w->last_ms = now_ms;
    }
}

void wa_on_join_failed(wa_t *w, wa_join_err_t why)
{
    if (w->state == WA_ST_JOINING || w->state == WA_ST_ADDRESS
        || w->state == WA_ST_JOINED) {
        w->join_err = why;
        wa_close(w, WA_OFF_JOIN);
    }
}

bool wa_on_drop(wa_t *w, bool auth)
{
    if (w->state == WA_ST_ADDRESS || w->state == WA_ST_JOINED) {
        wa_on_join_failed(w, WA_JOIN_LOST);
        return false;
    }
    if (w->state != WA_ST_JOINING) {
        return false;
    }
    if (!auth) {
        w->auth_drops = 0u;
        return true;
    }
    if (++w->auth_drops >= WA_AUTH_TRIES) {
        wa_on_join_failed(w, WA_JOIN_PASSWORD);
        return false;
    }
    return true;
}

void wa_on_failed(wa_t *w)
{
    wa_close(w, WA_OFF_FAILED);
}

bool wa_tick(wa_t *w, uint32_t now_ms, wa_off_t gate, bool stop)
{
    if (w->state == WA_ST_OFF) {
        return false;
    }
    if (stop) {
        wa_close(w, WA_OFF_STOP);
        return true;
    }
    if (gate != WA_OFF_NONE) {
        wa_close(w, gate);
        return true;
    }
    const uint32_t in_state = now_ms - w->since_ms;
    switch (w->state) {
    case WA_ST_STARTING:
    case WA_ST_SCANNING:
        if (in_state >= WA_START_MS) {
            wa_close(w, WA_OFF_FAILED);
            return true;
        }
        break;
    case WA_ST_JOINING:
        if (in_state >= WA_JOIN_MS) {
            w->join_err = WA_JOIN_NOT_FOUND;
            wa_close(w, WA_OFF_JOIN);
            return true;
        }
        break;
    case WA_ST_ADDRESS:
        if (in_state >= WA_ADDRESS_MS) {
            w->join_err = WA_JOIN_NO_ADDRESS;
            wa_close(w, WA_OFF_JOIN);
            return true;
        }
        break;
    case WA_ST_AP:
    case WA_ST_JOINED:
        if (w->busy == 0u && (uint32_t)(now_ms - w->last_ms) >= WA_IDLE_MS) {
            wa_close(w, WA_OFF_IDLE);
            return true;
        }
        break;
    case WA_ST_OFF:
    default:
        break;
    }
    return false;
}

void wa_request_begin(wa_t *w, uint32_t now_ms)
{
    if (wa_serving(w)) {
        ++w->busy;
        w->last_ms = now_ms;
    }
}

void wa_request_end(wa_t *w, uint32_t now_ms)
{
    if (wa_serving(w)) {
        if (w->busy > 0u) {
            --w->busy;
        }
        w->last_ms = now_ms;
    }
}

uint32_t wa_idle_left(const wa_t *w, uint32_t now_ms)
{
    if (!wa_serving(w) || w->busy > 0u) {
        return 0u;
    }
    const uint32_t gone = now_ms - w->last_ms;
    return (gone >= WA_IDLE_MS) ? 0u : WA_IDLE_MS - gone;
}

wa_code_t wa_code_check(wa_t *w, const char *given, bool code_on)
{
    if (!wa_serving(w)) {
        return WA_CODE_CLOSED;
    }
    if (!code_on) {
        return WA_CODE_OK;
    }
    if (given == NULL || given[0] == '\0') {
        return WA_CODE_MISSING;
    }
    /* Every character compared, whatever the first difference: the time
     * an answer takes says nothing about how much of a guess was right. */
    unsigned diff = 0u;
    size_t i = 0u;
    for (; i < WA_SECRET_LEN; ++i) {
        const char g = given[i];
        diff |= (unsigned)(uint8_t)(g ^ w->code[i]);
        if (g == '\0') {
            diff |= 0x100u;
            break;
        }
    }
    if (i == WA_SECRET_LEN && given[WA_SECRET_LEN] != '\0') {
        diff |= 0x200u;
    }
    if (diff == 0u) {
        return WA_CODE_OK;
    }
    if (++w->guesses >= WA_GUESS_MAX) {
        wa_close(w, WA_OFF_GUESSES);
    }
    return WA_CODE_WRONG;
}
