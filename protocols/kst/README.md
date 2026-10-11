# kst

Programming KST and Chaservo servos over the signal wire. The two brands
share one protocol and one register set. The core holds frame coding, reply
decoding, the register model, limits, write planning and the session state
machine. The pin driver puts the wire on one RP2350 pin.

| Part | Files | Depends on |
|---|---|---|
| Core | `kst_wire.c`, `kst_reg.c`, `kst_limits.c`, `kst_plan.c`, `kst_session.c`, `kst_pio.c`, 6 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/kst_line.pio`, `rp2350/kst_line.c`, `rp2350/kst_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

A driver with 3 functions (`kst_driver_t`) connects the core to a pin;
`rp2350/` holds the one for the RP2350. Neither part has been run on
hardware: no frame of this code has been sent to a servo.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`). `test/host/kst_headers.cpp`
  calls each of the 6 parts from C++11 and links against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two
  sessions on two drivers run side by side.
- A session object is 648 bytes on a 32-bit target and 672 bytes on x86-64.
  A plan is 208 bytes, a capture 200 bytes, a frame 18 bytes.
- Non-blocking: `kst_session_step()` reads the clock, polls the driver and
  returns. It never waits.

## Modules

| Header | Contents |
| --- | --- |
| `kst_wire.h` | the 27-bit read, write and sync frames as half-cell levels; the decoder for the 11-bit reply, 13 result codes |
| `kst_reg.h` | the 32-register image, the field table with access class and unit, raw and displayed values, the layout fingerprint with 9 rules |
| `kst_limits.h` | 49 rules with stable numbers: 24 hard (0 to 23), 25 soft (24 to 48) |
| `kst_plan.h` | the order of single-register writes for an edit, a restore and a pairing release; at most 33 writes |
| `kst_session.h` | programming-mode entry, read of all registers, plan execution with read-back and roll-back, verify |
| `kst_pio.h` | the arithmetic of the pin driver, without hardware access: the counts for a system clock, a frame as the words the PIO (programmable input/output) program reads, the program's stamps as a capture |

## Wire

Half duplex on the signal wire, idle low, Manchester coded, most significant
bit first. The master sends 27 bits at 25.40 us per half-cell. The servo
answers with 11 bits at 25.575 us per half-cell. A frame has no parity and
no checksum.

| Quantity | Value |
| --- | --- |
| Reply to a read: accepted first edge | 350 us to 600 us after the frame's last edge |
| Reply to a write: accepted first edge | 3.3 ms to 4.2 ms |
| Capture window | 1.5 ms after a read, 8 ms after a write |
| Accepted mean half-cell of a reply | 24.3 us to 26.9 us |
| Level shorter than 1.5 us | removed as a glitch and counted |
| Line high later than 100 us after the nominal end of a reply | reply refused |
| Gap from the end of a window to the next frame | 3 ms; 20 ms after a window without a valid reply; 3 ms inside the entry sequence |
| Entry | line low 100 ms, read 0x01, sync burst of 64 pulses, read 0x01 |

Every failed check of the decoder has its own result code (`kst_rx_t`), so a
log tells a silent servo from a late reply, a wrong bit rate, a broken cell
and a lost edge.

## Driver interface

```c
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const kst_frame_t *frame, uint32_t window_ns);
    bool (*poll)(void *ctx, kst_capture_t *out);
    uint32_t (*now_us)(void *ctx);
} kst_driver_t;
```

- Outside a transaction the driver holds the line low.
- `start()` clocks out `frame->n_half` half-cells of 25.40 us, each level
  from `kst_frame_level()`, drives low for 50 us, switches the pin to input
  and records every level change until `window_ns` after the frame's last
  edge. It returns at once, `false` when it cannot start.
- `poll()` returns `false` while the transaction runs. After the window it
  fills the capture and returns `true` once. The session waits 5 ms past
  the end of the window and then ends the operation with
  `KST_SES_ERR_DRIVER`.
- `now_us()` is a free-running microsecond clock. It may wrap.
- A capture is a start level and up to 48 edge timestamps in ns from the
  frame's last edge, unfiltered. The decoder applies the glitch rule.

There is no delay function. The session waits by returning `KST_SES_BUSY`.

## Use

```c
kst_session_t s;
kst_session_init(&s, &driver);

