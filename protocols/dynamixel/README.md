# dynamixel

Protocol 2.0 of ROBOTIS Dynamixel servos on a half-duplex TTL (transistor-
transistor logic) bus. The core holds the packets with their CRC (cyclic
redundancy check) and byte stuffing, the X-series control table, one
transaction at a time with its deadline, and the ID scan. The pin driver
puts the bus on one RP2350 pin.

| Part | Files | Depends on |
|---|---|---|
| Core | `dxl_packet.c`, `dxl_table.c`, `dxl_bus.c`, `dxl_pio.c`, 4 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/dxl_line.pio`, `rp2350/dxl_line.c`, `rp2350/dxl_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

A driver with 5 functions (`dxl_driver_t`) connects the core to a pin;
`rp2350/` holds the one for the RP2350. Neither part has been run on
hardware: no packet of this code has reached a servo. The module is built
from the ROBOTIS e-Manual (sources below).

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`). `test/host/dxl_headers.cpp`
  calls each of the 4 core parts from C++17 and links against the C
  library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two buses
  on two drivers run side by side.
- Object sizes with arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`)
  and with gcc on x86-64:

  | Object | Cortex-M33 | x86-64 |
  | --- | --- | --- |
  | `dxl_bus_t`, one bus | 568 bytes | 592 bytes |
  | `dxl_scan_t`, one scan | 148 bytes | 152 bytes |
  | `dxl_packet_t`, one parsed packet | 136 bytes | 136 bytes |
  | `dxl_line_t`, one RP2350 line | 668 bytes | 680 bytes |
  | `dxl_driver_t` | 24 bytes | 48 bytes |

- Code size of the core with the same compiler and options: 6,139 bytes
  (`dxl_packet.c` 1,256, `dxl_table.c` 2,499, `dxl_bus.c` 2,022,
  `dxl_pio.c` 362). Stack not measured.
- Non-blocking: `dxl_bus_step()` and `dxl_scan_step()` read the clock, poll
  the driver and return. Nothing waits.
- FACTORY_RESET is sent only through `dxl_factory_reset()` and
  `dxl_bus_factory_reset()`, with the confirmation word
  `DXL_FACTORY_RESET_CONFIRM` (0x46525354) and one servo ID. `dxl_encode()`
  refuses the instruction, and the broadcast ID is refused for it.

## Modules

| Header | What it does |
| --- | --- |
| `dxl_packet.h` | CRC-16 (polynomial 0x8005, initial value 0, most significant bit first), instruction packet encoder with byte stuffing, status packet parser with destuffing, the instructions PING, READ, WRITE, REG_WRITE, ACTION, REBOOT, FACTORY_RESET, SYNC_READ and SYNC_WRITE |
| `dxl_table.h` | the X-series control table: 42 fields with address, size, EEPROM or RAM, access, unit and range; the write class of an address range; the Baud Rate register; the model numbers; unit conversions; the restore of an EEPROM backup |
| `dxl_bus.h` | one transaction at a time on a `dxl_driver_t`, its deadline and its checks; the ID scan |
| `dxl_pio.h` | the RP2350 driver's arithmetic: clock divider, rate error, jitter, transmit stream |

## Wire

8 data bits, no parity, 1 stop bit (8N1), least significant bit first, idle
high, one wire for both directions. The master sends an instruction packet
and releases the line; the addressed servo answers with a status packet
after its Return Delay Time.

| Quantity | Value |
| --- | --- |
| Rates | 9,600, 57,600 (servo default), 115,200, 1,000,000, 2,000,000, 3,000,000, 4,000,000 and 4,500,000 bit/s: index 0 to 7 of the Baud Rate register (address 8) |
| IDs | 0 to 252; 254 is the broadcast ID; 253 and 255 are no IDs |
| Header | FF FF FD 00, then ID, Length (2 bytes), instruction, parameters, CRC (2 bytes) |
| Byte stuffing | FF FF FD in the instruction, ERR byte and parameters gets FD after it; the CRC covers the stuffed packet |
| Parameters | `DXL_PARAM_MAX` (128) after destuffing, a choice of this module; the servo's receive buffer size is not published |
| Longest packet on the wire | `DXL_WIRE_MAX` (182) bytes: 7 header, 130 body, 43 stuffing, 2 CRC |
| Return Delay Time (address 9) | 0 to 254 steps of 2 us; servo default 250, 500 us |
| Status Return Level (address 68) | 0: PING only, 1: also READ, 2: all (servo default) |

