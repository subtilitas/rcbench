# The link

<sub>**English** · [Deutsch](Link-de.md)</sub>

The two boards communicate over CAN (Controller Area Network) at 1Mbit/s. Wiring first, then the
protocol reference.

## Wiring

| | |
| --- | --- |
| Bus | classic CAN without FD (flexible data rate), 1Mbit/s, 29-bit identifiers only |
| Panel | TWAI controller on GPIO21 (RX) and GPIO22 (TX) to the board's TJA1051T/3 transceiver, clocked from the 40MHz crystal |
| IOmcu | XL2515 (MCP2515-compatible) on `spi1`: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8; SPI (Serial Peripheral Interface) clock 10MHz |
| Transceiver | SIT65HVD230 (3.3V) on the IOmcu module |
| Termination | 120Ω at both ends |
| Crystal | the XL2515 must have a 16MHz crystal; 8MHz limits the controller to 500kbit/s |

The panel's CAN pins are its own: CAN and the USB (Universal Serial Bus)
console run at the same time. Pin 1 of the panel's CAN header H11 is the
board's 5V rail, so the link cable can power the panel; the panel's
current draw on that path is not measured.

If frames do not cross: [Bringing up the link](Bringup.md).

## Failure behaviour

The IOmcu fills failsafe values after 200ms without a request; the panel
escalates after 1s without an answer. The failsafe latches. Traffic returning
stops the silence counter but does not lift the failsafe; leaving it takes a
write of 0x5AFE to the CLEAR register of the control page.

The IOmcu also holds an arm latch, set at every start, on the edge
into link silence, when the heartbeat stops being trusted and by an ARM it
refuses. While it is set a write of ARM is refused with NOT_ARMED. The same
CLEAR releases it. It is not the failsafe: STATUS does not report it, and the
supply's ON does not read it. [Safety](Safety.md#arm-latch) has the rules.

### Bus off

A CAN transmitter that gets no acknowledgement retransmits on its own and adds
8 to its transmit error counter each attempt. At 1Mbit/s a frame nobody
answers reaches the bus-off threshold of 256 in about 4ms, so any moment with
no second node on the bus — the IOmcu unpowered, reset, or held with
interrupts off long enough — takes the panel's controller off the bus.

The ESP32-P4's TWAI (Two-Wire Automotive Interface) controller does not return
on its own. `can_twai_recover()` runs on the identity poll while the link is
down, once per second: it calls `twai_initiate_recovery()` from the bus-off
state, waits out the 128 bus-free signals the controller counts in the
recovering state, and starts the controller again from the stopped state.
Recovery therefore takes up to three polls, about 3s. Without it the first
quiet moment on the bus is permanent and only a power cycle clears it.

The report printed every 5s while the link is down carries the counters:

```
LINK ...
  bus    tx errors 0 rx errors 0 bus errors 0
```

A transmit error counter climbing towards 256 says nobody is acknowledging. The
line ends in `-- BUS OFF` when the panel has already stopped transmitting. It
reads `the controller is not running` instead of the counters when TWAI never
started, which is a different diagnosis from a bus with no errors.

## Protocol

Pages of up to 32 sixteen-bit registers, read and written in windows. The
IOmcu transmits only in answer to a request. Protocol version 4.28. The
major version is register 0 of page 0. The major moves when a register
changes meaning or a page is renumbered; the minor moves when a page or a
register is added at the end, which an older panel can ignore.

An IOmcu reporting a protocol major other than the panel's is treated as
absent: the link stays down, the splash screen marks the IOmcu step
failed beside the version it reported, the panel logs both majors and raises
the alert `Protocol versions do not match. ARMED is blocked.`, and no write reaches the
IOmcu. Its outputs stay off, and the panel runs as it does with no
IOmcu connected. A panel and an IOmcu on different sides of a
major bump are therefore flashed together.

