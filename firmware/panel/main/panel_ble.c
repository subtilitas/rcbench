/**
 * @file panel_ble.c
 * @brief Bluetooth LE on the panel; see panel_ble.h.
 *
 * The host stack is NimBLE (sdkconfig.defaults, "Bluetooth LE through the
 * ESP32-C6"); esp_hosted's VHCI driver passes its HCI packets to the C6
 * over the SDIO link that Wi-Fi uses too.  NimBLE's transport start
 * (ble_transport_ll_init() in esp_hosted's vhci_drv.c) aborts the panel
 * when the SDIO link is not up, so nimble_port_init() runs only after
 * c6_link_up() returned ESP_OK.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_ble.h"

#include <stdatomic.h>
#include <string.h>

#include "c6_link.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/** @brief The start task's stack: the start's log lines and the C6's
 *         requests.  tools/stack_check.py holds it. */
#define BLE_START_STACK 4096u
/** @brief The start task's priority: below the card and the control task. */
#define BLE_START_PRIO 2u
/** @brief Longest scan panel_ble_scan() takes, ms. */
#define BLE_SCAN_MAX_MS 180000u
/** @brief A controller request that took this long went unanswered:
 *         esp_hosted ends each one after DEFAULT_RPC_RSP_TIMEOUT, 5s
 *         (rpc_slave_if.h), ms. */
#define BLE_RPC_SILENT_MS 4500u
/** @brief The first C6 firmware with the controller requests (FeatureControl,
 *         esp_hosted 2.5.2, docs/implemented_rpcs.md), as
 *         ESP_HOSTED_VERSION_VAL() packs it. */
#define BLE_RPC_FIRST ESP_HOSTED_VERSION_VAL(2, 5, 2)

_Static_assert(BLEU_EVT_DATA >= BLE_HS_ADV_MAX_SZ,
               "a legacy advertising report fits an event");
_Static_assert(sizeof(ble_addr_t) == sizeof(((bleu_addr_t *)0)->val) + 1u,
               "NimBLE's address is bleuart's: type and 6 bytes");

/** @brief Bytes of one notification flattened before it is cut into
 *         events: the largest ATT value of BLEU_MTU. */
#define BLE_NOTIFY_MAX (BLEU_MTU - 3u)

/** @brief Log tag. */
static const char *TAG = "ble";

/** @brief panel_ble_state_t, written by the start task and the host task. */
static atomic_int s_state = PANEL_BLE_OFF;
/** @brief The start ran once. */
static atomic_flag s_started = ATOMIC_FLAG_INIT;
/** @brief Events dropped on a full queue. */
static atomic_uint s_dropped;
/** @brief The address type the host uses, set at each sync. */
static atomic_uint s_own_type;
/** @brief When the host task started, esp_timer ms; 0 before. */
static atomic_uint s_host_t0;
/** @brief The host's syncs since start-up. */
static atomic_uint s_syncs;

/** @brief The queue to the IMU's link. */
static QueueHandle_t s_queue;
/** @brief The queue's control block: no heap allocation that can fail. */
static StaticQueue_t s_queue_mem;
/** @brief The queue's elements, 32 x 64 = 2,048 bytes of internal RAM. */
static uint8_t s_queue_buf[PANEL_BLE_QUEUE_LEN * sizeof(bleu_evt_t)];
/** @brief The console scan's report function; NULL when none runs. */
static panel_ble_report_fn s_report;
/** @brief Given at the host's first sync, for the start task's wait. */
static SemaphoreHandle_t s_synced;
/** @brief s_synced's memory. */
static StaticSemaphore_t s_synced_mem;

/** The connect variants, rotating after each failed connect: bleuart's
 *  parameters or NimBLE's defaults (NULL), each with the own address type
 *  the sync inferred and then with the other one.  Diagnostics for a
 *  sensor that fails every connect with HCI 0x3E. */
#define TRY_VARIANTS 4u
/** The next connect's variant, counted up after each failed connect. */
static atomic_uint s_try;
/** A connection was reported up and is not down yet. */
static atomic_bool s_conn_up;
/** The static random address is set for the variants that use it. */
static atomic_bool s_rnd_set;

/** Advertisers whose last report a connect's log line names. */
#define HEARD_MAX 8u

/** @brief One advertiser's last report. */
typedef struct {
    uint8_t  val[6];   /**< its address, least significant byte first */
    uint8_t  type;     /**< its address type */
    int8_t   rssi;     /**< dBm */
    uint32_t at;       /**< ble_now_ms() of the report */
    bool     used;     /**< the slot holds one */
} heard_t;
/** The last report of up to HEARD_MAX advertisers, the oldest replaced. */
static heard_t s_heard[HEARD_MAX];
/** The next slot to replace. */
static unsigned s_heard_next;
/** Guards s_heard: written on nimble_host, read on the IMU's task. */
static portMUX_TYPE s_heard_lock = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief ms since the panel started, 32 bits, wrapping.
 * @return The time.
 */