A write to the broadcast ID and SYNC_WRITE get no reply. A broadcast PING
draws one reply from every servo and is refused by `dxl_bus_ping()`; the
scan pings one ID at a time.

### Deadline

A transaction ends at its deadline, counted from `start()`:

```text
deadline = (DXL_LEAD_BITS + DXL_TX_BITS_PER_BYTE x bytes sent) bit times
         + the Return Delay Time the bus assumes
         + 10 x bytes of the longest reply, stuffing included, bit times
         + DXL_MARGIN_US
```

`DXL_LEAD_BITS` is 20, `DXL_TX_BITS_PER_BYTE` 11 (10, and a stop bit up to 1
bit time longer), `DXL_MARGIN_US` 1,000 us. The bus assumes a Return Delay
Time of 508 us, the longest, until `dxl_bus_set_return_delay()` sets the
servo's. The margin covers the servo's processing time, which ROBOTIS does
not publish, and the driver's start; it is a choice and not measured.

| Rate, bit/s | Deadline of a PING (10 bytes out, 15 back at most) | 253 IDs at 1 rate |
| --- | --- | --- |
| 9,600 | 30,675 us | 7.8 s |
| 57,600 | 6,370 us | 1.6 s |
| 115,200 | 3,940 us | 1.0 s |
| 1,000,000 | 1,788 us | 0.45 s |
| 4,500,000 | 1,571 us | 0.40 s |
| all 8 rates | | 12.4 s |

The scan times are for IDs that do not answer, at the assumed 508 us, and
leave out the time between `dxl_scan_step()` calls.

### Checks on a reply

`dxl_bus_step()` ends a transaction with its own code for each failed check
(`dxl_res_t`):

| Code | Condition |
| --- | --- |
| `DXL_ERR_TIMEOUT` | no byte by the deadline |
| `DXL_ERR_TRUNCATED` | bytes came, no whole packet by the deadline |
| `DXL_ERR_CRC` | a whole packet whose CRC does not fit |
| `DXL_ERR_LENGTH` | Length too short, or a packet longer than `DXL_WIRE_MAX` |
| `DXL_ERR_STUFFING` | FF FF FD not followed by FD |
| `DXL_ERR_NOT_STATUS` | a packet that is not a status packet (instruction 0x55) |
| `DXL_ERR_ID` | a status packet from another ID |
| `DXL_ERR_PARAMS` | not the parameter count the instruction asks for |
| `DXL_ERR_STATUS` | the ERR byte carries an error number (bits 0 to 6); the alert bit (bit 7) alone passes |

Bytes before a header are dropped and counted in `noise`. After a write of
1 byte to ID (address 7) the reply is taken from the old and from the new
ID: which one the servo uses is not published.

## Control table

The X-series table as the XL430-W250 page gives it, with the 3 current
fields of the XM430 (Current Limit 38, Goal Current 102, Present Current
126). Address 126 is Present Load in 0.1 % on the XL430-W250 and Present
Current in 2.69 mA on the XM430; `dxl_model_t.current` says which. Other
Protocol 2.0 families (MX 2.0, PRO, P, Y) have other tables and are not
described.

| Area | Addresses | On a write |
| --- | --- | --- |
| EEPROM | 0 to 63 | kept over a power cycle; taken only while Torque Enable (64) is 0; every write wears the servo's flash, endurance not published |
| RAM | 64 to 146 | back to the default at power-up |

`dxl_write_class(addr, len)` classifies a write: `DXL_WR_RAM` for whole
writable RAM fields, `DXL_WR_EEPROM` when one EEPROM field is among them,
`DXL_WR_READ_ONLY` for a read-only field, and `DXL_WR_UNKNOWN` for an
address no field has, part of a field, or Protocol Type (13), which this
module never writes. 18 EEPROM fields and 10 RAM fields are writable.

