# Bringing up the link

<sub>**English** · [Deutsch](Bringup-de.md)</sub>

How to verify that frames cross the CAN (Controller Area Network) bus between
the two boards, and how to read the report. The wiring is in [The
link](Link.md).

## Echo self-test

The self-test answers one question: do frames cross the bus intact? It uses no
page protocol. The panel sends a probe frame, the IOmcu sends it back,
and the panel compares it byte for byte. If the test passes and the link does
not work, the fault is above the wire.

### IOmcu

Nothing to configure. The IOmcu starts CAN at boot, prints whether the
controller answered, and echoes probes permanently. The echo costs one register
read per loop and answers only frames addressed to a page the map does not use.

Boot output on the IOmcu's USB (Universal Serial Bus) console, repeated
every 3s:

    rcbench-iomcu: CAN up, 1000000 bit/s, 0 requests served, 0 self-test echoes, tx_err 0 rx_err 0 eflg 0x00

`requests served` counts link page requests the panel sent and this end
answered; `self-test echoes` counts probe frames and is 0 unless the panel's
CAN self-test is running. The two together say which end has stopped:

| Requests | Panel says | Reading |
| --- | --- | --- |
| climbing | `LINK` | working |
| standing still | `NO LINK` | nothing is arriving — the panel has stopped transmitting, or the wiring is open |
| climbing | `NO LINK` | requests arrive and the answers do not get back |

`CAN did not answer on SPI` means the controller did not report configuration
mode after reset. The fault is on SPI (module not fitted, wiring on GP8 to
GP12), not on the CAN bus.

### Panel

The test runs at every start-up. It takes 1200ms, inside the splash and
before the identity poll: a broken bus is then diagnosed as a broken bus
rather than as an identity that never answered.

