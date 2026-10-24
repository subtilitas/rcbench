# kst

Programming KST and Chaservo servos over the signal wire. The two brands
share one protocol and one register set. The core holds frame coding, reply
decoding, the register model, limits, write planning, the session state
machine and the configuration file. The pin driver puts the wire on one RP2350 pin.

| Part | Files | Depends on |
|---|---|---|
| Core | `kst_wire.c`, `kst_reg.c`, `kst_limits.c`, `kst_plan.c`, `kst_session.c`, `kst_pio.c`, `kst_file.c`, 7 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/kst_line.pio`, `rp2350/kst_line.c`, `rp2350/kst_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

A driver with 3 functions (`kst_driver_t`) connects the core to a pin;
`rp2350/` holds the one for the RP2350. Neither part has been run on
hardware: no frame of this code has been sent to a servo.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`). `test/host/kst_headers.cpp`
  calls each of the 7 parts from C++11 and links against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two
  sessions on two drivers run side by side.
- A session object is 856 bytes with arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33
  -mthumb -Os`) and 872 bytes with gcc on x86-64.
  A plan is 208 bytes, a planner job 176 bytes, a capture 200 bytes, a
  frame 18 bytes.
- The planner does not recurse. Stack in the RP2350 IOmcu image of
  this repository, built with ARM GNU 14.2: `kst_plan_step()` and what it
  calls 208 bytes, `kst_plan_edit_begin()` 312, `kst_session_edit()` 352,
  `kst_session_restore()` 192 and `kst_session_step()` 528. That image
  does not call `kst_plan_edit()`, `kst_plan_restore()` or
  `kst_session_write()`; their stack in it is not measured. Not measured
  with another compiler or option set.
- The planner works in slices. `kst_plan_step(job, budget)` returns
  `KST_PLAN_BUSY` after `budget` to `budget + 9` units of work; a unit is a
  fixed piece of the search, the same on every target. The session takes
  `KST_PLAN_SLICE_UNITS` (100) per `kst_session_step()` call. On a
  Cortex-M33 at 150MHz the costliest slice found over the 71 costliest
  requests found is 816us, and a begin call 64us. The costliest single
  unit found is 1,228 cycles, which puts a slice of 109 units at 892us or
  less. The figures are computed in an emulator from the instructions
  executed, as 1 cycle per instruction, 1 more per load and per store and
  2 per basic block, with ARM GNU 14.2 at -O3. Not measured on hardware.
- A session stops planning after `KST_PLAN_WORK_CAP` (216,000) units, at
  most 2,161 slices, with `KST_PLAN_ERR_TOO_COMPLEX`. The costliest request
  found takes 120,136 units, so the cap is 1.8 times that. A request above
  120,136 units is not ruled out. `kst_plan_edit()`, `kst_plan_restore()`
  and `kst_session_write()` run the planner to its end without a cap:
  397 to 647ms for the costliest request found, by the same estimate.
- `kst_session_write()` checks a plan against the planner's, which it
  builds into the session's own copy of the running plan. A plan handed to
  it must not be that copy or overlap it (`KST_SES_ERR_ARG`).
- Non-blocking: `kst_session_step()` reads the clock, polls the driver and
  returns. It never waits.

## Modules

| Header | Contents |
| --- | --- |
| `kst_wire.h` | the 27-bit read, write and sync frames as half-cell levels; the decoder for the 11-bit reply, 13 result codes |
| `kst_reg.h` | the 32-register image, the field table with access class and unit, raw and displayed values, the servo type, the layout fingerprint with 9 rules |
| `kst_limits.h` | 49 rules with stable numbers: 24 hard (0 to 23), 25 soft (24 to 48) |
| `kst_plan.h` | the order of single-register writes for an edit, a restore and a pairing release; at most 33 writes; in one call or in slices with a work budget |
| `kst_session.h` | programming-mode entry, read of all registers, planning in slices, plan execution with read-back and roll-back, verify |
| `kst_pio.h` | the arithmetic of the pin driver, without hardware access: the counts for a system clock, a frame as the words the PIO (programmable input/output) program reads, the program's stamps as a capture |
| `kst_file.h` | a register image as a text file of 35 lines, its parser with 15 refusal codes, and the check of a file against a servo: type, access classes, limits |

