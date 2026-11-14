/**
 * @file
 * @brief The Dial's NimBLE peripheral: the dialknob GATT service, the
 *        advertising, the bound panel and the connection parameters.
 *
 * The service (protocols/dialknob, README "The service"): EVENT notify,
 * STATUS notify and read, SCREEN write without response, INFO read.  The
 * Dial advertises the service UUID with its name `DIAL-xxxx` in the scan
 * response: every 100ms to anyone while it holds no bound panel, every
 * 500ms to its bound panel only, through the controller's accept list,
 * once it holds one.
 *
 * The bind: the first central that subscribes to EVENT is the bound panel;
 * its address goes to NVS (non-volatile storage, namespace "dial", key
 * "panel") and to the accept list.  A 10,000ms hold of the screen button
 * forgets it (dial_ble_forget()).  The panel's own address is taken as the
 * controller reports it; the panel runs no privacy, so the address does not
 * change.
 *
 * Connection parameters: the central's at the connect; once the panel
 * subscribes to EVENT the Dial asks for 11.25 to 15ms, latency 0,
 * supervision 1,000ms; after 10,000ms without a turn it asks for latency
 * 12, and the next turn asks for 0 again.  Whether the panel grants them is
 * not measured.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "dial.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/** @brief The panel's packets waiting for the face task. */
#define PANEL_QUEUE 32u
/** @brief NVS namespace and key of the bound panel. @{ */
#define NVS_NS  "dial"
#define NVS_KEY "panel" /**< the key */
/** @} */
/** @brief No connection. */
#define CONN_NONE 0xFFFFu

/** @brief The log's tag. */
static const char *TAG = "ble";

/** @brief The service's UUID. */
static const ble_uuid128_t s_uuid_svc =
    BLE_UUID128_INIT(DK_UUID_LE(DK_UUID_SVC));
/** @brief EVENT's UUID. */
static const ble_uuid128_t s_uuid_event =
    BLE_UUID128_INIT(DK_UUID_LE(DK_UUID_EVENT));
/** @brief STATUS's UUID. */
static const ble_uuid128_t s_uuid_status =
    BLE_UUID128_INIT(DK_UUID_LE(DK_UUID_STATUS));
/** @brief SCREEN's UUID. */
static const ble_uuid128_t s_uuid_screen =
    BLE_UUID128_INIT(DK_UUID_LE(DK_UUID_SCREEN));
/** @brief INFO's UUID. */
static const ble_uuid128_t s_uuid_info =
    BLE_UUID128_INIT(DK_UUID_LE(DK_UUID_INFO));

/** @brief The characteristics' value handles, written by NimBLE at
 *         registration. @{ */
static uint16_t s_h_event;
static uint16_t s_h_status; /**< STATUS */
static uint16_t s_h_screen; /**< SCREEN */
static uint16_t s_h_info; /**< INFO */
/** @} */

/** @brief One packet from the panel. */
typedef struct {
    uint8_t len;               /**< its length */
    uint8_t b[DK_PKT_MAX];     /**< its bytes */
} panel_pkt_t;

/** @brief The link's state, written by the host task and read by the
 *         others. @{ */
static atomic_uint_least16_t s_conn = CONN_NONE;
static atomic_bool s_sub_event; /**< the panel subscribed to EVENT */
static atomic_bool s_sub_status; /**< the panel subscribed to STATUS */
static atomic_uint_least32_t s_links; /**< connections since start */
static atomic_uint_least16_t s_echo; /**< the last STATE's panel ms */
static atomic_bool s_idle; /**< the link runs at DK_LATENCY_IDLE */
static atomic_uint_least32_t s_last_turn; /**< the last turn, ms */
/** @} */
/** @brief The bound panel. @{ */
static atomic_bool s_bound;
static ble_addr_t s_panel; /**< the bound panel's address */
/** @} */
/** @brief The Dial's own address and its type. @{ */
static uint8_t s_own_type;
static uint8_t s_own[6]; /**< the address, least significant byte first */
static char s_name[DK_NAME_MAX + 1u] = "DIAL"; /**< the advertised name */
/** @} */
/** @brief The STATUS value a read returns. @{ */
static uint8_t s_status[DK_LEN_STATUS];
static size_t s_status_len; /**< the value's length */
/** @} */
/** @brief The panel's packets for the face task. */
static QueueHandle_t s_rx;

static int gap_event(struct ble_gap_event *ev, void *arg);

