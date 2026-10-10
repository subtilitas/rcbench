# pdmini

A driver for the WeAct PD Power Mini V1 Buck, a USB-C PD (Power Delivery)
trigger and buck module, on its UART (universal asynchronous
receiver-transmitter).

The module has two parts.

| Part | Files | Depends on |
|---|---|---|
| Core | `pdmini.c`, `include/pdmini.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/pd_uart.pio`, `rp2350/pd_uart.c`, `rp2350/pd_uart.h` | the core, pico-sdk |

## Constraints

- Set points: 1000 mV to 20000 mV and 50 mA to 3000 mA.
- A request is a command byte, its arguments and a CRC8 (cyclic redundancy
  check, polynomial 0x31, initial 0xFF). The module answers a write with
  nothing, so each write is confirmed by the matching read.
- One transaction at a time: the pins are handed over 5 ms before a request,
  a reply has 400 ms and 60 ms more per byte, at most 600 ms, and the pins
  rest 10 ms between transactions.
- Between transactions both pins are inputs with a pull-down. A transmit
  line idling high back-feeds an unpowered module.
- The output is read every 100 ms, its state and the input every 500 ms.
- 3 failed state reads in a row take the module for gone.
- A live output whose input reads under the set point plus 500 mV on 2 reads
  in a row is switched off and held off. The rule is chosen without a bench
  measurement.
- The core is non-blocking and keeps no time of its own. The caller passes
  milliseconds.
- The pin driver takes 2 PIO (programmable input/output) state machines and
  serves 1 module. Its receive queue is 8 bytes deep: 4.2 ms of reply at
  19200 baud, 0.17 ms at 460800 baud. A caller that polls less often loses
  bytes, and the transaction is tried again.

`include/pdmini.h` states the full rules for ON, OFF and a module that stops
answering.

## Interface

Core, `pdmini.h`:

```c
void pdmini_init(pdmini_t *d, const pdmini_io_t *io, uint32_t now_ms);
void pdmini_want(pdmini_t *d, bool output, uint16_t set_mv, uint16_t set_ma);
void pdmini_want_off(pdmini_t *d);
void pdmini_rx(pdmini_t *d, uint8_t byte, uint32_t now_ms);
void pdmini_step(pdmini_t *d, uint32_t now_ms);
const pdmini_status_t *pdmini_status(const pdmini_t *d);
bool pdmini_may_be_on(const pdmini_t *d);
```

The core reaches the hardware through `pdmini_io_t`: `attach`, `detach` and
`send`, each with a context pointer.

Driver, `pd_uart.h`:

```c
bool pd_uart_open(uint8_t tx, uint8_t rx, uint32_t baud);
void pd_uart_baud(uint32_t baud);
void pd_uart_close(void);
void pd_uart_attach(void *ctx);
void pd_uart_detach(void *ctx);
void pd_uart_send(void *ctx, const uint8_t *p, size_t n);
bool pd_uart_getc(uint8_t *b);
```

`pd_uart_attach`, `pd_uart_detach` and `pd_uart_send` are the three
callbacks of `pdmini_io_t`.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/pdmini)
target_link_libraries(my_target PRIVATE rcbench_pdmini rcbench_pdmini_rp2350)
```

```c
static pdmini_t pd;
static const pdmini_io_t io = {
    .attach = pd_uart_attach, .detach = pd_uart_detach, .send = pd_uart_send,
};

pd_uart_open(TX_PIN, RX_PIN, BAUD);   /* the rate the module is set to */
pdmini_init(&pd, &io, now_ms);
for (;;) {
    uint8_t b;
    while (pd_uart_getc(&b)) {
        pdmini_rx(&pd, b, now_ms);
    }
    pdmini_step(&pd, now_ms);
}
```

`add_subdirectory()` comes after `pico_sdk_init()`. The driver is an
INTERFACE library: `pd_uart.c` compiles in the target that links it, and
`pd_uart.pio.h` is generated in the build tree. Both headers compile from
C++; in C++ the `pdmini_io_t` members are set in declaration order.

A host build links `rcbench_pdmini` alone; the folder `rp2350/` is added only
where `pico_generate_pio_header` exists.
