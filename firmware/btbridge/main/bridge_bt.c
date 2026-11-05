/**
 * @file bridge_bt.c
 * @brief Bluedroid Classic for the bridge (bridge_bt.h): inquiry, name
 *        reads, pairing and the SPP initiator's link.
 *
 * A pairing and a connect run the same way: the SPP service search on the
 * device, then esp_spp_connect() with authentication, which pairs with the
 * PIN on the way.  A pairing closes the link once it is open and reports
 * OK; the link key stays in Bluedroid's bond list.  The bridge task owns
 * every field below except s_fwd and s_link, which the stack's task reads
 * for a link's received bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#include "bridge_bt.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_spp_api.h"

/** @brief The console lines' tag. */
static const char *TAG = "bt";

/** @brief The name the bridge gives itself; nobody sees it, the bridge is
 *         not discoverable. */
#define BRIDGE_NAME "rcbench-btbridge"
/** @brief Bytes held while the link is congested. */
#define TX_HOLD 512u

/** @brief What the SPP procedure under way is for. */
typedef enum {
    PURPOSE_NONE = 0, /**< none, or one that was stopped */
    PURPOSE_PAIR,     /**< AT+PAIR: open, close, OK */
    PURPOSE_LINK,     /**< data mode: open and keep */
} purpose_t;

/** @brief The callbacks' queue. */
static QueueHandle_t s_q;
/** @brief The UART a link's bytes go to. */
static uart_port_t s_uart;
/** @brief The procedure under way and its device. */
static purpose_t s_purpose;
/** @brief The device of s_purpose. */
static uint8_t s_peer[HC05EMU_ADDR_LEN];
/** @brief The PIN the stack answers a PIN request with. */
static char s_pin[HC05EMU_PIN_MAX];
/** @brief The handle of a connect under way, 0 none. */
static uint32_t s_cl_handle;
/** @brief The open link's handle, 0 none; read by the stack's task. */
static _Atomic uint32_t s_link;
/** @brief Received bytes go to the UART; read by the stack's task. */
static atomic_bool s_fwd;
/** @brief The link is congested. */
static bool s_cong;
/** @brief The bytes held for a congested link. */
static uint8_t s_hold[TX_HOLD];
/** @brief How many. */
static size_t s_hold_n;
/** @brief An inquiry was asked for: its results go to the emulator. */
static bool s_inq_wanted;
/** @brief That inquiry has begun: its end is reported. */
static bool s_inq_started;
/** @brief Handles this side closed: their CLOSE events are no failure of
 *         the procedure after them. */
static uint32_t s_closing[2];
/** @brief The next slot of s_closing. */
static unsigned s_closing_i;
/** @brief The stack's inquiry runs, ours or a cancelled one. */
static bool s_disc_busy;
/** @brief An inquiry waiting for the cancelled one to end: its units of
 *         1.28s, 0 none. */
static uint8_t s_inq_units;
/** @brief Its device count. */
static uint8_t s_inq_max;

/**
 * @brief Post an event from a callback; dropped while the queue is full.
 * @param ev The event.
 */