static uint32_t ble_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Queue one event without waiting; a full queue counts the drop.
 * @param e The event.
 */
static void post(const bleu_evt_t *e)
{
    if (s_queue == NULL || xQueueSend(s_queue, e, 0) != pdTRUE) {
        atomic_fetch_add(&s_dropped, 1u);
    }
}

/**
 * @brief An event of @p kind answering @p token with @p status.
 * @param kind   bleu_ev_kind_t.
 * @param token  The action's token.
 * @param conn   The connection handle.
 * @param status 0, or NimBLE's status.
 */
static void post_answer(uint8_t kind, uint16_t token, uint16_t conn,
                        int status)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof(e));
    e.kind = kind;
    e.token = token;
    e.conn = conn;
    e.status = (uint16_t)status;
    post(&e);
}

/**
 * @brief A token carried through NimBLE's callback argument.
 * @param arg The argument.
 * @return The token.
 */
static uint16_t token_of(void *arg)
{
    return (uint16_t)(uintptr_t)arg;
}

/**
 * @brief A token as a callback argument.
 * @param token The token.
 * @return The argument.
 */
static void *arg_of(uint16_t token)
{
    return (void *)(uintptr_t)token;
}

/**
 * @brief A GATT step's status: 0 for success and for NimBLE's end of a
 *        discovery (BLE_HS_EDONE).
 * @param err NimBLE's error; NULL is success.
 * @return The status.
 */
static int gatt_status(const struct ble_gatt_error *err)
{
    return (err == NULL || err->status == BLE_HS_EDONE) ? 0 : err->status;
}

/**
 * @brief A UUID as bleuart's event data: 2 or 16 bytes, least significant
 *        byte first.
 * @param u The UUID.
 * @param e Takes data and len.
 */
static void put_uuid(const ble_uuid_any_t *u, bleu_evt_t *e)
{
    if (u->u.type == BLE_UUID_TYPE_16) {
        e->data[0] = (uint8_t)(u->u16.value & 0xFFu);
        e->data[1] = (uint8_t)(u->u16.value >> 8);
        e->len = 2u;
    } else if (u->u.type == BLE_UUID_TYPE_128) {
        memcpy(e->data, u->u128.value, 16u);
        e->len = 16u;
    } else {
        e->len = 0u;
    }
}

/**
 * @brief The host's sync: the C6 answered the HCI reset and the reads
 *        after it.  Runs on nimble_host.
 */
static void on_sync(void)
{
    uint8_t type = BLE_OWN_ADDR_PUBLIC;
    const int rc = ble_hs_id_infer_auto(0, &type);
    if (rc != 0) {
        ESP_LOGW(TAG, "no own address: %d", rc);
    }
    atomic_store(&s_own_type, type);
    atomic_store(&s_state, PANEL_BLE_READY);
    if (atomic_fetch_add(&s_syncs, 1u) == 0u) {
        ESP_LOGI(TAG, "synced in %ums, own address type %u",
                 (unsigned)(ble_now_ms() - atomic_load(&s_host_t0)),
                 (unsigned)type);
    } else {
        ESP_LOGI(TAG, "synced again, own address type %u", (unsigned)type);
    }
    (void)xSemaphoreGive(s_synced);
}

/**
 * @brief The host lost its sync, for a controller that went silent or an
 *        HCI error; NimBLE resyncs on its own.  Runs on nimble_host.
 * @param reason NimBLE's reason.
 */
static void on_reset(int reason)
{
    atomic_store(&s_state, PANEL_BLE_NO_SYNC);
    ESP_LOGW(TAG, "host reset, reason %d", reason);
}

/**
 * @brief The host task: NimBLE's event loop, which never returns, as the
 *        host is never stopped.
 * @param arg Unused.
 */
static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief Keep a report's RSSI for the connect's log line.
 * @param d NimBLE's report.
 */
static void heard(const struct ble_gap_disc_desc *d)
{
    portENTER_CRITICAL(&s_heard_lock);
    unsigned i = 0;
    while (i < HEARD_MAX
           && !(s_heard[i].used && s_heard[i].type == d->addr.type
                && memcmp(s_heard[i].val, d->addr.val, 6u) == 0)) {
        ++i;
    }
    if (i == HEARD_MAX) {
        i = s_heard_next;
        s_heard_next = (s_heard_next + 1u) % HEARD_MAX;
    }
    s_heard[i].type = d->addr.type;
    memcpy(s_heard[i].val, d->addr.val, 6u);
    s_heard[i].rssi = d->rssi;
    s_heard[i].at = ble_now_ms();
    s_heard[i].used = true;
    portEXIT_CRITICAL(&s_heard_lock);
}

