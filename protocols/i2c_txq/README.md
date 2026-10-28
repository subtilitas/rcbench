# i2c_txq

A transaction queue for one I2C (Inter-Integrated Circuit) bus master. The
core holds the transactions of one bus, picks the next by class and
deadline, drops a soft one whose deadline has passed, ends one that does
not finish, checks the lines before a start, aborts, and runs the bus
clear and the trial after it. Nothing in it waits: every step that needs
time is taken on a later poll. The pin driver runs the queue on one of the
RP2350's two I2C blocks: each transaction goes to the block's transmit
FIFO (first in, first out) whole, STOP included, and the block's interrupt
reports its end.

In this repository nothing calls the module yet. The IOmcu's sensor bus
runs on `firmware/iomcu/src/sense_i2c.c`; the IOmcu image links the module
and holds none of its code.

| Part | Files | Depends on |
|---|---|---|
| Core | `i2c_txq.c`, `include/i2c_txq.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/i2c_txq_block.c`, `rp2350/i2c_txq_block.h` | the core; pico-sdk `hardware_i2c`, `hardware_gpio`, `hardware_irq`, `hardware_sync`, `hardware_timer` |

The driver runs the chip's I2C block and has no PIO program (PIO:
programmable input/output).

## Constraints

- Plain C11. Both headers have `extern "C"` guards and compile by
  themselves as C11 and as C++17 (`tools/check_protocols.py`).
  `test/host/i2c_txq_headers.cpp` calls the core from C++11 and links
  against the C library.
- The core includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and
  `<string.h>` and no other file outside this folder.
- No heap, no floating point, no global or static mutable state in the
  core. The driver keeps one pointer per I2C block for the interrupt.
- A queue holds 16 transactions (`I2C_TXQ_DEPTH`), the one on the wire not
  counted. The descriptors belong to the caller; the queue holds pointers.
- A transaction writes 0 to 5 bytes (`I2C_TXQ_WR_MAX`: a register pointer
  and 4), then reads 0 to 6 (`I2C_TXQ_RD_MAX`) after a repeated START, and
  ends with a STOP. At least 1 byte is written or read. The largest is 11
  FIFO entries; the RP2350's transmit and receive FIFOs are 16 deep.
- Addresses are 7 bits, 0x08 to 0x77. 0x00 to 0x07 and 0x78 to 0x7F are
  refused, so no general call (0x00) is ever sent.
