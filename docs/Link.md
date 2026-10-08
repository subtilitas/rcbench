# The link

<sub>**English** · [Deutsch](Link-de.md)</sub>

The two boards communicate over CAN (Controller Area Network) at 1 Mbit/s. Wiring first, then the
protocol reference.

## Wiring

| | |
| --- | --- |
| Bus | classic CAN without FD (flexible data rate), 1 Mbit/s, 29-bit identifiers only |
| Panel | TWAI controller on GPIO19 (RX) and GPIO20 (TX), through the board's USB (Universal Serial Bus)/CAN multiplexer |
| Coprocessor | XL2515 (MCP2515-compatible) on `spi1`: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8; SPI (Serial Peripheral Interface) clock 10 MHz |
| Transceiver | SIT65HVD230 (3.3 V) on the coprocessor module |
| Termination | 120 Ω at both ends |
| Crystal | the XL2515 must have a 16 MHz crystal; 8 MHz limits the controller to 500 kbit/s |

Selecting CAN removes the panel's native USB. GPIO19 and GPIO20 carry both, and
the FSUSB42UMX multiplexer (CH422G EXIO5: 0 = USB, 1 = CAN) selects one. The
console is therefore on UART0, the board's second USB-C socket behind the
USB-UART bridge, with USB-Serial-JTAG (the ESP32-S3's built-in USB serial and
debug bridge) as secondary.

If frames do not cross: [Bringing up the link](Bringup.md).

## Failure behaviour

The coprocessor fills failsafe values after 200 ms without a request; the panel
escalates after 1 s without an answer. The failsafe latches. Traffic returning
stops the silence counter but does not lift the failsafe; leaving it takes a
write of 0x5AFE to the CLEAR register of the control page.

### Bus off

A CAN transmitter that gets no acknowledgement retransmits on its own and adds
8 to its transmit error counter each attempt. At 1 Mbit/s a frame nobody
answers reaches the bus-off threshold of 256 in about 4 ms, so any moment with
no second node on the bus — the coprocessor unpowered, reset, or held with
interrupts off long enough — takes the panel's controller off the bus.

The ESP32-S3's TWAI (Two-Wire Automotive Interface) controller does not return
on its own. `can_twai_recover()` runs on the identity poll while the link is
down, once per second: it calls `twai_initiate_recovery()` from the bus-off
state, waits out the 128 bus-free signals the controller counts in the
recovering state, and starts the controller again from the stopped state.
Recovery therefore takes up to three polls, about 3 s. Without it the first
quiet moment on the bus is permanent and only a power cycle clears it.

The report printed every 5 s while the link is down carries the counters:

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
coprocessor transmits only in answer to a request. Protocol version 4.7. The
major version is register 0 of page 0. The major moves when a register
changes meaning or a page is renumbered; the minor moves when a page or a
register is added at the end, which an older panel can ignore.

A coprocessor reporting a protocol major other than the panel's is treated as
absent: the link stays down, the splash screen marks the coprocessor step
failed beside the version it reported, the panel logs both majors and raises
the alert `protocol mismatch -- will not arm`, and no write reaches the
coprocessor. Its outputs stay off, and the panel runs as it does with no
coprocessor connected. A panel and a coprocessor on different sides of a
major bump are therefore flashed together.

### Compatibility

Only the major is compared. The link comes up and the bench arms whatever
the two minors are. The panel reads the coprocessor's minor at link-up and
uses nothing that minor does not have: SERVO's frame rate from 4.1, its
sweep from 4.2, SUPPLY from 4.3, the sweep's RESUME from 4.6, SENSE and
SERVO_SENSE from 4.7. The coprocessor never reads the panel's minor; a page
an older panel does not know is a page it never writes.

| Panel | Coprocessor | Link and arming | SENSE (0x2B) and SERVO_SENSE (0x2C) |
| --- | --- | --- | --- |
| 4.7 | 4.7 | up, arms | served |
| 4.7 | 4.6 | up, arms | the panel sends nothing to either page; the coprocessor answers both with BAD_PAGE. BENCH bits 5 and 6 read 0 |
| 4.6 | 4.7 | up, arms | the panel neither reads nor writes either page and ignores BENCH bits 5 and 6. A set-up kept in the coprocessor's flash from a 4.7 panel stays in force and its two pins stay held from the outputs. Once the coprocessor reads an INA228, BENCH carries its voltage and current at the same scales, and the panel counts charge and energy from that current itself |