/**
 * @brief The INFO value.
 * @param out DK_LEN_INFO bytes
 * @return its length, or 0
 */
static size_t info_value(uint8_t *out)
{
    dk_info_t i;
    unsigned v[3] = { 0, 0, 0 };
    memset(&i, 0, sizeof i);
    sscanf(esp_app_get_description()->version, "%u.%u.%u", &v[0], &v[1],
           &v[2]);
    i.proto = DK_PROTOCOL;
    for (unsigned k = 0; k < 3u; ++k) {
        i.fw[k] = (uint8_t)(v[k] > 255u ? 255u : v[k]);
    }
    i.flags = (uint8_t)((atomic_load(&s_bound) ? DK_INFO_BOUND : 0u)
                        | (RCBENCH_DIAL_HOOKS ? DK_INFO_HOOKS : 0u));
    i.hw = DK_HW_DIAL11;
    dk_text_copy(i.name, sizeof i.name, s_name);
    return dk_info_encode(&i, out);
}

/**
 * @brief GATT access to the service's characteristics, in the host task.
 * @param conn the connection
 * @param attr the attribute's handle
 * @param ctxt the operation
 * @param arg  unused
 * @return 0 or an ATT error
 */
static int chr_access(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && attr == s_h_screen) {
        panel_pkt_t p;
        uint16_t len = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) > DK_PKT_MAX
            || ble_hs_mbuf_to_flat(ctxt->om, p.b, sizeof p.b, &len) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        p.len = (uint8_t)len;
        (void)xQueueSend(s_rx, &p, 0);  /* full: dropped, the seq skips */
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t v[DK_PKT_MAX];
        size_t n = 0;
        if (attr == s_h_info) {
            n = info_value(v);
        } else if (attr == s_h_status) {
            n = s_status_len;
            memcpy(v, s_status, n);
        } else {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
        return os_mbuf_append(ctxt->om, v, (uint16_t)n) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/** @brief The service. */
static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = &s_uuid_event.u, .access_cb = chr_access,
              .val_handle = &s_h_event, .flags = BLE_GATT_CHR_F_NOTIFY },
            { .uuid = &s_uuid_status.u, .access_cb = chr_access,
              .val_handle = &s_h_status,
              .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ },
            { .uuid = &s_uuid_screen.u, .access_cb = chr_access,
              .val_handle = &s_h_screen, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = &s_uuid_info.u, .access_cb = chr_access,
              .val_handle = &s_h_info, .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    { 0 },
};

/** @brief Read the bound panel from NVS. */
static void bind_load(void)
{
    nvs_handle_t h;
    uint8_t v[7];
    size_t n = sizeof v;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    if (nvs_get_blob(h, NVS_KEY, v, &n) == ESP_OK && n == sizeof v
        && v[0] <= BLE_ADDR_RANDOM) {
        s_panel.type = v[0];
        memcpy(s_panel.val, v + 1, 6);
        atomic_store(&s_bound, true);
    }
    nvs_close(h);
}

/**
 * @brief Keep a panel's address in NVS, or erase it.
 * @param a the address; NULL erases
 */
static void bind_store(const ble_addr_t *a)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS did not open: the bind is not kept");
        return;
    }
    if (a != NULL) {
        uint8_t v[7];
        v[0] = a->type;
        memcpy(v + 1, a->val, 6);
        nvs_set_blob(h, NVS_KEY, v, sizeof v);
    } else {
        nvs_erase_key(h, NVS_KEY);
    }
    nvs_commit(h);
    nvs_close(h);
}

/** @brief Start advertising: to anyone while unbound, through the accept
 *         list to the bound panel otherwise. */
static void adv_start(void)
{
    struct ble_hs_adv_fields f;
    struct ble_hs_adv_fields rsp;
    struct ble_gap_adv_params p;
    const bool bound = atomic_load(&s_bound);
    int rc;

    if (atomic_load(&s_conn) != CONN_NONE || ble_gap_adv_active()) {
        return;
    }
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &s_uuid_svc;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    memset(&rsp, 0, sizeof rsp);
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = (uint8_t)strlen(s_name);
    rsp.name_is_complete = 1;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = BLE_GAP_ADV_ITVL_MS(bound ? DK_ADV_BOUND_MS
                                           : DK_ADV_UNBOUND_MS);
    p.itvl_max = p.itvl_min;
    /* Bound: only the panel may connect; a scan still lists the Dial. */
    p.filter_policy = bound ? BLE_HCI_ADV_FILT_CONN : BLE_HCI_ADV_FILT_NONE;
    if (bound && ble_gap_wl_set(&s_panel, 1) != 0) {
        ESP_LOGW(TAG, "the accept list did not take the panel");
    }
    rc = ble_gap_adv_set_fields(&f);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_fields(&rsp);
    }
    if (rc == 0) {
        rc = ble_gap_adv_start(s_own_type, NULL, BLE_HS_FOREVER, &p,
                               gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising did not start: %d", rc);
    }
}

