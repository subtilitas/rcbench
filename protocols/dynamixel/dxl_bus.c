/*
 * One Protocol 2.0 transaction at a time, and the ID scan.  See dxl_bus.h.
 *
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxl_bus.h"

#include <string.h>

#include "dxl_table.h"

/* Parameters a PING reply carries: model number (2), firmware (1). */
#define PING_PARAMS 3u
/* Parameter count that is not checked. */
#define ANY_PARAMS 0xFFFFu

static uint32_t bits_us(uint32_t baud, uint64_t bits)
{
    const uint64_t us = (bits * 1000000u + baud - 1u) / baud;

    return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

uint32_t dxl_deadline_us(uint32_t baud, uint32_t return_delay_us,
                         size_t n_tx, size_t n_reply_max)
{
    if (baud == 0u) {
        return 0;
    }
    const uint64_t tx_bits = DXL_LEAD_BITS
                             + (uint64_t)DXL_TX_BITS_PER_BYTE * n_tx;
    const uint64_t total = (uint64_t)bits_us(baud, tx_bits) + return_delay_us
                           + bits_us(baud, 10u * (uint64_t)n_reply_max)
                           + DXL_MARGIN_US;

    return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

size_t dxl_reply_max(size_t n_params)
{
    const size_t body = 2u + n_params;

    return DXL_HEADER_SIZE + body + body / 3u + DXL_CRC_SIZE;
}

void dxl_bus_init(dxl_bus_t *bus, const dxl_driver_t *driver)
{
    if (bus == NULL) {
        return;
    }
    memset(bus, 0, sizeof(*bus));
    if (driver != NULL) {
        bus->drv = *driver;
    }
    bus->return_delay_us = DXL_RETURN_DELAY_MAX_US;
    bus->status_return = 2;
    bus->result = DXL_ERR_STATE;
}

dxl_res_t dxl_bus_set_baud(dxl_bus_t *bus, uint32_t baud)
{
    if (bus == NULL || dxl_baud_index(baud) < 0) {
        return DXL_ERR_ARG;
    }
    if (bus->running != 0u) {
        return DXL_ERR_STATE;
    }
    if (bus->drv.set_baud == NULL || !bus->drv.set_baud(bus->drv.ctx, baud)) {
        return DXL_ERR_DRIVER;
    }
    bus->baud = baud;
    return DXL_OK;
}

void dxl_bus_set_return_delay(dxl_bus_t *bus, uint8_t raw)
{
    if (bus != NULL && raw <= 254u) {
        bus->return_delay_us = (uint16_t)dxl_return_delay_us(raw);
    }
}

void dxl_bus_set_status_return(dxl_bus_t *bus, uint8_t level)
{
    if (bus != NULL && level <= 2u) {
        bus->status_return = level;
    }
}

bool dxl_bus_busy(const dxl_bus_t *bus)
{
    return bus != NULL && bus->running != 0u;
}

/* The packet in bus->tx (n_tx bytes) to the driver.  @p reply: a status
 * packet is due, with @p want parameters. */
static dxl_res_t begin(dxl_bus_t *bus, uint8_t id, bool reply, uint16_t want)
{
    if (bus->n_tx == 0u) {
        return DXL_ERR_ARG;
    }
    if (bus->drv.start == NULL || bus->drv.now_us == NULL
        || bus->drv.poll == NULL) {
        return DXL_ERR_DRIVER;
    }
    bus->id = id;
    bus->reply = reply ? 1u : 0u;
    bus->want_params = want;
    bus->got = 0;
    bus->n_rx = 0;
    bus->extra = 0;
    bus->noise = 0;
    bus->t_reply_us = 0;
    memset(&bus->status, 0, sizeof(bus->status));
    bus->timeout_us = reply
        ? dxl_deadline_us(bus->baud, bus->return_delay_us, bus->n_tx,
                          dxl_reply_max(want == ANY_PARAMS ? DXL_PARAM_MAX
                                                           : want))
        : dxl_deadline_us(bus->baud, 0, bus->n_tx, 0);
    bus->t_start = bus->drv.now_us(bus->drv.ctx);
    if (!bus->drv.start(bus->drv.ctx, bus->tx, bus->n_tx)) {
        bus->result = DXL_ERR_DRIVER;
        return DXL_ERR_DRIVER;
    }
    bus->running = 1;
    bus->result = DXL_BUSY;
    return DXL_BUSY;
}

/* Checks common to every transaction before encoding. */
static dxl_res_t ready(const dxl_bus_t *bus)
{
    if (bus == NULL) {
        return DXL_ERR_ARG;
    }
    if (bus->running != 0u) {
        return DXL_ERR_STATE;
    }
    if (bus->baud == 0u) {
        return DXL_ERR_DRIVER;
    }
    return DXL_OK;
}

dxl_res_t dxl_bus_ping(dxl_bus_t *bus, uint8_t id)
{
    const dxl_res_t r = ready(bus);

    if (r != DXL_OK) {
        return r;
    }
    /* A broadcast ping draws one reply per servo: not one transaction. */
    if (id > DXL_ID_MAX) {
        return DXL_ERR_ARG;
    }
    bus->has_alt = 0;
    bus->n_tx = (uint16_t)dxl_ping(id, bus->tx, sizeof(bus->tx));
    return begin(bus, id, true, PING_PARAMS);
}

dxl_res_t dxl_bus_read(dxl_bus_t *bus, uint8_t id, uint16_t addr,
                       uint16_t len)
{
    const dxl_res_t r = ready(bus);

    if (r != DXL_OK) {
        return r;
    }
    if (id > DXL_ID_MAX) {
        return DXL_ERR_ARG;
    }
    bus->has_alt = 0;
    bus->n_tx = (uint16_t)dxl_read(id, addr, len, bus->tx, sizeof(bus->tx));
    return begin(bus, id, bus->status_return >= 1u, len);
}

static dxl_res_t write_like(dxl_bus_t *bus, bool reg, uint8_t id,
                            uint16_t addr, const uint8_t *data, size_t n)
{
    const dxl_res_t r = ready(bus);

    if (r != DXL_OK) {
        return r;
    }
    bus->has_alt = 0;
    bus->n_tx = (uint16_t)(reg ? dxl_reg_write(id, addr, data, n, bus->tx,
                                               sizeof(bus->tx))
                               : dxl_write(id, addr, data, n, bus->tx,
                                           sizeof(bus->tx)));
    /* After a write to ID the servo may answer at the new ID; which ID
     * the reply to the write itself carries is not published. */
    if (!reg && bus->n_tx > 0u && addr == 7u && n == 1u) {
        bus->alt_id = data[0];
        bus->has_alt = 1;
    }
    return begin(bus, id,
                 id != DXL_ID_BROADCAST && bus->status_return >= 2u, 0);
}

dxl_res_t dxl_bus_write(dxl_bus_t *bus, uint8_t id, uint16_t addr,
                        const uint8_t *data, size_t n)
{
    return write_like(bus, false, id, addr, data, n);
}

dxl_res_t dxl_bus_reg_write(dxl_bus_t *bus, uint8_t id, uint16_t addr,
                            const uint8_t *data, size_t n)
{
    return write_like(bus, true, id, addr, data, n);
}

static dxl_res_t plain(dxl_bus_t *bus, uint8_t id, uint8_t instr)
{
    const dxl_res_t r = ready(bus);

    if (r != DXL_OK) {
        return r;
    }
    bus->has_alt = 0;
    bus->n_tx = (uint16_t)dxl_encode(id, instr, NULL, 0, bus->tx,
                                     sizeof(bus->tx));
    return begin(bus, id,
                 id != DXL_ID_BROADCAST && bus->status_return >= 2u, 0);
}

dxl_res_t dxl_bus_action(dxl_bus_t *bus, uint8_t id)
{
    return plain(bus, id, (uint8_t)DXL_INS_ACTION);
}

dxl_res_t dxl_bus_reboot(dxl_bus_t *bus, uint8_t id)
{
    return plain(bus, id, (uint8_t)DXL_INS_REBOOT);
}

dxl_res_t dxl_bus_factory_reset(dxl_bus_t *bus, uint8_t id, uint8_t option,
                                uint32_t confirm)
{
    const dxl_res_t r = ready(bus);

    if (r != DXL_OK) {
        return r;
    }
    bus->has_alt = 0;
    bus->n_tx = (uint16_t)dxl_factory_reset(id, option, confirm, bus->tx,
                                            sizeof(bus->tx));
    return begin(bus, id,
                 id != DXL_ID_BROADCAST && bus->status_return >= 2u, 0);
}

dxl_res_t dxl_bus_sync_write(dxl_bus_t *bus, uint16_t addr, uint16_t len,
                             const uint8_t *ids, const uint8_t *data,
                             size_t n_ids)
{
    const dxl_res_t r = ready(bus);

    if (r != DXL_OK) {
        return r;
    }
    bus->has_alt = 0;
    bus->n_tx = (uint16_t)dxl_sync_write(addr, len, ids, data, n_ids,
                                         bus->tx, sizeof(bus->tx));
    return begin(bus, DXL_ID_BROADCAST, false, 0);
}

static dxl_res_t finish(dxl_bus_t *bus, dxl_res_t res)
{
    if (bus->drv.stop != NULL) {
        bus->drv.stop(bus->drv.ctx);
    }
    bus->running = 0;
    bus->result = res;
    return res;
}

static dxl_res_t map_rx(dxl_rx_t rx)
{
    switch (rx) {
    case DXL_RX_ERR_CRC:
        return DXL_ERR_CRC;
    case DXL_RX_ERR_STUFFING:
        return DXL_ERR_STUFFING;
    case DXL_RX_ERR_ID:
        return DXL_ERR_ID;
    default:
        return DXL_ERR_LENGTH;
    }
}

/* A whole packet arrived: hold it to the transaction. */
static dxl_res_t judge(const dxl_bus_t *bus)
{
    const dxl_packet_t *p = &bus->status;

    if (p->instr != (uint8_t)DXL_INS_STATUS) {
        return DXL_ERR_NOT_STATUS;
    }
    if (p->id != bus->id && (bus->has_alt == 0u || p->id != bus->alt_id)) {
        return DXL_ERR_ID;
    }
    if (dxl_err_number(p->error) != 0u) {
        return DXL_ERR_STATUS;
    }
    if (bus->want_params != ANY_PARAMS && p->n_params != bus->want_params) {
        return DXL_ERR_PARAMS;
    }
    return DXL_OK;
}

dxl_res_t dxl_bus_step(dxl_bus_t *bus)
{
    if (bus == NULL) {
        return DXL_ERR_ARG;
    }
    if (bus->running == 0u) {
        return bus->result;
    }
    const uint32_t elapsed = bus->drv.now_us(bus->drv.ctx) - bus->t_start;
    const bool late = elapsed >= bus->timeout_us;

    /* Take what came, also after a reply, so a second sender shows. */
    for (;;) {
        uint8_t scratch[32];
        const size_t room = sizeof(bus->rx) - bus->n_rx;
        uint8_t *dst = bus->got != 0u ? scratch : &bus->rx[bus->n_rx];
        const size_t cap = bus->got != 0u ? sizeof(scratch) : room;
        size_t n;

        if (cap == 0u) {
            break;
        }
        n = bus->drv.poll(bus->drv.ctx, dst, cap);
        if (n == 0u) {
            break;
        }
        if (n > cap) {
            n = cap;
        }
        if (bus->got != 0u) {
            bus->extra += (uint32_t)n;
            continue;
        }
        if (bus->reply == 0u) {
            bus->noise += (uint32_t)n;
            continue;
        }
        bus->n_rx = (uint16_t)(bus->n_rx + n);

        size_t used = 0;
        const dxl_rx_t rx = dxl_parse(bus->rx, bus->n_rx, &bus->status, &used);

        if (rx == DXL_RX_MORE) {
            /* Drop what lies before a possible header. */
            if (used > 0u) {
                bus->noise += (uint32_t)used;
                memmove(bus->rx, &bus->rx[used], bus->n_rx - used);
                bus->n_rx = (uint16_t)(bus->n_rx - used);
            }
            continue;
        }
        if (rx != DXL_RX_OK) {
            return finish(bus, map_rx(rx));
        }
        bus->t_reply_us = elapsed;
        bus->noise += (uint32_t)(used - bus->status.wire_len);
        if (used < bus->n_rx) {
            bus->extra += (uint32_t)(bus->n_rx - used);
        }
        const dxl_res_t res = judge(bus);

        if (res != DXL_OK || bus->linger == 0u) {
            return finish(bus, res);
        }
        bus->got = 1;
    }
    if (!late) {
        return DXL_BUSY;
    }
    if (bus->got != 0u) {
        return finish(bus, judge(bus));
    }
    if (bus->reply == 0u) {
        return finish(bus, DXL_OK);
    }
    /* Bytes that held no header are a broken reply too, not silence. */
    return finish(bus, bus->n_rx > 0u || bus->noise > 0u ? DXL_ERR_TRUNCATED
                                                         : DXL_ERR_TIMEOUT);
}

void dxl_bus_abort(dxl_bus_t *bus)
{
    if (bus != NULL && bus->running != 0u) {
        (void)finish(bus, DXL_ERR_ABORTED);
    }
}

/* ------------------------------------------------------------------ scan */

/* The next rate of the mask from @p from on, or DXL_BAUD_COUNT. */
static uint8_t next_rate(uint8_t mask, unsigned from)
{
    for (unsigned i = from; i < DXL_BAUD_COUNT; ++i) {
        if ((mask & (1u << i)) != 0u) {
            return (uint8_t)i;
        }
    }
    return (uint8_t)DXL_BAUD_COUNT;
}

/* Set the rate of scan->baud_index or a later one the driver takes. */
static bool scan_rate(dxl_scan_t *s)
{
    while (s->baud_index < DXL_BAUD_COUNT) {
        if (dxl_bus_set_baud(s->bus, dxl_baud_rate(s->baud_index)) == DXL_OK) {
            return true;
        }
        s->refused_mask = (uint8_t)(s->refused_mask | (1u << s->baud_index));
        s->baud_index = next_rate(s->baud_mask, s->baud_index + 1u);
    }
    return false;
}

static dxl_res_t scan_end(dxl_scan_t *s, dxl_res_t res)
{
    s->bus->linger = 0;
    s->running = 0;
    s->result = res;
    return res;
}

/* Ping the current ID, or end the scan when the rates are done. */
static dxl_res_t scan_ping(dxl_scan_t *s)
{
    dxl_res_t r;

    if (s->baud_index >= DXL_BAUD_COUNT) {
        return scan_end(s, DXL_OK);
    }
    s->bus->linger = 1;
    r = dxl_bus_ping(s->bus, s->id);
    if (r != DXL_BUSY) {
        return scan_end(s, DXL_ERR_DRIVER);
    }
    s->pinged++;
    return DXL_BUSY;
}

dxl_res_t dxl_scan_start(dxl_scan_t *scan, dxl_bus_t *bus, uint8_t first,
                         uint8_t last, uint8_t baud_mask)
{
    if (scan == NULL || bus == NULL || first > DXL_ID_MAX
        || last > DXL_ID_MAX || first > last || baud_mask == 0u) {
        return DXL_ERR_ARG;
    }
    if (bus->running != 0u) {
        return DXL_ERR_STATE;
    }
    memset(scan, 0, sizeof(*scan));
    scan->bus = bus;
    scan->first = first;
    scan->last = last;
    scan->id = first;
    scan->baud_mask = baud_mask;
    scan->baud_index = next_rate(baud_mask, 0);
    scan->running = 1;
    scan->result = DXL_BUSY;
    if (!scan_rate(scan)) {
        return scan_end(scan, DXL_OK);
    }
    return scan_ping(scan);
}

static void scan_record(dxl_scan_t *s, dxl_res_t res)
{
    const dxl_bus_t *b = s->bus;
    uint8_t flags = 0;

    if (res == DXL_ERR_TIMEOUT) {
        return;
    }
    if (res == DXL_OK) {
        if (b->extra > 0u) {
            flags |= DXL_FOUND_EXTRA;
        }
        if (dxl_err_alert(b->status.error)) {
            flags |= DXL_FOUND_ALERT;
        }
    } else {
        flags |= DXL_FOUND_GARBLED;
    }
    if (s->n_found >= DXL_SCAN_FOUND_MAX) {
        s->overflow++;
        return;
    }
    dxl_found_t *f = &s->found[s->n_found++];

    f->id = s->id;
    f->baud_index = s->baud_index;
    f->flags = flags;
    f->result = (uint8_t)res;
    f->model = res == DXL_OK ? dxl_get_u16(&b->status.params[0]) : 0u;
    f->firmware = res == DXL_OK ? b->status.params[2] : 0u;
}

dxl_res_t dxl_scan_step(dxl_scan_t *scan)
{
    if (scan == NULL) {
        return DXL_ERR_ARG;
    }
    if (scan->running == 0u) {
        return scan->result;
    }
    const dxl_res_t r = dxl_bus_step(scan->bus);

    if (r == DXL_BUSY) {
        return DXL_BUSY;
    }
    scan_record(scan, r);
    if (scan->id < scan->last) {
        scan->id++;
    } else {
        scan->id = scan->first;
        scan->baud_index = next_rate(scan->baud_mask, scan->baud_index + 1u);
        if (scan->baud_index < DXL_BAUD_COUNT && !scan_rate(scan)) {
            return scan_end(scan, DXL_OK);
        }
    }
    return scan_ping(scan);
}

void dxl_scan_abort(dxl_scan_t *scan)
{
    if (scan == NULL || scan->running == 0u) {
        return;
    }
    dxl_bus_abort(scan->bus);
    (void)scan_end(scan, DXL_ERR_ABORTED);
}