Fields with their units: Model Number (0), Firmware Version (6), ID (7),
Baud Rate (8), Return Delay Time (9, 2 us), Min and Max Position Limit (52,
48, 0 to 4095 pulses of 0.088 deg), Temperature Limit (31, 1 deg C), Min
and Max Voltage Limit (34, 32, 0.1 V), Torque Enable (64), LED (65),
Status Return Level (68), Hardware Error Status (70), Profile Acceleration
(108), Profile Velocity (112, 0.229 rev/min), Goal Position (116), Present
Load or Current (126), Present Position (132), Present Input Voltage (144,
0.1 V), Present Temperature (146, 1 deg C), and the rest of the table in
`dxl_table.c`.

Hardware Error Status bits as the XM430-W350 page states them: 0x01 input
voltage, 0x04 overheating, 0x08 motor encoder, 0x10 electrical shock, 0x20
overload. The XL430-W250 page as read names 0x10 for the input voltage; which
is right for the XL430 is not known.

Known models; any other is shown by its number:

| Model Number | Model | Address 126 |
| --- | --- | --- |
| 1060 | XL430-W250 | Present Load |
| 1030 | XM430-W210 | Present Current |

`dxl_restore_plan()` turns an EEPROM backup (64 bytes from address 0) and the
servo's EEPROM now into the writes that restore it: the writable fields that
differ, in address order, then ID, then Baud Rate. After the ID write the
servo answers at the backup's ID, after the Baud Rate write at the backup's
rate. A backup with a value out of a field's range is refused with that
field's address.

## Interface

```c
typedef struct {
    void *ctx;
    bool (*set_baud)(void *ctx, uint32_t baud);
    bool (*start)(void *ctx, const uint8_t *tx, size_t n);
    size_t (*poll)(void *ctx, uint8_t *out, size_t cap);
    void (*stop)(void *ctx);
    uint32_t (*now_us)(void *ctx);
} dxl_driver_t;
```

- `set_baud()` sets the rate of the next transactions, `false` for a rate the
  driver cannot make. It is not called while a transaction runs.
- `start()` sends the bytes 8N1 with at most `DXL_LEAD_BITS` (20) bit times
  of idle high before the first start bit and at most 11 bit times per byte,
  then releases the line and collects what comes back. It returns at once.
- `poll()` copies the bytes received since the last call. A byte with a low
  stop bit is dropped.
- `stop()` ends the transaction; the line stays released.
- `now_us()` is a free-running microsecond clock. It may wrap.

## Use

```c
dxl_bus_t bus;
dxl_bus_init(&bus, &driver);
dxl_bus_set_baud(&bus, 57600);

dxl_bus_ping(&bus, 1);
while (dxl_bus_step(&bus) == DXL_BUSY) { /* call every 1 ms or faster */ }
if (bus.result == DXL_OK) {
    const uint16_t model = dxl_get_u16(&bus.status.params[0]);
    const dxl_model_t *m = dxl_model_find(model);   /* NULL: show the number */
}

dxl_bus_read(&bus, 1, 0, DXL_EEPROM_SIZE);          /* the backup */
while (dxl_bus_step(&bus) == DXL_BUSY) { }

const uint8_t goal[4] = { 0x00, 0x08, 0x00, 0x00 }; /* 2048 pulses, 180 deg */
dxl_bus_write(&bus, 1, 116, goal, sizeof(goal));
while (dxl_bus_step(&bus) == DXL_BUSY) { }

dxl_scan_t scan;
dxl_scan_start(&scan, &bus, 0, 252, 0x02);          /* 57,600 bit/s only */
while (dxl_scan_step(&scan) == DXL_BUSY) { }
/* scan.found[0 .. n_found - 1]: ID, rate, model, firmware, flags */
```