## Wire

Half duplex on the signal wire, idle low, Manchester coded, most significant
bit first. The master sends 27 bits at 25.40us per half-cell. The servo
answers with 11 bits at 25.575us per half-cell. A frame has no parity and
no checksum.

| Quantity | Value |
| --- | --- |
| Reply to a read: accepted first edge | 350us to 600us after the frame's last edge |
| Reply to a write: accepted first edge | 3.3ms to 4.2ms |
| Capture window | 1.5ms after a read, 8ms after a write |
| Accepted mean half-cell of a reply | 24.3us to 26.9us |
| Level shorter than 1.5us | removed as a glitch and counted |
| Line high later than 100us after the nominal end of a reply | reply refused |
| Gap from the end of a window to the next frame | 3ms; 20ms after a window without a valid reply; 3ms inside the entry sequence |
| Entry | line low 100ms, read 0x01, sync burst of 64 pulses, read 0x01 |

Every failed check of the decoder has its own result code (`kst_rx_t`), so a
log tells a silent servo from a late reply, a wrong bit rate, a broken cell
and a lost edge.

The servo's signal pin is a microcontroller GPIO (general-purpose
input/output). The high level of its reply is at most 5V and typically
3.3V. The level is not measured.

| Master | Rating of its pin | On the wire |
| --- | --- | --- |
| RP2350 GPIO0 to 39, IOVDD (input/output supply) at 3.3V | 5.5V | no level shifter, no clamp |
| The same pin, IOVDD at 0V | 3.63V | a servo that drives the wire at 5V exceeds the rating |

What the servo's pin does at idle while the master is unpowered is not
measured. Power the master before the servo's supply, or do not feed the
servo from another supply while the master is off.

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
- `start()` clocks out `frame->n_half` half-cells of 25.40us, each level
  from `kst_frame_level()`, drives low for 50us, switches the pin to input
  and records every level change until `window_ns` after the frame's last
  edge. It returns at once, `false` when it cannot start.
- `poll()` returns `false` while the transaction runs. After the window it
  fills the capture and returns `true` once. The session waits 5ms past
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
while (kst_session_step(&s) == KST_SES_BUSY) { /* call every 1ms */ }

kst_session_read_all(&s);              /* the first image is the backup */
while (kst_session_step(&s) == KST_SES_BUSY) { }

kst_image_t target = s.image;
kst_field_edit(&target, KST_F_DEAD_BAND, 50);