- Sizes with arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`): a
  queue (`i2c_txq_t`) is 224 bytes, a transaction (`i2c_txq_xfer_t`) 64
  bytes, a bus of the driver (`i2c_txq_block_t`) 248 bytes. With gcc on
  x86-64: 328, 72 and 352 bytes. `i2c_txq.c` is 2,526 bytes of code at
  `-Os`; `i2c_txq_block.c` is 2,336 bytes at `-O3` in the IOmcu's Release build.
- Times are microseconds on a 64-bit clock the caller passes in. Every
  comparison is made on the difference of two times, so the order holds
  across the wrap of the clock while the times compared are less than
  2^63us apart. "At once" is a not-before time equal to the present time,
  not 0.
- Every function of one queue is called from one context at a time.

## Transactions

The caller fills `i2c_txq_xfer_t`:

| Field | Meaning |
|---|---|
| `addr` | 7-bit address |
| `wr`, `wr_len` | bytes written, 0 to 5 |
| `rd_len` | bytes read, 0 to 6; after a write with a repeated START |
| `cls` | `I2C_TXQ_HARD` or `I2C_TXQ_SOFT` |
| `not_before_us` | not started before this time |
| `deadline_us` | the last time it may start |
| `user` | the caller's, not read |

The queue fills `rd`, `state`, `result`, `flags`, `start_us` and
`done_us`, and calls the done callback, if one is set, from the context
that ended the transaction.

The callback may submit again and poll, as `i2c_txq_block_submit()` does.
A poll made while another queue call is under way does nothing; the call
under way or the next poll takes up the new transaction. Queue calls never
nest deeper than one: a callback that submits an expired transaction again
on every call runs once per poll, not in a recursion.

The bus time is 9 clocks a byte, address bytes included, plus 1 clock
each for the START, the repeated START and the STOP
(`i2c_txq_clocks()`, `i2c_txq_bus_us()`). Gaps the controller leaves
between bytes are not counted. At 400kHz:

| Transaction | Clocks | Bus time |
|---|---|---|
| pointer, read 1 | 39 | 97.5us |
| pointer, read 2 | 48 | 120us |
| pointer, read 6 | 84 | 210us |
| pointer and 2 bytes written | 38 | 95us |
| read 1 without a pointer | 20 | 50us |
| 5 written, 6 read | 120 | 300us |

## Order

One transaction is on the wire at a time. Of the queued ones whose
not-before time has come:

1. a hard one goes before every soft one;
2. inside a class the earliest deadline goes first;
3. on equal deadlines the one submitted first goes first.

A soft transaction whose deadline has passed is never started: the next
poll ends it `I2C_TXQ_DROPPED`. A deadline equal to the present time
still starts. The deadline is checked again when a line low at the pick
rises in the settle time: a soft transaction past it then ends
`I2C_TXQ_DROPPED` without reaching the wire, and a trial passes to the next
one. A hard transaction is never dropped; one started after its
deadline carries `I2C_TXQ_F_LATE_START`. A transaction on the wire is not
cut for one that comes later: a hard one waits for the end of the one on
the wire, at most 300us at 400kHz.

A held queue (`i2c_txq_hold()`) starts nothing and still drops soft
transactions past their deadline.

## Results

| Code | Name | Condition |
|---|---|---|
| 0 | `I2C_TXQ_OK` | sent, acknowledged, every byte read |
| 1 | `I2C_TXQ_NACK_ADDR` | the address not acknowledged |
| 2 | `I2C_TXQ_NACK_DATA` | a written byte not acknowledged |
| 3 | `I2C_TXQ_ARB_LOST` | the controller lost arbitration |
| 4 | `I2C_TXQ_TIMEOUT` | not ended 1000us after its start (`I2C_TXQ_TIMEOUT_US`); the controller is reset |
| 5 | `I2C_TXQ_BUS_LOW` | a line low before the start, and still low after the probe |
| 6 | `I2C_TXQ_NO_PULLUP` | a line low before the start that rose on the pad pull-ups |
| 7 | `I2C_TXQ_DROPPED` | soft, its deadline passed before it started |
| 8 | `I2C_TXQ_ABORTED` | taken back by `i2c_txq_abort()`, a bus clear or a flush |
| 9 | `I2C_TXQ_FAULT` | another abort cause, or a byte count other than `rd_len` |

The controller's abort causes are the bits of the DesignWare
`IC_TX_ABRT_SOURCE` register (`I2C_TXQ_ABRT_*`). With several bits set the
address NACK wins over the data NACK, the data NACK over the lost
arbitration, the lost arbitration over the abort the driver set.

Flags of a finished transaction:

| Flag | Meaning |
|---|---|
| `I2C_TXQ_F_WIRE` | it reached the controller |
| `I2C_TXQ_F_TRIAL` | the first one after a bus clear |
| `I2C_TXQ_F_LATE_START` | hard, started after its deadline |
| `I2C_TXQ_F_LATE_STAMP` | `done_us` is the computed end, not the report's time |
| `I2C_TXQ_F_ABORT` | `i2c_txq_abort()` asked for its end |

The end of a transaction is stamped with the time the driver reports it.
A report more than 100us (`I2C_TXQ_LATE_US`) after `start_us` plus the bus
time takes `start_us` plus the bus time instead, and the flag. Only an
`I2C_TXQ_OK` transaction is stamped that way.

## Line check

Before a start both lines must read high.

| Step | Time | Outcome |
|---|---|---|
| a line low | up to 10us (`I2C_TXQ_SETTLE_US`) | high in that time: the transaction starts |
| still low | the pads' pull-ups on, up to 50us (`I2C_TXQ_PROBE_US`) | rises on them: `I2C_TXQ_NO_PULLUP`; low at 50us: `I2C_TXQ_BUS_LOW` |

The pull-ups go off again either way, and nothing is sent after a probe.
The RP2350's pad pull-up is 32 to 86kOhm (datasheet pad table): 50us is 13
time constants at 86kOhm on 45pF. A line a part holds low never rises.

## Abort

`i2c_txq_abort()` on a queued transaction, or on one in the line check,
ends it `I2C_TXQ_ABORTED` at once. On the wire it calls the driver's
`abort()`: the block's ABORT bit, which ends the transaction after the byte
under way with a STOP. A transaction that still ends `I2C_TXQ_OK` keeps
that result; any other end is `I2C_TXQ_ABORTED`. An abort that does not
end, because a part holds SCL, ends at the timeout.

## Bus clear

`i2c_txq_recover()` ends a running transaction `I2C_TXQ_ABORTED` (the
controller reset when it is on the wire), then runs 24 steps, each half a
clock (5us, `I2C_TXQ_CLEAR_HALF_US`) after the one before:

1. the controller reset, both pins plain GPIO (general-purpose
   input/output), released;
2. 9 clocks on SCL: low, then released, then up to 100us
   (`I2C_TXQ_CLEAR_STRETCH_US`) for a part that holds SCL low;
3. a STOP: SCL low, SDA low, SCL released, SDA released;
4. both lines read, the controller set up again and the pins back on it.

A part part way through a byte finishes it in the 9 clocks and lets SDA
go (NXP UM10204 section 3.1.16). `i2c_txq_clear_result()` is `I2C_TXQ_OK`
when both lines read high at step 4, `I2C_TXQ_BUS_LOW` when one is low.
Nothing starts during the clear; soft transactions past their deadline are
still dropped. A step is taken at the first poll at or after its time: at
a poll every 1ms the clear takes 24ms, at a poll every 5us or faster
120us, without SCL held.

The first transaction picked after the clear carries `I2C_TXQ_F_TRIAL`.
The trial is spent by any end but `I2C_TXQ_ABORTED`; after an abort the
next transaction is the trial. What a trial's result means for the bus --
stuck, working -- is the caller's decision.

## Interface

Core, `i2c_txq.h`:

```c
void          i2c_txq_cfg_default(i2c_txq_cfg_t *cfg);
bool          i2c_txq_init(i2c_txq_t *q, const i2c_txq_ops_t *ops, void *ctx,
                           const i2c_txq_cfg_t *cfg);
