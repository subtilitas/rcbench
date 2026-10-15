# feetech

A bus master for Feetech SCS and STS serial bus servos on one signal wire.
The core builds instruction frames, decodes status frames, holds the
register tables of both series with their byte order, and runs a session:
scan, read, write, EEPROM (electrically erasable programmable read-only
memory) write with unlock, lock and read-back, torque off, and the periodic
read of one servo's live values. The pin driver puts the bus on one RP2350
pin.

| Part | Files | Depends on |
|---|---|---|
| Core | `feetech_wire.c`, `feetech_reg.c`, `feetech_bus.c`, `feetech_pio.c`, 4 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/ft_line.pio`, `rp2350/ft_line.c`, `rp2350/ft_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

A driver with 3 functions (`feetech_driver_t`) connects the core to a pin;
`rp2350/` holds the one for the RP2350. Neither part has been run on
hardware: no frame of this code has been sent to a servo.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
  `test/host/feetech_headers.cpp` calls each of the 4 parts from C++17 and
  links against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two
  sessions on two drivers run side by side.
- A session object is 284 bytes with arm-none-eabi-gcc 14.2
  (`-mcpu=cortex-m33 -mthumb -Os`) and 304 bytes with gcc on x86-64. A
  frame is 65 bytes, a capture 67 bytes, a decoded status frame 20 bytes.
- Non-blocking: `feetech_bus_step()` polls the driver, starts what is due
  and returns. It never waits.
- SCS and STS servos on a TTL (transistor-transistor logic) single wire.
  SMS servos use the STS register table on an RS-485 pair; the core builds
  their frames, and the RP2350 pin driver does not drive RS-485.

## Modules

| Header | Contents |
| --- | --- |
| `feetech_wire.h` | instruction frames: PING, READ, WRITE, REG WRITE, ACTION, RESET, SYNC WRITE and any other instruction byte; the status frame decoder with 8 result codes; the echo check |
| `feetech_reg.h` | the registers by role for SCS and STS, their ranges and byte order; the 8 baud rates of register 6; sign-and-magnitude values; the live block decoded |
| `feetech_bus.h` | the session: scan, read, write, EEPROM write, torque off, the watch, stop; the driver interface; 13 result codes |
| `feetech_pio.h` | the arithmetic of the pin driver, without hardware access: the clock divider for a rate, a frame as the words the PIO (programmable input/output) program reads, a received word as a byte |

## Wire

Half duplex on one signal wire, asynchronous serial, 8 data bits, no
parity, 1 stop bit, least significant bit first, idle high.

```
instruction  FF FF ID LEN INSTR P1 .. Pn CHK
status       FF FF ID LEN ERR   P1 .. Pn CHK
```

| Quantity | Value |
| --- | --- |
| LEN | n + 2 |
| CHK | NOT of the low byte of ID + LEN + INSTR (or ERR) + P1 .. Pn |
| IDs | 0 to 253 one servo; 254 (0xFE) every servo, no reply except to PING |
| Baud rates, register 6 index 0 to 7 | 1,000,000; 500,000; 250,000; 128,000; 115,200; 76,800; 57,600; 38,400 |
| Factory setting | ID 1, index 0 (1,000,000 baud) |
| Two-byte values | STS: low byte at the lower address. SCS: high byte at the lower address |
| EEPROM area | addresses 0 to 39; kept over a power cycle while the lock flag (STS 55, SCS 48) is 0 |
| Longest frame built | 64 bytes; READ and WRITE carry 1 to 16 data bytes |

There is no byte stuffing. The decoder finds a frame by its header, its
length and its checksum; on a mismatch it drops the first byte and looks
again. Every failed check has its own result (`feetech_rx_t`): no byte, a
frame cut short, a wrong checksum, a good frame from another ID, another
parameter count than asked, bytes and no frame, and a stop bit low.

### Session timing

| Quantity | Value |
| --- | --- |
| Reply window after the frame | 1,000 us plus twice the reply's own wire time |
| Silence between two transactions | 100 us |
| Driver that has not ended a transaction | ended with `FEETECH_RES_DRIVER` 5 ms after the window |
| Live read of the watched servo | every 50 ms, between operations |
| Live block | 11 bytes from address 56 on SCS, 15 bytes on STS; empty after a read that fails; the reply of a read for a watch changed while it was on the wire is dropped |
| Starts of one stop frame the driver refuses | 3 (`FEETECH_BUS_STOP_TRIES`), one pass apart |

None of these times is measured on a servo.

### EEPROM write

