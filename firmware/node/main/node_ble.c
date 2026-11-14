/**
 * @file node_ble.c
 * @brief The node's Bluetooth LE (Low Energy) peripheral on NimBLE: the
 *        node service of protocols/rcnode, the advertising, and the
 *        `ble_tx` task that notifies the STREAM packets the role queues.
 *
 * One central at a time.  Advertising runs at a 100ms interval whenever no
 * link stands.  No pairing and no bonding: the readings are measurements,
 * and the panel's host has no security manager.  The panel asks for the
 * connection parameters; the node asks for none of its own.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/** @brief STREAM packets waiting for `ble_tx`: 32 is 640ms of windows. */
#define TX_QUEUE_LEN 32u
/** @brief The advertising interval, ms. */
#define ADV_ITVL_MS 100u

/** @brief One queued STREAM packet. */
typedef struct {
    uint8_t n;                   /**< its length */
    uint8_t b[RCNODE_PKT_MAX];   /**< its bytes */
} tx_item_t;

static ble_uuid128_t s_uuid_svc;      /**< the node service */
static ble_uuid128_t s_uuid_stream;   /**< STREAM */
static ble_uuid128_t s_uuid_status;   /**< STATUS */
static ble_uuid128_t s_uuid_control;  /**< CONTROL */
static ble_uuid128_t s_uuid_info;     /**< INFO */

static uint16_t s_h_stream;   /**< STREAM's value handle */
static uint16_t s_h_status;   /**< STATUS's value handle */

static uint8_t s_role;                          /**< RCNODE_ROLE_* */
static uint8_t s_own_type;                      /**< the address type in use */
static uint8_t s_addr[6];                       /**< the address, LSB first */
static char    s_name[RCNODE_NAME_MAX];         /**< the advertised name */

static atomic_bool     s_linked;      /**< a central is connected */
static atomic_bool     s_stream_sub;  /**< STREAM notifications on */
static atomic_bool     s_stopping;    /**< node_ble_stop() ran: no new advertising */
static atomic_uint     s_conn;        /**< the connection handle */
static atomic_uint     s_dropped;     /**< STREAM packets not sent */
static atomic_uint     s_stream_seq;  /**< the next STREAM seq */
static uint8_t         s_info_seq;    /**< INFO reads; host task only */

static QueueHandle_t s_txq;                       /**< tx_item_t */
static portMUX_TYPE  s_status_mux = portMUX_INITIALIZER_UNLOCKED;  /**< guards s_status */
static uint8_t       s_status[RCNODE_STATUS_LEN]; /**< the last status, encoded */
static uint8_t       s_status_seq;                /**< STATUS packets; node task only */

static int gap_event(struct ble_gap_event *event, void *arg);

/**
 * @brief The GATT (generic attribute profile) access callback of every
 *        characteristic: STATUS and INFO reads, CONTROL writes.
 *
 * @param conn The connection.
 * @param attr The attribute handle.
 * @param ctxt What is asked.
 * @param arg The characteristic's RCNODE_ID_*.
 * @return 0 or an ATT (attribute protocol) error.
 */