The scan pings each ID from `first` to `last` at each rate of the mask, and
lists up to `DXL_SCAN_FOUND_MAX` (16) servos; more are counted in
`overflow`. Each ping collects bytes until its deadline, also after a valid
reply, so a second servo on the same ID shows as `DXL_FOUND_EXTRA` or, when
the replies collide, `DXL_FOUND_GARBLED`. A rate the driver refuses is
skipped and set in `refused_mask`. IDs 253, 254 and 255 are refused as scan
bounds.

The caller's part: stop every other use of the pin, check the servo supply,
hold the backup, ask for a confirmation before an EEPROM write, and set
Torque Enable to 0 before one.

## RP2350 pin driver

`rp2350/dxl_line.c` implements `dxl_driver_t` on one pin. Its header
includes no pico-sdk header and has `extern "C"` guards.

| Resource | Use |
| --- | --- |
| PIO state machines | 1 per open line, in a PIO block that reaches the pin |
| PIO program space | 22 of 32 words |
| DMA (direct memory access) channels | 2 per open line: the bytes of a packet and the bytes of its reply |
| Interrupts, PIO interrupt flags | none |
| Memory | 1 `dxl_line_t` per line, held by the caller; no static state |

The KST line program (`protocols/kst`, 24 words) and this one (22 words) do
not fit one PIO block together.

The program runs 8 state machine cycles a bit. The clock divider is the
system clock over 8 times the rate, in 1/256 steps; `dxl_pio_divider()`
refuses a rate whose mean error exceeds 2 % (`DXL_PIO_ERROR_MAX_PPM`) or
whose divider falls outside 1 to 65,536. The rounding keeps the mean error
at or under 0.2 %. A fractional divider moves an edge by up to 1 system
clock: the jitter column.

| Rate, bit/s | Divider at 150 MHz | Mean error | Jitter, of a bit |
| --- | --- | --- | --- |
| 9,600 | 1953 + 32/256 | 0 | 0.006 % |
| 57,600 | 325 + 133/256 | +0.0004 % | 0.04 % |
| 115,200 | 162 + 195/256 | -0.0008 % | 0.08 % |
| 1,000,000 | 18 + 192/256 | 0 | 0.67 % |
| 2,000,000 | 9 + 96/256 | 0 | 1.3 % |
| 3,000,000 | 6 + 64/256 | 0 | 2.0 % |
| 4,000,000 | 4 + 176/256 | 0 | 2.7 % |
| 4,500,000 | 4 + 43/256 | -0.03 % (4,498,594 bit/s) | 3.0 % |

| Quantity | Value |
| --- | --- |
| Idle high before the first start bit | 158 cycles, 19.75 bit times |
| A byte sent | 83 cycles: start bit 8, 8 data bits of 8, stop bit 11 |
| Release | 9 cycles into the last stop bit, then the pad is an input |
| Receive sample | 12 cycles (1.5 bit times) after the start bit's falling edge, then every 8 cycles; the stop bit at 9.5 bit times |
| Rate window, bytes back to back | -2 % to +2 % at every phase; +3 % loses bytes |
| Rate window, 1 bit time between bytes | -4 % to +4 % |
| Reply bytes kept per transaction | `DXL_LINE_RX_BYTES` (364) |

The rate window is that of the model in `test/host/test_dxl_pio.c`. The
servo's clock error is not published.

The pad is an input with its pull-up on from the open on and between
transactions: the line idles high through the pull-up and the servo. A
transaction drives the pad high, sends the packet push-pull and releases
it. The internal pull-up is 50 to 80 kOhm. A pull-down on the board holds
the idle line low, which a servo reads as a break; whether the bench's
programming port needs its pull-down switched for this bus is not known.

The servo's DATA pin is 5 V TTL. The RP2350 GPIO0 to 39 are rated 5.5 V
with IOVDD at 3.3 V and 3.63 V with IOVDD at 0 V. Whether a servo reads the
RP2350's 3.3 V high as high is not known.