`feetech_bus_write_ee()` writes the lock flag 0, the bytes, the lock flag
1, then reads the bytes back and compares them (`FEETECH_RES_VERIFY` on a
difference). A write that moves the rate (register 6) first sends torque
off (register 40 := 0) to the broadcast ID at the old rate: every other
servo stays at that rate, where no later frame of the session reaches it.
A write that moves the servo's ID (register 5) or rate is followed: the lock and the read go to the new ID at the new
rate. When neither that write nor the lock after it is answered, the write
counts as lost: the line goes back to the old rate, the lock goes to the
old ID, and the operation ends with the write's result. A servo that took
the write and answered neither frame stays unlocked at the new ID; whether
that happens is not measured. The lock is sent after a failed unlock or
write as well. A write of the lock flag alone is that one write and its
read; it settles a lock the session owes that servo: 1 locks it, 0 leaves
it open on purpose, and a stop sends no lock to it.

The session owes a lock to one servo at a time: a lock that went
unanswered stays owed. An EEPROM write to another servo sends that lock
first; when it is not answered the write ends with its result and sends
nothing else.

`feetech_bus_stop()` ends the operation and the watch, sends torque off
(register 40 := 0) to the broadcast ID and sends the lock flag 1 to a servo
the session left unlocked. When the frame on the wire at the stop is an
EEPROM write that moves the rate, torque off goes out at the old rate and
at the new one. A stop frame the driver refuses 3 times in a row is left
out, and `stop_res` reads `FEETECH_RES_DRIVER` until the next stop; a lock
left out stays owed. The lock is the one frame of a stop that expects an
answer: the debt ends when the servo answers it. One unanswered stays owed,
and `stop_res` reads what came back (`FEETECH_RES_NO_REPLY` when nothing
did).

## Interface

```c
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const uint8_t *tx, unsigned n_tx, unsigned n_rx,
                  uint32_t wait_us);
    bool (*poll)(void *ctx, feetech_capture_t *out);
    bool (*set_baud)(void *ctx, uint32_t baud);
} feetech_driver_t;
```

- `start()` sends `n_tx` bytes, releases the line after the last stop bit
  and keeps the bytes that come until `n_rx` came or `wait_us` passed.
  `n_rx` counts the reply's bytes only. With `n_rx` 0 the transaction ends
  with the last stop bit. It returns at once, `false` when it cannot start.
- `poll()` returns `false` while the transaction runs and `true` once at
  its end, with the bytes, the count of bytes whose stop bit was low, and
  whether more came than were kept.
- `set_baud()` moves the line to another rate for the next transaction. It
  is never called while a transaction runs.
- A driver that hears its own frame hands it back ahead of the reply and
  keeps `n_tx` + `n_rx` bytes; the session counts the echo off
  (`feetech_echo()`).

## Use

```c
feetech_bus_t bus;
feetech_driver_t drv = ft_line_driver(&line);

feetech_bus_init(&bus, &drv, 1000000u);
feetech_bus_scan(&bus, 1, 20);
while (feetech_bus_step(&bus, time_us_32()) == FEETECH_RES_BUSY) {
    /* other work */
}
if (feetech_bus_found(&bus, 1)) {
    uint8_t goal[2];

    feetech_put16(FEETECH_STS, goal, 2048);
    feetech_bus_write(&bus, 1, FEETECH_REG_GOAL, goal, 2);
    feetech_bus_watch(&bus, 1, FEETECH_STS);
}
```

A PING answer does not say which series a servo is. The caller keeps the
series per servo and passes it to `feetech_bus_write_ee()`,
`feetech_bus_watch()`, `feetech_put16()` and `feetech_live()`.

## RP2350 pin driver

`ft_line_open(line, pin, baud)` claims 1 state machine, 22 words of program
space in a PIO block that reaches the pin, and 2 DMA (direct memory access)
channels. `ft_line_close()` gives them back. No interrupt and no PIO
interrupt flag is used.

| Quantity | Value |
| --- | --- |
| Bit | 8 state machine cycles; the divider from `clock_get_hz(clk_sys)`, refused above 0.5 % error |
| Divider error, every Feetech rate at 150 MHz and 125 MHz | 0.01 % or less |
| Pad between transactions | input, pull-up on |
| Pad before the first start bit | driven high for 10 cycles (1.25 bits) |
| Stop bit | 8 cycles after the last byte, 10 cycles between two bytes |
| Pad after the last stop bit | input, pull-up on |
| Receive sample | data bit 0 at 11 cycles after the start edge, then every 8 cycles; stop bit at 75 cycles |
| Rate error of a reply decoded in the host model | -3 % to +3 % |
| Pad after the close | input, pull-down on |

