# protocols/kst

The protocol core for programming KST and Chaservo servos over the signal
wire: frame coding, reply decoding, the register model, limits, write
planning and the session state machine. It has no hardware access. A driver
with 3 functions connects it to a pin.

Not in this folder: a pin driver, a link page and a user interface. No
firmware image contains this code.

## Constraints

- Plain C11 that also compiles as C++11. Every header has `extern "C"`
  guards. `test/host/kst_headers.cpp` holds this in the host build.
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

No frame of this code has been sent to a servo. The tests run against the
servo and line model in `test/host/kst_sim.h`.

## Tests

`test/host/test_kst_wire.c`, `test_kst_reg.c`, `test_kst_plan.c` and
`test_kst_session.c`, 150 cases. The table in `STATUS.md` has the line
coverage per file.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R kst
```