if (kst_session_edit(&s, &target, 0) == KST_SES_BUSY) {
    while (kst_session_step(&s) == KST_SES_BUSY) { /* plans, then writes */ }
}
```

`kst_session_edit()`, `kst_session_restore()` and
`kst_session_release_pairing()` plan inside the session and run the plan:
the planner runs once per operation and the session executes the plan it
built. A request the planner refuses without a search ends the call with
`KST_SES_ERR_NO_PLAN`; `plan_result` holds the planner's code and
`plan_rules` the hard rules a target breaks. A search that finds no order
of writes, or that reaches the cap, ends the operation with the same code
and `plan_result` `KST_PLAN_ERR_NO_PATH` or `KST_PLAN_ERR_TOO_COMPLEX`.
While `planning` is set no frame is on the wire, and `kst_session_abort()`
ends the operation with nothing sent.

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

`kst_session_power_cycled()` says that the servo's supply was off. The
servo on the line may then be another one. The session drops the backup,
the record of its writes (`wrote_regs`) and the lock, after the end of an
operation that runs. An edit, a write and a restore end with
`KST_SES_ERR_NO_BACKUP` until `kst_session_read_all()` takes the servo as it
then is for the new backup. The backup of the servo before the power cycle
is the caller's to keep.

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
have, a system clock that gives no 25.40us half-cell within 0.2%, no free
state machine or program space, and fewer than 2 free DMA channels; a
failed open claims nothing and leaves the pin as it was.

| Quantity | Value at a 150MHz system clock |
| --- | --- |
| Transmit half-cell | 3810 cycles, 25.40us |
| Resolution of an edge stamp | 4 cycles, 26.7ns |
| Frame start after `start()` | up to 31 half-cells, 787us: low half-cells fill the first word |
| Pad after the frame's last edge | driven low for 50us, then an input |
| Pad after a reply | driven low 165us after the reply's last rising edge, which is 60us to 165us after its last level change |
| Pad after a window without an edge | driven low at the end of the window |
| A state machine still running 1ms after the window | restarted with the pad driven low; the capture is marked as overflowed |

While a line is open and no transaction runs, the pad drives low. The pad's
pull-down is on from the open on, and the close leaves the pad an input
with the pull-down on. The internal pull-down is 50 to 80kOhm; a pull-down
on the board is expected.

Limits:

- A level change in the window before the reply starts the 165us early.
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
    while (kst_session_step(&s) == KST_SES_BUSY) { /* call every 1ms */ }
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

## Configuration file

`kst_file.h` turns the 32 registers of a servo into a text file and back.
The module works on buffers the caller holds: no file access, no heap, no
`stdio`. A written file has 35 lines and fewer than 1024 bytes; the one
below has 808. `kst_file_parse()` refuses a text above 3432 bytes
(`KST_FILE_MAX`). It holds one `kst_file_t`, 320 bytes, on its stack; a
`kst_file_load_t` is 72 bytes with arm-none-eabi-gcc 14.2 and 88 bytes with
gcc on x86-64.

```text
KST servo configuration, format 1
Type: V6
Max. Duty = 96 %
Boost = 300.0 us
Stretch = x1
Soft_Start = 1
20k_sel = 1 [not loaded]
spd_sel = 0
Dead Band = 2.0 us
Lock = 1 [not loaded]
Allow Uncont = 0
Pulse Lower = 900.0 us
Pulse Upper = 2100.0 us
Neutral = 1500.2 us
Reversion = 0
Uncontrolled Pos = 1500.0 us
Uncontrolled Time = 2.0 s
Left Angle = 150
Right Angle = 150
prot1_cancle = 0 [not loaded]
prot1en = 0 [not loaded]
prot1_time = 0.1 s [not loaded]
prot1_pwm = 0 % [not loaded]
prot_pot_err = 0 of 12 [not loaded]
pot_sam_times = 0 [not loaded]
prot_pwm_err = 0 of 8 [not loaded]
SPD = 1.0 %
node_addr = 16 [not loaded]
Servo / Gyro Servo = 0 [not loaded]
registers 32
00: 00 F5 F5 14 48 77 04 65
08: 41 A4 0D 80 53 07 14 00
10: 96 96 00 01 01 00 00 00
18: 00 00 00 0A 00 10 00 00
checksum FE83C002
```

| Lines | Content |
| --- | --- |
| 1 | `KST servo configuration, format 1`: the format version |
| 2 | `Type: V6` or `Type: V8`: the servo type of the registers |
| 3 to 29 | one line per named field, in the order of the field table: name, ` = `, the displayed value and its unit. A field no load writes carries ` [not loaded]`. A raw value without a displayed one is written as `raw 6, no displayed value` |
| 30 | `registers 32` |
| 31 to 34 | 8 registers each: the address of the first one, then 2 upper-case hexadecimal digits per register |
| 35 | `checksum` and 8 upper-case hexadecimal digits |

The characters are ASCII (American Standard Code for Information
Interchange) 0x20 to 0x7E. A line has 64 characters or fewer. Every line,
the last one included, ends with LF (line feed, 0x0A). The checksum is
CRC-32 (cyclic redundancy check) as IEEE 802.3 defines it, over every line
before the checksum line, with each line end counted as one LF.

### Servo type

`kst_type()` gives the type of an image: V6 with `20k_sel` (register 0x04
bit 6) set, V8 with it clear. No other register enters. The mapping rests
on the factory defaults the manufacturer states, 20kHz drive for V6.0 and
333Hz drive for V8.0. It is read on 1 servo, whose bit is set; no V8.0
servo has been read. `20k_sel` is a locked field: no file and no edit
changes it.

### The 3 kinds of file

A line whose first word is `checksum` decides how a text is read.

| Kind | Condition | What is taken |
| --- | --- | --- |
| `KST_FILE_VERIFIED` | a checksum line, and its value fits | the 4 register lines. The field lines are not interpreted. The type line has to state the type of the registers |
| `KST_FILE_EDITED_TYPED` | no checksum line; a type line or a register block | the field lines, as input a person made |
| `KST_FILE_EDITED_UNTYPED` | no checksum line, no type line, no register block | the field lines; the file states no servo type |

A file with a checksum line whose value does not fit is refused as damaged
(`KST_FILE_ERR_CHECKSUM`). A verified file has LF on every line or CR LF
(carriage return, 0x0D, then LF) on every line; a transfer that converts
every line end keeps the checksum valid. The checksum line may lack its
line end, and blank lines may follow it. Any other byte behind it refuses
the file (`KST_FILE_ERR_TRAILING`).

Both kinds take the 3 bytes EF BB BF, the byte order mark of UTF-8 (8-bit
Unicode Transformation Format), in front of the header. The mark is not
part of the checksum.

A person changes a file by removing the checksum line, or writes one from
nothing:

```text
KST servo configuration, format 1
# left aileron
Type: V6
Neutral = 1520 us
dead band = 3,0
Reversion = 1
```

- Line 1 is the header, unchanged.
- A field line is `<name> = <value>`; `:` is taken for `=`. The name is
  compared without case and without spaces.
- The value is a number in the field's unit. A unit with tenths takes a
  full stop or a comma and 1 decimal place. The unit may be left out.
  `raw <n>` gives the raw value. A trailing `[...]` is ignored.
- `#` starts a comment that runs to the end of the line. Empty lines are
  skipped. LF and CR LF may both occur.