static void post(const bridge_ev_t *ev)
{
    if (xQueueSend(s_q, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full: event %d dropped", (int)ev->kind);
    }
}

/**
 * @brief Copy a name the stack gave, cut to HC05EMU_NAME_MAX - 1 bytes.
 * @param dst The buffer, HC05EMU_NAME_MAX bytes.
 * @param src The name, not terminated.
 * @param n   Its length.
 */
static void copy_name(char *dst, const uint8_t *src, size_t n)
{
    if (n > HC05EMU_NAME_MAX - 1u) {
        n = HC05EMU_NAME_MAX - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/**
 * @brief An inquiry result: the class, the RSSI and a name from the
 *        result's own name or its EIR (extended inquiry response).
 * @param p The result.
 */
static void on_disc_res(const struct disc_res_param *p)
{
    bridge_ev_t ev = { .kind = BT_EV_FOUND, .rssi = INT16_MAX };
    memcpy(ev.addr, p->bda, sizeof ev.addr);
    for (int i = 0; i < p->num_prop; ++i) {
        const esp_bt_gap_dev_prop_t *pr = &p->prop[i];
        if (pr->type == ESP_BT_GAP_DEV_PROP_COD) {
            ev.cod = *(const uint32_t *)pr->val;
        } else if (pr->type == ESP_BT_GAP_DEV_PROP_RSSI) {
            ev.rssi = *(const int8_t *)pr->val;
        } else if (pr->type == ESP_BT_GAP_DEV_PROP_BDNAME && pr->len > 0) {
            copy_name(ev.name, pr->val, (size_t)pr->len);
            ev.named = true;
        } else if (pr->type == ESP_BT_GAP_DEV_PROP_EIR && !ev.named) {
            uint8_t len = 0;
            uint8_t *n = esp_bt_gap_resolve_eir_data(
                pr->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
            if (n == NULL) {
                n = esp_bt_gap_resolve_eir_data(
                    pr->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
            }
            if (n != NULL && len > 0u) {
                copy_name(ev.name, n, len);
                ev.named = true;
            }
        }
    }
    post(&ev);
}

/**
 * @brief The GAP callback, in the stack's task.
 * @param event The event.
 * @param param Its parameters.
 */
static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    bridge_ev_t ev = { 0 };
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        on_disc_res(&param->disc_res);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        ev.kind = param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED
                      ? BT_EV_DISC_STARTED
                      : BT_EV_DISC_STOPPED;
        post(&ev);
        break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        ev.kind = BT_EV_NAME;
        ev.ok = param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS;
        memcpy(ev.addr, param->read_rmt_name.bda, sizeof ev.addr);
        copy_name(ev.name, param->read_rmt_name.rmt_name,
                  strnlen((const char *)param->read_rmt_name.rmt_name,
                          ESP_BT_GAP_MAX_BDNAME_LEN));
        post(&ev);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        /* A fixed PIN answers by itself; this is for a stack that asks. */
        esp_bt_pin_code_t pin = { 0 };
        const size_t n = strnlen(s_pin, sizeof s_pin - 1u);
        memcpy(pin, s_pin, n);
        (void)esp_bt_gap_pin_reply(param->pin_req.bda, true, (uint8_t)n, pin);
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "pairing %s", param->auth_cmpl.stat
                                        == ESP_BT_STATUS_SUCCESS
                                        ? "done" : "failed");
        break;
    default:
        break;
    }
}

/**
 * @brief The SPP callback, in the stack's task.  A link's received bytes
 *        go to the UART here, while forwarding is on.
 * @param event The event.
 * @param param Its parameters.
 */
static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    bridge_ev_t ev = { 0 };
    switch (event) {
    case ESP_SPP_DATA_IND_EVT:
        if (atomic_load(&s_fwd)
            && param->data_ind.handle == atomic_load(&s_link)) {
            (void)uart_write_bytes(s_uart, param->data_ind.data,
                                   param->data_ind.len);
        }
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ev.kind = BT_EV_SDP;
        ev.ok = param->disc_comp.status == ESP_SPP_SUCCESS
                && param->disc_comp.scn_num > 0;
        ev.scn = ev.ok ? param->disc_comp.scn[0] : 0u;
        post(&ev);
        break;
    case ESP_SPP_CL_INIT_EVT:
        ev.kind = BT_EV_CL_INIT;
        ev.ok = param->cl_init.status == ESP_SPP_SUCCESS;
        ev.handle = param->cl_init.handle;
        post(&ev);
        break;
    case ESP_SPP_OPEN_EVT:
        ev.kind = BT_EV_OPEN;
        ev.ok = param->open.status == ESP_SPP_SUCCESS;
        ev.handle = param->open.handle;
        memcpy(ev.addr, param->open.rem_bda, sizeof ev.addr);
        post(&ev);
        break;
    case ESP_SPP_CLOSE_EVT:
        ev.kind = BT_EV_CLOSE;
        ev.handle = param->close.handle;
        post(&ev);
        break;
    case ESP_SPP_WRITE_EVT:
        if (param->write.cong) {
            ev.kind = BT_EV_CONG;
            ev.cong = true;
            ev.handle = param->write.handle;
            post(&ev);
        }
        break;
    case ESP_SPP_CONG_EVT:
        ev.kind = BT_EV_CONG;
        ev.cong = param->cong.cong;
        ev.handle = param->cong.handle;
        post(&ev);
        break;
    default:
        break;
    }
}

bool bridge_bt_start(QueueHandle_t q, uart_port_t uart)
{
    s_q = q;
    s_uart = uart;
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) {
        err = esp_bt_controller_init(&cfg);
    }
    if (err == ESP_OK) {
        err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    }
    esp_bluedroid_config_t bd = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bd.ssp_en = false; /* an HC-05 pairs with a PIN only */
    if (err == ESP_OK) {
        err = esp_bluedroid_init_with_cfg(&bd);
    }
    if (err == ESP_OK) {
        err = esp_bluedroid_enable();
    }
    if (err == ESP_OK) {
        err = esp_bt_gap_register_callback(gap_cb);
    }
    if (err == ESP_OK) {
        err = esp_spp_register_callback(spp_cb);
    }
    const esp_spp_cfg_t spp = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };
    if (err == ESP_OK) {
        err = esp_spp_enhanced_init(&spp);
    }
    if (err == ESP_OK) {
        (void)esp_bt_gap_set_device_name(BRIDGE_NAME);
        err = esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE,
                                       ESP_BT_NON_DISCOVERABLE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth not started: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

/**
 * @brief The procedure under way failed: the emulator hears it as FAIL to
 *        AT+PAIR or as a failed connect.
 * @param e      The emulator.
 * @param now_ms The clock.
 */
static void procedure_failed(hc05emu_t *e, uint32_t now_ms)
{
    const purpose_t was = s_purpose;
    s_purpose = PURPOSE_NONE;
    s_cl_handle = 0;
    if (was == PURPOSE_PAIR) {
        hc05emu_pair_done(e, s_peer, false, now_ms);
    } else if (was == PURPOSE_LINK) {
        hc05emu_link_event(e, s_peer, false, now_ms);
    }
}

/**
 * @brief Start an inquiry now, or once a cancelled one has ended.
 * @param e      The emulator.
 * @param units  Units of 1.28s.
 * @param max    Devices asked for.
 * @param now_ms The clock.
 */
static void start_inquiry(hc05emu_t *e, uint8_t units, uint8_t max,
                          uint32_t now_ms)
{
    s_inq_wanted = true;
    s_inq_started = false;
    if (s_disc_busy) {
        s_inq_units = units;
        s_inq_max = max;
        return;
    }
    s_inq_units = 0;
    if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, units,
                                   max) != ESP_OK) {
        ESP_LOGW(TAG, "inquiry refused");
        s_inq_wanted = false;
        hc05emu_inq_done(e, now_ms);
        return;
    }
    s_disc_busy = true;
}

