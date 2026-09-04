# DShot and the output drivers

<sub>**English** · [Deutsch](DShot-de.md)</sub>

What the coprocessor puts on an output pin, and what comes back. Four drivers:
servo PWM (pulse-width modulation), PPM (pulse-position modulation), DShot, and
bidirectional DShot.

The rules every output obeys — travel, role, rest, slew, clamping, arming and
the silence timeout — are in `shared/outputs/` and are the same for all four.
This page is about the wire.

## Which board

More than one coprocessor will exist. The one the bring-up runs on is a
Pico-form-factor module wired by hand; a later board carries its outputs on
pins that are soldered rather than chosen. They do not have the same pins, and
a pin index on one means a different pin -- or no pin -- on the other.

The coprocessor therefore says which board it is, in the hardware register of
the [identity page](Link.md#page-map), and the panel offers that board's pins
and no other. A board this build does not know offers nothing: guessing a pin
map is how an output ends up on the safety line. Changing board clears any
selection for the same reason.

A board whose outputs are soldered is shown rather than offered. The
coprocessor configures itself and the panel displays what it reads back
without letting it be edited, because a screen that re-bound pins which are
not on connectors would be offering a choice the board cannot honour.

## Which pin

On a board that is wired by hand, no output pin is fixed in the firmware. A slot on the [OUTPUTS
page](Link.md#page-map) carries its pin number, so the pin is a setting rather
than a build.

The coprocessor refuses a pin it must not drive: GP3 (the safety heartbeat),
GP8 to GP12 (the CAN (Controller Area Network) controller's SPI (Serial
Peripheral Interface) and interrupt), and any number above the last GPIO
(general-purpose input/output) the part has — 29 on the RP2350A the bring-up
module carries, 47 on the RP2350B the final board needs. A refused slot is left
unbound. The page still reads back what was asked for, so a slot that is not
driving is visible as a disagreement between the page and the output.

A PIO (programmable input/output) block addresses 32 pins from a base of 0 or
16, fixed while the block holds a program. Which block can reach a given pin is
therefore not known until the pin arrives, and the block is chosen when the
slot is bound. A pin no free block can reach is refused like any other.

## Servo PWM

Hardware PWM, not PIO: the part has 12 slices and 24 channels, and a servo
pulse is exactly what a slice does. The counter runs at 1 MHz, so a pulse is a
count of microseconds and the frame period is 1,000,000 divided by the rate.

| | |
| --- | --- |
| Frame rate | 40 to 400 Hz |
| Pulse | 500 to 2500 µs, refused outside |
| Resolution | 1 µs |

A slice is two channels sharing one counter, so two pins on the same slice run
at the same frame rate. A second binding that asks for a different rate on a
slice already in use is refused rather than retimed, because retiming would
move an output that nobody touched.

## PPM

One pin, up to eight channels. A frame is a run of 300 µs marks; the time from
the start of one mark to the start of the next is one channel's pulse width.
After the last channel comes a terminating mark and then the sync gap, and that
gap is what tells a receiver where the next frame starts.

Channels and frame rate are not independent. Eight channels at 2000 µs already
spend 16 ms of a 22.5 ms frame. The bench refuses a frame whose sync gap would
fall below 3000 µs, which is the shortest gap no receiver can read as a
channel: the channel ceiling is 2500 µs.

The shortest frame that carries n channels whatever they are commanded to is

    n × 2500 µs + 300 µs + 3000 µs

which is 23.3 ms for eight channels — so eight channels at 50 Hz is refused and
eight at 40 Hz is not. The refusal happens when the slot is configured, not when
the sticks reach the end of their travel.

Polarity is a pad inversion, not a different program.

The frame plays from a pair of DMA (direct memory access) channels that
retrigger each other, so the processor is not in the timing path. The frame
buffer is rewritten in place while it is being played: a channel updated
mid-frame takes effect on the next frame, and the rest of that frame carries the
previous values.

## DShot

Sixteen bits: eleven of value, one asking for telemetry on the separate serial
wire, and four of checksum. Most significant bit first. There is no start bit
and no idle pattern — the bit period is the whole of the synchronisation, so
both ends agree on the rate by configuration.

| Bit | High for |
| --- | --- |
| 0 | 37.5% of the bit period |
| 1 | 75% |

The state machine runs at eight times the bit rate, because eight is the
smallest number of cycles that makes both those fractions whole. DShot600 is
therefore 4.8 MHz.

The value is not a throttle across its whole range:

| Value | Meaning |
| ---: | --- |
| 0 | motor stop |
| 1 to 47 | commands: beeps, direction, 3D mode, save settings, extended telemetry |
| 48 to 2047 | throttle |

A command has to be repeated ten times before an ESC (electronic speed
controller) acts on it; sent once it does nothing, which looks exactly like a
driver that is not working.

The bench maps travel so that a command of zero is motor stop and everything
above it lands in 48 to 2047. A driver that mapped travel straight onto 0 to
2047 would send beeps and direction changes on its way up from idle.

Frames go out at 1 kHz. That rate is not on the wire: the OUTPUTS page's rate
field is a bit rate for DShot rather than a frame rate, so the bench chooses it.
1 kHz is twenty times the panel's poll rate and well inside every ESC's own
signal timeout.

**Nothing is sent while the bench is not driving.** No frames means no edges,
and an ESC stops on silence after its own timeout. Sending explicit zeros
instead would keep it armed and waiting, which is not what a disarmed bench
should look like from the ESC's side.

## Bidirectional DShot

The same frame with the line inverted and the checksum complemented, so an ESC
set up for one protocol ignores the other rather than acting on it. It is a
separate driver number on the [OUTPUTS page](Link.md#page-map), not a flag: the
two are different wires, not one wire with a setting.

After each frame the coprocessor releases the line and the ESC answers on it,
about 30 µs later, with 21 bits at five quarters of the DShot rate. The
turnaround happens inside the PIO block — the transmitter releases the pin and
raises a flag, and the receiver starts on that flag — because 30 µs is not a
deadline a loop can meet.

The reply is group-coded. Each nibble of a 16-bit value becomes a five-bit code
word chosen so the line changes level often, and the line is differential: each
bit is the previous one exclusive-ored with the data. Undoing it is
`x ^ (x >> 1)`. The first bit sent is a zero, so the burst starts with a falling
edge from the idle level and can be found at all.

The 16 bits are twelve of payload and four of checksum, and the checksum is the
complement of the one on an outgoing frame. Every single-bit error is caught:
the check is the exclusive-or of the four nibbles, and a flipped bit changes
exactly one of them.

The payload is a period, not a speed:

    period_us = mantissa (9 bits) << exponent (3 bits)
    electrical rpm = 60,000,000 / period_us

A payload of 0x0FFF means the motor is not turning.

### The reply is sampled, not timed

The coprocessor samples the line at five times the reply's bit rate and decodes
in software. The ESC's bit rate comes from its own crystal, a percent or two
from the bench's, and 21 bits is long enough for that to walk a fixed sample
point off the end of a bit — so the decoder resets its phase at every
transition, the way a UART (universal asynchronous receiver-transmitter)
resynchronises on a start bit, and reads each bit from the middle of its window.
`test_dshot_telem` holds this against a reply laid down at 4.75 and 5.25 samples
a bit.

### Pole count

An ESC reports electrical periods and has no idea what it is bolted to, so
mechanical rpm (revolutions per minute) needs the motor's magnet count. That is
the one number the wire does not carry. The panel sends it from the `Motor
poles` setting when the coprocessor answers, on the [CONTROL
page](Link.md#page-map).

Until it arrives the coprocessor reports no speed at all. A speed derived from a
guessed pole count is a plausible number with nothing to mark it as wrong, which
is worse than an empty field.

### Extended telemetry

An ESC sent command 13 interleaves temperature, voltage, current, stress and
status frames between the speed ones, marked by the top nibble of the payload.

The bench does not send that command, so every reply is read as a period. The
two cannot be told apart from the bits alone: the nibble that marks an extended
frame is an ordinary exponent and mantissa in a speed frame, and only an ESC
with extended telemetry enabled guarantees the normalisation that separates
them. The decoder therefore takes the mode as an argument rather than inferring
it.

## What has not been confirmed on a wire

The implementation is written from the published description of the protocol.
Everything below is exercised by the host suite against frames the same code
builds, which proves the arithmetic and not the wire. None of it has been put
on an oscilloscope or against an ESC on this bench:

- the reply rate of five quarters of the DShot rate;
- the leading-bit convention of the group code;
- the turnaround delay, and whether 30 µs is what an ESC actually waits;
- the extended-telemetry frame types and their units;
- every bit timing, against a real ESC's tolerance rather than against the
  specification.

## Where the code is

| | |
| --- | --- |
| Frames, group code, checksum, speed, sampler | `shared/dshot/` |
| PPM frame layout | `shared/ppm/` |
| Travel, role, rest, slew, arming, timeout | `shared/outputs/` |
| PIO programs | `firmware/iomcu/src/ppm.pio`, `dshot.pio` |
| Hardware PWM, PPM and DShot backends | `firmware/iomcu/src/out_*.c` |
| Binding the bank to the pins | `firmware/iomcu/src/outputs_hw.c` |