Registers 0 to 6 of IDENTITY and the two update pages, UPDATE_CTL (0xF0)
and UPDATE_DATA (0xF1), are frozen: they keep their numbers, layout and
meaning under every later major, and an IOmcu of any later major
answers them. Changing one of them is a new page, not a major change. A
panel that meets another major therefore still reads what the IOmcu
runs and offers its own IOmcu image
([Firmware update over the link](#firmware-update-over-the-link)).

### Compatibility

Only the major is compared. The link comes up and the bench switches to ARMED whatever
the two minors are. The panel reads the IOmcu's minor at link-up and
uses nothing that minor does not have: SERVO's frame rate from 4.1, its
sweep from 4.2, SUPPLY from 4.3, the sweep's RESUME from 4.6, SENSE and
SERVO_SENSE from 4.7, TONE from 4.8, SENSE's output encoder from 4.9, BIND_CFG,
BIND_OUT and BIND from 4.10, SERVO_WIN, SENSE's `RESETS` and a signed
`CAP_HOLD_MA` from 4.11, KST from 4.12, PROG, the programming ports of the servo programming page (`KST`, 0x32),
SUPPLY's flag bit 11 and SERVO_SENSE's port bit from 4.13. From 4.14 the programming port
plans a write over several passes and reports results 33 to 35; the panel
writes nothing differently for it. The operation LEAVE and write register 28
of that page are sent from 4.15. From 4.16 the IOmcu refuses a slew
above 3000 and a PWM endpoint longer than its frame, and the panel states a programming port's
range ahead of the write that switches the port's pulse on
([A pulse no longer than its frame](#a-pulse-no-longer-than-its-frame)). From 4.17 the TONE page refuses a busy
pin with BUSY rather than BAD_VALUE, and the panel offers a frame refused
with BUSY again ([A busy pin on the phase tap](#a-busy-pin-on-the-phase-tap)).
From 4.18 the panel's ARM names the roles it switches to ARMED
([An arm for one role](#an-arm-for-one-role)). From
4.19 SERVO's frame rate drives the PWM target alone, and a TARGET change
returns it to 0; the panel then shows the rate as not sent until it writes
it again. From 4.20 ARTWORK serves the board's drawing with a hash of it in
registers 7 and 8, and the panel reads ARTWORK from 4.20 only
([The board drawing](#the-board-drawing)). From 4.21 STATUS counts the
frames the IOmcu refused in registers 8 and 9, and the panel reads
them from 4.21 only ([Refused frames](#refused-frames)). From 4.22
UPDATE_CTL and UPDATE_DATA take a new IOmcu image over the link; an
older IOmcu refuses both with BAD_PAGE, and the panel then says a USB
(Universal Serial Bus) load is needed
([Firmware update over the link](#firmware-update-over-the-link)). From
4.23 HITEC (0x34) runs raw transactions of the Hitec servo wire on a
programming port, and the KST page refuses ENTER and a REBOOT from PWM with
BUSY while HITEC holds the port; an older IOmcu refuses HITEC with
BAD_PAGE. From 4.24 HITEC takes the receive mode of a D series line in
write register 23; the panel writes SETUP's Hitec D receive there, and a
4.23 IOmcu refuses the register with READ_ONLY and receives with the
pull-up. The panel sends no Hitec transaction
([The programming port for Hitec servos](LinkHitec.md)). From 4.25
SENSE_MAP (0x35) sets up to 4 INA3221 with a shunt and a watch target per
channel, and SENSE_WIN_A and SENSE_WIN_B (0x37, 0x38) carry every channel's
last window; an older IOmcu refuses the three with BAD_PAGE. This
build's panel writes SENSE_MAP from SETUP > SENSORS and reads neither
window page ([The INA3221 map and windows](LinkSensors.md)). From 4.26 PROG
has 28 registers: registers 24 to 27 are each port's FUNC, and the build
has 4 ports; the panel reads 24 registers of an older IOmcu and offers its
ports SERVO PWM only ([The programming ports](#the-programming-ports)). From
4.27 SENSE_LIMIT (0x36) holds a current limit per channel and flags each
channel whose window passed it, an alert only; an older IOmcu refuses it
with BAD_PAGE, and this build's panel writes no limit. From
4.28 a port's FUNC also takes DSHOT150, DSHOT300 and DSHOT600, and FLAGS
bits 12 to 15 say a port's DShot holds its pin; an older IOmcu refuses
those values with BAD_VALUE and the panel offers them from 4.28 only. The
IOmcu never reads the panel's minor; a page
an older panel does not know is a page it never writes.

| Panel | IOmcu | Link and switching to ARMED | SENSE (0x2B) and SERVO_SENSE (0x2C) |
| --- | --- | --- | --- |
| 4.7 | 4.7 | up, switches to ARMED | served |
| 4.7 | 4.6 | up, switches to ARMED | the panel sends nothing to either page; the IOmcu answers both with BAD_PAGE. BENCH bits 5 and 6 read 0 |
| 4.6 | 4.7 | up, switches to ARMED | the panel neither reads nor writes either page and ignores BENCH bits 5 and 6. A set-up kept in the IOmcu's flash from a 4.7 panel stays in force and its two pins stay held from the outputs. Once the IOmcu reads an INA228, BENCH carries its voltage and current at the same scales, and the panel counts charge and energy from that current itself |

This build's IOmcu serves both pages, keeps the set-up in flash and
holds its pins. Its core 1 reads the enabled parts on a 1ms tick, and core
0 publishes what core 1 read into both pages; the read-only registers read
0 until core 1 has read under the set-up in force, about 1ms after a
change. With the INA228 answering, BENCH carries its voltage and current
(the last 50ms window's means), their product as power, the run's peaks
from its 500Hz samples, and its charge and energy, with bits 5 and 6. On
the edge into driving it clears bit 6 and the charge and energy registers in
the same pass, rather than at its next 50Hz sample, so no read after the
switch to ARMED carries the last run's totals. A
run that starts with the INA228 answering keeps it as BENCH's source to the
run's end: if it stops answering, those fields go empty rather than back to
the ESC's telemetry. A capture's level before the command is CH1's mean
over the 50ms before the edge; an arm while the INA3221 is not reading CH1
ends at once as lost and counts in CAP_SEQ. This build's panel reads SENSE's registers 0 to 11 at every
link-up, writes the set-up from SETUP INTERFACES one frame at a time where it
differs (`shared/bench/sense_link.c`, `test_sense_link`), and reads the
identity page again after a write is taken. It writes no capture to
SERVO_SENSE. It reads SERVO_WIN (0x31) on a 4.11 IOmcu:
[The window ring](#the-window-ring).

The page refuses SDA and SCL that are not one block's pair whatever ENABLE
holds; an IOmcu of 0.15.0 or older takes them with ENABLE 0 and refuses
the part enabled afterwards. The wire format and the protocol minor are the
same. A panel of 0.15.0 or older, whose two pin settings are independent,
sees the refusal at the edit in place of at the enable: its alert `Sensor pins GP15/GP17 rejected. Check SETUP > INTERFACES.`, once per edit and per
link-up, and the page keeps the pins it had. Its pins at −1 are written as
ENABLE 0 with 0 for the pin and are taken as before. A set-up in the
IOmcu's flash whose pins are not a pair is refused whole at boot, and
the page starts at its defaults with no part enabled; a panel writes what
differs at link-up. This build's panel offers only pairs.

TONE (0x2D) is its own page, and 4.8 moves nothing else:

| Panel | IOmcu | TONE (0x2D) |
| --- | --- | --- |
| 4.8 | 4.8 | served |
| 4.8 | 4.7 | the IOmcu answers every read and write of the page with BAD_PAGE; a panel reads the minor at link-up and sends nothing there |
| 4.7 | 4.8 | the panel neither reads nor writes the page. A tap kept in the IOmcu's flash starts at boot, runs, and is never read; its pin stays held from the outputs |

The page has no capability bit. A panel that reads the minor knows the page
exists; FLAGS says whether the tap runs.

This build's IOmcu serves the page and keeps registers 0 to 6 in flash
(store record version 8, the tap's registers from version 6; a record of
version 3 to 5 reads with the tap off at
its defaults). Core 0 owns the PIO (programmable input/output) state machine
and its DMA (direct memory access) channel, core 1 reads the ring on its 1ms
tick and runs the detector, and core 0 takes the finished beeps and the status
into the page. The page, the ring reader, the service and the PIO program, run
in a cycle-counting model of the state machine, are host-tested
(`test_tone_page`, `test_edge_ring`, `test_tone_svc`, `test_tone_pio`).

The output encoder is part of SENSE, and 4.9 moves nothing else: ENABLE gains
bit 2 and the page grows from 26 registers to 32, 26 to 30 new and 31
reserved.

| Panel | IOmcu | The encoder (SENSE bit 2, registers 26 to 31) |
| --- | --- | --- |
| 4.9 | 4.9 | served |
| 4.9 | 4.8 or 4.7 | the panel does not write ENABLE bit 2 and reads SENSE's registers 12 to 25 only; with the encoder enabled on SETUP it says `IOmcu firmware older than 0.15.0. AS5600 not read.`, at link-up and when the encoder is enabled while the IOmcu answers. A 4.8 IOmcu's page has 26 registers and refuses a read past them |
| 4.9 | 4.6 or older | nothing is sent to SENSE; the same alert |
| 4.8 or 4.7 | 4.9 | the panel never writes bit 2 and never reads registers 26 to 31. An encoder kept in the IOmcu's flash from a 4.9 panel stays enabled and is read; its two pins stay held from the outputs |

The encoder is an ams OSRAM AS5600 on the servo's output shaft, at its fixed
address 0x36 on the bus SENSE already runs, GP16 and GP17 by default. It has
no parameter on the page beyond the enable bit: the pins and the 400kHz are
the bus's. Each register is read in a transaction of its own, since RAW ANGLE
and MAGNITUDE are registers the part treats specially: the address pointer
does not step on past them, and a read that arrives at one by the increment
from STATUS or AGC is not relied on. The IOmcu reads STATUS (1 byte)
and RAW ANGLE (2 bytes) on every second 1ms tick, 500Hz, on the ticks that
read no rotation item: 217.5µs of bus time at 400kHz. Once in 25 of those
slots it reads RAW ANGLE, AGC (1 byte) and MAGNITUDE (2 bytes) instead:
337.5µs. That is 500 angle reads a second at a 2ms interval, 480 STATUS
reads and 20 AGC and MAGNITUDE reads. A transaction of N bytes is 9 × (3 + N)
+ 3 clocks; the controller's own time between transactions is not counted
and not measured. An odd tick is the INA3221's CH1, one other INA3221
channel, the INA228's slot and the encoder: 600µs, 720µs in a slot with
MAGNITUDE. An even tick stays under 690µs. The encoder adds 11.1% to the
bus time of a second: with all three INA3221 channels and the INA228,
43.4% without it and 54.5% with it.

| Register | Name | Reads |
| ---: | --- | --- |
| 26 | `AS5600_FLAGS` | bit 0 answering at 0x36 with a STATUS an AS5600 can give; bit 1 STATUS MD (magnet detected); bit 2 ML (field too weak); bit 3 MH (field too strong); bit 4 something answers at 0x36 with ML and MH both set, which no AS5600 gives, and is not used; bit 5 the angle and bits 1 to 3 hold a reading of this set-up. Bits 1 to 3 read 0 while bit 0 does. `AS5600_ANGLE` is a position only while bits 0, 1 and 5 are all set |
| 27 | `AS5600_ANGLE` | RAW ANGLE, 0 to 4095 for one turn (360 / 4096 = 0.0879 degrees a count), as last read and with no centre applied; 0 until read |
| 28 | `AS5600_MAGNITUDE` | the CORDIC (coordinate rotation digital computer) magnitude, 12 bits, read at 20Hz; 0 until read |
| 29 | `AS5600_SAMPLES` | angle reads, modulo 65536: two reads of the page with the same count are one sample |
| 30 | `AS5600_STILL_MS` | how long the angle has stayed within 12 counts (1.05 degrees) of an anchor, in ms, saturating at 65535; 0 without a sample and while MD is clear |
| 31 | `RESETS` | since 4.11, and not the encoder's: the current monitors found reset, the INA228 in the low byte and the INA3221 in the high byte, each modulo 256, since the set-up in force was taken. [The window ring](#the-window-ring) has the rule. A 4.10 or older IOmcu reads 0 |

The anchor is the angle at the last sample that lay more than 12 counts from
the previous anchor, taken on the circle across the 4095 to 0 wrap. A move
that ends at time t_end, read at time t_read, shows `AS5600_STILL_MS` =
t_read - t_end to within the 2ms sample interval, so the panel times the end
of a move to the IOmcu's resolution whatever its own polling interval.
The panel's own error is the time between the IOmcu's reading and the
reply reaching the panel: not measured. The part going offline, or a new
set-up, clears the anchor and registers 26 to 31.

The magnet bits are STATUS (0x0B) bit 5 MD, bit 4 ML and bit 3 MH. The
datasheet (ams AS5600 DS000365 v1-06) says of MD clear: "If the measured
magnet field strength goes below the minimum specified level (Bz_ERROR), the
output is driven low [...] and the MD bit in the STATUS register is 0", with
Bz_ERROR at 8 mT. The count is then no position: a sample with MD clear
clears the anchor, `AS5600_STILL_MS` reads 0, and the panel hands the angle
to nothing -- not the SERVO screen, not ENC CENTRE, not a run. ML and MH are
"AGC maximum gain overflow, magnet too weak" and "AGC minimum gain overflow,
magnet too strong"; the datasheet states no effect on the angle and
specifies its noise only for 30 to 90 mT. With MD set the count is used
whatever ML and MH say, and the panel reports them. A slot that reads
MAGNITUDE reads no STATUS and goes by the one read 2ms before.

STATUS bits 7, 6, 2, 1 and 0 are blank in the datasheet's register map,
whose note says "Blank fields may contain factory settings". They are masked
off and decide nothing. The part has no identity register: the probe takes
whatever answers at 0x36 as the AS5600, except a STATUS with ML and MH both
set -- the gain at both ends of its range, which a device answering 0xFF
shows and an AS5600 does not. That exception follows from the two bits'
meanings; the datasheet does not state it.

STATUS and RAW ANGLE are one
sample for the failure count: a RAW ANGLE read that fails counts as a failure
even when the STATUS read before it answered, so 3 samples in a row whose RAW
ANGLE read fails take the part offline (3 failed transactions in a row, as for
the INA parts). The panel reads registers 12
to 31 every 40ms while the encoder is enabled on the page. Registers 26 to 31
are read only; a write is refused with READ_ONLY.

### A busy pin on the phase tap

The TONE page refuses a first frame (registers 0 to 3) for one of two
kinds of reason. BAD_VALUE is a frame no other set-up makes good: a value
out of range, a tone range the detector refuses, a GPIO past the bank,
reserved by the board or an ADC pin, or, with ENABLE set, an IOmcu
whose core 1 did not start at boot, which runs no capture before a restart.
BUSY (since 4.17) is a pin that an
output, SENSE, SUPPLY or a programming port holds, or a capture the
IOmcu cannot start for now. The same frame is taken once that pin or
that state machine is free.

The panel (`shared/bench/tone_link.c`) says a refused first frame once on
the band. It offers a frame refused with BUSY again every 5s
(`TONE_LINK_RETRY_MS`), one write each time, and writes one refused with
BAD_VALUE again only after the set-up on SETUP INTERFACES changes or the
link comes up again.

| Panel | IOmcu | A refused first frame |
| --- | --- | --- |
| 4.17 | 4.17 | BUSY: offered again every 5s. BAD_VALUE: written again after the next change of the set-up or link-up |
| 4.17 | 4.16 or older | the IOmcu says BAD_VALUE for both. The panel offers the frame again every 5s when its tone range and gap are ones the page takes (the highest tone above the lowest, the gap at least one period of the lowest tone), and not otherwise. A frame refused for a GPIO past the bank, reserved or an ADC pin then goes out every 5s and is refused each time |
| 4.16 or older | 4.17 | the panel takes BUSY as a refusal and offers the first frame again every 5s, as it does for BAD_VALUE |

`test_tone_page` holds each reason against its code and `test_link_pages`
the code through the dispatcher. `test_tone_link` holds a busy pin taken
once it is free from a 4.17 and a 4.16 page, a 4.16 page's BAD_VALUE
offered 3 times in 15s and no more, and a BAD_VALUE not written again in
15s from a 4.17 page for a board pin and from both pages for tones that
fail.

### A binding taken whole

BIND_CFG (0x2E), BIND_OUT (0x2F) and BIND (0x30) are new in 4.10, and 4.10
moves nothing else. A binding is two pages of 32 registers, CHAN_CFG and
OUTPUTS, 16 frames. A write to either page is in force frame by frame, so a
sequence that stops part way leaves the first entries of one binding and the
rest of another. The three pages make the change one step:

| Page | Registers | Rules |
| --- | --- | --- |
| BIND_CFG | 32, as CHAN_CFG | a CHAN_CFG page that is not in force. A frame is checked by CHAN_CFG's value rules (role 0 or 1, slew 0 to 3000, endpoints 400 to 2500µs) and refused with BAD_VALUE, storing nothing, on a breach. No output changes |
| BIND_OUT | 32, as OUTPUTS | an OUTPUTS page that is not in force. A frame is checked by OUTPUTS' value rules (a driver 0 to 4, a pin number up to 63) and refused with BAD_VALUE on a breach. No output changes |
| BIND | 1: `COMMIT` | a write carries the CRC (cyclic redundancy check) of the 64 prepared registers. Reads the CRC of what is prepared |

The CRC is CRC-16/CCITT-FALSE: polynomial 0x1021, seed 0xFFFF, no
reflection, no final XOR, over the 32 BIND_CFG registers and then the 32
BIND_OUT registers, each as its low byte and then its high byte, 128 bytes.

A write of `COMMIT`:

1. is refused with BAD_VALUE when its value is not the CRC of what the
   IOmcu holds prepared. Nothing changes. A frame that did not arrive,
   or an IOmcu that restarted since the pages were prepared, ends here;
2. judges the prepared CHAN_CFG page by every rule of a CHAN_CFG write and
   puts it in force, or refuses with BAD_VALUE and changes nothing;
3. judges the prepared OUTPUTS page by every rule of an OUTPUTS write —
   ARMED, the supply's, the sensor bus's and the phase tap's pins, the SERVO
   rate, what the silicon binds — and puts it in force, or refuses with
   BAD_VALUE and puts the CHAN_CFG page of before the commit back, and with
   it every channel's role, command and output as they were;
4. is acknowledged, and the binding is saved once.

So the pages in force are both the old binding or both the new one after
every way the sequence can end. A commit whose acknowledgement is lost has
been taken: the host reads the pages to learn it.

What is prepared starts as the pages in force at boot. It is not kept in
flash and not cleared by link silence; the CRC is what ties a commit to the
frames one host sent. CHAN_CFG and OUTPUTS stay writable one entry a frame,
in force as each is acknowledged: the SERVO screen writes a channel's range
that way, and the panel the throttle's endpoints.

| Panel | IOmcu | A binding edited on OUTPUTS or PICK A PIN |
| --- | --- | --- |
| 4.10 | 4.10 | prepared and committed: SERVO `FRAME_HZ` = 0, 8 frames to BIND_CFG, 8 to BIND_OUT, `COMMIT`, then OUTPUTS and CHAN_CFG read back. 20 exchanges |
| 4.10 | 4.9 or older | the panel reads the minor at link-up and sends nothing to the three pages. SERVO `FRAME_HZ` = 0 (4.1 and later), 8 frames to CHAN_CFG, 8 to OUTPUTS, each its own acknowledged exchange and in force as acknowledged, then both pages read back. 19 exchanges. A frame lost part way leaves the entries before it in force; the panel then shows the pages as they read and writes nothing over them but a binding with no pin |
| 4.9 or older | 4.10 | the panel never writes the three pages. It writes CHAN_CFG and OUTPUTS as 8 frames back to back in one exchange each, as between two older builds |
| another host | 4.10 | a `COMMIT` that does not name the CRC of the prepared pages is refused with BAD_VALUE; BIND_CFG and BIND_OUT are refused frame by frame as above |

Every write this panel sends that is wider than one frame goes out one frame
of up to 4 registers per exchange, each acknowledged before the next is sent
(`link_write_acked()` in `shared/link/link_port.c`). The XL2515 holds 2
received frames and the IOmcu reads it from its main loop; with one
request frame on its way at a time, a pass that is late delays a frame and
does not lose one to a full buffer. The same write as 8 frames back to back
loses 6 of them to one 20ms pass (`test_bind_link`). A reply still arrives
as back-to-back frames: the panel's TWAI driver queues 16.

The cost of one edit, and what it is held against:

| | 0.14.0 | This build, 4.10 IOmcu |
| --- | ---: | ---: |
| Exchanges | 5 | 20 |
| Request frames | 19 | 20 |
| Reply frames | 33 | 34 |
| Request frames on the bus at once, at most | 8 | 1 |
| CHAN_CFG and OUTPUTS applied to the outputs | 16 times, once a frame | once each, at the commit |
| Save requests to the flash store | 16 | 1 |

54 frames at about 130µs are 7ms of bus time in both. An exchange adds the
IOmcu's loop pass and the panel task's wake: [Bringing up the
link](Bringup.md) prints 334 to 1400µs for the round trip of one, which
puts the 20 between 7 and 28ms; the sequence itself is not timed on
hardware. The panel's control task runs every 5ms and sends the sequence
from one pass, so that pass is 2 to 6 periods long; the safety loop — the
heartbeat's 20ms edges, STOP — runs inside it, between two frames of a
page once 5ms have passed since it last ran, and in every 5ms an answer is
waited for. The
50ms poll with its ARM and THROTTLE write is late by the same time; an edit
is refused while the bench is ARMED, so a bench that is ARMED never waits on it. The
IOmcu's 200ms silence limit counts from the last request it heard,
and every exchange of the sequence is one: the sequence cannot starve it
however long it takes as a whole, and a single frame the IOmcu does
not take for 200ms latches `FAULT 01` as any request does. A frame that is
lost on the wire ends the sequence after the 1000ms of its own exchange,
with the link taken down and the binding in force unchanged.

`shared/outputs/out_stage.c` is the IOmcu's half and
`shared/outputs/bind_link.c` the panel's, under `test_bind_link`: the
sequence through a model of the 2-frame buffer with a 20ms deaf window
opened at every 125µs, a frame lost at each of the 18 positions, the
timeouts across the 2^32ms wrap.

### The window ring

SERVO_WIN (0x31) is new in 4.11. 4.11 also gives SENSE's register 31 a
meaning, `RESETS`, and makes SERVO_SENSE's `CAP_HOLD_MA` a signed register.
No page and no register moves.

SERVO_SENSE shows the last 50ms window of each channel and no other. A host
whose reads lie more than 50ms apart misses a window there. SERVO_WIN holds
the last 4 complete windows of INA3221 CH1, the servo under test, 200ms. A
host that takes each window number once misses none while two of its reads
lie at most 200ms apart. A read ends nothing, so a reply lost on the link
loses nothing. The page is read only: every write is refused with READ_ONLY.

| Register | Name | Reads |
| ---: | --- | --- |
| 0 | `WINDOW` | the number of the newest complete window, modulo 65536: the number SERVO_SENSE's register 12 shows |
| 1 | `FLAGS` | bit 0 a window has closed under the set-up in force, so `WINDOW` is a number; bit 7 SERVO_SENSE's register 13 bit 7, a sample of the capture's move read an end of the range |
| 2 | `CAP_STATE` | SERVO_SENSE's register 18 as it reads at this moment |
| 3 | `CAP_SEQ` | SERVO_SENSE's register 19 |
| 4 + 6 k | `MEAN_MA` | entry k, 0 to 3, is window number `WINDOW` - k modulo 65536: the mean current in mA, signed |
| 5 + 6 k | `MAX_MA` | the highest current sample in mA, signed |
| 6 + 6 k | `MIN_MA` | the lowest current sample in mA, signed |
| 7 + 6 k | `MEAN_MV` | the mean bus voltage in mV, at the load side of the shunt |
| 8 + 6 k | `MIN_MV` | the lowest bus voltage sample in mV |
| 9 + 6 k | `E_FLAGS` | bit 15 a window closed with this number; bit 8 it holds current samples; bit 9 it holds voltage samples; bit 10 a current sample read the top of the range, 163.8mV across the shunt; bit 11 one read the bottom; bits 0 to 7 the number of current samples that read an end, held at 255 |

The page is 28 registers. Registers 0 to 15 are the header and the two
newest windows: one request and four data frames, as a BENCH read. Currents
are rounded to the mA and held to -32767 to 32767, voltages to 0 to 65535mV.
The IOmcu reads CH1's current every 1ms and its bus voltage every
24ms: a window holds up to 50 current samples and 2 or 3 voltage samples.

An entry with bit 15 clear reads 0 in all six registers. Its number never
closed: the IOmcu's 1ms tick ran late by 50ms or more and no sample
fell in that window, or the number lies before the first window of the
set-up. An entry with bit 15 set and bits 8 and 9 clear closed with no
sample: the INA3221 was not online.

A clipped current sample counts in an entry as the reading it is, the end of
the range: 4095 steps of 40µV across the shunt at the top and 4096 at the
bottom, 1.638A and -1.6384A on the 0.1Ω shunt. It enters the mean, the
highest and the lowest at that value, and bits 0 to 7 count it. The current
was at least that; by how much more is not known. SERVO_SENSE's registers 0
to 11 leave a clipped sample out of their figures, so the two pages differ
for a window with a clipped sample, and only for such a window. The
IOmcu applies no rule to the count.

`CAP_STATE`, `CAP_SEQ` and bit 7 of `FLAGS` are SERVO_SENSE's as that page
reads in the same pass: an arm reads armed (1) here from the write's
acknowledgement on, and a capture that has ended shows in the read that
fetches the windows.

Nothing is kept: an IOmcu restart reads 0 throughout, and so does a
SENSE set-up taken, until the first window closes under it.

**A part that resets itself.** Both current monitors answer every read after
a reset of their own, on their power-on set-up. The INA228 then computes its
current at ADCRANGE 0: a set-up at ADCRANGE 1, any whose shunt voltage at
the maximum current is at most 40.96mV, the default of the MATEK
I2C-INA-BM board among them, reads a quarter of the current. The INA3221 converts all three channels at 1.1ms,
one CH1 result every 6.6ms where the bench set-up gives one every 280µs
(CH1 alone) to 840µs (three channels). The INA228 resets when its supply
falls under about 1.26V (TI SLYS021A §7.4.2).

The IOmcu reads the INA228's ADC_CONFIG and the INA3221's
Configuration back, each once in 40ms. A value other than the one written
is read a second time in the same tick, so one corrupted read is no reset.
A read-back of FFFFh is not read again: a bus that no part drives reads all
ones, and the part is taken as lost on that one read. When both reads
differ, or the one read is FFFFh:

- the part loses ONLINE in SENSE's `FLAGS` and is probed and set up again
  1000ms later;
- its byte of `RESETS` (SENSE register 31) counts one: the INA228 in the low
  byte, the INA3221 in the high byte, each modulo 256, since the set-up in
  force was taken. The count shows a reset found and repaired between two
  reads of `FLAGS`;
- the windows being filled from the part are emptied. For the INA3221 the
  SERVO_WIN entry of that window reads closed with no sample, and so do the
  entries of the next 1000ms;
- for the INA228, charge and energy read 0 and BENCH bit 6 stays clear until
  the next switch to ARMED, and BENCH's voltage, current and power read empty while the
  part is offline;
- a capture under way ends lost (state 8) and counts in `CAP_SEQ`.

The readings themselves are not checked for all ones. An all-ones current or
voltage register reads as code -1: -390µA on a 200 µΩ INA228 shunt,
-400µA on a 0.1Ω INA3221 shunt, and -8mV of INA3221 bus voltage. Such
readings stand in the windows until the next read-back takes the part
offline, at most 40ms later.

A reset is found within 40ms. The samples read between the reset and its
detection lie in the window before the emptied one when a window boundary
falls between them: the INA228's at a quarter for a set-up at ADCRANGE 1,
the INA3221's right in value and each up to 6.6ms old.

The read-backs have slots of their own in the schedule's rotation of 20
slots, one every second 1ms tick, 40ms: the INA228's ADC_CONFIG and the
INA3221's Configuration at 25Hz each, as the INA228's die temperature and
the INA3221's Mask/Enable. A read-back is one 2-byte read, 120µs. An even
tick stays at 690µs of bus time; the tick in which a set-up register is
read a second time is 742.5µs.

| Panel | IOmcu | SERVO_WIN (0x31), `RESETS` and `CAP_HOLD_MA` |
| --- | --- | --- |
| 4.11 | 4.11 | served. While SENSE enables the INA3221 with CH1, this build's panel reads SERVO_WIN every poll and SENSE's registers 12 to 31, `RESETS` among them. It arms no capture |
| 4.11 | 4.10 or older | the IOmcu answers a request to 0x31 with BAD_PAGE; the panel reads the minor at link-up and sends nothing there below 11. It takes the INA3221's windows from SERVO_SENSE, the last one per read, reads the PD mini as the servo rail's meter and says once `IOmcu firmware older than 0.16.0. Servo current is read from the PD mini.`. Register 31 reads 0, and a part that reset itself stays ONLINE on its power-on set-up. A `CAP_HOLD_MA` of 32768 to 65535 is refused with BAD_VALUE |
| 4.10 or older | 4.11 | the panel never reads 0x31 and reads register 31 as a reserved register it ignores. A holding level it writes, 0 to 32767mA, means the same. A part found reset reads offline for 1000ms and then online again |

**The panel's reads.** `shared/bench/sense_link.c` takes each CH1 window
number once and hands the windows over oldest first. It reads SERVO_WIN
while the IOmcu's minor is 11 or more and SENSE enables the INA3221
with CH1; with CH1 off, or on an older IOmcu, the windows are
SERVO_SENSE's last one per read (`shared/bench/sense_link_view.c`).

- Every poll it reads registers 0 to 15, the header and the two newest
  windows.
- When 100ms or more have passed since a read last left no window owed, a
  third window can have closed, and the read is the whole page, registers 0
  to 27, in place of registers 0 to 15. Every window then comes from one
  reply. Entries of two replies are not joined: a window that closes
  between them shifts every entry by one number.
- A read of registers 0 to 15 that owes more than two windows hands nothing
  over and is followed by a read of the whole page in the same poll.
- A window number more than 4 behind the newest has left the ring. It is
  counted as lost and not handed over. With reads at most 200ms apart none
  is lost; with reads 250ms apart one in five is.
- An entry with bit 15 clear is handed over as no window and is not
  counted. A skipped number that has left the ring before a read counts as
  a lost one: the page no longer says which it was.
- A reply lost on the link changes nothing: the next read owes the same
  windows and the ones closed since.
- The first read after a link-up hands nothing over. The hand-over starts
  with the window after the newest that read shows, so a window taken
  before the link went is not taken again.
- After a set-up the panel wrote, after a read with `FLAGS` bit 0 clear,
  and when the newest number lies behind the last one taken, the ring has
  started again: every closed entry of the next read is handed over.
- At most 4 windows wait for the control task. It takes them in every pass,
  so none waits longer than a pass.

CH2 and CH3 have no ring. Their figures are SERVO_SENSE's last window, and
the bench log writes them on the row whose window number that read showed.
When a window closes between the SERVO_SENSE read and the SERVO_WIN read of
one poll, the row of the window that closed in between has no CH2 and CH3
figures.

**The servo rail's meter.** `shared/bench/servo_source.c` decides once per
poll which meter measures the servo rail. The INA3221 channel that watches
the servo tester's target is the meter while all of these hold:

| # | Condition |
| ---: | --- |
| 1 | SETUP SENSORS has a fitted part with a channel that watches the servo tester's target; that channel is the part's CH1 |
| 2 | the IOmcu's protocol minor is 11 or more |
| 3 | SENSE's registers 0 to 11 are the set-up asked: no write owed, no frame refused; with SENSE_MAP its INA3221 enable bit and registers 8 to 11 are the map's, and SENSE_MAP's registers 0 to 25 are the map asked: read since the link came up, not refused, no write owed |
| 4 | the last SENSE read is less than 200ms old, and its `FLAGS` have the INA3221's bits 4 and 5 set and bit 9 clear |
| 5 | SERVO_WIN's newest window holds current samples and its number moved less than 200ms ago |
| 6 | the INA3221's byte of `RESETS` has not moved since the poll before |
| 7 | conditions 1 to 6 have held for 1000ms without a break |

The meter is the PD mini from the first poll in which one of 1 to 6 fails,
and the INA3221 again 1000ms after they all hold. With no IOmcu
answering it is the panel's model. A clipped window and a negative current
are readings and no condition. The module counts the changes of the meter,
modulo 2^32. The answer and that count are in the control task's snapshot
with the count of windows lost, and the windows are on a queue to the
render task, 8 deep, the oldest dropped when it is full, each with the
answer and the count of the poll that took it; the supply's samples carry
them on their queue as well. The SERVO screen takes the snapshot's answer,
every window and every supply sample once per frame and shows the meter's
reading in its CURRENT row, line and plot ([Screens](Screens.md#servo)). Of
two answers it keeps the one with the later count. A window whose count is
behind the meter in force is dropped, and a supply sample whose count is
behind is no point of the plot: the snapshot and the queues are three
paths, and each can be the oldest. A count more than one ahead of the one
in force means changes the screen did not see, and it empties the plot
whatever the meter is. An automatic servo test reads the meter in force
when it starts, to its end: a run on the INA3221 ends when a window, a
supply sample or the snapshot brings another meter or another change count
([Servo](Servo.md#the-meter)). The panel keeps why the INA3221 last stopped
being the meter with the count of that poll (`servo_source_dropped()`) and
hands it over with each of the three, so the run's report names the
condition: a reset count that moved in that poll is the reason kept, since
the IOmcu takes a part it found reset offline in the same read. Each
window is stamped with the latest it can have closed: the panel's tick at
the read, less 50ms for each window after it in that read
(`sense_link_win_t.taken_ms`).

`test_sense_windows` runs the modelled INA3221, the IOmcu's schedule
and pages and `sense_link` on one clock: polls 50, 53, 55, 100, 150, 199 and
200ms apart with no window lost, 201 and 250ms apart with the lost count
equal to the numbers missing, a reply lost in one, two and three polls in a
row, the window number across 65535, the reads across the 2^32ms tick wrap,
a set-up written, an IOmcu restart, a late IOmcu tick, a link
lost and back, a part that resets itself, and a 4.10 IOmcu.
`test_servo_source` holds each condition failing alone, a read fresh at
199ms and not at 200 and 201ms, the INA3221 back at 1000ms and not at
999ms, a reset repaired between two reads, and each timer across the tick
wrap.

`shared/sense/sense_sched.c` keeps the ring and runs the read-backs, under
`test_sense_sched`: the ring after 1, 4, 5 and 6 windows, across window
65535, with a tick late by 49, 50 and 51ms and by 5 windows, with 0, 1, 44,
45 and 50 clipped samples; a reset found at each of the 40 ticks of a
rotation, the part back at 1000ms and not at 999ms, across the 2^32ms
wrap; the bus time of every tick. `shared/outputs/sense_page.c` holds the
page, under `test_sense_page` and `test_link_pages`.

### The programming ports

PROG (0x33) is new in 4.13. No page and no register moves. A programming
port is an output of its own kind: it has its own pin, is no slot of OUTPUTS
and holds no channel of CHANNELS. Its pin carries one of these, never two:

- SERVO PWM (pulse-width modulation): a servo pulse from the PWM generator
  that drives the standard servo ports. The port's function `FUNC` 0.
- NONE (since 4.26): no driver. The pin is held for the port as an input
  with its pull-down and renders nothing. `FUNC` 1.
- DSHOT150, DSHOT300, DSHOT600 (since 4.28): plain DShot at 150, 300 or
  600kbit/s from a PIO (programmable I/O) state machine of its own, a
  frame every 1ms. The port is a throttle: MOTOR & ESC drives it. `FUNC`
  2, 3 and 4. No telemetry wire: bidirectional DShot is not offered on a
  port. A KST or HITEC session refuses a DShot port with NO_PIN: an ESC is
  on that lead.
- PROGRAMMING: the wire protocol for KST and Chaservo servos, which the
  servo programming page (`KST`, 0x32) runs, see
  [The programming port for KST and Chaservo servos](LinkKST.md); or, since
  4.23, the Hitec wire, which the Hitec page (`HITEC`, 0x34) runs, see
  [The programming port for Hitec servos](LinkHitec.md). One page holds a
  port at a time.

The page holds 4 ports. Register 0 says how many the build has: 4 from
4.26, 1 before. `shared/outputs/prog_port.c` holds the rules.

| Register | Name | Access | Holds |
| ---: | --- | --- | --- |
| 0 | `COUNT` | read | the ports of this build, 0 to 4 |
| 1 | `FLAGS` | read | bit n: the PWM generator has port n's pin (SERVO PWM). Bit 4 + n (since 4.26): port n's pin is parked (NONE). Bit 8 + n: port n names a pin it does not hold: the stored pin was taken at start, the silicon refused it, or no PIO state machine was free for its DShot. Bit 12 + n (since 4.28): port n's DShot holds its pin |
| 2 | `PWM` | read, write | bit n: port n renders its pulse while the bank is ARMED for the surfaces. Not kept |
| 3 | `TARGET` | read, write | what SERVO's frame rate, sweep and hold drive: 0 the standard servo ports, n + 1 port n. A change sets SERVO's frame rate to 0 (since 4.19). Not kept |
| 4 + 5 n | `PIN` | read, write | port n's IOmcu GPIO (general-purpose input/output), or 0xFFFF for none. Kept |
| 5 + 5 n | `CMD` | read, write | the command, 0 to 1000; a higher value is taken as 1000. Refused with BAD_VALUE on a DShot port, whose command is the bench throttle (since 4.28). Not kept |
| 6 + 5 n | `SLEW` | read, write | as CHAN_CFG's slew: 0 to 3000, a higher value refused with BAD_VALUE, nothing stored (since 4.16). Not applied on a DShot port. Kept; a kept slew above 3000 starts as 3000 |
| 7 + 5 n | `MIN_US` | read, write | the pulse at command 0, 400 to 2500µs, default 1000; while the port's `PWM` bit is set, at most the frame of the port's rate minus 1µs (since 4.16). Kept |
| 8 + 5 n | `MAX_US` | read, write | the pulse at command 1000, 400 to 2500µs, default 2000; while the port's `PWM` bit is set, at most the frame of the port's rate minus 1µs (since 4.16). Kept |
| 24 + n | `FUNC` | read, write | since 4.26: port n's function, 0 SERVO PWM, 1 NONE; since 4.28 also 2 DSHOT150, 3 DSHOT300, 4 DSHOT600. Any other value is refused with BAD_VALUE, and so are 2 to 4 by a 4.26 or 4.27 IOmcu. Default 0 for port 0, 1 for the others. Kept |

A port's pulse is a channel of the output bank: port n is bank channel
9 + n, above the 8 channels of CHANNELS and the bench throttle's channel 8.
The switch to ARMED, the clamp to 0 to 1000, the slew, the endpoints and the 500ms
command timeout are the bank's and act on a port as on a standard servo
port. A SERVO PWM port is a surface: it renders a pulse while the bank is ARMED for
the surfaces (ARM 1, 0x0004 or 0x0006), its `PWM` bit is set,
the generator has its pin and the servo programming page holds the port in
PWM.

A DShot port is a throttle (since 4.28): its bank channel has the
throttle's role and rest, 0. The bench throttle on CONTROL reaches it as it
reaches every slot bound as a motor, and the port sends a frame every 1ms
while the bank is ARMED for the throttle (ARM 1, 0x0002 or 0x0006): the
throttle as DShot 48 to 2047, or 0 at rest. While the bank is not ARMED
for the throttle -- SAFE, ARMED for the surfaces alone, STOP, heartbeat
loss, link silence -- the port sends no frame and the ESC times out, as on
a motor slot. `PWM`, `TARGET` and `CMD` never name a DShot port; its
`SLEW`, `MIN_US` and `MAX_US` stay on the page for SERVO PWM and are not
applied. A change of `FUNC` from or to DShot puts the channel at the new
role's rest. The port that `TARGET` names runs at SERVO's frame rate (register 0 of
SERVO), or at 50Hz while that reads 0. Every other port runs at 50Hz, and
the PWM slots run at their own OUTPUTS rates while a port is the target
(since 4.19).

The servo tester reaches a port three ways:

- `CMD` is the manual command. It is written at least every 500ms, as a
  channel of CHANNELS is.
- `TARGET` points SERVO's frame rate, sweep, hold and RESUME at one port. A
  change of `TARGET` ends a running or held sweep as a SWEEP of 0 does and
  sets SERVO's frame rate to 0 (since 4.19), so the new target starts at
  50Hz or, for the standard servo ports, at each slot's own rate. A change
  away from the standard servo ports while SERVO reads a rate other than 0
  is refused with BAD_VALUE when a PWM slot back at its own rate would carry
  an endpoint longer than that frame.
- A capture of SERVO_SENSE names a port with bit 11 of its arm register
  (since 4.13): bits 8 to 10 are then the port, not an output channel.
  Refused with BAD_VALUE for a port that renders no pulse. A port whose
  pulse stops ends a capture that has not finished.

Pins. A port's pin is never a slot's pin. A write of `PIN` is refused with
BAD_VALUE for a pin that is reserved, past GP63, a slot's, held by SUPPLY,
SENSE or TONE or another port's. For a SERVO PWM port it is also refused
for a pin past GP47 (no PWM slice), one that shares a PWM slice or a
compare register with a PWM slot's pin, and one on the compare register of
another SERVO PWM port; a NONE or DShot port's pin is under none of the PWM
rules. A DShot port needs a PIO block that reaches its pin and a free state
machine in it; without one the port holds no pin and bit 8 + n of `FLAGS`
is set. The output slots come first: an OUTPUTS or BIND write that binds a
DShot or PPM slot anew lets every DShot port go, binds the slots, and binds
the ports again on the state machines left. The IOmcu's DShot driver holds
12 pins, the 8 slots and the 4 ports. A `FUNC` or `PIN` write that binds a
DShot port is refused with BAD_VALUE, and changes nothing, while the
supply's PD mini UART waits for its save and the port would leave it no
PIO room, as a slot or TONE write is.
A pin the chip does not have is refused when the IOmcu cannot park it, and
the write is undone; a write of OUTPUTS or BIND that
puts a slot on a port's pin or beside it in the silicon is refused the same
way, and so is a SUPPLY, SENSE or TONE write that takes it. Channels. A
slot's channel range never reaches the ports' bank channels 9 to 12: a write
of OUTPUTS or BIND with such a slot is refused with BAD_VALUE, and a stored
slot with one starts unbound. Two ports may
share a slice. The RP2350 runs both channels of one PWM slice from one
divider and one wrap register, so both pins run at one rate. A port whose
pin shares its slice with another held port's pin takes a SERVO rate of 0
or 50Hz only, any other is refused with BAD_VALUE; and while the `TARGET`
port runs at a rate other than 50Hz, a `PIN` write that moves another port
onto its slice is refused with BAD_VALUE (both since 4.19). Neither `PIN`
nor `FUNC` changes while the bank is ARMED, while the port's `PWM` bit is
set, while it is the `TARGET` (before or after the frame) or while a wire
page (KST, HITEC) holds a port; a `PIN` is judged under the `FUNC` the frame
leaves. `PWM` and `TARGET` name only SERVO PWM ports whose pin the
generator has, and no port whose `PIN` or `FUNC` the same frame changes. A write that reaches a register of a port the
build does not have is refused with BAD_VALUE, a write of register 0 or 1
with READ_ONLY. A refused write changes nothing, SERVO's frame rate and sweep
included.

An IOmcu that has stored no port has port 0 on GP13, pad 17 of the
bring-up module, and the other ports without a pin. GP13's slice partner
GP12 is reserved and the pin with its compare register, GP29, is not on the
header. GP13 is therefore no slot's pin until port 0's `PIN` is written to
another pin or to 0xFFFF. A stored pin that a slot, SUPPLY, SENSE or TONE
holds at start stays theirs: the port holds no pin and bit 8 + n of `FLAGS`
is set until another `PIN` is written.

The functions are kept in one word of the store record, the word before
the ports' registers that was spare until 4.26 (`prog_func`): bits 4n to
4n + 3 hold 1 + port n's `FUNC`, 0 none stored. A record without one reads
as port 0 SERVO PWM and the others NONE. The word has no version of its
own: version 7 and version 8 records carry it at the same place.

A DShot function is stored as 3 to 5.

| Image the board goes down to | What it does with the word | Back on 4.26 or later |
| --- | --- | --- |
| 4.26 or 4.27 | reads a DShot port's 3 to 5 as no value it takes: the port starts at its default function and keeps its pin, unless that default is SERVO PWM and the pin is no PWM pin | the port is at the function the older image wrote back, the default |
| 4.25 | reads it as spare and writes it back as 0 | the ports keep their pins and are at the default functions; a kept pin a SERVO PWM port cannot hold (past GP47) is dropped |
| older than 4.25 | reads no version 8 record and starts with an empty store | every port is at its default pin and function |

The panel writes `PIN` and `FUNC` only when the operator asks, on OUTPUTS'
PROG PORT entry or in PICK A PIN ([Screens](Screens.md#the-prog-port-entry)),
never from a stored setting: the ports are the board's, as the outputs'
binding is. An ask waits for a bench at rest. A port that is the
`TARGET` is let go first (its `PWM` bit off, `TARGET` 0), and the target
the screen wants is written back after the page is read again.
| Panel | IOmcu | PROG (0x33) |
| --- | --- | --- |
| 4.13 | 4.13 | served |
| 4.13 | 4.12 or older | the IOmcu answers a request to 0x33 with BAD_PAGE; the panel reads the minor at link-up and sends nothing there below 13 |
| 4.12 or older | 4.13 | the panel never reads or writes 0x33: no port renders a pulse, and port 0 holds GP13 |
| 4.26 | 4.13 to 4.25 | the panel reads 24 registers and takes every port as SERVO PWM; it asks no `FUNC` and says `IOMCU OLDER THAN 0.22.0: SERVO PWM ONLY` |
| 4.13 to 4.25 | 4.26 | the panel reads 24 registers and never writes `FUNC`; a port it sets as the target must be SERVO PWM, or the IOmcu refuses the `TARGET` |
| 4.28 | 4.26 or 4.27 | the panel offers SERVO PWM and NONE only and asks no DShot `FUNC` |
| 4.26 or 4.27 | 4.28 | the panel shows a DShot port as NONE and never sets it as the target; MOTOR & ESC's throttle reaches it all the same |

`test_prog_port_fn` runs the functions: the five values taken and 5, 15,
32768 and 65535 refused, a NONE or DShot port's pin under the pin rules and
none of the PWM rules, a DShot port's channel a throttle at rest with no
slew, its `CMD` refused, `PWM` and `TARGET` refused on it, FLAGS bit
12 + n, the kept nibbles 3 to 5, a function change under every lock, SERVO PWM refused on a
pin the generator cannot take, the parked flag, the kept word and a record
an older image wrote back. `test_prog_link_ask` runs the panel's asks.
`test_prog_port` runs the page: every pin refusal in both directions, the
endpoints at 399, 400, 2500 and 2501µs, the slew at 3000 and 3001 and a kept
slew of 3001 and 65535, a pin move under a bank in ARMED, a
set `PWM` bit and a `TARGET`, 0 and 5 ports asked of a 4-port model, a
stored pin taken at start, the SERVO rate reaching the target port alone, a
target change judged at 50Hz, a pin refused onto the slice of a target at
its own rate, and a target change that would leave a slot with a pulse
longer than its frame. `test_outputs_hw_ports` runs a DShot port: bound at
150, 300 and 600kbit/s, moved by a rate or function change, no state
machine left, a DShot slot bound anew taking the port's state machine,
a frame every 1ms only while ARMED for the throttle, the
throttle at 1000 as 2047, none on SAFE, ARM SURFACE and STOP, and the
period across the microsecond clock's wrap. No pulse or frame of a port
has been measured on hardware.

PIO budget. The RP2350 has 3 PIO blocks of 4 state machines and 32
instructions. The pico-sdk loads one copy of a program for each pin. By
the generated program lengths, a plain DShot pin takes 1 state machine
and 4 instructions, a bidirectional one 2 and 16, a Hitec line 1 and 29
and its precharge sampler 1 more and up to 12. Two bidirectional slots,
two DShot ports and a Hitec line with precharge take 8 of 12 state
machines and 81 of 96 instructions; the arithmetic is not run on
hardware. A block reaches GP0 to GP31 or GP16 to GP47, so a pin above
GP31 can be refused earlier.

### The programming port for KST and Chaservo servos

The servo programming page (`KST`, 0x32) has [its own page](LinkKST.md): the
supply, leaving without a supply cycle, the registers, and what STOP, a switch
to SAFE, heartbeat loss and link loss do to a session.

### The programming port for Hitec servos

The Hitec page (`HITEC`, 0x34) has [its own page](LinkHitec.md): the way in
and out, the transactions, POWER, what a stop does to a port, and the
registers.

### The INA3221 map and windows

SENSE_MAP (0x35), SENSE_LIMIT (0x36), SENSE_WIN_A (0x37) and SENSE_WIN_B
(0x38) have [their own page](LinkSensors.md): up to 4 INA3221 and their 12
channels, the FAST channel that SERVO_WIN and a SERVO_SENSE capture follow,
how SENSE mirrors a map in force, the current limits, every channel's
window, and the flash record over two pages.

### Identifier

A 29-bit extended identifier carries the whole address, so a read is a frame
with no payload.

| Bits | Field | Width | Values |
| --- | --- | ---: | --- |
| 28..26 | priority | 3 | 0 control (CONTROL, LIMITS, FAILSAFE and SUPPLY pages, their acknowledgements included), 1 normal (SENSE, SERVO_SENSE and TONE among them), 2 bulk (reserved); lower wins arbitration |
| 25..22 | op | 4 | 1 READ, 2 WRITE, 3 DATA, 4 ACK (acknowledge), 5 NACK (negative acknowledge) |
| 21..14 | page | 8 | page map below |
| 13..6 | offset | 8 | first register in this frame |
| 5..0 | count | 6 | registers in this frame, 0..32 |

Priority is derived from the page: the control, limits and failsafe pages and
their acknowledgements are class 0; everything else is class 1.

### Frames

A frame carries up to four registers (8 bytes, little-endian). Each frame
carries its own offset and count, so a reply wider than four registers is
several independent frames in any order, and a dropped frame costs one register
range. The host poller tracks the window it asked for and completes when every
register has arrived; the transport does no reassembly. There is no CRC (cyclic
redundancy check) in the payload; CAN's 15-bit CRC, acknowledge slot and
retransmission apply.

A NACK carries its reason in register 0:

| Value | Reason |
| ---: | --- |
| 1 | BAD_PAGE |
| 2 | BAD_RANGE: offset + count past the end of the page |
| 3 | READ_ONLY |
| 4 | BAD_VALUE |
| 5 | NOT_ARMED |
| 6 | BUSY: the value is one the page takes, and something else holds what it names for now; the same write is taken once that is freed. TONE only, since 4.17 ([A busy pin on the phase tap](#a-busy-pin-on-the-phase-tap)) |

A write is all or nothing. Every register of a frame is checked before any of
them is stored, so a NACK leaves the page exactly as it was: none of the up to
four registers a frame carries is kept, and no side effect of one of them runs.
Clearing a latched failsafe is such a side effect.

A NACK is an answer, not a link fault. The panel counts them in its link
report (`nack`, [Bring-up](Bringup.md#link-report)): a set-up from SETUP
INTERFACES that a page refuses is one NACK per refused value, written once
until the value changes, and a pin a page refuses as held elsewhere is one
per 5s offer.

### Page map

| Page | Name | Access | Registers |
| ---: | --- | --- | --- |
| 0x00 | IDENTITY | read | protocol major, protocol minor, firmware major, minor, patch, hardware revision, capabilities bitmap |
| 0x01 | STATUS | read | 10 registers (since 4.21; 8 before it): state (0 idle, 1 armed, 2 failsafe), faults bitmap, uptime in ms (two registers), requests accepted (two registers), XL2515 receive error counter, XL2515 transmit error counter, CAN frames refused since power-up (two registers, low half first; [Refused frames](#refused-frames)) |
| 0x10 | CONTROL | read, write | ARM (0 switches to SAFE; 1 switches every output to ARMED; from 4.18 0x0002 switches the throttle channels to ARMED, 0x0004 the surfaces and the SERVO PWM programming ports, 0x0006 both; a DShot programming port is a throttle (since 4.28); any other value is refused with BAD_VALUE, [An arm for one role](#an-arm-for-one-role)), THROTTLE (0..10000, hundredths of a percent, and it commands every channel CHAN_CFG marks a throttle and every DShot programming port, since 4.28), MOTOR_POLES, CLEAR (write 0x5AFE to leave failsafe). Registers 0 to 2 are the frame that switches to ARMED |
| 0x11 | LIMITS | | declared, not served |
| 0x12 | FAILSAFE | | declared, not served |
| 0x13 | CHANNELS | read, write | one command per output channel, 0..1000 of the channel's travel; eight channels |
| 0x20 | BENCH | read | voltage (10mV), current (10mA), power (W), rpm (revolutions per minute), ESC (electronic speed controller) temperature and motor temperature (0.1°C, signed), charge (mAh), energy (0.1Wh), minimum voltage, maximum current, maximum power, maximum rpm, flags |
| 0x21 | reserved | | not assigned; not to be reused |
| 0x22 | OUTPUTS | read, write | per slot: driver (0 none, 1 PWM (pulse-width modulation), 2 PPM (pulse-position modulation), 3 DShot, 4 bidirectional DShot), pin, first channel and channel count in one register, rate in Hz (kbit/s for both DShot drivers); eight slots of four registers |
| 0x23 | CHAN_CFG | read, write | per channel: role (0 throttle, 1 surface), slew (thousandths of the span per second, 0 to 3000, 0 = immediate; a higher value is refused with BAD_VALUE and nothing is stored (since 4.16); a slew above 3000 kept in the IOmcu's flash by an older build starts as 3000), minimum and maximum pulse in µs, 400 to 2500 and, on a channel a PWM slot renders, at most its frame minus 1µs (since 4.16); eight channels of four registers |
| 0x24 | CATALOGUE | read | the board's own pins, one register each: GPIO (general-purpose input/output) number in 6 bits, the pad number printed beside it in 6, what holds it in 4 (0 free, 1 heartbeat, 2 CAN, 3 flash, 4 debug, 5 sensor, 15 other); 32 slots, and a pad number of 0 means there is no pin in that slot |
| 0x25 | SHAPE | read | where those pads are: outline width and height in 0.01mm, the corner pad 1 sits at and the pads in one row packed as (corner << 8) \| per side, the pitch in 0.01mm, and the distance from the edge to the centre of a pad row. Two rows on one pitch, numbered away from pad 1 along its edge and back along the opposite one. All zero when the IOmcu has no shape for its board, which means it is listed and not drawn |
| 0x26 | ARTWORK | read | what the board's drawing is, 9 registers (since 4.20; 7 before it): blocks of payload (0 when the IOmcu carries none), outline width and height in 0.01mm, format (0 none, 2 the board drawing; 1, an RGB565 photograph, is served by 4.19 and older only), payload length in two registers, a CRC (cyclic redundancy check) over the whole payload seeded zero, and an FNV-1a (Fowler-Noll-Vo) hash over it in two registers, low half first. [The board drawing](#the-board-drawing) has the layout |
| 0x27 | ART_DATA | read, write | the drawing itself: write register 0 to say which block, then read the page. Register 0 reads back the block being served and registers 1 to 31 carry 62 bytes of it. A block does not advance on being read, so a reply that goes missing is asked for again rather than skipped |
| 0x28 | PADS | read | the pads that are not pins, one register each: the pad number in 6 bits, what it is in 2 (0 no pad and the list ends, 1 ground, 2 a rail, 3 neither), and the rail in 8 bits of tenths of a volt. Zero volts on a rail means it is not a fixed voltage, which is not the same as a ground's 0V. 32 slots, in pad order |
| 0x29 | SERVO | read, write | register 0: the frame rate in Hz of the PWM target PROG's `TARGET` names, 40 to 560, or 0 for its own rate. With the standard servo ports as target: every PWM output whose first channel is a surface, 0 for each slot's own rate from OUTPUTS; refused with BAD_VALUE, and nothing changes, when it would leave a PWM slice asked for two rates, and while it is not 0 so is a CHAN_CFG or OUTPUTS write that would (since 4.1). With a programming port as target: that port's pin alone, 0 for 50Hz (since 4.19, [The target's frame rate](#the-targets-frame-rate)). Refused the same way when it would leave a PWM output or a pulsing programming port with an endpoint longer than its frame (since 4.16, [A pulse no longer than its frame](#a-pulse-no-longer-than-its-frame)). A change of `TARGET` sets register 0 to 0 (since 4.19). Registers 1 to 4, one frame: a sweep of the surfaces -- curve (0 stopped, 1 square, 2 sine, 3 triangle), speed in thousandths of a cycle a second (50 to 5000), amplitude in command units either side of the centre (0 to 500), hold at each end in ms (0 to 5000); a sweep starts or changes only from all four written together, refused with BAD_VALUE otherwise, and with NOT_ARMED while the bench is SAFE or one ARMED for the throttle alone (since 4.18); 0 in register 1 alone stops it, and 4 alone holds every surface where its output is, under the same switch to SAFE and 500ms rules. A sweep running when the hold begins keeps its phase (since 4.6): how far into the curve it was, and so its point, its dwell and the ends reached; one that has made its movements is held at the centre it ended on. A write is judged against the page as the IOmcu's next pass would leave it at that moment: a sweep past its last movement, unwritten for 500ms or SAFE has ended, even when the write is served before the pass that ends it. 5 alone (RESUME, since 4.6) carries that sweep on from the kept phase; the surfaces are commanded along the curve again at once and slew there from where they were held, at their own rate. RESUME is refused with BAD_VALUE when no phase is kept -- no sweep was running when the hold began, or the hold ended by a write of 0, a curve written over it, a switch to SAFE, 500ms unwritten or a restart -- and when registers 2 to 5 no longer read what the paused sweep runs, and with NOT_ARMED while the bench is SAFE or one ARMED for the throttle alone. A 4.5 IOmcu refuses 5 with BAD_VALUE. Register 5: the ends a sweep started after it reaches before it stops, 0 for no end. Register 6, read only: the ends the sweep has reached. A sweep stops on a write of 0, on a switch to SAFE and when it has not been written for 500ms, leaving each surface where its output has got to; repeating it keeps its curve going, and a sweep that has made its movements is not started again by a repeat (since 4.2). Nothing is kept: an IOmcu restart reads 0 throughout |
| 0x2A | SUPPLY | read, write | a WeAct PD Power Mini V1 Buck on a PIO (programmable input/output) UART on two of the IOmcu's pins (since 4.3). Registers 0 to 3, one frame: enable, the GPIO that transmits (to the module's DM) and the one that receives (its DP), and the module's UART Baudrate setting, 0 to 6 for 9600, 19200, 38400, 57600, 115200, 230400 and 460800 baud, or 7 (since 4.4) to find it: the UART tries the next rate after every WHO_AM_I without a valid answer, starting at 19200, until a module has answered once; refused with BAD_VALUE on a pin that is reserved, bound to an output or the other pin, and for any change while the output is asked on or may be on (bit 6 of the flags). Since 4.5, a change while a module has answered is acknowledged and held: registers 0 to 3 read the wiring in force and flag bit 8 is set until a state read of the module sent after the change answers, at most about 1.2s; a read showing the output off takes the change, one showing it on or failing refuses it, and bit 9 is set until the next write of registers 0 to 3. Such a change comes alone, without registers 4 to 6, and an ON is refused with BAD_VALUE while one is held. A host older than 4.5 that writes the wiring again while it is held is acknowledged and the hold goes on. The pins are no output's while held. Registers 4 to 6, one frame: output (1 on) and the set points in mV and mA, up to 20000mV and 3000mA; an ON is refused with NOT_ARMED without a live heartbeat, and while wiring just written is not yet in flash (a save is taken 400ms after the last write), and the output goes off when the heartbeat stops. Registers 7 to 16, read only: flags (bit 0 online, bit 1 output on, bits 3..2 mode 0 normal, 1 constant current, 2 overcurrent, bit 4 an output that would not switch, bit 5 set points that would not take, bit 6 an output on or possibly on -- read on, an ON not yet confirmed, or an OFF owed to a module that stopped answering, bit 7 an output the module switched off by itself while ON was asked, held off until OUTPUT is written 0, since 4.5 bit 8 a wiring change held for a state read and bit 9 one that read refused, bit 10 an output switched off because the module's input read under the set point plus 500mV on 2 input reads in a row, held off until OUTPUT is written 0, since 4.13 bit 11 a programming port holds registers 4 to 6: a write that is not an OFF is refused with BAD_VALUE (since 4.15 every change of a set point; before it an ON and a raise of the voltage), see [The programming port for KST and Chaservo servos](LinkKST.md)), the output's mV and mA, the set points read back, the input's state and mV, readings taken and transactions failed modulo 65536, and (since 4.4) the rate in use, 0 to 6, or 7 while AUTO has found none. Register 17 (since 4.4), written alone as 1 with the output asked off and the supply enabled, restarts the module (SYSTEM_RESET) once it has answered and its output reads off; it reads 0. The wiring is kept in the IOmcu's flash and driven at boot with the output off, so a module left on is switched off after a restart; the command is not kept |
| 0x2B | SENSE | read, write | two I2C (Inter-Integrated Circuit) current monitors on one bus on two of the IOmcu's pins, a TI INA228 in the ESC's power path and a TI INA3221 on the servo rail (since 4.7). Registers 0 to 3, one frame: enable (bit 0 INA228, bit 1 INA3221, bit 2 the AS5600 output encoder since 4.9), the SDA GPIO, the SCL GPIO, and the clock, 400kHz and no other value: at 100kHz one read takes 480 to 750µs and the IOmcu's 1ms schedule does not fit. The schedule reads INA3221 CH1 at 1000Hz, CH2 and CH3 in turn at 500Hz each, each channel's bus voltage every 24ms (41.7Hz), the INA228's current and voltage at 500Hz each, its die temperature, the INA3221's Mask/Enable and each part's set-up register read back (since 4.11) at 25Hz each, and the INA228's DIAG_ALRT, ENERGY and CHARGE at 50Hz. Both modules carry pull-ups on SDA and SCL, and they add in parallel: the DAOKAI INA3221 has 10kΩ to its VS (3.3V); the MATEK INA228's pull-up value and rail are unknown. The combined value must stay above about 1kΩ: an I2C output sinks 3mA at 0.4V, and (3.3V − 0.4V) / 3mA is 967Ω. Below 10kΩ it shortens the rise time: the 300ns rise limit at 400kHz allows 35pF of bus with 10kΩ alone, and more with less. SDA's GPIO number mod 4 is 0 or 2 and SCL is the GPIO after it, one I2C block's pair (GP16 and GP17 by default). Registers 4 to 7, one frame: the INA228's address, 0x40 to 0x4F (default 0x45, the MATEK I2C-INA-BM's as shipped; its solder bridges give 0x44 or 0x41); its shunt in µΩ, 50 to 20000 (default 200); the current its range is set for in 0.1A, 10 to 3000 (1.0 to 300.0A, the bench's design maximum; default 2048). The maximum chooses ADCRANGE only: 1 while the shunt's voltage at it is at most 40.96mV, 0 up to 163.84mV, and above that the write is refused. CURRENT_LSB is the shunt ADC's step divided by the shunt (78.125 nV or 312.5 nV over R), so CURRENT and the shunt voltage clip together, and SHUNT_CAL is 4096 at either range. A shunt whose full scale at the chosen range passes 2000A is refused as well, so below 81.92 µΩ only ADCRANGE 1 is taken; the rule is the driver's, `ina228_calibrate()`; register 7 reserved. Registers 8 to 11, one frame: the INA3221's address, 0x40 to 0x43 (default 0x40); its shunt in 0.1mΩ, 50 to 10000 (5mΩ to 1Ω, default 1000, the 0.1Ω that reads to 1.638A); the channels read, bits 0..2 for CH1 to CH3, at least one while enabled (default CH1); register 11 reserved. The reserved registers read 0 and take only 0. Refused with BAD_VALUE: a value out of range, SDA and SCL not one block's pair whatever ENABLE holds, while a part is enabled a pin that is reserved, bound to an output or held by SUPPLY, both parts on one address while both are enabled, and any change while the bank is ARMED; a write of the set-up in force is taken. One frame that is not a pair is taken: ENABLE 0 with SDA or SCL 0, which is what a panel writes for pins that are not set. The pins are no output's while any part is enabled, and an OUTPUTS write binding one is refused. Registers 12 to 25, read only: flags (bit 0 INA228 online, bit 1 its last identity read was an INA228's, bit 2 something else answers at its address, bit 3 a current read at the end of its range in the last 50ms window or since the run's switch to ARMED, so BENCH's current and power, or their peaks, are bounds and not values; bits 4 to 6 the same three for the INA3221; bit 8 the bus is open on its pins, bit 9 SDA or SCL is held low and being clocked free; lines low at idle that rise within 50µs on the pads' own pull-ups, 32 to 86kΩ, are held by nothing -- pins with no part and no pull-up wired to them -- and leave bit 9 clear, the parts not answering), the addresses that answered the last scan (bit n for 0x40 + n), the INA228's DEVICE_ID and the INA3221's die ID as read, transactions failed modulo 65536, the INA228's die temperature in 0.1°C (signed) and DIAG_ALRT, its charge in 0.01mAh (signed, 32 bit, registers 19 and 20, low first) and energy in 0.01Wh (32 bit, registers 21 and 22, low first) since the run's switch to ARMED, the ESC's own telemetry voltage (10mV) and current (10mA), and their valid bits (bit 0 voltage, bit 1 current). Registers 26 to 30, read only, since 4.9: the output encoder's flags, RAW ANGLE, MAGNITUDE, sample count and still time in ms (see below). Register 31, read only, since 4.11: `RESETS`, the current monitors found reset, the INA228 in the low byte and the INA3221 in the high byte, each modulo 256 ([The window ring](#the-window-ring)); a 4.10 or older IOmcu reads 0. Registers 0 to 11 are kept in the IOmcu's flash |
| 0x2C | SERVO_SENSE | read, write | the INA3221's channels and a move timed on the IOmcu's clock (since 4.7). Registers 0 to 11, read only, four a channel from CH1: mean current (mA, signed), highest current (mA, signed), mean bus voltage (mV) and lowest bus voltage (mV) over the last 50ms window, the voltage at the load side of the shunt. Register 12, read only: the window number modulo 65536; a read does not end a window. Register 13, read only: bits 0..2 a channel's window holds readings, bits 4..6 one of them read the top of the range (163.8mV across the shunt), which makes that channel's mean and highest current lower bounds, bit 7 the same of the capture. Registers 14 to 17, one frame: a capture -- bit 7 set, the INA3221 channel in bits 0..1, CH1 only (2 and 3 are refused; the field stays for a channel read fast enough later), and the output channel (0 to 7) in bits 8..10 whose next changed command starts the timing, or with bit 11 set (since 4.13) the programming port in those bits ([The programming ports](#the-programming-ports)); the holding level the move ends at in mA, signed, -32768 to 32767 (since 4.11; 0 to 32767 before it, a larger value refused); the movement threshold and the arrival band, each 1 to 32767mA. An arm is the whole frame and restarts a capture already running. 0 in register 14 switches to SAFE and is never refused; written at the head of the frame, the other three are not stored. Refused with BAD_VALUE: any other write that is not the whole frame, other bits set in register 14, a value out of range, a channel other than CH1 or one SENSE does not read, and an output channel that is not a surface on a PWM slot the IOmcu has bound (a pin whose compare register another pin holds is not); with NOT_ARMED on a SAFE bank or one ARMED for the throttle alone (since 4.18). A bank that stops driving the surfaces ends a capture that has not finished. Registers 18 to 24, read only: the state (0 idle, 1 armed, 2 waiting for movement, 3 moving, 4 arrived, 5 settled on an end stop, 6 late: movement and no arrival within 3000ms plus the meter's lag, 7 unseen: no movement in that time, 8 lost: the INA3221 stopped answering, or no PWM edge came within 3000ms of the arm), captures finished modulo 65536, the time from the PWM frame carrying the new pulse to movement and to arrival in 0.1ms, resolved to CH1's 1ms sample interval, the highest and mean filtered current of the move (mA, signed), and the samples in it. Nothing is kept: an IOmcu restart reads 0 throughout |
| 0x2D | TONE | read, write | the beeps of an ESC heard on one motor phase, through a series resistor and a zener clamp on one IOmcu GPIO, stamped by a PIO state machine at 26.7ns (since 4.8). Registers 0 to 3, one frame: enable (bit 0), the GPIO (default 22, pad 29), the lowest tone heard in Hz (50 to 2000, default 400) and the highest (above the lowest, to 6900, default 6500). Registers 4 to 7, one frame: the pitch change in percent that starts a new beep without a silence (0 splits on silence only, 50 at most, default 8), the silence that ends a beep in ms (1 to 100 and at least the period of the lowest tone, default 3), the tone periods that make a beep (1 to 64, default 3), and register 7 reserved. The reserved register reads 0 and takes only 0. Refused with BAD_VALUE: a value out of range, a combination the detector refuses (a silence shorter than the lowest tone's period), and, while the tap is enabled, a GPIO past the bank (63), reserved by the board, or an ADC (analog-to-digital converter) pin: GP26 to GP29 of the RP2350A and GP40 to GP47 of the RP2354B, which are not fault tolerant, and any GPIO on an IOmcu whose core 1 did not start at boot (no capture runs before a restart). Refused with BUSY (since 4.17; BAD_VALUE before it), while the tap is enabled: a GPIO bound to an output or held by SENSE, SUPPLY or a programming port, and a capture the IOmcu cannot start for now (core 1 in a pass, or no PIO state machine or DMA channel left by the outputs and the supply) ([A busy pin on the phase tap](#a-busy-pin-on-the-phase-tap)). The GPIO is no output's while the tap is enabled, and an OUTPUTS, SENSE or SUPPLY write that takes it is refused. A change is taken armed or not: the tap is an input and drives nothing. Registers 8 to 12, read only: flags (bit 0 the capture runs, bit 1 the tap is enabled and its pin could not be taken, bit 2 the capture ring or the state machine's FIFO overran since the capture started, bit 3 a run of bursts is under way, bit 4 the last window held a tone), the window number modulo 65536 (windows of 8ms), the last window's tone in 0.1Hz (0 for none) and the tone periods in it, and the number of the newest beep. The IOmcu keeps the last 64 beeps and numbers them 1 to 65535, then from 1 again; the newest number is 0 before the first beep. Register 13, EVT_SEL, is the one writable register after register 7 and is not kept: the number of the beep that registers 14 to 21 show. A read consumes no beep, so a reply lost on the link loses nothing. Registers 14 to 21, read only: EVT_SEL again while that beep is among the 64, else 0 with registers 15 to 21 reading 0; the beep's first rise in ms since the capture started (32 bit, registers 15 and 16, low first); its length to its last edge in 0.1ms; its mean pitch in 0.1Hz; its bursts; the carrier it was chopped at in 100Hz steps (0 unchopped); flags (bit 0 it began at a pitch change with no silence before it, bit 1 it ended at one). Registers 22 and 23, read only: beeps lost, and lows shorter than 500ns that the detector ignored, each modulo 65536. The capture's 8µs hold-off removes every low shorter than 8µs before the detector sees it, so register 23 reads 0 on the tap. The capture starts when the tap is enabled or its pin changes, and then empties the 64. Registers 0 to 6 are kept in the IOmcu's flash and the tap starts at boot. While the tap is disabled the pin is an input with its pull-down on (32 to 86kΩ), and it stays on while the tap runs |
| 0x2E | BIND_CFG | read, write | a CHAN_CFG page prepared and not in force (since 4.10): the registers and the value rules of CHAN_CFG. [A binding taken whole](#a-binding-taken-whole) has the three pages |
| 0x2F | BIND_OUT | read, write | an OUTPUTS page prepared and not in force (since 4.10): the registers and the value rules of OUTPUTS |
| 0x30 | BIND | read, write | register 0 `COMMIT` (since 4.10): a write of the CRC-16 of the 64 prepared registers puts both prepared pages in force or neither; refused with BAD_VALUE for another value and for a page its own rules refuse. Reads the CRC of what is prepared |
| 0x31 | SERVO_WIN | read | the last 4 complete 50ms windows of INA3221 CH1, newest first (since 4.11). Register 0: the newest window's number modulo 65536. Register 1: bit 0 a window has closed, bit 7 the capture's clipped bit. Registers 2 and 3: SERVO_SENSE's capture state and count. Then 4 entries of 6 registers, entry k from register 4 + 6 k for window number register 0 - k: mean, highest and lowest current (mA, signed), mean and lowest bus voltage (mV), and flags (bit 15 a window closed with this number, bit 8 current samples, bit 9 voltage samples, bit 10 a sample at the top of the range, bit 11 one at the bottom, bits 0 to 7 the samples at an end, held at 255). A clipped sample counts at the end of the range. Every write is refused with READ_ONLY. [The window ring](#the-window-ring) has the rules. Nothing is kept |
| 0x32 | `KST` | read, write | the servo programming page: the programming protocol for KST and Chaservo servos on a programming port (since 4.12; a port and the supply since 4.13). A read gives 32 registers: state, flags and port, `SEQ`, operation and frames, result and refusal, the failing register, 4 register masks, the fingerprint, the measured half-cell and reply delay, and the servo image in 16 registers. A write takes 28: the command in registers 0 to 3, one frame, the staged image, unlock set and start check in 4 to 25, and the supply voltage and current of an ENTER in 26 and 27. [The programming port for KST and Chaservo servos](LinkKST.md) has the rules. Nothing is kept |
| 0x33 | PROG | read, write | the programming ports (since 4.13): 28 registers, 24 before 4.26. The port count, flags, the pulse switch and the servo tester's target in registers 0 to 3, then 5 registers a port for 4 ports: pin, command, slew and the two endpoints, then each port's FUNC in 24 to 27 (since 4.26). [The programming ports](#the-programming-ports) has the rules. Pin, slew, endpoints and FUNC are kept in the IOmcu's flash |
| 0x34 | `HITEC` | read, write | raw transactions of the Hitec servo wire on a programming port (since 4.23). A read gives 32 registers: state, flags and port, `SEQ`, operation and transaction count, result and refusal, the capture's count and flags, the decoder's result, the decoded value, `VIEW`, the received bytes in 8 registers, and in 16 registers the bytes' start times or the run. A write takes 24: the command in registers 0 to 3, one frame, the staged transaction, decoder and POWER set points in 4 to 21, `VIEW` in 22, and the receive mode of a D series line in 23 (since 4.24). [The programming port for Hitec servos](LinkHitec.md) has the rules. Nothing is kept |
| 0x35 | SENSE_MAP | read, write | up to 4 INA3221 at 0x40 to 0x43 on SENSE's bus (since 4.25). Register 0 the parts fitted, bit a for 0x40 + a; 1 `FAST`, the channel read every 1ms (0 to 11, 0xFF none); 2 to 13 each channel's shunt in 0.1mΩ (0 off, 50 to 10000); 14 to 25 what each channel measures (0 none, 0x01 to 0x08 an output channel, 0x10 to 0x13 a programming port, 0x20 the supply); read only: 26 to 29 each part's die ID, 30 flags, 31 reserved. Registers 0 to 25 are kept in the IOmcu's flash. While the map is in force SENSE's ENABLE bit 1 and registers 8 to 10 mirror it. [The INA3221 map and windows](LinkSensors.md) has the rules |
| 0x36 | SENSE_LIMIT | read, write | a current limit per channel of SENSE_MAP (since 4.27), an alert only. Registers 0 to 11 each channel's limit in mA (0 none, 1 to 32767), kept in the IOmcu's flash; read only: 12 `OVER`, bit c a window of channel c above its limit since the last read of the register, which clears it; 13 `OVER_COUNT`, channel windows over, modulo 65536; 14 and 15 reserved. 16 registers. [The INA3221 map and windows](LinkSensors.md#sense_limit) has the rules |
| 0x37 | SENSE_WIN_A | read | channels 0 to 5's last 50ms window (since 4.25): the window number, flags, and 5 registers a channel: mean and highest current (mA, signed), mean and lowest bus voltage (mV), flags. Every write is refused with READ_ONLY. Nothing is kept |
| 0x38 | SENSE_WIN_B | read | channels 6 to 11's, as SENSE_WIN_A (since 4.25) |
| 0xF0 | UPDATE_CTL | read, write | the IOmcu's own firmware update (since 4.22; frozen across majors): 32 registers. Version, state, error, slot, slot sectors, next block, image size, then the command in register 8 with up to 3 arguments, and a SHA-256 digest in registers 16 to 31. A write that is not a command is refused with READ_ONLY. [Firmware update over the link](#firmware-update-over-the-link) has the rules. Nothing is kept |
| 0xF1 | UPDATE_DATA | read, write | the 64 bytes of the block in hand, low byte first in each register (since 4.22; frozen across majors): 32 registers. FINISH takes the expected SHA-256 from registers 0 to 15. Nothing is kept |

Faults bitmap: bit 0 link silent, bit 1 overcurrent, bit 2 over-temperature,
bit 3 stall, bit 4 heartbeat stopped, bit 5 protocol version mismatch, bit 6
the IOmcu's flash store is off for this boot (since 4.7): its second
core did not register for the flash lock-out, so nothing is saved and
set-ups written over the link run from RAM until a restart. Faults
are sticky until read and cleared; bit 6 stays set until a restart. Bit 0 needs the link to have carried at
least one request before it can be set: the IOmcu is awake before the
panel polls, and the wait for the first one is not silence.

Capabilities bitmap: bit 0 ESC drive, bit 1 ESC telemetry, bit 2 servo PWM, bit
3 servo current sense, bit 4 pack sense, bit 5 receiver bus, bit 6 vibration
sensor and index pulse, bit 7 cell monitor, bit 8 programming. The panel
derives the menu marks from it. Bits 3 and 4 say what the SENSE set-up
enables -- bit 3 the INA3221, bit 4 the INA228 -- fitted as configured,
not answering now (since 4.7). They change only when a SENSE write is
taken, and a panel reads them again after it writes SENSE. Whether a part
answers is SENSE's FLAGS and BENCH bit 5, which the panel polls. The TONE page
adds no bit.

BENCH flags: bit 0 voltage valid, bit 1 current valid, bit 2rpm valid, bit 3
the ESC's temperature valid, bit 4 the motor's temperature valid, bit 5 the
voltage, current and power are the INA228's and not the ESC's telemetry
(since 4.7), bit 6 charge and energy are the INA228's accumulators, cleared
at this run's switch to ARMED, with the part answering throughout (since 4.7), bit 7
simulated. Without bit 6 a panel counts charge and energy itself from the
current; with it, this build's panel shows the INA228's, in SENSE's 0.01mAh
and 0.01Wh steps from a SENSE read younger than 100ms, from 100ms into the run. The two temperatures carry
separate bits because they come from
different places and one of them usually does not come at all: an ESC reports
its own temperature over extended DShot telemetry and knows nothing about the
motor it drives. Bit 3 validated both until protocol 3.0, which is why that
change is a major. An IOmcu without a measurement front
end sets bit 7, and the panel draws SIMULATION across the screen.

MOTOR_POLES is the magnet count of the motor under test, even and between 2 and
42, or zero meaning nobody has said. A bidirectional DShot ESC reports
electrical periods and has no idea what it is bolted to, so this is the one
number the wire has to carry for the IOmcu to report a mechanical speed;
at zero it reports no speed rather than one derived from a guess. The panel
sends it from the `Motor poles` setting when an IOmcu starts answering,
again whenever the setting changes, and in the frame that switches to ARMED whether or not
a change is owed. A write
nobody answers stays owed and goes out again at the next 50ms poll; a write
the IOmcu refuses is not retried, because the same request refused once
is refused every time, and it waits for the next edit or link-up edge instead.
The guard at zero covers a count never sent and not one that is out of date:
every count the setting allows is inside the range the page takes.

The throttle channels' endpoints, the `Idle pulse` and `Full pulse`
settings, travel on CHAN_CFG and not in the frame that switches to ARMED.
The panel sends them when an IOmcu starts answering, after each edit and
after each binding, once the edit has rested 300ms, at a poll whose write
of ARM = 0 was acknowledged: it reads the page and writes it only when the
throttle channels differ, and checks the stored registers the
acknowledgement carries. A read or a write nobody answers stays owed. A
refused read, a refused write, an Idle pulse not below Full pulse or an
acknowledgement whose throttle channels hold another pair ends the debt
with an alert and blocks the switch to ARMED until an edit, a binding or a
link-up brings the pair into force. While they are owed or blocked with the
link up, the panel writes no switch to ARMED and says why, from `Pulse
range not yet sent to the IOmcu. Hold ARM again.` to `No pulse range from
the IOmcu. Update the IOmcu firmware, then hold ARM again.`; [A pulse range
on its way](Safety.md#a-pulse-range-on-its-way) has the rules and the
texts. The rule is the
panel's: the IOmcu takes a CHAN_CFG write before it acknowledges it and
has no rule of its own for an edit it has not received.

The IOmcu refuses a pin it must not drive -- the safety line, the CAN
controller's pins, GP23, GP24, GP25 and GP29 (used by the module, not brought
out) and any number above GP29 -- on the OUTPUTS and SUPPLY pages alike. An
OUTPUTS write with a slot the silicon cannot bind -- such a pin, or a PIO state
machine, instruction memory or DMA channel that the phase tap or the supply's
UART holds -- is refused with BAD_VALUE, the slots in force stay, and nothing
is saved. [DShot and the output drivers](DShot.md) has the rest.

CHAN_CFG and OUTPUTS entries are written whole, four registers at a time. A
channel's pulse range defaults to 1000..2000µs; endpoints outside 400..2500µs
are refused with BAD_VALUE, and so is an endpoint longer than the frame of the
PWM output that renders it
([A pulse no longer than its frame](#a-pulse-no-longer-than-its-frame)). A
command outside its range is clamped. Two slots
on one pin, or two slots rendering the same channel, are refused. Switching to ARMED is
decided by the IOmcu: a write of ARM is refused with NOT_ARMED while the
link is in failsafe, the heartbeat is not trusted or the arm latch is set.

While the bench is ARMED -- the bank drives, or the ARM register is set -- an
OUTPUTS write that changes any register is refused with BAD_VALUE, and so is
a CHAN_CFG write that changes a channel's role or the role, slew or
endpoints of a channel whose role is throttle. A surface's slew and endpoints
are taken while ARMED, and so is a write of the page in force; an OUTPUTS
write of the page in force while ARMED binds nothing anew. The rule is
`outputs_chan_cfg_armed_check()` and `outputs_slots_armed_check()`, under
`test_link_pages`.

The arm latch and the refusal while ARMED change no register and no frame, so they
carry no protocol version of their own. What a peer built without them sees:

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| older | this build | the link comes up and the bench switches to ARMED: that panel writes CLEAR ahead of every switch to ARMED. Its first switch to ARMED after a STOP can be refused once with NOT_ARMED, because it waits a fixed 100ms; the refusal leaves the latch set and the second hold switches to ARMED. An ARM = 1 it writes after an IOmcu restart is refused, and it latches a stop |
| this build | older | the link comes up and the bench switches to ARMED. The panel stops on the link-down edge, so a restart it notices leaves nothing ARMED. A restart it does not notice -- the request retransmitted until the IOmcu answers again -- is ARMED again by the next poll, as between two older builds. A heartbeat distrusted for under one poll interval does not latch there |
| another host | this build | an ARM with no CLEAR since the IOmcu started is refused with NOT_ARMED; a CHAN_CFG or OUTPUTS write as above is refused with BAD_VALUE while ARM is set |

An arm from the panel is two transactions. CLEAR travels first and alone: the
IOmcu checks ARM against its failsafe before it applies a CLEAR from the
same frame, so a frame carrying both is refused with NOT_ARMED whenever the
clear was needed. Then ARM, THROTTLE and MOTOR_POLES go as one three-register
frame from offset 0, so the IOmcu starts the run on the pole count the
panel sent or does not start it. Every 50ms poll after that writes ARM and
THROTTLE; a pole count edited during a run goes on a write of its own at the
next poll. The rules of the page -- the throttle range, the pole count, the
CLEAR magic, ARM refused in failsafe, and a refusal storing nothing -- are
`shared/link/link_control.c`, under `test_link_pages`. Whether the bench may
arm, and what sets and clears the latch, is `shared/safety/safety_gate.c`,
under `test_safety_gate`.

Before CLEAR the panel waits for the IOmcu to trust the heartbeat: 100ms
after the hold completes it reads STATUS register 1 once per 5ms pass until
bit 4 reads clear, for at most 200ms more. The IOmcu fills STATUS
registers 0 and 1 at the read.

### An arm for one role

From 4.18 the ARM register names the roles a switch to ARMED covers. A channel's role
is CHAN_CFG's: throttle or surface. A SERVO PWM programming port is a
surface, a DShot one a throttle (since 4.28).

| ARM | Name | Drives |
| ---: | --- | --- |
| 0x0000 | -- | nothing: a switch to SAFE |
| 0x0001 | `LINK_ARM_ALL` | every output, as an IOmcu before 4.18 does for any value but 0 |
| 0x0002 | `LINK_ARM_THROTTLE` | the slots whose channels are throttles: motors and ESCs; and the DShot programming ports (since 4.28) |
| 0x0004 | `LINK_ARM_SURFACE` | the slots whose channels are surfaces, and the SERVO PWM programming ports whose `PWM` bit is set |
| 0x0006 | both role bits | every output, as 0x0001 |

Any other value is refused with BAD_VALUE and changes nothing. It is
checked ahead of the failsafe, so it sets no arm latch. A channel of a role
the ARM does not cover stays at its rest and its slot renders no pulse: a
PWM pin stays low, a DShot (digital shot) slot sends no frame. A PPM
(pulse-position modulation) slot carries all its channels in one frame; it
runs while the ARM covers one of them, with the others at their rest.

The roles change only through a switch to SAFE. While ARM holds a value other than
0, a write of a different value other than 0 is refused with BAD_VALUE and
changes nothing; a write of the same value is taken. The panel writes ARM = 0
first, then the new value after the line has settled, 100ms at the
earliest. An output that leaves the ARM goes to its rest at once, with no
slew.

STOP, a heartbeat that is not trusted, link silence and the failsafe zero
ARM whatever roles it named, and the whole bank goes to rest. The arm latch,
CLEAR and the frame that switches to ARMED are as in [Compatibility](#compatibility).

What the screens write:

| Screen | ARM |
| --- | --- |
| MOTOR & ESC, its PROGRAM included (the stick run) | 0x0002 |
| SERVO | 0x0004 |

A SWEEP, HOLD or RESUME of the SERVO page and a SERVO_SENSE capture need
the surfaces ARMED: under 0x0002 they are refused with NOT_ARMED. A KST
session takes no port while the bank is ARMED for any role.

The rules are `link_arm_value_ok()` and `link_control_write()` in
`shared/link/link_control.c`, `outputs_arm_roles()` in
`shared/outputs/outputs.c`, and `arming_request_arm_roles()` in
`shared/safety/arming.c`, under `test_link_pages`, `test_outputs`,
`test_safety_gate` and `test_arming`.

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| 4.18 | 4.18 | as above |
| 4.18 | 4.17 or older | the panel writes 0x0001 and the IOmcu switches its whole bank to ARMED, as before 4.18: ARM on the SERVO screen also drives the throttle channels at their rest, and ARM on MOTOR & ESC also drives the surfaces and a pulsing programming port |
| 4.17 or older | 4.18 | that panel writes 1, and every output is ARMED |

### A pulse no longer than its frame

From 4.16 every endpoint a PWM (pulse-width modulation) output renders fits
its frame. The longest pulse a frame carries is the period minus 1µs,
rounded down: the counter's last count at 1MHz, which leaves the pin low
for 1µs. The rule is `out_pwm_pulse_max()` in
`shared/outputs/out_pwm_map.c`.

| Rate | Period | Longest endpoint |
| ---: | ---: | ---: |
| 560Hz | 1785µs | 1784µs |
| 500Hz | 2000µs | 1999µs |
| 400Hz | 2500µs | 2499µs |
| 399Hz and below | 2506µs and more | 2500µs, CHAN_CFG's ceiling |

The rate judged is the one the output runs at: SERVO's register 0 for a
slot whose first channel is a surface while that register is not 0 and the
standard servo ports are the target, the slot's own OUTPUTS rate otherwise;
for the programming port `TARGET` names SERVO's register 0, or 50Hz while
that reads 0, and 50Hz for every other port
([The target's frame rate](#the-targets-frame-rate)). A channel is judged by the longer of
its two endpoints, so a reversed pair counts the same. PPM (pulse-position
modulation) and DShot slots are not judged: their drivers build their own
frames.

A write that would leave a longer endpoint is refused with BAD_VALUE and
changes nothing:

| Write | Refused when |
| --- | --- |
| CHAN_CFG (0x23) | an endpoint of a channel a PWM slot renders is longer than that slot's frame |
| OUTPUTS (0x22) | a PWM slot's rate is too fast for its channel's endpoints |
| BIND (0x30) `COMMIT` | the prepared CHAN_CFG page, judged with the prepared OUTPUTS page, breaks the rule; either page alone may break it |
| SERVO (0x29) register 0 | the rate would leave the target -- a surface, or the target programming port with its `PWM` bit set -- with an endpoint longer than its frame. 0 is judged as well with the standard servo ports as target: it sends each surface back to its slot's own rate |
| PROG (0x33) `TARGET` | the change leaves the standard servo ports while SERVO's register 0 is not 0, and a PWM slot back at its own OUTPUTS rate would have an endpoint longer than that frame |
| PROG (0x33) `PWM`, `MIN_US`, `MAX_US` | a port whose `PWM` bit is set after the write has an endpoint longer than the frame of that port's rate |

The checks are `outputs_pulse_check()`, `outputs_chan_cfg_pulse_check()`
and `outputs_slots_pulse_check()` in `shared/outputs/outputs_pages.c`, and
`prog_port_write()` in `shared/outputs/prog_port.c`, under `test_outputs`,
`test_servo_page` and `test_prog_port`.

A standard servo at 1000 to 2000µs takes 499Hz and is refused at 500Hz
and 560Hz. SERVO's rate applies to its target alone, so with a programming
port as target 560Hz is refused only while that port's pulse is on with an
endpoint over 1784µs; a bound PWM surface keeps its own rate and is not
judged.

At start the IOmcu restores CHAN_CFG and OUTPUTS from its flash. A
stored PWM slot whose channel has an endpoint longer than the frame of the
slot's own rate starts unbound: OUTPUTS reads driver 0 for it, as for a
stored slot on the ports' channels. SERVO's register 0 and PROG's `PWM` bits
are not kept and start at 0, so nothing else is judged at start.

The PWM driver (`firmware/iomcu/src/out_pwm.c`) still cuts a level past the
frame's last count to that count. No page write reaches it.

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| 4.16 | 4.16 | as above. The panel's SERVO screen keeps a pause of at least 1000µs between pulses (500µs for a heli servo) and raises the rate only after the narrower range landed, so with OUTPUTS as its target it asks for no refused combination. With a programming port as its target it writes only the port's range, and its rate is refused while a bound PWM surface has an endpoint longer than the frame; the screen then shows REFUSED. Before a write that switches a port's pulse on, the panel writes the port the range the screen names |
| 4.16 | 4.15 or older | the IOmcu takes a longer endpoint. Its PWM driver cuts the pulse to the frame's last count, 1784µs at 560Hz, while the page, the panel and the log show the endpoint written. The range the panel writes ahead of a pulse is taken as any range write |
| 4.15 or older | 4.16 | the SERVO screen behaves as above. A port's `PWM` write is refused while the port keeps a range from an earlier profile that does not fit the port rate; that panel reads PROG again and writes it again on its next pass, and the port renders no pulse until a command has written it a range that fits |

### The target's frame rate

From 4.19 SERVO's register 0 drives the PWM target alone, the output that
PROG's `TARGET` names for the servo tester:

| `TARGET` | Runs at SERVO's rate | Runs at its own rate |
| --- | --- | --- |
| 0, the standard servo ports | every PWM slot whose first channel is a surface | every other slot at its OUTPUTS rate; every programming port at 50Hz |
| n + 1, programming port n | port n's pin; 0 means 50Hz | every slot at its OUTPUTS rate; every other port at 50Hz |

A SERVO rate change therefore moves no throttle, no other slot and no other
port. A change of `TARGET` sets register 0 to 0: the new target starts at
its own rate, and the old one goes back to its own. The panel writes the
rate again with the next position.

The RP2350 runs both channels of a PWM slice from one clock divider and one
wrap register, so the two pins of one slice cannot run at two rates. The
rules that keep a slice at one rate:

- A slot pin shares no slice with a port pin ([The programming ports](#the-programming-ports)).
- With the standard servo ports as target, a rate that would put a surface
  and a slot at another rate on one slice is refused with BAD_VALUE, as
  since 4.1.
- A port whose pin shares its slice with another held port's pin takes a
  SERVO rate of 0 or 50Hz only; any other is refused with BAD_VALUE.
- While the target port runs at a rate other than 50Hz, a `PIN` write
  that moves another port onto the target's slice is refused with
  BAD_VALUE.

The rules are `servo_page_write()` in `shared/outputs/servo_page.c` and
`prog_port_write()` in `shared/outputs/prog_port.c`; the IOmcu binds
the rates in `ports_apply()` and `target_sync()` in
`firmware/iomcu/src/iomcu_pages.c`.

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| 4.19 | 4.19 | as above. The panel tracks `TARGET` on PROG: a change it reads or writes acknowledged counts as rate 0, and a `TARGET` write without an answer counts as an unknown rate until a rate write lands. The SERVO screen shows the rate as not sent until the next position carries it |
| 4.19 | 4.16 to 4.18 | the IOmcu runs every PWM surface and every programming port at SERVO's rate, whatever `TARGET` names, and keeps it across a `TARGET` change. The panel tracks no `TARGET` change below minor 19, and its SERVO screen says every PWM surface runs at the rate. With a programming port as target, the rate is refused while a bound PWM surface has an endpoint longer than the frame |
| 4.18 or older | 4.19 | the IOmcu sets the rate to 0 on a `TARGET` change, while the panel counts the rate it wrote last. The panel then counts a rate at least as fast as the one in force, the side on which no pulse is sent too wide for its frame, and writes the rate again with its next position |

`test_servo_page`, `test_prog_port`, `test_prog_link` and `test_link_pages`
hold the rate independent of the slots, a port target at 39, 40, 499, 500,
560 and 561Hz against the PWM range and its endpoints, a shared slice that
takes 0 and 50Hz and refuses 40, 300 and 560Hz, a target change while
ARMED, and the panel's tracking of a `TARGET` change.

### The board drawing

From 4.20 the IOmcu serves a drawing of its own board on ARTWORK
(0x26) and ART_DATA (0x27): the outline, the parts as flat shapes and every
pad with what it is. PICK A PIN draws the board from it to scale. A new board
brings its drawing in its own IOmcu image; the panel needs no change
for it. The RP2350-CAN's drawing is 1,560 bytes in 26 blocks: 40 pads and
102 shapes on an outline of 51.00 × 21.00mm.

The payload is little-endian, in 0.01mm, with its origin at the top-left
corner of the outline seen from the component side, x along the pad rows.

| Part | Bytes | Fields |
| --- | --- | --- |
| header | 16 | version (1), pads (1 to 64), shapes (0 to 400, 16 bit), outline width and height, board colour and pad ring colour in RGB565, the outline's corner radius, the ring radius, the hole radius (below the ring radius), 0 |
| a pad | 8 | pad number (1 to 63, ascending), kind (1 ground, 2 a rail, 3 neither, 4 a GPIO), value (the GPIO number of a GPIO pad, a rail's voltage in 0.1V, else 0), flags (bit 0 a square ring), x and y of the centre |
| a shape | 12 | kind (1 rectangle, 2 rounded rectangle, 3 disc, 4 line), the corner radius of a rounded rectangle or the width of a line (else 0), colour in RGB565, then four coordinates: left, top, width and height of a rectangle; centre and radius of a disc; both ends of a line |

The longest payload is 5,328 bytes, 86 blocks. Every ring and every shape
lies inside the outline, and the length is exactly the header, the pads and
the shapes; the panel refuses anything else whole and draws the board from
SHAPE (0x25) instead. The layout is `LINK_DRAW_*` in
`shared/link/include/link_pages.h`; `link_drawing_parse()` in
`shared/link/link_drawing.c` is the panel's check.

The panel also refuses a drawing that disagrees with the board's other pages:
an outline other than SHAPE's, a pad that is not on SHAPE's grid, or a pad
whose kind or value differs from CATALOGUE (0x24) and PADS (0x28). The pin
lists stay on CATALOGUE and PADS, and the drawing only places them.

The panel keeps a fetched drawing in its `boardart` flash partition under the
board ID, with every ARTWORK register from register 1 on. At link-up it reads
ARTWORK and shows the kept drawing only when all of them match: format,
outline, length, CRC and hash. Any other drawing for the same board ID is
fetched again, so an IOmcu flashed with a changed drawing is never shown
with the old one. A fetch checks the payload against the CRC and the hash
before it is shown or kept.

`tools/gen_board_drawing.py` writes the IOmcu's drawing,
`firmware/iomcu/src/board_drawing_rp2350_can.c`, from the configurator's
`configurator/drawings/panel.json`, and refuses a drawing the panel would
refuse. CI (continuous integration) runs it with `--check`.

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| 4.20 | 4.20 | as above |
| 4.20 | 4.19 or older | the panel does not read ARTWORK and draws the board from SHAPE. The log says `hardware 1 serves no board drawing (protocol 4.19); PICK A PIN draws its shape` |
| 4.19 or older | 4.20 | the panel reads seven ARTWORK registers, finds format 2 and fetches nothing: `no photograph from hardware 1: it describes a photograph that cannot be one`. A photograph it kept before still shows |

`test_link_drawing`, `test_board_drawing`, `test_link_artxfer`,
`test_art_fetch`, `test_art_store` and `test_picker_screen` hold the layout
at its limits (64 pads, 400 shapes, a ring touching the edge, one past each),
a truncated, malformed or wrongly checksummed payload, a hash that differs in
either half, a link lost at every transaction of a fetch, a kept drawing
under the same board ID with another hash, and a board the panel's build
does not know. `test/tools/test_gen_board_drawing.py` holds the generator to
the same rules.

### Refused frames

From 4.21 the IOmcu counts the CAN frames it receives and refuses to
decode, and publishes the count in STATUS (0x01) registers 8 and 9
(`LINK_ST_REFUSED_LO` and `LINK_ST_REFUSED_HI`), low half first. A frame is
refused at one of two steps:

| Step | Refused | Code |
| --- | --- | --- |
| receive buffer | an 11-bit identifier, a remote frame (RTR, remote transmission request), a DLC (data length code) above 8 | `mcp2515_recv_next()` in `shared/can/mcp2515.c` |
| link decode | an unknown op, a count past the end of a page, a DLC that disagrees with the count (a READ with a payload, a DATA whose payload is not 2 bytes a register) | `link_can_decode_counted()` in `shared/link/link_can.c` |

The CAN self-test frames are answered, not refused, and are not counted. A
READ that a panel on ESP-IDF v5.5.0 or v5.5.1 sends as DLC 8 instead of
DLC 0 is refused at the link decode and counted.

The count is a 32-bit unsigned integer. It is 0 at power-up and at every
restart of the IOmcu, nothing over the link clears it, and it wraps
from 4,294,967,295 to 0.

The panel reads STATUS every 500ms. It asks a 4.21 IOmcu for 10
registers and an older one for the 8 its page has: a 4.20 page refuses a
10-register read with BAD_RANGE, which would lose every STATUS counter. The
panel forgets the count at each link-up and takes it again from the first
STATUS read after it.

MOTOR & ESC shows the count in its header strip, after the poll rate and the
error count, while the link is up and the count is above 0: `Rejected link frames: 37`, in German `Abgelehnte Verbindungsframes: 37`. No other screen shows
it. The bring-up log line `iomcu  frames ... refused N` carries it as well.
`ui_link_refused_label()` in `shared/ui/ui_band.c` builds the label.

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| 4.21 | 4.21 | as above |
| 4.21 | 4.20 or older | the panel reads 8 STATUS registers; the count reads 0 and MOTOR & ESC shows no label |
| 4.20 or older | 4.21 | the panel reads 8 STATUS registers and does not see the count |

`test_mcp2515` and `test_link_can` hold the count at each step, a frame taken
that adds nothing, and the wrap at 2^32. `test_link_bringup` carries the count
from an IOmcu's page to the panel over CAN frames, including a refused
DLC 8 READ, 0x0001FFFF and the wrap, and reads a 4.20 page without it.
`test_text` holds the label: hidden at 0 and with the link down, shown above
0 in English and German. The screenshot `docs/img/motor-refused.png` shows a
count of 37.

### Firmware update over the link

From 4.22 the panel writes a new IOmcu image over CAN. The IOmcu
is an RP2350; its boot ROM holds two equal image slots behind a partition
table and starts a new image on trial, as TBYB (try before you buy). There is
no bootloader of the bench's own.

The IOmcu's flash (4MB module, RP2350A):

| Range | Size | Before 4.22 | From 4.22 |
| --- | ---: | --- | --- |
| 0x000000 to 0x001FFF | 8KiB | image | partition table |
| 0x002000 to 0x1FEFFF | 2036KiB | image | slot A (partition 0) |
| 0x1FF000 to 0x3FBFFF | 2036KiB | image space, unused | slot B (partition 1) |
| 0x3FC000 to 0x3FDFFF | 8KiB | unused | output store, data partition `store` (partition 2) |
| 0x3FE000 to 0x3FFFFF | 8KiB | output store | unused: read once for old bindings, never written |

A slot is 2,084,864 bytes, 509 sectors of 4KiB. The 0.18 image is about
192KB, 9% of a slot. The last sector stays unused because the RP2350-E10
workaround of an A2 chip's UF2 (USB Flashing Format) load can erase it.
`firmware/iomcu/partitions.json` is the table; the build passes its offsets
to the code, and the build fails when the image does not fit one slot.

The store is read by its storage address through the XIP (execute in place)
window that neither translates nor caches (0x1C000000 + offset): the boot ROM
maps the running slot to 0x10000000, so 0x10000000 + offset is an address in
the slot, not the store. A store with no record yet reads the old store at
0x3FE000 once, so the bindings survive the USB load that installs the table.
The first save goes to the new store.

An IOmcu without the table -- a build before 4.22, or a 4.22 image
loaded alone over USB onto a board without it -- runs as before and reports
no slots: the panel then says a USB load is needed. The one USB load that
installs the table is in [First run](FirstRun.md#the-iomcus-partition-table).

#### The pages

UPDATE_CTL (0xF0), 32 registers:

| Register | Name | Meaning |
| ---: | --- | --- |
| 0 | `VERSION` | the update registers' own version, 1; it grows when a register or command is added at the end |
| 1 | `STATE` | 0 idle, 1 hashing the running slot, 2 receiving, 3 received, 4 verifying, 5 verified, 6 failed, 7 activated |
| 2 | `ERROR` | why the last refusal: 0 none, 1 no slots, 2 gate shut, 3 size, 4 order, 5 CRC, 6 flash, 7 digest, 8 state, 9 an image on trial, 10 link silent, 11 BUY refused, 12 no such command |
| 3 | `SLOT` | the running slot in bits 0 to 3 (0A, 1B, 15 none); bit 8 the running image is on trial and waits for BUY; bit 9 the other slot's first sector waits for its erase |
| 4 | `SLOT_SECTORS` | 4KiB sectors in one slot, 509; 0 without a table |
| 5 | `NEXT_BLOCK` | the block COMMIT takes next |
| 6, 7 | `SIZE` | bytes of the session's image, or of the running slot hashed by HASH_RUNNING, low half first |
| 8 | `CMD` | a write is a command; a read gives the last one taken |
| 9 to 11 | `ARG0` to `ARG2` | the command's arguments |
| 12 | `DIGEST_OF` | what `DIGEST` holds: 0 nothing, 1 the running slot, 2 the image written |
| 13 to 15 | | read 0 |
| 16 to 31 | `DIGEST` | a SHA-256 (Secure Hash Algorithm) digest, 32 bytes, low byte first in each register |

A command is one frame: a write of 1 to 4 registers that starts at register
8, the arguments after it. Any other write to UPDATE_CTL is refused with
READ_ONLY. A command that does not fit the state is refused with BAD_VALUE
and the reason in `ERROR`.

| Command | Value | Arguments | Does |
| --- | ---: | --- | --- |
| `HASH_RUNNING` | 1 | bytes, low and high half | SHA-256 of the running slot's first bytes, the TBYB flag read as clear; idle only |
| `BEGIN` | 2 | image bytes, low and high half | starts a session: 1 byte to one slot, erases the other slot's first sector |
| `COMMIT` | 3 | block number, CRC-16 | UPDATE_DATA is that block |
| `FINISH` | 4 | | compares the slot's SHA-256 with UPDATE_DATA registers 0 to 15 |
| `ACTIVATE` | 5 | | writes the first sector, reads it back and restarts into the slot |
| `ABORT` | 6 | | ends the session; the slot holds no image |
| `BUY` | 7 | | keeps the image on trial; acknowledged with nothing to do when none is |

UPDATE_DATA (0xF1) holds the 64 bytes of one block in 32 registers, low byte
first in each register. Nothing on either page is kept over a restart.

#### The transfer

1. The panel reads IDENTITY, then UPDATE_CTL. BAD_PAGE on UPDATE_CTL, or
   `SLOT` 15, ends here: the IOmcu takes images over USB only.
2. The panel hashes the image it carries, 8KiB a step, and sends BEGIN with
   its size.
3. For each 64-byte block, in order from block 0: 8 data frames into
   UPDATE_DATA, then COMMIT with the block number and its CRC-16 (cyclic
   redundancy check, CCITT polynomial 0x1021 seeded 0xFFFF, over the block's
   bytes). The last block is the image's remainder and its CRC covers those
   bytes only. Each frame waits for its acknowledgement; nothing else is in
   flight.
4. The IOmcu buffers 64 blocks and writes a 4KiB sector when the
   last of them commits. That COMMIT is answered only after the sector is
   erased and programmed, about 19.2ms.
5. FINISH: the IOmcu hashes the slot as written, with the first
   sector as it will be, 1KiB a pass, and compares the digest.
6. ACTIVATE: the IOmcu writes the first sector last, reads it back,
   answers, and 20ms later restarts as a flash update boot of the new slot.
7. The boot ROM checks the image's SHA-256 and starts it on trial under its
   16.7s watchdog. The panel waits 500ms, reads IDENTITY until the new
   image answers, and writes BUY. The boot ROM clears the TBYB flag in flash.
   The IOmcu then erases the first sector of the slot it came from, so
   the boot ROM has one image to choose at the next start. The BUY is
   acknowledged once the boot ROM took it. An erase that fails, or waits for
   the store's gate, is owed: `SLOT` bit 9 is set, the IOmcu tries
   again once a second while the gate is open, and a start that finds the
   other slot's first sector written outside a session or a trial owes the
   erase again. The panel shows nothing for it. An IOmcu without a
   partition table it writes has no other slot and erases nothing.

A block before `NEXT_BLOCK` is acknowledged and not written again: a COMMIT
whose acknowledgement was lost is sent again harmlessly. Any other block
number is refused with ORDER. A block that fails its CRC is refused and sent
again, up to 3 times. A slot written in part holds no image the boot ROM
starts: the first sector is erased at BEGIN and written at ACTIVATE.

The image the panel sends carries the TBYB flag. Each build's binary carries
none, so an image loaded over USB starts as a normal boot. The panel sets the
flag in its copy as it sends it. The boot ROM's picobin version of the image
is 1000 x major + minor and patch, so 0.18.0 is version 18.0.

#### Limits and failure

| Condition | IOmcu | Panel |
| --- | --- | --- |
| a session or an image on trial | refuses ARM with NOT_ARMED; SAFE is never refused | shows the progress screen, no band |
| store gate shut (bank driving, supply output on, servo programming session) | refuses BEGIN, COMMIT and ACTIVATE with BUSY | asks again, gives up after 10s |
| 5s without a request to UPDATE_CTL or UPDATE_DATA in a session; requests to other pages do not count | ends it with error 10 | starts again from block 0 |
| a session found at the check, left by this panel before a restart or a lost link | ended with ABORT | checks on: the check reads the state again and asks for the hash |
| ACTIVATED found at the check | restarts within 20ms | reads again until the restart, gives up after 10s |
| a restart mid-transfer | the session is gone | starts again from block 0, at most 3 times |
| no answer for 4s | | gives up: `no response` |
| SHA-256 mismatch at FINISH | state failed, error 7 | `firmware check failed`, the old image runs |
| no BUY within 16.7s of the restart | the boot ROM restarts into the old slot | gives up at 12s: `new firmware did not start` |
| hash of the running slot or the image | | gives up after 10s |

A failed or abandoned update leaves the old image running and the other slot
without an image. The next update writes it again from block 0.

#### Checking which build runs

Versions alone do not identify a development build. At link-up the panel
compares the SHA-256 of the image it carries, the TBYB flag read as clear,
with the IOmcu's HASH_RUNNING over the same number of bytes.

| Result | Panel |
| --- | --- |
| same | nothing |
| different | the alert `IOmcu runs a different build. Update it under SETUP > UPDATES.`; the question opens on the menu while the bench is SAFE, the supply output off, the panel's Wi-Fi radio wholly off and no panel install under way |
| an image on trial | writes BUY at once and says `New IOmcu firmware kept.` |
| BAD_PAGE or no slots | the alert `IOmcu cannot update over CAN. Install firmware once via USB.` |

The check runs at a protocol major mismatch as well: the identity registers
and the update pages answer across majors, and the update moves the
IOmcu to the panel's own major. A CANCEL is kept for that IOmcu
version until the panel restarts. An older carried image is offered too; the
question then says so. [Screens](Screens.md#iomcu-update) shows the
question and the progress.

A panel built without `RCBENCH_IOMCU_BIN` carries no image, runs no check and
offers nothing.

#### Time

For the 0.18 image, 191,592 bytes: 2,994 blocks, 9 exchanges a block,
26,946 exchanges and 47 sectors. A frame takes 131 to 160µs at 1Mbit/s,
so the bus time of request and acknowledgement is 7.1 to 8.6s, and the
sector writes add 0.9s. With the 334 to 1400µs round trip quoted under
[Budget](#budget) the exchanges take 9.0 to 37.7s. The whole update, the
IOmcu's hashes and its restart are not measured.

The host suite's simulated IOmcu runs a full-size update of the 0.18.0
image (184,872 bytes, 2,889 blocks) in 26,104 exchanges.

#### Update compatibility

| Panel | IOmcu | Behaviour |
| --- | --- | --- |
| 4.22 with an image | 4.21 or older | BAD_PAGE: the USB alert; the link runs as before |
| 4.22 with an image | 4.22, no table | no slots: the USB alert |
| 4.22 with an image | 4.22 or later minor, table | check, offer, update |
| 4.22 with an image | another major, 4.22 or later | the link stays down; check, offer, update over the frozen pages |
| 4.21 or older | 4.22 | the update pages are never asked; the IOmcu runs as a 4.21 one |

`test_link_update` runs the IOmcu's pages and the panel's engine over
the link layer against a simulated flash and boot ROM: block 0, the last
partial block, an image of exactly one slot and one byte over, a CRC error,
blocks out of order and repeated, a restart mid-transfer, NACKs, a link drop,
ARM during the session, a shut gate, a digest mismatch, a missing BUY, a
4.21 IOmcu's BAD_PAGE, and another minor and major. It also runs a
panel restart that leaves the session in each of RECEIVING, RECEIVED,
VERIFYING and VERIFIED with the ordinary poll running, a session that only
the ordinary poll talks to, a check that finds the restart due or a restart
that never comes, an ABORT at the check lost, refused or not taking, and an
erase after BUY that fails or waits for the gate, across a restart.
`test_link_sha256` holds the hash against the FIPS 180-4 vectors.
`test_link_pages` holds the frozen register numbers.

### Bit timing

`shared/can/can_timing.c` solves the segmentation for each controller's clock
and requires the bit rate to come out exact. Both ends sample at 75% of the
bit:

| Controller | Clock | Prescaler | Quanta per bit | tseg1 | tseg2 | sjw |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| TWAI (ESP32-P4) | 40MHz crystal | 2 | 20 | 14 | 5 | 4 |
| XL2515 | 16MHz crystal, halved internally | 1 | 8 | 5 | 2 | 2 |

Eight quanta is the minimum a bit may have. That is why the XL2515 needs a 16
MHz crystal for 1Mbit/s and why its sample point is fixed at 75%; the TWAI
timing is chosen to match. `test_can_timing` pins the XL2515 row. For the TWAI it checks
the 75% sample point and a jump width of at least 2, not the values of the row.

### Budget

A bench-page poll is one request frame and four data frames (13 registers). At
20Hz that is 1.55% of the bus. While a current monitor is enabled, the panel
reads SENSE's registers 12 to 25 (14 registers, one request and four data
frames) at 20Hz, about 1.6%, with the output encoder enabled registers 12 to 31
(20 registers, one request and five data frames), about 1.9%, and while the
INA3221 is, SERVO_SENSE's
registers 0 to 13 at 20Hz, every 50ms window, about 1.6%: about 3.2%
together.

On a 4.11 IOmcu with the INA3221 enabled on CH1 the panel also reads
SERVO_WIN's registers 0 to 15 at 20Hz (16 registers, one request and four
data frames, 0.78ms of bus time a poll, about 1.6%), and SENSE's read goes
to register 31 for `RESETS` (one data frame more, 0.16ms, about 0.3%). The
three reads are then 16 frames a poll, 2.5ms, about 5.0% of the bus,
against 10 frames and 3.1% without the ring. A poll that starts 100ms or
more after the last read that left no window owed reads the whole page
instead, 28 registers, one request and seven data frames: 19 frames,
2.9ms. A read of registers 0 to 15 followed by the whole page, after a
ring that started again, is 24 frames, 3.7ms, once. With the BENCH read
and the TONE read a poll's reads are at most 34 frames, 5.3ms of bus time
in the 50ms poll, and SERVO_WIN is one exchange more in it. The ring holds
200ms, so the windows survive a poll that takes four times its period.
`test_sense_windows` holds the frame counts. The time one exchange takes on
the bench is not measured.

While the tap is enabled, a read of TONE's registers 8 to 23 (16
registers, one request and four data frames, as the BENCH read) at 20Hz is
about 1.7% more, under 2%, and 4.9% for the three pages together, and a read of EVT_SEL
and registers 14 to 21 for each new beep is a write frame, a request and two
data frames. Each read is one
more exchange in the 50ms poll. Worst-case classic CAN payload at 1Mbit/s with
29-bit identifiers and full bit stuffing is about 52kB/s; the expected traffic
is 12 to 30kB/s.

### Tests

The identifier layout is checked bit by bit across the 29-bit space; the timing
solver is pinned to hand-checked examples; `test_link_loopback` runs the host
poller against the device dispatcher over a bus that drops, delays and reorders
frames: split replies arriving in reverse, refused writes, lost pieces leaving
a request unanswered rather than half-answered, and the device watchdog firing
on a quiet bus. `test_bind_link` runs a binding's write and read sequences
against the IOmcu's page rules over a bus with the XL2515's 2-frame
buffer: see [A binding taken whole](#a-binding-taken-whole).
