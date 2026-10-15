# sbus

Futaba S.BUS and S.BUS2: 16 channels in a frame of 25 bytes, decoded and
encoded; the S.BUS2 telemetry slots read after a frame; and an output that
sends a frame each period on a pin driver. The pin driver puts the wire on
one RP2350 pin.

| Part | Files | Depends on |
|---|---|---|
| Core | `sbus.c`, `sbus2.c`, `sbus_pio.c`, `sbus_out.c`, 4 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/sbus_line.pio`, `rp2350/sbus_line.c`, `rp2350/sbus_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

A driver with 3 functions (`sbus_driver_t`) connects the output to a pin;
`rp2350/` holds the one for the RP2350. The core is linked into the panel
image, the coprocessor image and the host tests. The pin driver is compiled
into the coprocessor image and called by no firmware. Neither part has been
run on hardware: no frame of this code has been sent to a servo or sensor.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
  `test/host/sbus_headers.cpp` calls each of the 4 parts from C++17 and
  links against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two
  outputs on two drivers run side by side.
- Sizes with arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`); gcc
  on x86-64 gives the same except where noted:

  | Object | Bytes |
  | --- | --- |
  | `sbus_out_t`, an output with its slot table | 960 (976 on x86-64) |
  | `sbus2_table_t`, 32 slots and the counters | 548 |
  | `sbus2_capture_t`, the bytes of one window | 260 |
  | `sbus_line_t`, one RP2350 line | 228 |
  | `sbus2_map_t` | 96 |
  | `sbus2_window_t` | 58 |
  | `sbus_frame_t` | 36 |

- Code with the same options: `sbus.c` 432 bytes, `sbus2.c` 1,394,
  `sbus_pio.c` 688, `sbus_out.c` 526. `sbus_line.c` is 1,572 bytes in the
  coprocessor image's build (`-O3`).
- Stack with the same options (`-fstack-usage`): `sbus_out_step()` 56
  bytes, `sbus2_window_decode()` 64, `sbus2_table_update()` 20,
  `sbus_encode()` 24, `sbus_pio_tx()` and `sbus_pio_capture()` 56 each. A
  step calls at most one of the driver's functions and the decoder, so its
  depth is under 200 bytes plus the driver's. Not measured in a running
  image.
- Non-blocking: `sbus_out_step()` reads the clock, polls the driver and
  returns. It never waits. Call it at least once per millisecond.
- The RP2350 pin driver claims 1 PIO (programmable input/output) state
  machine, 14 words of program space and 2 DMA (direct memory access)
  channels. It uses no interrupt.

## Wire

An 8E2 UART (universal asynchronous receiver-transmitter) at 100 kbaud,
inverted: the wire idles low and a start bit is high. Least significant bit
first, even parity, 2 stop bits. One byte is 12 bits, 120 us.

| Byte | Content |
| --- | --- |
| 0 | header 0x0F |
| 1 to 22 | 16 channels of 11 bits, least significant bit first |
| 23 | flags: ch17 0x01, ch18 0x02, frame lost 0x04, failsafe 0x08 |
| 24 | footer: 0x00 S.BUS; 0x04, 0x14, 0x24, 0x34 S.BUS2 |

| Quantity | Value |
| --- | --- |
| Frame on the wire | 25 x 120 us = 3.0 ms |
| Frame period, S.BUS | 14 ms default, 7 ms selectable; 7 ms to 50 ms accepted |
| Frame period, S.BUS2 | 14 ms default; 12 ms to 50 ms accepted |
| Raw channel range | 0 to 2047; 172 is 1000 us, 1811 is 2000 us, 992 is 1500 us |
| Frame boundary on receive | a gap of 2000 us or more (`SBUS_GAP_US`) |

S.BUS has no checksum. The header value occurs inside channel data, so the
decoder frames on silence and the caller passes a timestamp in microseconds
with every byte. The flags `failsafe` and `frame_lost` are decoded and not
acted on; a caller that drives anything from S.BUS treats `failsafe` as a
stop.

## S.BUS2 telemetry slots

After an S.BUS2 frame the sender releases the wire. The footer selects a
group of 8 of the 32 slots, and every sensor that holds a slot of that group
answers in its slot with 3 bytes in the same format: an ID byte and 2 data
bytes. The output rotates the footer 0x04, 0x14, 0x24, 0x34, so each slot
group is read every fourth frame: every 56 ms at a 14 ms period.

| Footer | Slots |
| --- | --- |
| 0x04 | 0 to 7 |
| 0x14 | 8 to 15 |
| 0x24 | 16 to 23 |
| 0x34 | 24 to 31 |

| Quantity | Value | Origin |
| --- | --- | --- |
| Slot 0 of a group, nominal start | 2000 us after the end of the footer | published; not measured |
| Slot spacing, nominal | 660 us | published; a capture shows about 700 us; not measured |
| Window of slot k | 200 us before to 400 us after its nominal start | this module; takes 660 us and 700 us spacing |
| Bytes of one record | 120 us apart; a gap above 200 us starts a new record | this module |
| Listening after the footer | 8000 us | this module; slot 7 at 700 us spacing ends by 7.26 ms |
| Slot ID | slot number as 5 bits written backwards, x 8, + 3: slot 1 is 0x83, slot 8 is 0x13 | slots 0 to 15 captured; 16 to 31 follow the rule and are not captured |

A record is taken when its 3 bytes have their parity and stop bits, its ID
names a slot of the footer's group and its first byte starts in that slot's
window. The first record of a slot is kept. Every other record is counted
by its reason, in this order: short, bad byte, wrong ID, early, late,
duplicate. A slot's value counts as live for 3 windows of its group after
its last record.

The bench takes the place of a receiver as the sender of frames. It
answers in no slot, slot 0 included: a Futaba receiver answers slot 0
itself, so slot 0 stays empty unless a receiver shares the wire.

### Sensor map and values

The map gives each slot a sensor, the sensor's part and an encoding. The
default is every sensor at its Futaba default slot; every slot is editable
with `sbus2_map_set()`.

| Slots | Sensor | Encoding |
| --- | --- | --- |
| 0 | receiver voltage (receiver battery and external input) | second data byte in 0.1 V |
| 1 | temperature, SBS-01T or SBS-01TE | first data byte less 100, in degrees Celsius |
| 2 | RPM, SBS-01RM, SBS-01RO or SBS-01RB | raw |
| 3 to 5 | altitude, SBS-01A | raw |
| 6 to 7 | voltage, SBS-01V | raw |
| 8 to 15 | GPS, SBS-01G | raw |
| 16 to 21 | servo sensor, SBS-01S | raw |
| 24 to 26 | current, SBS-01C | raw |
| 22, 23, 27 to 31 | none | raw |

Both encodings come from 2 captures each and are not measured on this
bench. The voltage encoding reads `03 C0 33` as 5.1 V and `03 C4 4F` as
7.9 V; the first data byte of slot 0 is not explained. The temperature
encoding is inferred: `83 81 80` reads as 29 C and `43 7E 80` as 26 C; the
second data byte is 0x80 in both and not read. The byte order of 16-bit
values, flag bits and the scale of RPM, altitude, vario, voltage, GPS,
current and servo data are not known, so those slots show their 2 data
bytes as hexadecimal, for example `C0 33`.

## Driver interface

```c
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const uint8_t *frame, uint32_t window_us);
    bool (*poll)(void *ctx, sbus2_capture_t *out);
    uint32_t (*now_us)(void *ctx);
} sbus_driver_t;
```

- Outside a transaction the driver holds the wire at its idle level, low.
- `start()` sends the 25 bytes at `frame`, then for `window_us` above 0
  releases the wire and records every byte that arrives until `window_us`
  after the end of the frame's last stop bit. It returns at once, `false`
  when a transaction runs or the line is not open.
- `poll()` returns `false` while the transaction runs and `true` once after
  it ends, with the bytes of the window, each with its data, parity and
  stop check and the time of its start bit in ns after the end of the
  footer.
- `now_us()` is a free-running microsecond clock. It may wrap.

## RP2350 pin driver

`rp2350/sbus_line.pio` is one program of 14 instructions at 8 state machine
cycles per bit: a divider of 187.5 at 150 MHz. Every system clock from
1.5625 MHz up gives a bit time within 0.1 % of 10 us; a clock below 800 kHz
is refused.

| Step | Timing at 8 cycles per bit (1.25 us a cycle) |
| --- | --- |
| Hand-over of a frame to its first start bit | 202.5 us: 2 cycles and 20 idle bits |
| End of the footer's last stop bit to the release of the pad | 2.5 us |
| To the first sample | 3.75 us |
| Sample interval while the wire idles | 2.5 us |
| Byte read: start bit seen to the next sample | 117.5 us |
| Start time of a byte | within 1.25 us |
| Window end | 2.5 us per sample counted; each byte adds 115 us, and the driver cuts a window that still runs at the time asked |

- The GPIO (general-purpose input/output) output and input overrides invert
  the pad in both directions; no external inverter is used. The pad's
  pull-down is on and its pull-up off from the open on.
- The pad drives push-pull for the frame and drives low between
  transactions. It never drives high outside the frame.
- A byte read at a sensor bit time 2 % off either way, back to back with
  the one before it, at any phase of the sample, decodes
  (`test/host/test_sbus_pio.c`, which runs the program in a model of the
  state machine). The sensors' tolerance of the bench's timing is not
  measured.
- The wire level is 3.3 V from the RP2350 pad. Whether S.BUS servos and
  sensors read a 3.3 V high at a 6 V to 7.4 V supply is not measured. The
  idle level of a released S.BUS2 wire, other than the pad's pull-down, is
  not measured.

The program shares its structure with `protocols/kst/rp2350/kst_line.pio`:
drive, release, capture in a window, drive again. The signalling differs
(UART with parity at 100 kbaud against Manchester half-cells of 25.40 us),
so it is a program of its own.

## Interface

`sbus.h`:

```c
void     sbus_decoder_reset(sbus_decoder_t *d);
bool     sbus_decode_byte(sbus_decoder_t *d, uint8_t byte, uint32_t now_us,
                          sbus_frame_t *out);