static int chr_access(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    const uintptr_t id = (uintptr_t)arg;
    if (id == RCNODE_ID_STATUS && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t b[RCNODE_STATUS_LEN];
        taskENTER_CRITICAL(&s_status_mux);
        memcpy(b, s_status, sizeof b);
        taskEXIT_CRITICAL(&s_status_mux);
        return os_mbuf_append(ctxt->om, b, sizeof b) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (id == RCNODE_ID_INFO && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        rcnode_info_t info;
        memset(&info, 0, sizeof info);
        info.hdr.seq    = s_info_seq;
        s_info_seq = (uint8_t)(s_info_seq + 1u);
        info.hdr.t_node = (uint32_t)node_us();
        info.proto      = RCNODE_PROTO;
        info.role       = s_role;
        info.board      = RCNODE_BOARD_C6_ZERO;
        info.addr_type  = s_own_type == BLE_OWN_ADDR_PUBLIC ? 0u : 1u;
        memcpy(info.addr, s_addr, sizeof s_addr);
        node_role.info(&info);
        uint8_t b[RCNODE_INFO_LEN];
        const size_t n = rcnode_enc_info(&info, b, sizeof b);
        return os_mbuf_append(ctxt->om, b, (uint16_t)n) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (id == RCNODE_ID_CONTROL && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t b[RCNODE_CTL_MAX];
        uint16_t n = 0u;
        if (OS_MBUF_PKTLEN(ctxt->om) > sizeof b
            || ble_hs_mbuf_to_flat(ctxt->om, b, sizeof b, &n) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        rcnode_ctl_t c;
        const rcnode_err_t e = rcnode_dec_ctl(b, n, &c);
        if (e == RCNODE_ERR_LEN) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (e != RCNODE_OK || !rcnode_ctl_for_role(s_role, c.op)) {
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        node_control_post(&c);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/** @brief The node service. */
static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &s_uuid_stream.u,
                .access_cb  = chr_access,
                .arg        = (void *)(uintptr_t)RCNODE_ID_STREAM,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_h_stream,
            },
            {
                .uuid       = &s_uuid_status.u,
                .access_cb  = chr_access,
                .arg        = (void *)(uintptr_t)RCNODE_ID_STATUS,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_h_status,
            },
            {
                .uuid      = &s_uuid_control.u,
                .access_cb = chr_access,
                .arg       = (void *)(uintptr_t)RCNODE_ID_CONTROL,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid      = &s_uuid_info.u,
                .access_cb = chr_access,
                .arg       = (void *)(uintptr_t)RCNODE_ID_INFO,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

/**
 * @brief A NimBLE 128-bit UUID (universally unique identifier) from an
 *        RCNODE_ID_*.
 *
 * @param u Receives the UUID.
 * @param id The id.
 */
static void make_uuid(ble_uuid128_t *u, uint16_t id)
{
    u->u.type = BLE_UUID_TYPE_128;
    rcnode_uuid(id, u->value);
}

/** @brief Start advertising, unless node_ble_stop() ran. */
static void advertise(void)
{
    if (atomic_load(&s_stopping)) {
        return;
    }
    uint8_t ad[RCNODE_ADV_LEN];
    uint8_t rsp[RCNODE_RSP_LEN];
    const size_t n_ad = rcnode_adv_build(s_role, s_addr, ad, sizeof ad);
    const size_t n_rsp = rcnode_rsp_build(rsp, sizeof rsp);
    int rc = ble_gap_adv_set_data(ad, (int)n_ad);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_data(rsp, (int)n_rsp);
    }
    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min  = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MS);
    p.itvl_max  = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MS);
    if (rc == 0) {
        rc = ble_gap_adv_start(s_own_type, NULL, BLE_HS_FOREVER, &p, gap_event,
                               NULL);
    }
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(NODE_TAG, "ble: advertising failed, rc %d", rc);
    }
}

/**
 * @brief GAP (generic access profile) events.
 *
 * @param event The event.
 * @param arg Unused.
 * @return 0.
 */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            atomic_store(&s_conn, event->connect.conn_handle);
            atomic_store(&s_linked, true);
            ESP_LOGI(NODE_TAG, "ble: connected, handle %u",
                     event->connect.conn_handle);
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&s_linked, false);
        atomic_store(&s_stream_sub, false);
        ESP_LOGI(NODE_TAG, "ble: disconnected, reason 0x%03X",
                 event->disconnect.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_h_stream) {
            atomic_store(&s_stream_sub, event->subscribe.cur_notify != 0u);
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(NODE_TAG, "ble: MTU %u", event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

/** @brief The host and the controller are in sync: the address, the name,
 *         the advertising. */
static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_type);
    }
    if (rc == 0) {
        rc = ble_hs_id_copy_addr(s_own_type, s_addr, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(NODE_TAG, "ble: no address, rc %d", rc);
        return;
    }
    (void)rcnode_name(s_role, s_addr, s_name);
    (void)ble_svc_gap_device_name_set(s_name);
    ESP_LOGI(NODE_TAG, "ble: %s at %02X:%02X:%02X:%02X:%02X:%02X", s_name,
             s_addr[5], s_addr[4], s_addr[3], s_addr[2], s_addr[1], s_addr[0]);
    advertise();
}

/**
 * @brief The host reset itself.
 *
 * @param reason NimBLE's reason.
 */
static void on_reset(int reason)
{
    ESP_LOGW(NODE_TAG, "ble: host reset, reason %d", reason);
}

/**
 * @brief NimBLE's host task.
 *
 * @param arg Unused.
 */
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief `ble_tx`: notifies each queued STREAM packet.
 *
 * @param arg Unused.
 */
static void tx_task(void *arg)
{
    (void)arg;
    tx_item_t it;
    for (;;) {
        if (xQueueReceive(s_txq, &it, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!atomic_load(&s_linked) || !atomic_load(&s_stream_sub)) {
            continue;
        }
        /* A notification past the MTU is cut by the stack without an
         * error: one that does not fit is dropped and counted. */
        const uint16_t conn = (uint16_t)atomic_load(&s_conn);
        if (!rcnode_fits_mtu(ble_att_mtu(conn), it.n)) {
            atomic_fetch_add(&s_dropped, 1u);
            continue;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(it.b, (uint16_t)it.n);
        if (om == NULL
            || ble_gatts_notify_custom(conn, s_h_stream, om) != 0) {
            atomic_fetch_add(&s_dropped, 1u);
        }
    }
}

void node_ble_start(uint8_t role)
{
    s_role = role;
    make_uuid(&s_uuid_svc, RCNODE_ID_SERVICE);
    make_uuid(&s_uuid_stream, RCNODE_ID_STREAM);
    make_uuid(&s_uuid_status, RCNODE_ID_STATUS);
    make_uuid(&s_uuid_control, RCNODE_ID_CONTROL);
    make_uuid(&s_uuid_info, RCNODE_ID_INFO);
    s_txq = xQueueCreate(TX_QUEUE_LEN, sizeof(tx_item_t));

    const esp_err_t e = nimble_port_init();
    if (e != ESP_OK || s_txq == NULL) {
        ESP_LOGE(NODE_TAG, "ble: start failed, %s", esp_err_to_name(e));
        return;
    }
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_svcs);
    }
    if (rc != 0) {
        ESP_LOGE(NODE_TAG, "ble: service not added, rc %d", rc);
        return;
    }
    xTaskCreate(tx_task, "ble_tx", 3072, NULL, 6, NULL);
    nimble_port_freertos_init(host_task);
}

void node_ble_stream(const uint8_t *b, size_t n)
{
    if (s_txq == NULL || n > RCNODE_PKT_MAX || !atomic_load(&s_stream_sub)) {
        return;
    }
    tx_item_t it;
    it.n = (uint8_t)n;
    memcpy(it.b, b, n);
    if (xQueueSend(s_txq, &it, 0) != pdTRUE) {
        atomic_fetch_add(&s_dropped, 1u);
    }
}

uint8_t node_ble_stream_seq(void)
{
    return (uint8_t)atomic_fetch_add(&s_stream_seq, 1u);
}

void node_ble_status(rcnode_status_t *s)
{
    s->hdr.seq    = s_status_seq;
    s_status_seq  = (uint8_t)(s_status_seq + 1u);
    s->hdr.t_node = (uint32_t)node_us();
    uint8_t b[RCNODE_STATUS_LEN];
    (void)rcnode_enc_status(s, b, sizeof b);
    taskENTER_CRITICAL(&s_status_mux);
    memcpy(s_status, b, sizeof b);
    taskEXIT_CRITICAL(&s_status_mux);
    if (atomic_load(&s_linked)) {
        ble_gatts_chr_updated(s_h_status);
    }
}

bool node_ble_linked(void)
{
    return atomic_load(&s_linked);
}

bool node_ble_subscribed(void)
{
    return atomic_load(&s_stream_sub);
}

uint16_t node_ble_dropped(void)
{
    return (uint16_t)atomic_load(&s_dropped);
}

bool node_ble_rssi(int8_t *dbm)
{
    if (!atomic_load(&s_linked)) {
        return false;
    }
    return ble_gap_conn_rssi((uint16_t)atomic_load(&s_conn), dbm) == 0;
}

const char *node_ble_name(void)
{
    return s_name;
}

void node_ble_stop(void)
{
    atomic_store(&s_stopping, true);
    (void)ble_gap_adv_stop();
    if (atomic_load(&s_linked)) {
        (void)ble_gap_terminate((uint16_t)atomic_load(&s_conn),
                                BLE_ERR_REM_USER_CONN_TERM);
    }
}