/**
 * @brief The last report of @p a.
 * @param a    The address.
 * @param rssi Takes its RSSI, dBm.
 * @param age  Takes its age, ms.
 * @return Whether one was heard since the start.
 */
static bool last_heard(const ble_addr_t *a, int8_t *rssi, uint32_t *age)
{
    bool found = false;
    portENTER_CRITICAL(&s_heard_lock);
    for (unsigned i = 0; i < HEARD_MAX && !found; ++i) {
        if (s_heard[i].used && s_heard[i].type == a->type
            && memcmp(s_heard[i].val, a->val, 6u) == 0) {
            *rssi = s_heard[i].rssi;
            *age = ble_now_ms() - s_heard[i].at;
            found = true;
        }
    }
    portEXIT_CRITICAL(&s_heard_lock);
    return found;
}

/**
 * @brief An advertising report as bleuart's event.
 * @param d NimBLE's report.
 * @param e Takes it.
 */
static void adv_event(const struct ble_gap_disc_desc *d, bleu_evt_t *e)
{
    heard(d);
    memset(e, 0, sizeof(*e));
    e->kind = BLEU_EV_ADV;
    e->rssi = d->rssi;
    e->status = d->event_type;
    e->addr.type = d->addr.type;
    memcpy(e->addr.val, d->addr.val, sizeof(e->addr.val));
    e->len = d->length_data <= BLEU_EVT_DATA ? d->length_data
                                             : (uint8_t)BLEU_EVT_DATA;
    if (d->data != NULL) {
        memcpy(e->data, d->data, e->len);
    } else {
        e->len = 0u;
    }
}

/**
 * @brief The IMU's scan: its reports and its end into the queue.  Runs on
 *        nimble_host.
 * @param ev  NimBLE's event.
 * @param arg The scan's token.
 * @return 0.
 */
static int scan_event(struct ble_gap_event *ev, void *arg)
{
    bleu_evt_t e;
    if (ev->type == BLE_GAP_EVENT_DISC) {
        adv_event(&ev->disc, &e);
        post(&e);
    } else if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGI(TAG, "scan done, reason %d", ev->disc_complete.reason);
        post_answer(BLEU_EV_SCAN_END, token_of(arg), 0xFFFFu,
                    ev->disc_complete.reason);
    }
    return 0;
}

/**
 * @brief A console scan's events to its report function.  Runs on
 *        nimble_host.
 * @param ev  NimBLE's event.
 * @param arg Unused.
 * @return 0.
 */
static int console_scan_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    const panel_ble_report_fn report = s_report;
    bleu_evt_t e;
    if (report == NULL) {
        return 0;
    }
    if (ev->type == BLE_GAP_EVENT_DISC) {
        adv_event(&ev->disc, &e);
        report(&e);
    } else if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        memset(&e, 0, sizeof(e));
        e.kind = BLEU_EV_SCAN_END;
        e.status = (uint16_t)ev->disc_complete.reason;
        s_report = NULL;
        report(&e);
    }
    return 0;
}

/**
 * @brief A connection's parameters into @p e's h1, h2 and h3: interval,
 *        latency, supervision timeout.
 * @param conn The connection.
 * @param e    Takes them.
 */
static void put_params(uint16_t conn, bleu_evt_t *e)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) == 0) {
        e->h1 = d.conn_itvl;
        e->h2 = d.conn_latency;
        e->h3 = d.supervision_timeout;
    }
}

/**
 * @brief A notification, flattened and cut into events of BLEU_EVT_DATA
 *        bytes, in order.
 * @param ev NimBLE's notification event.
 */
static void notify_events(const struct ble_gap_event *ev)
{
    static uint8_t flat[BLE_NOTIFY_MAX];  /* the host task's own */
    uint16_t len = 0u;
    if (ble_hs_mbuf_to_flat(ev->notify_rx.om, flat, sizeof(flat), &len)
        != 0) {
        len = (uint16_t)(len > sizeof(flat) ? sizeof(flat) : len);
    }
    for (uint16_t at = 0u; at < len; at = (uint16_t)(at + BLEU_EVT_DATA)) {
        bleu_evt_t e;
        memset(&e, 0, sizeof(e));
        e.kind = BLEU_EV_NOTIFY;
        e.conn = ev->notify_rx.conn_handle;
        e.h1 = ev->notify_rx.attr_handle;
        e.len = (uint8_t)((len - at) < BLEU_EVT_DATA ? (len - at)
                                                     : BLEU_EVT_DATA);
        memcpy(e.data, &flat[at], e.len);
        post(&e);
    }
}

/**
 * @brief Log a connection's record from NimBLE.
 * @param what The line's head.
 * @param d    The record.
 */
