/**
 * @file node_set.c
 * @brief The node's own settings on the node (node_store.h over NVS): the
 *        name, the INA228's address setting, the shunt and its agreed
 *        value, the last panel's language; the writes from the panel and
 *        from the display node's SETUP; RCNODE_T_CONFIG on STREAM.
 *
 * NVS (non-volatile storage): namespace "pwr", key "shunt_uohm", the
 * shunt, where the release before kept it; namespace "node", keys "name",
 * "i228", "shunt_agr" and "lang".  A mutex guards the settings: the `node`
 * task takes the panel's writes, `disp` the display node's SETUP, NimBLE's
 * host task reads the name for the advertising and the shunt for INFO.
 *
 * RCNODE_T_CONFIG goes on STREAM once a central wrote RCNODE_OP_CONFIG on
 * the link, at once and after each change; the link's end stops it, so a
 * central built before CONFIG never receives one.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

/** @brief The mutex over s_st. */
static SemaphoreHandle_t s_mx;
/** @brief The settings. */
static nst_t s_st;

/**
 * @brief A key's namespace and name in NVS.
 * @param k The key.
 * @param ns Receives the namespace.
 * @return The key's name.
 */
static const char *nvs_key(nst_key_t k, const char **ns)
{
    *ns = "node";
    switch (k) {
    case NST_K_NAME:   return "name";
    case NST_K_I228:   return "i228";
    case NST_K_SHUNT:  *ns = "pwr"; return "shunt_uohm";
    case NST_K_AGREED: return "shunt_agr";
    case NST_K_LANG:
    default:           return "lang";
    }
}

/**
 * @brief nst_io_t's read from NVS.
 * @param ctx Unused.  @param k The key.  @param buf Receives the value.
 * @param n Its size.
 * @return false when none is stored or NVS failed.
 */
static bool nvs_load(void *ctx, nst_key_t k, void *buf, size_t n)
{
    (void)ctx;
    const char *ns;
    const char *key = nvs_key(k, &ns);
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e;
    if (k == NST_K_NAME) {
        size_t len = n;
        e = nvs_get_str(h, key, buf, &len);
    } else if (n == sizeof(uint16_t)) {
        e = nvs_get_u16(h, key, buf);
    } else {
        e = nvs_get_u8(h, key, buf);
    }
    nvs_close(h);
    return e == ESP_OK;
}

/**
 * @brief nst_io_t's write to NVS, committed.
 * @param ctx Unused.  @param k The key.  @param buf The value.
 * @param n Its size.
 * @return false when NVS failed.
 */
static bool nvs_save(void *ctx, nst_key_t k, const void *buf, size_t n)
{
    (void)ctx;
    const char *ns;
    const char *key = nvs_key(k, &ns);
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e;
    if (k == NST_K_NAME) {
        e = nvs_set_str(h, key, buf);
    } else if (n == sizeof(uint16_t)) {
        uint16_t v;
        memcpy(&v, buf, sizeof v);
        e = nvs_set_u16(h, key, v);
    } else {
        e = nvs_set_u8(h, key, *(const uint8_t *)buf);
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e == ESP_OK;
}

/** @brief The store. */
static const nst_io_t k_io = { nvs_load, nvs_save, NULL };

/** @brief The node's ms clock.  @return ms. */
static uint32_t now_ms(void)
{
    return (uint32_t)(node_us() / 1000u);
}

void node_set_start(bool power)
{
    s_mx = xSemaphoreCreateMutex();
    nst_load(&s_st, &k_io, power);
    ESP_LOGI(NODE_TAG, "set: name \"%s\", INA228 %s 0x%02X, shunt %uuOhm, "
             "agreed %uuOhm", s_st.v.name,
             s_st.v.i228 == RCNODE_I228_AUTO ? "AUTO" : "at",
             (unsigned)s_st.v.i228, (unsigned)s_st.v.shunt,
             (unsigned)s_st.agreed);
}

/** @brief Take the mutex; before node_set_start() there is none. */
static void lock(void)
{
    if (s_mx != NULL) {
        (void)xSemaphoreTake(s_mx, portMAX_DELAY);
    }
}

/** @brief Give the mutex back. */
static void unlock(void)
{
    if (s_mx != NULL) {
        (void)xSemaphoreGive(s_mx);
    }
}

void node_set_vals(nst_vals_t *out)
{
    lock();
    *out = s_st.v;
    unlock();
}

uint8_t node_set_lang(void)
{
    lock();
    const uint8_t l = s_st.lang;
    unlock();
    return l;
}

/** @brief RCNODE_T_CONFIG on STREAM, when a central asked for it. */
static void push_config(void)
{
    rcnode_config_t c;
    lock();
    const bool due = nst_sends_config(&s_st);
    if (due) {
        nst_config(&s_st, now_ms(), &c);
    }
    unlock();
    if (!due) {
        return;
    }
    c.hdr.t_node = (uint32_t)node_us();
    uint8_t b[RCNODE_CONFIG_LEN];
    const size_t n = rcnode_enc_config(&c, b, sizeof b);
    if (n != 0u) {
        node_ble_stream(b, n);
    }
}

/**
 * @brief What a change touched, applied: the part's set-up and the
 *        advertised name.
 * @param ch NST_CH_*.
 */
static void apply(unsigned ch)
{
    nst_vals_t v;
    node_set_vals(&v);
#if !CONFIG_RCNODE_ROLE_BALANCE && !CONFIG_RCNODE_ROLE_SERVO
    if ((ch & NST_CH_SHUNT) != 0u) {
        node_pwr_set_shunt(v.shunt);
    }
    if ((ch & NST_CH_I228) != 0u) {
        node_pwr_set_addr(v.i228);
    }
#endif
    if ((ch & NST_CH_NAME) != 0u) {
        node_ble_rename();
    }
    if (ch != 0u) {
        push_config();
    }
}

unsigned node_set_setup_save(const nst_vals_t *v)
{
    lock();
    const unsigned ch = nst_setup_save(&s_st, &k_io, v, now_ms());
    const uint32_t failed = s_st.failed;
    unlock();
    ESP_LOGI(NODE_TAG, "set: SETUP saved, changed 0x%X, %u store writes failed",
             ch, (unsigned)failed);
    apply(ch);
    return ch;
}

void node_set_control(const rcnode_ctl_t *c)
{
    if (c->op == RCNODE_OP_CONFIG) {
        lock();
        nst_ask(&s_st, true);
        unlock();
        push_config();
        return;
    }
    if (c->op == RCNODE_OP_LANG) {
        lock();
        (void)nst_lang(&s_st, &k_io, c->lang);
        unlock();
        return;
    }
    lock();
    const unsigned ch = nst_panel_op(&s_st, &k_io, c);
    unlock();
    apply(ch);
    if (ch == 0u && c->op == RCNODE_OP_SHUNT) {
        push_config();      /* the same shunt again: the answer still owed */
    }
}

void node_set_link_end(void)
{
    lock();
    nst_ask(&s_st, false);
    unlock();
}

uint16_t node_set_shunt(void)
{
    lock();
    const uint16_t v = s_st.v.shunt;
    unlock();
    return v;
}