kst_session_enter(&s);                 /* the caller has stopped PWM */
while (kst_session_step(&s) == KST_SES_BUSY) { /* call every 1 ms */ }

kst_session_read_all(&s);              /* the first image is the backup */
while (kst_session_step(&s) == KST_SES_BUSY) { }

kst_image_t target = s.image;
kst_field_edit(&target, KST_F_DEAD_BAND, 50);

kst_plan_t plan;
kst_rules_t violated;
if (kst_plan_edit(&s.image, &target, 0, &plan, &violated) == KST_PLAN_OK) {
    kst_session_write(&s, &plan, false);
    while (kst_session_step(&s) == KST_SES_BUSY) { }
}
```

`kst_limits_edit(&s.backup, &target, unlocked, &refused)` returns the hard
and soft rules a target breaks. A plan whose target breaks a hard rule is
not built. Soft rules are for the caller to show and confirm.

A write plan runs as: read all registers and compare with the plan's start
image; per write the frame and 2 reads of that register; after the last
write a read of all 32 registers against the target. A write that does not
take after 3 attempts is undone back to the last consistent image. The
result codes tell a plan that changed nothing (`KST_SES_ERR_WRITE_FAILED`),
one that was undone (`KST_SES_ERR_ROLLED_BACK`) and one whose undo failed
(`KST_SES_ERR_TORN`). After `KST_SES_ERR_TORN`, `KST_SES_ERR_UNINTENDED` or
`KST_SES_ERR_VERIFY` the session accepts a restore plan and no other.

The caller's part: stop PWM (pulse-width modulation) on the pin before
entry, keep it stopped until the servo's supply was off, check the supply
voltage, store the backup image and a journal of the running plan
(`step_index`), and ask for the confirmations of soft rules and of an
unchecked restore.

## RP2350 pin driver

`rp2350/kst_line.c` implements `kst_driver_t` on one pin. Its header
includes no pico-sdk header and has `extern "C"` guards.

| Resource | Use |
| --- | --- |
| PIO state machines | 1 per open line, in a PIO block that reaches the pin |
| PIO program space | 24 of 32 words; lines in one block share the program |
| DMA (direct memory access) channels | 2 per open line: the words of a frame and the stamps of its reply window |
| Interrupts, PIO interrupt flags | none |
| Memory | 1 `kst_line_t` per line, held by the caller; no static state |

`kst_line_open()` claims the resources and `kst_line_close()` gives all of
them back. An open fails with its own code for a pin the chip does not
have, a system clock that gives no 25.40 us half-cell within 0.2 %, no free
state machine or program space, and fewer than 2 free DMA channels; a
failed open claims nothing and leaves the pin as it was.

| Quantity | Value at a 150 MHz system clock |
| --- | --- |
| Transmit half-cell | 3810 cycles, 25.40 us |
| Resolution of an edge stamp | 4 cycles, 26.7 ns |
| Frame start after `start()` | up to 31 half-cells, 787 us: low half-cells fill the first word |
| Pad after the frame's last edge | driven low for 50 us, then an input |
| Pad after a reply | driven low 165 us after the reply's last rising edge, which is 60 us to 165 us after its last level change |
| Pad after a window without an edge | driven low at the end of the window |
| A state machine still running 1 ms after the window | restarted with the pad driven low; the capture is marked as overflowed |

While a line is open and no transaction runs, the pad drives low. The pad's
pull-down is on from the open on, and the close leaves the pad an input
with the pull-down on. The internal pull-down is 50 to 80 kOhm; a pull-down
on the board is expected.

Limits:

- A level change in the window before the reply starts the 165 us early.
  The pad then drives low into the reply, the capture holds the edges up to
  that point and the decoder refuses it.
- The system clock must not change while a line is open.
- Every function of one line is called from one context.

```c
static kst_line_t line;
kst_session_t s;