static void log_desc(const char *what, const struct ble_gap_conn_desc *d)
{
    ESP_LOGI(TAG, "%s: handle %u, peer %02X:%02X:%02X:%02X:%02X:%02X type "
                  "%u (over the air type %u), own type %u, role %u, "
                  "interval %u x 1.25ms, latency %u, timeout %u x 10ms, "
                  "clock accuracy %u",
             what, (unsigned)d->conn_handle, d->peer_id_addr.val[5],
             d->peer_id_addr.val[4], d->peer_id_addr.val[3],
             d->peer_id_addr.val[2], d->peer_id_addr.val[1],
             d->peer_id_addr.val[0], (unsigned)d->peer_id_addr.type,
             (unsigned)d->peer_ota_addr.type, (unsigned)d->our_ota_addr.type,
             (unsigned)d->role, (unsigned)d->conn_itvl,
             (unsigned)d->conn_latency, (unsigned)d->supervision_timeout,
             (unsigned)d->master_clock_accuracy);
}

/**
 * @brief A connect that failed: the next one takes the next variant.
 * @param why The status or reason.
 */
static void try_failed(int why)
{
    const unsigned next = atomic_fetch_add(&s_try, 1u) + 1u;
    ESP_LOGI(TAG, "connect failed with 0x%X: the next try is variant %u",
             (unsigned)why, next % TRY_VARIANTS);
}

/**
 * @brief A connection's events into the queue.  Runs on nimble_host.
 * @param ev  NimBLE's event.
 * @param arg The connect's token.
 * @return 0: a parameter update the sensor asks for is accepted.
 */
static int conn_event(struct ble_gap_event *ev, void *arg)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof(e));
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        e.kind = BLEU_EV_CONNECT;
        e.token = token_of(arg);
        e.status = (uint16_t)ev->connect.status;
        e.conn = ev->connect.conn_handle;
        ESP_LOGI(TAG, "connect: status 0x%X, handle %u, token %u",
                 (unsigned)ev->connect.status, (unsigned)e.conn,
                 (unsigned)e.token);
        if (ev->connect.status == 0) {
            atomic_store(&s_conn_up, true);
            put_params(e.conn, &e);
        } else {
            try_failed(ev->connect.status);
        }
        {
            struct ble_gap_conn_desc d;
            if (ble_gap_conn_find(e.conn, &d) == 0) {
                log_desc("connect: record", &d);
            } else {
                ESP_LOGI(TAG, "connect: no record of handle %u",
                         (unsigned)e.conn);
            }
        }
        post(&e);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        /* The connect's token: a connection that failed to be
         * established (HCI 0x3E) ends the connect with it. */
        e.kind = BLEU_EV_DISCONNECT;
        e.token = token_of(arg);
        e.status = (uint16_t)ev->disconnect.reason;
        e.conn = ev->disconnect.conn.conn_handle;
        ESP_LOGI(TAG, "disconnect: reason 0x%X, handle %u, token %u",
                 (unsigned)ev->disconnect.reason, (unsigned)e.conn,
                 (unsigned)e.token);
        log_desc("disconnect: record", &ev->disconnect.conn);
        if (!atomic_exchange(&s_conn_up, false)) {
            try_failed(ev->disconnect.reason);
        }
        post(&e);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        e.kind = BLEU_EV_CONN_UPDATE;
        e.status = (uint16_t)ev->conn_update.status;
        e.conn = ev->conn_update.conn_handle;
        put_params(e.conn, &e);
        ESP_LOGI(TAG, "connection update: status %d, interval %u, latency "
                      "%u, timeout %u", ev->conn_update.status,
                 (unsigned)e.h1, (unsigned)e.h2, (unsigned)e.h3);
        post(&e);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        ESP_LOGI(TAG, "the sensor asks interval %u-%u, latency %u, timeout "
                      "%u: accepted",
                 (unsigned)ev->conn_update_req.peer_params->itvl_min,
                 (unsigned)ev->conn_update_req.peer_params->itvl_max,
                 (unsigned)ev->conn_update_req.peer_params->latency,
                 (unsigned)ev->conn_update_req.peer_params->supervision_timeout);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        notify_events(ev);
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %u", (unsigned)ev->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

/**
 * @brief Service discovery's answers.  Runs on nimble_host.
 * @param conn The connection.
 * @param err  NimBLE's status.
 * @param svc  The service found, or NULL at the end.
 * @param arg  The token.
 * @return 0.
 */
static int svc_cb(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_svc *svc, void *arg)
{
    if (err != NULL && err->status == 0 && svc != NULL) {
        bleu_evt_t e;
        memset(&e, 0, sizeof(e));
        e.kind = BLEU_EV_SVC;
        e.token = token_of(arg);
        e.conn = conn;
        e.h1 = svc->start_handle;
        e.h2 = svc->end_handle;
        post(&e);
        return 0;
    }
    post_answer(BLEU_EV_SVC_DONE, token_of(arg), conn, gatt_status(err));
    return 0;
}