- A field without a line keeps the servo's value.
- A name the field table does not have, a name given twice and a value the
  field does not encode refuse the file with the line number.
- The type line and the register block may each stay or go. The block
  states the type. A type line that contradicts the block refuses the
  file.
- Where the block shows as the line of an editable field reads, the block
  gives the field's raw value. Several raw values of Max. Duty show as one
  percentage: raw 244 and raw 245 both show 96%, and 96% converts to raw
  245. A file whose line is unchanged loads the raw value it was written
  from. A line that reads otherwise wins over the block. The block gives
  no value for a field without a line.
- The last line may lack its line end when the line shows its own end: a
  unit, a `[...]`, a comment, a remark behind a raw value, a type line, the
  `registers 32` line or a whole register line. A text that ends in the
  digits of a number, in a name or inside the register block is refused as
  truncated (`KST_FILE_ERR_TRUNCATED`).

A written file cut between 2 lines before the register block has no
checksum line and reads as an edited file with fewer field lines.
`kst_file_load()` states the count: `named` is the number of fields the
file gives a value for, `fields` the number of named fields, 27. A
verified file has `named` equal to `fields`.

### Loading

`kst_file_load()` holds a parsed file against the image read from the
connected servo and gives the image to write.

1. Type. A file that states a type loads into a servo of that type and
   into no other (`KST_FILE_LOAD_TYPE_MISMATCH`). A file that states no
   type passes this step.
2. Values. Only editable fields are taken: all of them from a verified
   file's registers, the named ones from an edited file's lines. An edited
   value becomes a raw value through `kst_field_raw_from_display()`, as a
   keypad entry does; a value the servo already displays changes nothing.
   A locked field is never taken from a file. Where the file holds another
   value for one, the field is reported in `skipped` and the load goes on:
   the protection block, `Servo / Gyro Servo`, `Lock` and `node_addr` deny
   no load.