kst_line_init(&line);
if (kst_line_open(&line, 14) == KST_LINE_OK) {   /* the pad drives low */
    const kst_driver_t driver = kst_line_driver(&line);

    kst_session_init(&s, &driver);
    kst_session_enter(&s);
    while (kst_session_step(&s) == KST_SES_BUSY) { /* call every 1 ms */ }
}
kst_line_close(&line);                           /* an input, pulled down */
```

The host suite assembles `kst_line.pio` with its own assembler for the
instructions the program uses, runs the words in a model of the state
machine and holds them to `test/host/fixtures/kst_line_words.txt`
(`test_kst_pio`). A firmware build that links the driver can compare the
words `pioasm` produced with that file.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/kst)
target_link_libraries(my_target PRIVATE rcbench_kst rcbench_kst_rp2350)
```

`rcbench_kst` is the core. `rcbench_kst_rp2350` exists when the pico-sdk is
initialised before the `add_subdirectory()`; it is an INTERFACE library, so
`kst_line.c` compiles in the target that links it. A project on another
chip links `rcbench_kst` and supplies its own `kst_driver_t`. Nothing in
the folder names a file outside it.

The caller's part on the pin: stop every other use of the pin before
`kst_line_open()`, and hold it stopped until the servo's supply was off.

## What is measured and what is not

Measured, on 1 programming card and 1 servo of unidentified model:

- The frame layout, the bit timing of both directions and the reply delays.
- The entry sequence.
- The register addresses and bit positions of the fields the vendor tool
  shows, and the displayed value of the raw values that servo and tool
  produced.

Not measured:

- The effect of any register on servo motion. The names, units and limits
  describe what the vendor tool displays and accepts.
- A second servo, a second model and a second card. The accept windows and
  the fingerprint are set around the one servo. A servo that fails the
  fingerprint is read and not written.
- The servo's behaviour on a write to register 0x00, to an address above
  0x1F and to a reserved bit. A plan never contains such a write.
- The servo's state between the 2 writes of a field that lies in 2
  registers. The planner orders the writes so that every intermediate image
  breaks no hard rule that start and target do not break; whether the servo
  acts on an intermediate image is unknown.
- The limits marked `proposed` in `kst_limits.h`: they are choices, not
  readings.
- Gyro Servo mode (register 0x1F bit 0). An image in that mode breaks hard
  rule 21 and no plan toward it is built.
- The Neutral conversion beyond 3 values: its constants are a fit and
  uncertain by about 1 us.
- The encoding of `prot1_time` and `prot1_pwm` on write: there is no
  conversion from a displayed value for them.
- Supply voltage limits during programming.

No frame of this code has been sent to a servo, and the pin driver has not
run on a chip. The tests run against the servo and line model in
`test/host/kst_sim.h`.

Before a write to a servo is relied on, a bench has to measure, on the
port path the servo is connected through:

1. The line's electricals: the levels and edges of a frame and of a reply at
   the connector, the pull-down, and the time the pad and the servo both
   drive.
2. The motor's state in programming mode, and whether a written value acts
   at once or after a power cycle.
3. What the servo does with a PWM (pulse-width modulation) pulse in
   programming mode and with a frame outside it.
4. Persistence: whether a written value survives a power cycle, and whether
   a commit step exists.
5. Write reliability: 1000 writes with read-back, and the count that do not
   take.

## Tests

`test/host/test_kst_wire.c`, `test_kst_reg.c`, `test_kst_plan.c`,
`test_kst_session.c` and `test_kst_pio.c`, 169 cases. The table in
`STATUS.md` has the line coverage per file. The pin driver's C file is not
in the host build.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R kst
```