/**
 * @brief Characteristic discovery's answers.  Runs on nimble_host.
 * @param conn The connection.
 * @param err  NimBLE's status.
 * @param chr  The characteristic, or NULL at the end.
 * @param arg  The token.
 * @return 0.
 */
static int chr_cb(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_chr *chr, void *arg)
{
    if (err != NULL && err->status == 0 && chr != NULL) {
        bleu_evt_t e;
        memset(&e, 0, sizeof(e));
        e.kind = BLEU_EV_CHR;
        e.token = token_of(arg);
        e.conn = conn;
        e.h1 = chr->def_handle;
        e.h2 = chr->val_handle;
        e.props = chr->properties;
        put_uuid(&chr->uuid, &e);
        post(&e);
        return 0;
    }
    post_answer(BLEU_EV_CHR_DONE, token_of(arg), conn, gatt_status(err));
    return 0;
}

/**
 * @brief Descriptor discovery's answers.  Runs on nimble_host.
 * @param conn    The connection.
 * @param err     NimBLE's status.
 * @param chr_val The characteristic's value handle.
 * @param dsc     The descriptor, or NULL at the end.
 * @param arg     The token.
 * @return 0.
 */
static int dsc_cb(uint16_t conn, const struct ble_gatt_error *err,
                  uint16_t chr_val, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val;
    if (err != NULL && err->status == 0 && dsc != NULL) {
        bleu_evt_t e;
        memset(&e, 0, sizeof(e));
        e.kind = BLEU_EV_DSC;
        e.token = token_of(arg);
        e.conn = conn;
        e.h1 = dsc->handle;
        put_uuid(&dsc->uuid, &e);
        post(&e);
        return 0;
    }
    post_answer(BLEU_EV_DSC_DONE, token_of(arg), conn, gatt_status(err));
    return 0;
}

/**
 * @brief The MTU exchange's answer.  Runs on nimble_host.
 * @param conn The connection.
 * @param err  NimBLE's status.
 * @param mtu  The MTU.
 * @param arg  The token.
 * @return 0.
 */
static int mtu_cb(uint16_t conn, const struct ble_gatt_error *err,
                  uint16_t mtu, void *arg)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof(e));
    e.kind = BLEU_EV_MTU;
    e.token = token_of(arg);
    e.conn = conn;
    e.status = (uint16_t)gatt_status(err);
    e.h1 = mtu;
    ESP_LOGI(TAG, "MTU exchange: status %d, MTU %u", gatt_status(err),
             (unsigned)mtu);
    post(&e);
    return 0;
}

/**
 * @brief A write's answer.  Runs on nimble_host.
 * @param conn The connection.
 * @param err  NimBLE's status.
 * @param attr The attribute.
 * @param arg  The token.
 * @return 0.
 */
static int write_cb(uint16_t conn, const struct ble_gatt_error *err,
                    struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    post_answer(BLEU_EV_WRITE_DONE, token_of(arg), conn, gatt_status(err));
    return 0;
}

/**
 * @brief A read's answer, its value in the event's data.  Runs on
 *        nimble_host.
 * @param conn The connection.
 * @param err  NimBLE's status.
 * @param attr The attribute and its value.
 * @param arg  The token.
 * @return 0.
 */
static int read_cb(uint16_t conn, const struct ble_gatt_error *err,
                   struct ble_gatt_attr *attr, void *arg)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof(e));
    e.kind = BLEU_EV_READ;
    e.token = token_of(arg);
    e.conn = conn;
    e.status = (uint16_t)gatt_status(err);
    if (e.status == 0u && attr != NULL && attr->om != NULL) {
        uint16_t len = 0u;
        (void)ble_hs_mbuf_to_flat(attr->om, e.data, BLEU_EVT_DATA, &len);
        e.len = (uint8_t)(len > BLEU_EVT_DATA ? BLEU_EVT_DATA : len);
        ESP_LOGI(TAG, "6DAA: %u bytes %02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned)e.len, e.data[0], e.data[1], e.data[2], e.data[3],
                 e.data[4], e.data[5], e.data[6], e.data[7]);
    }
    post(&e);
    return 0;
}

/**
 * @brief The C6's controller init and enable requests, each timed.
 *
 * esp_hosted 2.5.2 and later leave the C6's controller off until the host
 * asks; earlier firmware has no such requests and starts its controller
 * itself.  A request the C6 does not know is answered with an error or not
 * at all; esp_hosted ends an unanswered one after 5s.  After an unanswered
 * init the enable is not sent: the C6 does not know that request either.
 * The start sends them to a C6 that reports 2.5.2 or later, and to any C6
 * whose host has no sync without them (c6_has_requests()).
 */