3. Limits. The target passes `kst_limits_edit()`, the check an edit made by
   hand passes. A broken hard rule refuses the whole file
   (`KST_FILE_LOAD_HARD_LIMIT`) and names the field and, for an edited
   file, its line. Soft rules come back in `rules` for the caller to have
   confirmed.

`Max. Duty` is displayed in whole per cent and held in 255 raw steps, so
`Max. Duty = 10 %` converts to raw 25, 1 below hard limit 26, and is
refused; 11% to 98% load.

The target goes to `kst_session_edit()` or `kst_plan_edit()` like an edit
made by hand.

### Refusals

`kst_file_parse()` returns one of these and the line number.

| Code | Name | Condition |
| --- | --- | --- |
| 1 | `KST_FILE_ERR_ARG` | a NULL argument |
| 2 | `KST_FILE_ERR_OVERSIZED` | more than 3432 bytes |
| 3 | `KST_FILE_ERR_NOT_KST` | line 1 is not the header |
| 4 | `KST_FILE_ERR_VERSION` | the version is not a number from 1 up |
| 5 | `KST_FILE_ERR_NEWER` | a version above 1 |
| 6 | `KST_FILE_ERR_TRUNCATED` | the text ends inside the header, inside a number or a name, or inside the register block; a verified file ends inside a line other than a whole checksum line |
| 7 | `KST_FILE_ERR_LINE_END` | checksum line present: LF and CR LF in one file |
| 8 | `KST_FILE_ERR_MALFORMED` | a line is not what the format has at that place |
| 9 | `KST_FILE_ERR_REG_COUNT` | the register count is not 32 |
| 10 | `KST_FILE_ERR_CHECKSUM` | the checksum line's value does not fit |
| 11 | `KST_FILE_ERR_TRAILING` | bytes other than blank lines follow the checksum line |
| 12 | `KST_FILE_ERR_NAME` | edited file: no field has that name |
| 13 | `KST_FILE_ERR_DUPLICATE` | edited file: the field, the type or the register block is given twice |
| 14 | `KST_FILE_ERR_VALUE` | edited file: not a number in the field's unit, a value the field does not encode, or a type other than V6 and V8 |
| 15 | `KST_FILE_ERR_TYPE` | the type line contradicts the registers |

A damaged verified file may be refused with code 8 before its checksum is
compared. A one-byte change never leaves a written file verified. 2
one-byte changes turn it into an edited file with the same values: `#` in
place of the `c` of `checksum`, and `#` in place of the line end before it.

Where the files are kept is the caller's choice. In this repository
`shared/logfile/kst_store.c` names, saves, lists and loads them on the
panel's card, in the folder `BENCH/KST/CONF`. It keeps the base name `KST`,
in any case, free: `KST.TXT` in that folder is the caller's record of a
write under way. That name is
refused for a save and a load, and the file is left out of the list.

## What is measured and what is not

Measured, on 1 Tool Card #2, KST's programming card, and 1 Chaservo servo of
unidentified model:

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
  uncertain by about 1us.
- The encoding of `prot1_time` and `prot1_pwm` on write: there is no
  conversion from a displayed value for them.
- Supply voltage limits during programming.
- The servo type of a V8.0 servo. `kst_type()` takes `20k_sel` for the
  type on the manufacturer's stated defaults; the 1 servo read has the
  bit set.

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

## Trademarks

KST and Chaservo are names or trademarks of their respective owners; they
are used here only to say which servos this module works with. The project
is not affiliated with them. The protocol is implemented from wire captures
of hardware the project owns, the 1 programming card and 1 servo named
above; no vendor code and no vendor documentation is included. The full
notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_kst_wire.c`, `test_kst_reg.c`, `test_kst_plan.c`,
`test_kst_session.c`, `test_kst_pio.c` and `test_kst_file.c`, 268 cases. The table in
`STATUS.md` has the line coverage per file. The pin driver's C file is not
in the host build.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R kst
```