/**
 * @brief Ask for the connection parameters.
 * @param latency peripheral latency, events
 */
static void ask_params(uint16_t latency)
{
    const struct ble_gap_upd_params p = {
        .itvl_min = DK_ITVL_MIN,
        .itvl_max = DK_ITVL_MAX,
        .latency = latency,
        .supervision_timeout = DK_SUPERVISION,
    };
    const uint16_t conn = (uint16_t)atomic_load(&s_conn);
    if (conn != CONN_NONE && ble_gap_update_params(conn, &p) != 0) {
        ESP_LOGW(TAG, "the parameter request did not go");
    }
}

/**
 * @brief The panel subscribed to EVENT: bind it if none is bound, and ask
 *        for the active parameters.
 * @param conn the connection
 */
static void subscribed(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (!atomic_load(&s_bound) && ble_gap_conn_find(conn, &d) == 0) {
        s_panel = d.peer_id_addr;
        atomic_store(&s_bound, true);
        bind_store(&s_panel);
        ESP_LOGI(TAG, "bound to %02X:%02X:%02X:%02X:%02X:%02X",
                 s_panel.val[5], s_panel.val[4], s_panel.val[3],
                 s_panel.val[2], s_panel.val[1], s_panel.val[0]);
        dial_buzz_tone(2000, 60);
    }
    atomic_store(&s_idle, false);
    ask_params(DK_LATENCY_ACTIVE);
}

/**
 * @brief GAP (generic access profile) events, in the host task.
 * @param ev  the event
 * @param arg unused
 * @return 0
 */
static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            atomic_store(&s_conn, ev->connect.conn_handle);
            atomic_fetch_add(&s_links, 1u);
            ESP_LOGI(TAG, "connected");
        } else {
            adv_start();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason 0x%03X",
                 (unsigned)ev->disconnect.reason);
        atomic_store(&s_conn, CONN_NONE);
        atomic_store(&s_sub_event, false);
        atomic_store(&s_sub_status, false);
        adv_start();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        adv_start();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_h_event) {
            atomic_store(&s_sub_event, ev->subscribe.cur_notify != 0);
            if (ev->subscribe.cur_notify != 0) {
                subscribed(ev->subscribe.conn_handle);
            }
        } else if (ev->subscribe.attr_handle == s_h_status) {
            atomic_store(&s_sub_status, ev->subscribe.cur_notify != 0);
        }
        break;
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0) {
            ESP_LOGI(TAG, "interval %u x 1.25ms, latency %u, timeout %u x 10ms",
                     (unsigned)d.conn_itvl, (unsigned)d.conn_latency,
                     (unsigned)d.supervision_timeout);
        }
        break;
    }
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %u", (unsigned)ev->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

/** @brief The host and controller are in sync: the address, the name, the
 *         advertising. */
static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0
        || ble_hs_id_infer_auto(0, &s_own_type) != 0
        || ble_hs_id_copy_addr(s_own_type, s_own, NULL) != 0) {
        ESP_LOGE(TAG, "no address");
        return;
    }
    snprintf(s_name, sizeof s_name, "DIAL-%02X%02X", s_own[1], s_own[0]);
    ble_svc_gap_device_name_set(s_name);
    ESP_LOGI(TAG, "%s, %s", s_name,
             atomic_load(&s_bound) ? "bound" : "not bound");
    adv_start();
}

/** @brief The host was reset; it syncs again by itself.
 *  @param reason the host's reason */
static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset, reason %d", reason);
}

/** @brief NimBLE's host task. @param arg unused */
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void dial_ble_start(void)
{
    s_rx = xQueueCreate(PANEL_QUEUE, sizeof(panel_pkt_t));
    bind_load();
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE did not start");
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(s_svcs) != 0 || ble_gatts_add_svcs(s_svcs) != 0) {
        ESP_LOGE(TAG, "the GATT service was refused");
        return;
    }
    nimble_port_freertos_init(host_task);
}