static void controller_requests(void)
{
    const uint32_t t0 = ble_now_ms();
    const esp_err_t ei = esp_hosted_bt_controller_init();
    const uint32_t t1 = ble_now_ms();
    if (ei != ESP_OK && t1 - t0 >= BLE_RPC_SILENT_MS) {
        ESP_LOGW(TAG, "C6 controller init not answered in %ums: a C6 "
                      "firmware before 2.5.2, its controller runs from its "
                      "own start", (unsigned)(t1 - t0));
        return;
    }
    const esp_err_t ee = esp_hosted_bt_controller_enable();
    const uint32_t t2 = ble_now_ms();
    if (ei == ESP_OK && ee == ESP_OK) {
        ESP_LOGI(TAG, "C6 controller init and enable answered in %ums and "
                      "%ums", (unsigned)(t1 - t0), (unsigned)(t2 - t1));
    } else {
        ESP_LOGW(TAG, "C6 controller init %s in %ums, enable %s in %ums: "
                      "the host start goes on",
                 esp_err_to_name(ei), (unsigned)(t1 - t0),
                 esp_err_to_name(ee), (unsigned)(t2 - t1));
    }
}

/**
 * @brief Whether the C6's firmware has the controller requests.
 *
 * The version request exists since esp_hosted 1.0.0 and the controller
 * requests since 2.5.2 (docs/implemented_rpcs.md): a C6 that answers no
 * version request, such as the board's 0.0.0, has neither, and its
 * controller runs from its own start.
 *
 * @return true for a reported version of 2.5.2 or later.
 */
static bool c6_has_requests(void)
{
    c6_link_version_t v;
    if (!c6_link_version(&v)) {
        ESP_LOGI(TAG, "C6 firmware version not reported: older than 2.5.2, "
                      "no controller requests; its controller runs from its "
                      "own start");
        return false;
    }
    if (ESP_HOSTED_VERSION_VAL(v.major, v.minor, v.patch) < BLE_RPC_FIRST) {
        ESP_LOGI(TAG, "C6 firmware %u.%u.%u: older than 2.5.2, no controller "
                      "requests; its controller runs from its own start",
                 (unsigned)v.major, (unsigned)v.minor, (unsigned)v.patch);
        return false;
    }
    return true;
}

/**
 * @brief The start task: the link, the controller, the host, the sync.
 *        Ends when the wait for the sync has ended.
 * @param arg Unused.
 */
static void ble_start_task(void *arg)
{
    (void)arg;
    const size_t in0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t ps0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (c6_link_up() != ESP_OK) {
        atomic_store(&s_state, PANEL_BLE_NO_LINK);
        ESP_LOGE(TAG, "no link to the C6: no Bluetooth until the panel "
                      "restarts");
        vTaskDelete(NULL);
        return;
    }
    const bool requests = c6_has_requests();
    if (requests) {
        controller_requests();
    }
    const esp_err_t e = nimble_port_init();
    if (e != ESP_OK) {
        atomic_store(&s_state, PANEL_BLE_NO_SYNC);
        ESP_LOGE(TAG, "nimble_port_init: %s; no Bluetooth until the panel "
                      "restarts", esp_err_to_name(e));
        vTaskDelete(NULL);
        return;
    }
    (void)ble_att_set_preferred_mtu(BLEU_MTU);
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    atomic_store(&s_host_t0, ble_now_ms());
    nimble_port_freertos_init(ble_host_task);
    bool synced =
        xSemaphoreTake(s_synced, pdMS_TO_TICKS(PANEL_BLE_SYNC_MS)) == pdTRUE;
    if (!synced && !requests) {
        /* A C6 that is newer than its version answer said keeps its
         * controller off until asked; the host's resets go on meanwhile. */
        ESP_LOGW(TAG, "no sync within %ums without the controller requests: "
                      "sending them", (unsigned)PANEL_BLE_SYNC_MS);
        controller_requests();
        synced = xSemaphoreTake(s_synced, pdMS_TO_TICKS(PANEL_BLE_SYNC_MS))
                 == pdTRUE;
    }
    if (!synced) {
        /* A sync after this still makes the state READY (on_sync). */
        int expect = PANEL_BLE_STARTING;
        (void)atomic_compare_exchange_strong(&s_state, &expect,
                                             PANEL_BLE_NO_SYNC);
        ESP_LOGW(TAG, "no sync within %ums: the C6 answers no HCI; the host "
                      "keeps trying", (unsigned)PANEL_BLE_SYNC_MS);
    }
    ESP_LOGI(TAG, "host started; internal RAM %uB free before, %uB after; "
                  "PSRAM %uB before, %uB after",
             (unsigned)in0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)ps0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    vTaskDelete(NULL);
}