/**
 * @brief Begin a pairing or a connect: the PIN set, the SPP search on the
 *        device started.
 * @param e       The emulator.
 * @param a       The PAIR or CONNECT action.
 * @param purpose What for.
 * @param now_ms  The clock.
 */
static void open_link(hc05emu_t *e, const hc05emu_act_t *a, purpose_t purpose,
                      uint32_t now_ms)
{
    s_purpose = purpose;
    s_cl_handle = 0;
    memcpy(s_peer, a->addr, sizeof s_peer);
    memcpy(s_pin, a->pin, sizeof s_pin);
    esp_bt_pin_code_t pin = { 0 };
    const size_t n = strnlen(s_pin, sizeof s_pin - 1u);
    memcpy(pin, s_pin, n);
    (void)esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, (uint8_t)n, pin);
    if (esp_spp_start_discovery(s_peer) != ESP_OK) {
        procedure_failed(e, now_ms);
    }
}

/**
 * @brief Close @p h and keep it, so its CLOSE event is passed over.
 * @param h The handle; 0 does nothing.
 */
static void close_handle(uint32_t h)
{
    if (h == 0u) {
        return;
    }
    s_closing[s_closing_i] = h;
    s_closing_i = (s_closing_i + 1u) % 2u;
    (void)esp_spp_disconnect(h);
}

/**
 * @brief Whether @p h is one this side closed; forgotten once asked.
 * @param h The handle of a CLOSE event.
 * @return true for one of ours.
 */
static bool closed_here(uint32_t h)
{
    for (unsigned i = 0; i < 2u; ++i) {
        if (h != 0u && s_closing[i] == h) {
            s_closing[i] = 0;
            return true;
        }
    }
    return false;
}

/** @brief Close the open link and end every procedure, reporting none. */
static void stop_all(void)
{
    s_purpose = PURPOSE_NONE;
    s_inq_wanted = false;
    s_inq_started = false;
    s_inq_units = 0;
    if (s_disc_busy) {
        (void)esp_bt_gap_cancel_discovery();
    }
    close_handle(atomic_exchange(&s_link, 0u));
    close_handle(s_cl_handle);
    s_cl_handle = 0;
    s_cong = false;
    s_hold_n = 0;
}

void bridge_bt_perform(hc05emu_t *e, const hc05emu_act_t *a, uint32_t now_ms)
{
    switch (a->kind) {
    case HC05EMU_ACT_INQ:
        ESP_LOGI(TAG, "inquiry, %u x 1.28s, %u devices", (unsigned)a->units,
                 (unsigned)a->max);
        start_inquiry(e, a->units, a->max, now_ms);
        break;
    case HC05EMU_ACT_NAME: {
        uint8_t addr[HC05EMU_ADDR_LEN];
        memcpy(addr, a->addr, sizeof addr);
        if (esp_bt_gap_read_remote_name(addr) != ESP_OK) {
            hc05emu_name_done(e, a->addr, NULL, now_ms);
        }
        break;
    }
    case HC05EMU_ACT_PAIR:
        ESP_LOGI(TAG, "pairing");
        open_link(e, a, PURPOSE_PAIR, now_ms);
        break;
    case HC05EMU_ACT_CONNECT:
        ESP_LOGI(TAG, "connect");
        open_link(e, a, PURPOSE_LINK, now_ms);
        break;
    case HC05EMU_ACT_STOP:
        stop_all();
        break;
    case HC05EMU_ACT_NONE:
    case HC05EMU_ACT_SEND:
    case HC05EMU_ACT_BAUD:
    case HC05EMU_ACT_SAVE:
    default:
        break;
    }
}