```c
static dxl_line_t line;
dxl_bus_t bus;

dxl_line_init(&line);
if (dxl_line_open(&line, 13, 57600) == DXL_LINE_OK) { /* input, pulled up */
    const dxl_driver_t driver = dxl_line_driver(&line);

    dxl_bus_init(&bus, &driver);
    dxl_bus_set_baud(&bus, 57600);
    dxl_bus_ping(&bus, 1);
    while (dxl_bus_step(&bus) == DXL_BUSY) { }
}
dxl_line_close(&line);
```

Limits:

- The system clock must not change while a line is open.
- Every function of one line is called from one context.
- The program does not read while it sends: no echo, and a servo that
  answers early, before the release, is not heard.

The host suite assembles `dxl_line.pio` with its own assembler for the
instructions the program uses, runs the words in a model of the state
machine and holds them to `test/host/fixtures/dxl_line_words.txt`
(`test_dxl_pio`). The coprocessor build of this repository compares the
words `pioasm` produced with that file.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/dynamixel)
target_link_libraries(my_target PRIVATE rcbench_dynamixel rcbench_dynamixel_rp2350)
```

`rcbench_dynamixel` is the core. `rcbench_dynamixel_rp2350` exists when the
pico-sdk is initialised before the `add_subdirectory()`; it is an INTERFACE
library, so `dxl_line.c` compiles in the target that links it. A project on
another chip links `rcbench_dynamixel` and supplies its own
`dxl_driver_t`. Nothing in the folder names a file outside it.

## Not supported

- Protocol 1.0, and families other than the X series.
- The reply of SYNC_READ (one status packet per servo) and BULK_READ as a
  transaction; `dxl_sync_read()` only encodes the instruction.
- FAST_SYNC_READ and FAST_BULK_READ replies: one packet, not stuffed, with
  several servos in it. `dxl_parse()` does not parse them.
- A broadcast PING as a transaction.
- RS-485 models: other electrical layer, not described here.

## What is measured and what is not

Nothing of this module has run on hardware. The packets are held to the 20
packet examples of the Protocol 2.0 page, whose CRCs were recomputed by an
independent program. The transactions run against the bus model in
`test/host/dxl_sim.h`, the pin driver's program against the state machine
model in `test/host/test_dxl_pio.c`.

Not measured, and needed from a bench before the bus is relied on:

1. The servo's DATA input threshold and whether the RP2350's 3.3 V high is
   read as high; the servo's idle level and pull-up.
2. The level the servo drives and its edges at 1 to 4.5 Mbit/s on a bench
   wire.
3. The time from the last CRC byte to the first reply byte at Return Delay
   Time 250 and 0, for PING, READ and WRITE; whether `DXL_MARGIN_US` covers
   the servo's processing.
4. Reply order and collisions for several servos.
5. Whether the servo takes 4,500,000 bit/s from the RP2350 at -0.03 % and
   3.0 % jitter.
6. The time a servo needs after REBOOT, an ID or Baud Rate write and a
   FACTORY_RESET before it answers.
7. The servo's receive buffer size.
8. The ID the reply to a write of ID carries.
9. EEPROM write endurance.
10. Whether the bench's programming port needs its pull-down switched for
    this bus.

## Sources

- Protocol 2.0: <https://emanual.robotis.com/docs/en/dxl/protocol2/>
- CRC: <https://emanual.robotis.com/docs/en/dxl/crc/>
- XL430-W250: <https://emanual.robotis.com/docs/en/dxl/x/xl430-w250/>
- XM430-W350: <https://emanual.robotis.com/docs/en/dxl/x/xm430-w350/>

Facts are taken from these pages; no text, table and no vendor code is
copied. The CRC is implemented bitwise from its parameters.

## Trademarks

ROBOTIS and Dynamixel are names or trademarks of their respective owner;
they are used here only to say which servos this module works with. The
project is not affiliated with ROBOTIS. No vendor code is included. The full
notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).

## Tests

`test/host/test_dxl_packet.c`, `test_dxl_table.c`, `test_dxl_bus.c` and
`test_dxl_pio.c`, 66 cases, and `dxl_headers.cpp`. The table in `STATUS.md`
has the line coverage per file. The pin driver's C file is not in the host
build.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R dxl
```