A verdict that is not `every probe came back intact` puts the panel on the
[bus-fault screen](#the-bus-fault-screen) instead of the menu. Nothing has to
be enabled and no console is needed.

The console carries the detail. Watch the UART (universal asynchronous
receiver-transmitter) socket, not the native USB socket. GPIO19 and GPIO20
(general-purpose input/output pins 19 and 20) carry both native USB and the
CAN transceiver, and the multiplexer selects one; the console is on UART0 with
USB-Serial-JTAG (the ESP32-S3's built-in USB serial and debug bridge) as
secondary. It prints:

    I (…) can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
    I (…) rcbench:   round trip min 334 max 1356 us
    I (…) rcbench:   panel  tx_err 0 rx_err 0 bus_err 0
    I (…) rcbench:   iomcu  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

The last line is the IOmcu's own status, requested over the bus before
and after the echo phase. The difference between the two readings is the number
of echoes it sent during the test; the panel compares that with the number it
received.

### Verdicts

| Verdict | Meaning | Check |
| --- | --- | --- |
| `no probe came back` | nothing crosses | CANH/CANL swapped; far end powered; same bit rate at both ends; terminators at both ends |
| `probes come back altered` | frames cross and arrive wrong | sample point or bit timing; a missing terminator reflects |
| `probes cross, and not all of them` | marginal bus | timing, one terminator, or a bus too long for the rate |
| `probes go missing without a bus error` | frames arrived intact and were not read in time | a receive buffer overran; not a wiring fault. Compare with the IOmcu's overflow count |
| `every probe came back intact` | the wire is fine | a remaining fault is above the wire |

The console writes these verdicts. The splash and the bus fault screen show
their own wording: `All test frames returned intact.`, `No test frames
returned.`, `Test frames returned with changed data.`, `Some test frames were
lost.` and `Test frames lost without a bus error.`.

Corruption is reported before loss when both occur, because a marginal bus
produces both and the corruption identifies the cause.

Loss is split by the controller's bus error count. Frames lost with bus errors
were corrupted on the wire (termination, timing, length). Frames lost with zero
bus errors arrived intact and were dropped by a receiver that did not read in
time.

If the transmit error counter reaches 128 during the test, no other node is
acknowledging: the IOmcu is not on the bus at all. The test reports this
once, before the run is over.

### The bus-fault screen

A verdict other than `every probe came back intact` is shown on the panel
before the menu, because the fault is invisible from every other screen: a bus
that does not carry frames looks exactly like an IOmcu that is not
fitted, and both look like a bench that shows no numbers.

![Frames cross and arrive changed](img/busfault.png)

The verdict is the heading, the list under it is what to check in the order
that costs least to check, and the right-hand column is what both ends
counted. `returned` is green when anything came back at all; `BUS OFF` means
this panel has stopped transmitting.

![Nothing came back](img/busfault-silent.png)

Leaving it takes a two-second hold on the button across the bottom, the same
gesture and the same fade as ARM. A screen that says the bench cannot be
trusted should not be dismissible by a touch that could have been a sleeve.

Acknowledging does not repair anything and does not disable the test: the
bench runs in simulation, nothing drives an output, and the test runs again at
the next start-up.

The probe payloads cycle through all-dominant, all-recessive and both
alternating patterns, because CAN stuffs a complementary bit after five equal
bits and long runs of one level are what a marginal bus fails on.

## Link report

After bring-up the panel polls the IOmcu's identity page every second
until it answers, then polls the bench page at 20Hz and the status page every
500ms. Every 5s while the link is down, and once a minute while it is up, the
panel prints a diagnosis:

    I (…) rcbench: LINK works, and not every time
    W (…) rcbench:   check: marginal timing, or a poll period tighter than the round trip
    I (…) rcbench:   panel  polls 1200 replies 1187 timeouts 13 stale 0 nack 0 crc 0 resync 0
    I (…) rcbench:   iomcu  frames 1187 crc 0 resync 0 refused 0
    I (…) rcbench:   round trip min 620 avg 700 max 1400 us

| Diagnosis | Meaning |
| --- | --- |
| `no reply to any poll` | IOmcu powered; CANH/CANL; bit rate; terminators |
| `answering, wrong protocol` | flash both ends from the same tree |
| `requests land, answers do not` | the far end hears the panel and the panel does not hear it: its transmit path |
| `frames arrive corrupt` | bit timing or sample point disagreeing between the two ends |
| `answers arrive too late` | a reply slower than the poll timeout, or a stalled far end |
| `works, and not every time` | marginal timing, or a poll period tighter than the round trip |

`nack` in the panel line counts requests the IOmcu answered with a NACK
(negative acknowledge). A NACK is an answer: the link works, and the page
kept what it held ([Frames](Link.md#frames)). A set-up from SETUP
INTERFACES that a page refuses is one each: one write per refused value,
not written again until the value changes, so stepping through rows on SETUP
counts one NACK for each value the IOmcu refuses -- Tap gap 2ms with Tap
lowest tone at 400Hz, or PD mini RX on the sensor bus's SCL -- and none for
the values it takes. A pin refused because something else holds it is
offered again every 5s and counts one each time. `timeouts`, `stale` and
`crc` are the columns that count a link fault.

The `crc` and `resync` columns of the IOmcu line carry the XL2515's
receive and transmit error counters (status registers `LINK_ST_CRC_ERRORS` and
`LINK_ST_RESYNCS`). `refused` is the count of CAN frames the IOmcu
received and refused to decode since its power-up (`LINK_ST_REFUSED_LO` and
`LINK_ST_REFUSED_HI`, protocol 4.21; 0 from an older IOmcu). See
[Refused frames](Link.md#refused-frames).

## Why the IOmcu last started

At start-up and every 3s after it, the IOmcu's console names why the IOmcu
last started, with every raw value:

    rcbench-iomcu: reset FLASH_UPDATE (chip_reset 0x00010000 reason 0x1 boot 0x010104FF params 0x101FD000 0x00000000 scratch 0x101FD000 0x00000000)

The start-up line leaves before a host has the USB port open in most
cases; the 3s line repeats it. `chip_reset` is POWMAN CHIP_RESET, `reason`
WATCHDOG REASON, `boot` the boot ROM's boot word (0xttppbbdd: TBYB flags,
partition, boot type, diagnostic partition; `boot none` when the boot ROM
gave none), `params` its reboot parameters, `scratch` WATCHDOG SCRATCH2 and
SCRATCH3. The firmware reads them once at boot and then zeroes SCRATCH2 and
SCRATCH3. The longest line is 153 bytes with its LF.

CHIP_RESET changes only at a chip-level reset, and its bits are read-only
(RP2350 datasheet, sections 7.3 and 7.4, table 489). The boot ROM's reboot
resets the system through the power-on state machine, not the chip: after
an update over CAN, CHIP_RESET still reads HAD_POR from the power-up, as in
the line above. A chip-level reset resets the watchdog, so `reason 0x0`
means CHIP_RESET belongs to this start. `reset_cause_decode()` in
`shared/safety/reset_cause.c` picks the word; the first row that matches
wins:

| Word | Registers | Cause |
| --- | --- | --- |
| `FLASH_UPDATE` | boot type 4 | the update over CAN, a UF2 copied to the BOOTSEL drive, `picotool load -x` |
| `RAM_IMAGE` | boot type 3 | an image loaded into SRAM |
| `BOOTSEL_REBOOT` | boot type 2; or reason non-zero, boot type 0, scratch 0xFFFFFFFF 0x00000000 | back from BOOTSEL through `picotool reboot`, which picotool also sends after every command run with `-f`. The way into BOOTSEL is the 1200 baud request or picotool's reset request over USB |
| `SW_RESET` | reason 0x2 | the watchdog triggered at once by software |
| `WATCHDOG` | reason 0x1 | the watchdog counted down: the boot ROM's 16.7s TBYB (try before you buy) timer after an update the panel did not buy, or a reboot with other parameters |
| `POR` | reason 0, HAD_POR (bit 16) | power applied |
| `BOR` | reason 0, HAD_BOR (bit 17) | brown-out detector |
| `RUN_PIN` | reason 0, HAD_RUN_LOW (bit 18) | RUN pin held low: the reset button, or a host pulling RUN |
| `GLITCH` | reason 0, HAD_GLITCH_DETECT (bit 26) | supply glitch detector |
| `DEBUGGER` | reason 0, HAD_DP_RESET_REQ, HAD_RESCUE, HAD_HZD_SYS_RESET_REQ (bits 19, 21, 27) | a debugger reset |
| `POWER_DOWN` | reason 0, HAD_SWCORE_PD (bit 25) | switched-core power-down |
| `WATCHDOG` | reason 0, a HAD_WATCHDOG_* bit (22, 23, 24, 28) | a watchdog that POWMAN WDSEL lets reset the chip; this firmware sets none |
| `UNKNOWN` | none of the above | no cause recorded |

The boot ROM's reboot runs the watchdog in timer mode, so every reboot it
performs reads `reason 0x1`. A debugger's warm reset of a core clears
REASON without a chip-level reset, and the start after it reads as the
chip-level reset before. The `BOOTSEL_REBOOT` and `WATCHDOG` rows follow the
source of the RP2350 boot ROM and of picotool 2.x.

## What to record

The round trip, both ends' error counters, and the IOmcu's overflow
count. The round trip sets what a poll period has to clear; the counters say
whether the bus or the software was the limit.

With both boards powered, also confirm on a scope that GPIO28 (`PANEL_HEARTBEAT_PIN`) edges at the
rate [Safety](Safety.md) specifies. GPIO28 is IO28 of the 12-pin header P3.