i2c_txq_err_t i2c_txq_submit(i2c_txq_t *q, i2c_txq_xfer_t *x);
bool          i2c_txq_abort(i2c_txq_t *q, i2c_txq_xfer_t *x, uint64_t now);
void          i2c_txq_flush(i2c_txq_t *q, uint64_t now);
void          i2c_txq_poll(i2c_txq_t *q, uint64_t now);
void          i2c_txq_complete(i2c_txq_t *q, uint64_t now, uint32_t abrt,
                               const uint8_t *rx, size_t n);
bool          i2c_txq_recover(i2c_txq_t *q, uint64_t now);
void          i2c_txq_hold(i2c_txq_t *q, bool on);
uint32_t      i2c_txq_bus_us(const i2c_txq_xfer_t *x, uint32_t hz);
```

A driver implements `i2c_txq_ops_t`: read a line, start a transaction,
abort it, reset the controller, switch the pads' pull-ups, switch the pins
between the controller and GPIO, and drive or release a line. It reports
the end of each transaction with `i2c_txq_complete()`.

`i2c_txq_submit()` refuses with `I2C_TXQ_E_ARG` (NULL, a length or address
out of range, an unknown class), `I2C_TXQ_E_BUSY` (the descriptor is queued
or running) or `I2C_TXQ_E_FULL` (16 queued). It starts nothing; the next
poll does.

Driver, `i2c_txq_block.h`:

```c
void                i2c_txq_block_init(i2c_txq_block_t *b);
i2c_txq_block_err_t i2c_txq_block_open(i2c_txq_block_t *b, unsigned sda,
                                       unsigned scl, const i2c_txq_cfg_t *cfg);