uint16_t sbus_to_us(uint16_t raw);
uint16_t sbus_from_us(uint16_t us);
void     sbus_encode(const sbus_frame_t *f, uint8_t footer, uint8_t *out);
```

`sbus2.h`: `sbus2_footer()`, `sbus2_footer_group()`, `sbus2_slot_id()`,
`sbus2_id_slot()`, `sbus2_timing_default()`, `sbus2_window_decode()`,
`sbus2_table_reset()`, `sbus2_table_update()`, `sbus2_slot_live()`,
`sbus2_map_default()`, `sbus2_map_set()`, `sbus2_value()`,
`sbus2_value_text()`.

`sbus_out.h`:

```c
void           sbus_out_init(sbus_out_t *o, const sbus_driver_t *drv);
sbus_out_err_t sbus_out_config(sbus_out_t *o, unsigned mode,
                               uint32_t period_us);
bool           sbus_out_set_raw(sbus_out_t *o, unsigned ch, uint16_t raw);
bool           sbus_out_set_us(sbus_out_t *o, unsigned ch, uint16_t us);
void           sbus_out_set_flags(sbus_out_t *o, bool ch17, bool ch18,
                                  bool frame_lost, bool failsafe);
void           sbus_out_start(sbus_out_t *o);
void           sbus_out_stop(sbus_out_t *o);
void           sbus_out_clear_slots(sbus_out_t *o);
void           sbus_out_step(sbus_out_t *o);
```

`sbus_pio.h`: `sbus_pio_clock()`, `sbus_pio_tx()`, `sbus_pio_capture()`,
`sbus_pio_parity()`, the arithmetic of the pin driver without hardware
access.

`rp2350/sbus_line.h`: `sbus_line_init()`, `sbus_line_open()`,
`sbus_line_close()`, `sbus_line_driver()`.

The output starts with every channel at 992 (1500 us) and the flags 0. A
step starts a frame when one is due, and the schedule does not drift: the
next frame is due one period after the last was due. A frame started a
whole period late restarts the schedule and is counted. A caller that
drives servos starts the output only while the bench is ARMED and stops it
on STOP and SAFE; a transaction on the wire at the stop ends, its window is
read, and no frame follows.

## Use

```c
sbus_line_t line;
sbus_out_t out;

