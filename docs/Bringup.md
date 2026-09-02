# Bringing up the link

<sub>**English** · [Deutsch](Bringup-de.md)</sub>

How to verify that frames cross the CAN (Controller Area Network) bus between
the two boards, and how to read the report. The wiring is in [The
link](Link.md).

## Echo self-test

The self-test answers one question: do frames cross the bus intact? It uses no
page protocol. The panel sends a probe frame, the coprocessor sends it back,
and the panel compares it byte for byte. If the test passes and the link does
not work, the fault is above the wire.

### Coprocessor

Nothing to configure. The coprocessor starts CAN at boot, prints whether the
controller answered, and echoes probes permanently. The echo costs one register
read per loop and answers only frames addressed to a page the map does not use.

Boot output on the coprocessor's USB (Universal Serial Bus) console, repeated
every 3 s:

    rcbench-iomcu: CAN up, 1000000 bit/s, 0 echoes served, tx_err 0 rx_err 0 eflg 0x00

`CAN did not answer on SPI` means the controller did not report configuration
mode after reset. The fault is on SPI (module not fitted, wiring on GP8 to
GP12), not on the CAN bus.

### Panel

The test is opt-in, because starting CAN removes the panel's native USB:

```bash
cd firmware/panel
idf.py -DRCBENCH_CAN_SELFTEST=1 build flash
```

Watch the UART (universal asynchronous receiver-transmitter) socket, not the
native USB socket. GPIO19 and GPIO20 (general-purpose input/output pins 19 and 20) carry both native USB and the CAN
transceiver, and the multiplexer selects one; the console is on UART0 with
USB-Serial-JTAG (the ESP32-S3's built-in USB serial and debug bridge) as
secondary.

The test runs for 5 s at boot, before the identity poll, and prints:

    I (…) can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
    I (…) rcbench:   round trip min 334 max 1356 us
    I (…) rcbench:   panel  tx_err 0 rx_err 0 bus_err 0
    I (…) rcbench:   iomcu  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

The last line is the coprocessor's own status, requested over the bus before
and after the echo phase. The difference between the two readings is the number
of echoes it sent during the test; the panel compares that with the number it
received.

### Verdicts

| Verdict | Meaning | Check |
| --- | --- | --- |
| `no probe came back` | nothing crosses | CANH/CANL swapped; far end powered; same bit rate at both ends; terminators at both ends |
| `probes come back altered` | frames cross and arrive wrong | sample point or bit timing; a missing terminator reflects |
| `probes cross, and not all of them` | marginal bus | timing, one terminator, or a bus too long for the rate |
| `probes go missing without a bus error` | frames arrived intact and were not read in time | a receive buffer overran; not a wiring fault. Compare with the coprocessor's overflow count |
| `every probe came back intact` | the wire is fine | a remaining fault is above the wire |

Corruption is reported before loss when both occur, because a marginal bus
produces both and the corruption identifies the cause.

Loss is split by the controller's bus error count. Frames lost with bus errors
were corrupted on the wire (termination, timing, length). Frames lost with zero
bus errors arrived intact and were dropped by a receiver that did not read in
time.

If the transmit error counter reaches 128 during the test, no other node is
acknowledging: the coprocessor is not on the bus at all. The test reports this
once, before the five seconds are over.

The probe payloads cycle through all-dominant, all-recessive and both
alternating patterns, because CAN stuffs a complementary bit after five equal
bits and long runs of one level are what a marginal bus fails on.

## Link report

After bring-up the panel polls the coprocessor's identity page every second
until it answers, then polls the bench page at 20 Hz and the status page at 1
Hz. Every 5 s while the link is down, and once a minute while it is up, the
panel prints a diagnosis:

    I (…) rcbench: LINK works, and not every time
    W (…) rcbench:   check: marginal timing, or a poll period tighter than the round trip
    I (…) rcbench:   panel  polls 1200 replies 1187 timeouts 13 stale 0 nack 0 crc 0 resync 0
    I (…) rcbench:   iomcu  frames 1187 crc 0 resync 0
    I (…) rcbench:   round trip min 620 avg 700 max 1400 us

| Diagnosis | Meaning |
| --- | --- |
| `no reply to any poll` | coprocessor powered; CANH/CANL; bit rate; terminators |
| `answering, wrong protocol` | flash both ends from the same tree |
| `requests land, answers do not` | the far end hears the panel and the panel does not hear it: its transmit path |
| `frames arrive corrupt` | bit timing or sample point disagreeing between the two ends |
| `answers arrive too late` | a reply slower than the poll timeout, or a stalled far end |
| `works, and not every time` | marginal timing, or a poll period tighter than the round trip |

The `crc` and `resync` columns of the coprocessor line carry the XL2515's
receive and transmit error counters (status registers `LINK_ST_CRC_ERRORS` and
`LINK_ST_RESYNCS`).

## What to record

The round trip, both ends' error counters, and the coprocessor's overflow
count. The round trip sets what a poll period has to clear; the counters say
whether the bus or the software was the limit.

With both boards powered, also confirm on a scope that GPIO6 on J8 edges at the
rate [Safety](Safety.md) specifies.