bool dial_ble_ready(void)
{
    return atomic_load(&s_conn) != CONN_NONE && atomic_load(&s_sub_event);
}

bool dial_ble_send_event(const uint8_t *pkt, size_t len)
{
    const uint16_t conn = (uint16_t)atomic_load(&s_conn);
    struct os_mbuf *om;
    if (conn == CONN_NONE || !atomic_load(&s_sub_event)) {
        return false;
    }
    om = ble_hs_mbuf_from_flat(pkt, (uint16_t)len);
    return om != NULL && ble_gatts_notify_custom(conn, s_h_event, om) == 0;
}

void dial_ble_send_status(const uint8_t *pkt, size_t len)
{
    const uint16_t conn = (uint16_t)atomic_load(&s_conn);
    struct os_mbuf *om;
    if (len > sizeof s_status) {
        return;
    }
    memcpy(s_status, pkt, len);
    s_status_len = len;
    if (conn == CONN_NONE || !atomic_load(&s_sub_status)) {
        return;
    }
    om = ble_hs_mbuf_from_flat(pkt, (uint16_t)len);
    if (om != NULL) {
        (void)ble_gatts_notify_custom(conn, s_h_status, om);
    }
}

bool dial_ble_take(uint8_t *buf, size_t *len)
{
    panel_pkt_t p;
    if (s_rx == NULL || xQueueReceive(s_rx, &p, 0) != pdTRUE) {
        return false;
    }
    memcpy(buf, p.b, p.len);
    *len = p.len;
    return true;
}

void dial_ble_turned(uint32_t now_ms)
{
    atomic_store(&s_last_turn, now_ms);
    if (atomic_exchange(&s_idle, false) && dial_ble_ready()) {
        ask_params(DK_LATENCY_ACTIVE);
    }
}

void dial_ble_poll(uint32_t now_ms)
{
    if (dial_ble_ready() && !atomic_load(&s_idle)
        && now_ms - (uint32_t)atomic_load(&s_last_turn) >= DK_IDLE_MS) {
        atomic_store(&s_idle, true);
        ask_params(DK_LATENCY_IDLE);
    }
}

void dial_ble_forget(void)
{
    const uint16_t conn = (uint16_t)atomic_load(&s_conn);
    atomic_store(&s_bound, false);
    bind_store(NULL);
    if (conn != CONN_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);  /* then adv */
    } else {
        ble_gap_adv_stop();
        adv_start();
    }
}

void dial_ble_drop(void)
{
    const uint16_t conn = (uint16_t)atomic_load(&s_conn);
    /* Not ready from here: no EVENT goes before the disconnect event. */
    atomic_store(&s_sub_event, false);
    atomic_store(&s_sub_status, false);
    if (conn != CONN_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

bool dial_ble_bound(void)
{
    return atomic_load(&s_bound);
}

uint32_t dial_ble_link_count(void)
{
    return (uint32_t)atomic_load(&s_links);
}

int8_t dial_ble_rssi(void)
{
    const uint16_t conn = (uint16_t)atomic_load(&s_conn);
    int8_t rssi = DK_RSSI_NONE;
    if (conn == CONN_NONE || ble_gap_conn_rssi(conn, &rssi) != 0
        || rssi < DK_RSSI_MIN || rssi > DK_RSSI_MAX) {
        return DK_RSSI_NONE;
    }
    return rssi;
}

const char *dial_ble_name(void)
{
    return s_name;
}

void dial_ble_addresses(char *own, char *panel)
{
    snprintf(own, 20, "%02X:%02X:%02X:%02X:%02X:%02X", s_own[5], s_own[4],
             s_own[3], s_own[2], s_own[1], s_own[0]);
    if (!atomic_load(&s_bound)) {
        panel[0] = '\0';
        return;
    }
    snprintf(panel, 20, "%02X:%02X:%02X:%02X:%02X:%02X %c", s_panel.val[5],
             s_panel.val[4], s_panel.val[3], s_panel.val[2], s_panel.val[1],
             s_panel.val[0], s_panel.type == BLE_ADDR_RANDOM ? 'R' : 'P');
}

void dial_ble_set_echo(uint16_t ms)
{
    atomic_store(&s_echo, ms);
}

uint16_t dial_ble_echo(void)
{
    return (uint16_t)atomic_load(&s_echo);
}
