# pdmini

A driver for the WeAct PD Power Mini V1 Buck, a USB-C PD (Power Delivery)
trigger and buck module, on its UART (universal asynchronous
receiver-transmitter).

The module has two parts.

| Part | Files | Depends on |
|---|---|---|
| Core | `pdmini.c`, `pdmini_codec.c`, `pdmini_jobs.c`, `pdmini_int.h`, `include/pdmini.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/pd_uart.pio`, `rp2350/pd_uart.c`, `rp2350/pd_uart.h` | the core, pico-sdk |

## Constraints

- Set points: 1000mV to 20000mV and 50mA to 3000mA.
- A request is a command byte, its arguments and a CRC8 (cyclic redundancy
  check, polynomial 0x31, initial 0xFF). The module answers a write with
  nothing, so each write is confirmed by the matching read.
- One transaction at a time: the pins are handed over 5ms before a request,
  a reply has 400ms and 60ms more per byte, at most 600ms, and the pins
  rest 10ms between transactions.
- Between transactions both pins are inputs with a pull-down. A transmit
  line idling high back-feeds an unpowered module.
- The output is read every 100ms, its state and the input every 500ms,
  the active slot every 1000ms, with a module that answers within 12ms.
- Current limitation: a module that answers later stretches these periods,
  as each transaction takes its reply time. At rest the input is read
  850ms apart at 100ms late, 1.3s apart at 200ms and 2.5s apart at
  390ms; the state up to 1.5s apart (host model).
- A reading 400ms past its due goes before the next set-point write, one at
  a time with another transaction between two. The state read, and the
  input read before a write, may also follow another reading straight away.
  With a module that answers at once, set points changed every 50ms delay
  a display read to about 500ms and a state read to about 900ms after the
  last one, each with up to two transactions more.
- The input is read before a set-point write or an ON only when one is
  waiting and the last input read is 500ms old, and not between the reads
  for an ON.
- Set points and an ON reach a module that answers up to 390ms late (host
  model, 40 start phases):
  - one set point within 2.5s;
  - an ON within 3.6s when the set points are already written, up to
    7.5s when it is asked right after a set point change;
  - an output state the module changes itself seen within 2.1s while set
    points change every 50ms.

  The real module's reply time is not measured.
- An output the module switches on while OFF is asked is switched off at
  the next state read: about 500ms later with a module that answers at
  once, up to 1.8s with one that answers up to 390ms late, up to 2.1s
  while set points change every 50ms.
- Current limitation: with replies 279ms or more late and a set point
  changed every 50ms, the display is read 3.7s to 5.0s after the last
  display read (5.0s at 390ms). From 263ms late the gap is over 2.9s.
  A user that switches the output off after 3s without a reading, as the
  rcbench panel does, switches it off then; the panel reads the page every
  100ms, which adds up to 100ms to the gap it sees. At rest the display
  read stands up to 2.5s at 390ms (1.8s at 280ms).
- 3 failed state reads in a row take the module for gone.
- A voltage set point is capped 700mV under the input the module reports,
  rounded down to 10mV, and is never under 1000mV.
- A live output whose input reads under the set point plus 500mV on 2 reads
  in a row is switched off and held off. The rule is chosen without a bench
  measurement. From the sag to the cut takes about 1.0s with a module that
  answers at once; with input reads stretched by a late module, 1.7s at
  100ms late, 1.9s at 200ms and up to 4.7s up to 390ms (host model).
- The 200mV between the cap and the cut line lets the input drop under load
  without a cut: 3000mA through 1m of 20 AWG copper out and back
  (2 x 33mOhm) drops 200mV. The drop on a real source and cable is not
  measured. A dip of 200mV or more from the reading the cap was taken from
  cuts a set point at the cap.
- An ON asked while the input reads under 1500mV is not written: there is
  no room for the lowest set point. The output is held off as for a sag.
- The core is non-blocking and keeps no time of its own. The caller passes
  milliseconds.
- The pin driver takes 2 PIO (programmable input/output) state machines and
  serves 1 module. Its receive queue is 8 bytes deep: 4.2ms of reply at
  19200 baud, 0.17ms at 460800 baud. A caller that polls less often loses
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

## Trademarks

WeAct is a name or trademark of its owner; it is used here only to say
which module this driver works with. The project is not affiliated with it.
The protocol is implemented from the vendor's sheet `uart_protocol_v1.1`; no
vendor code is included.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).
