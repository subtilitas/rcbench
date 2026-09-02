/*
 * SPDX-License-Identifier: MIT
 */

#include "xl2515.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "can_timing.h"
#include "iomcu_pins.h"
#include "mcp2515.h"

static inline void cs_low(void)  { gpio_put(IOMCU_CAN_PIN_CS, 0); }
static inline void cs_high(void) { gpio_put(IOMCU_CAN_PIN_CS, 1); }

static void write_regs(uint8_t addr, const uint8_t *data, size_t n)
{
    const uint8_t hdr[2] = { MCP2515_CMD_WRITE, addr };
    cs_low();
    spi_write_blocking(IOMCU_CAN_SPI, hdr, 2);
    spi_write_blocking(IOMCU_CAN_SPI, data, n);
    cs_high();
}

static void write_reg(uint8_t addr, uint8_t value)
{
    write_regs(addr, &value, 1);
}

static void read_regs(uint8_t addr, uint8_t *out, size_t n)
{
    const uint8_t hdr[2] = { MCP2515_CMD_READ, addr };
    cs_low();
    spi_write_blocking(IOMCU_CAN_SPI, hdr, 2);
    spi_read_blocking(IOMCU_CAN_SPI, 0x00, out, n);
    cs_high();
}

static uint8_t read_reg(uint8_t addr)
{
    uint8_t v = 0;
    read_regs(addr, &v, 1);
    return v;
}

/* Read-modify-write in the part rather than here: the interrupt flags are set
 * by the controller between a read and a write, and clearing them with a
 * whole-register write would drop whatever arrived in between. */
static void bit_modify(uint8_t addr, uint8_t mask, uint8_t value)
{
    const uint8_t msg[4] = { MCP2515_CMD_BIT_MODIFY, addr, mask, value };
    cs_low();
    spi_write_blocking(IOMCU_CAN_SPI, msg, 4);
    cs_high();
}