void                i2c_txq_block_close(i2c_txq_block_t *b);
i2c_txq_err_t       i2c_txq_block_submit(i2c_txq_block_t *b, i2c_txq_xfer_t *x);
bool                i2c_txq_block_abort(i2c_txq_block_t *b, i2c_txq_xfer_t *x);
bool                i2c_txq_block_recover(i2c_txq_block_t *b);
void                i2c_txq_block_hold(i2c_txq_block_t *b, bool on);
void                i2c_txq_block_poll(i2c_txq_block_t *b);
```

- SDA is an even GPIO and SCL the next one. The block is I2C0 or I2C1 by
  (SDA / 2) mod 2, its interrupt I2C0_IRQ (36) or I2C1_IRQ (37). The open
  is refused with `I2C_TXQ_BLOCK_ERR_BUSY` when the block is open or its
  interrupt has a handler.
- The pads' pulls are off while the bus is open: the bus needs external
  pull-ups, 3.54kOhm at most for 300ns of rise on 100pF at 400kHz (NXP
  UM10204). The close leaves both pads inputs with the pull-down on.
- Every function is called from the core that opened the bus. Each masks
  the block's interrupt on that core while it works on the queue. The FIFO
  load runs with the core's interrupts off for at most 2 register writes
  of the address and 11 FIFO writes, and no wait: an interrupt between two
  entries would let the FIFO run empty without a STOP, and the block holds
  SCL low then. Nothing waits with interrupts off.
- The address goes to `IC_TAR` with the block disabled and enabled again,
  as pico-sdk 2.3.0 `i2c.c` does, and only when it changes.
- The end is reported after the STOP: on `STOP_DET`, and after an abort
  once `TX_ABRT` has come too. It is stamped with `time_us_64()` in the
  interrupt, which then starts the next transaction.
- The system clock must not change while a bus is open.

One bus at 400kHz, polled every 1ms:

```c
static i2c_txq_block_t bus;
static i2c_txq_xfer_t  rd;

i2c_txq_block_init(&bus);
i2c_txq_block_open(&bus, 16, 17, NULL);          /* I2C0 on GP16/GP17 */

rd.addr = 0x40; rd.wr[0] = 0x01; rd.wr_len = 1; rd.rd_len = 2;
rd.cls = I2C_TXQ_HARD;
rd.not_before_us = time_us_64();
rd.deadline_us = rd.not_before_us + 500;
i2c_txq_block_submit(&bus, &rd);
/* every 1ms: */
i2c_txq_block_poll(&bus);
/* rd.state == I2C_TXQ_ST_DONE: rd.result, rd.rd[0..1], rd.done_us */
```

## Measured and not measured

The host suite runs the core over a modelled bus
(`test/host/i2c_txq_model.c`) on a clock advanced 1us at a time
(`test_i2c_txq`, `test_i2c_txq_wire`).

Not measured: the driver has not run on a chip. The time the `IC_TAR`
change takes, the gap the block leaves between a STOP and the next START,
the interrupt latency, the order of `STOP_DET` and `TX_ABRT` after an
abort, and the CPU time a transaction costs are not measured.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/i2c_txq)
target_link_libraries(my_target PRIVATE rcbench_i2c_txq rcbench_i2c_txq_rp2350)
```

`add_subdirectory()` comes after `pico_sdk_init()`. The driver is an
INTERFACE library: `i2c_txq_block.c` compiles in the target that links it.
A host build links `rcbench_i2c_txq` alone; the folder `rp2350/` is added
only in a pico-sdk build.