The pad drives push-pull at 3.3 V while it sends. Whether a servo's TTL
line tolerates that, and whether a servo holds the line high with the
internal pull-up of 50 to 80 kOhm alone, is not measured.

## Use in another pico-sdk project

```cmake
add_subdirectory(path/to/protocols/feetech feetech)
target_link_libraries(my_app PRIVATE rcbench_feetech rcbench_feetech_rp2350)
```

`rp2350/CMakeLists.txt` runs `pico_generate_pio_header()` on
`ft_line.pio`; the folder needs nothing else from this repository.

## What is measured and what is not

Nothing of this module is measured on a servo. The frames are held to the
23 examples of the manual, the instruction generator workbook and a
third-party table (`test/host/test_feetech_wire.c`). These 20 items need a
servo on a bench:

1. The idle level of the signal line with the servo powered and no master
   attached; whether the servo pulls it high.
2. The servo's output stage (push-pull or open drain) and its high level on
   an SCS15, an STS3215 and an SMS servo.
3. Whether a master pin driven high back-feeds an unpowered servo.
4. The delay from the master's last stop bit to a reply's first start bit at
   1,000,000 baud, per model; on SCS with register 7 at 0 and at 254.
5. Whether STS has an adjustable return delay (its table lists address 7 as
   reserved).
6. The gap between two bytes of one reply.
7. The time from a WRITE to its status frame, and from a goal position
   write to the first motion.
8. What a scan reads when two servos share one ID.
9. The duration of an EEPROM write and the number of writes a servo
   tolerates; neither is published.
10. Whether an STS3215 and an SMS servo answer a 15-byte READ from
    address 56.
11. The sign of the present load and of the present current, and the
    scale of the present current (6.5 mA a count, from one third-party
    source) against a meter. The module takes bit 15 of the current as a
    direction bit, as in present speed, and drops it.
12. Whether SYNC READ (0x82) works on the STS3215, and on which firmware.
    The core does not send it.
13. Whether instructions 0x08 (reboot), 0x09 (backup) and 0x0B (calibrate)
    exist on a given firmware. The core does not send them.
14. What torque enable 2 does on STS.
15. The model numbers at registers 3 and 4 of an SCS15, an STS3215 and an
    SMS servo; with them a scan could tell the series.
16. An SCS and an STS servo on one bus while the master writes in both byte
    orders.
17. Whether other SCS models (SCS0009, SCS225, SCS2332) share the SCS15
    addresses.
18. Whether goal speed 0 means the highest speed or no motion in position
    mode. The BUS page sends 1000 by default.
19. Whether a servo with its lock flag at 1 acknowledges an EEPROM write
    and discards it. The read-back after every EEPROM write catches it.
20. The PIO receiver's resynchronisation after a framing error on a real
    line; the host model covers one framing error.

## Trademarks

Feetech is a name or trademark of its owner; it is used here only to say
which servos this module works with. The project is not affiliated with
it. No vendor code is included. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).

## Tests

| Suite | Under test |
| --- | --- |
| `test_feetech_wire` | the 23 published frames, the derived SCS frame, the decoder's faults, the builders' limits |
| `test_feetech_reg` | byte order per series, the tables, the rates, sign and magnitude, the live block |
| `test_feetech_bus` | the session against simulated servos (`feetech_sim.h`): scan ends 0 and 253, 254 refused, every reply fault, a driver that hangs, EEPROM writes in both byte orders, an ID or rate write that never arrived, the lock flag written on purpose, a lock owed to one servo before a write to another, STOP during a scan and an EEPROM write, stop frames the driver refuses, the watch and its block emptied by a failed read, a read for a watch changed on the wire, a driver that hears its own frame, the clock wrapping |
| `test_feetech_pio` | `ft_line.pio` assembled and run in a cycle model: the frame's timing, replies 3 % off the rate at 8 phases, a framing error, the dividers |
| `feetech_headers_cxx` | the 4 headers as C++17 |

rcbench's use of the module is tested apart from it: `test_bus_port` (the
coprocessor's BUS pages on the programming port), `test_bus_link` (the
panel's link module) and `test_bus_panel` (the BUS page on the SERVO
screen), all against `feetech_sim.h`.

## Sources

- Feetech, "Serial Bus Smart Control servo, Communication Protocol
  Manual", V1.01, 2019-02-19: frame, checksum, instructions, examples.
- Feetech, SCS15 memory table, version 0.5: the SCS register table.
- Feetech, instruction generator workbook and ST-3215-C047 datasheet: the
  STS register table and frames.