sbus_line_init(&line);
if (sbus_line_open(&line, 13) == SBUS_LINE_OK) {
    const sbus_driver_t drv = sbus_line_driver(&line);

    sbus_out_init(&out, &drv);
    sbus_out_config(&out, SBUS_MODE_SBUS2, SBUS_PERIOD_US);
    sbus_out_set_us(&out, 0, 1500);
    sbus_out_start(&out);
}
for (;;) {
    sbus_out_step(&out);          /* every 1 ms or more often */
    if (sbus2_slot_live(&out.table, 1)) {
        sbus2_map_t map;
        sbus2_value_t v;
        char text[SBUS2_VALUE_TEXT];

        sbus2_map_default(&map);
        sbus2_value(map.slot[1].decode, out.table.slot[1].data, &v);
        sbus2_value_text(&v, text, sizeof(text));   /* "29 C" */
    }
}
```

In another pico-sdk project:

```cmake
add_subdirectory(protocols/sbus)
target_link_libraries(my_target PRIVATE rcbench_sbus rcbench_sbus_rp2350)
```

## Sources

- Futaba T16IZ SUPER instruction manual, English: slot counts, allowed start
  slots and default slots of each sensor, S.BUS2 compatibility.
- Futaba sensor manuals SBS-01S, SBS-01G, SBS-01C, SBS-01RB, SBS-01TE,
  SBS-01V and SBS-01A: measuring ranges, slot counts, default slots.
- Futaba R6208SB receiver manual: normal and high-speed mode; high-speed
  mode needs digital or S.BUS servos. The 14 ms and 7 ms periods of the
  two modes are not stated there and not measured.
- Published logic-analyser and oscilloscope captures of a Futaba R7008SB
  receiver with T18MZ and T14SG transmitters and Futaba sensors: footers,
  slot IDs of slots 0 to 15, slot spacing of about 700 us, the records
  quoted above.

Futaba publishes no specification of the S.BUS2 slots. The slot-0 delay of
2000 us and the 660 us spacing are published figures that are not
measured on this bench, and the slot IDs of slots 16 to 31 are inferred
from the rule.

## Trademarks

Futaba, S.BUS and S.BUS2 are names or trademarks of their respective
owners; they are used here only to say which receivers, servos and sensors
this module works with. The project is not affiliated with them. No vendor
code is included.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).