void panel_ble_start(void)
{
    if (atomic_flag_test_and_set(&s_started)) {
        return;
    }
    s_queue = xQueueCreateStatic(PANEL_BLE_QUEUE_LEN, sizeof(bleu_evt_t),
                                 s_queue_buf, &s_queue_mem);
    s_synced = xSemaphoreCreateBinaryStatic(&s_synced_mem);
    atomic_store(&s_state, PANEL_BLE_STARTING);
    if (xTaskCreatePinnedToCore(ble_start_task, "blestart", BLE_START_STACK,
                                NULL, BLE_START_PRIO, NULL, 0) != pdPASS) {
        atomic_store(&s_state, PANEL_BLE_NO_SYNC);
        ESP_LOGE(TAG, "no memory for the start task: no Bluetooth until "
                      "the panel restarts");
    }
}

panel_ble_state_t panel_ble_state(void)
{
    return (panel_ble_state_t)atomic_load(&s_state);
}

bool panel_ble_get(bleu_evt_t *out)
{
    return s_queue != NULL && out != NULL
           && xQueueReceive(s_queue, out, 0) == pdTRUE;
}

uint32_t panel_ble_dropped(void)
{
    return atomic_load(&s_dropped);
}

esp_err_t panel_ble_scan(uint32_t ms, panel_ble_report_fn report)
{
    if (ms == 0u || ms > BLE_SCAN_MAX_MS || report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_load(&s_state) != PANEL_BLE_READY || ble_gap_disc_active()
        || ble_gap_conn_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Active, the controller's duplicate filter on; NimBLE's default
     * interval and window. */
    const struct ble_gap_disc_params p = {
        .passive = 0,
        .filter_duplicates = 1,
    };
    s_report = report;
    const int rc = ble_gap_disc((uint8_t)atomic_load(&s_own_type), (int32_t)ms,
                                &p, console_scan_event, NULL);
    if (rc != 0) {
        s_report = NULL;
        ESP_LOGW(TAG, "scan refused: %d", rc);
        return rc == BLE_HS_EALREADY || rc == BLE_HS_EBUSY
                   ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief The own address type of a variant: the inferred one, or for
 *        variants 2 and 3 the other one.  Random means a static random
 *        address, set once.
 * @param v The variant.
 * @return The type.
 */
static uint8_t own_type_of(unsigned v)
{
    const uint8_t inferred = (uint8_t)atomic_load(&s_own_type);
    if (v < 2u) {
        return inferred;
    }
    if (inferred != BLE_OWN_ADDR_PUBLIC) {
        return BLE_OWN_ADDR_PUBLIC;
    }
    if (!atomic_load(&s_rnd_set)) {
        ble_addr_t rnd;
        int rc = ble_hs_id_gen_rnd(0, &rnd);
        if (rc == 0) {
            rc = ble_hs_id_set_rnd(rnd.val);
        }
        ESP_LOGI(TAG, "connect: static random address "
                      "%02X:%02X:%02X:%02X:%02X:%02X set: %d",
                 rnd.val[5], rnd.val[4], rnd.val[3], rnd.val[2], rnd.val[1],
                 rnd.val[0], rc);
        atomic_store(&s_rnd_set, rc == 0);
    }
    return BLE_OWN_ADDR_RANDOM;
}

/**
 * @brief A connect to @p a in the current variant (s_try): bleuart's
 *        parameters or NimBLE's defaults, the inferred own address type or
 *        the other.  Every value goes to the log.
 * @param a The action.
 * @return NimBLE's status.
 */
static int do_connect(const bleu_act_t *a)
{
    ble_addr_t peer;
    peer.type = a->addr.type;
    memcpy(peer.val, a->addr.val, sizeof(peer.val));
    const struct ble_gap_conn_params cp = {
        .scan_itvl = BLEU_CONN_SCAN_ITVL,
        .scan_window = BLEU_CONN_SCAN_WINDOW,
        .itvl_min = BLEU_ITVL_MIN,
        .itvl_max = BLEU_ITVL_MAX,
        .latency = BLEU_LATENCY,
        .supervision_timeout = BLEU_SUPERVISION,
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    const unsigned v = atomic_load(&s_try) % TRY_VARIANTS;
    const bool dflt = (v % 2u) != 0u;
    const uint8_t own = own_type_of(v);
    ESP_LOGI(TAG, "connect to %02X:%02X:%02X:%02X:%02X:%02X type %u for %ums",
             peer.val[5], peer.val[4], peer.val[3], peer.val[2], peer.val[1],
             peer.val[0], (unsigned)peer.type, (unsigned)a->ms);
    ESP_LOGI(TAG, "connect: variant %u, own address %s (type %u), %s",
             v, own == BLE_OWN_ADDR_PUBLIC ? "public" : "random",
             (unsigned)own, dflt ? "NimBLE's default parameters"
                                 : "bleuart's parameters");
    if (dflt) {
        ESP_LOGI(TAG, "connect: interval %u-%u x 1.25ms, latency %u, "
                      "timeout %u x 10ms, scan 16/16 x 0.625ms",
                 (unsigned)BLE_GAP_INITIAL_CONN_ITVL_MIN,
                 (unsigned)BLE_GAP_INITIAL_CONN_ITVL_MAX,
                 (unsigned)BLE_GAP_INITIAL_CONN_LATENCY,
                 (unsigned)BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
    } else {
        ESP_LOGI(TAG, "connect: interval %u-%u x 1.25ms, latency %u, "
                      "timeout %u x 10ms, scan %u/%u x 0.625ms",
                 (unsigned)cp.itvl_min, (unsigned)cp.itvl_max,
                 (unsigned)cp.latency, (unsigned)cp.supervision_timeout,
                 (unsigned)cp.scan_itvl, (unsigned)cp.scan_window);
    }
    int8_t rssi = 0;
    uint32_t age = 0;
    if (last_heard(&peer, &rssi, &age)) {
        ESP_LOGI(TAG, "connect: last report %ddBm, %ums ago", (int)rssi,
                 (unsigned)age);
    } else {
        ESP_LOGI(TAG, "connect: no report heard since the start");
    }
    atomic_store(&s_conn_up, false);
    const int rc = ble_gap_connect(own, &peer, (int32_t)a->ms,
                                   dflt ? NULL : &cp, conn_event,
                                   arg_of(a->token));
    if (rc != 0) {
        try_failed(rc);
    }
    return rc;
}

esp_err_t panel_ble_controller(void)
{
    const panel_ble_state_t st = panel_ble_state();
    if (st == PANEL_BLE_OFF || st == PANEL_BLE_STARTING) {
        return ESP_ERR_INVALID_STATE;
    }
    controller_requests();
    return ESP_OK;
}

void panel_ble_do(const bleu_act_t *a)
{
    if (a == NULL) {
        return;
    }
    ble_uuid128_t svc = { .u = { .type = BLE_UUID_TYPE_128 } };
    memcpy(svc.value, bleu_uuid_svc, sizeof(svc.value));
    const struct ble_gap_disc_params dp = {
        .passive = 0,
        .filter_duplicates = 1,
    };
    int rc = 0;
    uint8_t answer = BLEU_EV_NONE;
    switch (a->kind) {
    case BLEU_ACT_SCAN:
        ESP_LOGI(TAG, "scan for %ums", (unsigned)a->ms);
        rc = ble_gap_disc((uint8_t)atomic_load(&s_own_type), (int32_t)a->ms,
                          &dp, scan_event, arg_of(a->token));
        answer = BLEU_EV_SCAN_END;
        break;
    case BLEU_ACT_SCAN_STOP:
        (void)ble_gap_disc_cancel();
        break;
    case BLEU_ACT_CONNECT:
        rc = do_connect(a);
        answer = BLEU_EV_CONNECT;
        break;
    case BLEU_ACT_CANCEL:
        (void)ble_gap_conn_cancel();
        break;
    case BLEU_ACT_DISCONNECT:
        (void)ble_gap_terminate(a->conn, BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLEU_ACT_DISC_SVC:
        rc = ble_gattc_disc_svc_by_uuid(a->conn, &svc.u, svc_cb,
                                        arg_of(a->token));
        answer = BLEU_EV_SVC_DONE;
        break;
    case BLEU_ACT_DISC_CHRS:
        rc = ble_gattc_disc_all_chrs(a->conn, a->h1, a->h2, chr_cb,
                                     arg_of(a->token));
        answer = BLEU_EV_CHR_DONE;
        break;
    case BLEU_ACT_DISC_DSCS:
        rc = ble_gattc_disc_all_dscs(a->conn, a->h1, a->h2, dsc_cb,
                                     arg_of(a->token));
        answer = BLEU_EV_DSC_DONE;
        break;
    case BLEU_ACT_MTU:
        rc = ble_gattc_exchange_mtu(a->conn, mtu_cb, arg_of(a->token));
        answer = BLEU_EV_MTU;
        break;
    case BLEU_ACT_SUBSCRIBE:
    case BLEU_ACT_WRITE:
        rc = ble_gattc_write_flat(a->conn, a->h1, a->data, a->len, write_cb,
                                  arg_of(a->token));
        answer = BLEU_EV_WRITE_DONE;
        break;
    case BLEU_ACT_WRITE_NR:
        rc = ble_gattc_write_no_rsp_flat(a->conn, a->h1, a->data, a->len);
        post_answer(BLEU_EV_WRITE_DONE, a->token, a->conn, rc);
        return;
    case BLEU_ACT_READ:
        rc = ble_gattc_read(a->conn, a->h1, read_cb, arg_of(a->token));
        answer = BLEU_EV_READ;
        break;
    case BLEU_ACT_NONE:
    default:
        return;
    }
    if (rc != 0 && answer != BLEU_EV_NONE) {
        ESP_LOGW(TAG, "action %u refused: %d", (unsigned)a->kind, rc);
        post_answer(answer, a->token, a->conn, rc);
    }
}