/** @brief Send what the congested link held, as far as it takes it. */
static void flush_hold(void)
{
    const uint32_t h = atomic_load(&s_link);
    if (h != 0u && s_hold_n > 0u && !s_cong) {
        if (esp_spp_write(h, (int)s_hold_n, s_hold) == ESP_OK) {
            s_hold_n = 0;
        }
    }
}

void bridge_bt_event(hc05emu_t *e, const bridge_ev_t *ev, uint32_t now_ms)
{
    switch (ev->kind) {
    case BT_EV_FOUND:
        if (s_inq_wanted) {
            hc05emu_found(e, ev->addr, ev->cod, ev->rssi,
                          ev->named ? ev->name : NULL, now_ms);
        }
        break;
    case BT_EV_DISC_STARTED:
        s_disc_busy = true;
        s_inq_started = s_inq_wanted;
        break;
    case BT_EV_DISC_STOPPED:
        s_disc_busy = false;
        if (s_inq_started) {
            s_inq_wanted = false;
            s_inq_started = false;
            hc05emu_inq_done(e, now_ms);
        } else if (s_inq_wanted && s_inq_units != 0u) {
            /* The cancelled inquiry ended: the one asked for starts. */
            start_inquiry(e, s_inq_units, s_inq_max, now_ms);
        }
        break;
    case BT_EV_NAME:
        hc05emu_name_done(e, ev->addr, ev->ok ? ev->name : NULL, now_ms);
        break;
    case BT_EV_SDP:
        if (s_purpose == PURPOSE_NONE) {
            break;
        }
        if (!ev->ok
            || esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER,
                               ev->scn, s_peer) != ESP_OK) {
            procedure_failed(e, now_ms);
        }
        break;
    case BT_EV_CL_INIT:
        if (s_purpose != PURPOSE_NONE && ev->ok) {
            s_cl_handle = ev->handle;
        } else if (s_purpose != PURPOSE_NONE) {
            procedure_failed(e, now_ms);
        }
        break;
    case BT_EV_OPEN:
        if (!ev->ok) {
            if (s_purpose != PURPOSE_NONE) {
                procedure_failed(e, now_ms);
            }
        } else if (s_purpose == PURPOSE_PAIR) {
            close_handle(ev->handle);
            s_purpose = PURPOSE_NONE;
            s_cl_handle = 0;
            hc05emu_pair_done(e, s_peer, true, now_ms);
        } else if (s_purpose == PURPOSE_LINK) {
            s_purpose = PURPOSE_NONE;
            s_cl_handle = 0;
            atomic_store(&s_link, ev->handle);
            s_cong = false;
            s_hold_n = 0;
            hc05emu_link_event(e, ev->addr, true, now_ms);
        } else {
            close_handle(ev->handle); /* nobody asked for it */
        }
        break;
    case BT_EV_CLOSE:
        if (closed_here(ev->handle)) {
            break;
        }
        if (ev->handle != 0u && ev->handle == atomic_load(&s_link)) {
            atomic_store(&s_link, 0u);
            hc05emu_link_event(e, s_peer, false, now_ms);
        } else if (s_purpose != PURPOSE_NONE
                   && (s_cl_handle == 0u || ev->handle == s_cl_handle)) {
            procedure_failed(e, now_ms);
        }
        break;
    case BT_EV_CONG:
        if (ev->handle == atomic_load(&s_link)) {
            s_cong = ev->cong;
            flush_hold();
        }
        break;
    default:
        break;
    }
}

void bridge_bt_write(const uint8_t *b, size_t n)
{
    const uint32_t h = atomic_load(&s_link);
    if (h == 0u || n == 0u) {
        return;
    }
    if (!s_cong && s_hold_n == 0u && esp_spp_write(h, (int)n, (uint8_t *)b)
                                         == ESP_OK) {
        return;
    }
    const size_t room = TX_HOLD - s_hold_n;
    const size_t take = n < room ? n : room;
    memcpy(s_hold + s_hold_n, b, take);
    s_hold_n += take;
    flush_hold();
}

void bridge_bt_forward(bool on)
{
    atomic_store(&s_fwd, on);
}