This build's coprocessor serves both pages, keeps the set-up in flash and
holds its pins. Its core 1 reads the enabled parts on a 1 ms tick, and core
0 publishes what core 1 read into both pages; the read-only registers read
0 until core 1 has read under the set-up in force, about 1 ms after a
change. With the INA228 answering, BENCH carries its voltage and current
(the last 50 ms window's means), their product as power, the run's peaks
from its 500 Hz samples, and its charge and energy, with bits 5 and 6. A
run that starts with the INA228 answering keeps it as BENCH's source to the
run's end: if it stops answering, those fields go empty rather than back to
the ESC's telemetry. A capture's level before the command is CH1's mean
over the 50 ms before the edge; an arm while the INA3221 is not reading CH1
ends at once as lost and counts in CAP_SEQ. None of this has run on
hardware. This build's panel writes neither page.

### Identifier

A 29-bit extended identifier carries the whole address, so a read is a frame
with no payload.

| Bits | Field | Width | Values |
| --- | --- | ---: | --- |
| 28..26 | priority | 3 | 0 control (CONTROL, LIMITS, FAILSAFE and SUPPLY pages, their acknowledgements included), 1 normal (SENSE and SERVO_SENSE among them), 2 bulk (reserved); lower wins arbitration |
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

A write is all or nothing. Every register of a frame is checked before any of
them is stored, so a NACK leaves the page exactly as it was: none of the up to
four registers a frame carries is kept, and no side effect of one of them runs.
Clearing a latched failsafe is such a side effect.

### Page map

| Page | Name | Access | Registers |
| ---: | --- | --- | --- |
| 0x00 | IDENTITY | read | protocol major, protocol minor, firmware major, minor, patch, hardware revision, capabilities bitmap |
| 0x01 | STATUS | read | state (0 idle, 1 armed, 2 failsafe), faults bitmap, uptime in ms (two registers), requests accepted (two registers), XL2515 receive error counter, XL2515 transmit error counter |
| 0x10 | CONTROL | read, write | ARM (non-zero arms), THROTTLE (0..10000, hundredths of a percent, and it commands every channel CHAN_CFG marks a throttle), MOTOR_POLES, CLEAR (write 0x5AFE to leave failsafe). Registers 0 to 2 are the frame that arms |
| 0x11 | LIMITS | | declared, not served |
| 0x12 | FAILSAFE | | declared, not served |
| 0x13 | CHANNELS | read, write | one command per output channel, 0..1000 of the channel's travel; eight channels |
| 0x20 | BENCH | read | voltage (10 mV), current (10 mA), power (W), rpm (revolutions per minute), ESC (electronic speed controller) temperature and motor temperature (0.1 °C, signed), charge (mAh), energy (0.1 Wh), minimum voltage, maximum current, maximum power, maximum rpm, flags |
| 0x21 | reserved | | not assigned; not to be reused |
| 0x22 | OUTPUTS | read, write | per slot: driver (0 none, 1 PWM (pulse-width modulation), 2 PPM (pulse-position modulation), 3 DShot, 4 bidirectional DShot), pin, first channel and channel count in one register, rate in Hz (kbit/s for both DShot drivers); eight slots of four registers |
| 0x23 | CHAN_CFG | read, write | per channel: role (0 throttle, 1 surface), slew (span per second, 0 = immediate), minimum and maximum pulse in µs; eight channels of four registers |
| 0x24 | CATALOGUE | read | the board's own pins, one register each: GPIO (general-purpose input/output) number in 6 bits, the pad number printed beside it in 6, what holds it in 4 (0 free, 1 heartbeat, 2 CAN, 3 flash, 4 debug, 5 sensor, 15 other); 32 slots, and a pad number of 0 means there is no pin in that slot |
| 0x25 | SHAPE | read | where those pads are: outline width and height in 0.01 mm, the corner pad 1 sits at and the pads in one row packed as (corner << 8) \| per side, the pitch in 0.01 mm, and the distance from the edge to the centre of a pad row. Two rows on one pitch, numbered away from pad 1 along its edge and back along the opposite one. All zero when the coprocessor has no shape for its board, which means it is listed and not drawn |
| 0x26 | ARTWORK | read | what a picture of the board is: blocks of payload (0 when the coprocessor carries none), width and height in pixels, format (0 none, 1 RGB565 with the low byte first), payload length in two registers, and a CRC (cyclic redundancy check) over the whole payload seeded zero |
| 0x27 | ART_DATA | read, write | the picture itself: write register 0 to say which block, then read the page. Register 0 reads back the block being served and registers 1 to 31 carry 62 bytes of it. A block does not advance on being read, so a reply that goes missing is asked for again rather than skipped |
| 0x28 | PADS | read | the pads that are not pins, one register each: the pad number in 6 bits, what it is in 2 (0 no pad and the list ends, 1 ground, 2 a rail, 3 neither), and the rail in 8 bits of tenths of a volt. Zero volts on a rail means it is not a fixed voltage, which is not the same as a ground's 0 V. 32 slots, in pad order |
| 0x29 | SERVO | read, write | register 0: the frame rate in Hz of every PWM output whose first channel is a surface, 40 to 560, or 0 for each slot's own rate from OUTPUTS; refused with BAD_VALUE, and nothing changes, when it would leave a PWM slice asked for two rates, and while it is not 0 so is a CHAN_CFG or OUTPUTS write that would (since 4.1). Registers 1 to 4, one frame: a sweep of the surfaces -- curve (0 stopped, 1 square, 2 sine, 3 triangle), speed in thousandths of a cycle a second (50 to 5000), amplitude in command units either side of the centre (0 to 500), hold at each end in ms (0 to 5000); a sweep starts or changes only from all four written together, refused with BAD_VALUE otherwise, and with NOT_ARMED on a disarmed bench; 0 in register 1 alone stops it, and 4 alone holds every surface where its output is, under the same disarm and 500 ms rules. A sweep running when the hold begins keeps its phase (since 4.6): how far into the curve it was, and so its point, its dwell and the ends reached; one that has made its movements is held at the centre it ended on. A write is judged against the page as the coprocessor's next pass would leave it at that moment: a sweep past its last movement, unwritten for 500 ms or disarmed has ended, even when the write is served before the pass that ends it. 5 alone (RESUME, since 4.6) carries that sweep on from the kept phase; the surfaces are commanded along the curve again at once and slew there from where they were held, at their own rate. RESUME is refused with BAD_VALUE when no phase is kept -- no sweep was running when the hold began, or the hold ended by a write of 0, a curve written over it, a disarm, 500 ms unwritten or a restart -- and when registers 2 to 5 no longer read what the paused sweep runs, and with NOT_ARMED on a disarmed bench. A 4.5 coprocessor refuses 5 with BAD_VALUE. Register 5: the ends a sweep started after it reaches before it stops, 0 for no end. Register 6, read only: the ends the sweep has reached. A sweep stops on a write of 0, on a disarm and when it has not been written for 500 ms, leaving each surface where its output has got to; repeating it keeps its curve going, and a sweep that has made its movements is not started again by a repeat (since 4.2). Nothing is kept: a coprocessor restart reads 0 throughout |
| 0x2A | SUPPLY | read, write | a WeAct PD Power Mini V1 Buck on a PIO (programmable input/output) UART on two of the coprocessor's pins (since 4.3). Registers 0 to 3, one frame: enable, the GPIO that transmits (to the module's DM) and the one that receives (its DP), and the module's UART Baudrate setting, 0 to 6 for 9600, 19200, 38400, 57600, 115200, 230400 and 460800 baud, or 7 (since 4.4) to find it: the UART tries the next rate after every WHO_AM_I without a valid answer, starting at 19200, until a module has answered once; refused with BAD_VALUE on a pin that is reserved, bound to an output or the other pin, and for any change while the output is asked on or may be on (bit 6 of the flags). Since 4.5, a change while a module has answered is acknowledged and held: registers 0 to 3 read the wiring in force and flag bit 8 is set until a state read of the module sent after the change answers, at most about 1.2 s; a read showing the output off takes the change, one showing it on or failing refuses it, and bit 9 is set until the next write of registers 0 to 3. Such a change comes alone, without registers 4 to 6, and an ON is refused with BAD_VALUE while one is held. A host older than 4.5 that writes the wiring again while it is held is acknowledged and the hold goes on. The pins are no output's while held. Registers 4 to 6, one frame: output (1 on) and the set points in mV and mA, up to 20000 mV and 3000 mA; an ON is refused with NOT_ARMED without a live heartbeat, and while wiring just written is not yet in flash (a save is taken 400 ms after the last write), and the output goes off when the heartbeat stops. Registers 7 to 16, read only: flags (bit 0 online, bit 1 output on, bits 3..2 mode 0 normal, 1 constant current, 2 overcurrent, bit 4 an output that would not switch, bit 5 set points that would not take, bit 6 an output on or possibly on -- read on, an ON not yet confirmed, or an OFF owed to a module that stopped answering, bit 7 an output the module switched off by itself while ON was asked, held off until OUTPUT is written 0, since 4.5 bit 8 a wiring change held for a state read and bit 9 one that read refused, bit 10 an output switched off because the module's input read under the set point plus 500 mV on 2 input reads in a row, held off until OUTPUT is written 0), the output's mV and mA, the set points read back, the input's state and mV, readings taken and transactions failed modulo 65536, and (since 4.4) the rate in use, 0 to 6, or 7 while AUTO has found none. Register 17 (since 4.4), written alone as 1 with the output asked off and the supply enabled, restarts the module (SYSTEM_RESET) once it has answered and its output reads off; it reads 0. The wiring is kept in the coprocessor's flash and driven at boot with the output off, so a module left on is switched off after a restart; the command is not kept |
| 0x2B | SENSE | read, write | two I2C (Inter-Integrated Circuit) current monitors on one bus on two of the coprocessor's pins, a TI INA228 in the ESC's power path and a TI INA3221 on the servo rail (since 4.7). Registers 0 to 3, one frame: enable (bit 0 INA228, bit 1 INA3221), the SDA GPIO, the SCL GPIO, and the clock, 400 kHz and no other value: at 100 kHz one read takes 480 to 750 µs and the coprocessor's 1 ms schedule does not fit. The schedule reads INA3221 CH1 at 1000 Hz, CH2 and CH3 at 50 Hz or at 1000 Hz for a synchronised pair, the INA228's current and voltage at 500 Hz each, and everything else at 50 Hz. Both modules carry pull-ups on SDA and SCL, and they add in parallel: the DAOKAI INA3221 has 10 kΩ to its VS (3.3 V); the MATEK INA228's pull-up value and rail are unknown. The combined value must stay above about 1 kΩ: an I2C output sinks 3 mA at 0.4 V, and (3.3 V − 0.4 V) / 3 mA is 967 Ω. Below 10 kΩ it shortens the rise time: the 300 ns rise limit at 400 kHz allows 35 pF of bus with 10 kΩ alone, and more with less. SDA's GPIO number mod 4 is 0 or 2 and SCL is the GPIO after it, one I2C block's pair (GP16 and GP17 by default). Registers 4 to 7, one frame: the INA228's address, 0x40 to 0x4F (default 0x45, the MATEK I2C-INA-BM's as shipped; its solder bridges give 0x44 or 0x41); its shunt in µΩ, 50 to 20000 (default 200); the current its range is set for in 0.1 A, 10 to 6553 (1.0 to 655.3 A, the BENCH current register's reach; default 2048). The maximum chooses ADCRANGE only: 1 while the shunt's voltage at it is at most 40.96 mV, 0 up to 163.84 mV, and above that the write is refused. CURRENT_LSB is the shunt ADC's step divided by the shunt (78.125 nV or 312.5 nV over R), so CURRENT and the shunt voltage clip together, and SHUNT_CAL is 4096 at either range. A shunt whose full scale at the chosen range passes 2000 A is refused as well, so below 81.92 µΩ only ADCRANGE 1 is taken; the rule is the driver's, `ina228_calibrate()`; register 7 reserved. Registers 8 to 11, one frame: the INA3221's address, 0x40 to 0x43 (default 0x40); its shunt in 0.1 mΩ, 50 to 10000 (5 mΩ to 1 Ω, default 1000, the 0.1 Ω that reads to 1.638 A); the channels read, bits 0..2 for CH1 to CH3, at least one while enabled (default CH1); register 11 reserved. The reserved registers read 0 and take only 0. Refused with BAD_VALUE: a value out of range, SDA and SCL not one block's pair, a pin that is reserved, bound to an output or held by SUPPLY, both parts on one address while both are enabled, and any change while the bank is armed; a write of the set-up in force is taken. The pins are no output's while either part is enabled, and an OUTPUTS write binding one is refused. Registers 12 to 25, read only: flags (bit 0 INA228 online, bit 1 its last identity read was an INA228's, bit 2 something else answers at its address, bit 3 a current read at the end of its range in the last 50 ms window or since the run's arm, so BENCH's current and power, or their peaks, are bounds and not values; bits 4 to 6 the same three for the INA3221; bit 8 the bus is open on its pins, bit 9 SDA is held low and being clocked free), the addresses that answered the last scan (bit n for 0x40 + n), the INA228's DEVICE_ID and the INA3221's die ID as read, transactions failed modulo 65536, the INA228's die temperature in 0.1 °C (signed) and DIAG_ALRT, its charge in 0.01 mAh (signed, 32 bit, registers 19 and 20, low first) and energy in 0.01 Wh (32 bit, registers 21 and 22, low first) since the run's arm, the ESC's own telemetry voltage (10 mV) and current (10 mA), and their valid bits (bit 0 voltage, bit 1 current). Registers 0 to 11 are kept in the coprocessor's flash |
| 0x2C | SERVO_SENSE | read, write | the INA3221's channels and a move timed on the coprocessor's clock (since 4.7). Registers 0 to 11, read only, four a channel from CH1: mean current (mA, signed), highest current (mA, signed), mean bus voltage (mV) and lowest bus voltage (mV) over the last 50 ms window, the voltage at the load side of the shunt. Register 12, read only: the window number modulo 65536; a read does not end a window. Register 13, read only: bits 0..2 a channel's window holds readings, bits 4..6 one of them read the top of the range (163.8 mV across the shunt), which makes that channel's mean and highest current lower bounds, bit 7 the same of the capture. Registers 14 to 17, one frame: a capture -- bit 7 set, the INA3221 channel in bits 0..1, CH1 only (2 and 3 are refused; the field stays for a channel read fast enough later), and the output channel (0 to 7) in bits 8..10 whose next changed command starts the timing; the holding level the move ends at, 0 to 32767 mA; the movement threshold and the arrival band, each 1 to 32767 mA. An arm is the whole frame and restarts a capture already running. 0 in register 14 disarms and is never refused; written at the head of the frame, the other three are not stored. Refused with BAD_VALUE: any other write that is not the whole frame, other bits set in register 14, a value out of range, a channel other than CH1 or one SENSE does not read, and an output channel that is not a surface on a PWM slot; with NOT_ARMED on a disarmed bank. A bank that stops driving ends a capture that has not finished. Registers 18 to 24, read only: the state (0 idle, 1 armed, 2 waiting for movement, 3 moving, 4 arrived, 5 settled on an end stop, 6 late: movement and no arrival within 3000 ms plus the meter's lag, 7 unseen: no movement in that time, 8 lost: the INA3221 stopped answering), captures finished modulo 65536, the time from the PWM frame carrying the new pulse to movement and to arrival in 0.1 ms, resolved to CH1's 1 ms sample interval, the highest and mean filtered current of the move (mA, signed), and the samples in it. Nothing is kept: a coprocessor restart reads 0 throughout |

Faults bitmap: bit 0 link silent, bit 1 overcurrent, bit 2 over-temperature,
bit 3 stall, bit 4 heartbeat stopped, bit 5 protocol version mismatch. Faults
are sticky until read and cleared. Bit 0 needs the link to have carried at
least one request before it can be set: the coprocessor is awake before the
panel polls, and the wait for the first one is not silence.

Capabilities bitmap: bit 0 ESC drive, bit 1 ESC telemetry, bit 2 servo PWM, bit
3 servo current sense, bit 4 pack sense, bit 5 receiver bus, bit 6 vibration
sensor and index pulse, bit 7 cell monitor, bit 8 programming. The panel
derives the menu marks from it.

BENCH flags: bit 0 voltage valid, bit 1 current valid, bit 2 rpm valid, bit 3
the ESC's temperature valid, bit 4 the motor's temperature valid, bit 5 the
voltage, current and power are the INA228's and not the ESC's telemetry
(since 4.7), bit 6 charge and energy are the INA228's accumulators, cleared
at this run's arm, with the part answering throughout (since 4.7), bit 7
simulated. Without bit 6 a panel counts charge and energy itself from the
current; this build's panel always does. The two temperatures carry
separate bits because they come from
different places and one of them usually does not come at all: an ESC reports
its own temperature over extended DShot telemetry and knows nothing about the
motor it drives. Bit 3 validated both until protocol 3.0, which is why that
change is a major. A coprocessor without a measurement front
end sets bit 7, and the panel draws SIMULATION across the screen.

MOTOR_POLES is the magnet count of the motor under test, even and between 2 and
42, or zero meaning nobody has said. A bidirectional DShot ESC reports
electrical periods and has no idea what it is bolted to, so this is the one
number the wire has to carry for the coprocessor to report a mechanical speed;
at zero it reports no speed rather than one derived from a guess. The panel
sends it from the `Motor poles` setting when a coprocessor starts answering,
again whenever the setting changes, and in the frame that arms whether or not
a change is owed. A write
nobody answers stays owed and goes out again at the next 50 ms poll; a write
the coprocessor refuses is not retried, because the same request refused once
is refused every time, and it waits for the next edit or link-up edge instead.
The guard at zero covers a count never sent and not one that is out of date:
every count the setting allows is inside the range the page takes.

The coprocessor refuses a pin it must not drive -- the safety line, the CAN
controller's pins, GP23, GP24, GP25 and GP29 (used by the module, not brought
out) and any number above GP29 -- on the OUTPUTS and SUPPLY pages alike, and a
slot it refuses is left unbound while the page still reads back what was asked
for. [DShot and the output drivers](DShot.md) has the rest.

CHAN_CFG and OUTPUTS entries are written whole, four registers at a time. A
channel's pulse range defaults to 1000..2000 µs; endpoints outside 400..2500 µs
are refused with BAD_VALUE. A command outside its range is clamped. Two slots
on one pin, or two slots rendering the same channel, are refused. Arming is
decided by the coprocessor: a write of ARM is refused with NOT_ARMED while the
link is in failsafe or the heartbeat is not trusted.

An arm from the panel is two transactions. CLEAR travels first and alone: the
coprocessor checks ARM against its failsafe before it applies a CLEAR from the
same frame, so a frame carrying both is refused with NOT_ARMED whenever the
clear was needed. Then ARM, THROTTLE and MOTOR_POLES go as one three-register
frame from offset 0, so the coprocessor starts the run on the pole count the
panel sent or does not start it. Every 50 ms poll after that writes ARM and
THROTTLE; a pole count edited during a run goes on a write of its own at the
next poll. The rules of the page -- the throttle range, the pole count, the
CLEAR magic, ARM refused in failsafe, and a refusal storing nothing -- are
`shared/link/link_control.c`, under `test_link_pages`.

### Bit timing

`shared/can/can_timing.c` solves the segmentation for each controller's clock
and requires the bit rate to come out exact. Both ends sample at 75% of the
bit:

| Controller | Clock | Prescaler | Quanta per bit | tseg1 | tseg2 | sjw |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| TWAI (ESP32-S3) | 80 MHz APB (advanced peripheral bus) | 4 | 20 | 14 | 5 | 4 |
| XL2515 | 16 MHz crystal, halved internally | 1 | 8 | 5 | 2 | 2 |

Eight quanta is the minimum a bit may have. That is why the XL2515 needs a 16
MHz crystal for 1 Mbit/s and why its sample point is fixed at 75%; the TWAI
timing is chosen to match. `test_can_timing` pins both.

### Budget

A bench-page poll is one request frame and four data frames (13 registers). At
20 Hz that is 1.55% of the bus. A read of SENSE's registers 12 to 25 and of
all 25 SERVO_SENSE registers is two request frames and eleven data frames,
about 2.5 times the bits of a bench-page poll: about 3.9% of the bus at
20 Hz. Worst-case classic CAN payload at 1 Mbit/s with
29-bit identifiers and full bit stuffing is about 52 kB/s; the expected traffic
is 12 to 30 kB/s.

### Tests

The identifier layout is checked bit by bit across the 29-bit space; the timing
solver is pinned to hand-checked examples; `test_link_loopback` runs the host
poller against the device dispatcher over a bus that drops, delays and reorders
frames: split replies arriving in reverse, refused writes, lost pieces leaving
a request unanswered rather than half-answered, and the device watchdog firing
on a quiet bus.