bool xl2515_init(uint32_t bitrate)
{
    spi_init(IOMCU_CAN_SPI, IOMCU_CAN_SPI_HZ);
    spi_set_format(IOMCU_CAN_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(IOMCU_CAN_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(IOMCU_CAN_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(IOMCU_CAN_PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(IOMCU_CAN_PIN_CS);
    gpio_set_dir(IOMCU_CAN_PIN_CS, GPIO_OUT);
    cs_high();
    gpio_init(IOMCU_CAN_PIN_INT);
    gpio_set_dir(IOMCU_CAN_PIN_INT, GPIO_IN);
    gpio_pull_up(IOMCU_CAN_PIN_INT);

    /* Reset, then let the oscillator settle. */
    const uint8_t reset = MCP2515_CMD_RESET;
    cs_low();
    spi_write_blocking(IOMCU_CAN_SPI, &reset, 1);
    cs_high();
    sleep_ms(10);

    /*
     * The datasheet guarantees configuration mode after a reset, so this
     * tests the SPI (Serial Peripheral Interface) wiring: a failure here
     * means nothing is listening on the SPI bus, which is a different fault
     * from a CAN (Controller Area Network) bus fault.
     */
    if ((read_reg(MCP2515_CANSTAT) & MCP2515_MODE_MASK) != MCP2515_MODE_CONFIG) {
        return false;
    }

    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, IOMCU_CAN_CRYSTAL_HZ);
    can_timing_t t;
    uint8_t cnf[3];
    if (!can_timing_solve(&lim, bitrate, CAN_SAMPLE_POINT_LINK, &t)
        || !mcp2515_encode_timing(&t, cnf)) {
        return false;   /* the crystal cannot make this rate; see can_timing */
    }
    write_reg(MCP2515_CNF1, cnf[0]);
    write_reg(MCP2515_CNF2, cnf[1]);
    write_reg(MCP2515_CNF3, cnf[2]);

    /*
     * Filters off.  There are two nodes on this bus and the panel is the only
     * other one, so filtering in the controller would only hide traffic that
     * the bring-up wants to see.  BUKT lets buffer 0 spill into buffer 1,
     * which is what stops a burst costing a frame.
     */
    write_reg(MCP2515_RXB0CTRL, MCP2515_RXM_ANY | MCP2515_BUKT);
    write_reg(MCP2515_RXB1CTRL, MCP2515_RXM_ANY);
    write_reg(MCP2515_CANINTE, 0);   /* polled, not interrupt-driven */
    write_reg(MCP2515_CANINTF, 0);

    write_reg(MCP2515_CANCTRL, MCP2515_MODE_NORMAL);
    sleep_ms(1);
    return (read_reg(MCP2515_CANSTAT) & MCP2515_MODE_MASK)
           == MCP2515_MODE_NORMAL;
}

bool xl2515_send(const link_can_frame_t *f)
{
    if (f == NULL || f->dlc > 8) {
        return false;
    }
    /* TXREQ still set means the previous frame has not won arbitration.
     * Overwriting it would drop it. */
    if ((read_reg(MCP2515_TXB0CTRL) & 0x08u) != 0u) {
        return false;
    }

    uint8_t buf[13];
    if (!mcp2515_pack_id(f->id, buf)) {
        return false;
    }
    buf[4] = f->dlc;
    memcpy(buf + 5, f->data, f->dlc);
    write_regs(MCP2515_TXB0SIDH, buf, 5u + f->dlc);

    const uint8_t rts = MCP2515_CMD_RTS_TXB0;
    cs_low();
    spi_write_blocking(IOMCU_CAN_SPI, &rts, 1);
    cs_high();
    return true;
}

bool xl2515_recv(link_can_frame_t *f)
{
    if (f == NULL) {
        return false;
    }
    const uint8_t intf = read_reg(MCP2515_CANINTF);
    uint8_t base, flag;
    if ((intf & MCP2515_INTF_RX0) != 0u) {
        base = MCP2515_RXB0SIDH;
        flag = MCP2515_INTF_RX0;
    } else if ((intf & MCP2515_INTF_RX1) != 0u) {
        base = MCP2515_RXB1SIDH;
        flag = MCP2515_INTF_RX1;
    } else {
        return false;
    }

    uint8_t buf[13];
    read_regs(base, buf, 13);
    bit_modify(MCP2515_CANINTF, flag, 0);

    memset(f, 0, sizeof(*f));
    if (!mcp2515_unpack_id(buf, &f->id)) {
        return false;   /* an 11-bit identifier; nothing here sends those */
    }
    /*
     * Bit 6 of the DLC (data length code) register is RTR (remote
     * transmission request): a remote frame, which carries a length but no
     * data.  Its data registers hold whatever the last data frame left
     * there, so accepting one would deliver stale bytes under a fresh
     * identifier.  Nothing on this bus sends remote frames, so one is
     * foreign traffic or a fault.
     */
    if ((buf[4] & 0x40u) != 0u) {
        return false;
    }
    f->dlc = (uint8_t)(buf[4] & 0x0Fu);
    if (f->dlc > 8) {
        return false;
    }
    memcpy(f->data, buf + 5, f->dlc);
    return true;
}

bool xl2515_take_overflow(void)
{
    const uint8_t eflg = read_reg(MCP2515_EFLG);
    if ((eflg & MCP2515_EFLG_OVR) == 0u) {
        return false;
    }
    /* Sticky until cleared, so clear them: the next report should say what
     * happened since this one, not what has ever happened. */
    bit_modify(MCP2515_EFLG, MCP2515_EFLG_OVR, 0);
    return true;
}

void xl2515_errors(uint8_t *tx_errors, uint8_t *rx_errors, uint8_t *flags)
{
    if (tx_errors != NULL) {
        *tx_errors = read_reg(MCP2515_TEC);
    }
    if (rx_errors != NULL) {
        *rx_errors = read_reg(MCP2515_REC);
    }
    if (flags != NULL) {
        *flags = read_reg(MCP2515_EFLG);
    }
}
